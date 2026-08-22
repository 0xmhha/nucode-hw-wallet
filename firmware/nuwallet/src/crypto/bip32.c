#include "bip32.h"
#include "hmac.h"
#include "sha2.h"
#include "../micro-ecc/uECC.h"
#include <string.h>

/* secp256k1 군 위수 n. CKDpriv 의 모듈러 덧셈에 필요하다. */
static const uint8_t SECP256K1_N[32] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,
    0xBA,0xAE,0xDC,0xE6,0xAF,0x48,0xA0,0x3B,0xBF,0xD2,0x5E,0x8C,0xD0,0x36,0x41,0x41};

/* big-endian 256비트 비교. a<b 면 -1, a==b 면 0, a>b 면 1. */
static int be_cmp(const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 32; i++) { if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1; }
    return 0;
}
static int be_is_zero(const uint8_t *a) {
    for (int i = 0; i < 32; i++) if (a[i]) return 0;
    return 1;
}
/* out = (a + b) mod n */
static void be_add_mod_n(const uint8_t *a, const uint8_t *b, uint8_t *out) {
    uint16_t carry = 0;
    uint8_t t[32];
    for (int i = 31; i >= 0; i--) {
        const uint16_t s = (uint16_t)a[i] + b[i] + carry;
        t[i] = (uint8_t)s; carry = s >> 8;
    }
    if (carry || be_cmp(t, SECP256K1_N) >= 0) {
        uint16_t borrow = 0;
        for (int i = 31; i >= 0; i--) {
            const int16_t d = (int16_t)t[i] - SECP256K1_N[i] - borrow;
            t[i] = (uint8_t)d; borrow = (d < 0) ? 1 : 0;
        }
    }
    memcpy(out, t, 32);
    memset(t, 0, sizeof(t));
}

int bip32_public_key(const uint8_t priv[32], uint8_t pub65[65]) {
    pub65[0] = 0x04;
    return uECC_compute_public_key(priv, pub65 + 1, uECC_secp256k1());
}

int bip32_public_key_compressed(const uint8_t priv[32], uint8_t pub33[33]) {
    uint8_t pub[64];
    if (!uECC_compute_public_key(priv, pub, uECC_secp256k1())) return 0;
    pub33[0] = (uint8_t)(0x02 + (pub[63] & 1));   /* Y 의 패리티 */
    memcpy(pub33 + 1, pub, 32);
    memset(pub, 0, sizeof(pub));
    return 1;
}

int bip32_from_seed(const uint8_t *seed, size_t seed_len, bip32_key *out) {
    uint8_t I[64];
    hmac_sha512((const uint8_t *)"Bitcoin seed", 12, seed, seed_len, I);
    if (be_is_zero(I) || be_cmp(I, SECP256K1_N) >= 0) { memset(I, 0, sizeof(I)); return 0; }
    memcpy(out->priv, I, 32);
    memcpy(out->chain, I + 32, 32);
    out->depth = 0; out->child = 0; out->parent_fp = 0;
    memset(I, 0, sizeof(I));
    return 1;
}

int bip32_derive(const bip32_key *parent, uint32_t index, bip32_key *out) {
    uint8_t data[37], I[64];
    if (index & BIP32_HARDENED) {
        data[0] = 0x00;
        memcpy(data + 1, parent->priv, 32);
    } else {
        if (!bip32_public_key_compressed(parent->priv, data)) return 0;
    }
    data[33] = (uint8_t)(index >> 24); data[34] = (uint8_t)(index >> 16);
    data[35] = (uint8_t)(index >> 8);  data[36] = (uint8_t)index;

    hmac_sha512(parent->chain, 32, data, 37, I);

    /* IL 이 n 이상이거나 결과가 0 이면 index+1 로 넘어가야 한다 (BIP-32).
       확률이 2^-127 수준이라 여기서는 실패로 처리한다. */
    if (be_cmp(I, SECP256K1_N) >= 0) { memset(I, 0, sizeof(I)); return 0; }
    uint8_t child_priv[32];
    be_add_mod_n(I, parent->priv, child_priv);
    if (be_is_zero(child_priv)) {
        memset(I, 0, sizeof(I)); memset(child_priv, 0, sizeof(child_priv));
        return 0;
    }

    /* 부모 핑거프린트 = RIPEMD160(SHA256(pub))[0..3] 인데, RIPEMD160 을 넣지
       않기 위해 여기서는 쓰지 않는다. xpub 직렬화가 필요해지면 그때 추가한다. */
    memcpy(out->priv, child_priv, 32);
    memcpy(out->chain, I + 32, 32);
    out->depth = (uint8_t)(parent->depth + 1);
    out->child = index;
    out->parent_fp = 0;

    memset(data, 0, sizeof(data)); memset(I, 0, sizeof(I));
    memset(child_priv, 0, sizeof(child_priv));
    return 1;
}

int bip32_derive_path(const uint8_t *seed, size_t seed_len,
                      const uint32_t *path, int depth, bip32_key *out) {
    if (depth < 0 || depth > BIP32_MAX_DEPTH) return 0;
    bip32_key cur, next;
    if (!bip32_from_seed(seed, seed_len, &cur)) return 0;
    for (int i = 0; i < depth; i++) {
        if (!bip32_derive(&cur, path[i], &next)) { bip32_wipe(&cur); return 0; }
        bip32_wipe(&cur);
        cur = next;
    }
    *out = cur;
    memset(&next, 0, sizeof(next));
    return 1;
}

void bip32_wipe(bip32_key *k) { memset(k, 0, sizeof(*k)); }
