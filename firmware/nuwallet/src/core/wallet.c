/* 생명주기 — 부팅과 연결 해제.
 *
 * 프로토콜 처리는 commands.c, 승인은 challenge.c, 시드는 session.c,
 * 송신은 wire.c 에 있다. 경계는 internal.h 참고.                            */
#include "internal.h"
#include "store.h"
#include <string.h>

/* ── 공장 초기화 ─────────────────────────────────────────────────────────
 *
 * BLE 명령이 아니다. PIN 을 잊으면 0x13 WIPE 는 쓸 수 없다 — 잠금 해제가
 * 필요하기 때문이다. 그때 보드를 되살리는 유일한 길이 이것이다.
 *
 * 인증을 요구하지 않아도 안전하다. 공장 초기화는 잠금을 우회하는 게 아니라
 * 시드를 **파괴**한다. 자금을 훔치는 경로가 되지 않는다.
 *
 * 주머니에서 눌리는 것만 막으면 된다. 그래서 2단계다.
 *   1) 대각선 두 버튼을 5초 동안 함께 누른다 — LED 가 하나씩 꺼지며 카운트다운
 *   2) 손을 뗀 뒤 확인 시퀀스를 누른다
 * 한 번의 홀드만으로 지우면 사고가 난다. 되돌릴 수 없다.                    */

static void factory_erase(nu_wallet *w) {
    nu_wipe_record(w);
    /* 본딩까지 지워야 한다. 안 지우면 호스트가 옛 페어링 키를 계속 써서
     * 다음 연결이 알림 구독에서 끊긴다 — 사용자는 원인을 찾을 수 없다. */
    if (w->hal->bonds_erase) w->hal->bonds_erase(w->hal->ctx);
    w->fac_stage = 0;
    w->fac_pos = 0;
    w->words_pending_count = 0;
    memset(w->words_pending, 0, sizeof w->words_pending);
    nu_emit_state(w);
}

int nu_factory_button(nu_wallet *w, uint8_t idx) {
    if (w->fac_stage != 2) return 0;

    const uint8_t want = (w->fac_pos == 0) ? NU_FACTORY_CONFIRM_A : NU_FACTORY_CONFIRM_B;
    if (idx != want) {                      /* 틀리면 확인을 취소한다 */
        w->fac_stage = 0;
        w->fac_pos = 0;
        return 1;
    }
    if (++w->fac_pos >= 2) factory_erase(w);
    return 1;
}

int nu_factory_tick(nu_wallet *w, uint32_t now_ms) {
    if (!w->hal->buttons) return 0;         /* 버튼 상태를 못 읽으면 비활성 */

    const uint8_t held = w->hal->buttons(w->hal->ctx);
    const uint8_t combo = (uint8_t)((1u << NU_FACTORY_BTN_A) | (1u << NU_FACTORY_BTN_B));
    const int both = ((held & combo) == combo);

    if (w->fac_stage == 2) {
        /* 확인 대기 — 4개를 천천히 깜빡여 "지금 지워진다" 를 알린다. */
        if (nu_elapsed(now_ms, w->fac_hold_ms) > NU_CHALLENGE_TIMEOUT_MS) {
            w->fac_stage = 0; w->fac_pos = 0; return 0;
        }
        nu_leds(w, ((now_ms / 250) & 1) ? 0x0f : 0x00);
        return 1;
    }

    if (!both) {                            /* 손을 뗐다 */
        if (w->fac_stage == 1) {
            const uint32_t held_ms = nu_elapsed(now_ms, w->fac_hold_ms);
            if (held_ms >= NU_FACTORY_HOLD_MS) {
                w->fac_stage = 2;           /* 카운트다운 완료 — 확인을 받는다 */
                w->fac_pos = 0;
                w->fac_hold_ms = now_ms;
                return 1;
            }
            w->fac_stage = 0;               /* 너무 일찍 뗐다 */
        }
        return 0;
    }

    if (w->fac_stage == 0) {                /* 홀드 시작 */
        w->fac_stage = 1;
        w->fac_hold_ms = now_ms;
    }

    /* 카운트다운: 남은 시간만큼 LED 를 켜 둔다 (4 -> 3 -> 2 -> 1 -> 0). */
    const uint32_t held_ms = nu_elapsed(now_ms, w->fac_hold_ms);
    const uint32_t left = (held_ms >= NU_FACTORY_HOLD_MS) ? 0
                        : (NU_FACTORY_HOLD_MS - held_ms);
    const uint8_t lit = (uint8_t)((left * 4 + NU_FACTORY_HOLD_MS - 1) / NU_FACTORY_HOLD_MS);
    uint8_t m = 0;
    for (uint8_t i = 0; i < lit && i < 4; i++) m |= (uint8_t)(1u << i);
    nu_leds(w, m);
    return 1;
}

void nu_wallet_disconnected(nu_wallet *w) {
    if (w->req.cmd) {
        /* 결과를 보낼 상대가 없으므로 이벤트 없이 조용히 버린다. */
        nu_remember(w, w->req.id, NU_SW_USER_REJECTED, NULL, 0);
        nu_request_clear(w);
    }
    nu_session_lock(w);
    w->tmp_count = 0;
    memset(w->tmp_words, 0, sizeof w->tmp_words);
}

void nu_wallet_init(nu_wallet *w, const nu_hal *hal, const char *device_name) {
    memset(w, 0, sizeof *w);
    w->hal = hal;
    if (device_name) {
        strncpy(w->name, device_name, sizeof w->name - 1);
    }
    w->pin_attempts = NU_PIN_ATTEMPTS;
    /* 0 으로 두면 부팅 후 5분이 지난 시점에는 "이미 만료" 로 읽힌다. */
    w->last_active_ms = hal->millis(hal->ctx);

    uint8_t raw[NU_STORE_MAX];
    const size_t n = hal->store_read(raw, sizeof raw, hal->ctx);
    if (n && nu_store_valid(raw, n)) {
        memcpy(w->rec, raw, n);
        w->rec_len = n;
        /* 카운터는 전원을 꺼도 살아 있어야 한다. 재부팅으로 초기화되면
         * PIN 시도 제한이 아무 의미가 없다. */
        w->pin_attempts = nu_store_tries(w->rec, w->rec_len);
    }
    memset(raw, 0, sizeof raw);
}
