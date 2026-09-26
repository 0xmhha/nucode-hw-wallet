import { ApprovalMeter } from '../../lib/ApprovalMeter';
import type { Approval } from '../../lib/approval';
import s from '../dapp.module.css';

export function AccountCard(p: {
  account: string; balance: string; nonce: string; deviceName: string;
  symbol: string; hasRpc: boolean; approval: Approval | null;
}) {
  return (
    <section className={s.card}>
      <h2>2. 계정</h2>
      <p className={s.hint}>주소는 보드가 파생합니다. 조회에는 버튼 승인이 필요 없습니다.</p>
      <dl className={s.kv}>
        <div><dt>주소</dt><dd>{p.account || '미연결'}</dd></div>
        <div><dt>잔액</dt><dd>{p.balance ? `${p.balance} ${p.symbol}` : (p.hasRpc ? '—' : 'RPC 미설정')}</dd></div>
        <div><dt>nonce</dt><dd>{p.nonce || '—'}</dd></div>
        <div><dt>기기</dt><dd>{p.deviceName || '—'}</dd></div>
      </dl>
      {p.approval && <ApprovalMeter approval={p.approval} s={s} />}
    </section>
  );
}
