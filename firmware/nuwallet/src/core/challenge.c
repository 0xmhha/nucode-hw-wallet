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
#include "../crypto/bip39.h"
#include "../crypto/bip32.h"
#include "../crypto/ecdsa.h"
#include "../crypto/keccak.h"
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

    if (cmd == NU_CMD_SIGN_TX || cmd == NU_CMD_SIGN_PERSONAL ||
        cmd == NU_CMD_SIGN_TYPED || cmd == NU_CMD_SIGN_SOLANA) {
        /* docs/protocol.md §6: REQUEST_ID(4) ‖ STATUS(2) ‖ SIG_LEN(1) ‖ SIGNATURE
         * 길이 접두사가 있어야 체인마다 다른 서명 길이를 SDK 가 구분할 수 있다.
         * Ethereum 은 r‖s‖recid 로 65바이트, Solana 는 Ed25519 64바이트다. */
        uint8_t p[7 + 65];
        nu_be32(p, id);
        nu_be16(p + 4, status);
        size_t n = 6;
        if (status == NU_SW_OK && sig64) {
            const int has_recid = (recid >= 0);
            p[6] = has_recid ? 65 : 64;
            memcpy(p + 7, sig64, 64);
            if (has_recid) p[71] = (uint8_t)recid;
            n = has_recid ? 72 : 71;
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

/* 승인 절차를 시작하고 PENDING 을 응답한다. steps 는 kind 가 정한다. */
void nu_challenge_start(nu_wallet *w, uint8_t cmd, uint8_t kind, uint8_t steps) {
    nu_request *r = &w->req;
    (void)steps;                               /* v2 는 kind 가 길이를 정한다 */

    r->cmd = cmd;
    r->id = ++w->next_id;
    r->kind = kind;
    r->pos = 0;
    r->stage = 0;
    r->started_ms = w->hal->millis(w->hal->ctx);
    r->phase_ms = r->started_ms;
    r->shown = 0;

    switch (kind) {
    case NU_APPROVAL_CONFIRM:
        /* 서명 확인 — LED 하나를 켜고 그 버튼을 받는다.
         *
         * 어느 LED 인지는 난수로 고른다. 고정하면 사용자가 기기를 보지 않고도
         * 누를 수 있어서, 감염된 호스트가 "아무 버튼이나 누르세요" 라고 유도해도
         * 구분이 안 된다. 한 번 누르는 비용은 같으니 무작위로 둔다. */
        r->seq_len = NU_CONFIRM_STEPS;
        r->attempts = NU_CHALLENGE_ATTEMPTS;
        r->showing = 1;
        {
            uint8_t rnd = 0;
            if (!w->hal->random(&rnd, 1, w->hal->ctx)) {
                nu_request_clear(w);
                nu_reply(w, NU_SW_DEVICE_ERROR, NULL, 0);
                return;
            }
            r->seq[0] = (uint8_t)(rnd % NU_BUTTON_COUNT);
        }
        break;

    case NU_APPROVAL_PIN:
        r->seq_len = NU_PIN_LEN;
        r->attempts = w->pin_attempts;
        r->showing = 0;                        /* PIN 은 절대 보여주지 않는다 */
        break;

    case NU_APPROVAL_PIN_NEW:
        r->seq_len = NU_PIN_LEN;
        r->attempts = NU_CHALLENGE_ATTEMPTS;   /* 재입력이 틀린 횟수 */
        r->showing = 0;
        break;

    default:
        nu_request_clear(w);
        nu_reply(w, NU_SW_DEVICE_ERROR, NULL, 0);
        return;
    }

    uint8_t p[4];
    nu_be32(p, r->id);
    nu_reply(w, NU_SW_PENDING, p, 4);          /* 응답 페이로드는 REQUEST_ID 4바이트 */

    /* KIND 를 함께 보낸다. 호스트가 "LED 를 보고 누르세요" 와 "PIN 을 누르세요"
     * 중 무엇을 띄울지 스스로 추측하면 v1 처럼 어긋난다. */
    uint8_t e[7];
    nu_be32(e, r->id);
    e[4] = r->seq_len;
    e[5] = cmd;
    e[6] = kind;
    nu_event(w, NU_EVT_CHALLENGE_STARTED, e, sizeof e);
    nu_emit_state(w);
}

/* 챌린지가 통과했을 때 실제 작업을 수행한다. */
static void challenge_approved(nu_wallet *w) {
    nu_request *r = &w->req;

    switch (r->cmd) {
    case NU_CMD_SIGN_SOLANA: {
        /* Ed25519 는 HAL 에 있다 — CryptoCell 하드웨어에 묶여 있어서다.
         * 원문을 그대로 넘긴다 (내부에서 해시한다). */
        uint8_t key[32], sig[64];
        if (!nu_derive_ed25519(w, r->path, r->depth, key)) {
            nu_challenge_finish(w, NU_SW_DEVICE_ERROR, NULL, 0);
            return;
        }
        const int ok = w->hal->ed25519_sign(key, r->payload, r->payload_len,
                                            sig, w->hal->ctx);
        memset(key, 0, sizeof key);
        /* recid 는 Ed25519 에 없다. -1 로 "없음" 을 표시한다. */
        nu_challenge_finish(w, ok ? NU_SW_OK : NU_SW_DEVICE_ERROR, ok ? sig : NULL, -1);
        memset(sig, 0, sizeof sig);
        return;
    }
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
        memcpy(w->pin, r->seq, NU_PIN_LEN);
        w->pin_len = r->seq_len;
        if (w->pin_attempts != NU_PIN_ATTEMPTS) nu_persist_tries(w, NU_PIN_ATTEMPTS);
        const int ok = nu_seed_from_words(w, words, count, r->passphrase);
        w->unlocked = ok;
        /* 버튼으로 끝나는 절차라 nu_wallet_handle 을 지나지 않는다. 여기서 민다. */
        if (ok) w->last_active_ms = w->hal->millis(w->hal->ctx);
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

/* PIN 을 새로 정한다. 두 번 받아서 같을 때만 확정한다 — 화면이 없어 입력한
 * 값을 보여줄 수 없으니, 오타를 잡을 방법이 재입력밖에 없다. */
static void pin_new_entered(nu_wallet *w) {
    nu_request *r = &w->req;

    if (r->stage == 0) {                    /* 1차 입력 끝 — 재입력을 받는다 */
        memcpy(r->first, r->seq, NU_PIN_LEN);
        r->stage = 1;
        r->pos = 0;
        /* 재입력에 시간을 새로 준다. 안 그러면 60초가 12번의 입력을 통틀어
         * 적용되어, PIN 을 처음 정하는 사람이 1차를 마치고 잠깐 생각하는
         * 사이에 만료된다. 실기기에서 실제로 그렇게 끊겼다. */
        r->started_ms = w->hal->millis(w->hal->ctx);
        progress(w);
        return;
    }

    if (memcmp(r->first, r->seq, NU_PIN_LEN) != 0) {
        /* 두 입력이 다르다. 처음부터 다시 받는다. */
        memset(r->first, 0, NU_PIN_LEN);
        r->stage = 0;
        r->pos = 0;
        if (r->attempts) r->attempts--;
        progress(w);
        if (r->attempts == 0) nu_challenge_finish(w, NU_SW_PIN_MISMATCH, NULL, 0);
        else r->started_ms = w->hal->millis(w->hal->ctx);
        return;
    }

    /* 확정. 셋업이면 대기 중인 워드를, PIN 변경이면 이미 열린 워드를 봉인한다. */
    const int is_setup = (r->cmd == NU_CMD_SETUP_CONFIRM || r->cmd == NU_CMD_SETUP_RESTORE);
    const uint16_t *words = is_setup ? w->words_pending : w->words;
    const uint8_t   count = is_setup ? w->words_pending_count : w->word_count;

    int ok = nu_persist(w, words, count, r->seq, NU_PIN_LEN);
    if (ok && is_setup) {
        memcpy(w->words, w->words_pending, sizeof w->words);
        w->word_count = count;
        ok = nu_seed_from_words(w, w->words, count, r->passphrase);
        w->unlocked = ok;
        memset(w->words_pending, 0, sizeof w->words_pending);
        w->words_pending_count = 0;
    }
    if (ok) {
        memcpy(w->pin, r->seq, NU_PIN_LEN);
        w->pin_len = NU_PIN_LEN;
        w->pin_attempts = NU_PIN_ATTEMPTS;  /* nu_persist() 가 만수로 새로 봉인했다 */
        w->last_active_ms = w->hal->millis(w->hal->ctx);
    }
    memset(r->first, 0, NU_PIN_LEN);
    nu_challenge_finish(w, ok ? NU_SW_OK : NU_SW_DEVICE_ERROR, NULL, 0);
}

void nu_wallet_button(nu_wallet *w, uint8_t idx) {
    nu_request *r = &w->req;
    if (idx >= NU_BUTTON_COUNT) return;
    if (nu_factory_button(w, idx)) return;  /* 공장 초기화 확인 시퀀스가 먼저 */
    if (!r->cmd) return;
    if (r->showing) return;                 /* LED 를 보여주는 동안은 무시 */

    if (r->kind == NU_APPROVAL_PIN) {
        if (r->pos < r->seq_len) r->seq[r->pos++] = idx;
        progress(w);
        if (r->pos >= r->seq_len) pin_entered(w);
        return;
    }

    if (r->kind == NU_APPROVAL_PIN_NEW) {
        if (r->pos < r->seq_len) r->seq[r->pos++] = idx;
        progress(w);
        if (r->pos >= r->seq_len) pin_new_entered(w);
        return;
    }

    /* NU_APPROVAL_CONFIRM — 켜진 LED 의 버튼을 한 번. */
    if (idx == r->seq[r->pos]) {
        r->pos++;
        progress(w);
        if (r->pos >= r->seq_len) challenge_approved(w);
        return;
    }

    /* 틀렸다 — 다시 보여주고 다시 받는다. */
    r->pos = 0;
    if (r->attempts) r->attempts--;
    progress(w);
    if (r->attempts == 0) { nu_challenge_finish(w, NU_SW_CHALLENGE_FAILED, NULL, 0); return; }
    r->showing = 1;
    r->shown = 0;
    r->phase_ms = w->hal->millis(w->hal->ctx);
}

/* ── 주기 처리 ──────────────────────────────────────────────────────────── */

void nu_wallet_tick(nu_wallet *w, uint32_t now_ms) {
    nu_request *r = &w->req;

    /* 공장 초기화가 진행 중이면 LED 를 독점한다. 그 상태에서 다른 표시가
     * 섞이면 사용자가 지금 무엇이 일어나는지 알 수 없다. */
    if (nu_factory_tick(w, now_ms)) return;

    /* B2 세션: PIN 한 번으로 열고, 조용하면 스스로 닫는다. 열어 둔 채로 두면
     * 감염된 호스트가 나중에 서명을 요청할 창이 계속 열려 있다. */
    if (w->unlocked && !r->cmd &&
        nu_elapsed(now_ms, w->last_active_ms) > NU_SESSION_IDLE_MS) {
        nu_session_lock(w);
        nu_emit_state(w);
    }

    if (!r->cmd) {
        /* 대기 중 표시: 지갑 없음 = 소등, 잠김 = LED0 점멸, 해제 = LED3 점등 */
        if (!w->rec_len)      nu_leds(w, 0);
        else if (!w->unlocked) nu_leds(w, ((now_ms / IDLE_BLINK) & 1) ? 0x01 : 0x00);
        else                   nu_leds(w, 0x08);
        return;
    }

    if (nu_elapsed(now_ms, r->started_ms) > NU_CHALLENGE_TIMEOUT_MS) {
        nu_challenge_finish(w, NU_SW_CHALLENGE_TIMEOUT, NULL, 0);
        return;
    }

    if (r->kind == NU_APPROVAL_PIN || r->kind == NU_APPROVAL_PIN_NEW) {
        /* PIN 은 값을 보여주지 않는다. 몇 자리 눌렀는지만 보여준다 —
         * 길이 6은 공개값이라 이건 정보를 흘리지 않는다.
         * LED 가 4개뿐이라 5·6번째는 순환시킨다. */
        uint8_t m = 0;
        for (uint8_t i = 0; i < r->pos; i++) m |= (uint8_t)(1u << (i % 4));
        nu_leds(w, m ? m : (((now_ms / 500) & 1) ? 0x0f : 0x00));
        return;
    }

    if (r->showing) {
        /* 시퀀스를 한 자리씩 보여준다: 켜짐 450ms, 꺼짐 200ms. */
        const uint32_t elapsed = nu_elapsed(now_ms, r->phase_ms);
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
