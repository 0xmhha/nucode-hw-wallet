/** 요청·응답·이벤트 메시지 인코딩. docs/protocol.md §3 */
import { SW, WalletError } from './constants.js';

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
