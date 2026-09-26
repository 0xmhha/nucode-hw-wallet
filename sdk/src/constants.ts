/**
 * NuWallet BLE 프로토콜 v2 — 기기와 주고받는 값의 목록.
 *
 * docs/protocol.md 와 펌웨어의 core/protocol.h 에 1:1 대응한다. 셋 중 하나를
 * 바꾸면 나머지 둘도 바꿔야 한다. scripts/check-protocol.py 가 이 파일을 읽어
 * 셋이 같은지 대조한다.
 */

export const SERVICE_UUID = '6e754000-7761-4c4c-4554-000000000001';
export const RX_UUID      = '6e754000-7761-4c4c-4554-000000000002'; // 호스트 -> 기기
export const TX_UUID      = '6e754000-7761-4c4c-4554-000000000003'; // 기기 -> 호스트

export const PROTOCOL_VERSION = 2;
export const MAX_MESSAGE      = 2048;

export const TAG = { MESSAGE: 0x05, EVENT: 0x06 } as const;

export const CMD = {
  GET_VERSION:     0x01,
  GET_STATE:       0x02,
  SETUP_GENERATE:  0x10,
  SETUP_CONFIRM:   0x11,
  SETUP_RESTORE:   0x12,
  WIPE:            0x13,
  SET_PIN:         0x14,
  UNLOCK:          0x15,
  LOCK:            0x16,
  GET_ADDRESS:     0x20,
  GET_CHAIN_ADDRESS: 0x21,
  SIGN_TX:         0x30,
  SIGN_PERSONAL:   0x31,
  SIGN_TYPED:      0x32,
  SIGN_SOLANA:     0x33,
  GET_RESULT:      0x40,
  CANCEL:          0x41,
} as const;

export const EVT = {
  CHALLENGE_STARTED:  0xa0,
  CHALLENGE_PROGRESS: 0xa1,
  SIGN_RESULT:        0xa2,
  DEVICE_STATE:       0xa3,
  REQUEST_RESULT:     0xa4,
} as const;

export const SW = {
  OK:                  0x9000,
  PENDING:             0x9001,
  LOCKED:              0x5515,
  CHALLENGE_TIMEOUT:   0x6501,
  CHALLENGE_FAILED:    0x6502,
  PIN_REQUIRED:        0x6503,
  PIN_MISMATCH:        0x6504,
  NOT_INITIALIZED:     0x6982,
  ALREADY_INITIALIZED: 0x6983,
  USER_REJECTED:       0x6985,
  BAD_PARAM:           0x6a80,
  UNSUPPORTED_CHAIN:   0x6a81,
  TOO_LARGE:           0x6a84,
  UNKNOWN_CMD:         0x6d00,
  DEVICE_ERROR:        0x6f00,
  FRAMING_ERROR:       0x6f01,
} as const;

const SW_TEXT: Record<number, string> = {
  [SW.OK]: '성공',
  [SW.PENDING]: '접수됨 — 기기에서 승인을 기다리는 중',
  [SW.LOCKED]: '기기가 잠겨 있습니다',
  [SW.CHALLENGE_TIMEOUT]: '승인 시간이 초과되었습니다',
  [SW.CHALLENGE_FAILED]: '버튼 시퀀스가 틀렸습니다',
  [SW.PIN_REQUIRED]: 'PIN 이 설정되어 있습니다. 먼저 잠금을 해제하세요',
  [SW.PIN_MISMATCH]: '두 번 입력한 PIN 이 다릅니다',
  [SW.NOT_INITIALIZED]: '지갑이 아직 생성되지 않았습니다',
  [SW.ALREADY_INITIALIZED]: '이미 지갑이 있습니다. 먼저 초기화하세요',
  [SW.USER_REJECTED]: '사용자가 거부했습니다',
  [SW.BAD_PARAM]: '요청 형식이 잘못되었습니다',
  [SW.UNSUPPORTED_CHAIN]: '기기가 지원하지 않는 체인입니다',
  [SW.TOO_LARGE]: '메시지가 너무 큽니다',
  [SW.UNKNOWN_CMD]: '기기가 모르는 명령입니다 (펌웨어 버전 확인)',
  [SW.DEVICE_ERROR]: '기기 내부 오류',
  [SW.FRAMING_ERROR]: '프레이밍 오류',
};

export class WalletError extends Error {
  constructor(readonly status: number, message?: string) {
    super(message ?? SW_TEXT[status] ?? `알 수 없는 상태 0x${status.toString(16)}`);
    this.name = 'WalletError';
  }
  get isUserRejection(): boolean {
    return this.status === SW.USER_REJECTED ||
           this.status === SW.CHALLENGE_FAILED ||
           this.status === SW.CHALLENGE_TIMEOUT;
  }
}

/** CHAIN_PATH 의 첫 바이트. docs/protocol.md §3.5 */
export const CHAIN = { ETHEREUM: 0x01, SOLANA: 0x02 } as const;

/** GET_STATE / GET_VERSION 의 FLAGS 비트. docs/protocol.md §5 */
export const FLAG = {
  INITIALIZED:      0x01,
  LOCKED:           0x02,
  CHALLENGE_ACTIVE: 0x04,
  HAS_PIN:          0x08,
} as const;

/** PIN 정책. docs/protocol.md §8 — v2 부터 길이가 6으로 고정이다. */
export const PIN = { LEN: 6, BUTTONS: 4 } as const;

/**
 * 승인 종류. `EVT_CHALLENGE_STARTED` 의 KIND 바이트로 온다.
 *
 * 호스트가 무엇을 띄울지 스스로 추측하면 안 된다 — 기기가 알려준다.
 */
export const APPROVAL = {
  /** 서명 확인 — 켜진 LED 하나를 1회 누른다. */
  CONFIRM: 0,
  /** PIN 입력 — 6회. LED 는 누른 개수만 보여준다. */
  PIN: 1,
  /** PIN 설정 — 6회 입력한 뒤 같은 값을 한 번 더. */
  PIN_NEW: 2,
} as const;
export type ApprovalKind = typeof APPROVAL[keyof typeof APPROVAL];
