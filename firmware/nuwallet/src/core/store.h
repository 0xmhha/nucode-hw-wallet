#ifndef NUWALLET_STORE_H
#define NUWALLET_STORE_H
/* 니모닉을 PIN 으로 봉인해서 플래시에 넣는 레코드.
 *
 * 레이아웃 (v1, 최대 104바이트)
 *   0..3    MAGIC       "NUW1"
 *   4       VERSION     1
 *   5       FLAGS       bit0 has_pin
 *   6       PIN_LEN     0..8   (PIN 자체는 저장하지 않는다. 길이만 UI 용으로)
 *   7       WORD_COUNT  12..24
 *   8..23   SALT        16
 *   24..55  MAC         32   HMAC-SHA256(kmac, raw[0..24) ‖ CT)
 *   56..    CT          WORD_COUNT*2   워드 인덱스 big-endian u16 배열 XOR 키스트림
 *   맨끝     TRIES       1   남은 PIN 시도 횟수. **MAC 밖이다.**
 *
 * TRIES 가 MAC 밖에 있는 이유: 틀린 PIN 으로 시도했을 때도 카운터를 줄여
 * 저장해야 하는데, MAC 을 다시 계산하려면 올바른 PIN 이 필요하다. MAC 밖이라
 * 플래시를 만질 수 있는 공격자는 카운터를 되돌릴 수 있지만, 그런 공격자는
 * 어차피 CT 와 SALT 를 그대로 떠 가서 오프라인으로 PIN 을 깰 수 있다.
 * 이 바이트가 막는 것은 "보드를 주웠고 버튼만 누를 수 있는" 공격자다.
 * 예전에는 카운터가 RAM 에만 있어서 전원만 껐다 켜면 무한히 시도할 수 있었다.
 *
 * 키 유도
 *   K     = PBKDF2-HMAC-SHA512(PIN, SALT, 4096)   64바이트
 *   kenc  = K[0..32),  kmac = K[32..64)
 *   블록i = HMAC-SHA256(kenc, SALT ‖ be32(i))
 *
 * PIN 이 없으면 길이 0 의 PIN 으로 같은 과정을 밟는다. 즉 암호화는 항상 걸리지만
 * 키는 공개값이 된다 — 플래시를 덤프하면 그대로 열린다. PIN 을 걸어도 4^8 밖에
 * 안 되므로 오프라인 무차별 대입을 막지 못한다. SECURITY.md §3 참고.        */
#include <stdint.h>
#include <stddef.h>
#include "hal.h"
#include "protocol.h"
#ifdef __cplusplus
extern "C" {
#endif

#define NU_STORE_MAGIC0 'N'
#define NU_STORE_MAGIC1 'U'
#define NU_STORE_MAGIC2 'W'
#define NU_STORE_MAGIC3 '1'
#define NU_STORE_VERSION 1

#define NU_STORE_OFF_SALT 8
#define NU_STORE_OFF_MAC  24
#define NU_STORE_OFF_CT   56
#define NU_STORE_MIN_LEN  (NU_STORE_OFF_CT + 12 * 2)

/* TRIES 바이트의 위치. 이 바이트가 없는 옛 레코드도 유효하게 받아들인다. */
static inline size_t nu_store_body_len(const uint8_t *raw) {
    return (size_t)NU_STORE_OFF_CT + (size_t)raw[7] * 2u;
}

/* 레코드가 형식상 유효한지. 유효하면 1. */
int nu_store_valid(const uint8_t *raw, size_t len);

static inline int      nu_store_has_pin(const uint8_t *raw)    { return raw[5] & 1; }
static inline uint8_t  nu_store_pin_len(const uint8_t *raw)    { return raw[6]; }
static inline uint8_t  nu_store_word_count(const uint8_t *raw) { return raw[7]; }

/* 남은 PIN 시도 횟수. TRIES 바이트가 없는 옛 레코드는 만수로 본다. */
static inline uint8_t nu_store_tries(const uint8_t *raw, size_t len) {
    const size_t off = nu_store_body_len(raw);
    return (len > off) ? raw[off] : (uint8_t)NU_PIN_ATTEMPTS;
}

/* 남은 횟수를 고쳐 쓴다. 레코드 길이를 돌려준다 (바이트가 없었으면 1 늘어난다). */
static inline size_t nu_store_set_tries(uint8_t *raw, size_t len, uint8_t tries) {
    const size_t off = nu_store_body_len(raw);
    raw[off] = tries;
    return (len > off) ? len : off + 1u;
}

/* PIN 으로 열어 워드 인덱스를 꺼낸다. MAC 이 맞아야 1, 틀리면 0 (= PIN 오류). */
int nu_store_open(const uint8_t *raw, size_t len,
                  const uint8_t *pin, uint8_t pin_len,
                  uint16_t *words_out, uint8_t *count_out);

/* 새 레코드를 만든다. salt 는 hal->random 으로 뽑는다. 성공 1. */
int nu_store_seal(const nu_hal *hal,
                  const uint16_t *words, uint8_t count,
                  const uint8_t *pin, uint8_t pin_len,
                  uint8_t *raw_out, size_t *raw_len_out);

#ifdef __cplusplus
}
#endif
#endif
