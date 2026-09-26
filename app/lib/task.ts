'use client';
/**
 * 버튼 하나가 하는 일을 감싼다. 도는 동안 다른 버튼을 막고, 실패하면 이유를
 * 한 줄로 만들어 넘기고, 끝나면 승인 표시를 치운다.
 */
import { useCallback, useState } from 'react';
import { WalletError } from '@/sdk/src/index';

export function describeError(e: unknown): string {
  if (e instanceof WalletError) return `${e.message} (0x${e.status.toString(16)})`;
  if (e instanceof Error) return e.message;
  return String(e);
}

export function useTask(opts: {
  onBegin?: (label: string) => void;
  onError: (label: string, message: string) => void;
  onSettled?: () => void;
}) {
  const { onBegin, onError, onSettled } = opts;
  const [busy, setBusy] = useState(false);

  const run = useCallback(async (label: string, task: () => Promise<void>) => {
    setBusy(true);
    onBegin?.(label);
    try {
      await task();
    } catch (e) {
      onError(label, describeError(e));
    } finally {
      setBusy(false);
      onSettled?.();
    }
  }, [onBegin, onError, onSettled]);

  return { busy, run };
}
