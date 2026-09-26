import s from '../dapp.module.css';

export function TransactionCard(p: {
  to: string; onTo: (v: string) => void;
  value: string; onValue: (v: string) => void;
  data: string; onData: (v: string) => void;
  symbol: string; busy: boolean; connected: boolean; hasRpc: boolean;
  onSignOnly: () => void; onSend: () => void;
}) {
  return (
    <section className={s.card}>
      <h2>3. 트랜잭션 전송</h2>
      <p className={s.hint}>
        nonce 와 수수료는 RPC 에서 조회합니다. 체인이 EIP-1559 를 지원하면 type 2 로,
        아니면 legacy 로 보냅니다. 단순 ETH 전송은 gas 21,000 을 씁니다.
        전송 전에 <strong>수수료 상한까지 포함해</strong> 잔액이 충분한지 확인합니다.
        보드는 서명되지 않은 RLP 원문을 받아 <strong>해시를 직접 계산</strong>합니다.
      </p>
      <label className={s.field}>
        <span>받는 주소</span>
        <input value={p.to} onChange={(e) => p.onTo(e.target.value)} placeholder="0x… (비우면 컨트랙트 생성)" />
      </label>
      <div className={s.row}>
        <label className={s.field}>
          <span>보낼 양 ({p.symbol})</span>
          <input value={p.value} onChange={(e) => p.onValue(e.target.value)} inputMode="decimal" />
        </label>
      </div>
      <label className={s.field}>
        <span>DATA (선택)</span>
        <textarea value={p.data} onChange={(e) => p.onData(e.target.value)} placeholder="0x…" />
      </label>
      <div className={s.buttons}>
        <button className={s.btn} onClick={p.onSignOnly} disabled={p.busy || !p.connected}>서명만 (전송 안 함)</button>
        <button className={`${s.btn} ${s.primary}`} onClick={p.onSend} disabled={p.busy || !p.connected || !p.hasRpc}>
          서명하고 전송
        </button>
      </div>
    </section>
  );
}
