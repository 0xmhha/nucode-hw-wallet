/**
 * EIP-1193 provider.
 *
 * 기존 DApp 코드가 `window.ethereum` 대신 이걸 꽂으면 그대로 동작하는 것이 목표다.
 *
 * 서명하지 않는 RPC(eth_call, eth_getBalance 등)는 `rpcUrl` 로 넘긴다. 기기는
 * 서명만 한다.
 */
import { NuWallet } from './client.js';
import { DEFAULT_PATH, WalletError, SW, fromHex, hex } from './protocol.js';
import { toChecksumAddress } from './address.js';
import type { ChallengeCallbacks, Eip1193Provider } from './types.js';
import { rlpEncode, encodeLegacyUnsigned, encodeLegacySigned } from './rlp.js';
import { BASE_SEPOLIA } from './networks.js';

export interface ProviderOptions extends ChallengeCallbacks {
  /** 서명 외 RPC 를 넘길 노드. 기본값은 Base Sepolia 공개 RPC. */
  rpcUrl?: string;
  /** EIP-155 v 계산과 eth_chainId 응답에 쓴다. 기본값 84532 (Base Sepolia). */
  chainId?: number;
  path?: string;
}

type Listener = (...args: any[]) => void;

const sendQueues = new Map<string, Promise<void>>();
const nextNonces = new Map<string, bigint>();

async function withLocalSendLock<T>(key: string, task: () => Promise<T>): Promise<T> {
  const previous = sendQueues.get(key) ?? Promise.resolve();
  let release!: () => void;
  const gate = new Promise<void>((resolve) => { release = resolve; });
  const tail = previous.then(() => gate);
  sendQueues.set(key, tail);
  await previous;
  try {
    return await task();
  } finally {
    release();
    if (sendQueues.get(key) === tail) sendQueues.delete(key);
  }
}

export class NuWalletProvider implements Eip1193Provider {
  private listeners = new Map<string, Set<Listener>>();
  private account: string | null = null;
  private rpcId = 1;

  readonly chainId: number;
  readonly rpcUrl: string;

  constructor(readonly wallet: NuWallet, readonly opts: ProviderOptions = {}) {
    this.chainId = opts.chainId ?? BASE_SEPOLIA.chainId;
    this.rpcUrl  = opts.rpcUrl  ?? BASE_SEPOLIA.rpcUrl;
    wallet.onDisconnect(() => {
      this.account = null;
      this.emit('disconnect', new WalletError(SW.DEVICE_ERROR, '기기 연결 끊김'));
      this.emit('accountsChanged', []);
    });
  }

  get path(): string { return this.opts.path ?? DEFAULT_PATH; }

  on(event: string, fn: Listener): void {
    if (!this.listeners.has(event)) this.listeners.set(event, new Set());
    this.listeners.get(event)!.add(fn);
  }
  removeListener(event: string, fn: Listener): void { this.listeners.get(event)?.delete(fn); }
  private emit(event: string, ...args: unknown[]) {
    for (const fn of this.listeners.get(event) ?? []) { try { fn(...args); } catch { /* noop */ } }
  }

  async request({ method, params }: { method: string; params?: unknown[] | object }): Promise<unknown> {
    const p = (Array.isArray(params) ? params : []) as any[];

    switch (method) {
      case 'eth_requestAccounts':
      case 'eth_accounts': {
        if (!this.wallet.isConnected) await this.wallet.connect();
        if (!this.account) {
          this.account = await this.wallet.getAddress('ethereum', this.path);
          this.emit('accountsChanged', [this.account]);
          this.emit('connect', { chainId: this.hexChainId });
        }
        return [this.account];
      }

      case 'eth_chainId': return this.hexChainId;
      case 'net_version': return String(this.chainId);

      case 'wallet_switchEthereumChain': {
        const requested = String((p[0] as { chainId?: string } | undefined)?.chainId ?? '').toLowerCase();
        if (requested === this.hexChainId.toLowerCase()) return null;
        throw new ProviderRpcError(4902,
          `NuWallet provider는 설정된 체인 ${this.hexChainId}만 지원합니다`);
      }

      case 'wallet_addEthereumChain': {
        const requested = String((p[0] as { chainId?: string } | undefined)?.chainId ?? '').toLowerCase();
        if (requested === this.hexChainId.toLowerCase()) return null;
        throw new ProviderRpcError(4200, '실행 중 체인 추가는 지원하지 않습니다');
      }

      case 'personal_sign': {
        // params: [data, address]  — MetaMask 순서를 따른다
        const data = typeof p[0] === 'string' ? p[0] : hex(p[0]);
        const sig = await this.wallet.signMessage(this.path, fromHex(data), this.cb());
        return sig.serialized;
      }

      case 'eth_sign': {
        // params: [address, data]
        const data = typeof p[1] === 'string' ? p[1] : hex(p[1]);
        const sig = await this.wallet.signMessage(this.path, fromHex(data), this.cb());
        return sig.serialized;
      }

      case 'eth_signTypedData_v4': {
        throw new WalletError(SW.BAD_PARAM,
          'eth_signTypedData_v4 는 구조체 해싱이 필요합니다. ' +
          'domainSeparator 와 messageHash 를 직접 계산해 wallet.signTypedHashes() 를 쓰세요.');
      }

      case 'eth_signTransaction':
      case 'eth_sendTransaction': {
        const input = p[0] ?? {};
        if (method === 'eth_signTransaction') {
          const tx = await this.fillTransaction(input);
          const unsigned = encodeLegacyUnsigned(tx, this.chainId);
          const sig = await this.wallet.signTransaction('ethereum', this.path, unsigned, this.chainId, this.cb());
          return hex(encodeLegacySigned(tx, sig.v, fromHex(sig.r), fromHex(sig.s)));
        }
        return await this.sendTransaction(input);
      }

      default:
        return await this.rpc(method, p);
    }
  }

  private cb(): ChallengeCallbacks {
    return { onStart: this.opts.onStart, onProgress: this.opts.onProgress };
  }

  private get hexChainId(): string { return '0x' + this.chainId.toString(16); }

  private async sendTransaction(input: any): Promise<unknown> {
    const from = this.account ?? await this.wallet.getAddress('ethereum', this.path);
    const key = `nuwallet:${this.chainId}:${from.toLowerCase()}`;
    return await withLocalSendLock(key, async () => {
      const run = async () => {
        const chainNonce = toBig(await this.rpc('eth_getTransactionCount', [from, 'pending']));
        const localNonce = nextNonces.get(key) ?? 0n;
        const nonce = input.nonce ?? (chainNonce > localNonce ? chainNonce : localNonce);
        const tx = await this.fillTransaction({ ...input, nonce }, from);
        const unsigned = encodeLegacyUnsigned(tx, this.chainId);
        const sig = await this.wallet.signTransaction('ethereum', this.path, unsigned, this.chainId, this.cb());
        const rawTx = encodeLegacySigned(tx, sig.v, fromHex(sig.r), fromHex(sig.s));
        try {
          const hash = await this.rpc('eth_sendRawTransaction', [hex(rawTx)]);
          nextNonces.set(key, tx.nonce + 1n);
          return hash;
        } catch (error) {
          if (/nonce too low/i.test(String((error as Error)?.message))) {
            const refreshed = toBig(await this.rpc('eth_getTransactionCount', [from, 'pending']));
            nextNonces.set(key, refreshed);
          }
          throw error;
        }
      };

      const locks = globalThis.navigator?.locks;
      return locks ? await locks.request(key, run) : await run();
    });
  }

  /** 빠진 필드를 노드에서 채운다. */
  private async fillTransaction(t: any, knownFrom?: string) {
    const from = knownFrom ?? this.account ?? await this.wallet.getAddress('ethereum', this.path);
    const nonce = t.nonce ?? await this.rpc('eth_getTransactionCount', [from, 'pending']);
    const gasPrice = t.gasPrice ?? await this.rpc('eth_gasPrice', []);
    const gas = t.gas ?? t.gasLimit ?? await this.rpc('eth_estimateGas', [{ ...t, from }]);
    return {
      nonce: toBig(nonce),
      gasPrice: toBig(gasPrice),
      gas: toBig(gas),
      to: t.to ? fromHex(t.to) : new Uint8Array(),
      value: toBig(t.value ?? '0x0'),
      data: t.data ? fromHex(t.data) : new Uint8Array(),
    };
  }

  private async rpc(method: string, params: unknown[]): Promise<unknown> {
    const res = await fetch(this.rpcUrl, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify(
        { jsonrpc: '2.0', id: this.rpcId++, method, params },
        (_key, value) => typeof value === 'bigint' ? `0x${value.toString(16)}` : value,
      ),
    });
    const j = await res.json() as any;
    if (j.error) throw new Error(`RPC ${method}: ${j.error.message}`);
    return j.result;
  }
}

class ProviderRpcError extends Error {
  constructor(readonly code: number, message: string) {
    super(message);
    this.name = 'ProviderRpcError';
  }
}

function toBig(v: unknown): bigint {
  if (typeof v === 'bigint') return v;
  if (typeof v === 'number') return BigInt(v);
  if (typeof v === 'string') return BigInt(v);
  return 0n;
}

export { rlpEncode, toChecksumAddress };
