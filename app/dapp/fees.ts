/**
 * 전송 전에 수수료를 정한다. provider 가 서명할 때 쓰는 규칙과 같다.
 *
 * 최신 블록에 baseFeePerGas 가 있으면 EIP-1559 로 내고, 상한은
 * baseFee × 2 + tip 이다. tip 은 eth_maxPriorityFeePerGas 이고, 노드가 받아 주지
 * 않으면 1.5 gwei 다. base fee 가 없는 체인은 eth_gasPrice 로 legacy 를 낸다.
 *
 * SDK 가 같은 이름(suggestFees, maxFeeCost)을 index 에서 내보내면 이 파일을 지우고
 * import 를 '@/sdk/src/index' 로 바꾼다. 시그니처를 맞춰 둔 이유다.
 */
export type RpcCall = (method: string, params: unknown[]) => Promise<unknown>;

export type FeeSuggestion =
  | { kind: 'eip1559'; maxFeePerGas: bigint; maxPriorityFeePerGas: bigint }
  | { kind: 'legacy'; gasPrice: bigint };

const FALLBACK_TIP = 1_500_000_000n;

export async function suggestFees(rpc: RpcCall): Promise<FeeSuggestion> {
  const block = await rpc('eth_getBlockByNumber', ['latest', false]) as
    { baseFeePerGas?: string | null } | null;
  const baseFee = block?.baseFeePerGas;
  if (baseFee === undefined || baseFee === null) {
    return { kind: 'legacy', gasPrice: BigInt(await rpc('eth_gasPrice', []) as string) };
  }
  let tip = FALLBACK_TIP;
  try { tip = BigInt(await rpc('eth_maxPriorityFeePerGas', []) as string); } catch { /* 1.5 gwei */ }
  return { kind: 'eip1559', maxFeePerGas: BigInt(baseFee) * 2n + tip, maxPriorityFeePerGas: tip };
}

/** 이 트랜잭션이 잔액에서 가져갈 수 있는 수수료의 최댓값. */
export function maxFeeCost(fees: FeeSuggestion, gas: bigint): bigint {
  return gas * (fees.kind === 'eip1559' ? fees.maxFeePerGas : fees.gasPrice);
}
