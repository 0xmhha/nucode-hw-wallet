/* Zephyr 전용. 다른 툴체인이 트리를 훑어도 이 파일은 비어 있게 둔다. */
#if defined(__ZEPHYR__)

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
#include <errno.h>
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

/* ── 수신 인박스 ────────────────────────────────────────────────────────────
 *
 * GATT write 콜백은 BT RX 스레드에서 돈다. 그 스택은 1KB 남짓인데 지갑 코어는
 * BIP-32 파생만 해도 ~1.9KB, PBKDF2 까지 가면 ~2.3KB 를 쓴다. 콜백 안에서
 * 코어를 부르면 스택이 넘쳐 보드가 죽고, 호스트에는 "연결이 끊겼다"로 보인다.
 * 그래서 여기서는 조립만 하고, 실제 처리는 메인 스레드(8KB)에서 한다.
 *
 * 프로토콜은 요청 하나가 끝나야 다음이 나가는 구조라 슬롯은 하나면 된다. */
static uint8_t   inbox[NU_MAX_MESSAGE];
static uint16_t  inbox_len;
static uint16_t  inbox_err;          /* 0 이 아니면 프레이밍 오류 */
static atomic_t  inbox_full;         /* 1 이면 메인 스레드가 처리해야 한다 */
K_SEM_DEFINE(rx_sem, 0, 1);

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
    if (r == 0) return len;                     /* 아직 조립 중 */

    /* 앞 요청을 메인 스레드가 아직 처리 중이면 받을 곳이 없다. 호스트가
     * 직렬화를 어긴 것이므로 ATT 오류로 즉시 알린다 — 조용히 버리면
     * 타임아웃만 나서 원인을 알 수 없다. */
    if (!atomic_cas(&inbox_full, 0, 1)) {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    if (r < 0) {
        inbox_err = (uint16_t)(-r);
        inbox_len = 0;
    } else {
        inbox_err = 0;
        inbox_len = (uint16_t)msg_len;
        memcpy(inbox, msg, msg_len);
    }
    k_sem_give(&rx_sem);                        /* 메인 루프를 즉시 깨운다 */
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

/* attrs[1] 은 TX 특성 선언. bt_gatt_notify 는 여기서 값 속성을 찾아간다.
 *
 * TX 버퍼는 몇 개 안 되므로 여러 패킷을 연달아 밀어 넣으면 -ENOMEM 이 난다.
 * 그냥 버리면 호스트 쪽 조립에서 SEQ 가 어긋나 메시지 전체가 깨진다.
 * 이 함수는 이제 메인 스레드에서만 불리므로 잠깐 자면서 기다려도 안전하다. */
static void send_packet(const uint8_t *pkt, size_t len, void *ctx) {
    ARG_UNUSED(ctx);
    for (int i = 0; i < 100; i++) {
        const int err = bt_gatt_notify(current, &nu_svc.attrs[1], pkt, len);
        if (err == 0) return;
        if (err != -ENOMEM) { LOG_WRN("notify 실패 (%d)", err); return; }
        k_sleep(K_MSEC(2));
    }
    LOG_ERR("notify TX 버퍼가 계속 모자랍니다 — 패킷을 버립니다");
}

/* 메인 루프가 부른다. 조립이 끝난 요청이 있으면 코어에 넘긴다.
 * 코어는 여기서, 즉 메인 스레드 스택에서 실행된다. */
void nu_ble_rx_poll(int timeout_ms) {
    if (k_sem_take(&rx_sem, K_MSEC(timeout_ms)) != 0) return;
    if (inbox_err) nu_wallet_framing_error(wallet, inbox_err);
    else           nu_wallet_handle(wallet, inbox, inbox_len);
    /* 응답을 다 내보낸 뒤에 슬롯을 연다. */
    atomic_clear(&inbox_full);
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
    atomic_clear(&inbox_full);
    k_sem_reset(&rx_sem);
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

#endif
