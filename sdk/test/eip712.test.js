/* EIP-712 구조체 인코딩.
 *
 * 기기는 domainSeparator 와 messageHash 두 개만 받아 서명한다 (0x32). 그 두
 * 값을 만드는 일이 여기까지 SDK 에 없어서 permit 을 쓰는 DApp 을 하나도 쓰지
 * 못했다 (docs/TASKS.md T5).
 *
 * 기댓값은 세 곳에서 가져왔고 서로를 검증한다.
 *   1. EIP-712 본문의 Mail 예제 — domainSeparator·hashStruct·digest 전부
 *   2. 컨트랙트가 상수로 박아 쓰는 EIP712Domain typehash
 *   3. EIP-2612 Permit typehash
 * 셋이 동시에 맞으려면 타입 문자열 조립과 keccak 이 둘 다 맞아야 한다.
 */
import test from 'node:test';
import assert from 'node:assert/strict';
import {
  encodeType, typeHash, hashStruct, domainSeparator, hashTypedData,
} from '../dist/eip712.js';
import { hex } from '../dist/protocol.js';

const TYPES = {
  EIP712Domain: [
    { name: 'name', type: 'string' },
    { name: 'version', type: 'string' },
    { name: 'chainId', type: 'uint256' },
    { name: 'verifyingContract', type: 'address' },
  ],
  Person: [
    { name: 'name', type: 'string' },
    { name: 'wallet', type: 'address' },
  ],
  Mail: [
    { name: 'from', type: 'Person' },
    { name: 'to', type: 'Person' },
    { name: 'contents', type: 'string' },
  ],
};

const DOMAIN = {
  name: 'Ether Mail',
  version: '1',
  chainId: 1,
  verifyingContract: '0xCcCCccccCCCCcCCCCCCcCcCccCcCCCcCcccccccC',
};

const MAIL = {
  from: { name: 'Cow', wallet: '0xCD2a3d9F938E13CD947Ec05AbC7FE734Df8DD826' },
  to: { name: 'Bob', wallet: '0xbBbBBBBbbBBBbbbBbbBbbbbBBbBbbbbBbBbbBBbB' },
  contents: 'Hello, Bob!',
};

test('encodeType — 참조 타입은 알파벳 순으로 뒤에 붙는다', () => {
  assert.equal(encodeType('Mail', TYPES),
    'Mail(Person from,Person to,string contents)Person(string name,address wallet)');
  assert.equal(encodeType('Person', TYPES), 'Person(string name,address wallet)');
});

test('typeHash — 컨트랙트가 상수로 쓰는 값과 같다', () => {
  // 수많은 EIP-712 컨트랙트에 그대로 박혀 있는 값이다.
  assert.equal(hex(typeHash('EIP712Domain', TYPES)),
    '0x8b73c3c69bb8fe3d512ecc4cf759cc79239f7b179b0ffacaa9a75d522b39400f');

  // EIP-2612 Permit. permit 을 쓰는 토큰이 전부 이 값을 쓴다.
  const permit = {
    Permit: [
      { name: 'owner', type: 'address' },
      { name: 'spender', type: 'address' },
      { name: 'value', type: 'uint256' },
      { name: 'nonce', type: 'uint256' },
      { name: 'deadline', type: 'uint256' },
    ],
  };
  assert.equal(hex(typeHash('Permit', permit)),
    '0x6e71edae12b1b97f4d1f60370fef10105fa2faae0126114a169c64845d6126c9');
});

test('EIP-712 본문의 Mail 예제 — 세 값이 모두 맞는다', () => {
  assert.equal(hex(domainSeparator(DOMAIN, TYPES)),
    '0xf2cee375fa42b42143804025fc449deafd50cc031ca257e0b194a650a912090f');
  assert.equal(hex(hashStruct('Mail', MAIL, TYPES)),
    '0xc52c0ee5d84264471806290a3f2c4cecfc5490626bf912d01f240d7a274b371e');

  const h = hashTypedData({ types: TYPES, primaryType: 'Mail', domain: DOMAIN, message: MAIL });
  assert.equal(hex(h.digest),
    '0xbe609aee343fb3c4b28e1df9e632fca64fcfaede20f02e86244efddf30957bd2');
  assert.equal(hex(h.domainSeparator),
    '0xf2cee375fa42b42143804025fc449deafd50cc031ca257e0b194a650a912090f');
  assert.equal(hex(h.messageHash),
    '0xc52c0ee5d84264471806290a3f2c4cecfc5490626bf912d01f240d7a274b371e');
});

test('JSON 문자열도 그대로 받는다 — DApp 이 넘기는 형태다', () => {
  const json = JSON.stringify({ types: TYPES, primaryType: 'Mail', domain: DOMAIN, message: MAIL });
  assert.equal(hex(hashTypedData(json).digest),
    '0xbe609aee343fb3c4b28e1df9e632fca64fcfaede20f02e86244efddf30957bd2');
});

test('배열과 중첩 구조체', () => {
  const types = {
    EIP712Domain: [{ name: 'name', type: 'string' }],
    Group: [
      { name: 'members', type: 'Person[]' },
      { name: 'scores', type: 'uint256[2]' },
    ],
    Person: TYPES.Person,
  };
  assert.equal(encodeType('Group', types),
    'Group(Person[] members,uint256[2] scores)Person(string name,address wallet)');

  // 값이 맞는지는 손으로 계산하지 않고, 배열 인코딩 규칙(원소 인코딩을 이어
  // 붙여 keccak)이 지켜지는지로 본다. 원소 하나를 바꾸면 해시가 바뀌어야 한다.
  const a = hashStruct('Group', { members: [MAIL.from, MAIL.to], scores: [1, 2] }, types);
  const b = hashStruct('Group', { members: [MAIL.from, MAIL.to], scores: [1, 3] }, types);
  const c = hashStruct('Group', { members: [MAIL.to, MAIL.from], scores: [1, 2] }, types);
  assert.notEqual(hex(a), hex(b));
  assert.notEqual(hex(a), hex(c));
  assert.equal(a.length, 32);
});

test('고정 길이 배열의 길이가 다르면 거부한다', () => {
  const types = {
    EIP712Domain: [{ name: 'name', type: 'string' }],
    Pair: [{ name: 'xs', type: 'uint256[2]' }],
  };
  assert.throws(() => hashStruct('Pair', { xs: [1, 2, 3] }, types), /길이/);
});

test('bytes 와 bytesN 은 다르게 다룬다', () => {
  const types = {
    EIP712Domain: [{ name: 'name', type: 'string' }],
    Blob: [{ name: 'dyn', type: 'bytes' }, { name: 'fixed', type: 'bytes4' }],
  };
  // bytesN 은 오른쪽 패딩으로 32바이트에 그대로 들어가고, bytes 는 keccak 된다.
  const h = hashStruct('Blob', { dyn: '0xdeadbeef', fixed: '0xdeadbeef' }, types);
  assert.equal(h.length, 32);
  assert.throws(() => hashStruct('Blob', { dyn: '0x', fixed: '0xdeadbeefaa' }, types), /bytes4/);
});

test('모르는 타입은 조용히 넘기지 않는다', () => {
  const types = { EIP712Domain: [], Thing: [{ name: 'x', type: 'Missing' }] };
  assert.throws(() => encodeType('Thing', types), /Missing/);
});

test('음수 int 는 2의 보수로 채운다', () => {
  const types = { EIP712Domain: [], N: [{ name: 'v', type: 'int256' }] };
  const neg = hashStruct('N', { v: -1 }, types);
  const pos = hashStruct('N', { v: 1 }, types);
  assert.notEqual(hex(neg), hex(pos));
  assert.throws(() => hashStruct('N', { v: -1 }, { EIP712Domain: [], N: [{ name: 'v', type: 'uint256' }] }),
    /음수/);
});
