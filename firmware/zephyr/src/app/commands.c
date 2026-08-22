/* 프로토콜 — 요청을 파싱해 디스패치한다.
 *
 * docs/protocol.md §5 의 명령이 전부 여기 있다. 실제 일은 session.c 와
 * challenge.c 가 하고, 이 파일은 바이트를 해석하고 무엇을 부를지만 정한다.  */
#include "internal.h"
#include "store.h"
#include "rlp.h"
#include "crypto/bip39.h"
#include "crypto/bip32.h"
#include "crypto/ecdsa.h"
#include "crypto/keccak.h"
#include <string.h>

/* ── 명령 처리 ──────────────────────────────────────────────────────────── */

/* DEPTH ‖ PATH 를 읽는다. 소비한 바이트 수, 형식 오류면 0. */
static size_t read_path(const uint8_t *p, size_t len, uint32_t *path, uint8_t *depth) {
    if (len < 1) return 0;
    const uint8_t d = p[0];
    if (d == 0 || d > BIP32_MAX_DEPTH) return 0;
    if (len < 1u + (size_t)d * 4u) return 0;
    for (uint8_t i = 0; i < d; i++) path[i] = nu_rd32(p + 1 + i * 4);
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
    nu_reply(w, NU_SW_OK, p, 4 + n);
}

static void cmd_get_state(nu_wallet *w) {
    uint8_t p[7];
    p[0] = nu_wallet_flags(w);
    nu_be32(p + 1, w->req.cmd ? w->req.id : 0);
    p[5] = w->req.cmd ? w->req.attempts : w->pin_attempts;
    p[6] = w->rec_len ? nu_store_pin_len(w->rec) : 0;
    nu_reply(w, NU_SW_OK, p, sizeof p);
}

static void cmd_setup_generate(nu_wallet *w, const uint8_t *p, size_t len) {
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

/* CONFIRM 과 RESTORE 는 확정 절차가 같다. */
static void finalize_setup(nu_wallet *w, const uint16_t *words, uint8_t count,
                           const char *passphrase) {
    if (!nu_persist(w, words, count, NULL, 0)) { nu_reply(w, NU_SW_DEVICE_ERROR, NULL, 0); return; }
    memcpy(w->words, words, (size_t)count * 2);
    w->word_count = count;
    w->pin_len = 0;
    w->pin_attempts = NU_PIN_ATTEMPTS;
    if (!nu_seed_from_words(w, words, count, passphrase)) {
        nu_reply(w, NU_SW_DEVICE_ERROR, NULL, 0); return;
    }
    w->unlocked = 1;
    w->tmp_count = 0;
    memset(w->tmp_words, 0, sizeof w->tmp_words);

    uint8_t addr[20];
    if (!nu_derive(w, NU_DEFAULT_PATH, 5, addr, NULL, NULL, NULL)) {
        nu_reply(w, NU_SW_DEVICE_ERROR, NULL, 0); return;
    }
    nu_reply(w, NU_SW_OK, addr, 20);
    nu_emit_state(w);
}

static void cmd_setup_confirm(nu_wallet *w, const uint8_t *p, size_t len) {
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
    finalize_setup(w, words, count, pass);
    memset(pass, 0, sizeof pass);
}

static void cmd_setup_restore(nu_wallet *w, const uint8_t *p, size_t len) {
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
    finalize_setup(w, words, count, pass);
    memset(pass, 0, sizeof pass);
}

static void cmd_set_pin(nu_wallet *w, const uint8_t *p, size_t len) {
    if (!w->rec_len) { nu_reply(w, NU_SW_NOT_INITIALIZED, NULL, 0); return; }
    if (!w->unlocked) { nu_reply(w, NU_SW_LOCKED, NULL, 0); return; }
    if (len < 1) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    const uint8_t n = p[0];
    if (n != 0 && (n < NU_PIN_MIN || n > NU_PIN_MAX)) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    if (len < 1u + n) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    for (uint8_t i = 0; i < n; i++) {
        if (p[1 + i] >= NU_BUTTON_COUNT) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    }
    memset(w->req.new_pin, 0, NU_PIN_MAX);
    memcpy(w->req.new_pin, p + 1, n);
    w->req.new_pin_len = n;
    nu_challenge_start(w, NU_CMD_SET_PIN, 0, NU_CHALLENGE_STEPS);
}

static void cmd_unlock(nu_wallet *w, const uint8_t *p, size_t len) {
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
    nu_challenge_start(w, NU_CMD_UNLOCK, 1, pin_len);
}

/* 경로를 받는 명령의 공통 앞부분. 통과하면 1, 아니면 응답까지 보내고 0. */
static int take_chain_path(nu_wallet *w, const uint8_t *p, size_t len,
                           uint32_t *path, uint8_t *depth, size_t *used) {
    if (!w->rec_len) { nu_reply(w, NU_SW_NOT_INITIALIZED, NULL, 0); return 0; }
    if (!w->unlocked) { nu_reply(w, NU_SW_LOCKED, NULL, 0); return 0; }
    const size_t n = read_chain_path(p, len, path, depth);
    if (n == READ_CHAIN_OTHER) { nu_reply(w, NU_SW_UNSUPPORTED_CHAIN, NULL, 0); return 0; }
    if (n == READ_CHAIN_BAD)   { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return 0; }
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
    if (!nu_derive(w, path, depth, out + 1, out + 22, NULL, NULL)) {
        nu_reply(w, NU_SW_DEVICE_ERROR, NULL, 0); return;
    }
    nu_reply(w, NU_SW_OK, out, sizeof out);
}

/* 0x21 — 주소만. 공개키도 chain code 도 주지 않는다. */
static void cmd_get_chain_address(nu_wallet *w, const uint8_t *p, size_t len) {
    uint32_t path[8]; uint8_t depth = 0; size_t used = 0;
    if (!take_chain_path(w, p, len, path, &depth, &used)) return;

    uint8_t addr[20];
    if (!nu_derive(w, path, depth, addr, NULL, NULL, NULL)) {
        nu_reply(w, NU_SW_DEVICE_ERROR, NULL, 0); return;
    }
    nu_reply(w, NU_SW_OK, addr, sizeof addr);
}

/* 서명 3종의 공통 진입점 — 해시는 각 명령이 만들어서 넘긴다. */
static void start_sign(nu_wallet *w, uint8_t cmd, const uint32_t *path, uint8_t depth,
                       const uint8_t hash[32]) {
    memcpy(w->req.path, path, sizeof w->req.path);
    w->req.depth = depth;
    memcpy(w->req.hash, hash, 32);
    nu_challenge_start(w, cmd, 0, NU_CHALLENGE_STEPS);
}

static void cmd_sign_tx(nu_wallet *w, const uint8_t *p, size_t len) {
    uint32_t path[8]; uint8_t depth = 0; size_t used = 0;
    if (!take_chain_path(w, p, len, path, &depth, &used)) return;
    if (len == used) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }

    nu_tx_summary tx;
    if (!nu_tx_parse(p + used, len - used, &tx)) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }

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
    if (len - used != 64) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }

    uint8_t buf[66];
    buf[0] = 0x19; buf[1] = 0x01;
    memcpy(buf + 2, p + used, 64);
    uint8_t hash[32];
    keccak256(buf, sizeof buf, hash);
    start_sign(w, NU_CMD_SIGN_TYPED, path, depth, hash);
}

static void cmd_get_result(nu_wallet *w, const uint8_t *p, size_t len) {
    if (len < 4) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    const uint32_t id = nu_rd32(p);
    if (w->req.cmd && w->req.id == id) {
        uint8_t out[2];
        nu_be16(out, NU_SW_PENDING);
        nu_reply(w, NU_SW_OK, out, 2);
        return;
    }
    if (w->last_id == id && id != 0) {
        uint8_t out[2 + sizeof w->last_payload];
        nu_be16(out, w->last_status);
        if (w->last_len) memcpy(out + 2, w->last_payload, w->last_len);
        nu_reply(w, NU_SW_OK, out, 2u + w->last_len);
        return;
    }
    nu_reply(w, NU_SW_BAD_PARAM, NULL, 0);
}

static void cmd_cancel(nu_wallet *w, const uint8_t *p, size_t len) {
    if (len < 4) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    const uint32_t id = nu_rd32(p);
    if (!w->req.cmd || w->req.id != id) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    nu_reply(w, NU_SW_OK, NULL, 0);
    nu_challenge_finish(w, NU_SW_USER_REJECTED, NULL, 0);
}

void nu_wallet_handle(nu_wallet *w, const uint8_t *msg, size_t len) {
    if (len < 3) { nu_reply(w, NU_SW_FRAMING_ERROR, NULL, 0); return; }
    const uint8_t cmd = msg[0];
    const size_t plen = (size_t)((msg[1] << 8) | msg[2]);
    if (len < 3 + plen) { nu_reply(w, NU_SW_FRAMING_ERROR, NULL, 0); return; }
    const uint8_t *p = msg + 3;

    /* 승인 대기 중에는 새 요청을 받지 않는다. 조회성 명령만 통과시킨다. */
    if (w->req.cmd) {
        switch (cmd) {
        case NU_CMD_GET_VERSION: case NU_CMD_GET_STATE:
        case NU_CMD_GET_RESULT:  case NU_CMD_CANCEL:
            break;
        default:
            nu_reply(w, NU_SW_USER_REJECTED, NULL, 0);
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
        if (!w->rec_len) { nu_reply(w, NU_SW_NOT_INITIALIZED, NULL, 0); break; }
        nu_challenge_start(w, NU_CMD_WIPE, 0, NU_CHALLENGE_STEPS);
        break;
    case NU_CMD_SET_PIN:        cmd_set_pin(w, p, plen); break;
    case NU_CMD_UNLOCK:         cmd_unlock(w, p, plen); break;
    case NU_CMD_LOCK:
        nu_session_lock(w);
        nu_reply(w, NU_SW_OK, NULL, 0);
        nu_emit_state(w);
        break;
    case NU_CMD_GET_ADDRESS:       cmd_get_address(w, p, plen); break;
    case NU_CMD_GET_CHAIN_ADDRESS: cmd_get_chain_address(w, p, plen); break;
    case NU_CMD_SIGN_SOLANA:
        /* ed25519 파생·서명이 아직 없다. UNKNOWN_CMD 로 답하면 펌웨어가 낡은
         * 것처럼 보이므로, 명령은 알지만 체인을 못 다룬다고 정확히 말한다. */
        nu_reply(w, NU_SW_UNSUPPORTED_CHAIN, NULL, 0);
        break;
    case NU_CMD_SIGN_TX:
    case NU_CMD_SIGN_PERSONAL:
    case NU_CMD_SIGN_TYPED:
        if (!w->rec_len) { nu_reply(w, NU_SW_NOT_INITIALIZED, NULL, 0); break; }
        if (!w->unlocked) { nu_reply(w, NU_SW_LOCKED, NULL, 0); break; }
        if (cmd == NU_CMD_SIGN_TX)            cmd_sign_tx(w, p, plen);
        else if (cmd == NU_CMD_SIGN_PERSONAL) cmd_sign_personal(w, p, plen);
        else                                  cmd_sign_typed(w, p, plen);
        break;
    case NU_CMD_GET_RESULT:     cmd_get_result(w, p, plen); break;
    case NU_CMD_CANCEL:         cmd_cancel(w, p, plen); break;
    default:                    nu_reply(w, NU_SW_UNKNOWN_CMD, NULL, 0); break;
    }
}

void nu_wallet_framing_error(nu_wallet *w, uint16_t status) {
    nu_reply(w, status, NULL, 0);
}
