export interface DeviceInfo {
  protocolVersion: number;
  firmware: string;          // "0.1"
  name: string;
  initialized: boolean;
  locked: boolean;
}

export interface DeviceState {
  initialized: boolean;
  locked: boolean;
  challengeActive: boolean;
  requestId: number;
}

export interface AccountInfo {
  address: string;           // 0x… 체크섬 적용
  publicKey: string;         // 0x04…  비압축 65바이트
  chainCode: string;
  path: string;
}

/** 기기가 서명을 승인받는 동안 SDK 가 호출하는 콜백. */
export interface ChallengeCallbacks {
  /** 기기가 LED 로 시퀀스를 표시하기 시작했다. steps 는 눌러야 하는 횟수. */
  onStart?: (info: { requestId: number; steps: number; command: number }) => void;
  /** 사용자가 한 단계 맞게 눌렀다. */
  onProgress?: (info: { requestId: number; step: number; attemptsLeft: number }) => void;
}

export interface Signature {
  r: string;                 // 0x… 32바이트
  s: string;                 // 0x… 32바이트
  recid: number;             // 0..3
  /** legacy(EIP-155) 또는 personal_sign 규칙으로 계산된 v */
  v: number;
  /** r ‖ s ‖ v(1바이트) 를 이어붙인 65바이트 */
  serialized: string;
}

export interface SignOptions extends ChallengeCallbacks {
  /** 승인 대기 타임아웃(ms). 기본 70초 — 기기 챌린지 제한이 60초다. */
  timeoutMs?: number;
  signal?: AbortSignal;
}

/** EIP-1193 최소 인터페이스 */
export interface Eip1193Provider {
  request(args: { method: string; params?: unknown[] | object }): Promise<unknown>;
  on(event: string, listener: (...args: any[]) => void): void;
  removeListener(event: string, listener: (...args: any[]) => void): void;
}
