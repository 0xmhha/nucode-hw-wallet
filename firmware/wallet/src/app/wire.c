/* 호스트로 나가는 바이트.
 *
 * 응답·이벤트 인코딩과 LED 출력만 한다. 무엇을 보낼지는 정하지 않는다 —
 * 그건 commands.c 와 challenge.c 의 일이다.                                */
#include "internal.h"
#include "store.h"
#include <string.h>

/* ── 작은 유틸 ──────────────────────────────────────────────────────────── */
void nu_be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
void nu_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}
uint32_t nu_rd32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

void nu_leds(nu_wallet *w, uint8_t mask) {
    if (mask == w->led_mask) return;
    w->led_mask = mask;
    w->hal->leds(mask, w->hal->ctx);
}

/* 송신 조립 버퍼.
 *
 * 스택에 두면 nu_reply() 가 불리는 깊은 프레임마다 그만큼 얹힌다 — 코어는 이미
 * BIP-32·PBKDF2 로 2KB 넘게 쓴다. 코어는 메인 스레드 한 곳에서만 돌고
 * (main.c 참고) reply/event 는 hal->send 가 끝나기 전에 돌아오지 않으므로,
 * 파일 정적 버퍼 하나를 돌려 써도 안전하다.
 *
 * 크기는 프레이밍 한계와 맞춘다. 예전에는 128바이트라서 그보다 큰 응답이
 * 조용히 DEVICE_ERROR 로 바뀌었다 — 2048바이트까지 쪼개 보낼 수 있는데도. */
static uint8_t tx_buf[NU_MAX_MESSAGE];

void nu_reply(nu_wallet *w, uint16_t status, const uint8_t *payload, size_t len) {
    if (len > sizeof tx_buf - 4) { status = NU_SW_TOO_LARGE; len = 0; }
    nu_be16(tx_buf, status);
    nu_be16(tx_buf + 2, (uint16_t)len);
    if (len) memcpy(tx_buf + 4, payload, len);
    w->hal->send(NU_TAG_MESSAGE, tx_buf, 4 + len, w->hal->ctx);
}

/* 니모닉은 최대 24*2 = 48바이트다. */
void nu_reply_words(nu_wallet *w, const uint16_t *words, uint8_t count) {
    uint8_t p[1 + 24 * 2];
    p[0] = count;
    for (uint8_t i = 0; i < count; i++) nu_be16(p + 1 + i * 2, words[i]);
    nu_reply(w, NU_SW_OK, p, 1 + (size_t)count * 2);
}

void nu_event(nu_wallet *w, uint8_t evt, const uint8_t *payload, size_t len) {
    if (len > sizeof tx_buf - 3) return;
    tx_buf[0] = evt;
    nu_be16(tx_buf + 1, (uint16_t)len);
    if (len) memcpy(tx_buf + 3, payload, len);
    w->hal->send(NU_TAG_EVENT, tx_buf, 3 + len, w->hal->ctx);
}

uint8_t nu_wallet_flags(const nu_wallet *w) {
    uint8_t f = 0;
    if (w->rec_len) f |= NU_FLAG_INITIALIZED;
    if (w->rec_len && !w->unlocked) f |= NU_FLAG_LOCKED;
    if (w->req.cmd) f |= NU_FLAG_CHALLENGE_ACTIVE;
    if (w->pin_len || (w->rec_len && nu_store_has_pin(w->rec))) f |= NU_FLAG_HAS_PIN;
    return f;
}

void nu_emit_state(nu_wallet *w) {
    const uint8_t f = nu_wallet_flags(w);
    nu_event(w, NU_EVT_DEVICE_STATE, &f, 1);
}
