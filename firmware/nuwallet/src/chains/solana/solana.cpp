#include "solana.h"
#include <Adafruit_nRFCrypto.h>
#include "nrf_cc310/include/crys_ec_edw_api.h"
#include <string.h>

static int seed_keypair(const uint8_t seed[32], uint8_t secret[64], uint8_t pub[32]) {
    CRYS_ECEDW_TempBuff_t temp;
    size_t secret_len = 64;
    size_t pub_len = 32;
    memset(&temp, 0, sizeof temp);
    const CRYSError_t rc = CRYS_ECEDW_SeedKeyPair(
        seed, 32, secret, &secret_len, pub, &pub_len, &temp);
    memset(&temp, 0, sizeof temp);
    return rc == CRYS_OK && secret_len == 64 && pub_len == 32;
}

int solana_public_key(const uint8_t seed[32], uint8_t public_key[32]) {
    uint8_t secret[64];
    const int ok = seed_keypair(seed, secret, public_key);
    memset(secret, 0, sizeof secret);
    return ok;
}

int solana_sign(const uint8_t seed[32], const uint8_t *message, size_t message_len,
                uint8_t signature[64]) {
    uint8_t secret[64], pub[32];
    if (!message || !seed_keypair(seed, secret, pub)) return 0;
    CRYS_ECEDW_TempBuff_t temp;
    size_t sig_len = 64;
    memset(&temp, 0, sizeof temp);
    const CRYSError_t rc = CRYS_ECEDW_Sign(
        signature, &sig_len, message, message_len, secret, sizeof secret, &temp);
    memset(&temp, 0, sizeof temp);
    memset(secret, 0, sizeof secret);
    memset(pub, 0, sizeof pub);
    return rc == CRYS_OK && sig_len == 64;
}
