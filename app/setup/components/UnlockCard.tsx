import { ApprovalMeter } from '../../lib/ApprovalMeter';
import type { Approval } from '../../lib/approval';
import s from '../setup.module.css';

export function UnlockCard(p: {
  passphrase: string; onPassphrase: (v: string) => void;
  canUnlock: boolean; busy: boolean; onUnlock: () => void;
  approval: Approval | null;
}) {
  return (
    <section className={s.card}>
      <h2>잠금 해제</h2>
      <p className={s.hint}>
        PIN 은 보드에서 직접 누릅니다. 패스프레이즈는 보드에 저장되지 않고 이번
        세션에만 쓰입니다 — 값이 다르면 다른 지갑이 됩니다.
      </p>
      <label className={s.field}>
        <span>BIP-39 패스프레이즈 (선택)</span>
        <input type="password" value={p.passphrase} onChange={(e) => p.onPassphrase(e.target.value)}
               placeholder="비워 두면 패스프레이즈 없음" autoComplete="off" />
      </label>
      <div className={s.buttons}>
        <button className={`${s.btn} ${s.primary}`} onClick={p.onUnlock} disabled={p.busy || !p.canUnlock}>
          잠금 해제
        </button>
      </div>
      {p.approval && <ApprovalMeter approval={p.approval} s={s} />}
    </section>
  );
}
