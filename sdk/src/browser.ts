import type { Eip1193Provider } from './types.js';

export interface Eip6963ProviderInfo {
  uuid: string;
  name: string;
  icon: string;
  rdns: string;
}

const DEFAULT_INFO: Eip6963ProviderInfo = {
  uuid: '6e754000-7761-4c4c-8554-000000000001',
  name: 'NuWallet (NU-40 DK)',
  rdns: 'io.nucode.nuwallet',
  icon: 'data:image/svg+xml,<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><rect width="64" height="64" rx="14" fill="%23111513"/><text x="32" y="40" text-anchor="middle" font-size="24" font-family="Arial" font-weight="700" fill="%23d8ff52">NU</text></svg>',
};

/**
 * NuWalletProvider를 EIP-6963 지갑 선택기에 등록한다.
 * 반환 함수는 requestProvider 리스너를 제거한다.
 */
export function announceNuWalletProvider(
  provider: Eip1193Provider,
  info: Partial<Eip6963ProviderInfo> = {},
): () => void {
  if (typeof window === 'undefined') return () => undefined;
  const detail = Object.freeze({ info: Object.freeze({ ...DEFAULT_INFO, ...info }), provider });
  const announce = () => window.dispatchEvent(
    new CustomEvent('eip6963:announceProvider', { detail }));
  window.addEventListener('eip6963:requestProvider', announce);
  announce();
  return () => window.removeEventListener('eip6963:requestProvider', announce);
}
