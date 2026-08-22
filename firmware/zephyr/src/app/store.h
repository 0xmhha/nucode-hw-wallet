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

/* 레코드가 형식상 유효한지. 유효하면 1. */
int nu_store_valid(const uint8_t *raw, size_t len);

static inline int      nu_store_has_pin(const uint8_t *raw)    { return raw[5] & 1; }
static inline uint8_t  nu_store_pin_len(const uint8_t *raw)    { return raw[6]; }
static inline uint8_t  nu_store_word_count(const uint8_t *raw) { return raw[7]; }

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
