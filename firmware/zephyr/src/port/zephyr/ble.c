#include "ble.h"
#include "../../app/framing.h"
#include "../../app/protocol.h"

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(nu_ble, LOG_LEVEL_INF);

static struct bt_uuid_128 uuid_svc = BT_UUID_INIT_128(NU_UUID128_SERVICE);
static struct bt_uuid_128 uuid_rx  = BT_UUID_INIT_128(NU_UUID128_RX);
static struct bt_uuid_128 uuid_tx  = BT_UUID_INIT_128(NU_UUID128_TX);

static struct bt_conn *current;
static nu_wallet      *wallet;
static nu_reasm        asm_in;
static bool            notify_on;
static char            dev_name[24];

/* ── 송신 ───────────────────────────────────────────────────────────────── */

static void send_packet(const uint8_t *pkt, size_t len, void *ctx);

static void hal_send(uint8_t tag, const uint8_t *msg, size_t len, void *ctx) {
    ARG_UNUSED(ctx);
    if (!current || !notify_on) return;
    /* ATT MTU 에서 notify 헤더 3바이트를 뺀 값이 한 패킷의 최대 크기다. */
    size_t mtu = bt_gatt_get_mtu(current);
    mtu = (mtu > 3) ? mtu - 3 : 20;
    nu_frame(tag, msg, len, mtu, send_packet, NULL);
}

/* ── 수신 ───────────────────────────────────────────────────────────────── */

static ssize_t on_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                        const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    ARG_UNUSED(attr); ARG_UNUSED(flags);
    if (offset) return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);

    /* 본딩되지 않은 연결은 아무것도 하지 못한다. 특성 권한으로도 막히지만
     * 스택 설정 실수를 대비해 한 번 더 본다 — SECURITY.md §4. */
    if (bt_conn_get_security(conn) < BT_SECURITY_L2) {
        return BT_GATT_ERR(BT_ATT_ERR_AUTHENTICATION);
    }

    const uint8_t *msg = NULL;
    size_t msg_len = 0;
    const int r = nu_reasm_push(&asm_in, buf, len, &msg, &msg_len);
    if (r < 0) {
        nu_wallet_framing_error(wallet, (uint16_t)(-r));
    } else if (r == 1) {
        nu_wallet_handle(wallet, msg, msg_len);
    }
    return len;
}

static void ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    ARG_UNUSED(attr);
    notify_on = (value == BT_GATT_CCC_NOTIFY);
}

BT_GATT_SERVICE_DEFINE(nu_svc,
    BT_GATT_PRIMARY_SERVICE(&uuid_svc),
    BT_GATT_CHARACTERISTIC(&uuid_tx.uuid, BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_NONE, NULL, NULL, NULL),
    BT_GATT_CCC(ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_CHARACTERISTIC(&uuid_rx.uuid,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE_ENCRYPT, NULL, on_write, NULL),
);

/* attrs[1] 은 TX 특성 선언. bt_gatt_notify 는 여기서 값 속성을 찾아간다. */
static void send_packet(const uint8_t *pkt, size_t len, void *ctx) {
    ARG_UNUSED(ctx);
    const int err = bt_gatt_notify(current, &nu_svc.attrs[1], pkt, len);
    if (err) LOG_WRN("notify 실패 (%d)", err);
}

/* ── 연결 ───────────────────────────────────────────────────────────────── */

static void connected(struct bt_conn *conn, uint8_t err) {
    if (err) { LOG_WRN("연결 실패 (%u)", err); return; }
    current = bt_conn_ref(conn);
    nu_reasm_init(&asm_in);
    /* 페어링/암호화를 우리 쪽에서 먼저 요구한다. */
    if (bt_conn_set_security(conn, BT_SECURITY_L2)) {
        LOG_WRN("보안 수준을 올리지 못했습니다");
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(reason);
    if (current) { bt_conn_unref(current); current = NULL; }
    notify_on = false;
    nu_reasm_init(&asm_in);
    nu_wallet_disconnected(wallet);   /* 진행 중 요청 폐기 + 재잠금 */
    ARG_UNUSED(conn);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
};

int nu_ble_connected(void) { return current != NULL; }

/* ── 시작 ───────────────────────────────────────────────────────────────── */

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    /* Web Bluetooth 가 filters.services 로 잡으려면 광고에 UUID 가 있어야 한다. */
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, NU_UUID128_SERVICE),
};

/* 기기 고유 이름 — 재부팅해도 같아야 사용자가 알아본다. */
static void make_name(char *out, size_t cap) {
    uint8_t id[8] = {0};
    const ssize_t n = hwinfo_get_device_id(id, sizeof id);
    const unsigned a = (n >= 3) ? id[0] : 0;
    const unsigned b = (n >= 3) ? id[1] : 0;
    const unsigned c = (n >= 3) ? id[2] : 0;
    snprintk(out, cap, "NuWallet-%02X%02X%02X", a, b, c);
}

int nu_ble_start(nu_wallet *w, nu_hal *hal, char *name_out, size_t name_cap) {
    wallet = w;
    hal->send = hal_send;
    nu_reasm_init(&asm_in);

    int err = bt_enable(NULL);
    if (err) { LOG_ERR("bt_enable 실패 (%d)", err); return err; }

    /* 본딩 정보를 유지하려면 설정을 불러와야 한다. */
    if (IS_ENABLED(CONFIG_BT_SETTINGS)) settings_load();

    make_name(dev_name, sizeof dev_name);
    err = bt_set_name(dev_name);
    if (err) LOG_WRN("이름 설정 실패 (%d)", err);
    if (name_out) strncpy(name_out, dev_name, name_cap - 1);

    const struct bt_data sd[] = {
        BT_DATA(BT_DATA_NAME_COMPLETE, dev_name, (uint8_t)strlen(dev_name)),
    };
#ifdef BT_LE_ADV_CONN_FAST_1
    const struct bt_le_adv_param *adv = BT_LE_ADV_CONN_FAST_1;   /* Zephyr 3.6+ */
#else
    const struct bt_le_adv_param *adv = BT_LE_ADV_CONN;
#endif
    err = bt_le_adv_start(adv, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err) { LOG_ERR("광고 시작 실패 (%d)", err); return err; }

    LOG_INF("%s 광고 시작", dev_name);
    return 0;
}
