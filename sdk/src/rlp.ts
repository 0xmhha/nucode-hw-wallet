/**
 * RLP 인코딩 — Ethereum legacy 트랜잭션용 최소 구현.
 *
 * SDK 가 unsigned RLP 를 만들어 기기로 보내고, 기기가 그것을 직접 Keccak 한다.
 * 해시를 보내지 않는 이유는 SECURITY.md 의 "블라인드 서명" 절 참고.
 */

export type RlpInput = Uint8Array | RlpInput[];

function encodeLength(len: number, offset: number): Uint8Array {
  if (len < 56) return new Uint8Array([offset + len]);
  const hexLen = len.toString(16);
  const lenBytes = fromHexRaw(hexLen.length % 2 ? '0' + hexLen : hexLen);
  return new Uint8Array([offset + 55 + lenBytes.length, ...lenBytes]);
}

function fromHexRaw(h: string): Uint8Array {
  const out = new Uint8Array(h.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(h.substr(i * 2, 2), 16);
  return out;
}

function cat(...a: Uint8Array[]): Uint8Array {
  const n = a.reduce((s, x) => s + x.length, 0);
  const o = new Uint8Array(n);
  let i = 0;
  for (const x of a) { o.set(x, i); i += x.length; }
  return o;
}

export function rlpEncode(input: RlpInput): Uint8Array {
  if (Array.isArray(input)) {
    const body = cat(...input.map(rlpEncode));
    return cat(encodeLength(body.length, 0xc0), body);
  }
  if (input.length === 1 && input[0]! < 0x80) return input;
  return cat(encodeLength(input.length, 0x80), input);
}

/** 정수를 RLP 규칙에 맞는 최소 길이 big-endian 바이트열로. 0 은 빈 문자열이다. */
export function toRlpInt(v: bigint | number): Uint8Array {
  let n = typeof v === 'bigint' ? v : BigInt(v);
  if (n < 0n) throw new Error('음수는 RLP 로 인코딩할 수 없습니다');
  if (n === 0n) return new Uint8Array();
  const bytes: number[] = [];
  while (n > 0n) { bytes.unshift(Number(n & 0xffn)); n >>= 8n; }
  return new Uint8Array(bytes);
}

export interface LegacyTx {
  nonce: bigint; gasPrice: bigint; gas: bigint;
  to: Uint8Array; value: bigint; data: Uint8Array;
}

/** EIP-155 서명 대상: RLP([nonce,gasPrice,gas,to,value,data,chainId,0,0]) */
export function encodeLegacyUnsigned(t: LegacyTx, chainId: number): Uint8Array {
  return rlpEncode([
    toRlpInt(t.nonce), toRlpInt(t.gasPrice), toRlpInt(t.gas),
    t.to, toRlpInt(t.value), t.data,
    toRlpInt(chainId), new Uint8Array(), new Uint8Array(),
  ]);
}

/** 서명이 붙은 최종 트랜잭션: RLP([...,v,r,s]) */
export function encodeLegacySigned(t: LegacyTx, v: number, r: Uint8Array, s: Uint8Array): Uint8Array {
  return rlpEncode([
    toRlpInt(t.nonce), toRlpInt(t.gasPrice), toRlpInt(t.gas),
    t.to, toRlpInt(t.value), t.data,
    toRlpInt(v), stripZeros(r), stripZeros(s),
  ]);
}

/** RLP 정수는 선행 0 을 가지면 안 된다. */
export function stripZeros(b: Uint8Array): Uint8Array {
  let i = 0;
  while (i < b.length && b[i] === 0) i++;
  return b.subarray(i);
}

// ── EIP-1559 (type 2) ──────────────────────────────────────────────────────
//
// EIP-2718 의 typed envelope 이라 legacy 와 달리 앞에 타입 바이트가 붙고, 서명
// 대상이 `0x02 ‖ RLP([…])` 그대로다. chainId 가 본문 첫 항목이라 EIP-155 처럼
// v 에 체인을 섞지 않는다. 서명의 첫 항목은 yParity(0 또는 1)다.
//
// 기기(core/rlp.c)가 항목 수와 순서를 검사하므로 여기가 틀리면 조용히 다른
// 트랜잭션에 서명되는 대신 BAD_PARAM 으로 거부된다.

/** accessList 한 칸: [주소 20바이트, 슬롯 32바이트들]. */
export type AccessListItem = [Uint8Array, Uint8Array[]];

export interface Eip1559Tx {
  chainId: number;
  nonce: bigint;
  maxPriorityFeePerGas: bigint;
  maxFeePerGas: bigint;
  gas: bigint;
  /** 비어 있으면 컨트랙트 생성이다. */
  to: Uint8Array;
  value: bigint;
  data: Uint8Array;
  accessList: AccessListItem[];
}

/** JSON-RPC 가 주는 `{address, storageKeys}` 배열을 RLP 항목으로 바꾼다. */
export function encodeAccessList(
  list: { address: string | Uint8Array; storageKeys: (string | Uint8Array)[] }[] = [],
): AccessListItem[] {
  return list.map((e) => {
    const addr = typeof e.address === 'string' ? fromHexRaw(strip0x(e.address)) : e.address;
    if (addr.length !== 20) throw new Error('accessList 주소는 20바이트여야 합니다');
    const keys = (e.storageKeys ?? []).map((k) => {
      const b = typeof k === 'string' ? fromHexRaw(strip0x(k)) : k;
      if (b.length !== 32) throw new Error('accessList 슬롯은 32바이트여야 합니다');
      return b;
    });
    return [addr, keys] as AccessListItem;
  });
}

function strip0x(s: string): string {
  const h = s.startsWith('0x') || s.startsWith('0X') ? s.slice(2) : s;
  return h.length % 2 ? '0' + h : h;
}

function body1559(t: Eip1559Tx): RlpInput[] {
  return [
    toRlpInt(t.chainId), toRlpInt(t.nonce),
    toRlpInt(t.maxPriorityFeePerGas), toRlpInt(t.maxFeePerGas), toRlpInt(t.gas),
    t.to, toRlpInt(t.value), t.data,
    (t.accessList ?? []).map(([addr, keys]) => [addr, keys] as RlpInput),
  ];
}

/** 서명 대상: `0x02 ‖ RLP([chainId,nonce,maxPrio,maxFee,gas,to,value,data,accessList])` */
export function encode1559Unsigned(t: Eip1559Tx): Uint8Array {
  return cat(new Uint8Array([0x02]), rlpEncode(body1559(t)));
}

/** 최종 트랜잭션: 위 항목 뒤에 yParity·r·s 를 붙인다. */
export function encode1559Signed(t: Eip1559Tx, yParity: number,
                                 r: Uint8Array, s: Uint8Array): Uint8Array {
  return cat(new Uint8Array([0x02]), rlpEncode([
    ...body1559(t), toRlpInt(yParity), stripZeros(r), stripZeros(s),
  ]));
}
