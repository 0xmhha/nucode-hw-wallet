/* 생명주기 — 부팅과 연결 해제.
 *
 * 프로토콜 처리는 commands.c, 승인은 challenge.c, 시드는 session.c,
 * 송신은 wire.c 에 있다. 경계는 internal.h 참고.                            */
#include "internal.h"
#include "store.h"
#include <string.h>

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
