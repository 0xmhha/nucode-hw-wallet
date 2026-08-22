#ifndef NUWALLET_BIP32_H
#define NUWALLET_BIP32_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define BIP32_HARDENED 0x80000000u
#define BIP32_MAX_DEPTH 8

typedef struct {
    uint8_t priv[32];
    uint8_t chain[32];
    uint8_t depth;
    uint32_t child;
    uint32_t parent_fp;
} bip32_key;

/* 시드 -> 마스터 키 (HMAC-SHA512, key="Bitcoin seed"). 실패 시 0. */
int bip32_from_seed(const uint8_t *seed, size_t seed_len, bip32_key *out);

/* CKDpriv. index 에 BIP32_HARDENED 를 OR 하면 하드닝 파생. 실패 시 0. */
int bip32_derive(const bip32_key *parent, uint32_t index, bip32_key *out);

/* m/a/b/... 경로 전체 파생. 실패 시 0. */
int bip32_derive_path(const uint8_t *seed, size_t seed_len,
                      const uint32_t *path, int depth, bip32_key *out);

/* 개인키 -> 비압축 공개키 (0x04 ‖ X ‖ Y). 실패 시 0. */
int bip32_public_key(const uint8_t priv[32], uint8_t pub65[65]);

/* 개인키 -> 압축 공개키 (33바이트). 실패 시 0. */
int bip32_public_key_compressed(const uint8_t priv[32], uint8_t pub33[33]);

void bip32_wipe(bip32_key *k);

#ifdef __cplusplus
}
#endif
#endif
