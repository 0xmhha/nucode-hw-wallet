#line 1 "/Users/0xtopaz/work/github/0xmhha/nucode-hw-wallet/firmware/nuwallet/config.h"
#pragma once
#include <Arduino.h>

/* NU40-DK 하드웨어 상수 — variant.h 로 검증된 값 */
#define BTN_1   PIN_BUTTON1   /* P0.11 */
#define BTN_2   PIN_BUTTON2   /* P0.12 */
#define BTN_3   PIN_BUTTON3   /* P0.24 */
#define BTN_4   PIN_BUTTON4   /* P0.25 */

#define LED_1   PIN_LED1      /* P0.13 */
#define LED_2   PIN_LED2      /* P0.14 */
#define LED_3   PIN_LED3      /* P0.15 */
#define LED_4   PIN_LED4      /* P0.16 */

/* 핀맵 PNG 는 "negative logic", variant.h 는 LED_STATE_ON=1 이라 서로 다르다.
 * 실물에서 LED 가 반대로 켜지면 이 값을 1 로 바꾼다. */
#define LED_INVERT      0

#define BTN_DEBOUNCE_MS 25

#define BLE_NAME_PREFIX "NuWallet-"

#define CONSOLE_BAUD    115200
/* 1 이면 시리얼로 상태 로그를 찍는다. 니모닉이나 키는 절대 찍지 않는다. */
#define DEBUG_LOG       1
