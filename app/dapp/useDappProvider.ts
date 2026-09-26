'use client';
/**
 * 설정이 바뀌면 provider 를 새로 만든다. chainId 는 서명에 들어가므로
 * 옛 설정으로 서명하는 일이 없어야 한다.
 */
import { useEffect, useMemo } from 'react';
import {
  NuWallet, NuWalletProvider, announceNuWalletProvider, type ChallengeCallbacks,
} from '@/sdk/src/index';

export function useDappProvider(
  wallet: NuWallet,
  cfg: { chainId: string; rpcUrl: string; path: string },
  callbacks: () => ChallengeCallbacks,
): NuWalletProvider {
  const { chainId, rpcUrl, path } = cfg;
  const provider = useMemo(() => new NuWalletProvider(wallet, {
    chainId: Number(chainId) || 0,
    rpcUrl: rpcUrl || undefined,
    path,
    ...callbacks(),
  }), [wallet, chainId, rpcUrl, path, callbacks]);

  /* EIP-6963. 이게 없으면 DApp 이 `window.ethereum` 을 통해서만 지갑을 찾고,
   * 확장 지갑이 이미 그 자리를 차지하고 있으면 NuWallet 은 보이지 않는다.
   * 여기서 알려 두면 선택기를 쓰는 DApp 이 목록에 NuWallet 을 띄운다.
   * provider 가 새로 생기면 옛 것의 등록을 지우고 다시 알린다. */
  useEffect(() => announceNuWalletProvider(provider), [provider]);

  return provider;
}
