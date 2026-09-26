/* 수수료 추정 (fees.ts).
 *
 * provider 가 서명할 때와 DApp 이 잔액을 확인할 때 같은 규칙을 써야 한다.
 * 그래서 규칙을 한 파일에 두고 둘 다 여기서 가져간다 (docs/TASKS.md T18). */
import test from 'node:test';
import assert from 'node:assert/strict';
import { suggestFees, maxFeeCost, DEFAULT_PRIORITY_FEE } from '../dist/fees.js';

function node(answers) {
  const calls = [];
  const rpc = async (method, params) => {
    calls.push(method);
    const a = answers[method];
    if (a instanceof Error) throw a;
    return typeof a === 'function' ? a(params) : a;
  };
  return { rpc, calls };
}

test('baseFeePerGas 가 있으면 1559 로, 상한은 base×2 + tip', async () => {
  const { rpc } = node({
    eth_getBlockByNumber: { baseFeePerGas: '0x3b9aca00' },   // 1 gwei
    eth_maxPriorityFeePerGas: '0x5f5e100',                   // 0.1 gwei
  });
  assert.deepEqual(await suggestFees(rpc), {
    kind: 'eip1559',
    maxPriorityFeePerGas: 100_000_000n,
    maxFeePerGas: 2_000_000_000n + 100_000_000n,
  });
});

test('노드가 tip 을 못 주면 1.5 gwei 로 잡는다', async () => {
  const { rpc } = node({
    eth_getBlockByNumber: { baseFeePerGas: '0x0' },
    eth_maxPriorityFeePerGas: new Error('method not found'),
  });
  const f = await suggestFees(rpc);
  assert.equal(DEFAULT_PRIORITY_FEE, 1_500_000_000n);
  assert.equal(f.maxPriorityFeePerGas, DEFAULT_PRIORITY_FEE);
  assert.equal(f.maxFeePerGas, DEFAULT_PRIORITY_FEE);
});

test('baseFeePerGas 가 없으면 legacy gasPrice 를 쓴다', async () => {
  const { rpc, calls } = node({
    eth_getBlockByNumber: { number: '0x1' },
    eth_gasPrice: '0x3b9aca00',
  });
  assert.deepEqual(await suggestFees(rpc), { kind: 'legacy', gasPrice: 1_000_000_000n });
  assert.ok(!calls.includes('eth_maxPriorityFeePerGas'));
});

test('블록을 못 읽으면 legacy 로 떨어진다', async () => {
  const { rpc } = node({ eth_getBlockByNumber: new Error('down'), eth_gasPrice: '0x1' });
  assert.equal((await suggestFees(rpc)).kind, 'legacy');
});

test('maxFeeCost 는 가스 × 상한 가격이다', () => {
  assert.equal(maxFeeCost({ kind: 'legacy', gasPrice: 3n }, 21000n), 63000n);
  assert.equal(maxFeeCost({ kind: 'eip1559', maxFeePerGas: 5n, maxPriorityFeePerGas: 1n }, 21000n), 105000n);
});
