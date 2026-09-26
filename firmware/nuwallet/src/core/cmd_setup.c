/* 셋업과 잠금 — 지갑을 만들고, PIN 을 정하고, 연다.
 *
 * 워드와 패스프레이즈를 읽어 승인 절차(challenge.c)를 시작하는 데까지가 이 파일의
 * 일이다. 실제 봉인과 시드 계산은 승인이 끝난 뒤 challenge.c 가 session.c 로 한다. */
#include "internal.h"
#include "store.h"
#include "../crypto/bip39.h"
#include <string.h>

/* COUNT ‖ WORD_IDX* 를 읽는다. 소비한 바이트 수, 오류면 0. */
static size_t read_words(const uint8_t *p, size_t len, uint16_t *words, uint8_t *count) {
    if (len < 1) return 0;
    const uint8_t c = p[0];
    if (c != 12 && c != 15 && c != 18 && c != 21 && c != 24) return 0;
    if (len < 1u + (size_t)c * 2u) return 0;
    for (uint8_t i = 0; i < c; i++) {
        words[i] = (uint16_t)((p[1 + i * 2] << 8) | p[2 + i * 2]);
        if (words[i] >= 2048) return 0;
    }
    *count = c;
    return 1u + (size_t)c * 2u;
}

static int read_passphrase(const uint8_t *p, size_t len, char *out) {
    if (len > NU_MAX_PASSPHRASE) return 0;
    memcpy(out, p, len);
    out[len] = 0;
    return 1;
}

void nu_cmd_setup_generate(nu_wallet *w, const uint8_t *p, size_t len) {
    if (w->rec_len) { nu_reply(w, NU_SW_ALREADY_INITIALIZED, NULL, 0); return; }
    if (len < 1) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    size_t ent_len;
    switch (p[0]) {
    case 12: ent_len = 16; break;
    case 15: ent_len = 20; break;
    case 18: ent_len = 24; break;
    case 21: ent_len = 28; break;
    case 24: ent_len = 32; break;
    default: nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return;
    }
    uint8_t ent[32];
    if (!w->hal->random(ent, ent_len, w->hal->ctx)) {
        nu_reply(w, NU_SW_DEVICE_ERROR, NULL, 0); return;
    }
    const int count = bip39_from_entropy(ent, ent_len, w->tmp_words);
    memset(ent, 0, sizeof ent);
    if (!count) { nu_reply(w, NU_SW_DEVICE_ERROR, NULL, 0); return; }
    w->tmp_count = (uint8_t)count;
    nu_reply_words(w, w->tmp_words, w->tmp_count);
}

/* CONFIRM 과 RESTORE 는 확정 절차가 같다.
 *
 * v2 에서 바뀌었다. v1 은 여기서 바로 저장하고 주소를 돌려줬는데, 그러면 PIN
 * 없는 레코드가 만들어진다. PIN 이 없으면 봉인 키가 PBKDF2("", salt) 이고
 * salt 는 레코드에 평문으로 들어 있어서, 플래시를 뜨면 그냥 열린다.
 *
 * 그래서 v2 는 워드를 들고만 있다가 사용자가 기기에서 PIN 을 정한 뒤에 봉인한다.
 * PIN 은 BLE 로 오지 않는다 — 버튼으로만 들어온다. 호스트는 PIN 을 모른다.
 * 주소는 셋업이 끝난 뒤 호스트가 0x21 로 물어본다. */
static void begin_setup(nu_wallet *w, uint8_t cmd, const uint16_t *words, uint8_t count,
                        const char *passphrase) {
    memcpy(w->words_pending, words, (size_t)count * 2);
    w->words_pending_count = count;
    w->tmp_count = 0;
    memset(w->tmp_words, 0, sizeof w->tmp_words);

    /* 패스프레이즈는 PIN 입력이 끝날 때까지 요청에 실어 둔다. */
    memset(w->req.passphrase, 0, sizeof w->req.passphrase);
    if (passphrase) {
        size_t n = strlen(passphrase);
        if (n > NU_MAX_PASSPHRASE) n = NU_MAX_PASSPHRASE;
        memcpy(w->req.passphrase, passphrase, n);
    }
    nu_challenge_start(w, cmd, NU_APPROVAL_PIN_NEW, NU_PIN_LEN);
}

void nu_cmd_setup_confirm(nu_wallet *w, const uint8_t *p, size_t len) {
    if (w->rec_len) { nu_reply(w, NU_SW_ALREADY_INITIALIZED, NULL, 0); return; }
    if (!w->tmp_count) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    uint16_t words[24]; uint8_t count = 0;
    const size_t used = read_words(p, len, words, &count);
    if (!used) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    if (count != w->tmp_count || memcmp(words, w->tmp_words, (size_t)count * 2) != 0) {
        nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return;
    }
    char pass[NU_MAX_PASSPHRASE + 1];
    if (!read_passphrase(p + used, len - used, pass)) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    begin_setup(w, NU_CMD_SETUP_CONFIRM, words, count, pass);
    memset(pass, 0, sizeof pass);
}

void nu_cmd_setup_restore(nu_wallet *w, const uint8_t *p, size_t len) {
    if (w->rec_len) { nu_reply(w, NU_SW_ALREADY_INITIALIZED, NULL, 0); return; }
    uint16_t words[24]; uint8_t count = 0;
    const size_t used = read_words(p, len, words, &count);
    if (!used) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }

    uint8_t ent[32]; size_t ent_len = 0;
    if (!bip39_to_entropy(words, count, ent, &ent_len)) {   /* 체크섬 검증 */
        nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return;
    }
    memset(ent, 0, sizeof ent);

    char pass[NU_MAX_PASSPHRASE + 1];
    if (!read_passphrase(p + used, len - used, pass)) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    begin_setup(w, NU_CMD_SETUP_RESTORE, words, count, pass);
    memset(pass, 0, sizeof pass);
}

/* 0x14 — PIN 변경. 페이로드는 없다.
 *
 * v1 은 새 PIN 을 호스트가 실어 보냈다. 그러면 호스트가 PIN 을 알게 되는데,
 * PIN 의 목적이 "호스트가 감염돼도 기기를 못 연다" 이므로 앞뒤가 맞지 않았다.
 * v2 는 기기에서 두 번 받는다. */
void nu_cmd_set_pin(nu_wallet *w, const uint8_t *p, size_t len) {
    (void)p; (void)len;
    if (!w->rec_len) { nu_reply(w, NU_SW_NOT_INITIALIZED, NULL, 0); return; }
    if (!w->unlocked) { nu_reply(w, NU_SW_LOCKED, NULL, 0); return; }
    nu_challenge_start(w, NU_CMD_SET_PIN, NU_APPROVAL_PIN_NEW, NU_PIN_LEN);
}

void nu_cmd_unlock(nu_wallet *w, const uint8_t *p, size_t len) {
    if (!w->rec_len) { nu_reply(w, NU_SW_NOT_INITIALIZED, NULL, 0); return; }
    char pass[NU_MAX_PASSPHRASE + 1];
    if (!read_passphrase(p, len, pass)) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }

    if (w->pin_attempts == 0) { nu_reply(w, NU_SW_CHALLENGE_FAILED, NULL, 0); return; }


    const uint8_t pin_len = nu_store_pin_len(w->rec);
    if (pin_len == 0) {
        /* PIN 이 없으면 버튼 입력 없이 바로 연다. */
        uint16_t words[24]; uint8_t count = 0;
        if (!nu_store_open(w->rec, w->rec_len, NULL, 0, words, &count)) {
            nu_reply(w, NU_SW_DEVICE_ERROR, NULL, 0); return;
        }
        memcpy(w->words, words, sizeof words);
        w->word_count = count;
        w->pin_len = 0;
        const int ok = nu_seed_from_words(w, words, count, pass);
        memset(words, 0, sizeof words);
        if (!ok) { nu_reply(w, NU_SW_DEVICE_ERROR, NULL, 0); return; }
        w->unlocked = 1;
        uint8_t addr[20];
        if (!nu_derive(w, NU_DEFAULT_PATH, 5, addr, NULL, NULL, NULL)) {
            nu_reply(w, NU_SW_DEVICE_ERROR, NULL, 0); return;
        }
        nu_reply(w, NU_SW_OK, addr, 20);
        nu_emit_state(w);
        memset(pass, 0, sizeof pass);
        return;
    }

    memcpy(w->req.passphrase, pass, sizeof pass);
    memset(pass, 0, sizeof pass);
    nu_challenge_start(w, NU_CMD_UNLOCK, NU_APPROVAL_PIN, NU_PIN_LEN);
}
