'use client';
/**
 * 기기 승인 진행 상황. 설정 화면과 DApp 이 같이 쓴다.
 *
 * 무엇을 누르라고 안내할지는 기기가 `EVT_CHALLENGE_STARTED` 의 KIND 로 알려 준다.
 * 웹이 명령을 보고 짐작하지 않는다. 짐작하면 펌웨어가 승인 방식을 바꿨을 때
 * 화면만 옛 안내를 띄운다 (v1 의 "1 → 2 → 3 → 4" 문구가 그렇게 남아 있었다).
 */
import { useCallback, useState } from 'react';
import type { ChallengeCallbacks } from '@/sdk/src/index';

/* docs/protocol.md 의 APPROVAL 값. SDK 가 index 에서 APPROVAL 을 내보내면
 * 이 상수를 지우고 그것을 쓴다. */
export const APPROVAL_KIND = { CONFIRM: 0, PIN: 1, PIN_NEW: 2 } as const;

export type Approval = { kind: number; steps: number; done: number };

/** 승인이 시작될 때 사용자에게 보여 줄 한 줄. */
export function approvalPrompt(kind: number, steps: number): string {
  switch (kind) {
    case APPROVAL_KIND.CONFIRM:
      return '보드에서 켜져 있는 LED 옆의 버튼을 한 번 누르세요.';
    case APPROVAL_KIND.PIN:
      return `보드에서 PIN ${steps}자리를 누르세요. LED 는 누른 개수만 보여 줍니다.`;
    case APPROVAL_KIND.PIN_NEW:
      return `보드에서 새 PIN ${steps}자리를 누르고, LED 가 번갈아 깜빡이면 같은 값을 한 번 더 누르세요.`;
    default:
      return `보드에서 승인하세요 (${steps}단계).`;
  }
}

/** 진행 표시 제목. PIN 을 새로 정할 때는 두 번째 입력으로 넘어가면 done 이 0 으로 돌아간다. */
export function approvalTitle(a: Approval): string {
  return a.kind === APPROVAL_KIND.CONFIRM ? '보드에서 승인을 기다립니다' : 'PIN 입력을 기다립니다';
}

/**
 * 승인 상태와 SDK 에 넘길 콜백을 만든다.
 * `onPrompt` 는 승인이 시작될 때 안내 문구를 받는다. 화면의 상태 줄에 쓴다.
 */
export function useApproval(onPrompt: (text: string) => void) {
  const [approval, setApproval] = useState<Approval | null>(null);

  const callbacks = useCallback((): ChallengeCallbacks => ({
    onStart: (i) => {
      setApproval({ kind: i.kind, steps: i.steps, done: 0 });
      onPrompt(approvalPrompt(i.kind, i.steps));
    },
    onProgress: (i) => setApproval((a) => (a ? { ...a, done: i.step } : a)),
  }), [onPrompt]);

  const clear = useCallback(() => setApproval(null), []);

  return { approval, callbacks, clear };
}
