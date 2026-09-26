/* 프로토콜 — 요청을 파싱해 디스패치한다.
 *
 * docs/protocol.md §5 의 명령이 전부 여기서 갈라진다. 명령마다 하는 일은
 * 영역별 파일에 있다.
 *
 *   cmd_setup.c     셋업, PIN 변경, 잠금 해제
 *   cmd_ethereum.c  이더리움 서명 3종 (해시를 기기가 만든다)
 *   cmd_solana.c    Solana 공개키와 서명
 *
 * 이 파일에는 체인 경로 파싱, 조회성 명령, 주소 조회, 결과 조회와 취소가 남는다. */
#include "internal.h"
#include "store.h"
#include "../crypto/bip32.h"
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

/* 경로를 받는 명령의 공통 앞부분. 통과하면 1, 아니면 응답까지 보내고 0. */
int nu_take_chain_path(nu_wallet *w, const uint8_t *p, size_t len,
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

static void cmd_get_address(nu_wallet *w, const uint8_t *p, size_t len) {
    uint32_t path[8]; uint8_t depth = 0; size_t used = 0; uint8_t chain = 0;
    if (!nu_take_chain_path(w, p, len, path, &depth, &used, &chain)) return;

    /* 응답은 길이 접두사를 붙인다 — 체인마다 주소·공개키 길이가 다르다. */
    if (chain == NU_CHAIN_SOLANA) {
        uint8_t out[1 + 32 + 1 + 32];
        uint8_t pub[32];
        if (!nu_solana_pubkey(w, path, depth, pub)) {
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
    if (!nu_take_chain_path(w, p, len, path, &depth, &used, &chain)) return;

    if (chain == NU_CHAIN_SOLANA) {
        uint8_t pub[32];
        if (!nu_solana_pubkey(w, path, depth, pub)) {
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
    case NU_CMD_SETUP_GENERATE: nu_cmd_setup_generate(w, p, plen); break;
    case NU_CMD_SETUP_CONFIRM:  nu_cmd_setup_confirm(w, p, plen); break;
    case NU_CMD_SETUP_RESTORE:  nu_cmd_setup_restore(w, p, plen); break;
    case NU_CMD_WIPE:
        if (!w->rec_len) { nu_reply(w, NU_SW_NOT_INITIALIZED, NULL, 0); break; }
        /* v2: 잠금이 풀린 세션에서만 지운다. 잠긴 상태에서 지우고 싶으면
         * 공장 초기화(버튼)를 쓴다 — PIN 을 잊었을 때의 경로는 그쪽이다. */
        if (!w->unlocked) { nu_reply(w, NU_SW_LOCKED, NULL, 0); break; }
        nu_challenge_start(w, NU_CMD_WIPE, NU_APPROVAL_CONFIRM, NU_CONFIRM_STEPS);
        break;
    case NU_CMD_SET_PIN:        nu_cmd_set_pin(w, p, plen); break;
    case NU_CMD_UNLOCK:         nu_cmd_unlock(w, p, plen); break;
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
        if (cmd == NU_CMD_SIGN_TX)            nu_cmd_sign_tx(w, p, plen);
        else if (cmd == NU_CMD_SIGN_PERSONAL) nu_cmd_sign_personal(w, p, plen);
        else if (cmd == NU_CMD_SIGN_TYPED)    nu_cmd_sign_typed(w, p, plen);
        else                                  nu_cmd_sign_solana(w, p, plen);
        break;
    case NU_CMD_GET_RESULT:     cmd_get_result(w, p, plen); break;
    case NU_CMD_CANCEL:         cmd_cancel(w, p, plen); break;
    default:                    nu_reply(w, NU_SW_UNKNOWN_CMD, NULL, 0); break;
    }
}

void nu_wallet_framing_error(nu_wallet *w, uint16_t status) {
    nu_reply(w, status, NULL, 0);
}
