import s from '../dapp.module.css';

export type LogLine = { dir: 'in' | 'out' | 'err'; text: string };

export function LogPanel({ log }: { log: LogLine[] }) {
  return (
    <section className={`${s.card} ${s.wide}`}>
      <h2>주고받은 것</h2>
      <p className={s.hint}>DApp ↔ SDK ↔ 보드 사이에 실제로 오간 호출입니다.</p>
      <pre className={s.log}>
        {log.length === 0 ? '아직 아무것도 하지 않았습니다.' : log.map((l, i) => (
          <span key={i} className={l.dir === 'err' ? s.err : l.dir === 'in' ? s.out : undefined}>
            {l.text}{'\n'}
          </span>
        ))}
      </pre>
    </section>
  );
}
