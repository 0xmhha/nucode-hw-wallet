import test from 'node:test';
import assert from 'node:assert/strict';
import { NuWalletProvider } from '../dist/provider.js';
import { WalletError, SW } from '../dist/protocol.js';

const address = '0x1111111111111111111111111111111111111111';
const target = '0x2222222222222222222222222222222222222222';

function firstRlpInteger(bytes) {
  let offset = bytes[0] <= 0xf7 ? 1 : 1 + (bytes[0] - 0xf7);
  const prefix = bytes[offset];
  if (prefix <= 0x7f) return BigInt(prefix);
  const length = prefix - 0x80;
  let value = 0n;
  for (let i = 0; i < length; i++) value = (value << 8n) | BigInt(bytes[offset + 1 + i]);
  return value;
}

test('serializes sends and reserves nonces when the RPC pending nonce is stale', async () => {
  const signedNonces = [];
  let activeSigns = 0;
  let maxActiveSigns = 0;
  let sent = 0;
  const wallet = {
    isConnected: true,
    onDisconnect() {},
    async getAddress() { return address; },
    async signTransaction(_chain, _path, unsigned) {
      activeSigns++;
      maxActiveSigns = Math.max(maxActiveSigns, activeSigns);
      signedNonces.push(firstRlpInteger(unsigned));
      await new Promise((resolve) => setTimeout(resolve, 10));
      activeSigns--;
      return { v: 27, r: '0x' + '01'.repeat(32), s: '0x' + '02'.repeat(32) };
    },
  };

  const originalFetch = globalThis.fetch;
  globalThis.fetch = async (_url, init) => {
    const { id, method } = JSON.parse(init.body);
    const result = method === 'eth_getTransactionCount' ? '0x3'
      : method === 'eth_gasPrice' ? '0x1'
      : method === 'eth_estimateGas' ? '0x5208'
      : method === 'eth_sendRawTransaction' ? `0x${(++sent).toString(16).padStart(64, '0')}`
      : null;
    return { async json() { return { jsonrpc: '2.0', id, result }; } };
  };

  try {
    const provider = new NuWalletProvider(wallet, { chainId: 84532, rpcUrl: 'https://rpc.invalid' });
    const request = () => provider.request({
      method: 'eth_sendTransaction',
      params: [{ from: address, to: target, value: '0x0' }],
    });
    await Promise.all([request(), request()]);
    assert.deepEqual(signedNonces, [3n, 4n]);
    assert.equal(maxActiveSigns, 1);
    assert.equal(sent, 2);
  } finally {
    globalThis.fetch = originalFetch;
  }
});

/* ── EIP-1559 · EIP-712 (docs/TASKS.md T4·T5) ───────────────────────────── */

/** 노드 흉내. 어떤 RPC 가 실제로 불렸는지도 기록해 둔다. */
function fakeNode({ baseFee = null, extra = {} } = {}) {
  const calls = [];
  let sent = 0;
  const raw = [];
  const fetchImpl = async (_url, init) => {
    const { id, method, params } = JSON.parse(init.body);
    calls.push(method);
    let result;
    switch (method) {
      case 'eth_getTransactionCount': result = '0x0'; break;
      case 'eth_gasPrice':            result = '0x3b9aca00'; break;      // 1 gwei
      case 'eth_estimateGas':         result = '0x5208'; break;
      case 'eth_maxPriorityFeePerGas': result = '0x59682f00'; break;     // 1.5 gwei
      case 'eth_getBlockByNumber':
        result = baseFee === null ? { number: '0x1' } : { number: '0x1', baseFeePerGas: baseFee };
        break;
      case 'eth_sendRawTransaction':
        raw.push(params[0]);
        result = `0x${(++sent).toString(16).padStart(64, '0')}`;
        break;
      default: result = extra[method] ?? null;
    }
    return { async json() { return { jsonrpc: '2.0', id, result }; } };
  };
  return { fetchImpl, calls, raw };
}

function fakeWallet(overrides = {}) {
  return {
    isConnected: true,
    onDisconnect() {},
    async getAddress() { return address; },
    async signTransaction(_chain, _path, unsigned, chainId) {
      this.lastUnsigned = unsigned;
      this.lastChainId = chainId;
      // typed 는 yParity, legacy 는 EIP-155 v — client.ts 가 하는 계산을 흉내낸다.
      const typed = unsigned[0] >= 0x01 && unsigned[0] <= 0x7f;
      return {
        recid: 1,
        v: typed ? 1 : 1 + chainId * 2 + 35,
        r: '0x' + '11'.repeat(32),
        s: '0x' + '22'.repeat(32),
      };
    },
    async signTypedHashes(_path, ds, mh) {
      this.lastHashes = [ds, mh];
      return { serialized: '0x' + 'ab'.repeat(65) };
    },
    ...overrides,
  };
}

async function withNode(node, run) {
  const original = globalThis.fetch;
  globalThis.fetch = node.fetchImpl;
  try { return await run(); } finally { globalThis.fetch = original; }
}

test('EIP-1559: baseFeePerGas 가 있는 체인이면 type 2 로 낸다', async () => {
  const node = fakeNode({ baseFee: '0x77359400' });        // 2 gwei
  const wallet = fakeWallet();
  await withNode(node, async () => {
    const provider = new NuWalletProvider(wallet, { chainId: 84532, rpcUrl: 'https://rpc.invalid' });
    await provider.request({
      method: 'eth_sendTransaction',
      params: [{ from: address, to: target, value: '0x1' }],
    });
  });

  assert.equal(wallet.lastUnsigned[0], 0x02, '서명 대상이 type 2 여야 한다');
  assert.equal(wallet.lastChainId, undefined, 'typed 는 EIP-155 chainId 를 쓰지 않는다');
  assert.ok(node.raw[0].startsWith('0x02'), '네트워크로 나간 것도 type 2 다');
  assert.ok(!node.calls.includes('eth_gasPrice'), 'type 2 면 gasPrice 를 묻지 않는다');
});

test('EIP-1559: baseFeePerGas 가 없는 체인이면 legacy 로 떨어진다', async () => {
  const node = fakeNode({ baseFee: null });
  const wallet = fakeWallet();
  await withNode(node, async () => {
    const provider = new NuWalletProvider(wallet, { chainId: 84532, rpcUrl: 'https://rpc.invalid' });
    await provider.request({
      method: 'eth_sendTransaction',
      params: [{ from: address, to: target, value: '0x1' }],
    });
  });
  assert.ok(wallet.lastUnsigned[0] >= 0xc0, 'legacy 는 RLP 리스트로 시작한다');
  assert.equal(wallet.lastChainId, 84532);
  assert.ok(node.calls.includes('eth_gasPrice'));
});

test('EIP-1559: gasPrice 를 준 호출자는 legacy 로 낸다', async () => {
  const node = fakeNode({ baseFee: '0x77359400' });
  const wallet = fakeWallet();
  await withNode(node, async () => {
    const provider = new NuWalletProvider(wallet, { chainId: 84532, rpcUrl: 'https://rpc.invalid' });
    await provider.request({
      method: 'eth_sendTransaction',
      params: [{ from: address, to: target, value: '0x1', gasPrice: '0x3b9aca00' }],
    });
  });
  assert.ok(wallet.lastUnsigned[0] >= 0xc0, 'gasPrice 를 줬으면 그 뜻대로 legacy 다');
});

test('EIP-1559: maxFeePerGas 를 주면 baseFee 를 묻지 않고 type 2 로 낸다', async () => {
  const node = fakeNode({ baseFee: null });
  const wallet = fakeWallet();
  await withNode(node, async () => {
    const provider = new NuWalletProvider(wallet, { chainId: 84532, rpcUrl: 'https://rpc.invalid' });
    await provider.request({
      method: 'eth_signTransaction',
      params: [{
        from: address, to: target, value: '0x1',
        maxFeePerGas: '0x77359400', maxPriorityFeePerGas: '0x3b9aca00',
      }],
    });
  });
  assert.equal(wallet.lastUnsigned[0], 0x02);
  assert.ok(!node.calls.includes('eth_getBlockByNumber'), '둘 다 줬으면 블록을 볼 이유가 없다');
});

const TYPED = {
  types: {
    EIP712Domain: [
      { name: 'name', type: 'string' },
      { name: 'version', type: 'string' },
      { name: 'chainId', type: 'uint256' },
      { name: 'verifyingContract', type: 'address' },
    ],
    Person: [{ name: 'name', type: 'string' }, { name: 'wallet', type: 'address' }],
    Mail: [
      { name: 'from', type: 'Person' },
      { name: 'to', type: 'Person' },
      { name: 'contents', type: 'string' },
    ],
  },
  primaryType: 'Mail',
  domain: {
    name: 'Ether Mail', version: '1', chainId: 1,
    verifyingContract: '0xCcCCccccCCCCcCCCCCCcCcCccCcCCCcCcccccccC',
  },
  message: {
    from: { name: 'Cow', wallet: '0xCD2a3d9F938E13CD947Ec05AbC7FE734Df8DD826' },
    to: { name: 'Bob', wallet: '0xbBbBBBBbbBBBbbbBbbBbbbbBBbBbbbbBbBbbBBbB' },
    contents: 'Hello, Bob!',
  },
};

const toHexStr = (b) => '0x' + Array.from(b, (x) => x.toString(16).padStart(2, '0')).join('');

test('EIP-712: eth_signTypedData_v4 가 기기로 두 해시를 넘긴다', async () => {
  const wallet = fakeWallet();
  const provider = new NuWalletProvider(wallet, { chainId: 1, rpcUrl: 'https://rpc.invalid' });
  const sig = await provider.request({
    method: 'eth_signTypedData_v4',
    params: [address, JSON.stringify(TYPED)],
  });
  assert.equal(sig, '0x' + 'ab'.repeat(65));
  assert.equal(toHexStr(wallet.lastHashes[0]),
    '0xf2cee375fa42b42143804025fc449deafd50cc031ca257e0b194a650a912090f');
  assert.equal(toHexStr(wallet.lastHashes[1]),
    '0xc52c0ee5d84264471806290a3f2c4cecfc5490626bf912d01f240d7a274b371e');
});

test('EIP-712: 다른 체인을 가리키는 서명 요청은 거부한다', async () => {
  const provider = new NuWalletProvider(fakeWallet(), { chainId: 84532, rpcUrl: 'https://rpc.invalid' });
  await assert.rejects(
    () => provider.request({ method: 'eth_signTypedData_v4', params: [address, TYPED] }),
    /chainId/);
});

/* ── EIP-1193 오류 코드 (docs/TASKS.md T6) ──────────────────────────────── */

test('사용자가 거부하면 4001 로 바꿔 준다', async () => {
  const wallet = fakeWallet({
    async signMessage() { throw new WalletError(SW.USER_REJECTED); },
  });
  const provider = new NuWalletProvider(wallet, { chainId: 1, rpcUrl: 'https://rpc.invalid' });
  await assert.rejects(
    () => provider.request({ method: 'personal_sign', params: ['0x01', address] }),
    (e) => {
      assert.equal(e.code, 4001, '표준 코드가 아니면 DApp 이 취소를 구분하지 못한다');
      assert.equal(e.name, 'ProviderRpcError');
      assert.ok(e.data instanceof WalletError, '원래 오류는 data 로 남긴다');
      return true;
    });
});

test('버튼 시퀀스 실패와 시간 초과도 사용자 거부로 본다', async () => {
  for (const status of [SW.CHALLENGE_FAILED, SW.CHALLENGE_TIMEOUT]) {
    const wallet = fakeWallet({ async signMessage() { throw new WalletError(status); } });
    const provider = new NuWalletProvider(wallet, { chainId: 1, rpcUrl: 'https://rpc.invalid' });
    await assert.rejects(
      () => provider.request({ method: 'personal_sign', params: ['0x01', address] }),
      (e) => e.code === 4001);
  }
});

test('잠긴 기기는 4100, 모르는 명령은 4200, 형식 오류는 -32602', async () => {
  const cases = [
    [SW.LOCKED, 4100],
    [SW.PIN_REQUIRED, 4100],
    [SW.NOT_INITIALIZED, 4100],
    [SW.UNKNOWN_CMD, 4200],
    [SW.UNSUPPORTED_CHAIN, 4200],
    [SW.BAD_PARAM, -32602],
    [SW.TOO_LARGE, -32602],
    [SW.DEVICE_ERROR, -32603],
  ];
  for (const [status, code] of cases) {
    const wallet = fakeWallet({ async signMessage() { throw new WalletError(status); } });
    const provider = new NuWalletProvider(wallet, { chainId: 1, rpcUrl: 'https://rpc.invalid' });
    await assert.rejects(
      () => provider.request({ method: 'personal_sign', params: ['0x01', address] }),
      (e) => { assert.equal(e.code, code, `status 0x${status.toString(16)}`); return true; });
  }
});

test('이미 ProviderRpcError 면 코드를 덮어쓰지 않는다', async () => {
  const provider = new NuWalletProvider(fakeWallet(), { chainId: 84532, rpcUrl: 'https://rpc.invalid' });
  await assert.rejects(
    () => provider.request({ method: 'wallet_switchEthereumChain', params: [{ chainId: '0x1' }] }),
    (e) => e.code === 4902);
});
