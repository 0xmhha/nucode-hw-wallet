#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(nu40_pet, LOG_LEVEL_INF);

#define PET_SERVICE_UUID BT_UUID_128_ENCODE(0x7d2a0001, 0x7b7a, 0x4f45, 0x8d68, 0x36f1a4d9c101)
#define PET_NOTIFY_UUID  BT_UUID_128_ENCODE(0x7d2a0002, 0x7b7a, 0x4f45, 0x8d68, 0x36f1a4d9c101)
#define PET_WRITE_UUID   BT_UUID_128_ENCODE(0x7d2a0003, 0x7b7a, 0x4f45, 0x8d68, 0x36f1a4d9c101)

static struct bt_uuid_128 service_uuid = BT_UUID_INIT_128(PET_SERVICE_UUID);
static struct bt_uuid_128 notify_uuid = BT_UUID_INIT_128(PET_NOTIFY_UUID);
static struct bt_uuid_128 write_uuid = BT_UUID_INIT_128(PET_WRITE_UUID);
static struct bt_conn *current_conn;
static bool notify_enabled;
static uint32_t blink_ms = 760;
static uint8_t mood_rgb = BIT(0) | BIT(1);

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec led_r = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led_g = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led_b = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);
static struct gpio_callback button_cb;
static struct k_work feed_work;

static void notify_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    notify_enabled = (value == BT_GATT_CCC_NOTIFY);
}

static ssize_t mood_written(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                            const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    char command[96];
    size_t size = MIN(len, sizeof(command) - 1);
    memcpy(command, buf, size);
    command[size] = '\0';

    if (strstr(command, "happy")) {
        mood_rgb = BIT(1);           /* green */
        blink_ms = 1400;
    } else if (strstr(command, "hungry")) {
        mood_rgb = BIT(0);           /* red */
        blink_ms = 260;
    } else if (strstr(command, "okay")) {
        mood_rgb = BIT(0) | BIT(1);  /* yellow */
        blink_ms = 760;
    }
    return len;
}

BT_GATT_SERVICE_DEFINE(pet_service,
    BT_GATT_PRIMARY_SERVICE(&service_uuid),
    BT_GATT_CHARACTERISTIC(&notify_uuid.uuid, BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE, NULL, NULL, NULL),
    BT_GATT_CCC(notify_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(&write_uuid.uuid, BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE, NULL, mood_written, NULL));

static void feed_work_handler(struct k_work *work)
{
    static const char event[] = "{\"type\":\"feed\"}";
    if (current_conn && notify_enabled) {
        bt_gatt_notify(current_conn, &pet_service.attrs[2], event, sizeof(event) - 1);
    }
}

static void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    static int64_t last_press;
    int64_t now = k_uptime_get();
    if (now - last_press > 180) {
        last_press = now;
        k_work_submit(&feed_work);
    }
}

static void connected(struct bt_conn *conn, uint8_t err)
{
    if (!err) current_conn = bt_conn_ref(conn);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    if (current_conn) { bt_conn_unref(current_conn); current_conn = NULL; }
    notify_enabled = false;
}

BT_CONN_CB_DEFINE(connection_callbacks) = { .connected = connected, .disconnected = disconnected };

static int hardware_init(void)
{
    const struct gpio_dt_spec *leds[] = { &led_r, &led_g, &led_b };
    if (!gpio_is_ready_dt(&button)) return -ENODEV;
    if (gpio_pin_configure_dt(&button, GPIO_INPUT) != 0) return -EIO;
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    gpio_init_callback(&button_cb, button_pressed, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb);
    for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
        if (!gpio_is_ready_dt(leds[i]) || gpio_pin_configure_dt(leds[i], GPIO_OUTPUT_INACTIVE) != 0) return -ENODEV;
    }
    return 0;
}

int main(void)
{
    bool on = false;
    k_work_init(&feed_work, feed_work_handler);
    if (hardware_init() != 0 || bt_enable(NULL) != 0) return 0;

    const struct bt_le_adv_param *params = BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_NAME,
                                                            BT_GAP_ADV_FAST_INT_MIN_2,
                                                            BT_GAP_ADV_FAST_INT_MAX_2, NULL);
    const struct bt_data ad[] = { BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
                                  BT_DATA_BYTES(BT_DATA_UUID128_ALL, PET_SERVICE_UUID) };
    bt_le_adv_start(params, ad, ARRAY_SIZE(ad), NULL, 0);

    while (1) {
        on = !on;
        gpio_pin_set_dt(&led_r, on && (mood_rgb & BIT(0)));
        gpio_pin_set_dt(&led_g, on && (mood_rgb & BIT(1)));
        gpio_pin_set_dt(&led_b, on && (mood_rgb & BIT(2)));
        k_msleep(blink_ms / 2);
    }
    return 0;
}
