# NU-40 DK 펌웨어 설치 가이드

이 문서는 NU-40 DK(nRF52840) 보드에 `arduino-cli`를 사용하지 않고 펌웨어를 설치하는 방법을 설명한다.

설치할 것은 **지갑 펌웨어**(`firmware/nuwallet/`) 다. 저장소에 남아 있는 NU-40 PET
데모는 별개이며, 지갑 웹앱과 함께 쓸 수 없다.

## 권장 방법: UF2 파일 복사

NU-40 DK에는 Adafruit nRF52 UF2/DFU 부트로더가 탑재되어 있다. 따라서 별도의 플래싱 프로그램 없이 보드를 USB 드라이브로 마운트한 뒤 `.uf2` 파일을 복사할 수 있다.

설치할 파일은 하나다.

```text
firmware/nuwallet/build/NUWALLET.UF2    지갑 펌웨어
```

> ⚠️  **옛 NU-40 PET 데모를 지갑 대신 굽지 않는다.** 두 펌웨어는 서로 다른 BLE
> 서비스 UUID 를 광고한다 — 지갑은 `6e754000-…`, 데모는 `7d2a0001-…`. 데모가
> 올라간 보드는 지갑 웹앱의 기기 선택 창에 **아예 나타나지 않는다.**
> "페어링이 안 된다"로 보이는 증상의 흔한 원인이다. 데모의 `.uf2` 는 저장소에
> 두지 않는다 (필요하면 `firmware/arduino/nu40_pet/` 에서 만든다).
>
> 지갑 `.uf2` 의 정본 위치는 `firmware/nuwallet/build/` 하나다 (`.gitignore` 가
> 이 경로만 추적한다). `firmware/build/` 안에 `NUWALLET.UF2` 가 보인다면 예전
> 빌드가 남은 것이니 무시하거나 지운다.

### 설치 순서

1. NU-40 DK를 USB 데이터 케이블로 컴퓨터에 연결한다.
2. 보드의 `RESET` 버튼을 빠르게 두 번 누른다.
3. 보드가 UF2 부트로더 모드로 진입하면 `NRF52BOOT`라는 USB 드라이브가 나타난다.
4. `firmware/nuwallet/build/NUWALLET.UF2`를 `NRF52BOOT` 드라이브에 복사한다.
5. 복사가 끝나면 드라이브가 자동으로 사라지고 보드가 새 펌웨어로 재부팅된다.

macOS Finder에서 파일을 드래그해도 되고, 저장소 루트에서 다음 명령을 실행해도 된다.

```sh
cp firmware/nuwallet/build/NUWALLET.UF2 /Volumes/NRF52BOOT/
```

> 복사가 끝나면 드라이브가 스스로 사라진다. macOS 가 "디스크를 제대로 꺼내지
> 않았습니다" 경고를 띄우는데 정상이다.

마운트 여부는 다음 명령으로 확인한다.

```sh
ls /Volumes
```

`NRF52BOOT`가 출력되면 펌웨어를 복사할 준비가 된 것이다.

## `NRF52BOOT`가 나타나지 않을 때

- `RESET` 버튼을 천천히 두 번 누르는 것이 아니라 빠르게 연속 두 번 누른다.
- 충전 전용 케이블이 아닌 USB 데이터 케이블인지 확인한다.
- USB 허브를 제거하고 컴퓨터의 USB 포트에 직접 연결한다.
- 다른 USB 포트나 케이블로 다시 시도한다.
- 기존 애플리케이션이 실행 중인 직렬 포트와 UF2 부트로더 드라이브는 서로 다르다. USB 직렬 포트가 보인다고 해서 부트로더 모드인 것은 아니다.

## 펌웨어 파일 형식과 도구

| 파일 또는 빌드 방식 | 설치 방법 | 용도 |
| --- | --- | --- |
| `.uf2` | Finder 또는 `cp`로 `NRF52BOOT`에 복사 | 가장 간단한 권장 방식 |
| Zephyr 빌드 | `west flash` | Zephyr 워크스페이스와 지원 디버거를 사용하는 경우 |
| `.hex` | J-Link Commander, OpenOCD 또는 Nordic 도구 | SWD 디버거로 직접 플래시하는 경우 |

일반적인 설치에는 `.hex` 파일이나 SWD 도구가 필요하지 않다. `NUWALLET.UF2` 를
복사하는 방식이 가장 간단하다.

## 펌웨어를 직접 빌드할 때

설치는 위의 UF2 복사로 끝나지만, `.uf2` 파일 자체는 만들어야 한다.

### 1. 컴파일

벤더가 제공하는 것은 `arduino-cli` 코어 하나뿐이다
(`~/Library/Arduino15/packages/nucode/hardware/nrf52/`). 보드 정의(`nu40dk`),
핀 배치(`nu40dk_nrf52840`), 부트로더가 모두 여기 들어 있다.

지갑 펌웨어는 스케치 디렉터리에서 빌드한다. 산출물은 그 옆 `build/` 에 둔다 —
`firmware/build/` 는 옛 데모 전용이라 섞으면 어느 쪽 `.uf2` 인지 알 수 없게 된다.

```sh
cd firmware/nuwallet
arduino-cli compile --fqbn nucode:nrf52:nu40dk --output-dir ./build .
```

### 2. `.hex` → `.uf2`

**코어의 UF2 생성 규칙은 `platform.txt` 에서 주석 처리되어 있다.** 그래서
`arduino-cli compile` 은 `.uf2` 를 만들지 않는다. 변환기를 직접 돌린다.

```sh
CORE=~/Library/Arduino15/packages/nucode/hardware/nrf52/1.0.2
python3 "$CORE/tools/uf2conv/uf2conv.py" -f 0xADA52840 -c \
        -o build/NUWALLET.UF2 build/nuwallet.ino.hex
```

`0xADA52840` 은 nRF52840 의 UF2 family id 다 (`boards.txt` 의
`nu40dk.build.uf2_family`).

### 3. UF2 없이 바로 올리기

부트로더 모드로 들어가는 것이 귀찮으면 시리얼 DFU 로 바로 올려도 된다.
`arduino-cli` 가 `adafruit-nrfutil` 을 호출하고, 1200bps 터치로 보드를 알아서
부트로더에 넣는다. RESET 을 두 번 누를 필요가 없다.

**먼저 `adafruit-nrfutil` 이 있어야 한다.** 코어에 들어 있는 바이너리는 Windows
용뿐이라 macOS·Linux 에서는 직접 설치한다.

```sh
pip3 install --user adafruit-nrfutil
```

`--output-dir` 로 빌드했다면 업로드에도 같은 경로를 `--input-dir` 로 알려줘야
한다. 없으면 기본 빌드 캐시를 뒤지다 산출물을 찾지 못한다.

```sh
cd firmware/nuwallet
arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn nucode:nrf52:nu40dk \
            --input-dir ./build .
```

## Zephyr 는 지금 바로 되지 않는다

`west build -b nucode_nu40/nrf52840` 같은 명령을 본 적이 있다면 **동작하지 않는다.**
`nucode_nu40` 보드 정의는 이 저장소에도, Zephyr 업스트림에도 없다.

Zephyr 로 가려면 보드를 직접 정의해야 한다.

| 필요한 것 | 값 |
| --- | --- |
| LED | P0.13 / P0.14 / P0.15 / P0.16 (`LED_STATE_ON = 1`) |
| 버튼 | P0.11 / P0.12 / P0.24 / P0.25 |
| 플래시 배치 | SoftDevice S140 v6 가 앞을 차지한다 (`nrf52840_s140_v6.ld`). 앱은 그 뒤에서 시작해야 부트로더가 유지된다 |
| 출력 | `CONFIG_BUILD_OUTPUT_UF2=y` — 그래야 UF2 복사 방식을 그대로 쓸 수 있다 |
| 툴체인 | nRF Connect SDK / west 워크스페이스 (1~2GB) |

보드 정의를 쓰지 않고 `nrf52840dk/nrf52840` 로 빌드하면 **핀이 달라 LED 와 버튼이
동작하지 않고**, 부트로더 영역을 덮어써 UF2 복구 경로를 잃을 수 있다.

## 설치 후 확인

펌웨어가 시작되면 보드가 BLE 로 광고한다. 광고 이름은 펌웨어마다 다르다.

| 펌웨어 | 광고 이름 | 서비스 UUID | 확인할 페이지 |
| --- | --- | --- | --- |
| 지갑 | `NuWallet-A2B4C6` 형태 (기기 고유) | `6e754000-…` | `/`, `/setup`, `/dapp`, `/debug` |
| NU-40 PET 데모 | `NU-40 PET` | `7d2a0001-…` | 데모 페이지 |

지갑 이름의 접미사 6자는 `NRF_FICR->DEVICEID` 에서 뽑으며 문자와 숫자가 번갈아
나온다 (헷갈리는 `I`·`O`·`0`·`1` 은 빼고 쓴다). 16진수가 아니다. 보드마다 다르고
재부팅해도 같다.

> **펌웨어를 다시 구웠다면 OS 블루투스 설정에서 기존 `NuWallet-…` 기기를 먼저
> 삭제(잊기)한다.** 보드는 본딩 키를 잊었는데 PC 는 옛 키를 그대로 쓰기 때문에,
> 그냥 다시 연결하면 알림 구독 단계에서 링크가 끊긴다. 재플래시할 때마다 겪는다.
> macOS 는 시스템 설정 → Bluetooth → 해당 기기 → i → "이 기기 잊기".

Chrome 또는 Edge 계열 브라우저에서 프로젝트 웹 앱을 열고 보드 연결 기능을 사용해
동작을 확인할 수 있다. 연결이 안 되면 `docs/BLUETOOTH.md` 를 본다.

Web Bluetooth는 보안상 HTTPS 또는 `localhost` 환경에서 사용해야 한다.

## 보안 주의

이 보드의 Adafruit nRF52 부트로더는 UF2/DFU를 통해 임의 펌웨어를 설치할 수 있으며, 이 프로젝트는 상용 하드웨어 지갑 수준의 보안 장치를 갖춘 제품이 아니다. 실제 자금이나 중요한 개인키를 저장하지 않는다. 자세한 내용은 저장소 루트의 `SECURITY.md`를 참고한다.
