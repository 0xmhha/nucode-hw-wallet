#ifndef NUWALLET_PROTOCOL_H
#define NUWALLET_PROTOCOL_H
/* NuWallet BLE 프로토콜 v1 — 상수.
 *
 * docs/protocol.md 와 sdk/src/protocol.ts 에 1:1 대응한다.
 * 셋 중 하나를 바꾸면 나머지 둘도 바꿔야 한다.                              */
#include <stdint.h>

#define NU_PROTOCOL_VERSION 2
#define NU_FW_MAJOR         0
#define NU_FW_MINOR         1

#define NU_MAX_MESSAGE      2048
/* Solana 트랜잭션의 최대 크기(1232). Ed25519 는 원문을 서명하므로 해시로
 * 줄이지 못하고 그대로 들고 있어야 한다. */
#define NU_MAX_SIGN_PAYLOAD 1232

/* 프레이밍 태그 */
#define NU_TAG_MESSAGE 0x05
#define NU_TAG_EVENT   0x06

/* 명령 */
#define NU_CMD_GET_VERSION    0x01
#define NU_CMD_GET_STATE      0x02
#define NU_CMD_SETUP_GENERATE 0x10
#define NU_CMD_SETUP_CONFIRM  0x11
#define NU_CMD_SETUP_RESTORE  0x12
#define NU_CMD_WIPE           0x13
#define NU_CMD_SET_PIN        0x14
#define NU_CMD_UNLOCK         0x15
#define NU_CMD_LOCK           0x16
#define NU_CMD_GET_ADDRESS    0x20
#define NU_CMD_GET_CHAIN_ADDRESS 0x21
#define NU_CMD_SIGN_TX        0x30
#define NU_CMD_SIGN_PERSONAL  0x31
#define NU_CMD_SIGN_TYPED     0x32
#define NU_CMD_SIGN_SOLANA    0x33

#define NU_CHAIN_ETHEREUM 0x01
#define NU_CHAIN_SOLANA   0x02
#define NU_CMD_GET_RESULT     0x40
#define NU_CMD_CANCEL         0x41

/* 이벤트 */
#define NU_EVT_CHALLENGE_STARTED  0xa0
#define NU_EVT_CHALLENGE_PROGRESS 0xa1
#define NU_EVT_SIGN_RESULT        0xa2
#define NU_EVT_DEVICE_STATE       0xa3
#define NU_EVT_REQUEST_RESULT     0xa4

/* 상태 코드 */
#define NU_SW_OK                  0x9000
#define NU_SW_PENDING             0x9001
#define NU_SW_LOCKED              0x5515
#define NU_SW_CHALLENGE_TIMEOUT   0x6501
#define NU_SW_CHALLENGE_FAILED    0x6502
#define NU_SW_PIN_REQUIRED        0x6503
#define NU_SW_PIN_MISMATCH        0x6504
#define NU_SW_NOT_INITIALIZED     0x6982
#define NU_SW_ALREADY_INITIALIZED 0x6983
#define NU_SW_USER_REJECTED       0x6985
#define NU_SW_BAD_PARAM           0x6a80
#define NU_SW_UNSUPPORTED_CHAIN   0x6a81
#define NU_SW_TOO_LARGE           0x6a84
#define NU_SW_UNKNOWN_CMD         0x6d00
#define NU_SW_DEVICE_ERROR        0x6f00
#define NU_SW_FRAMING_ERROR       0x6f01

/* GET_STATE / GET_VERSION 의 FLAGS */
#define NU_FLAG_INITIALIZED      0x01
#define NU_FLAG_LOCKED           0x02
#define NU_FLAG_CHALLENGE_ACTIVE 0x04
#define NU_FLAG_HAS_PIN          0x08

/* ── 승인 종류 (EVT_CHALLENGE_STARTED 의 KIND) ────────────────────────────
 *
 * v2 에서 서명 승인이 바뀌었다. v1 은 매번 랜덤 시퀀스를 LED 로 보여주고 그대로
 * 누르게 했다. v2 는 PIN 으로 세션을 열고, 서명 하나당 **버튼 한 번**만 받는다.
 *
 * 잃은 것을 분명히 해 둔다: 랜덤 시퀀스는 "사용자가 지금 기기를 보고 있다"를
 * 증명했다. 한 번 누르기는 "사람이 여기 있다"까지만 증명한다. 그래서 켜진 LED
 * 하나를 누르게 해서 최소한의 주시(注視)는 남긴다. SECURITY.md §2 참고. */
#define NU_APPROVAL_CONFIRM    0      /* 서명 확인 — 켜진 LED 를 1회 누른다 */
#define NU_APPROVAL_PIN        1      /* PIN 입력 — 6회. LED 는 개수만 보여준다 */
#define NU_APPROVAL_PIN_NEW    2      /* PIN 설정 — 6회 입력 후 6회 재입력 */

/* 정책 */
#define NU_PIN_LEN             6      /* 고정. 길이가 고정이라 "입력 끝" 신호가 필요 없다 */
#define NU_CONFIRM_STEPS       1      /* 서명 확인에 누르는 횟수 */
#define NU_CHALLENGE_TIMEOUT_MS 60000
#define NU_CHALLENGE_ATTEMPTS  3      /* 서명 확인 재시도 */
#define NU_PIN_ATTEMPTS        5      /* PIN 재시도. 소진하면 지갑을 지운다 */
#define NU_SESSION_IDLE_MS     300000 /* 5분 동안 조용하면 다시 잠근다 */
#define NU_BUTTON_COUNT        4
#define NU_MAX_PASSPHRASE      64

/* 공장 초기화 — BLE 명령이 아니다. PIN 을 잊었을 때의 유일한 복구 경로라
 * 오프라인이어야 한다. 주머니에서 눌리지 않도록 2단계로 받는다.
 *   1) 대각선 두 버튼(0, 3)을 5초 동안 함께 누른다 — LED 가 카운트다운
 *   2) 손을 뗀 뒤 확인 시퀀스(1, 2)를 누른다
 * 시드와 **BLE 본딩 키를 함께** 지운다. 본딩을 남기면 호스트가 옛 키를 계속
 * 써서 연결이 끊긴다 — 사용자는 원인을 찾을 수 없다. */
#define NU_FACTORY_HOLD_MS     5000
#define NU_FACTORY_BTN_A       0
#define NU_FACTORY_BTN_B       3
#define NU_FACTORY_CONFIRM_A   1
#define NU_FACTORY_CONFIRM_B   2

/* GATT UUID — 바이트 순서는 BLE 규약대로 리틀엔디언(역순)이다. */
#define NU_UUID128_SERVICE \
    0x01,0x00,0x00,0x00,0x00,0x00,0x54,0x45,0x4c,0x4c,0x61,0x77,0x00,0x40,0x75,0x6e
#define NU_UUID128_RX \
    0x02,0x00,0x00,0x00,0x00,0x00,0x54,0x45,0x4c,0x4c,0x61,0x77,0x00,0x40,0x75,0x6e
#define NU_UUID128_TX \
    0x03,0x00,0x00,0x00,0x00,0x00,0x54,0x45,0x4c,0x4c,0x61,0x77,0x00,0x40,0x75,0x6e

#endif
