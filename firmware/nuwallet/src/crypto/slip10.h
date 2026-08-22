#ifndef NUWALLET_SLIP10_H
#define NUWALLET_SLIP10_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SLIP10_HARDENED 0x80000000u
#define SLIP10_MAX_DEPTH 8

typedef struct {
    uint8_t key[32];
    uint8_t chain[32];
} slip10_ed25519_key;

/* SLIP-0010 Ed25519 derives hardened children only. */
int slip10_ed25519_derive(const uint8_t *seed, size_t seed_len,
                          const uint32_t *path, int depth,
                          slip10_ed25519_key *out);
void slip10_ed25519_wipe(slip10_ed25519_key *key);

#ifdef __cplusplus
}
#endif
#endif
