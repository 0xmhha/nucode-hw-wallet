/**
 * EIP-712 구조체 해싱.
 *
 * 기기는 `0x32 SIGN_TYPED` 로 32바이트 해시 두 개만 받는다. 구조체를 그 두
 * 값으로 줄이는 일은 호스트 몫인데, 지금까지 SDK 에 없어서 permit 을 쓰는
 * DApp 을 하나도 붙이지 못했다.
 *
 * 외부 라이브러리를 쓰지 않는다. keccak256 은 address.ts 에 이미 있다.
 *
 * ⚠️ 기기는 해시만 보므로 사용자는 자기가 무엇에 서명하는지 알 수 없다.
 *    SECURITY.md 의 블라인드 서명 절이 그대로 적용된다. 호스트 화면에 원문을
 *    보여주는 것이 유일한 방어선이다.
 */
import { keccak256 } from './address.js';
import { concat, fromHex } from './protocol.js';

export interface TypedField { name: string; type: string }
export type TypedTypes = Record<string, TypedField[]>;

export interface TypedData {
  types: TypedTypes;
  primaryType: string;
  domain: Record<string, unknown>;
  message: Record<string, unknown>;
}

const utf8 = (s: string) => new TextEncoder().encode(s);

/* ── 타입 인코딩 ─────────────────────────────────────────────────────────
 *
 * primaryType 을 먼저 쓰고, 거기서 (간접적으로라도) 닿는 구조체 타입을
 * 알파벳 순으로 뒤에 붙인다. 순서가 규약이라 이걸 틀리면 typeHash 가 달라져
 * 컨트랙트가 서명을 거부한다.                                              */

/** `uint256[2]`, `Person[]` 에서 원소 타입만 떼어낸다. */
function elementType(type: string): string {
  const i = type.indexOf('[');
  return i < 0 ? type : type.slice(0, i);
}

function collectDeps(type: string, types: TypedTypes, found: Set<string>): void {
  const base = elementType(type);
  if (found.has(base) || !types[base]) return;
  found.add(base);
  for (const f of types[base]!) collectDeps(f.type, types, found);
}

export function encodeType(primaryType: string, types: TypedTypes): string {
  const fields = types[primaryType];
  if (!fields) throw new Error(`EIP-712: 타입 정의가 없습니다: ${primaryType}`);

  const deps = new Set<string>();
  collectDeps(primaryType, types, deps);
  deps.delete(primaryType);

  const order = [primaryType, ...[...deps].sort()];
  return order.map((name) => {
    const fs = types[name]!;
    for (const f of fs) {
      const base = elementType(f.type);
      if (!ATOMIC.test(base) && !types[base]) {
        throw new Error(`EIP-712: 타입 정의가 없습니다: ${base}`);
      }
    }
    return `${name}(${fs.map((f) => `${f.type} ${f.name}`).join(',')})`;
  }).join('');
}

export function typeHash(primaryType: string, types: TypedTypes): Uint8Array {
  return keccak256(utf8(encodeType(primaryType, types)));
}

/* ── 값 인코딩 ───────────────────────────────────────────────────────────
 *
 * 모든 값이 32바이트로 간다. 동적인 것(string·bytes·배열·구조체)은 먼저
 * 해시해서 32바이트로 만든다.                                              */

const ATOMIC = /^(address|bool|bytes([1-9]|[12][0-9]|3[0-2])?|u?int([0-9]+)?|string)$/;

function pad32(b: Uint8Array, right = false): Uint8Array {
  if (b.length > 32) throw new Error('EIP-712: 32바이트를 넘는 원자 값');
  const out = new Uint8Array(32);
  out.set(b, right ? 0 : 32 - b.length);
  return out;
}

function intToBytes(v: unknown, type: string): Uint8Array {
  const n = typeof v === 'bigint' ? v
    : typeof v === 'number' ? BigInt(v)
    : typeof v === 'string' ? BigInt(v)
    : (() => { throw new Error(`EIP-712: ${type} 에 숫자가 아닌 값`); })();
  if (n < 0n) {
    if (type.startsWith('uint')) throw new Error(`EIP-712: ${type} 에 음수를 넣을 수 없습니다`);
    // 2의 보수. 부호 있는 정수도 32바이트로 채운다.
    const two = (1n << 256n) + n;
    if (two < 0n) throw new Error(`EIP-712: ${type} 범위를 벗어났습니다`);
    return bigToBytes(two, 32);
  }
  return bigToBytes(n, 32);
}

function bigToBytes(n: bigint, len: number): Uint8Array {
  const out = new Uint8Array(len);
  for (let i = len - 1; i >= 0 && n > 0n; i--) { out[i] = Number(n & 0xffn); n >>= 8n; }
  if (n > 0n) throw new Error('EIP-712: 값이 32바이트를 넘습니다');
  return out;
}

function asBytes(v: unknown, what: string): Uint8Array {
  if (v instanceof Uint8Array) return v;
  if (typeof v === 'string') return fromHex(v);
  throw new Error(`EIP-712: ${what} 에 바이트가 아닌 값`);
}

function encodeValue(type: string, value: unknown, types: TypedTypes): Uint8Array {
  if (type.endsWith(']')) {
    const open = type.lastIndexOf('[');
    const inner = type.slice(0, open);
    const fixed = type.slice(open + 1, -1);
    if (!Array.isArray(value)) throw new Error(`EIP-712: ${type} 에 배열이 아닌 값`);
    if (fixed && value.length !== Number(fixed)) {
      throw new Error(`EIP-712: ${type} 의 길이가 ${value.length} 입니다`);
    }
    return keccak256(concat(...value.map((v) => encodeValue(inner, v, types))));
  }

  if (types[type]) return hashStruct(type, value as Record<string, unknown>, types);

  if (type === 'string') {
    if (typeof value !== 'string') throw new Error('EIP-712: string 에 문자열이 아닌 값');
    return keccak256(utf8(value));
  }
  if (type === 'bytes') return keccak256(asBytes(value, 'bytes'));
  if (type === 'address') {
    const b = asBytes(value, 'address');
    if (b.length !== 20) throw new Error('EIP-712: address 는 20바이트여야 합니다');
    return pad32(b);
  }
  if (type === 'bool') return pad32(new Uint8Array([value ? 1 : 0]));

  const fixedBytes = /^bytes([1-9]|[12][0-9]|3[0-2])$/.exec(type);
  if (fixedBytes) {
    const b = asBytes(value, type);
    if (b.length !== Number(fixedBytes[1])) {
      throw new Error(`EIP-712: ${type} 는 ${fixedBytes[1]}바이트여야 합니다`);
    }
    return pad32(b, true);
  }

  if (/^u?int/.test(type)) return intToBytes(value, type);

  throw new Error(`EIP-712: 다룰 수 없는 타입: ${type}`);
}

/** keccak256(typeHash ‖ 각 필드를 32바이트로 인코딩한 것) */
export function hashStruct(primaryType: string, data: Record<string, unknown>,
                           types: TypedTypes): Uint8Array {
  const fields = types[primaryType];
  if (!fields) throw new Error(`EIP-712: 타입 정의가 없습니다: ${primaryType}`);
  const parts = [typeHash(primaryType, types)];
  for (const f of fields) parts.push(encodeValue(f.type, data?.[f.name], types));
  return keccak256(concat(...parts));
}

/**
 * 도메인 구분자. `types.EIP712Domain` 에 적힌 필드만 쓴다 — DApp 마다 넣는
 * 필드가 달라서(salt 를 쓰거나 verifyingContract 를 빼거나) 고정하면 안 된다.
 */
export function domainSeparator(domain: Record<string, unknown>,
                                types: TypedTypes): Uint8Array {
  if (!types.EIP712Domain) throw new Error('EIP-712: types 에 EIP712Domain 이 없습니다');
  return hashStruct('EIP712Domain', domain, types);
}

export interface TypedDataHashes {
  domainSeparator: Uint8Array;
  messageHash: Uint8Array;
  /** keccak256(0x19 0x01 ‖ domainSeparator ‖ messageHash). 기기가 다시 계산한다. */
  digest: Uint8Array;
}

/** DApp 이 넘기는 JSON(문자열이든 객체든)을 기기가 받는 두 해시로 줄인다. */
export function hashTypedData(input: TypedData | string): TypedDataHashes {
  const td: TypedData = typeof input === 'string' ? JSON.parse(input) : input;
  if (!td?.types || !td.primaryType) throw new Error('EIP-712: types 와 primaryType 이 필요합니다');

  const ds = domainSeparator(td.domain ?? {}, td.types);
  const mh = td.primaryType === 'EIP712Domain'
    ? ds                                   // 도메인만 서명하는 드문 경우
    : hashStruct(td.primaryType, td.message ?? {}, td.types);

  return {
    domainSeparator: ds,
    messageHash: mh,
    digest: keccak256(concat(new Uint8Array([0x19, 0x01]), ds, mh)),
  };
}
