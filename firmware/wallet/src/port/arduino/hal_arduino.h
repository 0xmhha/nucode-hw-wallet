#ifndef NUWALLET_HAL_ARDUINO_H
#define NUWALLET_HAL_ARDUINO_H
/* Arduino(Bluefruit) 용 nu_hal 구현 — 난수 / 저장 / LED / 시간.
 * 송신(hal->send)은 스케치의 BLE 글루가 채운다. */
#include "../../app/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* GPIO 와 파일시스템을 올리고 hal 을 채운다. 성공 0. */
int nu_arduino_hal_init(nu_hal *hal);

/* 버튼 폴링. 눌림 엣지가 있으면 그 번호(0..3), 없으면 -1.
 * 스케치의 loop() 가 매 회 부른다 — 인터럽트를 쓰지 않는 이유는 지갑 코어가
 * 재진입을 가정하지 않기 때문이다. */
int nu_arduino_poll_button(uint32_t now_ms);

#ifdef __cplusplus
}
#endif
#endif
