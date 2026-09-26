import s from '../dapp.module.css';

export function MessageCard(p: {
  message: string; onMessage: (v: string) => void;
  busy: boolean; connected: boolean;
  onPersonalSign: () => void; onSignTypedData: () => void;
}) {
  return (
    <section className={s.card}>
      <h2>4. 메시지 서명</h2>
      <p className={s.hint}>
        <code>personal_sign</code> (EIP-191). 보드가 접두사를 붙여 해시하므로 호스트가
        준 해시를 그대로 서명하지 않습니다.
      </p>
      <label className={s.field}>
        <span>메시지</span>
        <textarea value={p.message} onChange={(e) => p.onMessage(e.target.value)} />
      </label>
      <div className={s.buttons}>
        <button className={s.btn} onClick={p.onPersonalSign} disabled={p.busy || !p.connected}>
          메시지 서명
        </button>
      </div>

      <p className={s.hint}>
        <code>eth_signTypedData_v4</code> (EIP-712). 아래 버튼은 ERC-2612
        <code>Permit</code> 을 위 폼의 값으로 채워 서명합니다. 보드는 32바이트 해시
        두 개만 받으므로, <strong>무엇에 서명하는지는 이 화면에서만 볼 수 있습니다.</strong>
      </p>
      <div className={s.buttons}>
        <button className={s.btn} onClick={p.onSignTypedData} disabled={p.busy || !p.connected}>
          Permit 서명 (EIP-712)
        </button>
      </div>
    </section>
  );
}
