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
