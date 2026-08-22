#include "keccak.h"
#include <string.h>

#define RATE 136   /* 1088비트 = Keccak-256 */

static const uint64_t RC[24] = {
0x0000000000000001ULL,0x0000000000008082ULL,0x800000000000808aULL,0x8000000080008000ULL,
0x000000000000808bULL,0x0000000080000001ULL,0x8000000080008081ULL,0x8000000000008009ULL,
0x000000000000008aULL,0x0000000000000088ULL,0x0000000080008009ULL,0x000000008000000aULL,
0x000000008000808bULL,0x800000000000008bULL,0x8000000000008089ULL,0x8000000000008003ULL,
0x8000000000008002ULL,0x8000000000000080ULL,0x000000000000800aULL,0x800000008000000aULL,
0x8000000080008081ULL,0x8000000000008080ULL,0x0000000080000001ULL,0x8000000080008008ULL};
static const int ROTC[24] = {1,3,6,10,15,21,28,36,45,55,2,14,27,41,56,8,25,43,62,18,39,61,20,44};
static const int PILN[24] = {10,7,11,17,18,3,5,16,8,21,24,4,15,23,19,13,12,2,20,14,22,9,6,1};

#define ROL64(x,n) (((x)<<(n))|((x)>>(64-(n))))

static void keccakf(uint64_t st[25]) {
    uint64_t t, bc[5];
    for (int r = 0; r < 24; r++) {
        for (int i = 0; i < 5; i++) bc[i] = st[i]^st[i+5]^st[i+10]^st[i+15]^st[i+20];
        for (int i = 0; i < 5; i++) {
            t = bc[(i+4)%5] ^ ROL64(bc[(i+1)%5], 1);
            for (int j = 0; j < 25; j += 5) st[j+i] ^= t;
        }
        t = st[1];
        for (int i = 0; i < 24; i++) {
            int j = PILN[i];
            bc[0] = st[j];
            st[j] = ROL64(t, ROTC[i]);
            t = bc[0];
        }
        for (int j = 0; j < 25; j += 5) {
            for (int i = 0; i < 5; i++) bc[i] = st[j+i];
            for (int i = 0; i < 5; i++) st[j+i] ^= (~bc[(i+1)%5]) & bc[(i+2)%5];
        }
        st[0] ^= RC[r];
    }
}

static void absorb(keccak_ctx *c) {
    for (int i = 0; i < RATE/8; i++) {
        uint64_t v = 0;
        for (int j = 7; j >= 0; j--) v = (v << 8) | c->buf[i*8+j];   /* little-endian */
        c->st[i] ^= v;
    }
    keccakf(c->st);
}

void keccak256_init(keccak_ctx *c) { memset(c, 0, sizeof(*c)); }

void keccak256_update(keccak_ctx *c, const uint8_t *d, size_t n) {
    while (n) {
        size_t k = RATE - c->n; if (k > n) k = n;
        memcpy(c->buf + c->n, d, k); c->n += k; d += k; n -= k;
        if (c->n == RATE) { absorb(c); c->n = 0; }
    }
}

void keccak256_final(keccak_ctx *c, uint8_t out[32]) {
    memset(c->buf + c->n, 0, RATE - c->n);
    c->buf[c->n] |= 0x01;          /* Keccak 패딩 (SHA3 는 0x06) */
    c->buf[RATE-1] |= 0x80;
    absorb(c);
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 8; j++) out[i*8+j] = (uint8_t)(c->st[i] >> (j*8));
}

void keccak256(const uint8_t *d, size_t n, uint8_t out[32]) {
    keccak_ctx c; keccak256_init(&c); keccak256_update(&c, d, n); keccak256_final(&c, out);
}
