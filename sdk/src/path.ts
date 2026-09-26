/** BIP-32 경로와 CHAIN_PATH 인코딩. docs/protocol.md §3.5 */
import { CHAIN, SW, WalletError } from './constants.js';
import { concat } from './bytes.js';

export const HARDENED = 0x80000000;
export const DEFAULT_PATH = "m/44'/60'/0'/0/0";
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
  ethereum: { name: 'Base Sepolia', chainId: 84532 },
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
