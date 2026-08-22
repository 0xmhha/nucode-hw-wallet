#include "keystore.h"
#include "../crypto/bip39.h"
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <string.h>

using namespace Adafruit_LittleFS_Namespace;

static const char *KS_PATH = "/wallet.bin";
static const uint32_t KS_MAGIC = 0x4e755731;   /* "NuW1" */

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    uint16_t words[KS_MAX_WORDS];
    uint16_t crc;
} ks_blob;

static uint16_t crc16(const uint8_t *d, size_t n) {
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)d[i] << 8;
        for (int b = 0; b < 8; b++) c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}

/* v1 파일 형식은 구조체 끝 패딩 때문에 crc 필드까지(0으로 둔 채) 포함한다.
 * 저장/로드 양쪽에서 반드시 같은 상태로 계산해야 한다. */
static uint16_t blob_crc(ks_blob *b) {
    const uint16_t saved = b->crc;
    b->crc = 0;
    const uint16_t crc = crc16((const uint8_t *)b, sizeof(*b) - sizeof(uint16_t));
    b->crc = saved;
    return crc;
}

int keystore_begin(void) { return InternalFS.begin() ? 1 : 0; }

int keystore_is_initialized(void) {
    uint16_t words[KS_MAX_WORDS];
    int count = 0;
    const int valid = keystore_load(words, &count);
    memset(words, 0, sizeof words);
    return valid;
}

int keystore_save(const uint16_t *words, int count) {
    if (count < 12 || count > KS_MAX_WORDS) return 0;
    ks_blob b;
    memset(&b, 0, sizeof b);
    b.magic = KS_MAGIC; b.version = 1; b.count = (uint16_t)count;
    memcpy(b.words, words, sizeof(uint16_t) * (size_t)count);
    b.crc = blob_crc(&b);

    InternalFS.remove(KS_PATH);
    File f(InternalFS);
    if (!f.open(KS_PATH, FILE_O_WRITE)) return 0;
    const size_t w = f.write((const uint8_t *)&b, sizeof b);
    f.close();
    memset(&b, 0, sizeof b);
    return w == sizeof(ks_blob);
}

int keystore_load(uint16_t *words, int *count) {
    File f(InternalFS);
    if (!f.open(KS_PATH, FILE_O_READ)) return 0;
    ks_blob b;
    const int n = f.read((uint8_t *)&b, sizeof b);
    f.close();
    if (n != (int)sizeof b) return 0;
    if (b.magic != KS_MAGIC || b.version != 1) return 0;
    if (b.crc != blob_crc(&b)) return 0;
    if (b.count < 12 || b.count > KS_MAX_WORDS) return 0;
    memcpy(words, b.words, sizeof(uint16_t) * b.count);
    *count = b.count;
    memset(&b, 0, sizeof b);
    return 1;
}

int keystore_wipe(void) {
    /* LittleFS 의 remove 는 블록을 즉시 지우지 않는다. 덮어쓰기부터 한다. */
    ks_blob z;
    memset(&z, 0xFF, sizeof z);
    File f(InternalFS);
    if (f.open(KS_PATH, FILE_O_WRITE)) {
        f.write((const uint8_t *)&z, sizeof z);
        f.close();
    }
    InternalFS.remove(KS_PATH);
    return 1;
}

int keystore_derive(const uint32_t *path, int depth,
                    const char *passphrase, bip32_key *out) {
    uint16_t words[KS_MAX_WORDS];
    int count = 0;
    if (!keystore_load(words, &count)) return 0;

    char mnemonic[KS_MAX_WORDS * 9 + 1];
    if (!bip39_to_string(words, count, mnemonic, sizeof mnemonic)) {
        memset(words, 0, sizeof words);
        return 0;
    }
    uint8_t seed[64];
    bip39_seed(mnemonic, passphrase ? passphrase : "", seed);

    const int ok = bip32_derive_path(seed, 64, path, depth, out);

    memset(mnemonic, 0, sizeof mnemonic);
    memset(seed, 0, sizeof seed);
    memset(words, 0, sizeof words);
    return ok;
}

int keystore_derive_ed25519(const uint32_t *path, int depth,
                            const char *passphrase, slip10_ed25519_key *out) {
    uint16_t words[KS_MAX_WORDS];
    int count = 0;
    if (!keystore_load(words, &count)) return 0;
    char mnemonic[KS_MAX_WORDS * 9 + 1];
    if (!bip39_to_string(words, count, mnemonic, sizeof mnemonic)) {
        memset(words, 0, sizeof words);
        return 0;
    }
    uint8_t seed[64];
    bip39_seed(mnemonic, passphrase ? passphrase : "", seed);
    const int ok = slip10_ed25519_derive(seed, sizeof seed, path, depth, out);
    memset(mnemonic, 0, sizeof mnemonic);
    memset(seed, 0, sizeof seed);
    memset(words, 0, sizeof words);
    return ok;
}
