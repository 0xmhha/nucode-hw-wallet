/** 바이트열 도우미. */
import { SW, WalletError } from './constants.js';

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
