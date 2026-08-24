// ============================================================================
//  NuWallet — NU40-DK BLE 하드웨어 지갑 (Ethereum · Solana)
//
//  ⚠️  실제 자금을 넣지 마세요. SECURITY.md 를 먼저 읽으세요.
//      nRF52840 에는 보안 요소가 없고 기기에 화면이 없습니다.
//
//  지갑 로직은 전부 src/core/ 에 있습니다. 그쪽은 Arduino 도 Zephyr 도 모르고
//  src/core/hal.h 만 압니다 — 그래서 같은 코드가 보드와 호스트 테스트에서
//  똑같이 돕니다 (firmware/test).
//
//  이 스케치가 하는 일은 셋뿐입니다.
//    1. HAL 과 BLE 를 올린다
//    2. 버튼과 BLE 요청을 코어에 넘긴다
//    3. 코어를 틱 시킨다 (타임아웃 · LED · 세션 유휴 · 공장 초기화)
//
//  코어는 **오직 loop() 에서만** 실행됩니다. 재진입을 가정하지 않기도 하고,
//  BIP-32 파생과 PBKDF2 가 2KB 넘는 스택을 쓰기 때문이기도 합니다 — BLE 콜백
//  스택에서 부르면 넘칩니다.
//
//  빌드:  arduino-cli compile --fqbn nucode:nrf52:nu40dk --output-dir ./build .
// ============================================================================

#include <Arduino.h>
#include <bluefruit.h>
#include <Adafruit_nRFCrypto.h>

#include "config.h"
#include "src/core/wallet.h"
#include "src/port/arduino/hal_arduino.h"
#include "src/transport/ble_transport.h"

static nu_hal    HAL;
static nu_wallet W;
static char      g_name[24];

// ── 기기 이름 ────────────────────────────────────────────────────────────────
// DEVICEID 로 보드마다 고유하게, 재부팅해도 동일하게.
static void make_name() {
  static const char AB[] = "ABCDEFGHJKLMNPQRSTUVWXYZ";  // I, O 제외
  static const char NU[] = "23456789";                  // 0, 1 제외
  uint32_t id0 = NRF_FICR->DEVICEID[0];
  uint32_t id1 = NRF_FICR->DEVICEID[1];
  uint32_t h = id0 ^ (id1 * 2654435761UL);
  char sfx[7];
  for (int i = 0; i < 6; i++) {
    if (i % 2 == 0) { sfx[i] = AB[h % 24]; h /= 24; }
    else            { sfx[i] = NU[h % 8];  h /= 8;  }
    if (h == 0) h = id1 ^ (uint32_t)(i * 0x9E3779B9UL);
  }
  sfx[6] = 0;
  snprintf(g_name, sizeof g_name, "%s%s", BLE_NAME_PREFIX, sfx);
}

// 코어의 hal->send 가 이걸 부른다. 프레이밍은 전송 계층이 한다.
static void hal_send(uint8_t tag, const uint8_t *msg, size_t len, void *ctx) {
  (void)ctx;
  nuble_send(tag, msg, (uint16_t)len);
}

// ============================================================================
void setup() {
  Serial.begin(CONSOLE_BAUD);
  make_name();

  // CryptoCell 은 한 번 초기화한 뒤 TRNG 와 Ed25519 가 함께 쓴다.
  nRFCrypto.begin();

  if (nu_arduino_hal_init(&HAL)) {
#if DEBUG_LOG
    Serial.println(F("HAL 초기화 실패 — 멈춥니다"));
#endif
    for (;;) delay(1000);
  }
  HAL.send = hal_send;

  nu_wallet_init(&W, &HAL, g_name);
  nuble_begin(g_name, &W);

#if DEBUG_LOG
  Serial.println();
  Serial.println(F("=== NuWallet ==="));
  Serial.print(F("기기 이름   ")); Serial.println(g_name);
  Serial.print(F("초기화됨    "));
  Serial.println((nu_wallet_flags(&W) & NU_FLAG_INITIALIZED) ? F("예") : F("아니오"));
  Serial.println(F("⚠️  프로토타입입니다. 실제 자금을 넣지 마세요. SECURITY.md 참고."));
#endif
}

void loop() {
  const uint32_t now = millis();

  // 버튼 — 눌림 엣지 하나를 코어에 넘긴다. 공장 초기화의 "붙잡고 있음" 은
  // 코어가 HAL 의 buttons() 로 직접 본다.
  const int edge = nu_arduino_poll_button(now);
  if (edge >= 0) nu_wallet_button(&W, (uint8_t)edge);

  // BLE 요청 — 조립은 콜백에서 끝났고, 실행은 여기(메인 스택)에서 한다.
  nuble_task();

  /* 타임아웃 · LED · 세션 유휴 · 공장 초기화 카운트다운.
   *
   * 위에서 캐시한 now 가 아니라 **지금** 시각을 넘긴다. 사이에 들어간
   * nuble_task() 가 PBKDF2 를 돌리면 몇 초가 지나 있고, 캐시한 값을 넘기면
   * 코어가 "시간이 뒤로 갔다" 를 보게 된다. 코어도 nu_elapsed() 로 방어하지만
   * 애초에 어긋난 값을 주지 않는 것이 맞다. */
  nu_wallet_tick(&W, millis());
}
