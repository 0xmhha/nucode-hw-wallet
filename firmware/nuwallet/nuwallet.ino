// ============================================================================
//  NuWallet — NU40-DK BLE 하드웨어 지갑 (Ethereum)
//
//  ⚠️  실제 자금을 넣지 마세요. SECURITY.md 를 먼저 읽으세요.
//      nRF52840 에는 보안 요소가 없고 기기에 화면이 없습니다.
//
//  버튼 4개로 서명을 승인합니다. 기기가 랜덤 시퀀스를 LED 로 보여주고
//  사용자가 그대로 누릅니다. 시퀀스는 BLE 로 절대 나가지 않습니다.
//
//  빌드:  make build      업로드: make upload
//  FQBN:  nucode:nrf52:nu40dk
// ============================================================================

#include <Arduino.h>
#include <bluefruit.h>
#include <Adafruit_nRFCrypto.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>

#include "config.h"
#include "src/controller/protocol.h"
#include "src/transport/ble_transport.h"
#include "src/storage/keystore.h"
#include "src/ui/challenge.h"

static challenge_t CH;
static char g_name[24];

// ── 하드웨어 헬퍼 ────────────────────────────────────────────────────────────
static const uint8_t LEDS[4] = { LED_1, LED_2, LED_3, LED_4 };
static const uint8_t BTNS[4] = { BTN_1, BTN_2, BTN_3, BTN_4 };

static void led(uint8_t i, bool on);
static void buttons_begin();
static int buttons_poll(uint32_t now);
 extern "C" void nuwallet_rng(uint8_t *buf, uint16_t len);
 extern "C" const char * nuwallet_device_name(void);
 extern "C" void nuwallet_request_challenge(uint32_t request_id, uint8_t cmd);
static void make_name();
void setup();
void loop();
static void led(uint8_t i, bool on) {
#if LED_INVERT
  on = !on;
#endif
  on ? ledOn(LEDS[i]) : ledOff(LEDS[i]);
}

struct Btn { bool raw, stable; uint32_t at; };
static Btn B[4];

static void buttons_begin() {
  for (int i = 0; i < 4; i++) {
    pinMode(BTNS[i], INPUT_PULLUP);
    B[i].raw = B[i].stable = (digitalRead(BTNS[i]) == LOW);
    B[i].at = millis();
  }
}

// 눌림 엣지가 있으면 그 버튼 번호를, 없으면 -1 을 반환한다.
static int buttons_poll(uint32_t now) {
  int edge = -1;
  for (int i = 0; i < 4; i++) {
    const bool r = (digitalRead(BTNS[i]) == LOW);
    if (r != B[i].raw) { B[i].raw = r; B[i].at = now; continue; }
    if (B[i].stable != B[i].raw && (now - B[i].at) >= BTN_DEBOUNCE_MS) {
      B[i].stable = B[i].raw;
      if (B[i].stable) edge = i;
    }
  }
  return edge;
}

// ── 프로토콜 계층이 부르는 콜백 ──────────────────────────────────────────────
extern "C" void nuwallet_rng(uint8_t *buf, uint16_t len) {
  // CC310 TRNG. 니모닉 엔트로피와 관리 작업 챌린지에 쓴다.
  nRFCrypto.Random.generate(buf, len);
}

extern "C" const char *nuwallet_device_name(void) { return g_name; }

extern "C" void nuwallet_request_challenge(uint32_t request_id, uint8_t cmd) {
  ch_start(&CH, request_id, cmd, CH_STEPS_DEFAULT);
  proto_emit_challenge_started(request_id, CH.steps, cmd);
#if DEBUG_LOG
  Serial.print(F("[ch] 시작 req=")); Serial.print(request_id);
  Serial.print(F(" cmd=0x")); Serial.println(cmd, HEX);
#endif
}

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

// ============================================================================
void setup() {
  for (int i = 0; i < 4; i++) { pinMode(LEDS[i], OUTPUT); led(i, false); }
  buttons_begin();

  Serial.begin(CONSOLE_BAUD);
  make_name();

  // CryptoCell은 한 번 초기화한 뒤 TRNG와 Ed25519가 함께 사용한다.
  nRFCrypto.begin();

  keystore_begin();
  ch_init(&CH, nuwallet_rng);
  proto_init(nuble_send);
  nuble_begin(g_name);

#if DEBUG_LOG
  Serial.println();
  Serial.println(F("=== NuWallet ==="));
  Serial.print(F("기기 이름   ")); Serial.println(g_name);
  Serial.print(F("초기화됨    ")); Serial.println(keystore_is_initialized() ? F("예") : F("아니오"));
  Serial.println(F("⚠️  프로토타입입니다. 실제 자금을 넣지 마세요. SECURITY.md 참고."));
#endif

  // 초기화 전에는 LED1 을 켜 둔다 (셋업 필요 표시)
  led(0, !keystore_is_initialized());
}

void loop() {
  const uint32_t now = millis();

  // 버튼 -> 챌린지
  const int edge = buttons_poll(now);
  if (edge >= 0 && CH.state != CH_IDLE) {
    if (ch_button(&CH, (uint8_t)edge)) {
      proto_emit_challenge_progress(CH.request_id, CH.pos, CH.attempts_left);
    }
  }

  // 챌린지 진행 + LED 표시
  uint8_t want[4] = {0,0,0,0};
  ch_task(&CH, now, want);

  switch (CH.state) {
    case CH_APPROVED:
#if DEBUG_LOG
      Serial.println(F("[ch] 승인됨 — 서명 진행"));
#endif
      proto_execute_pending();
      ch_cancel(&CH);
      break;
    case CH_REJECTED:
#if DEBUG_LOG
      Serial.println(F("[ch] 거부됨 — 시도 횟수 소진"));
#endif
      proto_challenge_resolved(CH.request_id, SW_CHALLENGE_FAILED);
      ch_cancel(&CH);
      break;
    case CH_TIMEOUT:
#if DEBUG_LOG
      Serial.println(F("[ch] 시간 초과"));
#endif
      proto_challenge_resolved(CH.request_id, SW_CHALLENGE_TIMEOUT);
      ch_cancel(&CH);
      break;
    default:
      break;
  }

  // LED: 챌린지 중이면 챌린지가 우선, 아니면 상태 표시
  if (CH.state == CH_SHOWING || CH.state == CH_WAITING) {
    for (int i = 0; i < 4; i++) led(i, want[i]);
  } else {
    led(0, !keystore_is_initialized());               // 셋업 필요
    led(1, nuble_connected());                        // 연결됨
    led(2, nuble_paired());                           // 본딩됨
    led(3, false);
  }

  nuble_task();
}
