/* Solana — Ed25519 공개키와 서명.
 *
 * Ed25519 는 HAL 이 하드웨어로 한다 (nRF CryptoCell). HAL 이 못 하면 이 체인은
 * 지원하지 않는 것이고, 그 판단은 commands.c 의 read_chain_path 가 한다. */
#include "internal.h"
#include <string.h>

/* Solana 는 주소가 곧 공개키다. 파생 → 공개키를 한 번에 한다. 실패 0. */
int nu_solana_pubkey(nu_wallet *w, const uint32_t *path, uint8_t depth,
                     uint8_t pub[32]) {
    uint8_t key[32];
    if (!nu_derive_ed25519(w, path, depth, key)) return 0;
    const int ok = w->hal->ed25519_pub(key, pub, w->hal->ctx);
    memset(key, 0, sizeof key);
    return ok;
}

/* 0x33 — Solana. Ed25519 는 원문을 그대로 서명한다 (내부에서 해시한다).
 * 그래서 해시로 줄이지 못하고 페이로드를 승인 때까지 들고 있어야 한다. */
void nu_cmd_sign_solana(nu_wallet *w, const uint8_t *p, size_t len) {
    uint32_t path[8]; uint8_t depth = 0; size_t used = 0; uint8_t chain = 0;
    if (!nu_take_chain_path(w, p, len, path, &depth, &used, &chain)) return;
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
