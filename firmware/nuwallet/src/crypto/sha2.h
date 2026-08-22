#ifndef NUWALLET_SHA2_H
#define NUWALLET_SHA2_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* 이식 가능한 SHA-256 / SHA-512.
 * CC310 하드웨어 대신 소프트웨어를 쓰는 이유: 같은 코드를 호스트에서 표준
 * 테스트 벡터로 검증할 수 있다. 지갑 코드에서는 검증 가능성이 속도보다 중요하다.
 * 성능이 문제가 되면 PBKDF2 만 CC310 으로 옮기면 된다. */

#define SHA256_DIGEST 32
#define SHA256_BLOCK  64
#define SHA512_DIGEST 64
#define SHA512_BLOCK  128

typedef struct { uint32_t h[8]; uint64_t len; uint8_t buf[SHA256_BLOCK]; size_t n; } sha256_ctx;
typedef struct { uint64_t h[8]; uint64_t len; uint8_t buf[SHA512_BLOCK]; size_t n; } sha512_ctx;

void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const uint8_t *d, size_t n);
void sha256_final(sha256_ctx *c, uint8_t out[SHA256_DIGEST]);
void sha256(const uint8_t *d, size_t n, uint8_t out[SHA256_DIGEST]);

void sha512_init(sha512_ctx *c);
void sha512_update(sha512_ctx *c, const uint8_t *d, size_t n);
void sha512_final(sha512_ctx *c, uint8_t out[SHA512_DIGEST]);
void sha512(const uint8_t *d, size_t n, uint8_t out[SHA512_DIGEST]);

#ifdef __cplusplus
}
#endif
#endif
