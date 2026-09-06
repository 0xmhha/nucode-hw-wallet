/* 트랜잭션 인코딩.
 *
 * 기기는 해시가 아니라 **직렬화된 트랜잭션 자체**를 받아 스스로 Keccak 한다
 * (SECURITY.md 의 블라인드 서명 절). 그래서 SDK 가 만든 바이트가 틀리면 기기가
 * 거부하거나, 더 나쁘게는 의도와 다른 트랜잭션에 서명한다.
 *
 * legacy 는 EIP-155 본문의 예제로 고정한다. type 2 는 여기서 구조를 보고,
 * 실제 수용 여부는 적합성 테스트가 펌웨어 파서(rlp.c)로 확인한다.
 */
import test from 'node:test';
import assert from 'node:assert/strict';
import {
  encodeLegacyUnsigned, encode1559Unsigned, encode1559Signed, encodeAccessList,
} from '../dist/rlp.js';
import { hex, fromHex } from '../dist/protocol.js';

const TO = fromHex('0x3535353535353535353535353535353535353535');

test('legacy — EIP-155 본문의 예제와 바이트가 같다', () => {
  const tx = {
    nonce: 9n, gasPrice: 20000000000n, gas: 21000n,
    to: TO, value: 1000000000000000000n, data: new Uint8Array(),
  };
  assert.equal(hex(encodeLegacyUnsigned(tx, 1)),
    '0xec098504a817c800825208943535353535353535353535353535353535353535'
    + '880de0b6b3a764000080018080');
});

/* RLP 를 되읽는 최소 디코더. 인코더와 같은 코드를 쓰지 않으려고 따로 짰다. */
function rlpDecode(b, i = 0) {
  const p = b[i];
  if (p <= 0x7f) return [b.subarray(i, i + 1), i + 1];
  if (p <= 0xb7) return [b.subarray(i + 1, i + 1 + (p - 0x80)), i + 1 + (p - 0x80)];
  if (p <= 0xbf) {
    const ll = p - 0xb7;
    let n = 0; for (let k = 0; k < ll; k++) n = n * 256 + b[i + 1 + k];
    return [b.subarray(i + 1 + ll, i + 1 + ll + n), i + 1 + ll + n];
  }
  let start, end;
  if (p <= 0xf7) { start = i + 1; end = start + (p - 0xc0); }
  else {
    const ll = p - 0xf7;
    let n = 0; for (let k = 0; k < ll; k++) n = n * 256 + b[i + 1 + k];
    start = i + 1 + ll; end = start + n;
  }
  const items = [];
  let j = start;
  while (j < end) { const [v, next] = rlpDecode(b, j); items.push(v); j = next; }
  return [items, end];
}

const big = (b) => (b.length ? BigInt('0x' + hex(b).slice(2)) : 0n);

test('type 2 — 0x02 접두사 뒤에 EIP-1559 순서로 아홉 항목이 온다', () => {
  const tx = {
    chainId: 84532, nonce: 7n,
    maxPriorityFeePerGas: 1500000000n, maxFeePerGas: 30000000000n,
    gas: 21000n, to: TO, value: 1000n, data: new Uint8Array(), accessList: [],
  };
  const raw = encode1559Unsigned(tx);
  assert.equal(raw[0], 0x02, '2718 타입 바이트');

  const [items] = rlpDecode(raw.subarray(1));
  assert.equal(items.length, 9);
  assert.equal(big(items[0]), 84532n);
  assert.equal(big(items[1]), 7n);
  assert.equal(big(items[2]), 1500000000n);
  assert.equal(big(items[3]), 30000000000n);
  assert.equal(big(items[4]), 21000n);
  assert.equal(hex(items[5]), hex(TO));
  assert.equal(big(items[6]), 1000n);
  assert.equal(items[7].length, 0);
  assert.ok(Array.isArray(items[8]), 'accessList 는 리스트여야 한다');
});

test('type 2 서명본 — 뒤에 yParity·r·s 가 붙어 열두 항목이 된다', () => {
  const tx = {
    chainId: 1, nonce: 0n, maxPriorityFeePerGas: 1n, maxFeePerGas: 2n,
    gas: 21000n, to: TO, value: 0n, data: new Uint8Array(), accessList: [],
  };
  const r = fromHex('0x' + '11'.repeat(32));
  const s = fromHex('0x' + '22'.repeat(32));
  const raw = encode1559Signed(tx, 1, r, s);
  assert.equal(raw[0], 0x02);

  const [items] = rlpDecode(raw.subarray(1));
  assert.equal(items.length, 12);
  assert.equal(big(items[9]), 1n, 'yParity 는 0 이나 1 이다 (EIP-155 v 가 아니다)');
  assert.equal(hex(items[10]), hex(r));
  assert.equal(hex(items[11]), hex(s));

  // yParity 0 은 RLP 에서 빈 문자열이다.
  const [zero] = rlpDecode(encode1559Signed(tx, 0, r, s).subarray(1));
  assert.equal(zero[9].length, 0);
});

test('컨트랙트 생성 — to 가 비면 빈 문자열로 들어간다', () => {
  const tx = {
    chainId: 1, nonce: 0n, maxPriorityFeePerGas: 1n, maxFeePerGas: 2n,
    gas: 100000n, to: new Uint8Array(), value: 0n,
    data: fromHex('0x6000'), accessList: [],
  };
  const [items] = rlpDecode(encode1559Unsigned(tx).subarray(1));
  assert.equal(items[5].length, 0);
  assert.equal(hex(items[7]), '0x6000');
});

test('accessList — [주소, [슬롯…]] 쌍으로 들어간다', () => {
  const list = encodeAccessList([{
    address: '0x' + 'ab'.repeat(20),
    storageKeys: ['0x' + '01'.repeat(32), '0x' + '02'.repeat(32)],
  }]);
  assert.equal(list.length, 1);
  assert.equal(list[0].length, 2);
  assert.equal(hex(list[0][0]), '0x' + 'ab'.repeat(20));
  assert.equal(list[0][1].length, 2);

  const tx = {
    chainId: 1, nonce: 0n, maxPriorityFeePerGas: 1n, maxFeePerGas: 2n,
    gas: 21000n, to: TO, value: 0n, data: new Uint8Array(), accessList: list,
  };
  const [items] = rlpDecode(encode1559Unsigned(tx).subarray(1));
  assert.equal(items[8].length, 1);
  assert.equal(hex(items[8][0][0]), '0x' + 'ab'.repeat(20));
});

test('주소 길이가 20 이 아니면 거부한다', () => {
  assert.throws(() => encodeAccessList([{ address: '0xabcd', storageKeys: [] }]), /20/);
});
