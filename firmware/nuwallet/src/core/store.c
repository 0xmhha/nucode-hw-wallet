#include "store.h"
#include "../crypto/hmac.h"
#include "../crypto/sha2.h"
#include <string.h>

#define KDF_ITERS 4096

static void derive(const uint8_t *pin, uint8_t pin_len, const uint8_t salt[16],
                   uint8_t kenc[32], uint8_t kmac[32]) {
    uint8_t k[64];
    pbkdf2_hmac_sha512(pin, pin_len, salt, 16, KDF_ITERS, k, sizeof k);
    memcpy(kenc, k, 32);
    memcpy(kmac, k + 32, 32);
    memset(k, 0, sizeof k);
}

/* CT = PT XOR HMAC-SHA256(kenc, salt ‖ be32(block)) — 대칭이라 복호도 같은 함수. */
static void keystream_xor(const uint8_t kenc[32], const uint8_t salt[16],
                          uint8_t *buf, size_t n) {
    uint8_t in[20], blk[32];
    memcpy(in, salt, 16);
    for (size_t off = 0, i = 0; off < n; off += 32, i++) {
        in[16] = (uint8_t)(i >> 24); in[17] = (uint8_t)(i >> 16);
        in[18] = (uint8_t)(i >> 8);  in[19] = (uint8_t)i;
        hmac_sha256(kenc, 32, in, sizeof in, blk);
        const size_t m = (n - off < 32) ? (n - off) : 32;
        for (size_t j = 0; j < m; j++) buf[off + j] ^= blk[j];
    }
    memset(blk, 0, sizeof blk);
}

static void mac_of(const uint8_t kmac[32], const uint8_t *raw, size_t ct_len,
                   uint8_t out[32]) {
    /* 헤더(매직·플래그·워드 수·솔트)까지 덮어야 헤더 변조를 잡는다. */
    uint8_t buf[NU_STORE_OFF_MAC + 48];
    memcpy(buf, raw, NU_STORE_OFF_MAC);
    memcpy(buf + NU_STORE_OFF_MAC, raw + NU_STORE_OFF_CT, ct_len);
    hmac_sha256(kmac, 32, buf, NU_STORE_OFF_MAC + ct_len, out);
    memset(buf, 0, sizeof buf);
}

static int count_ok(uint8_t c) {
    return c == 12 || c == 15 || c == 18 || c == 21 || c == 24;
}

int nu_store_valid(const uint8_t *raw, size_t len) {
    if (len < NU_STORE_MIN_LEN) return 0;
    if (raw[0] != NU_STORE_MAGIC0 || raw[1] != NU_STORE_MAGIC1 ||
        raw[2] != NU_STORE_MAGIC2 || raw[3] != NU_STORE_MAGIC3) return 0;
    if (raw[4] != NU_STORE_VERSION) return 0;
    if (!count_ok(raw[7])) return 0;
    if (raw[6] > NU_PIN_LEN) return 0;
    if (len < (size_t)NU_STORE_OFF_CT + (size_t)raw[7] * 2) return 0;
    return 1;
}

int nu_store_open(const uint8_t *raw, size_t len,
                  const uint8_t *pin, uint8_t pin_len,
                  uint16_t *words_out, uint8_t *count_out) {
    if (!nu_store_valid(raw, len)) return 0;
    const uint8_t count = raw[7];
    const size_t ct_len = (size_t)count * 2;

    uint8_t kenc[32], kmac[32], mac[32], pt[48];
    derive(pin, pin_len, raw + NU_STORE_OFF_SALT, kenc, kmac);
    mac_of(kmac, raw, ct_len, mac);

    /* 상수 시간 비교. PIN 오류와 정상을 타이밍으로 구분하지 못하게 한다. */
    uint8_t diff = 0;
    for (int i = 0; i < 32; i++) diff |= (uint8_t)(mac[i] ^ raw[NU_STORE_OFF_MAC + i]);
    if (diff) {
        memset(kenc, 0, 32); memset(kmac, 0, 32); memset(mac, 0, 32);
        return 0;
    }

    memcpy(pt, raw + NU_STORE_OFF_CT, ct_len);
    keystream_xor(kenc, raw + NU_STORE_OFF_SALT, pt, ct_len);
    for (uint8_t i = 0; i < count; i++) {
        words_out[i] = (uint16_t)((pt[i * 2] << 8) | pt[i * 2 + 1]);
        if (words_out[i] >= 2048) {   /* MAC 이 맞으면 나올 수 없다. 방어적 확인. */
            memset(pt, 0, sizeof pt); memset(kenc, 0, 32); memset(kmac, 0, 32);
            return 0;
        }
    }
    *count_out = count;

    memset(pt, 0, sizeof pt);
    memset(kenc, 0, 32); memset(kmac, 0, 32); memset(mac, 0, 32);
    return 1;
}

int nu_store_seal(const nu_hal *hal,
                  const uint16_t *words, uint8_t count,
                  const uint8_t *pin, uint8_t pin_len,
                  uint8_t *raw_out, size_t *raw_len_out) {
    if (!count_ok(count) || pin_len > NU_PIN_LEN) return 0;
    const size_t ct_len = (size_t)count * 2;
    const size_t total = NU_STORE_OFF_CT + ct_len;

    memset(raw_out, 0, total + 1);
    raw_out[0] = NU_STORE_MAGIC0; raw_out[1] = NU_STORE_MAGIC1;
    raw_out[2] = NU_STORE_MAGIC2; raw_out[3] = NU_STORE_MAGIC3;
    raw_out[4] = NU_STORE_VERSION;
    raw_out[5] = (uint8_t)(pin_len ? 1 : 0);
    raw_out[6] = pin_len;
    raw_out[7] = count;
    if (!hal->random(raw_out + NU_STORE_OFF_SALT, 16, hal->ctx)) return 0;

    for (uint8_t i = 0; i < count; i++) {
        raw_out[NU_STORE_OFF_CT + i * 2]     = (uint8_t)(words[i] >> 8);
        raw_out[NU_STORE_OFF_CT + i * 2 + 1] = (uint8_t)words[i];
    }

    uint8_t kenc[32], kmac[32];
    derive(pin, pin_len, raw_out + NU_STORE_OFF_SALT, kenc, kmac);
    keystream_xor(kenc, raw_out + NU_STORE_OFF_SALT, raw_out + NU_STORE_OFF_CT, ct_len);
    mac_of(kmac, raw_out, ct_len, raw_out + NU_STORE_OFF_MAC);
    memset(kenc, 0, 32); memset(kmac, 0, 32);

    /* 새 레코드는 시도 횟수가 만수다. MAC 밖이라 나중에 이 바이트만 고쳐 쓴다. */
    raw_out[total] = NU_PIN_ATTEMPTS;

    *raw_len_out = total + 1;
    return 1;
}
