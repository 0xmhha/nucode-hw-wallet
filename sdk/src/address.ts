/**
 * EIP-55 체크섬 주소.
 *
 * keccak256 이 필요한데 SDK 가 암호 라이브러리에 의존하지 않도록 여기에
 * 최소 구현을 둔다 (~70줄). 기기가 이미 주소를 계산해서 주므로 SDK 는
 * 체크섬 대소문자만 입히면 된다.
 */

const RC64 = [
  0x0000000000000001n, 0x0000000000008082n, 0x800000000000808an, 0x8000000080008000n,
  0x000000000000808bn, 0x0000000080000001n, 0x8000000080008081n, 0x8000000000008009n,
  0x000000000000008an, 0x0000000000000088n, 0x0000000080008009n, 0x000000008000000an,
  0x000000008000808bn, 0x800000000000008bn, 0x8000000000008089n, 0x8000000000008003n,
  0x8000000000008002n, 0x8000000000000080n, 0x000000000000800an, 0x800000008000000an,
  0x8000000080008081n, 0x8000000000008080n, 0x0000000080000001n, 0x8000000080008008n,
];
const ROTC = [1,3,6,10,15,21,28,36,45,55,2,14,27,41,56,8,25,43,62,18,39,61,20,44];
const PILN = [10,7,11,17,18,3,5,16,8,21,24,4,15,23,19,13,12,2,20,14,22,9,6,1];
const M = (1n << 64n) - 1n;
const rol = (x: bigint, n: number) => ((x << BigInt(n)) | (x >> BigInt(64 - n))) & M;

function keccakF(st: bigint[]) {
  for (let r = 0; r < 24; r++) {
    const bc: bigint[] = [];
    for (let i = 0; i < 5; i++) bc[i] = st[i]! ^ st[i+5]! ^ st[i+10]! ^ st[i+15]! ^ st[i+20]!;
    for (let i = 0; i < 5; i++) {
      const t = bc[(i+4)%5]! ^ rol(bc[(i+1)%5]!, 1);
      for (let j = 0; j < 25; j += 5) st[j+i] = st[j+i]! ^ t;
    }
    let t = st[1]!;
    for (let i = 0; i < 24; i++) {
      const j = PILN[i]!;
      const tmp = st[j]!;
      st[j] = rol(t, ROTC[i]!);
      t = tmp;
    }
    for (let j = 0; j < 25; j += 5) {
      const row: bigint[] = [];
      for (let i = 0; i < 5; i++) row[i] = st[j+i]!;
      for (let i = 0; i < 5; i++) st[j+i] = row[i]! ^ ((~row[(i+1)%5]! & M) & row[(i+2)%5]!);
    }
    st[0] = st[0]! ^ RC64[r]!;
  }
}

export function keccak256(data: Uint8Array): Uint8Array {
  const RATE = 136;
  const st: bigint[] = new Array(25).fill(0n);
  const padded = new Uint8Array(Math.ceil((data.length + 1) / RATE) * RATE);
  padded.set(data);
  padded[data.length] = 0x01;
  padded[padded.length - 1]! |= 0x80;

  for (let off = 0; off < padded.length; off += RATE) {
    for (let i = 0; i < RATE / 8; i++) {
      let v = 0n;
      for (let j = 7; j >= 0; j--) v = (v << 8n) | BigInt(padded[off + i*8 + j]!);
      st[i] = st[i]! ^ v;
    }
    keccakF(st);
  }
  const out = new Uint8Array(32);
  for (let i = 0; i < 4; i++)
    for (let j = 0; j < 8; j++) out[i*8+j] = Number((st[i]! >> BigInt(j*8)) & 0xffn);
  return out;
}

/** EIP-55 체크섬 대소문자를 입힌다. */
export function toChecksumAddress(addr: string): string {
  const lower = (addr.startsWith('0x') ? addr.slice(2) : addr).toLowerCase();
  const hash = keccak256(new TextEncoder().encode(lower));
  let out = '0x';
  for (let i = 0; i < lower.length; i++) {
    const nibble = (hash[i >> 1]! >> (i % 2 === 0 ? 4 : 0)) & 0xf;
    out += nibble >= 8 ? lower[i]!.toUpperCase() : lower[i]!;
  }
  return out;
}
