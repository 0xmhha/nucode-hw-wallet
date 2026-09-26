import s from '../setup.module.css';

export function ImportCard(p: {
  mnemonic: string; onMnemonic: (v: string) => void;
  connected: boolean; initialized: boolean; busy: boolean;
  onImport: () => void; onWipe: () => void;
}) {
  const words = p.mnemonic.trim() ? p.mnemonic.trim().split(/\s+/u).length : 0;
  return (
    <section className={s.card}>
      <h2>기존 니모닉 가져오기</h2>
      <p className={s.hint}>
        보드에 지갑이 없을 때만 됩니다. 체크섬은 보드가 검증합니다.
        오프라인에서 만든 니모닉을 넣는 편이 <code>새로 생성</code>보다 안전합니다 —
        PC 가 니모닉 전체를 보지 않기 때문입니다.
      </p>
      <label className={s.field}>
        <span>니모닉 12 / 15 / 18 / 21 / 24 단어 {words > 0 && `· 지금 ${words}개`}</span>
        <textarea value={p.mnemonic} onChange={(e) => p.onMnemonic(e.target.value)}
                  placeholder="abandon abandon … about" autoComplete="off" spellCheck={false} />
      </label>
      <div className={s.buttons}>
        <button className={`${s.btn} ${s.primary}`} onClick={p.onImport}
                disabled={p.busy || !p.connected || !p.mnemonic.trim() || p.initialized}>
          가져오기
        </button>
        <button className={`${s.btn} ${s.danger}`} onClick={p.onWipe}
                disabled={p.busy || !p.connected || !p.initialized}>
          보드 초기화 (WIPE)
        </button>
      </div>
      {p.initialized && (
        <p className={s.hint} style={{ marginTop: 14, marginBottom: 0 }}>
          이미 지갑이 있습니다. 다른 니모닉을 넣으려면 먼저 초기화하세요.
        </p>
      )}
    </section>
  );
}
