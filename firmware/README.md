# 펌웨어

NU-40 DK (nRF52840) 를 BLE 하드웨어 지갑으로 만드는 펌웨어.
프로토콜 정의는 `docs/protocol.md` 하나다.

> **실제 자금을 넣지 마세요.** `SECURITY.md` 를 먼저 읽으세요.

| | 위치 | 빌드 |
|---|---|---|
| 펌웨어 (Zephyr) | `zephyr/` | ⚠️ 보드 정의 필요 — 아래 참고 |
| 공유 라이브러리 | `nuwallet/src/` | 위 빌드와 호스트 테스트가 함께 컴파일 |

`nuwallet/src/` 에는 보드용 코드가 없다. 암호 스택(`crypto/`, `micro-ecc/`)과
체인 어댑터(`chains/`)만 있고, Zephyr `CMakeLists.txt` 와 호스트 테스트가
상대 경로로 직접 참조한다. SHA-2 · HMAC · PBKDF2 · Keccak-256 · BIP-39 · BIP-32 ·
SLIP-0010 · secp256k1(RFC 6979) 을 직접 구현했고 전부 공식 테스트 벡터로 검증한다.

## 보드에 올리기

설치 자체는 UF2 복사로 끝난다 — RESET 두 번 → `NRF52BOOT` 드라이브에 `.uf2` 복사.
자세한 절차와 `.uf2` 만드는 법은 `docs/nu40-dk-firmware-installation.md` 를 본다.

**다만 지금 이 저장소의 Zephyr 앱은 바로 빌드되지 않는다.** 벤더(nucode)가
제공하는 것은 `arduino-cli` 코어 하나뿐이고, `nucode_nu40` Zephyr 보드 정의는
저장소에도 Zephyr 업스트림에도 없다. 보드 정의를 직접 쓰거나, 지갑 코어
(`zephyr/src/app/` — 플랫폼 독립 순수 C)를 Arduino 위에 올려야 한다.

## 보드 없이 테스트

```sh
cd firmware/test && make test
```

- `test_crypto` — 공식 벡터 (BIP-39 / BIP-32 / RFC 6979 / Keccak / EIP-155)
- `test_wallet` — 지갑 코어 통합. 실제 BLE 에 나갈 바이트를 그대로 만들어 넣고
  받아서 확인한다. 니모닉 생성·복구, 주소 파생, 서명, PIN, 잠금, 초기화,
  취소, 타임아웃, 잘못된 입력까지 다룬다.

## 더 읽기

- `zephyr/README.md` — 펌웨어 구조, LED 표시 규칙, 빌드 방법
- `nuwallet/src/README.md` — 공유 라이브러리 구성과 유지 규칙

> 이전에는 Arduino(Bluefruit) 포트가 `nuwallet/` 에 함께 있었다. 같은 프로토콜을
> 두 번 구현하게 되어 제거했고, 지금은 **Zephyr 하나만** 남았다.

## 옛 데모

`arduino/nu40_pet/` 와 `src/main.c` 는 이전 NU-40 PET(다마고치) 데모다.
지갑과 무관하며, `CMakeLists.txt` / `prj.conf` 도 그쪽 것이다.
지갑 Zephyr 앱을 빌드할 때는 `firmware` 가 아니라 **`firmware/zephyr`** 를
가리켜야 한다.

## 브라우저

Web Bluetooth 는 HTTPS 또는 localhost 에서만 동작하며, Chrome/Edge 계열
데스크톱 브라우저가 필요하다.
