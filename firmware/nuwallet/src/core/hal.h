#ifndef NUWALLET_HAL_H
#define NUWALLET_HAL_H
/* 지갑 코어가 플랫폼에 요구하는 전부.
 *
 * 코어(wallet.c)는 Zephyr 도 Arduino 도 모르며, 이 구조체만 안다. 덕분에
 * 같은 코어를 호스트에서 테스트할 수 있다 (port/host, firmware/test).      */
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* 저장 영역은 한 덩어리로만 읽고 쓴다. 지갑 레코드는 100바이트 남짓이라
 * 부분 갱신이나 웨어 레벨링이 필요 없다. */
#define NU_STORE_MAX 128

typedef struct {
    /* 암호학적으로 안전한 난수. 실패하면 안 된다 — 실패 시 0 반환. */
    int (*random)(uint8_t *out, size_t n, void *ctx);

    /* 저장 영역 읽기. 저장된 게 없으면 0, 있으면 읽은 바이트 수. */
    size_t (*store_read)(uint8_t *out, size_t max, void *ctx);
    /* 저장 영역 쓰기. 성공 1. */
    int (*store_write)(const uint8_t *in, size_t n, void *ctx);
    /* 저장 영역 삭제. 성공 1. */
    int (*store_erase)(void *ctx);

    /* LED 4개. bit0..bit3 이 각각 LED0..LED3. */
    void (*leds)(uint8_t mask, void *ctx);

    /* 지금 눌려 있는 버튼의 비트마스크. 눌림 이벤트(nu_wallet_button)와 달리
     * "계속 누르고 있는" 상태를 본다 — 공장 초기화가 두 버튼을 5초 동안
     * 붙잡고 있는지 알아야 한다. NULL 이면 공장 초기화가 비활성화된다. */
    uint8_t (*buttons)(void *ctx);

    /* Ed25519 (Solana). NULL 이면 기기가 그 체인을 지원하지 않는다고 답한다.
     *
     * 왜 HAL 에 있나: nRF52840 은 CryptoCell 하드웨어로 Ed25519 를 하고, 그건
     * 이식 가능한 C 로 옮길 수 없다. 반대로 secp256k1 은 micro-ecc 로 코어가
     * 직접 한다. 곡선 하나만 밖으로 뺀 것은 그 하나만 하드웨어에 묶여서다.
     *
     * key 는 SLIP-0010 이 파생한 32바이트 시드다. 서명 대상은 해시하지 않은
     * 원문 그대로 넘어온다 — Ed25519 는 내부에서 해시한다. */
    int (*ed25519_pub)(const uint8_t key[32], uint8_t pub[32], void *ctx);
    int (*ed25519_sign)(const uint8_t key[32], const uint8_t *msg, size_t len,
                        uint8_t sig[64], void *ctx);

    /* BLE 본딩 키를 전부 지운다. 성공 1. NULL 이면 건너뛴다.
     *
     * 공장 초기화가 이걸 부르지 않으면 보드는 초기화됐는데 호스트는 옛 페어링을
     * 기억한다. 그러면 다음 연결이 알림 구독 단계에서 끊기고, 사용자는 원인을
     * 찾을 수 없다 — 펌웨어를 다시 올릴 때마다 겪는 그 증상이다. */
    int (*bonds_erase)(void *ctx);

    /* 완성된 메시지 하나를 호스트로 보낸다. 프레이밍은 전송 계층이 한다. */
    void (*send)(uint8_t tag, const uint8_t *msg, size_t len, void *ctx);

    /* 부팅 후 경과 ms. */
    uint32_t (*millis)(void *ctx);

    void *ctx;
} nu_hal;

#ifdef __cplusplus
}
#endif
#endif
