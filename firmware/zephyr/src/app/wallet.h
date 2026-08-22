#ifndef NUWALLET_WALLET_H
#define NUWALLET_WALLET_H
/* 지갑 코어 — docs/protocol.md 의 명령을 전부 처리하는 상태 기계.
 *
 * 플랫폼 의존이 전혀 없다. BLE 도 Zephyr 도 모르고 nu_hal 만 안다.
 * 그래서 firmware/test 에서 보드 없이 전체 흐름을 돌려볼 수 있다.          */
#include <stdint.h>
#include <stddef.h>
#include "hal.h"
#include "protocol.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  cmd;                    /* 0 이면 진행 중인 요청 없음 */
    uint32_t id;
    uint8_t  kind;                   /* 0 = 랜덤 챌린지, 1 = PIN 입력 */
    uint8_t  seq[NU_PIN_MAX];        /* kind 0: 기대 시퀀스 / kind 1: 입력 버퍼 */
    uint8_t  seq_len, pos, attempts;
    uint32_t started_ms, phase_ms;
    uint8_t  shown;                  /* LED 로 보여준 단계 수 */
    uint8_t  showing;                /* 1 이면 아직 시퀀스 표시 중 */

    uint8_t  hash[32];               /* 서명 대상 */
    uint32_t path[8];
    uint8_t  depth;

    uint8_t  new_pin[NU_PIN_MAX];    /* SET_PIN */
    uint8_t  new_pin_len;

    char     passphrase[NU_MAX_PASSPHRASE + 1];  /* UNLOCK */
} nu_request;

typedef struct {
    const nu_hal *hal;
    char     name[24];

    uint8_t  rec[NU_STORE_MAX];      /* 플래시에 있는 레코드 사본 */
    size_t   rec_len;                /* 0 이면 지갑 없음 */

    int      unlocked;
    uint8_t  seed[64];
    uint16_t words[24];              /* 잠금 해제 상태에서만 유효. 재봉인에 필요 */
    uint8_t  word_count;
    uint8_t  pin[NU_PIN_MAX];
    uint8_t  pin_len;
    uint8_t  pin_attempts;           /* 남은 PIN 시도 */

    uint16_t tmp_words[24];          /* SETUP_GENERATE 후 CONFIRM 전 */
    uint8_t  tmp_count;

    nu_request req;

    uint32_t last_id;                /* GET_RESULT 용 마지막 결과 */
    uint16_t last_status;
    uint8_t  last_payload[66];
    uint8_t  last_len;

    uint32_t next_id;
    uint8_t  led_mask;
} nu_wallet;

/* 부팅 시 한 번. 플래시에서 레코드를 읽어 들인다. */
void nu_wallet_init(nu_wallet *w, const nu_hal *hal, const char *device_name);

/* 조립이 끝난 요청 메시지 (CMD ‖ LEN ‖ PAYLOAD) 하나를 처리한다.
 * 응답은 hal->send 로 나간다. */
void nu_wallet_handle(nu_wallet *w, const uint8_t *msg, size_t len);

/* 프레이밍 오류를 코어에 알린다 (전송 계층이 호출). */
void nu_wallet_framing_error(nu_wallet *w, uint16_t status);

/* 버튼 눌림. idx 는 0..3. */
void nu_wallet_button(nu_wallet *w, uint8_t idx);

/* 주기 호출 — 타임아웃과 LED 표시를 진행시킨다. 10ms 주기면 충분하다. */
void nu_wallet_tick(nu_wallet *w, uint32_t now_ms);

/* BLE 연결이 끊겼을 때. 진행 중 요청을 버리고 다시 잠근다. */
void nu_wallet_disconnected(nu_wallet *w);

uint8_t nu_wallet_flags(const nu_wallet *w);

#ifdef __cplusplus
}
#endif
#endif
