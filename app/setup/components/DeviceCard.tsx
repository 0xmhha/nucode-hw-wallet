import type { LockState } from '@/sdk/src/index';
import s from '../setup.module.css';

export function DeviceCard(p: {
  connected: boolean; deviceName: string; state: LockState | null; address: string;
  busy: boolean; onConnect: () => void; onRefresh: () => void; onLock: () => void;
}) {
  const { state } = p;
  const locked = !!state?.locked;
  return (
    <section className={s.card}>
      <h2>기기 상태</h2>
      <p className={s.hint}>보드가 알려주는 값 그대로입니다.</p>
      <dl className={s.kv}>
        <div><dt>연결</dt><dd className={p.connected ? s.yes : s.no}>{p.connected ? p.deviceName || '연결됨' : '미연결'}</dd></div>
        <div><dt>지갑</dt><dd className={state?.initialized ? s.yes : s.no}>{state ? (state.initialized ? '있음' : '없음') : '—'}</dd></div>
        <div><dt>잠금</dt><dd className={locked ? s.no : s.yes}>{state ? (locked ? '잠김' : '해제됨') : '—'}</dd></div>
        <div><dt>PIN</dt><dd>{state ? (state.hasPin ? `설정됨 · ${state.pinLength}자리` : '없음') : '—'}</dd></div>
        <div><dt>남은 시도</dt><dd>{state ? state.attemptsLeft : '—'}</dd></div>
        <div><dt>주소</dt><dd>{p.address || '—'}</dd></div>
      </dl>
      <div className={s.buttons}>
        <button className={`${s.btn} ${s.primary}`} onClick={p.onConnect} disabled={p.busy || p.connected}>
          {p.connected ? '연결됨' : 'Bluetooth 연결'}
        </button>
        <button className={s.btn} onClick={p.onRefresh} disabled={p.busy || !p.connected}>상태 갱신</button>
        <button className={s.btn} onClick={p.onLock} disabled={p.busy || !p.connected || locked}>잠그기</button>
      </div>
    </section>
  );
}
