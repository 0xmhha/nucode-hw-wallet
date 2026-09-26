/* 이더리움 서명 — 서명할 해시를 기기가 직접 만든다.
 *
 * 세 명령 모두 호스트가 준 해시를 그대로 서명하지 않는다. 원문(RLP, 메시지,
 * EIP-712 의 두 해시)을 받아 기기가 keccak 을 돌린다. SECURITY.md §2. */
#include "internal.h"
#include "rlp.h"
#include "../crypto/keccak.h"
#include <string.h>

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

void nu_cmd_sign_tx(nu_wallet *w, const uint8_t *p, size_t len) {
    uint32_t path[8]; uint8_t depth = 0; size_t used = 0; uint8_t chain = 0;
    if (!nu_take_chain_path(w, p, len, path, &depth, &used, &chain)) return;
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

void nu_cmd_sign_personal(nu_wallet *w, const uint8_t *p, size_t len) {
    uint32_t path[8]; uint8_t depth = 0; size_t used = 0; uint8_t chain = 0;
    if (!nu_take_chain_path(w, p, len, path, &depth, &used, &chain)) return;
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

void nu_cmd_sign_typed(nu_wallet *w, const uint8_t *p, size_t len) {
    uint32_t path[8]; uint8_t depth = 0; size_t used = 0; uint8_t chain = 0;
    if (!nu_take_chain_path(w, p, len, path, &depth, &used, &chain)) return;
    if (chain != NU_CHAIN_ETHEREUM) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }
    if (len - used != 64) { nu_reply(w, NU_SW_BAD_PARAM, NULL, 0); return; }

    uint8_t buf[66];
    buf[0] = 0x19; buf[1] = 0x01;
    memcpy(buf + 2, p + used, 64);
    uint8_t hash[32];
    keccak256(buf, sizeof buf, hash);
    start_sign(w, NU_CMD_SIGN_TYPED, path, depth, hash);
}
