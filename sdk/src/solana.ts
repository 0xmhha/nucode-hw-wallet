import { NuWallet } from './client.js';
import { DEFAULT_PATHS, fromHex } from './protocol.js';
import type { ChallengeCallbacks } from './types.js';

export interface SolanaTransactionLike {
  serializeMessage(): Uint8Array;
  addSignature(publicKey: unknown, signature: Uint8Array): void;
}

export interface SolanaWeb3Like {
  PublicKey: new (address: string) => unknown;
}

export interface SolanaAdapterOptions extends ChallengeCallbacks {
  path?: string;
}

/** @solana/web3.js Transaction과 호환되는 최소 지갑 어댑터. */
export class NuWalletSolanaAdapter {
  public publicKey: unknown | null = null;
  private address = '';

  constructor(
    readonly wallet: NuWallet,
    readonly web3: SolanaWeb3Like,
    readonly opts: SolanaAdapterOptions = {},
  ) {}

  get connected(): boolean { return this.wallet.isConnected && this.publicKey !== null; }
  get path(): string { return this.opts.path ?? DEFAULT_PATHS.solana; }

  async connect(): Promise<{ publicKey: unknown }> {
    if (!this.wallet.isConnected) await this.wallet.connect();
    this.address = await this.wallet.getAddress('solana', this.path);
    this.publicKey = new this.web3.PublicKey(this.address);
    return { publicKey: this.publicKey };
  }

  async disconnect(): Promise<void> {
    await this.wallet.disconnect();
    this.publicKey = null;
    this.address = '';
  }

  async signTransaction<T extends SolanaTransactionLike>(tx: T): Promise<T> {
    if (!this.publicKey) await this.connect();
    const result = await this.wallet.signTransaction(
      'solana', this.path, tx.serializeMessage(), undefined,
      { onStart: this.opts.onStart, onProgress: this.opts.onProgress });
    tx.addSignature(this.publicKey, fromHex(result.serialized));
    return tx;
  }

  async signAllTransactions<T extends SolanaTransactionLike>(txs: T[]): Promise<T[]> {
    const out: T[] = [];
    for (const tx of txs) out.push(await this.signTransaction(tx));
    return out;
  }
}
