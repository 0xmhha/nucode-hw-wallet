#line 1 "/Users/0xtopaz/work/github/0xmhha/nucode-hw-wallet/firmware/nuwallet/src/transport/ble_transport.cpp"
#include "ble_transport.h"
#include "../core/framing.h"
#include "../core/protocol.h"
#include "../core/wallet.h"
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

/* ── 수신 조립 ──────────────────────────────────────────────────────────────
 *
 * 프레이밍은 코어(core/framing.c)가 한다. 예전에는 여기에 같은 것을 한 벌 더
 * 두고 있었는데, 두 벌이 어긋나면 조용히 깨진 메시지가 된다.
 *
 * Bluefruit 의 write 콜백은 BLE 스택 태스크에서 돈다. 거기서 PBKDF2 와 ECC 를
 * 돌리면 연결 이벤트 처리가 굶어 supervision timeout 으로 링크가 끊긴다.
 * 완성된 요청을 한 개짜리 mailbox 로 넘기고 loop() 에서 처리한다. */
static nu_reasm  asm_in;
static nu_wallet *g_wallet;

static uint8_t dispatch_buf[NU_MAX_MESSAGE];
static volatile uint16_t dispatch_len = 0;
static volatile bool dispatch_ready = false;

static void asm_reset(void) { nu_reasm_init(&asm_in); }

static void framing_error(uint16_t status) {
    asm_reset();
    uint8_t e[4] = { (uint8_t)(status >> 8), (uint8_t)status, 0, 0 };
    nuble_send(NU_TAG_MESSAGE, e, 4);
}

static void rx_written(uint16_t conn_hdl, BLECharacteristic *chr,
                       uint8_t *data, uint16_t len) {
    (void)conn_hdl; (void)chr;

    const uint8_t *msg = NULL;
    size_t msg_len = 0;
    const int r = nu_reasm_push(&asm_in, data, len, &msg, &msg_len);
    if (r == 0) return;                          /* 아직 조립 중 */
    if (r < 0) { framing_error((uint16_t)(-r)); return; }

    /* 앞 요청을 loop() 가 아직 처리 중이면 받을 곳이 없다. 조용히 버리면
     * 타임아웃만 나서 원인을 알 수 없으므로 프레이밍 오류로 알린다. */
    if (dispatch_ready) { framing_error(NU_SW_FRAMING_ERROR); return; }
    memcpy(dispatch_buf, msg, msg_len);
    dispatch_len = (uint16_t)msg_len;
    dispatch_ready = true;
}

/* ── 송신 ────────────────────────────────────────────────────────────────── */

/* notify() 는 TX 버퍼가 빌 때까지 세마포어로 기다리므로 여기서 유실되지 않는다. */
static void send_packet(const uint8_t *pkt, size_t len, void *ctx) {
    (void)ctx;
    if (!chr_tx.notify(pkt, (uint16_t)len)) {
        /* 구독이 끊겼거나 링크가 죽었다. 다음 패킷도 나가지 못한다. */
    }
}

void nuble_send(uint8_t tag, const uint8_t *payload, uint16_t len) {
    if (!Bluefruit.connected()) return;
    /* 협상된 MTU 에서 ATT 헤더 3바이트를 뺀 것이 한 패킷의 최대 크기다. */
    uint16_t mtu = Bluefruit.Connection(0) ? Bluefruit.Connection(0)->getMtu() : 23;
    if (mtu < 23) mtu = 23;
    nu_frame(tag, payload, len, (size_t)(mtu - 3), send_packet, NULL);
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
    dispatch_ready = false;
    /* 코어에 알려 세션을 닫는다. 이걸 빠뜨리면 호스트가 끊긴 뒤에도 지갑이
     * 열린 채로 남는다 — 다음에 붙는 쪽이 PIN 없이 서명을 요청할 수 있다.
     * docs/protocol.md §8 은 "연결 해제 후 자동으로 잠긴다" 고 적고 있다. */
    if (g_wallet) nu_wallet_disconnected(g_wallet);
}

int nuble_connected(void) { return Bluefruit.connected() ? 1 : 0; }

int nuble_paired(void) {
    /* secured() = 링크 암호화됨, bonded() = 키가 저장됨.
     * 명령 처리 조건으로는 secured() 면 충분하다. */
    BLEConnection *c = Bluefruit.Connection(0);
    return (c && c->connected() && c->secured()) ? 1 : 0;
}

void nuble_begin(const char *device_name, nu_wallet *w) {
    g_wallet = w;
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

/* loop() 가 부른다. 조립이 끝난 요청을 코어에 넘긴다 — **메인 스택에서** 돈다.
 * BIP-32 파생과 PBKDF2 가 2KB 넘는 스택을 쓰므로 BLE 콜백에서 부르면 안 된다. */
void nuble_task(void) {
    if (!dispatch_ready) return;
    const uint16_t len = dispatch_len;
    nu_wallet_handle(g_wallet, dispatch_buf, len);
    memset(dispatch_buf, 0, len);
    dispatch_ready = false;          /* 응답을 다 내보낸 뒤에 슬롯을 연다 */
}
