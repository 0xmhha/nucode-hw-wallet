/* 잠금 해제된 세션.
 *
 * 시드는 잠금이 풀려 있는 동안만 RAM 에 있다. 여기서 다루는 것은 그 시드와,
 * 시드에서 나오는 것들(주소·공개키·서명키), 그리고 플래시 레코드다.
 *
 * 개인키는 이 파일 밖으로 나가지 않는다 — nu_derive 가 호출자의 버퍼에 넣어
 * 주고, 호출자가 쓰고 나서 지운다.                                          */
#include "internal.h"
#include "store.h"
#include "crypto/bip39.h"
#include "crypto/bip32.h"
#include "crypto/ecdsa.h"
#include "crypto/keccak.h"
#include <string.h>

/* ── 세션 ───────────────────────────────────────────────────────────────── */

void nu_session_lock(nu_wallet *w) {
    w->unlocked = 0;
    memset(w->seed, 0, sizeof w->seed);
    memset(w->words, 0, sizeof w->words);
    memset(w->pin, 0, sizeof w->pin);
    w->word_count = 0;
    w->pin_len = 0;
}

/* 워드 + 패스프레이즈 -> 시드. 실패 0. */
int nu_seed_from_words(nu_wallet *w, const uint16_t *words, uint8_t count,
                           const char *passphrase) {
    char mnemonic[24 * 9 + 1];
    if (!bip39_to_string(words, count, mnemonic, sizeof mnemonic)) return 0;
    bip39_seed(mnemonic, passphrase ? passphrase : "", w->seed);
    memset(mnemonic, 0, sizeof mnemonic);
    return 1;
}

/* 경로 파생 후 주소/공개키. 실패 0. */
int nu_derive(const nu_wallet *w, const uint32_t *path, uint8_t depth,
                       uint8_t addr[20], uint8_t pub65[65], uint8_t chain[32],
                       uint8_t priv[32]) {
    bip32_key k;
    if (!bip32_derive_path(w->seed, 64, path, depth, &k)) return 0;
    uint8_t pub[65];
    if (!bip32_public_key(k.priv, pub)) { bip32_wipe(&k); return 0; }
    uint8_t h[32];
    keccak256(pub + 1, 64, h);
    if (addr)  memcpy(addr, h + 12, 20);
    if (pub65) memcpy(pub65, pub, 65);
    if (chain) memcpy(chain, k.chain, 32);
    if (priv)  memcpy(priv, k.priv, 32);
    bip32_wipe(&k);
    return 1;
}

const uint32_t NU_DEFAULT_PATH[5] = {
    0x8000002cu, 0x8000003cu, 0x80000000u, 0u, 0u    /* m/44'/60'/0'/0/0 */
};

/* ── 저장 ───────────────────────────────────────────────────────────────── */

int nu_persist(nu_wallet *w, const uint16_t *words, uint8_t count,
                   const uint8_t *pin, uint8_t pin_len) {
    uint8_t raw[NU_STORE_MAX];
    size_t raw_len = 0;
    if (!nu_store_seal(w->hal, words, count, pin, pin_len, raw, &raw_len)) return 0;
    if (!w->hal->store_write(raw, raw_len, w->hal->ctx)) return 0;
    memcpy(w->rec, raw, raw_len);
    w->rec_len = raw_len;
    return 1;
}

/* 남은 PIN 시도 횟수를 플래시에 반영한다.
 *
 * RAM 에만 두면 전원을 껐다 켜는 것만으로 카운터가 되살아나 무한히 시도할 수
 * 있었다. TRIES 는 MAC 밖이라 PIN 없이도 고쳐 쓸 수 있다 — store.h 참고. */
void nu_persist_tries(nu_wallet *w, uint8_t tries) {
    if (!w->rec_len) return;
    w->pin_attempts = tries;
    w->rec_len = nu_store_set_tries(w->rec, w->rec_len, tries);
    if (!w->hal->store_write(w->rec, w->rec_len, w->hal->ctx)) {
        /* 쓰기가 실패하면 카운터를 못 줄인 것이다. 무한 시도를 허용하느니
         * 이번 시도를 실패로 끝낸다. */
        w->pin_attempts = 0;
    }
}

/* 시도 횟수가 바닥났다. 지갑을 지운다 — 주운 보드로 계속 눌러 볼 수 없게. */
void nu_wipe_record(nu_wallet *w) {
    w->hal->store_erase(w->hal->ctx);
    memset(w->rec, 0, sizeof w->rec);
    w->rec_len = 0;
    w->tmp_count = 0;
    nu_session_lock(w);
}
