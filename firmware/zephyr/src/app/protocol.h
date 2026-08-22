#ifndef NUWALLET_PROTOCOL_H
#define NUWALLET_PROTOCOL_H
/* NuWallet BLE 프로토콜 v1 — 상수.
 *
 * docs/protocol.md 와 sdk/src/protocol.ts 에 1:1 대응한다.
 * 셋 중 하나를 바꾸면 나머지 둘도 바꿔야 한다.                              */
#include <stdint.h>

#define NU_PROTOCOL_VERSION 1
#define NU_FW_MAJOR         0
#define NU_FW_MINOR         1

#define NU_MAX_MESSAGE      2048

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
#define NU_SW_NOT_INITIALIZED     0x6982
#define NU_SW_ALREADY_INITIALIZED 0x6983
#define NU_SW_USER_REJECTED       0x6985
#define NU_SW_BAD_PARAM           0x6a80
#define NU_SW_TOO_LARGE           0x6a84
#define NU_SW_UNKNOWN_CMD         0x6d00
#define NU_SW_DEVICE_ERROR        0x6f00
#define NU_SW_FRAMING_ERROR       0x6f01

/* GET_STATE / GET_VERSION 의 FLAGS */
#define NU_FLAG_INITIALIZED      0x01
#define NU_FLAG_LOCKED           0x02
#define NU_FLAG_CHALLENGE_ACTIVE 0x04
#define NU_FLAG_HAS_PIN          0x08

/* 정책 */
#define NU_CHALLENGE_STEPS     4      /* 서명 승인 시 눌러야 하는 횟수 */
#define NU_CHALLENGE_TIMEOUT_MS 60000
#define NU_CHALLENGE_ATTEMPTS  3      /* 서명 챌린지 재시도 */
#define NU_PIN_ATTEMPTS        5      /* PIN 재시도 */
#define NU_PIN_MIN             4
#define NU_PIN_MAX             8
#define NU_BUTTON_COUNT        4
#define NU_MAX_PASSPHRASE      64

/* GATT UUID — 바이트 순서는 BLE 규약대로 리틀엔디언(역순)이다. */
#define NU_UUID128_SERVICE \
    0x01,0x00,0x00,0x00,0x00,0x00,0x54,0x45,0x4c,0x4c,0x61,0x77,0x00,0x40,0x75,0x6e
#define NU_UUID128_RX \
    0x02,0x00,0x00,0x00,0x00,0x00,0x54,0x45,0x4c,0x4c,0x61,0x77,0x00,0x40,0x75,0x6e
#define NU_UUID128_TX \
    0x03,0x00,0x00,0x00,0x00,0x00,0x54,0x45,0x4c,0x4c,0x61,0x77,0x00,0x40,0x75,0x6e

#endif
