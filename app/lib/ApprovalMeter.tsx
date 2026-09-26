import { approvalTitle, type Approval } from './approval';

/** 누른 개수를 점으로 보여 준다. 스타일은 페이지의 CSS 모듈을 받는다. */
export function ApprovalMeter({ approval, s }: {
  approval: Approval;
  s: Record<string, string>;
}) {
  return (
    <div className={s.approval}>
      <strong>{approvalTitle(approval)} ({approval.done}/{approval.steps})</strong>
      <div className={s.dots}>
        {Array.from({ length: approval.steps }, (_, i) => (
          <span key={i} className={`${s.dot} ${i < approval.done ? s.on : ''}`} />
        ))}
      </div>
    </div>
  );
}
