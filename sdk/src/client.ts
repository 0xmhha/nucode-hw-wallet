/**
 * NuWallet 고수준 클라이언트.
 *
 * 프로토콜 인코딩은 protocol.ts, BLE 는 transport.ts 가 맡는다.
 * 여기서는 명령을 의미 있는 API 로 감싸고, 서명 승인의 비동기 흐름을 다룬다.
 */
import {
  CMD, EVT, SW, WalletError, DEFAULT_PATHS,
  encodeChainPath, concat, hex, fromHex, type Chain,
} from './protocol.js';
import { BleTransport, type TransportOptions } from './transport.js';
import { toChecksumAddress } from './address.js';
import type {
  DeviceInfo, DeviceState, AccountInfo, Signature, SignOptions,
} from './types.js';

const DEFAULT_SIGN_TIMEOUT = 70_000;   // 기기 챌린지 제한 60초 + 여유

export class NuWallet {
  readonly transport: BleTransport;

  constructor(opts: TransportOptions = {}) {
    this.transport = new BleTransport(opts);
  }

  static get isSupported(): boolean { return BleTransport.isSupported; }
  get isConnected(): boolean { return this.transport.isConnected; }
  get deviceName(): string { return this.transport.deviceName; }

  connect(): Promise<void> { return this.transport.connect(); }
  disconnect(): Promise<void> { return this.transport.disconnect(); }
  onDisconnect(h: () => void): () => void { return this.transport.onDisconnect(h); }

  private async cmd(c: number, payload?: Uint8Array, timeoutMs?: number): Promise<Uint8Array> {
    const r = await this.transport.send(c, payload, timeoutMs);
    if (r.status !== SW.OK) throw new WalletError(r.status);
    return r.payload;
  }

  // ── 기기 정보 ─────────────────────────────────────────────────────────────

  async getInfo(): Promise<DeviceInfo> {
    const p = await this.cmd(CMD.GET_VERSION);
    if (p.length < 4) throw new WalletError(SW.DEVICE_ERROR, '짧은 응답');
    return {
      protocolVersion: p[0]!,
      firmware: `${p[1]}.${p[2]}`,
      initialized: (p[3]! & 0x01) !== 0,
      locked:      (p[3]! & 0x02) !== 0,
      name: new TextDecoder().decode(p.subarray(4)),
    };
  }

  async getState(): Promise<DeviceState> {
    const p = await this.cmd(CMD.GET_STATE);
    const dv = new DataView(p.buffer, p.byteOffset, p.byteLength);
    return {
      initialized:     (p[0]! & 0x01) !== 0,
      locked:          (p[0]! & 0x02) !== 0,
      challengeActive: (p[0]! & 0x04) !== 0,
      requestId: p.length >= 5 ? dv.getUint32(1, false) : 0,
    };
  }

  // ── 셋업 ─────────────────────────────────────────────────────────────────

  /**
   * 새 니모닉을 생성한다. **아직 저장되지 않는다** — confirmSetup 이 와야 확정된다.
   *
   * ⚠️  니모닉이 BLE 로 평문 전송된다. 기기에 화면이 없어 다른 방법이 없다.
   *     호스트 PC 가 니모닉을 보게 되므로, 감염된 PC 에서는 안전하지 않다.
   *     SECURITY.md §3 참고.
   */
  async generateMnemonic(strength: 128 | 256 = 128): Promise<number[]> {
    const p = await this.cmd(
      CMD.SETUP_GENERATE, new Uint8Array([strength === 256 ? 24 : 12]));
    return decodeWords(p);
  }

  /**
   * 사용자가 백업했음을 확인하고 저장한다.
   *
   * v2 부터 여기서 바로 저장되지 않는다. 기기가 **PIN 설정 절차**를 시작하고,
   * 사용자가 버튼으로 6자리를 두 번 누른 뒤에야 봉인·저장된다. PIN 없이 저장하면
   * 봉인 키가 공개값이 되어 플래시만 뜨면 열리기 때문이다.
   *
   * 끝나면 지갑은 잠금 해제 상태이고, 주소는 이어서 조회해 돌려준다.
   */
  async confirmSetup(words: number[], opts: SignOptions = {}): Promise<string> {
    await this.setupCommand(CMD.SETUP_CONFIRM, encodeWords(words), opts);
    return this.getAddress('ethereum');
  }

  /** 기존 니모닉으로 복구한다. BIP-39 체크섬을 기기가 검증한다.
   *  confirmSetup 과 마찬가지로 PIN 설정을 거친다. */
  async restore(words: number[], opts: SignOptions = {}): Promise<string> {
    await this.setupCommand(CMD.SETUP_RESTORE, encodeWords(words), opts);
    return this.getAddress('ethereum');
  }

  /** 지갑을 지운다. 기기에서 버튼 승인이 필요하다. */
  async wipe(opts: SignOptions = {}): Promise<void> {
    await this.pendingCommand(CMD.WIPE, new Uint8Array(), opts);
  }

  // ── 계정 ─────────────────────────────────────────────────────────────────

  /**
   * 주소와 공개키를 읽는다. docs/protocol.md §5 `0x20 GET_ADDRESS`.
   * 응답은 체인마다 길이가 달라서 길이 접두사로 온다.
   */
  async getAccount(chain: Chain = 'ethereum',
                   path: string = DEFAULT_PATHS[chain]): Promise<AccountInfo> {
    const p = await this.cmd(CMD.GET_ADDRESS, encodeChainPath(chain, path));
    // ADDR_LEN(1) ‖ ADDRESS ‖ PUBKEY_LEN(1) ‖ PUBKEY — 체인마다 길이가 다르다.
    if (p.length < 2) throw new WalletError(SW.DEVICE_ERROR, '주소 응답이 짧습니다');
    const alen = p[0]!;
    if (p.length < 2 + alen) throw new WalletError(SW.DEVICE_ERROR, '주소 응답 길이 불일치');
    const addr = p.subarray(1, 1 + alen);
    const plen = p[1 + alen]!;
    if (p.length < 2 + alen + plen) throw new WalletError(SW.DEVICE_ERROR, '공개키 응답 길이 불일치');
    const pub = p.subarray(2 + alen, 2 + alen + plen);

    return {
      chain,
      path,
      address: chain === 'ethereum' ? toChecksumAddress(hex(addr)) : base58(addr),
      publicKey: hex(pub),
    };
  }

  /**
   * 주소만 필요할 때. `0x21 GET_CHAIN_ADDRESS` 는 공개키를 싣지 않아 응답이
   * 짧다 — 연결 직후처럼 자주 부르는 경로에서 쓴다.
   */
  async getAddress(chain: Chain = 'ethereum',
                   path: string = DEFAULT_PATHS[chain]): Promise<string> {
    const p = await this.cmd(CMD.GET_CHAIN_ADDRESS, encodeChainPath(chain, path));
    const expected = chain === 'ethereum' ? 20 : 32;
    if (p.length !== expected) throw new WalletError(SW.DEVICE_ERROR, '주소 응답 길이 불일치');
    return chain === 'ethereum' ? toChecksumAddress(hex(p)) : base58(p);
  }

  // ── 서명 ─────────────────────────────────────────────────────────────────

  /**
   * 서명되지 않은 트랜잭션에 서명한다.
   * @param unsignedTx  legacy 는 RLP([...,chainId,0,0]), typed 는 0x02‖RLP([...])
   *                    **기기가 직접 Keccak-256 을 계산한다.** 해시를 보내지 않는다.
   * @param chainId     EIP-155 v 계산에 쓴다. typed 트랜잭션은 생략한다.
   */
  async signTransaction(chain: Chain, path: string, unsignedTx: Uint8Array | string,
                        chainId?: number, opts: SignOptions = {}): Promise<Signature> {
    const tx = typeof unsignedTx === 'string' ? fromHex(unsignedTx) : unsignedTx;
    const raw = await this.pendingCommand(
      chain === 'solana' ? CMD.SIGN_SOLANA : CMD.SIGN_TX,
      concat(encodeChainPath(chain, path), tx), opts);
    if (chain === 'solana') {
      const sig = takeSig(raw, 64);
      return { raw: hex(sig), serialized: hex(sig) } as Signature;
    }
    const typed = tx.length > 0 && tx[0]! >= 0x01 && tx[0]! <= 0x7f;
    return makeSignature(raw, typed ? 'typed' : 'eip155', chainId);
  }

  /** personal_sign (EIP-191). 기기가 접두사를 붙여 해시한다. */
  async signMessage(path: string = DEFAULT_PATHS.ethereum, message: Uint8Array | string = '',
                    opts: SignOptions = {}): Promise<Signature> {
    const msg = typeof message === 'string' ? new TextEncoder().encode(message) : message;
    const raw = await this.pendingCommand(
      CMD.SIGN_PERSONAL, concat(encodeChainPath('ethereum', path), msg), opts);
    return makeSignature(raw, 'legacy');
  }

  /** EIP-712. 구조체 인코딩은 호출자가 하고 두 해시만 넘긴다. */
  async signTypedHashes(path: string, domainSeparator: Uint8Array | string,
                        messageHash: Uint8Array | string,
                        opts: SignOptions = {}): Promise<Signature> {
    const d = typeof domainSeparator === 'string' ? fromHex(domainSeparator) : domainSeparator;
    const m = typeof messageHash === 'string' ? fromHex(messageHash) : messageHash;
    if (d.length !== 32 || m.length !== 32) {
      throw new WalletError(SW.BAD_PARAM, 'domainSeparator 와 messageHash 는 각각 32바이트여야 합니다');
    }
    const raw = await this.pendingCommand(
      CMD.SIGN_TYPED, concat(encodeChainPath('ethereum', path), d, m), opts);
    return makeSignature(raw, 'legacy');
  }

  /**
   * 셋업 계열. 기기가 PENDING 을 주고, 사용자가 버튼으로 PIN 을 정한 뒤에야
   * 봉인·저장된다.
   *
   * v1 펌웨어는 이 자리에서 OK 를 주고 **PIN 없이** 저장했다. 그 레코드는
   * 봉인 키가 `PBKDF2("", salt)` 이고 salt 가 평문이라 플래시만 뜨면 열린다.
   * 조용히 받아주면 사용자는 잠긴 줄 알지만 잠기지 않은 지갑을 갖게 되므로,
   * 분명한 오류를 낸다.
   */
  private async setupCommand(cmd: number, payload: Uint8Array,
                             opts: SignOptions): Promise<void> {
    const first = await this.transport.send(cmd, payload);
    if (first.status === SW.OK) {
      throw new WalletError(SW.DEVICE_ERROR,
        '펌웨어가 낡았습니다 (프로토콜 v1). 이 펌웨어는 PIN 없이 지갑을 저장하며, ' +
        '그 저장은 암호화된 것이 아닙니다. 보드에 최신 펌웨어를 올린 뒤 다시 하세요.');
    }
    if (first.status !== SW.PENDING) throw new WalletError(first.status);
    await this.awaitApproval(first.payload, opts);
  }

  /**
   * PENDING 을 받고 이벤트로 결과를 기다리는 명령의 공통 흐름.
   * docs/protocol.md §7 참고.
   */
  private async pendingCommand(cmd: number, payload: Uint8Array,
                               opts: SignOptions): Promise<Uint8Array> {
    const first = await this.transport.send(cmd, payload);
    if (first.status !== SW.PENDING) throw new WalletError(first.status);
    return this.awaitApproval(first.payload, opts);
  }

  /** PENDING 응답을 받은 뒤, 기기가 결과 이벤트를 보낼 때까지 기다린다. */
  private async awaitApproval(firstPayload: Uint8Array,
                              opts: SignOptions): Promise<Uint8Array> {
    if (firstPayload.length < 4) throw new WalletError(SW.DEVICE_ERROR, 'requestId 없음');
    const dv = new DataView(firstPayload.buffer, firstPayload.byteOffset, firstPayload.byteLength);
    const requestId = dv.getUint32(0, false);

    const timeoutMs = opts.timeoutMs ?? DEFAULT_SIGN_TIMEOUT;

    return await new Promise<Uint8Array>((resolve, reject) => {
      let done = false;
      const finish = (fn: () => void) => {
        if (done) return;
        done = true;
        clearTimeout(timer);
        off();
        offDisc();
        opts.signal?.removeEventListener('abort', onAbort);
        fn();
      };
      const onAbort = () => {
        void this.transport.send(CMD.CANCEL, be32(requestId)).catch(() => undefined);
        finish(() => reject(new WalletError(SW.USER_REJECTED, '호출자가 취소했습니다')));
      };
      const timer = setTimeout(() => {
        void this.transport.send(CMD.CANCEL, be32(requestId)).catch(() => undefined);
        finish(() => reject(new WalletError(SW.CHALLENGE_TIMEOUT)));
      }, timeoutMs);

      const off = this.transport.onEvent((e) => {
        const p = e.payload;
        if (p.length < 4) return;
        const edv = new DataView(p.buffer, p.byteOffset, p.byteLength);
        if (edv.getUint32(0, false) !== requestId) return;

        if (e.evt === EVT.CHALLENGE_STARTED && p.length >= 6) {
          opts.onStart?.({
            requestId, steps: p[4]!, command: p[5]!,
            kind: p.length >= 7 ? p[6]! : 0,
          });
        } else if (e.evt === EVT.CHALLENGE_PROGRESS && p.length >= 6) {
          opts.onProgress?.({ requestId, step: p[4]!, attemptsLeft: p[5]! });
        } else if (e.evt === EVT.SIGN_RESULT && p.length >= 6) {
          const status = edv.getUint16(4, false);
          if (status !== SW.OK) finish(() => reject(new WalletError(status)));
          else finish(() => resolve(p.subarray(6)));
        } else if (e.evt === EVT.REQUEST_RESULT && p.length >= 7) {
          /* 서명이 아닌 승인(셋업·PIN 변경·WIPE)의 결과. 페이로드는 없다. */
          const status = edv.getUint16(5, false);
          if (status !== SW.OK) finish(() => reject(new WalletError(status)));
          else finish(() => resolve(new Uint8Array()));
        } else if (e.evt === EVT.REQUEST_RESULT && p.length >= 7) {
          // WIPE 처럼 서명이 아닌 승인 요청의 결과. 돌려줄 페이로드가 없다.
          // 이걸 안 보면 WIPE 가 영원히 안 끝난다 — docs/protocol.md §6.
          const status = edv.getUint16(5, false);
          if (status !== SW.OK) finish(() => reject(new WalletError(status)));
          else finish(() => resolve(new Uint8Array()));
        }
      });
      const offDisc = this.transport.onDisconnect(() => {
        finish(() => reject(new WalletError(SW.DEVICE_ERROR, '기기 연결이 끊겼습니다')));
      });
      opts.signal?.addEventListener('abort', onAbort);
    });
  }
}

// ── 내부 헬퍼 ────────────────────────────────────────────────────────────────

function be32(v: number): Uint8Array {
  const b = new Uint8Array(4);
  new DataView(b.buffer).setUint32(0, v >>> 0, false);
  return b;
}

function encodeWords(words: number[]): Uint8Array {
  const out = new Uint8Array(1 + words.length * 2);
  out[0] = words.length;
  const dv = new DataView(out.buffer);
  words.forEach((w, i) => dv.setUint16(1 + i * 2, w, false));
  return out;
}

function decodeWords(p: Uint8Array): number[] {
  const n = p[0] ?? 0;
  const dv = new DataView(p.buffer, p.byteOffset, p.byteLength);
  const out: number[] = [];
  for (let i = 0; i < n; i++) out.push(dv.getUint16(1 + i * 2, false));
  return out;
}

/** SIG_LEN ‖ SIGNATURE 에서 서명만 꺼낸다. */
function takeSig(raw: Uint8Array, expectedLength: number): Uint8Array {
  /* 초기 Arduino 펌웨어는 SIG_LEN 없이 서명만 보냈다. 전체 길이가 체인별
   * 고정 길이와 정확히 같을 때만 레거시 응답으로 인정한다. */
  if (raw.length === expectedLength) return raw;
  if (raw.length < 1) throw new WalletError(SW.DEVICE_ERROR, '서명이 비어 있습니다');
  const n = raw[0]!;
  if (n !== expectedLength || raw.length !== 1 + n) {
    throw new WalletError(SW.DEVICE_ERROR,
      `서명 길이가 맞지 않습니다 (SIG_LEN=${n}, 실제 ${raw.length - 1})`);
  }
  return raw.subarray(1, 1 + n);
}

type VMode = 'legacy' | 'eip155' | 'typed';

/**
 * 기기가 준 r ‖ s ‖ recid 를 Ethereum 서명으로 만든다.
 * v 계산 규칙은 docs/protocol.md §6 참고.
 */
function makeSignature(raw: Uint8Array, mode: VMode, chainId?: number): Signature {
  /* docs/protocol.md §6: SIG_LEN(1) ‖ SIGNATURE
   *
   * 길이 접두사를 "있으면 쓰고 없으면 만다" 식으로 추측하면 안 된다.
   * r 의 첫 바이트가 우연히 64 인 서명이 256개 중 하나꼴로 나오는데, 그때
   * 한 바이트를 잘라내고 엉뚱한 서명을 만들어 낸다. */
  const body = takeSig(raw, 65);
  if (body.length !== 65) {
    throw new WalletError(SW.DEVICE_ERROR,
      `Ethereum 서명은 65바이트여야 합니다 (받은 길이 ${body.length})`);
  }
  const r = body.subarray(0, 32);
  const s = body.subarray(32, 64);
  const recid = body[64]!;

  let v: number;
  if (mode === 'typed') v = recid;                       // yParity
  else if (mode === 'eip155') {
    if (chainId === undefined) {
      throw new WalletError(SW.BAD_PARAM, 'EIP-155 서명에는 chainId 가 필요합니다');
    }
    v = recid + chainId * 2 + 35;
  } else v = recid + 27;

  return {
    r: hex(r), s: hex(s), recid, v,
    serialized: hex(concat(r, s, new Uint8Array([v & 0xff]))),
  };
}


/** Solana 주소는 공개키의 base58 이다. 의존성 없이 짧게 구현한다. */
function base58(b: Uint8Array): string {
  const A = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz';
  const digits: number[] = [0];
  for (const byte of b) {
    let carry = byte;
    for (let i = 0; i < digits.length; i++) {
      carry += digits[i]! << 8;
      digits[i] = carry % 58;
      carry = (carry / 58) | 0;
    }
    while (carry) { digits.push(carry % 58); carry = (carry / 58) | 0; }
  }
  let out = '';
  for (const byte of b) { if (byte === 0) out += A[0]; else break; }
  for (let i = digits.length - 1; i >= 0; i--) out += A[digits[i]!];
  return out;
}
