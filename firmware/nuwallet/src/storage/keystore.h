#line 1 "/Users/0xtopaz/work/github/0xmhha/nucode-hw-wallet/firmware/nuwallet/src/storage/keystore.h"
#ifndef NUWALLET_KEYSTORE_H
#define NUWALLET_KEYSTORE_H
#include <stdint.h>
#include <stddef.h>
#include "../crypto/bip32.h"
#include "../crypto/slip10.h"
#ifdef __cplusplus
extern "C" {
#endif

/* 니모닉을 LittleFS 에 저장한다.
 *
 * 시드가 아니라 "워드 인덱스"를 저장하는 이유: BIP-39 패스프레이즈를 지원하려면
 * 시드가 아니라 니모닉이 필요하다 (seed = PBKDF2(mnemonic, "mnemonic"+passphrase)).
 * 패스프레이즈는 저장하지 않고 서명할 때마다 호스트가 넣는다. 그래서 플래시를
 * 통째로 덤프당해도 패스프레이즈를 쓴 지갑의 자금은 남는다.
 *
 * 경고: 저장은 평문이다. nRF52840 에 보안 요소가 없고, 어차피 기기에 있는
 * 무엇으로 암호화해도 같은 기기에서 복호화 가능하므로 방어가 되지 않는다.
 * SECURITY.md 참고. */

#define KS_MAX_WORDS 24

int  keystore_begin(void);                       /* LittleFS 마운트 */
int  keystore_is_initialized(void);
int  keystore_save(const uint16_t *words, int count);
int  keystore_load(uint16_t *words, int *count);
int  keystore_wipe(void);

/* 저장된 니모닉 + 패스프레이즈로 경로를 파생한다.
 * passphrase 는 NULL 또는 "" 이면 미사용. 실패 시 0. */
int  keystore_derive(const uint32_t *path, int depth,
                     const char *passphrase, bip32_key *out);
int  keystore_derive_ed25519(const uint32_t *path, int depth,
                             const char *passphrase, slip10_ed25519_key *out);

#ifdef __cplusplus
}
#endif
#endif
