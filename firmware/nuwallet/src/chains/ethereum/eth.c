#include "eth.h"
#include "../../crypto/keccak.h"
#include <string.h>

void eth_address_from_pubkey(const uint8_t pub65[65], uint8_t addr[20]) {
    uint8_t h[32];
    keccak256(pub65 + 1, 64, h);      /* 0x04 접두 제외 */
    memcpy(addr, h + 12, 20);
    memset(h, 0, sizeof h);
}

void eth_tx_hash(const uint8_t *payload, size_t len, uint8_t out32[32]) {
    keccak256(payload, len, out32);
}

void eth_personal_hash(const uint8_t *msg, size_t len, uint8_t out32[32]) {
    static const char PFX[] = "\x19" "Ethereum Signed Message:\n";
    char num[24];
    size_t nl = 0;
    if (len == 0) { num[nl++] = '0'; }
    else {
        char tmp[24]; size_t t = 0;
        size_t v = len;
        while (v) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
        while (t) num[nl++] = tmp[--t];
    }
    keccak_ctx c;
    keccak256_init(&c);
    keccak256_update(&c, (const uint8_t *)PFX, sizeof(PFX) - 1);
    keccak256_update(&c, (const uint8_t *)num, nl);
    keccak256_update(&c, msg, len);
    keccak256_final(&c, out32);
}

void eth_typed_hash(const uint8_t domain32[32], const uint8_t message32[32], uint8_t out32[32]) {
    keccak_ctx c;
    static const uint8_t PFX[2] = { 0x19, 0x01 };
    keccak256_init(&c);
    keccak256_update(&c, PFX, 2);
    keccak256_update(&c, domain32, 32);
    keccak256_update(&c, message32, 32);
    keccak256_final(&c, out32);
}

/* ── RLP ─────────────────────────────────────────────────────────────────── */
typedef struct { const uint8_t *p; size_t n; size_t i; } rd;

static int rd_u8(rd *r, uint8_t *out) {
    if (r->i >= r->n) return 0;
    *out = r->p[r->i++]; return 1;
}
/* 항목 하나의 (오프셋, 길이, 리스트여부) 를 읽는다. */
static int rlp_item(rd *r, size_t *off, size_t *len, int *is_list) {
    uint8_t b;
    if (!rd_u8(r, &b)) return 0;
    *is_list = 0;
    if (b <= 0x7f) { *off = r->i - 1; *len = 1; return 1; }
    if (b <= 0xb7) { *len = b - 0x80; *off = r->i; r->i += *len; return r->i <= r->n; }
    if (b <= 0xbf) {
        const size_t ll = b - 0xb7;
        if (r->i + ll > r->n) return 0;
        size_t L = 0;
        for (size_t k = 0; k < ll; k++) L = (L << 8) | r->p[r->i + k];
        r->i += ll; *off = r->i; *len = L; r->i += L;
        return r->i <= r->n;
    }
    *is_list = 1;
    if (b <= 0xf7) { *len = b - 0xc0; *off = r->i; r->i += *len; return r->i <= r->n; }
    {   const size_t ll = b - 0xf7;
        if (r->i + ll > r->n) return 0;
        size_t L = 0;
        for (size_t k = 0; k < ll; k++) L = (L << 8) | r->p[r->i + k];
        r->i += ll; *off = r->i; *len = L; r->i += L;
        return r->i <= r->n; }
}
static uint64_t be_u64(const uint8_t *p, size_t n) {
    uint64_t v = 0;
    if (n > 8) n = 8;
    for (size_t i = 0; i < n; i++) v = (v << 8) | p[i];
    return v;
}

void eth_parse_tx(const uint8_t *payload, size_t len, eth_tx_summary *out) {
    memset(out, 0, sizeof *out);
    if (!len) return;

    const uint8_t *p = payload;
    size_t n = len;
    if (p[0] >= 0x01 && p[0] <= 0x7f) {     /* EIP-2718 타입 트랜잭션 */
        out->typed = p[0];
        p++; n--;
    }
    rd r = { p, n, 0 };
    size_t off, ilen; int is_list;
    if (!rlp_item(&r, &off, &ilen, &is_list) || !is_list) return;

    rd f = { p + off, ilen, 0 };
    size_t fo, fl; int flist;
    /* 필드 순서가 legacy 와 typed 에서 다르다. */
    const int typed = out->typed != 0;
    int idx = 0;
    while (f.i < f.n && idx < 9) {
        if (!rlp_item(&f, &fo, &fl, &flist)) return;
        const uint8_t *v = f.p + fo;
        if (!typed) {
            switch (idx) {
                case 0: out->nonce = be_u64(v, fl); break;
                case 3: if (fl == 20) { memcpy(out->to, v, 20); out->has_to = 1; } break;
                case 4: if (fl <= 32) { memcpy(out->value + (32 - fl), v, fl); out->value_len = fl; } break;
                case 5: out->data_len = fl; break;
                case 6: out->chain_id = be_u64(v, fl); break;
            }
        } else {
            switch (idx) {
                case 0: out->chain_id = be_u64(v, fl); break;
                case 1: out->nonce = be_u64(v, fl); break;
                case 5: if (fl == 20) { memcpy(out->to, v, 20); out->has_to = 1; } break;
                case 6: if (fl <= 32) { memcpy(out->value + (32 - fl), v, fl); out->value_len = fl; } break;
                case 7: out->data_len = fl; break;
            }
        }
        idx++;
    }
    out->ok = 1;
}
