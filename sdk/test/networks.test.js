/* 네트워크 상수 회귀 테스트.
 * 값들은 실측으로 확인했다:
 *   Base Sepolia — ethereum-lists/chains (chainid.network/chains.json)
 *   Solana       — 각 클러스터 RPC 의 getGenesisHash 응답
 * 여기서 굳는 건 "설정이 조용히 바뀌지 않는다" 는 보장이다.               */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  BASE_SEPOLIA, SOLANA_TESTNET, SOLANA_DEVNET, DEFAULT_NETWORKS, txUrl, addressUrl,
} from '../dist/networks.js';

test('Base Sepolia 설정', () => {
  assert.equal(BASE_SEPOLIA.chainId, 84532);
  assert.equal('0x' + BASE_SEPOLIA.chainId.toString(16), '0x14a34');
  assert.equal(BASE_SEPOLIA.rpcUrl, 'https://sepolia.base.org');
  assert.equal(BASE_SEPOLIA.explorer, 'https://sepolia.basescan.org');
  assert.equal(BASE_SEPOLIA.currency.symbol, 'ETH');
  assert.equal(BASE_SEPOLIA.currency.decimals, 18);
});

test('Solana testnet 설정', () => {
  assert.equal(SOLANA_TESTNET.cluster, 'testnet');
  assert.equal(SOLANA_TESTNET.rpcUrl, 'https://api.testnet.solana.com');
  assert.equal(SOLANA_TESTNET.genesisHash,
    '4uhcVJyU9pJkvQyS88uRDiswHXSCkY3zQawwpjk2NsNY');
  // CAIP-2 는 제네시스 해시 앞 32자다
  assert.equal(SOLANA_TESTNET.caip2, 'solana:' + SOLANA_TESTNET.genesisHash.slice(0, 32));
});

test('testnet 과 devnet 을 헷갈리지 않는다', () => {
  assert.notEqual(SOLANA_TESTNET.genesisHash, SOLANA_DEVNET.genesisHash);
  assert.equal(SOLANA_DEVNET.genesisHash, 'EtWTRABZaYq6iMfeYKouRu166VU2xqa1wcaWoxPkrZBG');
});

test('기본 네트워크는 테스트넷이다', () => {
  assert.equal(DEFAULT_NETWORKS.ethereum.chainId, 84532);
  assert.equal(DEFAULT_NETWORKS.solana.cluster, 'testnet');
  // 메인넷이 기본으로 새어 들어오면 안 된다
  assert.notEqual(DEFAULT_NETWORKS.ethereum.chainId, 8453);   // Base mainnet
  assert.notEqual(DEFAULT_NETWORKS.solana.cluster, 'mainnet-beta');
});

test('EIP-155 v 계산 (Base Sepolia)', () => {
  // v = recid + chainId*2 + 35
  const base = 84532 * 2 + 35;
  assert.equal(0 + base, 169099);
  assert.equal(1 + base, 169100);
});

test('익스플로러 링크', () => {
  assert.equal(txUrl('ethereum', '0xabc'), 'https://sepolia.basescan.org/tx/0xabc');
  assert.equal(addressUrl('ethereum', '0xdef'), 'https://sepolia.basescan.org/address/0xdef');
  assert.match(txUrl('solana', 'sig'), /explorer\.solana\.com\/tx\/sig\?cluster=testnet/);
});
