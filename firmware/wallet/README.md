# NuWallet 펌웨어 — Zephyr 포트

NU-40 DK (nRF52840) 에서 지갑 코어를 Zephyr 로 돌리는 포트. 지갑 로직은 여기에
없다. Arduino 스케치와 같은 코어(`../nuwallet/src/core/`)를 컴파일하고, 이 폴더는
그 코어가 요구하는 HAL 과 BLE 전송만 채운다. 프로토콜은 `docs/protocol.md` v2 다.

> **실제 자금을 넣지 마세요.** `SECURITY.md` 를 먼저 읽으세요.

지금 보드에 올려 쓰는 것은 Arduino 포트다. 이 포트는 빌드되지만 실기기 확인은
아직이다 (`docs/TASKS.md` T14).

## 구조

```
src/main.c          HAL·BLE 를 올리고 10ms 주기로 코어를 틱 시킨다
src/port/zephyr/
  hal_zephyr.c      난수(sys_csrand_get) · 저장(settings/NVS) · LED · 버튼
  ble.c             GATT 서비스, 본딩 강제, MTU 에 맞춘 프레이밍, 재광고,
                    본딩 삭제(공장 초기화)
src/port/host/      호스트 테스트용 HAL (보드 이미지에 안 들어간다. firmware/test)
boards/             업스트림 보드 정의와 실물이 다른 곳을 고치는 오버레이
west.yml            Zephyr 버전 고정과 받을 모듈 목록
```

코어는 **메인 스레드에서만** 실행된다. BIP-32 와 PBKDF2 가 2KB 넘는 스택을 쓰는데
BT RX 스레드 스택은 그보다 작다. 그래서 BLE 요청, 버튼, 연결 해제는 모두 BT
스레드나 워크큐에서 표시만 하고 메인 루프가 코어에 넘긴다.

Arduino 포트와 다른 점:

- **Solana 를 지원하지 않는다.** Arduino 는 CryptoCell 로 Ed25519 를 하는데
  Zephyr 에는 그 경로가 없다. `hal->ed25519_*` 가 비어 있어 코어가
  `UNSUPPORTED_CHAIN` 으로 답한다.
- 광고 이름은 같다. 두 포트가 코어의 `nu_device_name()` 에 같은 칩 번호를 넣는다.

## 빌드와 올리기

`docs/nu40-dk-firmware-installation.md` 의 "Zephyr 로 빌드하기" 에 준비부터 되돌리기
까지 있다. 준비가 끝났다면 저장소 루트에서:

```sh
ZEPHYR_WS=~/zephyr-nu scripts/build-zephyr.sh
```

**올리기 전에 그 문서의 "먼저 읽을 것"을 본다.** Zephyr 앱은 Arduino 가 쓰는
SoftDevice 자리를 덮어쓰고, 지갑 레코드도 옮겨지지 않는다.

## 테스트 (보드 불필요)

코어는 `firmware/test` 에서 호스트 HAL 로 검증한다. 이 폴더의 Zephyr HAL 과 BLE
코드는 호스트에서 돌릴 수 없어 빌드로만 확인한다.

LED 가 무엇을 뜻하는지는 `docs/protocol.md` 의 LED 절에 있다.
