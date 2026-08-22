/**
 * 네트워크 설정 — 단일 출처.
 *
 * 이 프로젝트는 **테스트넷 전용**이다. SECURITY.md 에 적힌 이유로 메인넷 자금을
 * 이 기기에 두면 안 된다. 메인넷 항목을 일부러 넣지 않았다.
 *
 * 아래 값은 전부 실측으로 확인했다 (기억으로 적지 않았다).
 *   - Base Sepolia : ethereum-lists/chains (chainid.network/chains.json)
 *   - Solana       : 각 클러스터 RPC 에 getGenesisHash 를 직접 질의
 */
import type { Chain } from './protocol.js';

export interface EvmNetwork {
  kind: 'evm';
  name: string;
  chainId: number;
  rpcUrl: string;
  explorer: string;
  currency: { name: string; symbol: string; decimals: number };
  faucet?: string;
}

export interface SolanaNetwork {
  kind: 'solana';
  name: string;
  cluster: 'testnet' | 'devnet' | 'mainnet-beta';
  rpcUrl: string;
  explorer: string;
  /** 클러스터 식별용 제네시스 해시. CAIP-2 는 앞 32자를 쓴다. */
  genesisHash: string;
  caip2: string;
  faucet?: string;
}

export const BASE_SEPOLIA: EvmNetwork = {
  kind: 'evm',
  name: 'Base Sepolia',
  chainId: 84532,                       // 0x14a34
  rpcUrl: 'https://sepolia.base.org',
  explorer: 'https://sepolia.basescan.org',
  currency: { name: 'Sepolia Ether', symbol: 'ETH', decimals: 18 },
  faucet: 'https://portal.cdp.coinbase.com/products/faucet',
};

export const SOLANA_TESTNET: SolanaNetwork = {
  kind: 'solana',
  name: 'Solana Testnet',
  cluster: 'testnet',
  rpcUrl: 'https://api.testnet.solana.com',
  explorer: 'https://explorer.solana.com/?cluster=testnet',
  genesisHash: '4uhcVJyU9pJkvQyS88uRDiswHXSCkY3zQawwpjk2NsNY',
  caip2: 'solana:4uhcVJyU9pJkvQyS88uRDiswHXSCkY3z',
  faucet: 'https://faucet.solana.com',
};

/** 참고용. Solana 개발 도구/에어드랍은 대부분 devnet 을 가정한다. */
export const SOLANA_DEVNET: SolanaNetwork = {
  kind: 'solana',
  name: 'Solana Devnet',
  cluster: 'devnet',
  rpcUrl: 'https://api.devnet.solana.com',
  explorer: 'https://explorer.solana.com/?cluster=devnet',
  genesisHash: 'EtWTRABZaYq6iMfeYKouRu166VU2xqa1wcaWoxPkrZBG',
  caip2: 'solana:EtWTRABZaYq6iMfeYKouRu166VU2xqa1',
  faucet: 'https://faucet.solana.com',
};

/** 체인별 기본 네트워크. 프로바이더와 웹이 이걸 쓴다. */
export const DEFAULT_NETWORKS: { ethereum: EvmNetwork; solana: SolanaNetwork } = {
  ethereum: BASE_SEPOLIA,
  solana:   SOLANA_TESTNET,
};

export function defaultNetwork(chain: Chain): EvmNetwork | SolanaNetwork {
  return DEFAULT_NETWORKS[chain];
}

/** 트랜잭션 해시로 익스플로러 링크를 만든다. */
export function txUrl(chain: Chain, hash: string): string {
  if (chain === 'ethereum') return `${BASE_SEPOLIA.explorer}/tx/${hash}`;
  return `https://explorer.solana.com/tx/${hash}?cluster=${SOLANA_TESTNET.cluster}`;
}

export function addressUrl(chain: Chain, address: string): string {
  if (chain === 'ethereum') return `${BASE_SEPOLIA.explorer}/address/${address}`;
  return `https://explorer.solana.com/address/${address}?cluster=${SOLANA_TESTNET.cluster}`;
}
