/* Arduino(Bluefruit) HAL. */
#if defined(ARDUINO)

#include "hal_arduino.h"
#include "../../app/protocol.h"

#include <Arduino.h>
#include <Adafruit_nRFCrypto.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <string.h>

using namespace Adafruit_LittleFS_Namespace;

/* NU40-DK 핀맵. variant.h 의 값을 그대로 쓴다. */
static const uint8_t LEDS[NU_BUTTON_COUNT] = { PIN_LED1, PIN_LED2, PIN_LED3, PIN_LED4 };
static const uint8_t BTNS[NU_BUTTON_COUNT] = { PIN_BUTTON1, PIN_BUTTON2, PIN_BUTTON3, PIN_BUTTON4 };

#ifndef NU_LED_INVERT
#define NU_LED_INVERT 0
#endif
#define NU_DEBOUNCE_MS 25

static const char *STORE_PATH = "/nuwallet.rec";

/* ── 난수 ───────────────────────────────────────────────────────────────── */

static int hal_random(uint8_t *out, size_t n, void *ctx) {
    (void)ctx;
    /* nRF52840 의 하드웨어 RNG. 실패를 삼키면 예측 가능한 니모닉이 만들어진다. */
    nRFCrypto.begin();
    const bool ok = nRFCrypto.Random.generate(out, n);
    nRFCrypto.end();
    return ok ? 1 : 0;
}

/* ── 저장 ───────────────────────────────────────────────────────────────── */
/* 레코드는 이미 store.c 가 PIN 으로 봉인해서 넘겨준다. 여기서는 바이트를
 * 그대로 넣고 뺄 뿐, 내용을 해석하지 않는다. */

static size_t hal_store_read(uint8_t *out, size_t max, void *ctx) {
    (void)ctx;
    File f(InternalFS);
    if (!f.open(STORE_PATH, FILE_O_READ)) return 0;
    const int n = f.read(out, max);
    f.close();
    return n > 0 ? (size_t)n : 0;
}

static int hal_store_write(const uint8_t *in, size_t n, void *ctx) {
    (void)ctx;
    InternalFS.remove(STORE_PATH);
    File f(InternalFS);
    if (!f.open(STORE_PATH, FILE_O_WRITE)) return 0;
    const size_t w = f.write(in, n);
    f.close();
    return w == n ? 1 : 0;
}

static int hal_store_erase(void *ctx) {
    (void)ctx;
    /* LittleFS 의 remove 는 블록을 즉시 지우지 않는다. 덮어쓰기부터 한다. */
    uint8_t z[NU_STORE_MAX];
    memset(z, 0xFF, sizeof z);
    File f(InternalFS);
    if (f.open(STORE_PATH, FILE_O_WRITE)) {
        f.write(z, sizeof z);
        f.close();
    }
    InternalFS.remove(STORE_PATH);
    return 1;
}

/* ── LED ────────────────────────────────────────────────────────────────── */

static void hal_leds(uint8_t mask, void *ctx) {
    (void)ctx;
    for (int i = 0; i < NU_BUTTON_COUNT; i++) {
        bool on = (mask >> i) & 1;
#if NU_LED_INVERT
        on = !on;
#endif
        digitalWrite(LEDS[i], on ? HIGH : LOW);
    }
}

/* ── 시간 ───────────────────────────────────────────────────────────────── */

static uint32_t hal_millis(void *ctx) { (void)ctx; return millis(); }

/* ── 버튼 ───────────────────────────────────────────────────────────────── */

struct Btn { bool raw, stable; uint32_t at; };
static Btn B[NU_BUTTON_COUNT];

int nu_arduino_poll_button(uint32_t now_ms) {
    int edge = -1;
    for (int i = 0; i < NU_BUTTON_COUNT; i++) {
        const bool r = (digitalRead(BTNS[i]) == LOW);      /* 풀업, 누르면 LOW */
        if (r != B[i].raw) { B[i].raw = r; B[i].at = now_ms; continue; }
        if (B[i].stable != B[i].raw && (now_ms - B[i].at) >= NU_DEBOUNCE_MS) {
            B[i].stable = B[i].raw;
            if (B[i].stable) edge = i;                     /* 눌림 엣지만 센다 */
        }
    }
    return edge;
}

/* ── 초기화 ─────────────────────────────────────────────────────────────── */

int nu_arduino_hal_init(nu_hal *hal) {
    for (int i = 0; i < NU_BUTTON_COUNT; i++) {
        pinMode(LEDS[i], OUTPUT);
        digitalWrite(LEDS[i], NU_LED_INVERT ? HIGH : LOW);
        pinMode(BTNS[i], INPUT_PULLUP);
        B[i].raw = B[i].stable = (digitalRead(BTNS[i]) == LOW);
        B[i].at = millis();
    }
    if (!InternalFS.begin()) return -1;

    hal->random      = hal_random;
    hal->store_read  = hal_store_read;
    hal->store_write = hal_store_write;
    hal->store_erase = hal_store_erase;
    hal->leds        = hal_leds;
    hal->millis      = hal_millis;
    hal->send        = NULL;            /* 스케치의 BLE 글루가 채운다 */
    hal->ctx         = NULL;
    return 0;
}

#endif /* ARDUINO */
