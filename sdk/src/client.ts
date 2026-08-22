/**
 * NuWallet 고수준 클라이언트.
 *
 * 프로토콜 인코딩은 protocol.ts, BLE 는 transport.ts 가 맡는다.
 * 여기서는 명령을 의미 있는 API 로 감싸고, 서명 승인의 비동기 흐름을 다룬다.
 */
import {
  CMD, EVT, SW, WalletError, DEFAULT_PATH,
  encodePath, concat, hex, fromHex,
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
    const p = await this.cmd(CMD.SETUP_GENERATE, new Uint8Array([strength]));
    return decodeWords(p);
  }

  /** 사용자가 백업했음을 확인하고 저장한다. 첫 주소를 돌려준다. */
  async confirmSetup(words: number[]): Promise<string> {
    const p = await this.cmd(CMD.SETUP_CONFIRM, encodeWords(words));
    return toChecksumAddress(hex(p.subarray(0, 20)));
  }

  /** 기존 니모닉으로 복구한다. BIP-39 체크섬을 기기가 검증한다. */
  async restore(words: number[]): Promise<string> {
    const p = await this.cmd(CMD.SETUP_RESTORE, encodeWords(words));
    return toChecksumAddress(hex(p.subarray(0, 20)));
  }

  /** 지갑을 지운다. 기기에서 버튼 승인이 필요하다. */
  async wipe(opts: SignOptions = {}): Promise<void> {
    await this.pendingCommand(CMD.WIPE, new Uint8Array(), opts);
  }

  // ── 계정 ─────────────────────────────────────────────────────────────────

  async getAccount(path: string = DEFAULT_PATH): Promise<AccountInfo> {
    const p = await this.cmd(CMD.GET_ADDRESS, encodePath(path));
    if (p.length < 117) throw new WalletError(SW.DEVICE_ERROR, '짧은 응답');
    return {
      address:   toChecksumAddress(hex(p.subarray(0, 20))),
      publicKey: hex(p.subarray(20, 85)),
      chainCode: hex(p.subarray(85, 117)),
      path,
    };
  }

  async getAddress(path: string = DEFAULT_PATH): Promise<string> {
    return (await this.getAccount(path)).address;
  }

  // ── 서명 ─────────────────────────────────────────────────────────────────

  /**
   * 서명되지 않은 트랜잭션에 서명한다.
   * @param unsignedTx  legacy 는 RLP([...,chainId,0,0]), typed 는 0x02‖RLP([...])
   *                    **기기가 직접 Keccak-256 을 계산한다.** 해시를 보내지 않는다.
   * @param chainId     EIP-155 v 계산에 쓴다. typed 트랜잭션은 생략한다.
   */
  async signTransaction(path: string, unsignedTx: Uint8Array | string,
                        chainId?: number, opts: SignOptions = {}): Promise<Signature> {
    const tx = typeof unsignedTx === 'string' ? fromHex(unsignedTx) : unsignedTx;
    const typed = tx.length > 0 && tx[0]! >= 0x01 && tx[0]! <= 0x7f;
    const raw = await this.pendingCommand(CMD.SIGN_TX, concat(encodePath(path), tx), opts);
    return makeSignature(raw, typed ? 'typed' : 'eip155', chainId);
  }

  /** personal_sign (EIP-191). 기기가 접두사를 붙여 해시한다. */
  async signMessage(path: string, message: Uint8Array | string,
                    opts: SignOptions = {}): Promise<Signature> {
    const msg = typeof message === 'string' ? new TextEncoder().encode(message) : message;
    const raw = await this.pendingCommand(CMD.SIGN_PERSONAL, concat(encodePath(path), msg), opts);
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
    const raw = await this.pendingCommand(CMD.SIGN_TYPED, concat(encodePath(path), d, m), opts);
    return makeSignature(raw, 'legacy');
  }

  /**
   * PENDING 을 받고 이벤트로 결과를 기다리는 명령의 공통 흐름.
   * docs/protocol.md §7 참고.
   */
  private async pendingCommand(cmd: number, payload: Uint8Array,
                               opts: SignOptions): Promise<Uint8Array> {
    const first = await this.transport.send(cmd, payload);
    if (first.status !== SW.PENDING) throw new WalletError(first.status);
    if (first.payload.length < 4) throw new WalletError(SW.DEVICE_ERROR, 'requestId 없음');
    const dv = new DataView(first.payload.buffer, first.payload.byteOffset, first.payload.byteLength);
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
          opts.onStart?.({ requestId, steps: p[4]!, command: p[5]! });
        } else if (e.evt === EVT.CHALLENGE_PROGRESS && p.length >= 6) {
          opts.onProgress?.({ requestId, step: p[4]!, attemptsLeft: p[5]! });
        } else if (e.evt === EVT.SIGN_RESULT && p.length >= 6) {
          const status = edv.getUint16(4, false);
          if (status !== SW.OK) finish(() => reject(new WalletError(status)));
          else finish(() => resolve(p.subarray(6)));
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

type VMode = 'legacy' | 'eip155' | 'typed';

/**
 * 기기가 준 r ‖ s ‖ recid 를 Ethereum 서명으로 만든다.
 * v 계산 규칙은 docs/protocol.md §6 참고.
 */
function makeSignature(raw: Uint8Array, mode: VMode, chainId?: number): Signature {
  if (raw.length < 65) throw new WalletError(SW.DEVICE_ERROR, '서명 길이가 65바이트가 아닙니다');
  const r = raw.subarray(0, 32);
  const s = raw.subarray(32, 64);
  const recid = raw[64]!;

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
