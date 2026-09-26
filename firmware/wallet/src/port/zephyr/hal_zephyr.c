/* Zephyr 전용. 다른 툴체인이 트리를 훑어도 이 파일은 비어 있게 둔다. */
#if defined(__ZEPHYR__)

/* Zephyr HAL.
 *
 *  난수 : sys_csrand_get() — nRF52840 의 RNG 주변장치를 쓴다
 *  저장 : settings 서브시스템 (NVS). 지갑 레코드 한 덩어리만 넣는다
 *  LED  : devicetree 의 led0..led3 별칭
 *  버튼 : sw0..sw3 별칭. 인터럽트 -> 디바운스 워크 -> 콜백                 */
#include "hal_zephyr.h"
#include "core/protocol.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <string.h>

#if __has_include(<zephyr/random/random.h>)
#include <zephyr/random/random.h>
#else
#include <zephyr/random/rand32.h>
#endif

LOG_MODULE_REGISTER(nu_hal, LOG_LEVEL_INF);

#define LED_NODE(n) DT_ALIAS(led##n)
#define SW_NODE(n)  DT_ALIAS(sw##n)

static const struct gpio_dt_spec leds[NU_BUTTON_COUNT] = {
    GPIO_DT_SPEC_GET_OR(LED_NODE(0), gpios, {0}),
    GPIO_DT_SPEC_GET_OR(LED_NODE(1), gpios, {0}),
    GPIO_DT_SPEC_GET_OR(LED_NODE(2), gpios, {0}),
    GPIO_DT_SPEC_GET_OR(LED_NODE(3), gpios, {0}),
};
static const struct gpio_dt_spec buttons[NU_BUTTON_COUNT] = {
    GPIO_DT_SPEC_GET_OR(SW_NODE(0), gpios, {0}),
    GPIO_DT_SPEC_GET_OR(SW_NODE(1), gpios, {0}),
    GPIO_DT_SPEC_GET_OR(SW_NODE(2), gpios, {0}),
    GPIO_DT_SPEC_GET_OR(SW_NODE(3), gpios, {0}),
};
static struct gpio_callback btn_cb[NU_BUTTON_COUNT];

/* ── 저장 ───────────────────────────────────────────────────────────────── */

static uint8_t rec[NU_STORE_MAX];
static size_t  rec_len;

static int rec_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    if (settings_name_next(name, NULL) != 0) return -ENOENT;
    if (len > sizeof rec) return -EINVAL;
    const ssize_t n = read_cb(cb_arg, rec, len);
    if (n < 0) return (int)n;
    rec_len = (size_t)n;
    return 0;
}
SETTINGS_STATIC_HANDLER_DEFINE(nuwallet, "nuwallet/rec", NULL, rec_set, NULL, NULL);

static size_t hal_store_read(uint8_t *out, size_t max, void *ctx) {
    ARG_UNUSED(ctx);
    if (!rec_len) return 0;
    const size_t n = rec_len < max ? rec_len : max;
    memcpy(out, rec, n);
    return n;
}
static int hal_store_write(const uint8_t *in, size_t n, void *ctx) {
    ARG_UNUSED(ctx);
    if (n > sizeof rec) return 0;
    memcpy(rec, in, n);
    rec_len = n;
    return settings_save_one("nuwallet/rec", rec, rec_len) == 0;
}
static int hal_store_erase(void *ctx) {
    ARG_UNUSED(ctx);
    memset(rec, 0, sizeof rec);
    rec_len = 0;
    return settings_delete("nuwallet/rec") == 0;
}

/* ── 난수 ───────────────────────────────────────────────────────────────── */

static int hal_random(uint8_t *out, size_t n, void *ctx) {
    ARG_UNUSED(ctx);
    /* sys_csrand_get 은 엔트로피가 모자라면 실패한다. 실패를 삼키면 안 된다 —
     * 예측 가능한 니모닉이 만들어진다. */
    return sys_csrand_get(out, n) == 0;
}

/* ── LED ────────────────────────────────────────────────────────────────── */

static void hal_leds(uint8_t mask, void *ctx) {
    ARG_UNUSED(ctx);
    for (int i = 0; i < NU_BUTTON_COUNT; i++) {
        if (!device_is_ready(leds[i].port)) continue;
        gpio_pin_set_dt(&leds[i], (mask >> i) & 1);
    }
}

/* ── 시간 ───────────────────────────────────────────────────────────────── */

static uint32_t hal_millis(void *ctx) {
    ARG_UNUSED(ctx);
    return k_uptime_get_32();
}

/* ── 버튼 ───────────────────────────────────────────────────────────────── */

static nu_button_cb button_cb;
static struct k_work_delayable debounce[NU_BUTTON_COUNT];
static atomic_t pending_mask;

void nu_zephyr_set_button_cb(nu_button_cb cb) { button_cb = cb; }

static void debounce_fn(struct k_work *work) {
    struct k_work_delayable *dw = k_work_delayable_from_work(work);
    for (int i = 0; i < NU_BUTTON_COUNT; i++) {
        if (dw != &debounce[i]) continue;
        atomic_clear_bit(&pending_mask, i);
        /* 여전히 눌려 있을 때만 유효한 입력으로 본다. */
        if (gpio_pin_get_dt(&buttons[i]) > 0 && button_cb) button_cb((uint8_t)i);
        return;
    }
}

static void btn_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins) {
    ARG_UNUSED(port);
    ARG_UNUSED(cb);
    for (int i = 0; i < NU_BUTTON_COUNT; i++) {
        if (!(pins & BIT(buttons[i].pin))) continue;
        /* 이미 디바운스 대기 중이면 무시 — 채터링으로 여러 번 세지 않는다. */
        if (atomic_test_and_set_bit(&pending_mask, i)) continue;
        k_work_schedule(&debounce[i], K_MSEC(30));
    }
}

/* ── 초기화 ─────────────────────────────────────────────────────────────── */

int nu_zephyr_hal_init(nu_hal *hal) {
    for (int i = 0; i < NU_BUTTON_COUNT; i++) {
        if (device_is_ready(leds[i].port)) {
            gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_INACTIVE);
        } else {
            LOG_WRN("led%d 별칭이 없습니다", i);
        }
        k_work_init_delayable(&debounce[i], debounce_fn);
        if (!device_is_ready(buttons[i].port)) {
            LOG_WRN("sw%d 별칭이 없습니다 — 이 버튼은 동작하지 않습니다", i);
            continue;
        }
        gpio_pin_configure_dt(&buttons[i], GPIO_INPUT);
        gpio_pin_interrupt_configure_dt(&buttons[i], GPIO_INT_EDGE_TO_ACTIVE);
        gpio_init_callback(&btn_cb[i], btn_isr, BIT(buttons[i].pin));
        gpio_add_callback(buttons[i].port, &btn_cb[i]);
    }

    const int err = settings_subsys_init();
    if (err) { LOG_ERR("settings 초기화 실패 (%d)", err); return err; }

    hal->random      = hal_random;
    hal->store_read  = hal_store_read;
    hal->store_write = hal_store_write;
    hal->store_erase = hal_store_erase;
    hal->leds        = hal_leds;
    hal->millis      = hal_millis;
    hal->send        = NULL;          /* ble.c 가 채운다 */
    hal->ctx         = NULL;
    return 0;
}

#endif
