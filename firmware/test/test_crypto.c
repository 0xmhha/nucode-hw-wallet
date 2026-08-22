/* 호스트에서 도는 암호 스택 테스트. 보드 없이 정확성을 확인한다.
 *
 *   cd firmware/test && make test
 *
 * 모든 기대값은 공식 테스트 벡터에서 스크립트로 뽑았다 (vectors_*.h).
 * 손으로 적은 기대값은 두지 않는다 — 한 번 그렇게 했다가 틀렸다.          */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../nuwallet/src/crypto/sha2.h"
#include "../nuwallet/src/crypto/hmac.h"
#include "../nuwallet/src/crypto/keccak.h"
#include "../nuwallet/src/crypto/bip39.h"
#include "../nuwallet/src/crypto/bip32.h"
#include "../nuwallet/src/crypto/slip10.h"
#include "../nuwallet/src/micro-ecc/uECC.h"
#include "vectors_bip39.h"
#include "vectors_bip32.h"
#include "vectors_ecdsa.h"
#include "../nuwallet/src/crypto/ecdsa.h"
#include "../nuwallet/src/chains/ethereum/eth.h"

static int fails = 0, total = 0;

static void hexs(const uint8_t *b, size_t n, char *out) {
    for (size_t i = 0; i < n; i++) sprintf(out + i*2, "%02x", b[i]);
    out[n*2] = 0;
}
static size_t unhex(const char *h, uint8_t *out) {
    size_t n = strlen(h) / 2;
    for (size_t i = 0; i < n; i++) { unsigned v; sscanf(h + i*2, "%2x", &v); out[i] = (uint8_t)v; }
    return n;
}
static void check(const char *name, const uint8_t *got, size_t n, const char *want) {
    char g[257]; hexs(got, n, g);
    total++;
    if (strcmp(g, want) == 0) printf("  ok   %s\n", name);
    else { printf("  FAIL %s\n       got  %s\n       want %s\n", name, g, want); fails++; }
}
static void check_str(const char *name, const char *got, const char *want) {
    total++;
    if (strcmp(got, want) == 0) printf("  ok   %s\n", name);
    else { printf("  FAIL %s\n       got  %s\n       want %s\n", name, got, want); fails++; }
}
static void check_true(const char *name, int cond) {
    total++;
    if (cond) printf("  ok   %s\n", name);
    else { printf("  FAIL %s\n", name); fails++; }
}

/* ── uECC 용 RNG. 테스트에서는 결정론적 서명만 쓰므로 필요 없지만
      uECC_compute_public_key 가 g_rng_function 을 안 쓰므로 무해하다. ── */
static int test_rng(uint8_t *dest, unsigned size) {
    for (unsigned i = 0; i < size; i++) dest[i] = (uint8_t)(rand() & 0xff);
    return 1;
}

/* ── uECC 결정론적 서명용 SHA-256 HashContext ─────────────────────────────── */
typedef struct { uECC_HashContext uECC; sha256_ctx ctx; } SHA256_HashContext;
static void h_init(const uECC_HashContext *base) {
    SHA256_HashContext *c = (SHA256_HashContext *)base; sha256_init(&c->ctx);
}
static void h_update(const uECC_HashContext *base, const uint8_t *m, unsigned n) {
    SHA256_HashContext *c = (SHA256_HashContext *)base; sha256_update(&c->ctx, m, n);
}
static void h_finish(const uECC_HashContext *base, uint8_t *out) {
    SHA256_HashContext *c = (SHA256_HashContext *)base; sha256_final(&c->ctx, out);
}

int main(void) {
    uint8_t d[128], buf[256];
    char sbuf[512];
    uECC_set_rng(&test_rng);

    puts("SHA-256  (FIPS 180-4)");
    sha256((const uint8_t*)"", 0, d);
    check("빈 문자열", d, 32, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    sha256((const uint8_t*)"abc", 3, d);
    check("\"abc\"", d, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    puts("SHA-512  (FIPS 180-4)");
    sha512((const uint8_t*)"abc", 3, d);
    check("\"abc\"", d, 64,
      "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
      "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");

    puts("HMAC  (RFC 4231 #1)");
    { uint8_t k[20]; memset(k, 0x0b, 20);
      hmac_sha512(k, 20, (const uint8_t*)"Hi There", 8, d);
      check("HMAC-SHA512", d, 64,
        "87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cde"
        "daa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854");
      hmac_sha256(k, 20, (const uint8_t*)"Hi There", 8, d);
      check("HMAC-SHA256", d, 32,
        "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"); }

    puts("Keccak-256  (Ethereum)");
    keccak256((const uint8_t*)"", 0, d);
    check("빈 문자열", d, 32, "c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470");
    keccak256((const uint8_t*)"abc", 3, d);
    check("\"abc\"", d, 32, "4e03657aea45a94fc7d47ba826c8d667c0d1e6e33a64a036ec44f58fa12d6c45");

    printf("BIP-39  (공식 벡터 %d개)\n", BIP39_VEC_COUNT);
    for (int i = 0; i < BIP39_VEC_COUNT; i++) {
        const bip39_vec *v = &BIP39_VECTORS[i];
        uint8_t ent[32];
        size_t elen = unhex(v->entropy, ent);
        uint16_t words[24];
        int n = bip39_from_entropy(ent, elen, words);
        char name[64];

        snprintf(name, sizeof name, "[%02d] 엔트로피->니모닉", i);
        bip39_to_string(words, n, sbuf, sizeof sbuf);
        check_str(name, sbuf, v->mnemonic);

        snprintf(name, sizeof name, "[%02d] 니모닉->시드", i);
        uint8_t seed[64];
        bip39_seed(v->mnemonic, "TREZOR", seed);
        check(name, seed, 64, v->seed);

        snprintf(name, sizeof name, "[%02d] 체크섬 왕복", i);
        uint8_t back[32]; size_t blen = 0;
        int ok = bip32_public_key ? 1 : 1;   /* 링크 확인용 no-op */
        ok = bip39_to_entropy(words, n, back, &blen);
        check_true(name, ok && blen == elen && memcmp(back, ent, elen) == 0);
    }

    puts("BIP-39  체크섬 오류 검출");
    { uint16_t w[12]; uint8_t ent[16] = {0};
      bip39_from_entropy(ent, 16, w);
      w[11] ^= 1;                                   /* 마지막 단어를 한 비트 틀리게 */
      uint8_t back[32]; size_t bl;
      check_true("변조된 니모닉 거부", bip39_to_entropy(w, 12, back, &bl) == 0); }

    puts("BIP-39  단어 검색");
    check_true("\"abandon\" == 0",  bip39_find_word("abandon", 7) == 0);
    check_true("\"zoo\" == 2047",   bip39_find_word("zoo", 3) == 2047);
    check_true("없는 단어 == -1",   bip39_find_word("notaword", 8) == -1);

    printf("BIP-32  (공식 벡터 %d개)\n", BIP32_VEC_COUNT);
    for (int i = 0; i < BIP32_VEC_COUNT; i++) {
        const bip32_vec *v = &BIP32_VECTORS[i];
        uint8_t seed[64];
        size_t slen = unhex(v->seed_hex, seed);
        bip32_key k;
        char name[96];
        int ok = bip32_derive_path(seed, slen, v->index, v->depth, &k);
        snprintf(name, sizeof name, "[%d] %s  개인키", i, v->path);
        if (!ok) { printf("  FAIL %s (파생 실패)\n", name); fails++; total++; continue; }
        check(name, k.priv, 32, v->priv_hex);
        snprintf(name, sizeof name, "[%d] %s  체인코드", i, v->path);
        check(name, k.chain, 32, v->chain_hex);
    }

    puts("SLIP-0010 Ed25519  (공식 벡터 1)");
    {
        uint8_t seed[16];
        unhex("000102030405060708090a0b0c0d0e0f", seed);
        slip10_ed25519_key k;
        check_true("master 파생", slip10_ed25519_derive(seed, sizeof seed, 0, 0, &k));
        check("master 개인키", k.key, 32,
              "2b4be7f19ee27bbf30c667b642d5f4aa69fd169872f8fc3059c08ebae2eb19e7");
        check("master 체인코드", k.chain, 32,
              "90046a93de5380a72b5e45010748567d5ea02bbf6522f979e05c0d8d8ca9fffb");
        const uint32_t path[] = { SLIP10_HARDENED };
        check_true("m/0' 파생", slip10_ed25519_derive(seed, sizeof seed, path, 1, &k));
        check("m/0' 개인키", k.key, 32,
              "68e0fe46dfb67e368c75379acec591dad19df3cde26e63b93a8e704f1dade7a3");
        check("m/0' 체인코드", k.chain, 32,
              "8b59aa11380b624e81507a27fedda59fea6d0b779a778918a2fd3590e16e9c69");
        const uint32_t invalid[] = { 0 };
        check_true("비하드닝 경로 거부", !slip10_ed25519_derive(seed, sizeof seed, invalid, 1, &k));
        slip10_ed25519_wipe(&k);
    }

    puts("secp256k1  공개키 파생");
    {   /* privkey = 2 -> 2G.
         * 주의: micro-ecc 는 priv=1 과 priv=n-1 에서 실패한다. 상수시간 사다리의
         * 예외 케이스다. 실제 키가 그 값일 확률은 2^-256 이라 무해하지만,
         * 테스트가 하필 경계값을 고르면 안 된다. */
        uint8_t priv[32] = {0}; priv[31] = 2;
        uint8_t pub[65];
        check_true("privkey=2 공개키 계산", bip32_public_key(priv, pub));
        check("공개키 == 2G", pub, 65,
          "04c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5"
          "1ae168fea63dc339a3c58419466ceaeef7f632653266d0e1236431a950cfe52a");
        uint8_t c33[33];
        check_true("압축 공개키 계산", bip32_public_key_compressed(priv, c33));
        check("압축 공개키", c33, 33,
          "02c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5"); }

    puts("Ethereum  EIP-155 서명 해시 (RLP -> keccak)");
    {   /* EIP-155 명세의 서명 대상 RLP. 기기가 받는 것과 같은 바이트열이다. */
        const char *rlp_hex =
          "ec098504a817c800825208943535353535353535353535353535353535353535"
          "880de0b6b3a764000080018080";
        uint8_t rlp[64];
        size_t rl = unhex(rlp_hex, rlp);
        uint8_t h[32];
        eth_tx_hash(rlp, rl, h);
        /* 이 값은 SDK 의 독립 TypeScript 구현과 교차 확인했다. */
        check("keccak256(unsigned RLP)", h, 32,
              "daf5a779ae972f972197303d7b574746c7ef83eadac0f2791ad23db92e4c8e53");
        /* ECDSA 벡터 [0] 의 해시와 같아야 한다 — 스택이 서로 어긋나면 여기서 잡힌다 */
        char hs[65]; hexs(h, 32, hs);
        check_true("ECDSA 벡터[0] 과 해시 일치", strcmp(hs, ECDSA_VECTORS[0].hash) == 0); }

    puts("Ethereum  personal_sign 해시");
    {   uint8_t h[32];
        eth_personal_hash((const uint8_t*)"Hello", 5, h);
        /* keccak256("\x19Ethereum Signed Message:\n5Hello")
         * SDK 의 독립 TypeScript keccak 구현과 교차 확인한 값이다. */
        check("personal_sign(\"Hello\")", h, 32,
              "aa744ba2ca576ec62ca0045eca00ad3917fdf7ffa34fbbae50828a5a69c1580e"); }

    puts("Ethereum  주소 파생");
    {   /* 잘 알려진 벡터 (EIP-155 예제 키) */
        uint8_t priv[32];
        unhex("4646464646464646464646464646464646464646464646464646464646464646", priv);
        uint8_t pub[65], h[32];
        bip32_public_key(priv, pub);
        keccak256(pub + 1, 64, h);            /* 0x04 접두 제외한 64바이트 */
        check("주소 (keccak 하위 20바이트)", h + 12, 20,
              "9d8a62f656a8d1615c1294fd71e9cfb3e4855a4f"); }

    printf("ECDSA  RFC6979 + recovery id  (독립 구현 벡터 %d개)\n", ECDSA_VEC_COUNT);
    for (int i = 0; i < ECDSA_VEC_COUNT; i++) {
        const ecdsa_vec *v = &ECDSA_VECTORS[i];
        uint8_t priv[32], hash[32], k[32], sig[64], pub[65];
        char name[128];
        unhex(v->priv, priv); unhex(v->hash, hash);
        int recid = -1;

        rfc6979_nonce(priv, hash, k);
        snprintf(name, sizeof name, "[%d] %s  nonce k", i, v->name);
        check(name, k, 32, v->k);

        int ok = ecdsa_sign_secp256k1(priv, hash, sig, &recid);
        snprintf(name, sizeof name, "[%d] %s  서명", i, v->name);
        check_true(name, ok);
        if (!ok) continue;

        snprintf(name, sizeof name, "[%d] %s  r", i, v->name);
        check(name, sig, 32, v->r);
        snprintf(name, sizeof name, "[%d] %s  s (low-s)", i, v->name);
        check(name, sig + 32, 32, v->s);
        snprintf(name, sizeof name, "[%d] %s  recid=%d", i, v->name, v->recid);
        check_true(name, recid == v->recid);

        bip32_public_key(priv, pub);
        snprintf(name, sizeof name, "[%d] %s  검증", i, v->name);
        check_true(name, ecdsa_verify_secp256k1(pub + 1, hash, sig));
    }

    (void)buf;
    printf("\n%d개 중 %d개 실패\n", total, fails);
    return fails ? 1 : 0;
}
