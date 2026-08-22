#line 1 "/Users/0xtopaz/work/github/0xmhha/nucode-hw-wallet/firmware/nuwallet/src/ui/challenge.h"
#ifndef NUWALLET_CHALLENGE_H
#define NUWALLET_CHALLENGE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* 버튼 승인.
 *
 * 서명은 사용자가 버튼 1 -> 2 -> 3 -> 4를 누른다. WIPE/PIN 변경 같은 관리
 * 작업은 임의 LED 시퀀스를 유지한다. 서명의 순서를 고정해 사용성을 높였으므로,
 * 임의 LED 시퀀스가 제공하던 관찰 확인 효과는 없다. */

#define CH_STEPS_DEFAULT 4
#define CH_MAX_STEPS     8
#define CH_MAX_ATTEMPTS  3
#define CH_TIMEOUT_MS    60000

typedef enum {
    CH_IDLE = 0,
    CH_SHOWING,      /* LED 로 시퀀스 표시 중 */
    CH_WAITING,      /* 사용자 입력 대기 */
    CH_APPROVED,
    CH_REJECTED,     /* 시도 횟수 소진 */
    CH_TIMEOUT,
} ch_state;

typedef struct {
    ch_state state;
    uint8_t  seq[CH_MAX_STEPS];
    uint8_t  steps;
    uint8_t  pos;            /* 지금까지 맞게 누른 개수 */
    uint8_t  attempts_left;
    uint32_t started_ms;
    uint32_t request_id;
    uint8_t  cmd;            /* 승인 대상 명령 */
    /* LED 표시 진행 상태 */
    uint8_t  show_idx;
    uint32_t show_ms;
    uint8_t  show_on;
} challenge_t;

/* 관리 작업의 임의 챌린지에 사용할 암호학적 난수 콜백(TRNG). */
typedef void (*ch_rng_fn)(uint8_t *buf, uint16_t len);

void ch_init(challenge_t *c, ch_rng_fn rng);
void ch_start(challenge_t *c, uint32_t request_id, uint8_t cmd, uint8_t steps);
void ch_cancel(challenge_t *c);

/* 버튼(0..3) 눌림 이벤트. 진행 상태가 바뀌면 1 을 반환한다. */
int  ch_button(challenge_t *c, uint8_t btn);

/* 주기적 호출. 타임아웃과 LED 표시를 진행시킨다.
 * led_out 에 4개 LED 의 on/off 를 채운다. 상태가 바뀌면 1 반환. */
int  ch_task(challenge_t *c, uint32_t now_ms, uint8_t led_out[4]);

#ifdef __cplusplus
}
#endif
#endif
