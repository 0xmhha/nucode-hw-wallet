/**
 * 기기 상태 코드를 EIP-1193 / EIP-1474 오류로 바꾼다.
 *
 * DApp 은 "사용자가 취소(4001)"와 "기기가 잠김(4100)"과 "펌웨어가 모르는
 * 명령(4200)"에 서로 다르게 반응해야 한다. 상태 코드를 그대로 올리면 구분할
 * 방법이 없다.
 */
import { SW, WalletError } from './protocol.js';

/** EIP-1193 §5 의 오류. `code` 로 DApp 이 분기한다. */
export class ProviderRpcError extends Error {
  constructor(readonly code: number, message: string, readonly data?: unknown) {
    super(message);
    this.name = 'ProviderRpcError';
  }
}

/* 기기 상태 코드 → EIP-1193 / EIP-1474 오류 코드.
 *
 * 4001 사용자 거부 · 4100 권한 없음 · 4200 지원하지 않는 메서드
 * -32602 잘못된 파라미터 · -32603 내부 오류
 *
 * 승인 시간 초과와 버튼 시퀀스 실패도 4001 로 본다. DApp 입장에서는 셋 다
 * "사용자가 승인하지 않았다"이고, 재시도 UI 가 같기 때문이다. */
const PROVIDER_CODE: Record<number, number> = {
  [SW.USER_REJECTED]:       4001,
  [SW.CHALLENGE_FAILED]:    4001,
  [SW.CHALLENGE_TIMEOUT]:   4001,
  [SW.LOCKED]:              4100,
  [SW.PIN_REQUIRED]:        4100,
  [SW.PIN_MISMATCH]:        4100,
  [SW.NOT_INITIALIZED]:     4100,
  [SW.ALREADY_INITIALIZED]: 4100,
  [SW.UNKNOWN_CMD]:         4200,
  [SW.UNSUPPORTED_CHAIN]:   4200,
  [SW.BAD_PARAM]:          -32602,
  [SW.TOO_LARGE]:          -32602,
  [SW.DEVICE_ERROR]:       -32603,
  [SW.FRAMING_ERROR]:      -32603,
};

export function toProviderError(e: unknown): unknown {
  if (e instanceof ProviderRpcError) return e;
  if (e instanceof WalletError) {
    const code = PROVIDER_CODE[e.status] ?? -32603;
    return new ProviderRpcError(code, e.message, e);
  }
  return e;
}
