#ifndef NUWALLET_ECDSA_H
#define NUWALLET_ECDSA_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* RFC 6979 §3.2 결정론적 nonce (HMAC-SHA256, secp256k1).
 * uECC 자체 결정론적 서명은 RFC6979 와 다른 k 를 만든다. 표준을 따라야
 * 다른 구현과 대조 검증이 가능하므로 여기서 직접 만든다. */
void rfc6979_nonce(const uint8_t priv[32], const uint8_t hash32[32], uint8_t k_out[32]);

/* secp256k1 ECDSA 서명.
 *   sig64  : r ‖ s (각 32바이트 big-endian)
 *   recid  : 0..3, Ethereum 의 v = recid + 27 (EIP-155: recid + chainId*2 + 35)
 *   s 는 EIP-2 에 따라 low-s 로 정규화하고, 그때 recid 의 bit0 을 뒤집는다.
 * 실패 시 0. */
int ecdsa_sign_secp256k1(const uint8_t priv[32], const uint8_t hash32[32],
                         uint8_t sig64[64], int *recid);

/* 검증 (테스트용). */
int ecdsa_verify_secp256k1(const uint8_t pub64[64], const uint8_t hash32[32],
                           const uint8_t sig64[64]);

#ifdef __cplusplus
}
#endif
#endif
