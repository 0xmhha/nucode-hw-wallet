#ifndef NUWALLET_HOST_ED25519_H
#define NUWALLET_HOST_ED25519_H
/* 호스트 테스트용 Ed25519 **대역**.
 *
 * ⚠️  진짜 Ed25519 가 아니다. Ed25519 는 nRF52840 의 CryptoCell 하드웨어에만
 *     있어서 호스트에서 돌릴 수 없다. 이 대역은 길이와 결정성만 맞춘다 —
 *     같은 키에서 같은 공개키, 같은 (키, 메시지)에서 같은 서명이 나온다.
 *     그 이상은 보장하지 않는다.
 *
 * 이걸로 통과한 Solana 테스트는 "프로토콜 배관과 SLIP-0010 파생이 맞다" 는
 * 뜻일 뿐이다. 서명 값의 정확성은 실기기로만 확인할 수 있다.
 *
 * 두 브리지(protocol_bridge, arduino_bridge)가 같은 대역을 써야 골든 벡터를
 * 서로 비교할 수 있다. 그래서 한 곳에 둔다.                                */
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

int host_ed25519_pub(const uint8_t key[32], uint8_t pub[32]);
int host_ed25519_sign(const uint8_t key[32], const uint8_t *msg, size_t len,
                      uint8_t sig[64]);

#ifdef __cplusplus
}
#endif
#endif
