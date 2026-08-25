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
 * NU-40 DK 실물은 LOW 에서 켜진다 — 실기기 확인 결과 1 이 맞다.
 * HAL 이 이 값을 읽는다 (src/port/arduino/hal_arduino.cpp). */
#define NU_LED_INVERT   1

#define BTN_DEBOUNCE_MS 25

#define BLE_NAME_PREFIX "NuWallet-"

/* 부팅 직후 몇 초 동안 LED 를 하나씩 순서대로 켠다 — 극성과 핀 매핑 확인용.
 * "LED 가 이상하다" 를 말로 주고받으면 극성인지 매핑인지 다른 코드가 끼어든
 * 것인지 구분이 안 된다. 이 값을 16000 쯤으로 두고 한 번 보면 바로 갈린다.
 * 0 이면 건너뛰고 곧바로 지갑으로 들어간다. */
#define LED_SELFTEST_MS 0

#define CONSOLE_BAUD    115200
/* 1 이면 시리얼로 상태 로그를 찍는다. 니모닉이나 키는 절대 찍지 않는다. */
#define DEBUG_LOG       1
