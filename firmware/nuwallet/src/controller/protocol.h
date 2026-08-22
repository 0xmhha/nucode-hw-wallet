#line 1 "/Users/0xtopaz/work/github/0xmhha/nucode-hw-wallet/firmware/nuwallet/src/controller/protocol.h"
#ifndef NUWALLET_PROTOCOL_H
#define NUWALLET_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* docs/protocol.md 와 sdk/src/protocol.ts 에 1:1 대응한다.
 * 셋 중 하나를 바꾸면 나머지 둘도 바꿔야 한다. */

#define PROTO_VERSION   1
#define PROTO_MAX_MSG   2048
#define FW_MAJOR        0
#define FW_MINOR        1

/* 프레이밍 태그 */
#define TAG_MESSAGE     0x05
#define TAG_EVENT       0x06

/* 명령 */
#define CMD_GET_VERSION     0x01
#define CMD_GET_STATE       0x02
#define CMD_SETUP_GENERATE  0x10
#define CMD_SETUP_CONFIRM   0x11
#define CMD_SETUP_RESTORE   0x12
#define CMD_WIPE            0x13
/* 경로를 받는 명령은 모두 [CHAIN:1][DEPTH:1][PATH...] 를 쓴다 — docs/protocol.md §3.5.
 * 체인별 별도 명령(구 0x21 GET_CHAIN_ADDRESS, 0x33 SIGN_SOLANA)은 폐기했다. */
#define CMD_GET_ADDRESS     0x20
#define CMD_GET_CHAIN_ADDRESS 0x21
#define CMD_SIGN_TX         0x30
#define CMD_SIGN_PERSONAL   0x31
#define CMD_SIGN_TYPED      0x32
#define CMD_SIGN_SOLANA     0x33
#define CMD_GET_RESULT      0x40
#define CMD_CANCEL          0x41

/* 이벤트 */
#define EVT_CHALLENGE_STARTED   0xA0
#define EVT_CHALLENGE_PROGRESS  0xA1
#define EVT_SIGN_RESULT         0xA2
#define EVT_DEVICE_STATE        0xA3

/* 상태 코드 */
#define SW_OK                   0x9000
#define SW_PENDING              0x9001
#define SW_LOCKED               0x5515
#define SW_CHALLENGE_TIMEOUT    0x6501
#define SW_CHALLENGE_FAILED     0x6502
#define SW_NOT_INITIALIZED      0x6982
#define SW_ALREADY_INITIALIZED  0x6983
#define SW_USER_REJECTED        0x6985
#define SW_BAD_PARAM            0x6A80
#define SW_UNSUPPORTED_CHAIN    0x6A81
#define SW_TOO_LARGE            0x6A84
#define SW_UNKNOWN_CMD          0x6D00
#define SW_DEVICE_ERROR         0x6F00
#define SW_FRAMING_ERROR        0x6F01

/* 상태 플래그 */
#define FLAG_INITIALIZED    0x01
#define FLAG_LOCKED         0x02
#define FLAG_CHALLENGE      0x04

#define CHAIN_ETHEREUM      0x01
#define CHAIN_SOLANA        0x02

/* 응답을 쓰는 콜백. 프로토콜 계층은 전송 수단을 모른다. */
typedef void (*proto_send_fn)(uint8_t tag, const uint8_t *payload, uint16_t len);

void proto_init(proto_send_fn send);

/* 조립이 끝난 요청 메시지 하나를 처리한다. */
void proto_handle(const uint8_t *msg, uint16_t len);

/* 챌린지가 끝났을 때 메인 루프가 호출한다. */
void proto_challenge_resolved(uint32_t request_id, uint16_t status);
void proto_emit_challenge_started(uint32_t request_id, uint8_t steps, uint8_t cmd);
void proto_emit_challenge_progress(uint32_t request_id, uint8_t step, uint8_t attempts);

/* 보류 중인 요청이 있으면 해당 명령을 실행하고 결과를 이벤트로 보낸다. */
void proto_execute_pending(void);
uint32_t proto_pending_id(void);
uint8_t  proto_pending_cmd(void);
void     proto_clear_pending(void);

#ifdef __cplusplus
}
#endif
#endif
