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
import {
  rlpEncode, encodeLegacyUnsigned, encodeLegacySigned,
  encode1559Unsigned, encode1559Signed, encodeAccessList,
  type LegacyTx, type Eip1559Tx,
} from './rlp.js';
import { hashTypedData, type TypedData } from './eip712.js';
import { BASE_SEPOLIA } from './networks.js';
import { ProviderRpcError, toProviderError } from './errors.js';
import { fetchBaseFee, fetchPriorityFee, maxFeeFor, toBig } from './fees.js';

export interface ProviderOptions extends ChallengeCallbacks {
  /** 서명 외 RPC 를 넘길 노드. 기본값은 Base Sepolia 공개 RPC. */
  rpcUrl?: string;
  /** EIP-155 v 계산과 eth_chainId 응답에 쓴다. 기본값 84532 (Base Sepolia). */
  chainId?: number;
  path?: string;
}

type Listener = (...args: any[]) => void;

/** DApp 이 `eth_sendTransaction` 에 넘기는 트랜잭션. 무엇이 오든 다 비어 있을 수 있다. */
interface TxInput {
  from?: string;
  to?: string;
  gas?: string | number | bigint;
  gasLimit?: string | number | bigint;
  gasPrice?: string | number | bigint;
  maxFeePerGas?: string | number | bigint;
  maxPriorityFeePerGas?: string | number | bigint;
  nonce?: string | number | bigint;
  value?: string | number | bigint;
  data?: string;
  type?: string | number;
  accessList?: { address: string; storageKeys: string[] }[];
}

/** 채워 넣기가 끝난 트랜잭션. 어느 형식으로 낼지는 여기서 이미 정해져 있다. */
type Filled =
  | { kind: 'legacy'; tx: LegacyTx }
  | { kind: 'eip1559'; tx: Eip1559Tx };

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
  /** 노드가 EIP-1559 를 쓰는지. 한 번 확인하고 기억한다 (null 이면 아직 모름). */
  private baseFeeChain: boolean | null = null;

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

  /**
   * EIP-1193 진입점. 기기 상태 코드를 표준 오류 코드로 바꿔서 던진다 —
   * DApp 은 "사용자가 취소(4001)"와 "기기가 잠김(4100)"과 "펌웨어가 모르는
   * 명령(4200)"에 서로 다르게 반응해야 하는데, 상태 코드를 그대로 올리면
   * 구분할 방법이 없다.
   */
  async request(args: { method: string; params?: unknown[] | object }): Promise<unknown> {
    try {
      return await this.handle(args);
    } catch (e) {
      throw toProviderError(e);
    }
  }

  private async handle({ method, params }: { method: string; params?: unknown[] | object }): Promise<unknown> {
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

      case 'eth_signTypedData':
      case 'eth_signTypedData_v3':
      case 'eth_signTypedData_v4': {
        // params: [address, typedData] — typedData 는 JSON 문자열이거나 객체다.
        const typed = p[1] ?? p[0];
        const h = hashTypedData(typed as TypedData | string);

        // 도메인이 다른 체인을 가리키면 서명해 주지 않는다. 기기는 해시만 보고
        // 서명하므로 여기서 막지 않으면 아무도 못 막는다.
        const td = (typeof typed === 'string' ? JSON.parse(typed) : typed) as TypedData;
        const domainChain = td?.domain?.chainId as string | number | undefined;
        if (domainChain !== undefined && domainChain !== null) {
          const want = toBig(typeof domainChain === 'string' ? domainChain : `0x${Number(domainChain).toString(16)}`);
          if (want !== BigInt(this.chainId)) {
            throw new ProviderRpcError(4901,
              `서명 요청의 domain.chainId(${want})가 연결된 체인(${this.chainId})과 다릅니다`);
          }
        }

        const sig = await this.wallet.signTypedHashes(
          this.path, h.domainSeparator, h.messageHash, this.cb());
        return sig.serialized;
      }

      case 'eth_signTransaction':
      case 'eth_sendTransaction': {
        const input = p[0] ?? {};
        if (method === 'eth_signTransaction') {
          return hex(await this.signFilled(await this.fillTransaction(input)));
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

  private async sendTransaction(input: TxInput): Promise<unknown> {
    const from = this.account ?? await this.wallet.getAddress('ethereum', this.path);
    const key = `nuwallet:${this.chainId}:${from.toLowerCase()}`;
    return await withLocalSendLock(key, async () => {
      const run = async () => {
        const chainNonce = toBig(await this.rpc('eth_getTransactionCount', [from, 'pending']));
        const localNonce = nextNonces.get(key) ?? 0n;
        const nonce = input.nonce ?? (chainNonce > localNonce ? chainNonce : localNonce);
        const filled = await this.fillTransaction({ ...input, nonce }, from);
        const rawTx = await this.signFilled(filled);
        try {
          const hash = await this.rpc('eth_sendRawTransaction', [hex(rawTx)]);
          nextNonces.set(key, filled.tx.nonce + 1n);
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

  /**
   * 빠진 필드를 노드에서 채우고, legacy 로 낼지 EIP-1559 로 낼지 정한다.
   *
   * 고르는 규칙은 이렇다. 호출자가 `gasPrice` 를 줬거나 `type` 을 0 으로 박았으면
   * 그 뜻대로 legacy 로 낸다. `maxFeePerGas` 계열을 줬거나 `type` 이 2 면 1559 다.
   * 아무 말도 없으면 최신 블록에 `baseFeePerGas` 가 있는지 보고 정한다 — 런던
   * 이후 체인이면 1559 가 수수료를 덜 낸다.
   */
  private async fillTransaction(t: TxInput, knownFrom?: string): Promise<Filled> {
    const from = knownFrom ?? this.account ?? await this.wallet.getAddress('ethereum', this.path);
    const nonce = toBig(t.nonce ?? await this.rpc('eth_getTransactionCount', [from, 'pending']));
    const gas = toBig(t.gas ?? t.gasLimit ?? await this.rpc('eth_estimateGas', [{ ...t, from }]));
    const to = t.to ? fromHex(t.to) : new Uint8Array();
    const value = toBig(t.value ?? '0x0');
    const data = t.data ? fromHex(t.data) : new Uint8Array();
    const type = t.type === undefined || t.type === null ? undefined : Number(toBig(t.type));

    const wants1559 = type === 2
      || (type === undefined && (t.maxFeePerGas !== undefined || t.maxPriorityFeePerGas !== undefined));
    const wantsLegacy = type === 0 || type === 1 || (type === undefined && t.gasPrice !== undefined);

    if (!wantsLegacy && (wants1559 || await this.chainHasBaseFee())) {
      const { maxFeePerGas, maxPriorityFeePerGas } = await this.fill1559Fees(t);
      return {
        kind: 'eip1559',
        tx: {
          chainId: this.chainId, nonce, maxPriorityFeePerGas, maxFeePerGas,
          gas, to, value, data, accessList: encodeAccessList(t.accessList ?? []),
        },
      };
    }

    const gasPrice = toBig(t.gasPrice ?? await this.rpc('eth_gasPrice', []));
    return { kind: 'legacy', tx: { nonce, gasPrice, gas, to, value, data } };
  }

  /** 최신 블록에 baseFeePerGas 가 있으면 런던 이후 체인이다. 한 번만 묻는다. */
  private async chainHasBaseFee(): Promise<boolean> {
    if (this.baseFeeChain === null) this.baseFeeChain = (await fetchBaseFee(this.rpcCall)) !== null;
    return this.baseFeeChain;
  }

  /** 호출자가 준 값은 그대로 쓰고, 빠진 것만 fees.ts 의 규칙으로 채운다. */
  private async fill1559Fees(t: TxInput): Promise<{ maxFeePerGas: bigint; maxPriorityFeePerGas: bigint }> {
    if (t.maxFeePerGas !== undefined && t.maxPriorityFeePerGas !== undefined) {
      return { maxFeePerGas: toBig(t.maxFeePerGas), maxPriorityFeePerGas: toBig(t.maxPriorityFeePerGas) };
    }
    const priority = t.maxPriorityFeePerGas !== undefined
      ? toBig(t.maxPriorityFeePerGas)
      : await fetchPriorityFee(this.rpcCall);
    if (t.maxFeePerGas !== undefined) {
      return { maxFeePerGas: toBig(t.maxFeePerGas), maxPriorityFeePerGas: priority };
    }
    const base = await fetchBaseFee(this.rpcCall) ?? 0n;
    return { maxFeePerGas: maxFeeFor(base, priority), maxPriorityFeePerGas: priority };
  }

  private readonly rpcCall = (method: string, params: unknown[]) => this.rpc(method, params);

  /** 채워진 트랜잭션을 기기로 보내 서명받고, 네트워크에 낼 바이트로 만든다. */
  private async signFilled(f: Filled): Promise<Uint8Array> {
    if (f.kind === 'eip1559') {
      const unsigned = encode1559Unsigned(f.tx);
      const sig = await this.wallet.signTransaction(
        'ethereum', this.path, unsigned, undefined, this.cb());
      return encode1559Signed(f.tx, sig.v, fromHex(sig.r), fromHex(sig.s));
    }
    const unsigned = encodeLegacyUnsigned(f.tx, this.chainId);
    const sig = await this.wallet.signTransaction(
      'ethereum', this.path, unsigned, this.chainId, this.cb());
    return encodeLegacySigned(f.tx, sig.v, fromHex(sig.r), fromHex(sig.s));
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



export { rlpEncode, toChecksumAddress, ProviderRpcError };
