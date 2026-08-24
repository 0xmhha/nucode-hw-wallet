#include "host_ed25519.h"
#include "../nuwallet/src/crypto/sha2.h"

int host_ed25519_pub(const uint8_t key[32], uint8_t pub[32]) {
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *)"nuwallet-host-ed25519-pub", 25);
    sha256_update(&c, key, 32);
    sha256_final(&c, pub);
    return 1;
}

int host_ed25519_sign(const uint8_t key[32], const uint8_t *msg, size_t len,
                      uint8_t sig[64]) {
    sha256_ctx c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *)"nuwallet-host-ed25519-sig-A", 27);
    sha256_update(&c, key, 32);
    sha256_update(&c, msg, len);
    sha256_final(&c, sig);

    sha256_init(&c);
    sha256_update(&c, (const uint8_t *)"nuwallet-host-ed25519-sig-B", 27);
    sha256_update(&c, sig, 32);
    sha256_final(&c, sig + 32);
    return 1;
}
