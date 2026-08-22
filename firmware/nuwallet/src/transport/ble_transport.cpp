#line 1 "/Users/0xtopaz/work/github/0xmhha/nucode-hw-wallet/firmware/nuwallet/src/transport/ble_transport.cpp"
#include "ble_transport.h"
#include "../controller/protocol.h"
#include <bluefruit.h>
#include <string.h>

/* docs/protocol.md §1 의 UUID.
 * Bluefruit 은 128비트 UUID 를 리틀엔디언 바이트 배열로 받는다 —
 * 문자열 표기의 역순이다. */
#define UUID128(a,b,c,d,e,f,g,h, i,j,k,l, m,n,o,p) \
  { p,o,n,m, l,k,j,i, h,g,f,e, d,c,b,a }

/* 6e754000-7761-4c4c-4554-000000000001 */
static const uint8_t UUID_SVC[16] = UUID128(
  0x6e,0x75,0x40,0x00, 0x77,0x61, 0x4c,0x4c, 0x45,0x54, 0x00,0x00,0x00,0x00,0x00,0x01);
/* ...0002  RX (호스트 -> 기기) */
static const uint8_t UUID_RX[16] = UUID128(
  0x6e,0x75,0x40,0x00, 0x77,0x61, 0x4c,0x4c, 0x45,0x54, 0x00,0x00,0x00,0x00,0x00,0x02);
/* ...0003  TX (기기 -> 호스트, notify) */
static const uint8_t UUID_TX[16] = UUID128(
  0x6e,0x75,0x40,0x00, 0x77,0x61, 0x4c,0x4c, 0x45,0x54, 0x00,0x00,0x00,0x00,0x00,0x03);

static BLEService        svc(UUID_SVC);
static BLECharacteristic chr_rx(UUID_RX);
static BLECharacteristic chr_tx(UUID_TX);

/* ── 수신 조립 ──────────────────────────────────────────────────────────── */
static uint8_t  asm_buf[PROTO_MAX_MSG];
static uint16_t asm_total = 0;
static uint16_t asm_have  = 0;
static uint16_t asm_seq   = 0;

/* Bluefruit write callback은 BLE 스택 태스크에서 실행된다. 여기서 PBKDF2와
 * ECC를 직접 수행하면 연결 이벤트 처리가 굶어 supervision timeout으로 링크가
 * 끊어진다. 완성된 요청을 한 개짜리 mailbox로 넘기고 loop()에서 처리한다. */
static uint8_t dispatch_buf[PROTO_MAX_MSG];
static volatile uint16_t dispatch_len = 0;
static volatile bool dispatch_ready = false;

static void asm_reset(void) { asm_total = asm_have = asm_seq = 0; }

static void framing_error(void) {
    asm_reset();
    uint8_t e[4] = { (uint8_t)(SW_FRAMING_ERROR >> 8), (uint8_t)SW_FRAMING_ERROR, 0, 0 };
    nuble_send(TAG_MESSAGE, e, 4);
}

static void rx_written(uint16_t conn_hdl, BLECharacteristic *chr, uint8_t *data, uint16_t len) {
    (void)conn_hdl; (void)chr;
    if (len < 3) { framing_error(); return; }

    const uint16_t seq = (uint16_t)((data[1] << 8) | data[2]);
    if (seq == 0) {
        if (len < 5) { framing_error(); return; }
        asm_total = (uint16_t)((data[3] << 8) | data[4]);
        if (asm_total > PROTO_MAX_MSG) { framing_error(); return; }
        asm_have = 0; asm_seq = 1;
        const uint16_t n = (uint16_t)(len - 5);
        if (n > asm_total) { framing_error(); return; }
        memcpy(asm_buf, data + 5, n);
        asm_have = n;
    } else {
        if (seq != asm_seq) { framing_error(); return; }
        asm_seq++;
        const uint16_t n = (uint16_t)(len - 3);
        if (asm_have + n > asm_total) { framing_error(); return; }
        memcpy(asm_buf + asm_have, data + 3, n);
        asm_have = (uint16_t)(asm_have + n);
    }

    if (asm_have >= asm_total) {
        const uint16_t total = asm_total;
        if (dispatch_ready) { framing_error(); return; }
        memcpy(dispatch_buf, asm_buf, total);
        dispatch_len = total;
        dispatch_ready = true;
        asm_reset();
    }
}

/* ── 송신 ────────────────────────────────────────────────────────────────── */
void nuble_send(uint8_t tag, const uint8_t *payload, uint16_t len) {
    if (!Bluefruit.connected()) return;

    /* 협상된 MTU 에서 ATT 헤더 3바이트를 뺀 것이 한 번에 보낼 수 있는 양이다. */
    uint16_t mtu = Bluefruit.Connection(0) ? Bluefruit.Connection(0)->getMtu() : 23;
    if (mtu < 23) mtu = 23;
    uint16_t chunk = (uint16_t)(mtu - 3);
    if (chunk > 244) chunk = 244;

    uint8_t pkt[247];
    uint16_t off = 0, seq = 0;
    do {
        const uint16_t head = (seq == 0) ? 5 : 3;
        uint16_t room = (uint16_t)(chunk > head ? chunk - head : 1);
        uint16_t n = (uint16_t)(len - off);
        if (n > room) n = room;
        pkt[0] = tag;
        pkt[1] = (uint8_t)(seq >> 8); pkt[2] = (uint8_t)seq;
        if (seq == 0) { pkt[3] = (uint8_t)(len >> 8); pkt[4] = (uint8_t)len; }
        if (n) memcpy(pkt + head, payload + off, n);
        chr_tx.notify(pkt, (uint16_t)(head + n));
        off = (uint16_t)(off + n);
        seq++;
    } while (off < len);
}

/* ── 연결 콜백 ───────────────────────────────────────────────────────────── */
static void on_connect(uint16_t conn_hdl) {
    BLEConnection *c = Bluefruit.Connection(conn_hdl);
    if (c) c->requestPHY();
    asm_reset();
}
static void on_disconnect(uint16_t conn_hdl, uint8_t reason) {
    (void)conn_hdl; (void)reason;
    asm_reset();
}

int nuble_connected(void) { return Bluefruit.connected() ? 1 : 0; }

int nuble_paired(void) {
    /* secured() = 링크 암호화됨, bonded() = 키가 저장됨.
     * 명령 처리 조건으로는 secured() 면 충분하다. */
    BLEConnection *c = Bluefruit.Connection(0);
    return (c && c->connected() && c->secured()) ? 1 : 0;
}

void nuble_begin(const char *device_name) {
    Bluefruit.begin();
    Bluefruit.setTxPower(4);
    Bluefruit.setName(device_name);
    Bluefruit.Periph.setConnectCallback(on_connect);
    Bluefruit.Periph.setDisconnectCallback(on_disconnect);

    /* 본딩 강제. 페어링 없이는 특성 접근을 막는다.
     * 버튼 4개·LED 4개로는 Numeric Comparison 을 제대로 못 해서 Just Works 로
     * 떨어진다 — MITM 방어가 안 된다. SECURITY.md §4 참고. */
    Bluefruit.Security.setIOCaps(false, false, false);
    Bluefruit.Security.setMITM(false);

    svc.begin();

    chr_rx.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
    chr_rx.setPermission(SECMODE_ENC_NO_MITM, SECMODE_ENC_NO_MITM);
    chr_rx.setMaxLen(247);
    chr_rx.setWriteCallback(rx_written);
    chr_rx.begin();

    chr_tx.setProperties(CHR_PROPS_NOTIFY);
    chr_tx.setPermission(SECMODE_ENC_NO_MITM, SECMODE_NO_ACCESS);
    chr_tx.setMaxLen(247);
    chr_tx.begin();

    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addService(svc);
    Bluefruit.ScanResponse.addName();
    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);
    Bluefruit.Advertising.setFastTimeout(30);
    Bluefruit.Advertising.start(0);
}

void nuble_task(void) {
    if (!dispatch_ready) return;
    const uint16_t len = dispatch_len;
    dispatch_ready = false;
    proto_handle(dispatch_buf, len);
    memset(dispatch_buf, 0, len);
}
