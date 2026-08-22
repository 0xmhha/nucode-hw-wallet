#include "bip39.h"
#include "bip39_wordlist.h"
#include "sha2.h"
#include "hmac.h"
#include <string.h>

int bip39_from_entropy(const uint8_t *ent, size_t ent_len, uint16_t *out) {
    if (ent_len < 16 || ent_len > 32 || (ent_len % 4) != 0) return 0;
    const int ent_bits = (int)ent_len * 8;
    const int cs_bits  = ent_bits / 32;
    const int words    = (ent_bits + cs_bits) / 11;

    uint8_t h[SHA256_DIGEST];
    sha256(ent, ent_len, h);

    /* 엔트로피 뒤에 체크섬 비트를 붙인 비트열에서 11비트씩 잘라낸다. */
    for (int i = 0; i < words; i++) {
        uint32_t v = 0;
        for (int b = 0; b < 11; b++) {
            const int bit = i * 11 + b;
            uint8_t val;
            if (bit < ent_bits) val = (uint8_t)((ent[bit / 8] >> (7 - (bit % 8))) & 1);
            else {
                const int cb = bit - ent_bits;
                val = (uint8_t)((h[cb / 8] >> (7 - (cb % 8))) & 1);
            }
            v = (v << 1) | val;
        }
        out[i] = (uint16_t)v;
    }
    memset(h, 0, sizeof(h));
    return words;
}

int bip39_to_entropy(const uint16_t *words, int count, uint8_t *ent_out, size_t *ent_len_out) {
    if (count != 12 && count != 15 && count != 18 && count != 21 && count != 24) return 0;
    const int total_bits = count * 11;
    const int ent_bits   = total_bits * 32 / 33;
    const int cs_bits    = total_bits - ent_bits;
    const size_t ent_len = (size_t)ent_bits / 8;

    uint8_t buf[33];
    memset(buf, 0, sizeof(buf));
    for (int i = 0; i < count; i++) {
        if (words[i] >= BIP39_WORD_COUNT) return 0;
        for (int b = 0; b < 11; b++) {
            const int bit = i * 11 + b;
            if ((words[i] >> (10 - b)) & 1) buf[bit / 8] |= (uint8_t)(0x80 >> (bit % 8));
        }
    }
    uint8_t h[SHA256_DIGEST];
    sha256(buf, ent_len, h);
    /* 체크섬 비트 비교 */
    for (int b = 0; b < cs_bits; b++) {
        const int bit = ent_bits + b;
        const uint8_t got  = (uint8_t)((buf[bit / 8] >> (7 - (bit % 8))) & 1);
        const uint8_t want = (uint8_t)((h[b / 8] >> (7 - (b % 8))) & 1);
        if (got != want) { memset(buf, 0, sizeof(buf)); memset(h, 0, sizeof(h)); return 0; }
    }
    memcpy(ent_out, buf, ent_len);
    if (ent_len_out) *ent_len_out = ent_len;
    memset(buf, 0, sizeof(buf)); memset(h, 0, sizeof(h));
    return 1;
}

size_t bip39_to_string(const uint16_t *words, int count, char *buf, size_t buf_len) {
    size_t o = 0;
    for (int i = 0; i < count; i++) {
        const char *w = bip39_word(words[i]);
        const size_t n = strlen(w);
        if (o + n + 2 > buf_len) { buf[0] = 0; return 0; }
        if (i) buf[o++] = ' ';
        memcpy(buf + o, w, n); o += n;
    }
    buf[o] = 0;
    return o;
}

void bip39_seed(const char *mnemonic, const char *passphrase, uint8_t seed[64]) {
    /* salt = "mnemonic" ‖ passphrase  (BIP-39) */
    uint8_t salt[8 + 128];
    memcpy(salt, "mnemonic", 8);
    size_t pl = passphrase ? strlen(passphrase) : 0;
    if (pl > 128) pl = 128;
    if (pl) memcpy(salt + 8, passphrase, pl);
    pbkdf2_hmac_sha512((const uint8_t *)mnemonic, strlen(mnemonic),
                       salt, 8 + pl, 2048, seed, 64);
    memset(salt, 0, sizeof(salt));
}

int bip39_find_word(const char *w, size_t len) {
    /* 워드리스트는 사전순이라 이분 탐색이 된다. */
    int lo = 0, hi = BIP39_WORD_COUNT - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        const char *m = bip39_word((uint16_t)mid);
        const size_t ml = strlen(m);
        const size_t n = (len < ml) ? len : ml;
        int c = memcmp(w, m, n);
        if (c == 0) c = (len < ml) ? -1 : (len > ml ? 1 : 0);
        if (c == 0) return mid;
        if (c < 0) hi = mid - 1; else lo = mid + 1;
    }
    return -1;
}
