#line 1 "/Users/0xtopaz/work/github/0xmhha/nucode-hw-wallet/firmware/nuwallet/src/ui/challenge.c"
#include "challenge.h"
#include <string.h>

/* LED 표시 타이밍 (ms) */
#define SHOW_ON_MS    420
#define SHOW_OFF_MS   180
#define SHOW_GAP_MS   700    /* 시퀀스 한 바퀴 끝나고 다시 보여주기 전 */

static ch_rng_fn g_rng = 0;

void ch_init(challenge_t *c, ch_rng_fn rng) {
    memset(c, 0, sizeof *c);
    g_rng = rng;
    c->state = CH_IDLE;
}

void ch_start(challenge_t *c, uint32_t request_id, uint8_t cmd, uint8_t steps) {
    if (steps == 0 || steps > CH_MAX_STEPS) steps = CH_STEPS_DEFAULT;
    memset(c, 0, sizeof *c);
    c->steps = steps;
    c->request_id = request_id;
    c->cmd = cmd;
    c->attempts_left = CH_MAX_ATTEMPTS;

    if (cmd >= 0x30 && cmd <= 0x33) {
        /* 서명 사용성 우선: 물리 버튼 1 -> 2 -> 3 -> 4.
         * 내부 버튼 번호는 0부터 시작하므로 0,1,2,3으로 저장한다. */
        for (int i = 0; i < steps; i++) c->seq[i] = (uint8_t)(i & 0x03);
    } else {
        /* WIPE/PIN 변경 같은 관리 작업은 기존 임의 챌린지를 유지한다. */
        uint8_t r[CH_MAX_STEPS];
        if (g_rng) g_rng(r, steps); else memset(r, 0, steps);
        for (int i = 0; i < steps; i++) c->seq[i] = (uint8_t)(r[i] & 0x03);
        memset(r, 0, sizeof r);
    }

    c->state = CH_SHOWING;
    c->show_idx = 0; c->show_on = 0; c->show_ms = 0;
}

void ch_cancel(challenge_t *c) {
    memset(c, 0, sizeof *c);
    c->state = CH_IDLE;
}

int ch_button(challenge_t *c, uint8_t btn) {
    if (c->state != CH_WAITING && c->state != CH_SHOWING) return 0;
    if (btn > 3) return 0;
    /* 표시 중에 눌러도 받아준다 — 이미 외운 사용자를 기다리게 할 이유가 없다. */
    c->state = CH_WAITING;

    if (btn == c->seq[c->pos]) {
        c->pos++;
        if (c->pos >= c->steps) c->state = CH_APPROVED;
        return 1;
    }
    /* 틀렸다 — 처음부터 다시 */
    c->pos = 0;
    if (c->attempts_left) c->attempts_left--;
    if (c->attempts_left == 0) c->state = CH_REJECTED;
    else { c->state = CH_SHOWING; c->show_idx = 0; c->show_on = 0; c->show_ms = 0; }
    return 1;
}

int ch_task(challenge_t *c, uint32_t now_ms, uint8_t led_out[4]) {
    led_out[0] = led_out[1] = led_out[2] = led_out[3] = 0;
    if (c->state == CH_IDLE || c->state == CH_APPROVED ||
        c->state == CH_REJECTED || c->state == CH_TIMEOUT) return 0;

    if (c->started_ms == 0) c->started_ms = now_ms;
    if (now_ms - c->started_ms > CH_TIMEOUT_MS) {
        c->state = CH_TIMEOUT;
        return 1;
    }

    if (c->state == CH_SHOWING) {
        if (c->show_ms == 0) c->show_ms = now_ms;
        const uint32_t dt = now_ms - c->show_ms;
        if (c->show_idx >= c->steps) {
            /* 한 바퀴 다 보여줬다 — 잠깐 쉬고 입력을 기다린다 */
            if (dt >= SHOW_GAP_MS) { c->state = CH_WAITING; return 1; }
            return 0;
        }
        if (!c->show_on) {
            if (dt < SHOW_ON_MS) { led_out[c->seq[c->show_idx]] = 1; return 0; }
            c->show_on = 1; c->show_ms = now_ms; return 0;
        }
        if (dt >= SHOW_OFF_MS) { c->show_on = 0; c->show_idx++; c->show_ms = now_ms; }
        return 0;
    }

    /* CH_WAITING — 지금까지 맞게 누른 개수만큼 LED 를 켜 둔다 */
    for (int i = 0; i < c->pos && i < 4; i++) led_out[i] = 1;
    return 0;
}
