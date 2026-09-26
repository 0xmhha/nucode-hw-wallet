import { PIN } from '@/sdk/src/index';
import s from '../setup.module.css';

const BUTTON_LABELS = ['1', '2', '3', '4'];   // 화면 표기는 1~4, 프로토콜 값은 0~3

export function PinCard(p: { canChange: boolean; busy: boolean; onChange: () => void }) {
  return (
    <section className={s.card}>
      <h2>PIN · 버튼 6번</h2>
      <p className={s.hint}>
        PIN 은 <strong>보드에서만</strong> 입력합니다. 이 화면은 값을 받지 않습니다 —
        웹이 PIN 을 알면 &ldquo;호스트가 감염돼도 기기를 못 연다&rdquo;는 목적이 무너지기 때문입니다.
        아래 패드는 보드의 버튼 배치를 보여줄 뿐, 누르는 곳은 보드입니다.
      </p>
      <div className={s.pad}>
        {BUTTON_LABELS.map((label, i) => (
          <div key={label} className={s.key} aria-hidden="true">
            {label}<small>SW{i}</small>
          </div>
        ))}
      </div>
      <p className={s.hint}>
        PIN 은 {PIN.LEN}자리 고정입니다. 바꿀 때는 보드에서 새 값을 누르고, LED 가 번갈아
        깜빡이면 오타를 잡기 위해 같은 값을 한 번 더 누릅니다. PIN 은 없앨 수 없습니다.
      </p>
      <div className={s.buttons}>
        <button className={`${s.btn} ${s.primary}`} onClick={p.onChange} disabled={p.busy || !p.canChange}>
          PIN 변경
        </button>
      </div>
      <p className={s.hint}>
        PIN 을 잊었다면 보드에서 <strong>공장 초기화</strong>를 합니다 — 버튼 1과 4를 5초 동안
        함께 누른 뒤, LED 카운트다운이 끝나면 손을 떼고 버튼 2 → 3 을 누릅니다.
        지갑과 페어링 정보가 지워지며, 복구 문구로만 되살릴 수 있습니다.
      </p>
    </section>
  );
}
