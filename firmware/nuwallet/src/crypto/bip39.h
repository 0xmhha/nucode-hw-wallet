#ifndef NUWALLET_BIP39_H
#define NUWALLET_BIP39_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define BIP39_MAX_WORDS 24
#define BIP39_SEED_LEN  64

/* 엔트로피 -> 워드 인덱스. ent_len 은 16/20/24/28/32 바이트.
 * 반환값은 단어 개수 (12/15/18/21/24), 실패 시 0. */
int bip39_from_entropy(const uint8_t *ent, size_t ent_len, uint16_t *words_out);

/* 워드 인덱스 -> 엔트로피. 체크섬을 검증한다. 실패 시 0. */
int bip39_to_entropy(const uint16_t *words, int count, uint8_t *ent_out, size_t *ent_len_out);

/* 워드 인덱스 -> 니모닉 문자열 (공백 구분, NUL 종료).
 * buf 는 최소 24*9 = 216 바이트. 반환값은 문자열 길이. */
size_t bip39_to_string(const uint16_t *words, int count, char *buf, size_t buf_len);

/* 니모닉 문자열 + 패스프레이즈 -> 512비트 시드 (PBKDF2-HMAC-SHA512, 2048회). */
void bip39_seed(const char *mnemonic, const char *passphrase, uint8_t seed[BIP39_SEED_LEN]);

/* 단어 문자열 -> 인덱스. 못 찾으면 -1. */
int bip39_find_word(const char *w, size_t len);

#ifdef __cplusplus
}
#endif
#endif
