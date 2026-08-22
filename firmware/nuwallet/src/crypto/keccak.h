#ifndef NUWALLET_KECCAK_H
#define NUWALLET_KECCAK_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Keccak-256 — Ethereum 이 쓰는 원조 Keccak 이다. SHA3-256 이 아니다.
 * 차이는 패딩 한 바이트: Keccak 은 0x01, SHA3 는 0x06. */
#define KECCAK256_DIGEST 32
typedef struct { uint64_t st[25]; uint8_t buf[136]; size_t n; } keccak_ctx;

void keccak256_init(keccak_ctx *c);
void keccak256_update(keccak_ctx *c, const uint8_t *d, size_t n);
void keccak256_final(keccak_ctx *c, uint8_t out[KECCAK256_DIGEST]);
void keccak256(const uint8_t *d, size_t n, uint8_t out[KECCAK256_DIGEST]);

#ifdef __cplusplus
}
#endif
#endif
