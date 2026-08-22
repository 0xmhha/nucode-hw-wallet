#include "slip10.h"
#include "hmac.h"
#include <string.h>

int slip10_ed25519_derive(const uint8_t *seed, size_t seed_len,
                          const uint32_t *path, int depth,
                          slip10_ed25519_key *out) {
    static const uint8_t master_key[] = "ed25519 seed";
    uint8_t i64[64];
    uint8_t data[37];
    if (!seed || !out || depth < 0 || depth > SLIP10_MAX_DEPTH) return 0;

    hmac_sha512(master_key, sizeof(master_key) - 1, seed, seed_len, i64);
    memcpy(out->key, i64, 32);
    memcpy(out->chain, i64 + 32, 32);

    for (int i = 0; i < depth; i++) {
        const uint32_t child = path[i];
        if (!(child & SLIP10_HARDENED)) {
            slip10_ed25519_wipe(out);
            memset(i64, 0, sizeof i64);
            return 0;
        }
        data[0] = 0;
        memcpy(data + 1, out->key, 32);
        data[33] = (uint8_t)(child >> 24);
        data[34] = (uint8_t)(child >> 16);
        data[35] = (uint8_t)(child >> 8);
        data[36] = (uint8_t)child;
        hmac_sha512(out->chain, 32, data, sizeof data, i64);
        memcpy(out->key, i64, 32);
        memcpy(out->chain, i64 + 32, 32);
    }
    memset(data, 0, sizeof data);
    memset(i64, 0, sizeof i64);
    return 1;
}

void slip10_ed25519_wipe(slip10_ed25519_key *key) {
    if (key) memset(key, 0, sizeof *key);
}
