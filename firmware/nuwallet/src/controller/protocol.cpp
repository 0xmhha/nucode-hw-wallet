#line 1 "/Users/0xtopaz/work/github/0xmhha/nucode-hw-wallet/firmware/nuwallet/src/controller/protocol.cpp"
#include "protocol.h"
#include "../storage/keystore.h"
#include "../chains/ethereum/eth.h"
#include "../chains/solana/solana.h"
#include "../crypto/bip39.h"
#include "../crypto/bip32.h"
#include "../crypto/ecdsa.h"
#include "../crypto/sha2.h"
#include <string.h>
#include <Arduino.h>
#include <bluefruit.h>

/* 스케치(.ino)가 제공하는 훅. .ino 쪽이 extern "C" 로 정의하므로 여기서도
 * C 링키지로 선언해야 한다. */
extern "C" {
  void nuwallet_rng(uint8_t *buf, uint16_t len);
  void nuwallet_request_challenge(uint32_t request_id, uint8_t cmd);
  const char *nuwallet_device_name(void);
}

static proto_send_fn g_send = 0;

/* ── 보류 중인 서명 요청 ────────────────────────────────────────────────────
 * 동시에 하나만 처리한다. 진행 중에 새 요청이 오면 거절한다. */
static struct {
    uint32_t id;
    uint8_t  cmd;
    uint8_t  active;
    uint32_t path[BIP32_MAX_DEPTH];
    uint8_t  depth;
    uint8_t  payload[PROTO_MAX_MSG];
    uint16_t payload_len;
    /* 완료된 결과 (GET_RESULT 로 다시 가져갈 수 있게 보관) */
    uint16_t result_status;
    uint8_t  result[66];             /* SIG_LEN(1) + 최대 서명(65) */
    uint8_t  result_len;
    uint8_t  has_result;
} P;

/* 셋업 중 임시로 들고 있는 니모닉. SETUP_CONFIRM 이 와야 저장된다. */
static struct { uint16_t words[24]; int count; int valid; } G;

static uint32_t g_next_id = 1;

/* ── 헬퍼 ────────────────────────────────────────────────────────────────── */
static void be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}
static uint32_t rd32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void reply(uint16_t status, const uint8_t *payload, uint16_t len) {
    static uint8_t buf[PROTO_MAX_MSG + 4];
    if (len > PROTO_MAX_MSG) { status = SW_TOO_LARGE; len = 0; }
    be16(buf, status);
    be16(buf + 2, len);
    if (len && payload) memcpy(buf + 4, payload, len);
    if (g_send) g_send(TAG_MESSAGE, buf, (uint16_t)(4 + len));
}
static void reply_ok(void) { reply(SW_OK, 0, 0); }

static void emit(uint8_t evt, const uint8_t *payload, uint16_t len) {
    static uint8_t buf[128];
    if (len > sizeof(buf) - 3) return;
    buf[0] = evt;
    be16(buf + 1, len);
    if (len && payload) memcpy(buf + 3, payload, len);
    if (g_send) g_send(TAG_EVENT, buf, (uint16_t)(3 + len));
}

static uint8_t state_flags(void) {
    uint8_t f = 0;
    if (keystore_is_initialized()) f |= FLAG_INITIALIZED;
    if (P.active) f |= FLAG_CHALLENGE;
    return f;
}

/* 경로를 읽는다. 반환값은 소비한 바이트 수, 실패 시 0. */
static uint16_t read_path(const uint8_t *p, uint16_t len, uint32_t *out, uint8_t *depth) {
    if (len < 1) return 0;
    const uint8_t d = p[0];
    if (d > BIP32_MAX_DEPTH) return 0;
    if (len < 1u + (uint16_t)d * 4u) return 0;
    for (uint8_t i = 0; i < d; i++) out[i] = rd32(p + 1 + i * 4);
    *depth = d;
    return (uint16_t)(1 + d * 4);
}

/* ── 명령 구현 ───────────────────────────────────────────────────────────── */

static void do_get_version(void) {
    uint8_t out[64];
    const char *name = nuwallet_device_name();
    const size_t nl = strlen(name);
    out[0] = PROTO_VERSION; out[1] = FW_MAJOR; out[2] = FW_MINOR; out[3] = state_flags();
    size_t n = nl; if (n > sizeof(out) - 4) n = sizeof(out) - 4;
    memcpy(out + 4, name, n);
    reply(SW_OK, out, (uint16_t)(4 + n));
}

static void do_get_state(void) {
    uint8_t out[6];
    out[0] = state_flags();
    be32(out + 1, P.active ? P.id : 0);
    out[5] = 0;                       /* 남은 시도 횟수는 이벤트로 전달된다 */
    reply(SW_OK, out, sizeof out);
}

static void do_setup_generate(const uint8_t *p, uint16_t len) {
    if (keystore_is_initialized()) { reply(SW_ALREADY_INITIALIZED, 0, 0); return; }
    if (len < 1) { reply(SW_BAD_PARAM, 0, 0); return; }
    const uint8_t strength = p[0];
    if (strength != 12 && strength != 24) { reply(SW_BAD_PARAM, 0, 0); return; }

    uint8_t ent[32];
    const size_t elen = strength == 24 ? 32 : 16;
    nuwallet_rng(ent, (uint16_t)elen);

    G.count = bip39_from_entropy(ent, elen, G.words);
    memset(ent, 0, sizeof ent);
    if (!G.count) { reply(SW_DEVICE_ERROR, 0, 0); return; }
    G.valid = 1;

    uint8_t out[1 + 24 * 2];
    out[0] = (uint8_t)G.count;
    for (int i = 0; i < G.count; i++) be16(out + 1 + i * 2, G.words[i]);
    reply(SW_OK, out, (uint16_t)(1 + G.count * 2));
}

/* 워드 인덱스 배열을 읽는다. */
static int read_words(const uint8_t *p, uint16_t len, uint16_t *w, int *count) {
    if (len < 1) return 0;
    const int c = p[0];
    if (c != 12 && c != 15 && c != 18 && c != 21 && c != 24) return 0;
    if (len < 1 + c * 2) return 0;
    for (int i = 0; i < c; i++) w[i] = (uint16_t)((p[1 + i * 2] << 8) | p[2 + i * 2]);
    *count = c;
    return 1;
}

static void reply_first_address(void) {
    /* m/44'/60'/0'/0/0 */
    static const uint32_t PATH[5] = {
        44u | BIP32_HARDENED, 60u | BIP32_HARDENED, 0u | BIP32_HARDENED, 0u, 0u };
    bip32_key k;
    if (!keystore_derive(PATH, 5, "", &k)) { reply(SW_DEVICE_ERROR, 0, 0); return; }
    uint8_t pub[65], addr[20];
    if (!bip32_public_key(k.priv, pub)) { bip32_wipe(&k); reply(SW_DEVICE_ERROR, 0, 0); return; }
    eth_address_from_pubkey(pub, addr);
    bip32_wipe(&k);
    reply(SW_OK, addr, 20);
}

static void do_setup_confirm(const uint8_t *p, uint16_t len) {
    if (keystore_is_initialized()) { reply(SW_ALREADY_INITIALIZED, 0, 0); return; }
    if (!G.valid) { reply(SW_BAD_PARAM, 0, 0); return; }
    uint16_t w[24]; int c = 0;
    if (!read_words(p, len, w, &c)) { reply(SW_BAD_PARAM, 0, 0); return; }
    if (c != G.count || memcmp(w, G.words, sizeof(uint16_t) * (size_t)c) != 0) {
        reply(SW_BAD_PARAM, 0, 0); return;
    }
    if (!keystore_save(G.words, G.count)) { reply(SW_DEVICE_ERROR, 0, 0); return; }
    memset(&G, 0, sizeof G);
    reply_first_address();
}

static void do_setup_restore(const uint8_t *p, uint16_t len) {
    if (keystore_is_initialized()) { reply(SW_ALREADY_INITIALIZED, 0, 0); return; }
    uint16_t w[24]; int c = 0;
    if (!read_words(p, len, w, &c)) { reply(SW_BAD_PARAM, 0, 0); return; }
    uint8_t ent[32]; size_t elen = 0;
    if (!bip39_to_entropy(w, c, ent, &elen)) {    /* 체크섬 검증 */
        memset(ent, 0, sizeof ent);
        reply(SW_BAD_PARAM, 0, 0); return;
    }
    memset(ent, 0, sizeof ent);
    if (!keystore_save(w, c)) { reply(SW_DEVICE_ERROR, 0, 0); return; }
    memset(w, 0, sizeof w);
    reply_first_address();
}

static void do_get_address(const uint8_t *p, uint16_t len) {
    if (!keystore_is_initialized()) { reply(SW_NOT_INITIALIZED, 0, 0); return; }
    uint32_t path[BIP32_MAX_DEPTH]; uint8_t depth = 0;
    if (!read_path(p, len, path, &depth)) { reply(SW_BAD_PARAM, 0, 0); return; }

    bip32_key k;
    if (!keystore_derive(path, depth, "", &k)) { reply(SW_DEVICE_ERROR, 0, 0); return; }
    uint8_t out[20 + 65 + 32];
    uint8_t pub[65];
    if (!bip32_public_key(k.priv, pub)) { bip32_wipe(&k); reply(SW_DEVICE_ERROR, 0, 0); return; }
    eth_address_from_pubkey(pub, out);
    memcpy(out + 20, pub, 65);
    memcpy(out + 85, k.chain, 32);
    bip32_wipe(&k);
    reply(SW_OK, out, sizeof out);
}

/* 체인 공통 주소 조회. 개인키/chain code는 절대 응답하지 않는다. */
static void do_get_chain_address(const uint8_t *p, uint16_t len) {
    if (!keystore_is_initialized()) { reply(SW_NOT_INITIALIZED, 0, 0); return; }
    if (len < 2) { reply(SW_BAD_PARAM, 0, 0); return; }
    const uint8_t chain = p[0];
    uint32_t path[BIP32_MAX_DEPTH]; uint8_t depth = 0;
    if (!read_path(p + 1, len - 1, path, &depth)) { reply(SW_BAD_PARAM, 0, 0); return; }
    if (chain == CHAIN_ETHEREUM) {
        bip32_key k; uint8_t pub[65], addr[20];
        memset(&k, 0, sizeof k);
        if (!keystore_derive(path, depth, "", &k) || !bip32_public_key(k.priv, pub)) {
            bip32_wipe(&k); reply(SW_DEVICE_ERROR, 0, 0); return;
        }
        eth_address_from_pubkey(pub, addr);
        bip32_wipe(&k); memset(pub, 0, sizeof pub);
        reply(SW_OK, addr, sizeof addr);
        return;
    }
    if (chain == CHAIN_SOLANA) {
        slip10_ed25519_key k; uint8_t pub[32];
        memset(&k, 0, sizeof k);
        if (!keystore_derive_ed25519(path, depth, "", &k) || !solana_public_key(k.key, pub)) {
            slip10_ed25519_wipe(&k); reply(SW_DEVICE_ERROR, 0, 0); return;
        }
        slip10_ed25519_wipe(&k);
        reply(SW_OK, pub, sizeof pub); /* Solana 주소는 base58(public key) */
        return;
    }
    reply(SW_BAD_PARAM, 0, 0);
}

/* 서명 요청을 접수만 하고 챌린지를 시작한다. */
static void accept_sign(uint8_t cmd, const uint8_t *p, uint16_t len) {
    if (!keystore_is_initialized()) { reply(SW_NOT_INITIALIZED, 0, 0); return; }
    if (P.active) { reply(SW_USER_REJECTED, 0, 0); return; }

    /* 페이로드는 CHAIN_PATH 로 시작한다 — [CHAIN:1][DEPTH:1][PATH: u32 x DEPTH].
     * docs/protocol.md §3.5. 0x21 GET_CHAIN_ADDRESS 와 같은 형식이며,
     * 경로를 받는 명령은 전부 이 형식을 쓴다.
     *
     * 여기서 체인 바이트를 건너뛰지 않으면 그것을 DEPTH 로 읽는다. 0x01 은
     * "깊이 1" 로 보여서 엉뚱한 키로 파생하고, 남은 경로 바이트가 서명 대상
     * 앞에 붙어 페이로드까지 망가진다. */
    if (len < 1) { reply(SW_BAD_PARAM, 0, 0); return; }
    const uint8_t chain = p[0];
    if (chain != CHAIN_ETHEREUM && chain != CHAIN_SOLANA) {
        reply(SW_UNSUPPORTED_CHAIN, 0, 0); return;
    }
    /* 명령과 곡선이 어긋나면 거부한다. secp256k1 명령을 Ed25519 키로
     * 서명하는 일이 없어야 한다. */
    const uint8_t want = (cmd == CMD_SIGN_SOLANA) ? CHAIN_SOLANA : CHAIN_ETHEREUM;
    if (chain != want) { reply(SW_BAD_PARAM, 0, 0); return; }

    uint32_t path[BIP32_MAX_DEPTH]; uint8_t depth = 0;
    const uint16_t used = read_path(p + 1, (uint16_t)(len - 1), path, &depth);
    if (!used) { reply(SW_BAD_PARAM, 0, 0); return; }
    const uint16_t rest = (uint16_t)(len - 1 - used);
    if (rest == 0 || rest > PROTO_MAX_MSG) { reply(SW_BAD_PARAM, 0, 0); return; }
    if (cmd == CMD_SIGN_TYPED && rest != 64) { reply(SW_BAD_PARAM, 0, 0); return; }

    memset(&P, 0, sizeof P);
    P.id = g_next_id++;
    P.cmd = cmd;
    P.active = 1;
    P.depth = depth;
    memcpy(P.path, path, sizeof(uint32_t) * depth);
    memcpy(P.payload, p + 1 + used, rest);
    P.payload_len = rest;

    uint8_t out[4];
    be32(out, P.id);
    reply(SW_PENDING, out, 4);
    nuwallet_request_challenge(P.id, cmd);
}

static void do_get_result(const uint8_t *p, uint16_t len) {
    if (len < 4) { reply(SW_BAD_PARAM, 0, 0); return; }
    const uint32_t id = rd32(p);
    if (P.has_result && P.id == id) {
        reply(P.result_status, P.result, P.result_status == SW_OK ? P.result_len : 0);
        return;
    }
    if (P.active && P.id == id) { reply(SW_PENDING, 0, 0); return; }
    reply(SW_BAD_PARAM, 0, 0);
}

static void do_cancel(const uint8_t *p, uint16_t len) {
    if (len < 4) { reply(SW_BAD_PARAM, 0, 0); return; }
    const uint32_t id = rd32(p);
    if (P.active && P.id == id) {
        P.active = 0;
        P.result_status = SW_USER_REJECTED;
        P.has_result = 1;
    }
    reply_ok();
}

/* ── 디스패치 ────────────────────────────────────────────────────────────── */
void proto_init(proto_send_fn send) {
    g_send = send;
    memset(&P, 0, sizeof P);
    memset(&G, 0, sizeof G);
}

void proto_handle(const uint8_t *msg, uint16_t len) {
    if (len < 3) { reply(SW_FRAMING_ERROR, 0, 0); return; }
    const uint8_t cmd = msg[0];
    const uint16_t plen = (uint16_t)((msg[1] << 8) | msg[2]);
    if (len < 3u + plen) { reply(SW_FRAMING_ERROR, 0, 0); return; }
    const uint8_t *p = msg + 3;

    switch (cmd) {
        case CMD_GET_VERSION:    do_get_version(); break;
        case CMD_GET_STATE:      do_get_state(); break;
        case CMD_SETUP_GENERATE: do_setup_generate(p, plen); break;
        case CMD_SETUP_CONFIRM:  do_setup_confirm(p, plen); break;
        case CMD_SETUP_RESTORE:  do_setup_restore(p, plen); break;
        case CMD_WIPE:
            if (!keystore_is_initialized()) { reply(SW_NOT_INITIALIZED, 0, 0); break; }
            if (P.active) { reply(SW_USER_REJECTED, 0, 0); break; }
            memset(&P, 0, sizeof P);
            P.id = g_next_id++; P.cmd = CMD_WIPE; P.active = 1;
            { uint8_t out[4]; be32(out, P.id); reply(SW_PENDING, out, 4); }
            nuwallet_request_challenge(P.id, CMD_WIPE);
            break;
        case CMD_GET_ADDRESS:    do_get_address(p, plen); break;
        case CMD_GET_CHAIN_ADDRESS: do_get_chain_address(p, plen); break;
        case CMD_SIGN_TX:
        case CMD_SIGN_PERSONAL:
        case CMD_SIGN_TYPED:     accept_sign(cmd, p, plen); break;
        case CMD_SIGN_SOLANA:    accept_sign(cmd, p, plen); break;
        case CMD_GET_RESULT:     do_get_result(p, plen); break;
        case CMD_CANCEL:         do_cancel(p, plen); break;
        default:                 reply(SW_UNKNOWN_CMD, 0, 0); break;
    }
}

/* ── 이벤트 ──────────────────────────────────────────────────────────────── */
void proto_emit_challenge_started(uint32_t id, uint8_t steps, uint8_t cmd) {
    uint8_t out[6];
    be32(out, id); out[4] = steps; out[5] = cmd;
    emit(EVT_CHALLENGE_STARTED, out, sizeof out);
}
void proto_emit_challenge_progress(uint32_t id, uint8_t step, uint8_t attempts) {
    uint8_t out[6];
    be32(out, id); out[4] = step; out[5] = attempts;
    emit(EVT_CHALLENGE_PROGRESS, out, sizeof out);
}

uint32_t proto_pending_id(void)  { return P.active ? P.id : 0; }
uint8_t  proto_pending_cmd(void) { return P.active ? P.cmd : 0; }
void     proto_clear_pending(void) { P.active = 0; }

/* 챌린지가 실패/타임아웃으로 끝났을 때. */
void proto_challenge_resolved(uint32_t id, uint16_t status) {
    if (!P.active || P.id != id) return;
    P.active = 0;
    P.result_status = status;
    P.has_result = 1;
    uint8_t out[6];
    be32(out, id); be16(out + 4, status);
    emit(EVT_SIGN_RESULT, out, 6);
}

/* 챌린지가 통과했을 때 실제 서명을 수행한다. */
void proto_execute_pending(void) {
    if (!P.active) return;
    const uint32_t id = P.id;
    P.active = 0;

    if (P.cmd == CMD_WIPE) {
        keystore_wipe();
        P.result_status = SW_OK; P.has_result = 1;
        uint8_t out[6]; be32(out, id); be16(out + 4, SW_OK);
        emit(EVT_SIGN_RESULT, out, 6);
        return;
    }

    /* 실패 결과를 한 곳에서 만든다 (goto 로 블록에 뛰어들면 안 된다). */
    uint8_t out[7 + 65];
    be32(out, id);

    if (P.cmd == CMD_SIGN_SOLANA) {
        slip10_ed25519_key k;
        uint8_t sig[64];
        const int ok = keystore_derive_ed25519(P.path, P.depth, "", &k) &&
                       solana_sign(k.key, P.payload, P.payload_len, sig);
        slip10_ed25519_wipe(&k);
        if (!ok) {
            be16(out + 4, SW_DEVICE_ERROR);
            P.result_status = SW_DEVICE_ERROR; P.result_len = 0; P.has_result = 1;
            emit(EVT_SIGN_RESULT, out, 6);
            return;
        }
        be16(out + 4, SW_OK);
        out[6] = 64;
        memcpy(out + 7, sig, 64);
        P.result[0] = 64;
        memcpy(P.result + 1, sig, 64);
        P.result_status = SW_OK; P.result_len = 65; P.has_result = 1;
        memset(sig, 0, sizeof sig);
        emit(EVT_SIGN_RESULT, out, 71);
        return;
    }

    bip32_key k;
    uint8_t hash[32];
    uint8_t sig[64];
    int recid = 0;
    int ok = 0;

    if (keystore_derive(P.path, P.depth, "", &k)) {
        int hashed = 1;
        switch (P.cmd) {
            case CMD_SIGN_TX:       eth_tx_hash(P.payload, P.payload_len, hash); break;
            case CMD_SIGN_PERSONAL: eth_personal_hash(P.payload, P.payload_len, hash); break;
            case CMD_SIGN_TYPED:    eth_typed_hash(P.payload, P.payload + 32, hash); break;
            default:                hashed = 0; break;
        }
        if (hashed) ok = ecdsa_sign_secp256k1(k.priv, hash, sig, &recid);
        bip32_wipe(&k);
        memset(hash, 0, sizeof hash);
    }

    if (!ok) {
        be16(out + 4, SW_DEVICE_ERROR);
        P.result_status = SW_DEVICE_ERROR;
        P.has_result = 1;
        emit(EVT_SIGN_RESULT, out, 6);
        return;
    }

    be16(out + 4, SW_OK);
    out[6] = 65;
    memcpy(out + 7, sig, 64);
    out[71] = (uint8_t)recid;
    P.result[0] = 65;
    memcpy(P.result + 1, sig, 64);
    P.result[65] = (uint8_t)recid;
    P.result_status = SW_OK;
    P.result_len = 66;
    P.has_result = 1;
    memset(sig, 0, sizeof sig);
    emit(EVT_SIGN_RESULT, out, 72);
}
