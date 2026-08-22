import test from 'node:test';
import assert from 'node:assert/strict';
import { NuWalletProvider } from '../dist/provider.js';

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
