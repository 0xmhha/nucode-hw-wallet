import { BleTransport } from './transport.js';
import {
  CMD, EVT, SW, WalletError, concat, encodeChainPath, encodePath, hex,
  type Chain, type DeviceEvent,
} from './protocol.js';

export interface PendingSignature { requestId: number; signature: Promise<Uint8Array> }

export class NuWallet {
  constructor(readonly transport = new BleTransport()) {}

  connect(): Promise<void> { return this.transport.connect(); }
  disconnect(): Promise<void> { return this.transport.disconnect(); }

  async generate(strength: 128 | 256 = 256): Promise<number[]> {
    const r = await this.transport.send(CMD.SETUP_GENERATE, new Uint8Array([strength === 256 ? 24 : 12]));
    this.assert(r.status);
    const count = r.payload[0] ?? 0;
    if (r.payload.length !== 1 + count * 2) throw new WalletError(SW.FRAMING_ERROR);
    const dv = new DataView(r.payload.buffer, r.payload.byteOffset, r.payload.byteLength);
    return Array.from({ length: count }, (_, i) => dv.getUint16(1 + i * 2, false));
  }

  async confirm(words: number[]): Promise<void> {
    const p = new Uint8Array(1 + words.length * 2);
    p[0] = words.length;
    const dv = new DataView(p.buffer);
    words.forEach((word, i) => dv.setUint16(1 + i * 2, word, false));
    const r = await this.transport.send(CMD.SETUP_CONFIRM, p);
    this.assert(r.status);
  }

  async address(chain: Chain): Promise<string> {
    const r = await this.transport.send(CMD.GET_ADDRESS, encodeChainPath(chain));
    this.assert(r.status);
    if (chain === 'ethereum') return hex(r.payload);
    return base58(r.payload);
  }

  signEthereum(message: Uint8Array, path?: string): Promise<PendingSignature> {
    return this.beginSign(CMD.SIGN_PERSONAL, concat(encodeChainPath('ethereum', path), message));
  }

  signSolana(message: Uint8Array, path?: string): Promise<PendingSignature> {
    return this.beginSign(CMD.SIGN_TX, concat(encodeChainPath('solana', path), message));
  }

  private async beginSign(cmd: number, payload: Uint8Array): Promise<PendingSignature> {
    const r = await this.transport.send(cmd, payload);
    if (r.status !== SW.PENDING || r.payload.length !== 4) this.assert(r.status);
    const requestId = new DataView(r.payload.buffer, r.payload.byteOffset, 4).getUint32(0, false);
    return { requestId, signature: this.waitForSignature(requestId) };
  }

  private waitForSignature(requestId: number): Promise<Uint8Array> {
    return new Promise((resolve, reject) => {
      const off = this.transport.onEvent((event: DeviceEvent) => {
        if (event.evt !== EVT.SIGN_RESULT || event.payload.length < 6) return;
        const dv = new DataView(event.payload.buffer, event.payload.byteOffset, event.payload.byteLength);
        if (dv.getUint32(0, false) !== requestId) return;
        off();
        const status = dv.getUint16(4, false);
        if (status !== SW.OK) reject(new WalletError(status));
        else resolve(event.payload.slice(6));
      });
    });
  }

  private assert(status: number): void {
    if (status !== SW.OK) throw new WalletError(status);
  }
}

function base58(bytes: Uint8Array): string {
  const alphabet = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz';
  let value = 0n;
  for (const b of bytes) value = value * 256n + BigInt(b);
  let out = '';
  while (value > 0n) { out = alphabet[Number(value % 58n)]! + out; value /= 58n; }
  for (const b of bytes) { if (b !== 0) break; out = '1' + out; }
  return out || '1';
}
