/* 사람의 승인.
 *
 * 서명·WIPE·PIN 변경은 기기에서 버튼을 눌러야 실행된다. 두 가지 방식이 있고
 * 섞으면 안 된다 (docs/protocol.md §8).
 *
 *   랜덤 챌린지  기기가 매번 새로 뽑아 LED 로 보여준다. "사람이 여기 있다"의 증명
 *   PIN 입력     사용자가 아는 고정 시퀀스. LED 로 보여주지 않는다
 *
 * LED 표시와 타임아웃도 여기서 돌린다 — 승인 절차의 일부라서다.            */
#include "internal.h"
#include "store.h"
#include "crypto/bip39.h"
#include "crypto/bip32.h"
#include "crypto/ecdsa.h"
#include "crypto/keccak.h"
#include <string.h>

/* ── LED 타이밍 ─────────────────────────────────────────────────────────── */
#define SHOW_ON_MS   450
#define SHOW_GAP_MS  200
#define IDLE_BLINK   1000

/* ── 챌린지 ─────────────────────────────────────────────────────────────── */

void nu_request_clear(nu_wallet *w) {
    memset(&w->req, 0, sizeof w->req);
    nu_leds(w, 0);
}

void nu_remember(nu_wallet *w, uint32_t id, uint16_t status,
                     const uint8_t *payload, uint8_t len) {
    w->last_id = id;
    w->last_status = status;
    w->last_len = len;
    if (len) memcpy(w->last_payload, payload, len);
}

/* 챌린지를 건 요청을 끝낸다. 결과 이벤트까지 여기서 보낸다. */
void nu_challenge_finish(nu_wallet *w, uint16_t status,
                   const uint8_t *sig64, int recid) {
    const uint32_t id = w->req.id;
    const uint8_t cmd = w->req.cmd;

    if (cmd == NU_CMD_SIGN_TX || cmd == NU_CMD_SIGN_PERSONAL || cmd == NU_CMD_SIGN_TYPED) {
        /* docs/protocol.md §6: REQUEST_ID(4) ‖ STATUS(2) ‖ SIG_LEN(1) ‖ SIGNATURE
         * 길이 접두사가 있어야 체인마다 다른 서명 길이를 SDK 가 구분할 수 있다. */
        uint8_t p[7 + 65];
        nu_be32(p, id);
        nu_be16(p + 4, status);
        size_t n = 6;
        if (status == NU_SW_OK && sig64) {
            p[6] = 65;                      /* Ethereum: r ‖ s ‖ recid */
            memcpy(p + 7, sig64, 64);
            p[71] = (uint8_t)recid;
            n = 72;
        }
        nu_remember(w, id, status, p + 6, (uint8_t)(n - 6));
        nu_event(w, NU_EVT_SIGN_RESULT, p, n);
    } else {
        uint8_t p[7];
        nu_be32(p, id);
        p[4] = cmd;
        nu_be16(p + 5, status);
        nu_remember(w, id, status, NULL, 0);
        nu_event(w, NU_EVT_REQUEST_RESULT, p, sizeof p);
    }
    nu_request_clear(w);
    nu_emit_state(w);
}

static void progress(nu_wallet *w) {
    uint8_t p[6];
    nu_be32(p, w->req.id);
    p[4] = w->req.pos;
    p[5] = w->req.attempts;
    nu_event(w, NU_EVT_CHALLENGE_PROGRESS, p, sizeof p);
}

/* 랜덤 챌린지 또는 PIN 입력을 시작하고 PENDING 을 응답한다. */
void nu_challenge_start(nu_wallet *w, uint8_t cmd, uint8_t kind, uint8_t steps) {
    nu_request *r = &w->req;
    r->cmd = cmd;
    r->id = ++w->next_id;
    r->kind = kind;
    r->seq_len = steps;
    r->pos = 0;
    r->attempts = (kind == 1) ? w->pin_attempts : NU_CHALLENGE_ATTEMPTS;
    r->started_ms = w->hal->millis(w->hal->ctx);
    r->phase_ms = r->started_ms;
    r->shown = 0;
    r->showing = (kind == 0) ? 1 : 0;

    if (kind == 0) {
        /* 랜덤 시퀀스. 난수가 없으면 서명을 승인받을 방법이 없으므로 실패시킨다. */
        uint8_t rnd[NU_PIN_MAX];
        if (!w->hal->random(rnd, steps, w->hal->ctx)) {
            nu_request_clear(w);
            nu_reply(w, NU_SW_DEVICE_ERROR, NULL, 0);
            return;
        }
        for (uint8_t i = 0; i < steps; i++) r->seq[i] = (uint8_t)(rnd[i] % NU_BUTTON_COUNT);
        memset(rnd, 0, sizeof rnd);
    }

    uint8_t p[6];
    nu_be32(p, r->id);
    nu_be16(p + 4, NU_SW_PENDING);
    nu_reply(w, NU_SW_PENDING, p, 4);          /* 응답 페이로드는 REQUEST_ID 4바이트 */

    uint8_t e[6];
    nu_be32(e, r->id);
    e[4] = steps;
    e[5] = cmd;
    nu_event(w, NU_EVT_CHALLENGE_STARTED, e, 6);
    nu_emit_state(w);
}

/* 챌린지가 통과했을 때 실제 작업을 수행한다. */
static void challenge_approved(nu_wallet *w) {
    nu_request *r = &w->req;

    switch (r->cmd) {
    case NU_CMD_SIGN_TX:
    case NU_CMD_SIGN_PERSONAL:
    case NU_CMD_SIGN_TYPED: {
        uint8_t priv[32], sig[64];
        int recid = 0;
        if (!nu_derive(w, r->path, r->depth, NULL, NULL, NULL, priv)) {
            nu_challenge_finish(w, NU_SW_DEVICE_ERROR, NULL, 0);
            return;
        }
        const int ok = ecdsa_sign_secp256k1(priv, r->hash, sig, &recid);
        memset(priv, 0, sizeof priv);
        nu_challenge_finish(w, ok ? NU_SW_OK : NU_SW_DEVICE_ERROR, ok ? sig : NULL, recid);
        memset(sig, 0, sizeof sig);
        return;
    }
    case NU_CMD_WIPE:
        w->hal->store_erase(w->hal->ctx);
        memset(w->rec, 0, sizeof w->rec);
        w->rec_len = 0;
        w->tmp_count = 0;
        nu_session_lock(w);
        nu_challenge_finish(w, NU_SW_OK, NULL, 0);
        return;

    case NU_CMD_SET_PIN: {
        const int ok = nu_persist(w, w->words, w->word_count, r->new_pin, r->new_pin_len);
        if (ok) {
            memcpy(w->pin, r->new_pin, NU_PIN_MAX);
            w->pin_len = r->new_pin_len;
            w->pin_attempts = NU_PIN_ATTEMPTS;   /* nu_persist() 가 만수로 새로 봉인했다 */
        }
        nu_challenge_finish(w, ok ? NU_SW_OK : NU_SW_DEVICE_ERROR, NULL, 0);
        return;
    }
    default:
        nu_challenge_finish(w, NU_SW_DEVICE_ERROR, NULL, 0);
        return;
    }
}

/* PIN 입력이 끝났다. 레코드를 열어보는 것으로 검증한다 —
 * 자리별로 비교하지 않으므로 부분 일치 오라클이 생기지 않는다. */
static void pin_entered(nu_wallet *w) {
    nu_request *r = &w->req;
    uint16_t words[24];
    uint8_t count = 0;

    if (nu_store_open(w->rec, w->rec_len, r->seq, r->seq_len, words, &count)) {
        memcpy(w->words, words, sizeof words);
        w->word_count = count;
        memcpy(w->pin, r->seq, NU_PIN_MAX);
        w->pin_len = r->seq_len;
        if (w->pin_attempts != NU_PIN_ATTEMPTS) nu_persist_tries(w, NU_PIN_ATTEMPTS);
        const int ok = nu_seed_from_words(w, words, count, r->passphrase);
        w->unlocked = ok;
        memset(words, 0, sizeof words);
        nu_challenge_finish(w, ok ? NU_SW_OK : NU_SW_DEVICE_ERROR, NULL, 0);
        return;
    }
    memset(words, 0, sizeof words);

    r->pos = 0;
    if (w->pin_attempts) nu_persist_tries(w, (uint8_t)(w->pin_attempts - 1));
    r->attempts = w->pin_attempts;
    progress(w);
    if (w->pin_attempts == 0) {
        nu_wipe_record(w);                 /* 소진 — 시드를 지운다 */
        nu_challenge_finish(w, NU_SW_CHALLENGE_FAILED, NULL, 0);
    } else {
        r->started_ms = w->hal->millis(w->hal->ctx);   /* 다음 시도에 시간을 다시 준다 */
    }
}

void nu_wallet_button(nu_wallet *w, uint8_t idx) {
    nu_request *r = &w->req;
    if (!r->cmd || idx >= NU_BUTTON_COUNT) return;
    if (r->showing) return;                 /* 시퀀스를 보여주는 동안은 무시 */

    if (r->kind == 1) {                     /* PIN 입력 */
        if (r->pos < r->seq_len) r->seq[r->pos++] = idx;
        progress(w);
        if (r->pos >= r->seq_len) pin_entered(w);
        return;
    }

    if (idx == r->seq[r->pos]) {
        r->pos++;
        progress(w);
        if (r->pos >= r->seq_len) challenge_approved(w);
        return;
    }

    /* 틀렸다 — 처음부터 다시. */
    r->pos = 0;
    if (r->attempts) r->attempts--;
    progress(w);
    if (r->attempts == 0) { nu_challenge_finish(w, NU_SW_CHALLENGE_FAILED, NULL, 0); return; }
    r->showing = 1;                         /* 시퀀스를 다시 보여준다 */
    r->shown = 0;
    r->phase_ms = w->hal->millis(w->hal->ctx);
}

/* ── 주기 처리 ──────────────────────────────────────────────────────────── */

void nu_wallet_tick(nu_wallet *w, uint32_t now_ms) {
    nu_request *r = &w->req;

    if (!r->cmd) {
        /* 대기 중 표시: 지갑 없음 = 소등, 잠김 = LED0 점멸, 해제 = LED3 점등 */
        if (!w->rec_len)      nu_leds(w, 0);
        else if (!w->unlocked) nu_leds(w, ((now_ms / IDLE_BLINK) & 1) ? 0x01 : 0x00);
        else                   nu_leds(w, 0x08);
        return;
    }

    if (now_ms - r->started_ms > NU_CHALLENGE_TIMEOUT_MS) {
        nu_challenge_finish(w, NU_SW_CHALLENGE_TIMEOUT, NULL, 0);
        return;
    }

    if (r->kind == 1) {
        /* PIN 입력 — 시퀀스를 보여주지 않는다. 입력한 자릿수만 표시한다. */
        uint8_t m = 0;
        for (uint8_t i = 0; i < r->pos && i < 4; i++) m |= (uint8_t)(1u << i);
        nu_leds(w, m ? m : (((now_ms / 500) & 1) ? 0x0f : 0x00));
        return;
    }

    if (r->showing) {
        /* 시퀀스를 한 자리씩 보여준다: 켜짐 450ms, 꺼짐 200ms. */
        const uint32_t elapsed = now_ms - r->phase_ms;
        if (r->shown >= r->seq_len) {
            if (elapsed >= SHOW_GAP_MS) { r->showing = 0; nu_leds(w, 0); }
            else nu_leds(w, 0);
            return;
        }
        if (elapsed < SHOW_ON_MS) {
            nu_leds(w, (uint8_t)(1u << r->seq[r->shown]));
        } else if (elapsed < SHOW_ON_MS + SHOW_GAP_MS) {
            nu_leds(w, 0);
        } else {
            r->shown++;
            r->phase_ms = now_ms;
        }
        return;
    }

    /* 입력 대기 — 맞게 누른 개수만큼 LED 를 켠다. */
    uint8_t m = 0;
    for (uint8_t i = 0; i < r->pos && i < 4; i++) m |= (uint8_t)(1u << i);
    nu_leds(w, m);
}
