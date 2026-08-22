#include "wallet.h"
#include "store.h"
#include "rlp.h"
#include "crypto/bip39.h"
#include "crypto/bip32.h"
#include "crypto/ecdsa.h"
#include "crypto/keccak.h"
#include <string.h>

/* ── LED 타이밍 ─────────────────────────────────────────────────────────── */
#define SHOW_ON_MS   450
#define SHOW_GAP_MS  200
#define IDLE_BLINK   1000

/* ── 작은 유틸 ──────────────────────────────────────────────────────────── */
static void be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}
static uint32_t rd32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static void leds(nu_wallet *w, uint8_t mask) {
    if (mask == w->led_mask) return;
    w->led_mask = mask;
    w->hal->leds(mask, w->hal->ctx);
}

/* 송신 조립 버퍼.
 *
 * 스택에 두면 reply() 가 불리는 깊은 프레임마다 그만큼 얹힌다 — 코어는 이미
 * BIP-32·PBKDF2 로 2KB 넘게 쓴다. 코어는 메인 스레드 한 곳에서만 돌고
 * (main.c 참고) reply/event 는 hal->send 가 끝나기 전에 돌아오지 않으므로,
 * 파일 정적 버퍼 하나를 돌려 써도 안전하다.
 *
 * 크기는 프레이밍 한계와 맞춘다. 예전에는 128바이트라서 그보다 큰 응답이
 * 조용히 DEVICE_ERROR 로 바뀌었다 — 2048바이트까지 쪼개 보낼 수 있는데도. */
static uint8_t tx_buf[NU_MAX_MESSAGE];

static void reply(nu_wallet *w, uint16_t status, const uint8_t *payload, size_t len) {
    if (len > sizeof tx_buf - 4) { status = NU_SW_TOO_LARGE; len = 0; }
    be16(tx_buf, status);
    be16(tx_buf + 2, (uint16_t)len);
    if (len) memcpy(tx_buf + 4, payload, len);
    w->hal->send(NU_TAG_MESSAGE, tx_buf, 4 + len, w->hal->ctx);
}

/* 니모닉은 최대 24*2 = 48바이트다. */
static void reply_words(nu_wallet *w, const uint16_t *words, uint8_t count) {
    uint8_t p[1 + 24 * 2];
    p[0] = count;
    for (uint8_t i = 0; i < count; i++) be16(p + 1 + i * 2, words[i]);
    reply(w, NU_SW_OK, p, 1 + (size_t)count * 2);
}

static void event(nu_wallet *w, uint8_t evt, const uint8_t *payload, size_t len) {
    if (len > sizeof tx_buf - 3) return;
    tx_buf[0] = evt;
    be16(tx_buf + 1, (uint16_t)len);
    if (len) memcpy(tx_buf + 3, payload, len);
    w->hal->send(NU_TAG_EVENT, tx_buf, 3 + len, w->hal->ctx);
}

uint8_t nu_wallet_flags(const nu_wallet *w) {
    uint8_t f = 0;
    if (w->rec_len) f |= NU_FLAG_INITIALIZED;
    if (w->rec_len && !w->unlocked) f |= NU_FLAG_LOCKED;
    if (w->req.cmd) f |= NU_FLAG_CHALLENGE_ACTIVE;
    if (w->pin_len || (w->rec_len && nu_store_has_pin(w->rec))) f |= NU_FLAG_HAS_PIN;
    return f;
}

static void emit_state(nu_wallet *w) {
    const uint8_t f = nu_wallet_flags(w);
    event(w, NU_EVT_DEVICE_STATE, &f, 1);
}

/* ── 세션 ───────────────────────────────────────────────────────────────── */

static void lock(nu_wallet *w) {
    w->unlocked = 0;
    memset(w->seed, 0, sizeof w->seed);
    memset(w->words, 0, sizeof w->words);
    memset(w->pin, 0, sizeof w->pin);
    w->word_count = 0;
    w->pin_len = 0;
}

/* 워드 + 패스프레이즈 -> 시드. 실패 0. */
static int seed_from_words(nu_wallet *w, const uint16_t *words, uint8_t count,
                           const char *passphrase) {
    char mnemonic[24 * 9 + 1];
    if (!bip39_to_string(words, count, mnemonic, sizeof mnemonic)) return 0;
    bip39_seed(mnemonic, passphrase ? passphrase : "", w->seed);
    memset(mnemonic, 0, sizeof mnemonic);
    return 1;
}

/* 경로 파생 후 주소/공개키. 실패 0. */
static int derive_addr(const nu_wallet *w, const uint32_t *path, uint8_t depth,
                       uint8_t addr[20], uint8_t pub65[65], uint8_t chain[32],
                       uint8_t priv[32]) {
    bip32_key k;
    if (!bip32_derive_path(w->seed, 64, path, depth, &k)) return 0;
    uint8_t pub[65];
    if (!bip32_public_key(k.priv, pub)) { bip32_wipe(&k); return 0; }
    uint8_t h[32];
    keccak256(pub + 1, 64, h);
    if (addr)  memcpy(addr, h + 12, 20);
    if (pub65) memcpy(pub65, pub, 65);
    if (chain) memcpy(chain, k.chain, 32);
    if (priv)  memcpy(priv, k.priv, 32);
    bip32_wipe(&k);
    return 1;
}

static const uint32_t DEFAULT_PATH[5] = {
    0x8000002cu, 0x8000003cu, 0x80000000u, 0u, 0u    /* m/44'/60'/0'/0/0 */
};

/* ── 저장 ───────────────────────────────────────────────────────────────── */

static int persist(nu_wallet *w, const uint16_t *words, uint8_t count,
                   const uint8_t *pin, uint8_t pin_len) {
    uint8_t raw[NU_STORE_MAX];
    size_t raw_len = 0;
    if (!nu_store_seal(w->hal, words, count, pin, pin_len, raw, &raw_len)) return 0;
    if (!w->hal->store_write(raw, raw_len, w->hal->ctx)) return 0;
    memcpy(w->rec, raw, raw_len);
    w->rec_len = raw_len;
    return 1;
}

/* 남은 PIN 시도 횟수를 플래시에 반영한다.
 *
 * RAM 에만 두면 전원을 껐다 켜는 것만으로 카운터가 되살아나 무한히 시도할 수
 * 있었다. TRIES 는 MAC 밖이라 PIN 없이도 고쳐 쓸 수 있다 — store.h 참고. */
static void persist_tries(nu_wallet *w, uint8_t tries) {
    if (!w->rec_len) return;
    w->pin_attempts = tries;
    w->rec_len = nu_store_set_tries(w->rec, w->rec_len, tries);
    if (!w->hal->store_write(w->rec, w->rec_len, w->hal->ctx)) {
        /* 쓰기가 실패하면 카운터를 못 줄인 것이다. 무한 시도를 허용하느니
         * 이번 시도를 실패로 끝낸다. */
        w->pin_attempts = 0;
    }
}

/* 시도 횟수가 바닥났다. 지갑을 지운다 — 주운 보드로 계속 눌러 볼 수 없게. */
static void wipe_record(nu_wallet *w) {
    w->hal->store_erase(w->hal->ctx);
    memset(w->rec, 0, sizeof w->rec);
    w->rec_len = 0;
    w->tmp_count = 0;
    lock(w);
}

/* ── 챌린지 ─────────────────────────────────────────────────────────────── */

static void request_clear(nu_wallet *w) {
    memset(&w->req, 0, sizeof w->req);
    leds(w, 0);
}

static void remember(nu_wallet *w, uint32_t id, uint16_t status,
                     const uint8_t *payload, uint8_t len) {
    w->last_id = id;
    w->last_status = status;
    w->last_len = len;
    if (len) memcpy(w->last_payload, payload, len);
}

/* 챌린지를 건 요청을 끝낸다. 결과 이벤트까지 여기서 보낸다. */
static void finish(nu_wallet *w, uint16_t status,
                   const uint8_t *sig64, int recid) {
    const uint32_t id = w->req.id;
    const uint8_t cmd = w->req.cmd;

    if (cmd == NU_CMD_SIGN_TX || cmd == NU_CMD_SIGN_PERSONAL || cmd == NU_CMD_SIGN_TYPED) {
        /* docs/protocol.md §6: REQUEST_ID(4) ‖ STATUS(2) ‖ SIG_LEN(1) ‖ SIGNATURE
         * 길이 접두사가 있어야 체인마다 다른 서명 길이를 SDK 가 구분할 수 있다. */
        uint8_t p[7 + 65];
        be32(p, id);
        be16(p + 4, status);
        size_t n = 6;
        if (status == NU_SW_OK && sig64) {
            p[6] = 65;                      /* Ethereum: r ‖ s ‖ recid */
            memcpy(p + 7, sig64, 64);
            p[71] = (uint8_t)recid;
            n = 72;
        }
        remember(w, id, status, p + 6, (uint8_t)(n - 6));
        event(w, NU_EVT_SIGN_RESULT, p, n);
    } else {
        uint8_t p[7];
        be32(p, id);
        p[4] = cmd;
        be16(p + 5, status);
        remember(w, id, status, NULL, 0);
        event(w, NU_EVT_REQUEST_RESULT, p, sizeof p);
    }
    request_clear(w);
    emit_state(w);
}

static void progress(nu_wallet *w) {
    uint8_t p[6];
    be32(p, w->req.id);
    p[4] = w->req.pos;
    p[5] = w->req.attempts;
    event(w, NU_EVT_CHALLENGE_PROGRESS, p, sizeof p);
}

/* 랜덤 챌린지 또는 PIN 입력을 시작하고 PENDING 을 응답한다. */
static void challenge_start(nu_wallet *w, uint8_t cmd, uint8_t kind, uint8_t steps) {
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
            request_clear(w);
            reply(w, NU_SW_DEVICE_ERROR, NULL, 0);
            return;
        }
        for (uint8_t i = 0; i < steps; i++) r->seq[i] = (uint8_t)(rnd[i] % NU_BUTTON_COUNT);
        memset(rnd, 0, sizeof rnd);
    }

    uint8_t p[6];
    be32(p, r->id);
    be16(p + 4, NU_SW_PENDING);
    reply(w, NU_SW_PENDING, p, 4);          /* 응답 페이로드는 REQUEST_ID 4바이트 */

    uint8_t e[6];
    be32(e, r->id);
    e[4] = steps;
    e[5] = cmd;
    event(w, NU_EVT_CHALLENGE_STARTED, e, 6);
    emit_state(w);
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
        if (!derive_addr(w, r->path, r->depth, NULL, NULL, NULL, priv)) {
            finish(w, NU_SW_DEVICE_ERROR, NULL, 0);
            return;
        }
        const int ok = ecdsa_sign_secp256k1(priv, r->hash, sig, &recid);
        memset(priv, 0, sizeof priv);
        finish(w, ok ? NU_SW_OK : NU_SW_DEVICE_ERROR, ok ? sig : NULL, recid);
        memset(sig, 0, sizeof sig);
        return;
    }
    case NU_CMD_WIPE:
        w->hal->store_erase(w->hal->ctx);
        memset(w->rec, 0, sizeof w->rec);
        w->rec_len = 0;
        w->tmp_count = 0;
        lock(w);
        finish(w, NU_SW_OK, NULL, 0);
        return;

    case NU_CMD_SET_PIN: {
        const int ok = persist(w, w->words, w->word_count, r->new_pin, r->new_pin_len);
        if (ok) {
            memcpy(w->pin, r->new_pin, NU_PIN_MAX);
            w->pin_len = r->new_pin_len;
            w->pin_attempts = NU_PIN_ATTEMPTS;   /* persist() 가 만수로 새로 봉인했다 */
        }
        finish(w, ok ? NU_SW_OK : NU_SW_DEVICE_ERROR, NULL, 0);
        return;
    }
    default:
        finish(w, NU_SW_DEVICE_ERROR, NULL, 0);
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
        if (w->pin_attempts != NU_PIN_ATTEMPTS) persist_tries(w, NU_PIN_ATTEMPTS);
        const int ok = seed_from_words(w, words, count, r->passphrase);
        w->unlocked = ok;
        memset(words, 0, sizeof words);
        finish(w, ok ? NU_SW_OK : NU_SW_DEVICE_ERROR, NULL, 0);
        return;
    }
    memset(words, 0, sizeof words);

    r->pos = 0;
    if (w->pin_attempts) persist_tries(w, (uint8_t)(w->pin_attempts - 1));
    r->attempts = w->pin_attempts;
    progress(w);
    if (w->pin_attempts == 0) {
        wipe_record(w);                 /* 소진 — 시드를 지운다 */
        finish(w, NU_SW_CHALLENGE_FAILED, NULL, 0);
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
    if (r->attempts == 0) { finish(w, NU_SW_CHALLENGE_FAILED, NULL, 0); return; }
    r->showing = 1;                         /* 시퀀스를 다시 보여준다 */
    r->shown = 0;
    r->phase_ms = w->hal->millis(w->hal->ctx);
}

/* ── 명령 처리 ──────────────────────────────────────────────────────────── */

/* DEPTH ‖ PATH 를 읽는다. 소비한 바이트 수, 형식 오류면 0. */
static size_t read_path(const uint8_t *p, size_t len, uint32_t *path, uint8_t *depth) {
    if (len < 1) return 0;
    const uint8_t d = p[0];
    if (d == 0 || d > BIP32_MAX_DEPTH) return 0;
    if (len < 1u + (size_t)d * 4u) return 0;
    for (uint8_t i = 0; i < d; i++) path[i] = rd32(p + 1 + i * 4);
    *depth = d;
    return 1u + (size_t)d * 4u;
}

/* CHAIN ‖ DEPTH ‖ PATH 를 읽는다 (docs/protocol.md §3.5).
 *
 * 경로를 받는 모든 명령이 앞에 체인 바이트를 하나 둔다. 기기는 체인 바이트로
 * 곡선과 파생 규칙만 고른다 — 어느 네트워크인지는 모르고, 알 필요도 없다.
 *
 * 반환값: 소비한 바이트 수. 형식 오류면 0, 모르는 체인이면 (size_t)-1. */
#define READ_CHAIN_BAD   ((size_t)0)
#define READ_CHAIN_OTHER ((size_t)-1)

static size_t read_chain_path(const uint8_t *p, size_t len,
                              uint32_t *path, uint8_t *depth) {
    if (len < 1) return READ_CHAIN_BAD;
    if (p[0] != NU_CHAIN_ETHEREUM) return READ_CHAIN_OTHER;
    const size_t used = read_path(p + 1, len - 1, path, depth);
    return used ? used + 1 : READ_CHAIN_BAD;
}


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

static void cmd_get_version(nu_wallet *w) {
    uint8_t p[4 + sizeof w->name];
    p[0] = NU_PROTOCOL_VERSION;
    p[1] = NU_FW_MAJOR;
    p[2] = NU_FW_MINOR;
    p[3] = nu_wallet_flags(w);
    const size_t n = strlen(w->name);
    memcpy(p + 4, w->name, n);
    reply(w, NU_SW_OK, p, 4 + n);
}

static void cmd_get_state(nu_wallet *w) {
    uint8_t p[7];
    p[0] = nu_wallet_flags(w);
    be32(p + 1, w->req.cmd ? w->req.id : 0);
    p[5] = w->req.cmd ? w->req.attempts : w->pin_attempts;
    p[6] = w->rec_len ? nu_store_pin_len(w->rec) : 0;
    reply(w, NU_SW_OK, p, sizeof p);
}

static void cmd_setup_generate(nu_wallet *w, const uint8_t *p, size_t len) {
    if (w->rec_len) { reply(w, NU_SW_ALREADY_INITIALIZED, NULL, 0); return; }
    if (len < 1) { reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    size_t ent_len;
    switch (p[0]) {
    case 12: ent_len = 16; break;
    case 15: ent_len = 20; break;
    case 18: ent_len = 24; break;
    case 21: ent_len = 28; break;
    case 24: ent_len = 32; break;
    default: reply(w, NU_SW_BAD_PARAM, NULL, 0); return;
    }
    uint8_t ent[32];
    if (!w->hal->random(ent, ent_len, w->hal->ctx)) {
        reply(w, NU_SW_DEVICE_ERROR, NULL, 0); return;
    }
    const int count = bip39_from_entropy(ent, ent_len, w->tmp_words);
    memset(ent, 0, sizeof ent);
    if (!count) { reply(w, NU_SW_DEVICE_ERROR, NULL, 0); return; }
    w->tmp_count = (uint8_t)count;
    reply_words(w, w->tmp_words, w->tmp_count);
}

/* CONFIRM 과 RESTORE 는 확정 절차가 같다. */
static void finalize_setup(nu_wallet *w, const uint16_t *words, uint8_t count,
                           const char *passphrase) {
    if (!persist(w, words, count, NULL, 0)) { reply(w, NU_SW_DEVICE_ERROR, NULL, 0); return; }
    memcpy(w->words, words, (size_t)count * 2);
    w->word_count = count;
    w->pin_len = 0;
    w->pin_attempts = NU_PIN_ATTEMPTS;
    if (!seed_from_words(w, words, count, passphrase)) {
        reply(w, NU_SW_DEVICE_ERROR, NULL, 0); return;
    }
    w->unlocked = 1;
    w->tmp_count = 0;
    memset(w->tmp_words, 0, sizeof w->tmp_words);

    uint8_t addr[20];
    if (!derive_addr(w, DEFAULT_PATH, 5, addr, NULL, NULL, NULL)) {
        reply(w, NU_SW_DEVICE_ERROR, NULL, 0); return;
    }
    reply(w, NU_SW_OK, addr, 20);
    emit_state(w);
}

static void cmd_setup_confirm(nu_wallet *w, const uint8_t *p, size_t len) {
    if (w->rec_len) { reply(w, NU_SW_ALREADY_INITIALIZED, NULL, 0); return; }
    if (!w->tmp_count) { reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    uint16_t words[24]; uint8_t count = 0;
    const size_t used = read_words(p, len, words, &count);
    if (!used) { reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    if (count != w->tmp_count || memcmp(words, w->tmp_words, (size_t)count * 2) != 0) {
        reply(w, NU_SW_BAD_PARAM, NULL, 0); return;
    }
    char pass[NU_MAX_PASSPHRASE + 1];
    if (!read_passphrase(p + used, len - used, pass)) { reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    finalize_setup(w, words, count, pass);
    memset(pass, 0, sizeof pass);
}

static void cmd_setup_restore(nu_wallet *w, const uint8_t *p, size_t len) {
    if (w->rec_len) { reply(w, NU_SW_ALREADY_INITIALIZED, NULL, 0); return; }
    uint16_t words[24]; uint8_t count = 0;
    const size_t used = read_words(p, len, words, &count);
    if (!used) { reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }

    uint8_t ent[32]; size_t ent_len = 0;
    if (!bip39_to_entropy(words, count, ent, &ent_len)) {   /* 체크섬 검증 */
        reply(w, NU_SW_BAD_PARAM, NULL, 0); return;
    }
    memset(ent, 0, sizeof ent);

    char pass[NU_MAX_PASSPHRASE + 1];
    if (!read_passphrase(p + used, len - used, pass)) { reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    finalize_setup(w, words, count, pass);
    memset(pass, 0, sizeof pass);
}

static void cmd_set_pin(nu_wallet *w, const uint8_t *p, size_t len) {
    if (!w->rec_len) { reply(w, NU_SW_NOT_INITIALIZED, NULL, 0); return; }
    if (!w->unlocked) { reply(w, NU_SW_LOCKED, NULL, 0); return; }
    if (len < 1) { reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    const uint8_t n = p[0];
    if (n != 0 && (n < NU_PIN_MIN || n > NU_PIN_MAX)) { reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    if (len < 1u + n) { reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    for (uint8_t i = 0; i < n; i++) {
        if (p[1 + i] >= NU_BUTTON_COUNT) { reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    }
    memset(w->req.new_pin, 0, NU_PIN_MAX);
    memcpy(w->req.new_pin, p + 1, n);
    w->req.new_pin_len = n;
    challenge_start(w, NU_CMD_SET_PIN, 0, NU_CHALLENGE_STEPS);
}

static void cmd_unlock(nu_wallet *w, const uint8_t *p, size_t len) {
    if (!w->rec_len) { reply(w, NU_SW_NOT_INITIALIZED, NULL, 0); return; }
    char pass[NU_MAX_PASSPHRASE + 1];
    if (!read_passphrase(p, len, pass)) { reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }

    if (w->pin_attempts == 0) { reply(w, NU_SW_CHALLENGE_FAILED, NULL, 0); return; }


    const uint8_t pin_len = nu_store_pin_len(w->rec);
    if (pin_len == 0) {
        /* PIN 이 없으면 버튼 입력 없이 바로 연다. */
        uint16_t words[24]; uint8_t count = 0;
        if (!nu_store_open(w->rec, w->rec_len, NULL, 0, words, &count)) {
            reply(w, NU_SW_DEVICE_ERROR, NULL, 0); return;
        }
        memcpy(w->words, words, sizeof words);
        w->word_count = count;
        w->pin_len = 0;
        const int ok = seed_from_words(w, words, count, pass);
        memset(words, 0, sizeof words);
        if (!ok) { reply(w, NU_SW_DEVICE_ERROR, NULL, 0); return; }
        w->unlocked = 1;
        uint8_t addr[20];
        if (!derive_addr(w, DEFAULT_PATH, 5, addr, NULL, NULL, NULL)) {
            reply(w, NU_SW_DEVICE_ERROR, NULL, 0); return;
        }
        reply(w, NU_SW_OK, addr, 20);
        emit_state(w);
        memset(pass, 0, sizeof pass);
        return;
    }

    memcpy(w->req.passphrase, pass, sizeof pass);
    memset(pass, 0, sizeof pass);
    challenge_start(w, NU_CMD_UNLOCK, 1, pin_len);
}

/* 경로를 받는 명령의 공통 앞부분. 통과하면 1, 아니면 응답까지 보내고 0. */
static int take_chain_path(nu_wallet *w, const uint8_t *p, size_t len,
                           uint32_t *path, uint8_t *depth, size_t *used) {
    if (!w->rec_len) { reply(w, NU_SW_NOT_INITIALIZED, NULL, 0); return 0; }
    if (!w->unlocked) { reply(w, NU_SW_LOCKED, NULL, 0); return 0; }
    const size_t n = read_chain_path(p, len, path, depth);
    if (n == READ_CHAIN_OTHER) { reply(w, NU_SW_UNSUPPORTED_CHAIN, NULL, 0); return 0; }
    if (n == READ_CHAIN_BAD)   { reply(w, NU_SW_BAD_PARAM, NULL, 0); return 0; }
    *used = n;
    return 1;
}

static void cmd_get_address(nu_wallet *w, const uint8_t *p, size_t len) {
    uint32_t path[8]; uint8_t depth = 0; size_t used = 0;
    if (!take_chain_path(w, p, len, path, &depth, &used)) return;

    /* 응답은 길이 접두사를 붙인다 — 체인마다 주소·공개키 길이가 다르다. */
    uint8_t out[1 + 20 + 1 + 65];
    out[0] = 20;
    out[21] = 65;
    if (!derive_addr(w, path, depth, out + 1, out + 22, NULL, NULL)) {
        reply(w, NU_SW_DEVICE_ERROR, NULL, 0); return;
    }
    reply(w, NU_SW_OK, out, sizeof out);
}

/* 0x21 — 주소만. 공개키도 chain code 도 주지 않는다. */
static void cmd_get_chain_address(nu_wallet *w, const uint8_t *p, size_t len) {
    uint32_t path[8]; uint8_t depth = 0; size_t used = 0;
    if (!take_chain_path(w, p, len, path, &depth, &used)) return;

    uint8_t addr[20];
    if (!derive_addr(w, path, depth, addr, NULL, NULL, NULL)) {
        reply(w, NU_SW_DEVICE_ERROR, NULL, 0); return;
    }
    reply(w, NU_SW_OK, addr, sizeof addr);
}

/* 서명 3종의 공통 진입점 — 해시는 각 명령이 만들어서 넘긴다. */
static void start_sign(nu_wallet *w, uint8_t cmd, const uint32_t *path, uint8_t depth,
                       const uint8_t hash[32]) {
    memcpy(w->req.path, path, sizeof w->req.path);
    w->req.depth = depth;
    memcpy(w->req.hash, hash, 32);
    challenge_start(w, cmd, 0, NU_CHALLENGE_STEPS);
}

static void cmd_sign_tx(nu_wallet *w, const uint8_t *p, size_t len) {
    uint32_t path[8]; uint8_t depth = 0; size_t used = 0;
    if (!take_chain_path(w, p, len, path, &depth, &used)) return;
    if (len == used) { reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }

    nu_tx_summary tx;
    if (!nu_tx_parse(p + used, len - used, &tx)) { reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }

    /* 기기가 직접 해시한다. 호스트가 준 해시를 그대로 서명하면 무엇이든
     * 서명하게 된다 — SECURITY.md §2. */
    uint8_t hash[32];
    keccak256(p + used, len - used, hash);
    start_sign(w, NU_CMD_SIGN_TX, path, depth, hash);
}

static void cmd_sign_personal(nu_wallet *w, const uint8_t *p, size_t len) {
    uint32_t path[8]; uint8_t depth = 0; size_t used = 0;
    if (!take_chain_path(w, p, len, path, &depth, &used)) return;
    const size_t mlen = len - used;

    /* "\x19Ethereum Signed Message:\n" ‖ len ‖ message  (EIP-191) */
    static const char PREFIX[] = "\x19" "Ethereum Signed Message:\n";
    char num[11];
    int nlen = 0;
    {
        size_t v = mlen;
        char tmp[11];
        do { tmp[nlen++] = (char)('0' + (v % 10)); v /= 10; } while (v);
        for (int i = 0; i < nlen; i++) num[i] = tmp[nlen - 1 - i];
    }
    keccak_ctx c;
    keccak256_init(&c);
    keccak256_update(&c, (const uint8_t *)PREFIX, sizeof PREFIX - 1);
    keccak256_update(&c, (const uint8_t *)num, (size_t)nlen);
    keccak256_update(&c, p + used, mlen);
    uint8_t hash[32];
    keccak256_final(&c, hash);
    start_sign(w, NU_CMD_SIGN_PERSONAL, path, depth, hash);
}

static void cmd_sign_typed(nu_wallet *w, const uint8_t *p, size_t len) {
    uint32_t path[8]; uint8_t depth = 0; size_t used = 0;
    if (!take_chain_path(w, p, len, path, &depth, &used)) return;
    if (len - used != 64) { reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }

    uint8_t buf[66];
    buf[0] = 0x19; buf[1] = 0x01;
    memcpy(buf + 2, p + used, 64);
    uint8_t hash[32];
    keccak256(buf, sizeof buf, hash);
    start_sign(w, NU_CMD_SIGN_TYPED, path, depth, hash);
}

static void cmd_get_result(nu_wallet *w, const uint8_t *p, size_t len) {
    if (len < 4) { reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    const uint32_t id = rd32(p);
    if (w->req.cmd && w->req.id == id) {
        uint8_t out[2];
        be16(out, NU_SW_PENDING);
        reply(w, NU_SW_OK, out, 2);
        return;
    }
    if (w->last_id == id && id != 0) {
        uint8_t out[2 + sizeof w->last_payload];
        be16(out, w->last_status);
        if (w->last_len) memcpy(out + 2, w->last_payload, w->last_len);
        reply(w, NU_SW_OK, out, 2u + w->last_len);
        return;
    }
    reply(w, NU_SW_BAD_PARAM, NULL, 0);
}

static void cmd_cancel(nu_wallet *w, const uint8_t *p, size_t len) {
    if (len < 4) { reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    const uint32_t id = rd32(p);
    if (!w->req.cmd || w->req.id != id) { reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    reply(w, NU_SW_OK, NULL, 0);
    finish(w, NU_SW_USER_REJECTED, NULL, 0);
}

void nu_wallet_handle(nu_wallet *w, const uint8_t *msg, size_t len) {
    if (len < 3) { reply(w, NU_SW_FRAMING_ERROR, NULL, 0); return; }
    const uint8_t cmd = msg[0];
    const size_t plen = (size_t)((msg[1] << 8) | msg[2]);
    if (len < 3 + plen) { reply(w, NU_SW_FRAMING_ERROR, NULL, 0); return; }
    const uint8_t *p = msg + 3;

    /* 승인 대기 중에는 새 요청을 받지 않는다. 조회성 명령만 통과시킨다. */
    if (w->req.cmd) {
        switch (cmd) {
        case NU_CMD_GET_VERSION: case NU_CMD_GET_STATE:
        case NU_CMD_GET_RESULT:  case NU_CMD_CANCEL:
            break;
        default:
            reply(w, NU_SW_USER_REJECTED, NULL, 0);
            return;
        }
    }

    switch (cmd) {
    case NU_CMD_GET_VERSION:    cmd_get_version(w); break;
    case NU_CMD_GET_STATE:      cmd_get_state(w); break;
    case NU_CMD_SETUP_GENERATE: cmd_setup_generate(w, p, plen); break;
    case NU_CMD_SETUP_CONFIRM:  cmd_setup_confirm(w, p, plen); break;
    case NU_CMD_SETUP_RESTORE:  cmd_setup_restore(w, p, plen); break;
    case NU_CMD_WIPE:
        if (!w->rec_len) { reply(w, NU_SW_NOT_INITIALIZED, NULL, 0); break; }
        challenge_start(w, NU_CMD_WIPE, 0, NU_CHALLENGE_STEPS);
        break;
    case NU_CMD_SET_PIN:        cmd_set_pin(w, p, plen); break;
    case NU_CMD_UNLOCK:         cmd_unlock(w, p, plen); break;
    case NU_CMD_LOCK:
        lock(w);
        reply(w, NU_SW_OK, NULL, 0);
        emit_state(w);
        break;
    case NU_CMD_GET_ADDRESS:       cmd_get_address(w, p, plen); break;
    case NU_CMD_GET_CHAIN_ADDRESS: cmd_get_chain_address(w, p, plen); break;
    case NU_CMD_SIGN_SOLANA:
        /* ed25519 파생·서명이 아직 없다. UNKNOWN_CMD 로 답하면 펌웨어가 낡은
         * 것처럼 보이므로, 명령은 알지만 체인을 못 다룬다고 정확히 말한다. */
        reply(w, NU_SW_UNSUPPORTED_CHAIN, NULL, 0);
        break;
    case NU_CMD_SIGN_TX:
    case NU_CMD_SIGN_PERSONAL:
    case NU_CMD_SIGN_TYPED:
        if (!w->rec_len) { reply(w, NU_SW_NOT_INITIALIZED, NULL, 0); break; }
        if (!w->unlocked) { reply(w, NU_SW_LOCKED, NULL, 0); break; }
        if (cmd == NU_CMD_SIGN_TX)            cmd_sign_tx(w, p, plen);
        else if (cmd == NU_CMD_SIGN_PERSONAL) cmd_sign_personal(w, p, plen);
        else                                  cmd_sign_typed(w, p, plen);
        break;
    case NU_CMD_GET_RESULT:     cmd_get_result(w, p, plen); break;
    case NU_CMD_CANCEL:         cmd_cancel(w, p, plen); break;
    default:                    reply(w, NU_SW_UNKNOWN_CMD, NULL, 0); break;
    }
}

void nu_wallet_framing_error(nu_wallet *w, uint16_t status) {
    reply(w, status, NULL, 0);
}

/* ── 주기 처리 ──────────────────────────────────────────────────────────── */

void nu_wallet_tick(nu_wallet *w, uint32_t now_ms) {
    nu_request *r = &w->req;

    if (!r->cmd) {
        /* 대기 중 표시: 지갑 없음 = 소등, 잠김 = LED0 점멸, 해제 = LED3 점등 */
        if (!w->rec_len)      leds(w, 0);
        else if (!w->unlocked) leds(w, ((now_ms / IDLE_BLINK) & 1) ? 0x01 : 0x00);
        else                   leds(w, 0x08);
        return;
    }

    if (now_ms - r->started_ms > NU_CHALLENGE_TIMEOUT_MS) {
        finish(w, NU_SW_CHALLENGE_TIMEOUT, NULL, 0);
        return;
    }

    if (r->kind == 1) {
        /* PIN 입력 — 시퀀스를 보여주지 않는다. 입력한 자릿수만 표시한다. */
        uint8_t m = 0;
        for (uint8_t i = 0; i < r->pos && i < 4; i++) m |= (uint8_t)(1u << i);
        leds(w, m ? m : (((now_ms / 500) & 1) ? 0x0f : 0x00));
        return;
    }

    if (r->showing) {
        /* 시퀀스를 한 자리씩 보여준다: 켜짐 450ms, 꺼짐 200ms. */
        const uint32_t elapsed = now_ms - r->phase_ms;
        if (r->shown >= r->seq_len) {
            if (elapsed >= SHOW_GAP_MS) { r->showing = 0; leds(w, 0); }
            else leds(w, 0);
            return;
        }
        if (elapsed < SHOW_ON_MS) {
            leds(w, (uint8_t)(1u << r->seq[r->shown]));
        } else if (elapsed < SHOW_ON_MS + SHOW_GAP_MS) {
            leds(w, 0);
        } else {
            r->shown++;
            r->phase_ms = now_ms;
        }
        return;
    }

    /* 입력 대기 — 맞게 누른 개수만큼 LED 를 켠다. */
    uint8_t m = 0;
    for (uint8_t i = 0; i < r->pos && i < 4; i++) m |= (uint8_t)(1u << i);
    leds(w, m);
}

void nu_wallet_disconnected(nu_wallet *w) {
    if (w->req.cmd) {
        /* 결과를 보낼 상대가 없으므로 이벤트 없이 조용히 버린다. */
        remember(w, w->req.id, NU_SW_USER_REJECTED, NULL, 0);
        request_clear(w);
    }
    lock(w);
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
