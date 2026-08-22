#ifndef NUWALLET_SOLANA_H
#define NUWALLET_SOLANA_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* nRF52840 CryptoCell-backed Ed25519. seed is the 32-byte SLIP-0010 key. */
int solana_public_key(const uint8_t seed[32], uint8_t public_key[32]);
int solana_sign(const uint8_t seed[32], const uint8_t *message, size_t message_len,
                uint8_t signature[64]);

#ifdef __cplusplus
}
#endif
#endif
