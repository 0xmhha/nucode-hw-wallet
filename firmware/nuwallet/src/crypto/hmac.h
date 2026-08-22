#ifndef NUWALLET_HMAC_H
#define NUWALLET_HMAC_H
#include "sha2.h"
#ifdef __cplusplus
extern "C" {
#endif

void hmac_sha256(const uint8_t *key, size_t klen, const uint8_t *msg, size_t mlen,
                 uint8_t out[SHA256_DIGEST]);
void hmac_sha512(const uint8_t *key, size_t klen, const uint8_t *msg, size_t mlen,
                 uint8_t out[SHA512_DIGEST]);

/* BIP-39 시드 파생용. dklen 은 64 고정으로 충분하다. */
void pbkdf2_hmac_sha512(const uint8_t *pw, size_t pwlen,
                        const uint8_t *salt, size_t saltlen,
                        uint32_t iters, uint8_t *out, size_t dklen);
#ifdef __cplusplus
}
#endif
#endif
