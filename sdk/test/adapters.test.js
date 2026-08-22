import test from 'node:test';
import assert from 'node:assert/strict';
import {
  NuWalletProvider, NuWalletSolanaAdapter, announceNuWalletProvider,
} from '../dist/index.js';

function fakeWallet() {
  return {
    isConnected: false,
    connects: 0,
    onDisconnect() { return () => {}; },
    async connect() { this.isConnected = true; this.connects++; },
    async disconnect() { this.isConnected = false; },
    async getAddress(chain) {
      return chain === 'ethereum'
        ? '0x5c1a41c2222F62207c57aC5fa85d185aE3eb6cF8'
        : 'So1anaNuWa11et111111111111111111111111111';
    },
    async signTransaction(chain, path, message) {
      assert.equal(chain, 'solana');
      assert.equal(path, "m/44'/501'/0'/0'");
      assert.deepEqual([...message], [1, 2, 3]);
      return { serialized: `0x${'ab'.repeat(64)}` };
    },
  };
}

test('EIP-1193: DApp의 Base Sepolia 전환 요청을 그대로 수락한다', async () => {
  const provider = new NuWalletProvider(fakeWallet());
  assert.equal(await provider.request({
    method: 'wallet_switchEthereumChain', params: [{ chainId: '0x14a34' }],
  }), null);
  await assert.rejects(
    provider.request({ method: 'wallet_switchEthereumChain', params: [{ chainId: '0x1' }] }),
    (e) => e.code === 4902,
  );
});

test('Solana adapter: web3.js Transaction 메시지를 보드로 서명한다', async () => {
  const wallet = fakeWallet();
  class PublicKey { constructor(value) { this.value = value; } }
  const adapter = new NuWalletSolanaAdapter(wallet, { PublicKey });
  const tx = {
    signature: null,
    serializeMessage: () => new Uint8Array([1, 2, 3]),
    addSignature(publicKey, signature) { this.publicKey = publicKey; this.signature = signature; },
  };
  const signed = await adapter.signTransaction(tx);
  assert.equal(wallet.connects, 1);
  assert.equal(signed.publicKey.value, 'So1anaNuWa11et111111111111111111111111111');
  assert.equal(signed.signature.length, 64);
  assert.ok(signed.signature.every((v) => v === 0xab));
});

test('EIP-6963: 기존 지갑 선택기에 NuWallet을 발표한다', () => {
  const oldWindow = globalThis.window;
  const target = new EventTarget();
  globalThis.window = target;
  let detail;
  target.addEventListener('eip6963:announceProvider', (event) => { detail = event.detail; });
  const provider = { request: async () => [], on() {}, removeListener() {} };
  const off = announceNuWalletProvider(provider);
  assert.equal(detail.info.name, 'NuWallet (NU-40 DK)');
  assert.equal(detail.provider, provider);
  off();
  globalThis.window = oldWindow;
});
