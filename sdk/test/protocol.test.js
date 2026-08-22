/* SDK 단위 테스트 — 하드웨어 없이 도는 것만.
 *   node --test test/*.test.js                                              */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  frame, Reassembler, encodeRequest, decodeResponse, decodeEvent,
  parsePath, encodePath, hex, fromHex, concat, HARDENED, SW, WalletError,
} from '../dist/protocol.js';
import { rlpEncode, toRlpInt, stripZeros, encodeLegacyUnsigned, encodeLegacySigned } from '../dist/rlp.js';
import { keccak256, toChecksumAddress } from '../dist/address.js';

test('프레이밍: 왕복', () => {
  for (const len of [0, 1, 14, 15, 16, 17, 100, 2048]) {
    const payload = new Uint8Array(len);
    for (let i = 0; i < len; i++) payload[i] = (i * 7 + 3) & 0xff;
    for (const mtu of [20, 23, 64, 244]) {
      const pkts = frame(0x05, payload, mtu);
      const r = new Reassembler();
      let out = null;
      for (const p of pkts) { const a = r.push(p); if (a) out = a; }
      assert.ok(out, `조립 실패 len=${len} mtu=${mtu}`);
      assert.equal(out.tag, 0x05);
      assert.deepEqual(Array.from(out.payload), Array.from(payload), `len=${len} mtu=${mtu}`);
    }
  }
});

test('프레이밍: 첫 패킷에 총 길이가 들어간다', () => {
  const pkts = frame(0x05, new Uint8Array(300), 20);
  assert.equal(pkts[0][0], 0x05);
  assert.equal((pkts[0][1] << 8) | pkts[0][2], 0);       // seq 0
  assert.equal((pkts[0][3] << 8) | pkts[0][4], 300);     // total
  assert.equal((pkts[1][1] << 8) | pkts[1][2], 1);       // seq 1
});

test('프레이밍: 시퀀스가 건너뛰면 거부', () => {
  const pkts = frame(0x05, new Uint8Array(100), 20);
  const r = new Reassembler();
  r.push(pkts[0]);
  assert.throws(() => r.push(pkts[2]), (e) => e instanceof WalletError && e.status === SW.FRAMING_ERROR);
});

test('프레이밍: 2048 초과는 거부', () => {
  assert.throws(() => frame(0x05, new Uint8Array(2049), 20),
                (e) => e instanceof WalletError && e.status === SW.TOO_LARGE);
});

test('메시지 인코딩/디코딩', () => {
  const req = encodeRequest(0x30, new Uint8Array([1, 2, 3]));
  assert.deepEqual(Array.from(req), [0x30, 0, 3, 1, 2, 3]);

  const resp = decodeResponse(new Uint8Array([0x90, 0x00, 0, 2, 0xaa, 0xbb]));
  assert.equal(resp.status, 0x9000);
  assert.deepEqual(Array.from(resp.payload), [0xaa, 0xbb]);

  const evt = decodeEvent(new Uint8Array([0xa0, 0, 2, 0x11, 0x22]));
  assert.equal(evt.evt, 0xa0);
  assert.deepEqual(Array.from(evt.payload), [0x11, 0x22]);
});

test('BIP-32 경로 파싱', () => {
  assert.deepEqual(parsePath("m/44'/60'/0'/0/0"),
    [44 + HARDENED, 60 + HARDENED, 0 + HARDENED, 0, 0].map(v => v >>> 0));
  assert.deepEqual(parsePath("44h/60h/0h/0/0"),
    [44 + HARDENED, 60 + HARDENED, 0 + HARDENED, 0, 0].map(v => v >>> 0));

  const enc = encodePath("m/44'/60'/0'/0/0");
  assert.equal(enc[0], 5);
  assert.equal(enc.length, 21);
  assert.equal(hex(enc.subarray(1, 5)), '0x8000002c');   // 44'

  assert.throws(() => parsePath("m/0/1/2/3/4/5/6/7/8"));  // 8단계 초과
});

test('hex 왕복', () => {
  const b = new Uint8Array([0x00, 0x0f, 0xff, 0xa5]);
  assert.equal(hex(b), '0x000fffa5');
  assert.deepEqual(Array.from(fromHex('0x000fffa5')), Array.from(b));
  assert.deepEqual(Array.from(fromHex('000fffa5')), Array.from(b));
});

test('Keccak-256 (Ethereum 표준 벡터)', () => {
  assert.equal(hex(keccak256(new Uint8Array())),
    '0xc5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470');
  assert.equal(hex(keccak256(new TextEncoder().encode('abc'))),
    '0x4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45');
  assert.equal(hex(keccak256(new TextEncoder().encode(
    'The quick brown fox jumps over the lazy dog'))),
    '0x4d741b6f1eb29cb2a9b9911c82f56fa8d73b04959d3d9d222895df6c0b28aa15');
});

test('EIP-55 체크섬 주소 (명세 예시)', () => {
  for (const a of [
    '0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed',
    '0xfB6916095ca1df60bB79Ce92cE3Ea74c37c5d359',
    '0xdbF03B407c01E7cD3CBea99509d93f8DDDC8C6FB',
    '0xD1220A0cf47c7B9Be7A2E6BA89F429762e7b9aDb',
  ]) {
    assert.equal(toChecksumAddress(a.toLowerCase()), a);
  }
});

test('RLP: 명세 예시', () => {
  const S = (s) => new TextEncoder().encode(s);
  assert.equal(hex(rlpEncode(S('dog'))), '0x83646f67');
  assert.equal(hex(rlpEncode([S('cat'), S('dog')])), '0xc88363617483646f67');
  assert.equal(hex(rlpEncode(S(''))), '0x80');
  assert.equal(hex(rlpEncode([])), '0xc0');
  assert.equal(hex(rlpEncode(new Uint8Array([0x00]))), '0x00');
  assert.equal(hex(rlpEncode(new Uint8Array([0x0f]))), '0x0f');
  assert.equal(hex(rlpEncode(new Uint8Array([0x04, 0x00]))), '0x820400');
  assert.equal(hex(rlpEncode(S('Lorem ipsum dolor sit amet, consectetur adipisicing elit'))),
    '0xb8374c6f72656d20697073756d20646f6c6f722073697420616d65742c20636f6e7365637465747572206164697069736963696e6720656c6974');
});

test('RLP 정수 인코딩', () => {
  assert.equal(hex(toRlpInt(0n)), '0x');
  assert.equal(hex(toRlpInt(1n)), '0x01');
  assert.equal(hex(toRlpInt(255n)), '0xff');
  assert.equal(hex(toRlpInt(256n)), '0x0100');
  assert.throws(() => toRlpInt(-1n));
});

test('선행 0 제거', () => {
  assert.equal(hex(stripZeros(new Uint8Array([0, 0, 1, 2]))), '0x0102');
  assert.equal(hex(stripZeros(new Uint8Array([0, 0, 0]))), '0x');
});

test('EIP-155 서명 대상 인코딩 (명세 예시)', () => {
  // EIP-155 명세의 예제 트랜잭션
  const tx = {
    nonce: 9n,
    gasPrice: 20000000000n,
    gas: 21000n,
    to: fromHex('0x3535353535353535353535353535353535353535'),
    value: 1000000000000000000n,
    data: new Uint8Array(),
  };
  const unsigned = encodeLegacyUnsigned(tx, 1);
  assert.equal(hex(unsigned),
    '0xec098504a817c800825208943535353535353535353535353535353535353535880de0b6b3a764000080018080');
  // 기기가 계산하는 서명 해시와 같아야 한다
  assert.equal(hex(keccak256(unsigned)),
    '0xdaf5a779ae972f972197303d7b574746c7ef83eadac0f2791ad23db3cce0b0e7');
});

test('서명된 트랜잭션 인코딩 (명세 예시)', () => {
  const tx = {
    nonce: 9n, gasPrice: 20000000000n, gas: 21000n,
    to: fromHex('0x3535353535353535353535353535353535353535'),
    value: 1000000000000000000n, data: new Uint8Array(),
  };
  const raw = encodeLegacySigned(tx, 37,
    fromHex('0x28ef61340bd939bc2195fe537567866003e1a15d3c71ff63e1590620aa636276'),
    fromHex('0x67cbe9d8997f761aecb703304b3800ccf555c9f3dc64214b297fb1966a3b6d83'));
  assert.equal(hex(raw),
    '0xf86c098504a817c800825208943535353535353535353535353535353535353535880de0b6b3a76400008025' +
    'a028ef61340bd939bc2195fe537567866003e1a15d3c71ff63e1590620aa636276' +
    'a067cbe9d8997f761aecb703304b3800ccf555c9f3dc64214b297fb1966a3b6d83');
});
