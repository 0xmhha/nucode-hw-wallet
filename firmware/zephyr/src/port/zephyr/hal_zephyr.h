#ifndef NUWALLET_HAL_ZEPHYR_H
#define NUWALLET_HAL_ZEPHYR_H
/* Zephyr 용 nu_hal 구현 — 난수 / 저장 / LED / 시간. 송신은 ble.c 가 채운다. */
#include "../../app/hal.h"

/* GPIO·설정 서브시스템을 올리고 hal 을 채운다. 성공 0. */
int nu_zephyr_hal_init(nu_hal *hal);

/* 버튼이 눌리면 이 콜백이 불린다 (시스템 워크큐 컨텍스트). */
typedef void (*nu_button_cb)(uint8_t idx);
void nu_zephyr_set_button_cb(nu_button_cb cb);

#endif
