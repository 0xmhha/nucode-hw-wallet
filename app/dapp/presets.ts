/* 체인 프리셋. RPC 는 사용자가 직접 넣게 둔다. 공개 엔드포인트를 코드에
 * 박아 두면 금방 죽고, 데모용 키를 넣어 두면 그게 곧 유출이다. */
export const PRESETS = [
  { name: 'Base Sepolia 테스트넷', chainId: 84532, rpc: 'https://sepolia.base.org', symbol: 'ETH' },
  { name: 'Holesky 테스트넷', chainId: 17000, rpc: 'https://ethereum-holesky-rpc.publicnode.com', symbol: 'ETH' },
  { name: '로컬 노드 (anvil/hardhat)', chainId: 31337, rpc: 'http://127.0.0.1:8545', symbol: 'ETH' },
  { name: 'Ethereum 메인넷', chainId: 1, rpc: '', symbol: 'ETH' },
] as const;

export type Preset = typeof PRESETS[number];
