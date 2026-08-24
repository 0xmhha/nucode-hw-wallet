/* 프로토콜 — 요청을 파싱해 디스패치한다.
 *
 * docs/protocol.md §5 의 명령이 전부 여기 있다. 실제 일은 session.c 와
 * challenge.c 가 하고, 이 파일은 바이트를 해석하고 무엇을 부를지만 정한다.  */
#include "internal.h"
#include "store.h"
#include "rlp.h"
#include "../crypto/bip39.h"
#include "../crypto/bip32.h"
#include "../crypto/ecdsa.h"
#include "../crypto/keccak.h"
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

static size_t read_chain_path(const nu_wallet *w, const uint8_t *p, size_t len,
                              uint32_t *path, uint8_t *depth, uint8_t *chain) {
    if (len < 1) return READ_CHAIN_BAD;
    const uint8_t c = p[0];
    if (c != NU_CHAIN_ETHEREUM && c != NU_CHAIN_SOLANA) return READ_CHAIN_OTHER;
    /* Ed25519 는 하드웨어에 묶여 있다. HAL 이 못 하면 이 기기는 그 체인을
     * 지원하지 않는 것이다 — "모르는 명령" 이 아니라 "모르는 체인" 이다. */
    if (c == NU_CHAIN_SOLANA && !w->hal->ed25519_sign) return READ_CHAIN_OTHER;
    const size_t used = read_path(p + 1, len - 1, path, depth);
    if (!used) return READ_CHAIN_BAD;
    *chain = c;
    return used + 1;
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
    begin_setup(w, NU_CMD_SETUP_CONFIRM, words, count, pass);
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
    begin_setup(w, NU_CMD_SETUP_RESTORE, words, count, pass);
    memset(pass, 0, sizeof pass);
}

/* 0x14 — PIN 변경. 페이로드는 없다.
 *
 * v1 은 새 PIN 을 호스트가 실어 보냈다. 그러면 호스트가 PIN 을 알게 되는데,
 * PIN 의 목적이 "호스트가 감염돼도 기기를 못 연다" 이므로 앞뒤가 맞지 않았다.
 * v2 는 기기에서 두 번 받는다. */
static void cmd_set_pin(nu_wallet *w, const uint8_t *p, size_t len) {
    (void)p; (void)len;
    if (!w->rec_len) { nu_reply(w, NU_SW_NOT_INITIALIZED, NULL, 0); return; }
    if (!w->unlocked) { nu_reply(w, NU_SW_LOCKED, NULL, 0); return; }
    nu_challenge_start(w, NU_CMD_SET_PIN, NU_APPROVAL_PIN_NEW, NU_PIN_LEN);
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
    nu_challenge_start(w, NU_CMD_UNLOCK, NU_APPROVAL_PIN, NU_PIN_LEN);
}

/* 경로를 받는 명령의 공통 앞부분. 통과하면 1, 아니면 응답까지 보내고 0. */
static int take_chain_path(nu_wallet *w, const uint8_t *p, size_t len,
                           uint32_t *path, uint8_t *depth, size_t *used,
                           uint8_t *chain) {
    if (!w->rec_len) { nu_reply(w, NU_SW_NOT_INITIALIZED, NULL, 0); return 0; }
    if (!w->unlocked) { nu_reply(w, NU_SW_LOCKED, NULL, 0); return 0; }
    const size_t n = read_chain_path(w, p, len, path, depth, chain);
    if (n == READ_CHAIN_OTHER) { nu_reply(w, NU_SW_UNSUPPORTED_CHAIN, NULL, 0); return 0; }
    if (n == READ_CHAIN_BAD)   { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return 0; }
    *used = n;
    return 1;
}

/* Solana 는 주소가 곧 공개키다. 파생 → 공개키를 한 번에 한다. 실패 0. */
static int solana_pubkey(nu_wallet *w, const uint32_t *path, uint8_t depth,
                         uint8_t pub[32]) {
    uint8_t key[32];
    if (!nu_derive_ed25519(w, path, depth, key)) return 0;
    const int ok = w->hal->ed25519_pub(key, pub, w->hal->ctx);
    memset(key, 0, sizeof key);
    return ok;
}

static void cmd_get_address(nu_wallet *w, const uint8_t *p, size_t len) {
    uint32_t path[8]; uint8_t depth = 0; size_t used = 0; uint8_t chain = 0;
    if (!take_chain_path(w, p, len, path, &depth, &used, &chain)) return;

    /* 응답은 길이 접두사를 붙인다 — 체인마다 주소·공개키 길이가 다르다. */
    if (chain == NU_CHAIN_SOLANA) {
        uint8_t out[1 + 32 + 1 + 32];
        uint8_t pub[32];
        if (!solana_pubkey(w, path, depth, pub)) {
            nu_reply(w, NU_SW_DEVICE_ERROR, NULL, 0); return;
        }
        out[0] = 32; memcpy(out + 1, pub, 32);
        out[33] = 32; memcpy(out + 34, pub, 32);
        nu_reply(w, NU_SW_OK, out, sizeof out);
        return;
    }

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
    uint32_t path[8]; uint8_t depth = 0; size_t used = 0; uint8_t chain = 0;
    if (!take_chain_path(w, p, len, path, &depth, &used, &chain)) return;

    if (chain == NU_CHAIN_SOLANA) {
        uint8_t pub[32];
        if (!solana_pubkey(w, path, depth, pub)) {
            nu_reply(w, NU_SW_DEVICE_ERROR, NULL, 0); return;
        }
        nu_reply(w, NU_SW_OK, pub, sizeof pub);
        return;
    }

    uint8_t addr[20];
    if (!nu_derive(w, path, depth, addr, NULL, NULL, NULL)) {
        nu_reply(w, NU_SW_DEVICE_ERROR, NULL, 0); return;
    }
    nu_reply(w, NU_SW_OK, addr, sizeof addr);
}

/* secp256k1 서명 3종의 공통 진입점 — 해시는 각 명령이 만들어서 넘긴다. */
static void start_sign(nu_wallet *w, uint8_t cmd, const uint32_t *path, uint8_t depth,
                       const uint8_t hash[32]) {
    memcpy(w->req.path, path, sizeof w->req.path);
    w->req.depth = depth;
    w->req.chain = NU_CHAIN_ETHEREUM;
    memcpy(w->req.hash, hash, 32);
    w->req.payload_len = 0;
    nu_challenge_start(w, cmd, NU_APPROVAL_CONFIRM, NU_CONFIRM_STEPS);
}

/* 0x33 — Solana. Ed25519 는 원문을 그대로 서명한다 (내부에서 해시한다).
 * 그래서 해시로 줄이지 못하고 페이로드를 승인 때까지 들고 있어야 한다. */
static void cmd_sign_solana(nu_wallet *w, const uint8_t *p, size_t len) {
    uint32_t path[8]; uint8_t depth = 0; size_t used = 0; uint8_t chain = 0;
    if (!take_chain_path(w, p, len, path, &depth, &used, &chain)) return;
    if (chain != NU_CHAIN_SOLANA) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }

    const size_t n = len - used;
    if (n == 0) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    if (n > NU_MAX_SIGN_PAYLOAD) { nu_reply(w, NU_SW_TOO_LARGE, NULL, 0); return; }

    memcpy(w->req.path, path, sizeof w->req.path);
    w->req.depth = depth;
    w->req.chain = NU_CHAIN_SOLANA;
    memcpy(w->req.payload, p + used, n);
    w->req.payload_len = (uint16_t)n;
    nu_challenge_start(w, NU_CMD_SIGN_SOLANA, NU_APPROVAL_CONFIRM, NU_CONFIRM_STEPS);
}

static void cmd_sign_tx(nu_wallet *w, const uint8_t *p, size_t len) {
    uint32_t path[8]; uint8_t depth = 0; size_t used = 0; uint8_t chain = 0;
    if (!take_chain_path(w, p, len, path, &depth, &used, &chain)) return;
    if (chain != NU_CHAIN_ETHEREUM) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
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
    uint32_t path[8]; uint8_t depth = 0; size_t used = 0; uint8_t chain = 0;
    if (!take_chain_path(w, p, len, path, &depth, &used, &chain)) return;
    if (chain != NU_CHAIN_ETHEREUM) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
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
    uint32_t path[8]; uint8_t depth = 0; size_t used = 0; uint8_t chain = 0;
    if (!take_chain_path(w, p, len, path, &depth, &used, &chain)) return;
    if (chain != NU_CHAIN_ETHEREUM) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
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

    /* 세션 유휴 시계를 민다.
     *
     * 이걸 여기서 하지 않으면 "유휴" 가 아니라 "잠금 해제 후 경과 시간" 이 된다 —
     * 계속 쓰고 있는데도 5분이 지나면 닫힌다. 실기기에서 잡힌 버그다.
     * 잠금 해제 자체도 UNLOCK 명령이 여기를 지나므로 함께 해결된다. */
    w->last_active_ms = w->hal->millis(w->hal->ctx);

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
        /* v2: 잠금이 풀린 세션에서만 지운다. 잠긴 상태에서 지우고 싶으면
         * 공장 초기화(버튼)를 쓴다 — PIN 을 잊었을 때의 경로는 그쪽이다. */
        if (!w->unlocked) { nu_reply(w, NU_SW_LOCKED, NULL, 0); break; }
        nu_challenge_start(w, NU_CMD_WIPE, NU_APPROVAL_CONFIRM, NU_CONFIRM_STEPS);
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
    case NU_CMD_SIGN_TX:
    case NU_CMD_SIGN_PERSONAL:
    case NU_CMD_SIGN_TYPED:
        if (!w->rec_len) { nu_reply(w, NU_SW_NOT_INITIALIZED, NULL, 0); break; }
        if (!w->unlocked) { nu_reply(w, NU_SW_LOCKED, NULL, 0); break; }
        if (cmd == NU_CMD_SIGN_TX)            cmd_sign_tx(w, p, plen);
        else if (cmd == NU_CMD_SIGN_PERSONAL) cmd_sign_personal(w, p, plen);
        else if (cmd == NU_CMD_SIGN_TYPED)    cmd_sign_typed(w, p, plen);
        else                                  cmd_sign_solana(w, p, plen);
        break;
    case NU_CMD_GET_RESULT:     cmd_get_result(w, p, plen); break;
    case NU_CMD_CANCEL:         cmd_cancel(w, p, plen); break;
    default:                    nu_reply(w, NU_SW_UNKNOWN_CMD, NULL, 0); break;
    }
}

void nu_wallet_framing_error(nu_wallet *w, uint16_t status) {
    nu_reply(w, status, NULL, 0);
}
