/* Arduino(Bluefruit) HAL. */
#if defined(ARDUINO)

#include "hal_arduino.h"
#include "../../core/protocol.h"

#include "../../chains/solana/solana.h"

#include <Arduino.h>
#include <bluefruit.h>
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

/* ── Ed25519 (Solana) ───────────────────────────────────────────────────────
 * CryptoCell 하드웨어에만 있어서 코어가 직접 하지 못한다. secp256k1 은
 * micro-ecc 로 코어가 직접 한다 — 곡선 하나만 HAL 에 있는 이유다. */

static int hal_ed25519_pub(const uint8_t key[32], uint8_t pub[32], void *ctx) {
    (void)ctx;
    return solana_public_key(key, pub);
}

static int hal_ed25519_sign(const uint8_t key[32], const uint8_t *msg, size_t len,
                            uint8_t sig[64], void *ctx) {
    (void)ctx;
    return solana_sign(key, msg, len, sig);
}

/* ── 본딩 ───────────────────────────────────────────────────────────────────
 * 공장 초기화가 부른다. 시드만 지우고 본딩을 남기면, 보드는 초기화됐는데
 * 호스트는 옛 페어링 키를 계속 써서 다음 연결이 알림 구독에서 끊긴다.
 * 사용자는 원인을 찾을 수 없다. */

static int hal_bonds_erase(void *ctx) {
    (void)ctx;
    Bluefruit.Periph.clearBonds();
    Bluefruit.Central.clearBonds();
    return 1;
}

/* ── 버튼 ───────────────────────────────────────────────────────────────── */

struct Btn { bool raw, stable; uint32_t at; };
static Btn B[NU_BUTTON_COUNT];

/* 지금 눌려 있는 버튼의 마스크. 공장 초기화가 "두 버튼을 붙잡고 있는지" 를
 * 알아야 하는데, 눌림 엣지만으로는 알 수 없다. */
static uint8_t hal_buttons(void *ctx) {
    (void)ctx;
    uint8_t m = 0;
    for (int i = 0; i < NU_BUTTON_COUNT; i++) {
        if (B[i].stable) m |= (uint8_t)(1u << i);
    }
    return m;
}

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
    hal->buttons     = hal_buttons;
    hal->bonds_erase = hal_bonds_erase;
    hal->ed25519_pub  = hal_ed25519_pub;
    hal->ed25519_sign = hal_ed25519_sign;
    hal->send        = NULL;            /* 스케치의 BLE 글루가 채운다 */
    hal->ctx         = NULL;
    return 0;
}

#endif /* ARDUINO */
