#include "rlp.h"
#include <string.h>

typedef struct { const uint8_t *p, *end; } rlp_rd;

/* 항목 하나를 읽는다. 성공 1.
 *   is_list  : 리스트면 1
 *   body/len : 페이로드 (리스트면 자식들이 들어 있는 구간) */
static int rlp_item(rlp_rd *r, int *is_list, const uint8_t **body, size_t *body_len) {
    if (r->p >= r->end) return 0;
    const uint8_t b = *r->p;
    size_t len, lol;

    if (b < 0x80) {                       /* 단일 바이트 문자열 */
        *is_list = 0; *body = r->p; *body_len = 1; r->p += 1; return 1;
    }
    if (b <= 0xb7) { *is_list = 0; len = (size_t)(b - 0x80); r->p += 1; goto payload; }
    if (b <= 0xbf) { *is_list = 0; lol = (size_t)(b - 0xb7); r->p += 1; goto longlen; }
    if (b <= 0xf7) { *is_list = 1; len = (size_t)(b - 0xc0); r->p += 1; goto payload; }
    /* b <= 0xff */  { *is_list = 1; lol = (size_t)(b - 0xf7); r->p += 1; goto longlen; }

longlen:
    if (lol > 4 || (size_t)(r->end - r->p) < lol) return 0;
    if (r->p[0] == 0) return 0;                    /* 길이에 선행 0 — 비정규 */
    len = 0;
    for (size_t i = 0; i < lol; i++) len = (len << 8) | r->p[i];
    if (len < 56) return 0;                        /* 짧은 형식을 썼어야 한다 */
    r->p += lol;

payload:
    if ((size_t)(r->end - r->p) < len) return 0;
    /* 길이 1 이면서 0x80 미만인 문자열은 단일 바이트 형식이어야 한다. */
    if (!*is_list && len == 1 && r->p[0] < 0x80) return 0;
    *body = r->p; *body_len = len; r->p += len;
    return 1;
}

/* 정수 문자열 하나를 읽는다. 선행 0 을 금지한다 (RLP 정규형). */
static int rlp_uint(rlp_rd *r, const uint8_t **body, size_t *len) {
    int is_list;
    if (!rlp_item(r, &is_list, body, len) || is_list) return 0;
    if (*len > 32) return 0;
    if (*len == 1 && (*body)[0] == 0) return 0;    /* 0 은 빈 문자열로 쓴다 */
    if (*len > 1 && (*body)[0] == 0) return 0;
    return 1;
}

static uint64_t be_u64(const uint8_t *b, size_t n) {
    uint64_t v = 0;
    if (n > 8) n = 8;
    for (size_t i = 0; i < n; i++) v = (v << 8) | b[i];
    return v;
}

/* [.. to, value, data ..] 공통 부분. */
static int read_to_value_data(rlp_rd *r, nu_tx_summary *out) {
    int is_list; const uint8_t *b; size_t n;

    if (!rlp_item(r, &is_list, &b, &n) || is_list) return 0;
    if (n == 0)      { out->has_to = 0; }
    else if (n == 20) { out->has_to = 1; memcpy(out->to, b, 20); }
    else return 0;

    if (!rlp_uint(r, &b, &n)) return 0;
    out->value_len = (uint8_t)n;
    if (n) memcpy(out->value, b, n);

    if (!rlp_item(r, &is_list, &b, &n) || is_list) return 0;
    out->data_len = (uint32_t)n;
    return 1;
}

int nu_tx_parse(const uint8_t *buf, size_t len, nu_tx_summary *out) {
    if (!buf || len < 3) return 0;
    memset(out, 0, sizeof *out);

    rlp_rd outer;
    if (buf[0] == 0x01 || buf[0] == 0x02) {   /* EIP-2718 typed envelope */
        out->type = buf[0];
        outer.p = buf + 1; outer.end = buf + len;
    } else if (buf[0] >= 0xc0) {              /* legacy: 바깥이 곧 리스트 */
        out->type = 0;
        outer.p = buf; outer.end = buf + len;
    } else {
        return 0;
    }

    int is_list; const uint8_t *body; size_t body_len;
    if (!rlp_item(&outer, &is_list, &body, &body_len) || !is_list) return 0;
    if (outer.p != outer.end) return 0;       /* 뒤에 남는 바이트 금지 */

    rlp_rd r = { body, body + body_len };
    const uint8_t *b; size_t n;

    if (out->type == 0) {
        /* [nonce, gasPrice, gas, to, value, data, chainId, 0, 0] */
        for (int i = 0; i < 3; i++) if (!rlp_uint(&r, &b, &n)) return 0;
        if (!read_to_value_data(&r, out)) return 0;
        if (!rlp_item(&r, &is_list, &b, &n) || is_list || n > 8) return 0;
        out->chain_id = be_u64(b, n);
        /* EIP-155 의 뒤 두 자리는 반드시 빈 문자열이다. */
        for (int i = 0; i < 2; i++) {
            if (!rlp_item(&r, &is_list, &b, &n) || is_list || n != 0) return 0;
        }
    } else if (out->type == 2) {
        /* [chainId, nonce, maxPrio, maxFee, gas, to, value, data, accessList] */
        if (!rlp_item(&r, &is_list, &b, &n) || is_list || n > 8) return 0;
        out->chain_id = be_u64(b, n);
        for (int i = 0; i < 4; i++) if (!rlp_uint(&r, &b, &n)) return 0;
        if (!read_to_value_data(&r, out)) return 0;
        if (!rlp_item(&r, &is_list, &b, &n) || !is_list) return 0;   /* accessList */
    } else {
        /* type 1: [chainId, nonce, gasPrice, gas, to, value, data, accessList] */
        if (!rlp_item(&r, &is_list, &b, &n) || is_list || n > 8) return 0;
        out->chain_id = be_u64(b, n);
        for (int i = 0; i < 3; i++) if (!rlp_uint(&r, &b, &n)) return 0;
        if (!read_to_value_data(&r, out)) return 0;
        if (!rlp_item(&r, &is_list, &b, &n) || !is_list) return 0;
    }

    return r.p == r.end;                      /* 항목이 남거나 모자라면 거부 */
}
