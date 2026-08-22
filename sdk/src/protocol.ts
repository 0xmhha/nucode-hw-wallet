/**
 * NuWallet BLE 프로토콜 v1 — 상수와 코덱.
 *
 * 이 파일은 docs/protocol.md 와 펌웨어의 protocol.h 에 1:1 대응한다.
 * 셋 중 하나를 바꾸면 나머지 둘도 바꿔야 한다.
 */

export const SERVICE_UUID = '6e754000-7761-4c4c-4554-000000000001';
export const RX_UUID      = '6e754000-7761-4c4c-4554-000000000002'; // 호스트 -> 기기
export const TX_UUID      = '6e754000-7761-4c4c-4554-000000000003'; // 기기 -> 호스트

export const PROTOCOL_VERSION = 1;
export const MAX_MESSAGE      = 2048;

export const TAG = { MESSAGE: 0x05, EVENT: 0x06 } as const;

export const CMD = {
  GET_VERSION:     0x01,
  GET_STATE:       0x02,
  SETUP_GENERATE:  0x10,
  SETUP_CONFIRM:   0x11,
  SETUP_RESTORE:   0x12,
  WIPE:            0x13,
  SET_PIN:         0x14,
  UNLOCK:          0x15,
  LOCK:            0x16,
  GET_ADDRESS:     0x20,
  GET_CHAIN_ADDRESS: 0x21,
  SIGN_TX:         0x30,
  SIGN_PERSONAL:   0x31,
  SIGN_TYPED:      0x32,
  SIGN_SOLANA:     0x33,
  GET_RESULT:      0x40,
  CANCEL:          0x41,
} as const;

export const EVT = {
  CHALLENGE_STARTED:  0xa0,
  CHALLENGE_PROGRESS: 0xa1,
  SIGN_RESULT:        0xa2,
  DEVICE_STATE:       0xa3,
  REQUEST_RESULT:     0xa4,
} as const;

export const SW = {
  OK:                  0x9000,
  PENDING:             0x9001,
  LOCKED:              0x5515,
  CHALLENGE_TIMEOUT:   0x6501,
  CHALLENGE_FAILED:    0x6502,
  PIN_REQUIRED:        0x6503,
  NOT_INITIALIZED:     0x6982,
  ALREADY_INITIALIZED: 0x6983,
  USER_REJECTED:       0x6985,
  BAD_PARAM:           0x6a80,
  UNSUPPORTED_CHAIN:   0x6a81,
  TOO_LARGE:           0x6a84,
  UNKNOWN_CMD:         0x6d00,
  DEVICE_ERROR:        0x6f00,
  FRAMING_ERROR:       0x6f01,
} as const;

const SW_TEXT: Record<number, string> = {
  [SW.OK]: '성공',
  [SW.PENDING]: '접수됨 — 기기에서 승인을 기다리는 중',
  [SW.LOCKED]: '기기가 잠겨 있습니다',
  [SW.CHALLENGE_TIMEOUT]: '승인 시간이 초과되었습니다',
  [SW.CHALLENGE_FAILED]: '버튼 시퀀스가 틀렸습니다',
  [SW.PIN_REQUIRED]: 'PIN 이 설정되어 있습니다. 먼저 잠금을 해제하세요',
  [SW.NOT_INITIALIZED]: '지갑이 아직 생성되지 않았습니다',
  [SW.ALREADY_INITIALIZED]: '이미 지갑이 있습니다. 먼저 초기화하세요',
  [SW.USER_REJECTED]: '사용자가 거부했습니다',
  [SW.BAD_PARAM]: '요청 형식이 잘못되었습니다',
  [SW.UNSUPPORTED_CHAIN]: '기기가 지원하지 않는 체인입니다',
  [SW.TOO_LARGE]: '메시지가 너무 큽니다',
  [SW.UNKNOWN_CMD]: '기기가 모르는 명령입니다 (펌웨어 버전 확인)',
  [SW.DEVICE_ERROR]: '기기 내부 오류',
  [SW.FRAMING_ERROR]: '프레이밍 오류',
};

export class WalletError extends Error {
  constructor(readonly status: number, message?: string) {
    super(message ?? SW_TEXT[status] ?? `알 수 없는 상태 0x${status.toString(16)}`);
    this.name = 'WalletError';
  }
  get isUserRejection(): boolean {
    return this.status === SW.USER_REJECTED ||
           this.status === SW.CHALLENGE_FAILED ||
           this.status === SW.CHALLENGE_TIMEOUT;
  }
}

// ── 프레이밍 ────────────────────────────────────────────────────────────────

/** 페이로드를 MTU 크기 패킷들로 쪼갠다. docs/protocol.md §2 */
export function frame(tag: number, payload: Uint8Array, mtu: number): Uint8Array[] {
  if (payload.length > MAX_MESSAGE) throw new WalletError(SW.TOO_LARGE);
  const out: Uint8Array[] = [];
  let off = 0, seq = 0;
  while (off < payload.length || seq === 0) {
    const head = seq === 0 ? 5 : 3;
    const room = Math.max(1, mtu - head);
    const n = Math.min(room, payload.length - off);
    const pkt = new Uint8Array(head + n);
    const dv = new DataView(pkt.buffer);
    pkt[0] = tag;
    dv.setUint16(1, seq, false);
    if (seq === 0) dv.setUint16(3, payload.length, false);
    pkt.set(payload.subarray(off, off + n), head);
    out.push(pkt);
    off += n; seq++;
  }
  return out;
}

export interface Assembled { tag: number; payload: Uint8Array }

/** 패킷을 순서대로 먹여서 메시지를 조립한다. */
export class Reassembler {
  private tag = 0;
  private total = 0;
  private seq = 0;
  private buf: number[] = [];

  push(pkt: Uint8Array): Assembled | null {
    if (pkt.length < 3) throw new WalletError(SW.FRAMING_ERROR, '패킷이 너무 짧습니다');
    const dv = new DataView(pkt.buffer, pkt.byteOffset, pkt.byteLength);
    const tag = pkt[0]!;
    const seq = dv.getUint16(1, false);

    if (seq === 0) {
      if (pkt.length < 5) throw new WalletError(SW.FRAMING_ERROR, '첫 패킷이 너무 짧습니다');
      this.tag = tag;
      this.total = dv.getUint16(3, false);
      this.seq = 1;
      this.buf = Array.from(pkt.subarray(5));
    } else {
      if (seq !== this.seq) { this.reset(); throw new WalletError(SW.FRAMING_ERROR, `시퀀스 불일치: ${seq} != ${this.seq}`); }
      this.seq++;
      for (const b of pkt.subarray(3)) this.buf.push(b);
    }
    if (this.buf.length >= this.total) {
      const payload = new Uint8Array(this.buf.slice(0, this.total));
      const tagOut = this.tag;
      this.reset();
      return { tag: tagOut, payload };
    }
    return null;
  }
  reset() { this.total = 0; this.seq = 0; this.buf = []; }
}

// ── 메시지 인코딩 ───────────────────────────────────────────────────────────

export function encodeRequest(cmd: number, payload: Uint8Array = new Uint8Array()): Uint8Array {
  const out = new Uint8Array(3 + payload.length);
  out[0] = cmd;
  new DataView(out.buffer).setUint16(1, payload.length, false);
  out.set(payload, 3);
  return out;
}

export interface Response { status: number; payload: Uint8Array }

export function decodeResponse(msg: Uint8Array): Response {
  if (msg.length < 4) throw new WalletError(SW.FRAMING_ERROR, '응답이 너무 짧습니다');
  const dv = new DataView(msg.buffer, msg.byteOffset, msg.byteLength);
  const status = dv.getUint16(0, false);
  const len = dv.getUint16(2, false);
  if (msg.length < 4 + len) throw new WalletError(SW.FRAMING_ERROR, '응답 길이 불일치');
  return { status, payload: msg.subarray(4, 4 + len) };
}

export interface DeviceEvent { evt: number; payload: Uint8Array }

export function decodeEvent(msg: Uint8Array): DeviceEvent {
  if (msg.length < 3) throw new WalletError(SW.FRAMING_ERROR, '이벤트가 너무 짧습니다');
  const dv = new DataView(msg.buffer, msg.byteOffset, msg.byteLength);
  const len = dv.getUint16(1, false);
  return { evt: msg[0]!, payload: msg.subarray(3, 3 + len) };
}

// ── BIP-32 경로 ─────────────────────────────────────────────────────────────

export const HARDENED = 0x80000000;
export const DEFAULT_PATH = "m/44'/60'/0'/0/0";
export const CHAIN = { ETHEREUM: 0x01, SOLANA: 0x02 } as const;
export type Chain = 'ethereum' | 'solana';

export const CHAIN_ID: Record<Chain, number> = {
  ethereum: CHAIN.ETHEREUM,
  solana:   CHAIN.SOLANA,
};

export const DEFAULT_PATHS: Record<Chain, string> = {
  ethereum: "m/44'/60'/0'/0/0",
  solana:   "m/44'/501'/0'/0'",   // SLIP-0010: 전부 하드닝
};

export const TESTNET = {
  ethereum: { name: 'Sepolia', chainId: 11155111 },
  solana: { name: 'Devnet', cluster: 'devnet' },
} as const;

export function parsePath(path: string): number[] {
  const parts = path.replace(/^m\//, '').split('/').filter(Boolean);
  if (parts.length > 8) throw new WalletError(SW.BAD_PARAM, '경로가 8단계를 넘습니다');
  return parts.map(p => {
    const hard = p.endsWith("'") || p.endsWith('h');
    const n = Number.parseInt(hard ? p.slice(0, -1) : p, 10);
    if (!Number.isInteger(n) || n < 0 || n >= HARDENED) {
      throw new WalletError(SW.BAD_PARAM, `잘못된 경로 요소: ${p}`);
    }
    return hard ? (n + HARDENED) >>> 0 : n >>> 0;
  });
}

export function encodePath(path: string | number[]): Uint8Array {
  const idx = typeof path === 'string' ? parsePath(path) : path;
  const out = new Uint8Array(1 + idx.length * 4);
  out[0] = idx.length;
  const dv = new DataView(out.buffer);
  idx.forEach((v, i) => dv.setUint32(1 + i * 4, v >>> 0, false));
  return out;
}

/**
 * docs/protocol.md §3.5 의 CHAIN_PATH.
 *   [CHAIN:1] [DEPTH:1] [PATH: u32 x DEPTH]
 * 경로를 받는 모든 명령이 이 형태를 쓴다.
 */
export function encodeChainPath(chain: Chain, path: string | number[] = DEFAULT_PATHS[chain]): Uint8Array {
  const idx = typeof path === 'string' ? parsePath(path) : path;
  if (chain === 'solana' && idx.some(v => (v >>> 0) < HARDENED)) {
    throw new WalletError(SW.BAD_PARAM,
      'Solana 는 SLIP-0010 이라 경로 요소가 전부 하드닝이어야 합니다');
  }
  return concat(new Uint8Array([CHAIN_ID[chain]]), encodePath(idx));
}

export function concat(...parts: Uint8Array[]): Uint8Array {
  const n = parts.reduce((s, p) => s + p.length, 0);
  const out = new Uint8Array(n);
  let o = 0;
  for (const p of parts) { out.set(p, o); o += p.length; }
  return out;
}

export function hex(b: Uint8Array): string {
  return '0x' + Array.from(b, x => x.toString(16).padStart(2, '0')).join('');
}

export function fromHex(s: string): Uint8Array {
  const t = s.startsWith('0x') ? s.slice(2) : s;
  if (t.length % 2) throw new WalletError(SW.BAD_PARAM, 'hex 길이가 홀수입니다');
  const out = new Uint8Array(t.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = Number.parseInt(t.substr(i * 2, 2), 16);
  return out;
}

/** GET_STATE / GET_VERSION 의 FLAGS 비트. docs/protocol.md §5 */
export const FLAG = {
  INITIALIZED:      0x01,
  LOCKED:           0x02,
  CHALLENGE_ACTIVE: 0x04,
  HAS_PIN:          0x08,
} as const;

/** PIN 정책. docs/protocol.md §8 */
export const PIN = { MIN: 4, MAX: 8, BUTTONS: 4 } as const;
