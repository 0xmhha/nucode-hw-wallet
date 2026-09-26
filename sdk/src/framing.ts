/**
 * BLE 프레이밍. TAG(1) ‖ SEQ(2) ‖ [TOTAL_LEN(2) — seq 0 에만] ‖ PAYLOAD
 * docs/protocol.md §2
 */
import { MAX_MESSAGE, SW, WalletError } from './constants.js';

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
