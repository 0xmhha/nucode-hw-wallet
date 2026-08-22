#ifndef NUWALLET_HAL_HOST_H
#define NUWALLET_HAL_HOST_H
/* 호스트(PC)용 nu_hal 구현. 보드 없이 지갑 코어 전체를 돌리기 위한 것으로,
 * 펌웨어 이미지에는 들어가지 않는다.
 *
 *  - 저장 영역: RAM 배열 (재부팅 시뮬레이션을 위해 밖에서 보존 가능)
 *  - 난수: xorshift. 결정론적이라 테스트가 재현된다. 실기기에서 쓰면 안 된다.
 *  - 시간: 테스트가 직접 밀어 준다
 *  - 송신: 링 버퍼에 쌓아 두고 테스트가 꺼내 본다                          */
#include "../../app/hal.h"

#define NU_HOST_OUT_MAX 32
#define NU_HOST_MSG_MAX 300

typedef struct {
    uint8_t tag;
    uint8_t data[NU_HOST_MSG_MAX];
    size_t  len;
} nu_host_msg;

typedef struct {
    uint8_t  store[NU_STORE_MAX];
    size_t   store_len;
    uint32_t now;
    uint32_t rng;
    uint8_t  leds;
    int      rng_fail;                 /* 1 이면 난수 실패를 흉내낸다 */
    nu_host_msg out[NU_HOST_OUT_MAX];
    int      out_n;
} nu_host;

void nu_host_init(nu_host *h, nu_hal *hal, uint32_t seed);
void nu_host_clear_out(nu_host *h);

#endif
