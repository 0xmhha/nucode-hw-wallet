#include "ecdsa.h"
#include "hmac.h"
#include "sha2.h"
#include "../micro-ecc/uECC.h"
#include <string.h>

static const uint8_t SECP256K1_N[32] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,
    0xBA,0xAE,0xDC,0xE6,0xAF,0x48,0xA0,0x3B,0xBF,0xD2,0x5E,0x8C,0xD0,0x36,0x41,0x41};
/* n / 2, EIP-2 low-s 판정용 */
static const uint8_t SECP256K1_N_HALF[32] = {
    0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x5D,0x57,0x6E,0x73,0x57,0xA4,0x50,0x1D,0xDF,0xE9,0x2F,0x46,0x68,0x1B,0x20,0xA0};

static int be_cmp(const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 32; i++) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}
static int be_is_zero(const uint8_t *a) {
    for (int i = 0; i < 32; i++) if (a[i]) return 0;
    return 1;
}
/* out = n - a  (a < n 가정) */
static void be_sub_from_n(const uint8_t *a, uint8_t *out) {
    int borrow = 0;
    for (int i = 31; i >= 0; i--) {
        int d = (int)SECP256K1_N[i] - a[i] - borrow;
        if (d < 0) { d += 256; borrow = 1; } else borrow = 0;
        out[i] = (uint8_t)d;
    }
}

void rfc6979_nonce(const uint8_t priv[32], const uint8_t hash32[32], uint8_t k_out[32]) {
    /* RFC 6979 §3.2.
     * bits2octets(h1) = h1 mod n. secp256k1 은 해시와 n 이 모두 256비트라
     * h1 >= n 인 경우에만 감산이 필요하다. */
    uint8_t h1[32];
    memcpy(h1, hash32, 32);
    if (be_cmp(h1, SECP256K1_N) >= 0) {
        int borrow = 0;
        for (int i = 31; i >= 0; i--) {
            int d = (int)h1[i] - SECP256K1_N[i] - borrow;
            if (d < 0) { d += 256; borrow = 1; } else borrow = 0;
            h1[i] = (uint8_t)d;
        }
    }

    uint8_t V[32], K[32], buf[32 + 1 + 32 + 32];
    memset(V, 0x01, 32);
    memset(K, 0x00, 32);

    /* K = HMAC_K(V ‖ 0x00 ‖ int2octets(x) ‖ bits2octets(h1)) */
    memcpy(buf, V, 32); buf[32] = 0x00;
    memcpy(buf + 33, priv, 32); memcpy(buf + 65, h1, 32);
    hmac_sha256(K, 32, buf, 97, K);
    hmac_sha256(K, 32, V, 32, V);

    /* K = HMAC_K(V ‖ 0x01 ‖ int2octets(x) ‖ bits2octets(h1)) */
    memcpy(buf, V, 32); buf[32] = 0x01;
    memcpy(buf + 33, priv, 32); memcpy(buf + 65, h1, 32);
    hmac_sha256(K, 32, buf, 97, K);
    hmac_sha256(K, 32, V, 32, V);

    for (;;) {
        hmac_sha256(K, 32, V, 32, V);
        if (!be_is_zero(V) && be_cmp(V, SECP256K1_N) < 0) {
            memcpy(k_out, V, 32);
            break;
        }
        /* K = HMAC_K(V ‖ 0x00);  V = HMAC_K(V) */
        memcpy(buf, V, 32); buf[32] = 0x00;
        hmac_sha256(K, 32, buf, 33, K);
        hmac_sha256(K, 32, V, 32, V);
    }
    memset(V, 0, sizeof V); memset(K, 0, sizeof K);
    memset(buf, 0, sizeof buf); memset(h1, 0, sizeof h1);
}

int ecdsa_sign_secp256k1(const uint8_t priv[32], const uint8_t hash32[32],
                         uint8_t sig64[64], int *recid) {
    uint8_t k[32];
    int rid = 0;
    rfc6979_nonce(priv, hash32, k);
    const int ok = uECC_sign_with_k_recoverable(priv, hash32, 32, k, sig64,
                                                uECC_secp256k1(), &rid);
    memset(k, 0, sizeof k);
    if (!ok) return 0;

    /* EIP-2: s 는 반드시 n/2 이하여야 한다. 뒤집으면 R 의 y 부호가 바뀌므로
     * recovery id 의 bit0 도 함께 뒤집는다. */
    if (be_cmp(sig64 + 32, SECP256K1_N_HALF) > 0) {
        uint8_t t[32];
        be_sub_from_n(sig64 + 32, t);
        memcpy(sig64 + 32, t, 32);
        rid ^= 1;
        memset(t, 0, sizeof t);
    }
    if (recid) *recid = rid;
    return 1;
}

int ecdsa_verify_secp256k1(const uint8_t pub64[64], const uint8_t hash32[32],
                           const uint8_t sig64[64]) {
    return uECC_verify(pub64, hash32, 32, sig64, uECC_secp256k1());
}
