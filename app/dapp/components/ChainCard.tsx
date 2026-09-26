import { PRESETS } from '../presets';
import s from '../dapp.module.css';

export function ChainCard(p: {
  presetIdx: number; onPreset: (i: number) => void;
  chainId: string; onChainId: (v: string) => void;
  path: string; onPath: (v: string) => void;
  rpcUrl: string; onRpcUrl: (v: string) => void;
  busy: boolean; connected: boolean;
  onConnect: () => void; onReload: () => void;
}) {
  return (
    <section className={s.card}>
      <h2>1. 체인 설정</h2>
      <p className={s.hint}>
        chainId 는 EIP-155 서명에 그대로 들어갑니다. 잘못 넣으면 다른 체인에서
        재생 가능한 서명이 됩니다.
      </p>
      <label className={s.field}>
        <span>프리셋</span>
        <select value={p.presetIdx} onChange={(e) => p.onPreset(Number(e.target.value))}>
          {PRESETS.map((x, i) => <option key={x.chainId} value={i}>{x.name} · {x.chainId}</option>)}
        </select>
      </label>
      <div className={s.row}>
        <label className={s.field}>
          <span>CHAIN ID</span>
          <input value={p.chainId} onChange={(e) => p.onChainId(e.target.value)} inputMode="numeric" />
        </label>
        <label className={s.field}>
          <span>파생 경로</span>
          <input value={p.path} onChange={(e) => p.onPath(e.target.value)} />
        </label>
      </div>
      <label className={s.field}>
        <span>RPC URL</span>
        <input value={p.rpcUrl} onChange={(e) => p.onRpcUrl(e.target.value)}
               placeholder="https://… (서명 외 호출을 여기로 넘깁니다)" />
      </label>
      <div className={s.buttons}>
        <button className={`${s.btn} ${s.primary}`} onClick={p.onConnect} disabled={p.busy || p.connected}>
          {p.connected ? '연결됨' : '지갑 연결'}
        </button>
        <button className={s.btn} onClick={p.onReload} disabled={p.busy || !p.connected}>상태 갱신</button>
      </div>
    </section>
  );
}
