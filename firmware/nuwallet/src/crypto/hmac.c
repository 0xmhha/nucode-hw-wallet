#include "hmac.h"
#include <string.h>

void hmac_sha256(const uint8_t *key, size_t klen, const uint8_t *msg, size_t mlen,
                 uint8_t out[SHA256_DIGEST]) {
    uint8_t k[SHA256_BLOCK], pad[SHA256_BLOCK], tmp[SHA256_DIGEST];
    sha256_ctx c;
    memset(k, 0, sizeof(k));
    if (klen > SHA256_BLOCK) sha256(key, klen, k);
    else memcpy(k, key, klen);

    for (size_t i = 0; i < SHA256_BLOCK; i++) pad[i] = k[i] ^ 0x36;
    sha256_init(&c); sha256_update(&c, pad, SHA256_BLOCK);
    sha256_update(&c, msg, mlen); sha256_final(&c, tmp);

    for (size_t i = 0; i < SHA256_BLOCK; i++) pad[i] = k[i] ^ 0x5c;
    sha256_init(&c); sha256_update(&c, pad, SHA256_BLOCK);
    sha256_update(&c, tmp, SHA256_DIGEST); sha256_final(&c, out);

    memset(k, 0, sizeof(k)); memset(pad, 0, sizeof(pad)); memset(tmp, 0, sizeof(tmp));
}

void hmac_sha512(const uint8_t *key, size_t klen, const uint8_t *msg, size_t mlen,
                 uint8_t out[SHA512_DIGEST]) {
    uint8_t k[SHA512_BLOCK], pad[SHA512_BLOCK], tmp[SHA512_DIGEST];
    sha512_ctx c;
    memset(k, 0, sizeof(k));
    if (klen > SHA512_BLOCK) sha512(key, klen, k);
    else memcpy(k, key, klen);

    for (size_t i = 0; i < SHA512_BLOCK; i++) pad[i] = k[i] ^ 0x36;
    sha512_init(&c); sha512_update(&c, pad, SHA512_BLOCK);
    sha512_update(&c, msg, mlen); sha512_final(&c, tmp);

    for (size_t i = 0; i < SHA512_BLOCK; i++) pad[i] = k[i] ^ 0x5c;
    sha512_init(&c); sha512_update(&c, pad, SHA512_BLOCK);
    sha512_update(&c, tmp, SHA512_DIGEST); sha512_final(&c, out);

    memset(k, 0, sizeof(k)); memset(pad, 0, sizeof(pad)); memset(tmp, 0, sizeof(tmp));
}

void pbkdf2_hmac_sha512(const uint8_t *pw, size_t pwlen,
                        const uint8_t *salt, size_t saltlen,
                        uint32_t iters, uint8_t *out, size_t dklen) {
    uint8_t u[SHA512_DIGEST], t[SHA512_DIGEST];
    uint8_t *block = (uint8_t *)0;
    uint32_t blk = 1;
    size_t done = 0;
    /* salt 는 최대 "mnemonic" + 패스프레이즈. 스택에 담을 크기로 제한한다. */
    uint8_t sbuf[256 + 4];
    if (saltlen > 256) saltlen = 256;
    (void)block;

    while (done < dklen) {
        memcpy(sbuf, salt, saltlen);
        sbuf[saltlen]     = (uint8_t)(blk >> 24);
        sbuf[saltlen + 1] = (uint8_t)(blk >> 16);
        sbuf[saltlen + 2] = (uint8_t)(blk >> 8);
        sbuf[saltlen + 3] = (uint8_t)blk;
        hmac_sha512(pw, pwlen, sbuf, saltlen + 4, u);
        memcpy(t, u, SHA512_DIGEST);
        for (uint32_t i = 1; i < iters; i++) {
            hmac_sha512(pw, pwlen, u, SHA512_DIGEST, u);
            for (int j = 0; j < SHA512_DIGEST; j++) t[j] ^= u[j];
        }
        size_t n = dklen - done; if (n > SHA512_DIGEST) n = SHA512_DIGEST;
        memcpy(out + done, t, n);
        done += n; blk++;
    }
    memset(u, 0, sizeof(u)); memset(t, 0, sizeof(t)); memset(sbuf, 0, sizeof(sbuf));
}
