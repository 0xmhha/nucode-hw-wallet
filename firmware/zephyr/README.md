# NuWallet 펌웨어 (Zephyr)

nRF52840 (NU-40 DK) 용 하드웨어 지갑 펌웨어. `docs/protocol.md` 의 BLE 프로토콜
v1 을 구현한다.

> **실제 자금을 넣지 마세요.** `SECURITY.md` 를 먼저 읽으세요.

## 구조

핵심은 **지갑 코어가 플랫폼을 모른다**는 것이다. Zephyr 도 BLE 도 모르고
`nu_hal` 구조체만 안다. 덕분에 보드 없이 호스트에서 전체 흐름을 테스트할 수 있고,
실제로 그렇게 하고 있다 (`firmware/test/test_wallet.c`, 103개 검사).

```
src/app/          플랫폼 독립. 여기에 로직이 전부 있다
  protocol.h        docs/protocol.md 의 상수. SDK 의 protocol.ts 와 1:1
  framing.c         BLE 패킷 <-> 메시지 (§2). 동적 할당 없음
  rlp.c             서명 대상 트랜잭션의 형식 검증 + 요약 추출
  store.c           니모닉을 PIN 으로 봉인해 플래시에 넣는 레코드
  wallet.c          명령 처리 · 챌린지 · PIN · 잠금 상태 기계
  hal.h             코어가 플랫폼에 요구하는 전부 (난수/저장/LED/시간/송신)

src/port/zephyr/  Zephyr 구현
  hal_zephyr.c      sys_csrand_get, settings(NVS), GPIO LED/버튼 디바운스
  ble.c             GATT 서비스, 본딩 강제, MTU 에 맞춘 프레이밍

src/port/host/    호스트 테스트용 구현 (펌웨어 이미지에 안 들어감)
src/main.c        HAL·BLE 를 올리고 10ms 주기로 코어를 틱 시키는 것이 전부

암호 스택은 ../nuwallet/src/crypto 와 ../nuwallet/src/micro-ecc 를 공유한다.
```

## 빌드

```sh
west build -b nucode_nu40/nrf52840 firmware/zephyr
west flash
```

보드 정의가 없으면 nRF52840 DK 로도 그대로 올라간다.

```sh
west build -b nrf52840dk/nrf52840 firmware/zephyr
```

devicetree 에 `led0..led3`, `sw0..sw3` 별칭이 있어야 한다. 없는 것은 부팅 시
경고만 내고 그 버튼·LED 만 동작하지 않는다.

## 테스트 (보드 불필요)

```sh
cd firmware/test && make test
```

`test_wallet` 은 실제 BLE 에 나갈 바이트를 그대로 만들어 코어에 넣고 그대로 받아
확인한다. 챌린지는 코어가 뽑은 시퀀스를 들여다보고 눌러 준다 — 실기기에서는
사람이 LED 를 보고 하는 일이다.

검증하는 것: 프레이밍 왕복 · RLP 검증 · 레코드 봉인/개봉/변조 감지 ·
니모닉 생성/확정/복구 · 알려진 주소 파생 · 트랜잭션 서명과 검증 ·
personal_sign · PIN 설정/잠금/해제/재부팅 유지 · 패스프레이즈 · WIPE ·
취소 · 타임아웃 · 3회 실패 폐기 · 잘못된 입력 거부.

## LED 가 말하는 것

| 상태 | LED |
|---|---|
| 지갑 없음 | 전부 꺼짐 |
| 잠김 | LED0 이 1초 주기로 점멸 |
| 잠금 해제됨 | LED3 점등 |
| 서명 챌린지 표시 중 | 눌러야 할 버튼이 하나씩 450ms 켜짐 |
| 챌린지 입력 중 | 맞게 누른 개수만큼 LED0 부터 점등 |
| PIN 입력 중 | 입력한 자릿수만큼 점등. **시퀀스는 보여주지 않는다** |

PIN 과 서명 챌린지는 다른 것이다. `docs/protocol.md` §8 참고.

## 주의할 점

- `hal->random` 이 실패하면 **삼키지 않는다.** 예측 가능한 니모닉이 만들어지느니
  명령을 실패시킨다.
- 개인키는 파생 직후 서명에 쓰고 바로 `memset` 한다. 컴파일러가 그 `memset` 을
  지워 버리는 것을 막을 확실한 방법은 없다 — `memset_s` 가 없다.
- 요청은 한 번에 하나만 처리한다. 승인 대기 중 새 요청은 `0x6985` 로 거절한다.
- BLE 연결이 끊기면 진행 중 요청을 버리고 다시 잠근다.
