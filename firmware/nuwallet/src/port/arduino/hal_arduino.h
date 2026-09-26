#ifndef NUWALLET_HAL_ARDUINO_H
#define NUWALLET_HAL_ARDUINO_H
/* Arduino(Bluefruit) 용 nu_hal 구현 — 난수 / 저장 / LED / 시간.
 * 송신(hal->send)은 스케치의 BLE 글루가 채운다. */
#include "../../core/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* GPIO 와 파일시스템을 올리고 hal 을 채운다. 성공 0. */
int nu_arduino_hal_init(nu_hal *hal);

/* 버튼 폴링. 눌림 엣지가 있으면 그 번호(0..3), 없으면 -1.
 * 스케치의 loop() 가 매 회 부른다 — 인터럽트를 쓰지 않는 이유는 지갑 코어가
 * 재진입을 가정하지 않기 때문이다. */
int nu_arduino_poll_button(uint32_t now_ms);

/* config.h 의 NU_ENABLE_APPROTECT 가 1 이면 SWD 포트를 잠근다. 0 이면 아무것도 안 한다.
 * 아직 잠겨 있지 않으면 UICR 에 쓰고 리셋하므로 돌아오지 않는다.
 * SoftDevice 가 켜지면 NVMC 에 직접 쓸 수 없으므로 setup() 맨 앞에서 부른다. */
void nu_arduino_approtect(void);

#ifdef __cplusplus
}
#endif
#endif
