/**
 * 수수료 추정.
 *
 * provider 가 서명할 트랜잭션을 채울 때와, DApp 이 보내기 전에 잔액이 되는지
 * 확인할 때 같은 규칙을 써야 한다. 둘이 다르면 확인을 통과하고도 노드가
 * 거부한다. 그래서 규칙을 여기 한 곳에 둔다.
 */

/** JSON-RPC 한 번. provider.request 나 fetch 를 감싸서 넘긴다. */
export type RpcCall = (method: string, params: unknown[]) => Promise<unknown>;

export type FeeSuggestion =
  | { kind: 'eip1559'; maxFeePerGas: bigint; maxPriorityFeePerGas: bigint }
  | { kind: 'legacy'; gasPrice: bigint };

/** 노드가 eth_maxPriorityFeePerGas 를 받아 주지 않을 때 쓰는 tip. 1.5 gwei. */
export const DEFAULT_PRIORITY_FEE = 1_500_000_000n;

export function toBig(v: unknown): bigint {
  if (typeof v === 'bigint') return v;
  if (typeof v === 'number') return BigInt(v);
  if (typeof v === 'string') return BigInt(v);
  return 0n;
}

/**
 * 최신 블록의 baseFeePerGas. 없으면(런던 이전 체인) null.
 * 블록을 못 읽어도 null 이다 — 그러면 legacy 로 가는데, legacy 는 어디서나 통한다.
 */
export async function fetchBaseFee(rpc: RpcCall): Promise<bigint | null> {
  try {
    const block = await rpc('eth_getBlockByNumber', ['latest', false]) as
      { baseFeePerGas?: string | null } | null;
    const b = block?.baseFeePerGas;
    return b === undefined || b === null ? null : toBig(b);
  } catch {
    return null;
  }
}

export async function fetchPriorityFee(rpc: RpcCall): Promise<bigint> {
  try { return toBig(await rpc('eth_maxPriorityFeePerGas', [])); }
  catch { return DEFAULT_PRIORITY_FEE; }
}

/**
 * base fee 는 블록마다 최대 12.5% 오른다. 두 배로 잡으면 몇 블록 동안 계속
 * 올라도 트랜잭션이 밀려나지 않는다. 실제로 내는 것은 base + tip 이고 나머지는
 * 돌려받으므로 두 배로 잡아도 손해는 없다.
 */
export function maxFeeFor(baseFee: bigint, priorityFee: bigint): bigint {
  return baseFee * 2n + priorityFee;
}

/** 지금 체인에 맞는 수수료를 고른다. provider 가 아무 지정이 없을 때 쓰는 규칙과 같다. */
export async function suggestFees(rpc: RpcCall): Promise<FeeSuggestion> {
  const base = await fetchBaseFee(rpc);
  if (base === null) {
    return { kind: 'legacy', gasPrice: toBig(await rpc('eth_gasPrice', [])) };
  }
  const tip = await fetchPriorityFee(rpc);
  return { kind: 'eip1559', maxFeePerGas: maxFeeFor(base, tip), maxPriorityFeePerGas: tip };
}

/** 이 가스량으로 최악의 경우 내게 되는 수수료. 잔액 확인에 쓴다. */
export function maxFeeCost(fees: FeeSuggestion, gas: bigint): bigint {
  return gas * (fees.kind === 'eip1559' ? fees.maxFeePerGas : fees.gasPrice);
}
