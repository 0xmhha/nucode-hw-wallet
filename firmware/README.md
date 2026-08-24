# 펌웨어

NU-40 DK (nRF52840) 를 BLE 하드웨어 지갑으로 만드는 펌웨어.
프로토콜 정의는 `docs/protocol.md` 하나다.

> **실제 자금을 넣지 마세요.** `SECURITY.md` 를 먼저 읽으세요.

**구현은 하나다.** 지갑 로직은 `nuwallet/src/core/` 한 곳에 있고, 보드 포트만 둘이다.

| | 위치 | 빌드 |
|---|---|---|
| **코어** (프로토콜·승인·저장·서명) | `nuwallet/src/core/` | 아래 두 포트와 호스트 테스트가 함께 컴파일 |
| 공유 암호 라이브러리 | `nuwallet/src/{crypto,micro-ecc,chains}/` | 위와 같음 |
| Arduino 포트 (보드에 올라가는 것) | `nuwallet/` (스케치) + `nuwallet/src/{port/arduino,transport}/` | `arduino-cli` — 바로 된다 |
| Zephyr 포트 | `wallet/` | ⚠️ 보드 정의 필요 — 아래 참고 |
| 호스트 포트 (테스트) | `wallet/src/port/host/` | `firmware/test` |

코어는 `core/hal.h` 하나만 안다 — Arduino 도 Zephyr 도 모른다. 포트가 채우는 것은
난수·저장·LED·버튼·시간·송신, 그리고 Ed25519 다. **Ed25519 만 HAL 에 있는 이유**는
그것만 nRF52840 의 CryptoCell 하드웨어에 묶여 있어서다 — secp256k1 은 micro-ecc 로
코어가 직접 한다.

> 코어가 스케치 폴더 안에 있는 것은 Arduino 가 스케치 폴더 밖의 소스를 컴파일하지
> 못하기 때문이다. Zephyr `CMakeLists.txt` 와 `firmware/test` 가 상대 경로로 가져간다.

> 예전에는 컨트롤러가 두 벌이었다. 같은 프로토콜을 두 번 구현하다 보니 보드 쪽에서만
> 5건이 갈라졌다 — 체인 바이트 미소비로 **모든 서명이 깨짐**, RLP 검증 없이 서명,
> chain code 유출 등. 적합성 하네스가 그것을 잡았고, 그래서 한 벌로 합쳤다.

## 보드에 올리기

설치 자체는 UF2 복사로 끝난다 — RESET 두 번 → `NRF52BOOT` 드라이브에 `.uf2` 복사.
자세한 절차와 `.uf2` 만드는 법은 `docs/nu40-dk-firmware-installation.md` 를 본다.

```sh
cd firmware/nuwallet
arduino-cli compile --fqbn nucode:nrf52:nu40dk --output-dir ./build .
CORE=~/Library/Arduino15/packages/nucode/hardware/nrf52/1.0.2
python3 "$CORE/tools/uf2conv/uf2conv.py" -f 0xADA52840 -c \
        -o build/NUWALLET.UF2 build/nuwallet.ino.hex
```

**Zephyr 앱은 지금 바로 빌드되지 않는다.** 벤더(nucode)가 제공하는 것은
`arduino-cli` 코어 하나뿐이고, `nucode_nu40` Zephyr 보드 정의는 저장소에도
Zephyr 업스트림에도 없다. Zephyr 로 가려면 보드 정의를 직접 써야 한다.
지갑 코어(`wallet/src/app/` — 플랫폼 독립 순수 C)는 `hal.h` 만 알기 때문에
포팅 자체는 HAL 하나를 새로 쓰는 일이다.

## 보드 없이 테스트

```sh
cd firmware/test && make test
```

- `test_crypto` — 공식 벡터 (BIP-39 / BIP-32 / RFC 6979 / Keccak / EIP-155)
- `test_wallet` — 지갑 코어 통합. 실제 BLE 에 나갈 바이트를 그대로 만들어 넣고
  받아서 확인한다. 니모닉 생성·복구, 주소 파생, 서명, PIN, 잠금, 초기화,
  취소, 타임아웃, 잘못된 입력까지 다룬다.

## 더 읽기

- `wallet/README.md` — Zephyr 포트 구조, 빌드 방법
- `nuwallet/src/README.md` — 공유 라이브러리 구성과 유지 규칙
- `docs/protocol.md` — 프로토콜 정의. 두 구현의 유일한 기준

## 옛 데모

`arduino/nu40_pet/` 와 `src/main.c` 는 이전 NU-40 PET(다마고치) 데모다.
지갑과 무관하며, `firmware/CMakeLists.txt` / `firmware/prj.conf` 도 그쪽 것이다.
지갑 Zephyr 앱을 빌드할 때는 `firmware` 가 아니라 **`firmware/wallet`** 를
가리켜야 한다.

데모는 **다른 서비스 UUID(`7d2a0001-…`)를 광고한다.** 이걸 보드에 구우면 지갑
웹앱의 기기 선택 창에 아무것도 뜨지 않는다. 데모의 `.uf2` 는 저장소에 두지 않는다 —
빌드 산출물이고, 실수로 굽는 것을 막기 위해서다.

## 브라우저

Web Bluetooth 는 HTTPS 또는 localhost 에서만 동작하며, Chrome/Edge 계열
데스크톱 브라우저가 필요하다.
