/* BIP-32 공식 테스트 벡터.
 * 출처: https://github.com/bitcoin/bips/blob/master/bip-0032.mediawiki
 * xprv 대신 파생된 개인키(hex)를 직접 비교한다 — Base58/RIPEMD160 없이 검증 가능. */
#ifndef VECTORS_BIP32_H
#define VECTORS_BIP32_H

typedef struct {
    const char *seed_hex;
    const char *path;        /* 사람이 읽는 표기 */
    int depth;
    uint32_t index[8];
    const char *priv_hex;    /* 파생된 개인키 32바이트 */
    const char *chain_hex;   /* 체인코드 32바이트 */
} bip32_vec;

#define H 0x80000000u
#define BIP32_VEC_COUNT 6
static const bip32_vec BIP32_VECTORS[BIP32_VEC_COUNT] = {
  /* Test vector 1: seed 000102030405060708090a0b0c0d0e0f */
  { "000102030405060708090a0b0c0d0e0f", "m", 0, {0},
    "e8f32e723decf4051aefac8e2c93c9c5b214313817cdb01a1494b917c8436b35",
    "873dff81c02f525623fd1fe5167eac3a55a049de3d314bb42ee227ffed37d508" },
  { "000102030405060708090a0b0c0d0e0f", "m/0'", 1, {0+H},
    "edb2e14f9ee77d26dd93b4ecede8d16ed408ce149b6cd80b0715a2d911a0afea",
    "47fdacbd0f1097043b78c63c20c34ef4ed9a111d980047ad16282c7ae6236141" },
  { "000102030405060708090a0b0c0d0e0f", "m/0'/1", 2, {0+H, 1},
    "3c6cb8d0f6a264c91ea8b5030fadaa8e538b020f0a387421a12de9319dc93368",
    "2a7857631386ba23dacac34180dd1983734e444fdbf774041578e9b6adb37c19" },
  { "000102030405060708090a0b0c0d0e0f", "m/0'/1/2'/2/1000000000", 5,
    {0+H, 1, 2+H, 2, 1000000000},
    "471b76e389e528d6de6d816857e012c5455051cad6660850e58372a6c3e6e7c8",
    "c783e67b921d2beb8f6b389cc646d7263b4145701dadd2161548a8b078e65e9e" },
  /* Test vector 2 */
  { "fffcf9f6f3f0edeae7e4e1dedbd8d5d2cfccc9c6c3c0bdbab7b4b1aeaba8a5a29f9c999693908d8a8784817e7b7875726f6c696663605d5a5754514e4b484542",
    "m", 0, {0},
    "4b03d6fc340455b363f51020ad3ecca4f0850280cf436c70c727923f6db46c3e",
    "60499f801b896d83179a4374aeb7822aaeaceaa0db1f85ee3e904c4defbd9689" },
  { "fffcf9f6f3f0edeae7e4e1dedbd8d5d2cfccc9c6c3c0bdbab7b4b1aeaba8a5a29f9c999693908d8a8784817e7b7875726f6c696663605d5a5754514e4b484542",
    "m/0/2147483647'/1", 3, {0, 2147483647u+H, 1},
    "704addf544a06e5ee4bea37098463c23613da32020d604506da8c0518e1da4b7",
    "f366f48f1ea9f2d1d3fe958c95ca84ea18e4c4ddb9366c336c927eb246fb38cb" },
};
#undef H
#endif
