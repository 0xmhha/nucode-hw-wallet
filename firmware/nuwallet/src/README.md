# 공유 라이브러리

Zephyr 펌웨어가 `CMakeLists.txt` 에서 여기를 직접 참조하고, 호스트 테스트
(`firmware/test/`)도 같은 파일을 컴파일한다. **보드용 코드는 여기 없다** —
펌웨어 본체는 `firmware/zephyr/` 다.

| 디렉터리 | 역할 | 쓰는 곳 |
| --- | --- | --- |
| `crypto/` | BIP-32/39, SLIP-0010, SHA-2, HMAC, Keccak, ECDSA(RFC6979) | Zephyr CMake, 호스트 테스트 |
| `micro-ecc/` | secp256k1. BSD-2 외부 코드 + recovery id 노출 패치 | Zephyr CMake, 호스트 테스트 |
| `chains/ethereum/` | 주소 파생, 트랜잭션·메시지 해시, 최소 RLP 파서 | 호스트 테스트 |
| `chains/solana/` | Ed25519 어댑터 (nRF52840 CryptoCell) | 아직 빌드에 미포함 |

## 유지 규칙

- **`crypto/` 와 `micro-ecc/` 는 경로를 바꾸지 말 것.** `firmware/zephyr/CMakeLists.txt`
  의 `set(SHARED ...)` 와 `firmware/test/Makefile` 이 상대 경로로 직접 가리킨다.
- micro-ecc 는 원본이 아니다. `uECC.c` · `uECC.h` 에 `NuWallet 패치` 주석이 붙은
  구간이 있다 — recovery id 노출, 외부 k 서명, 기본 RNG 비활성화 세 가지다.
  업스트림을 갱신할 때 그 패치를 다시 적용해야 한다.
- 암호 코드를 고치면 반드시 `cd firmware/test && make test` 로 확인한다.
  공식 테스트 벡터(BIP-39 24개, BIP-32 6개, RFC6979)로 검증한다.

## 이전 Arduino 포트

이 디렉터리에는 Arduino(Bluefruit) 포트가 있었다. Zephyr 포트와 프로토콜이 이중으로
구현되는 문제가 있어 제거했고, 지금은 **Zephyr 하나만 남았다**.
공유 암호 스택은 그대로 유지된다.
