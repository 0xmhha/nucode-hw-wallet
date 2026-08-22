# NU-40 DK 펌웨어 설치 가이드

이 문서는 NU-40 DK(nRF52840) 보드에 `arduino-cli`를 사용하지 않고 펌웨어를 설치하는 방법을 설명한다.

## 권장 방법: UF2 파일 복사

NU-40 DK에는 Adafruit nRF52 UF2/DFU 부트로더가 탑재되어 있다. 따라서 별도의 플래싱 프로그램 없이 보드를 USB 드라이브로 마운트한 뒤 `.uf2` 파일을 복사할 수 있다.

이 저장소에서 바로 설치할 수 있는 펌웨어는 다음과 같다.

```text
firmware/build/NU40PET.UF2      NU-40 PET 데모 (다마고치)
```

> 지갑 펌웨어의 `.uf2` 는 아직 이 디렉터리에 없다. 아래 "펌웨어를 직접 빌드할 때"
> 를 따라 만든다. 설치 절차는 어느 펌웨어든 동일하다.

### 설치 순서

1. NU-40 DK를 USB 데이터 케이블로 컴퓨터에 연결한다.
2. 보드의 `RESET` 버튼을 빠르게 두 번 누른다.
3. 보드가 UF2 부트로더 모드로 진입하면 `NRF52BOOT`라는 USB 드라이브가 나타난다.
4. `firmware/build/NU40PET.UF2`를 `NRF52BOOT` 드라이브에 복사한다.
5. 복사가 끝나면 드라이브가 자동으로 사라지고 보드가 새 펌웨어로 재부팅된다.

macOS Finder에서 파일을 드래그해도 되고, 저장소 루트에서 다음 명령을 실행해도 된다.

```sh
cp firmware/build/NU40PET.UF2 /Volumes/NRF52BOOT/
```

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

이 저장소에는 `firmware/build/nu40_pet.ino.hex`도 있지만, 일반적인 설치에는 `.hex` 파일이나 SWD 도구가 필요하지 않다. `NU40PET.UF2`를 복사하는 방식이 가장 간단하다.

## 펌웨어를 직접 빌드할 때

설치는 위의 UF2 복사로 끝나지만, `.uf2` 파일 자체는 만들어야 한다.

### 1. 컴파일

벤더가 제공하는 것은 `arduino-cli` 코어 하나뿐이다
(`~/Library/Arduino15/packages/nucode/hardware/nrf52/`). 보드 정의(`nu40dk`),
핀 배치(`nu40dk_nrf52840`), 부트로더가 모두 여기 들어 있다.

```sh
arduino-cli compile --fqbn nucode:nrf52:nu40dk --output-dir firmware/build <스케치 경로>
```

### 2. `.hex` → `.uf2`

**코어의 UF2 생성 규칙은 `platform.txt` 에서 주석 처리되어 있다.** 그래서
`arduino-cli compile` 은 `.uf2` 를 만들지 않는다. 변환기를 직접 돌린다.

```sh
CORE=~/Library/Arduino15/packages/nucode/hardware/nrf52/1.0.2
python3 "$CORE/tools/uf2conv/uf2conv.py" -f 0xADA52840 -c \
        -o firmware/build/NUWALLET.UF2 firmware/build/<스케치>.ino.hex
```

`0xADA52840` 은 nRF52840 의 UF2 family id 다 (`boards.txt` 의
`nu40dk.build.uf2_family`).

### 3. UF2 없이 바로 올리기

부트로더 모드로 들어가는 것이 귀찮으면 시리얼 DFU 로 바로 올려도 된다.
`arduino-cli` 가 `adafruit-nrfutil` 을 호출하고, 1200bps 터치로 보드를 알아서
부트로더에 넣는다. RESET 을 두 번 누를 필요가 없다.

```sh
arduino-cli upload -p /dev/cu.usbmodemXXXX --fqbn nucode:nrf52:nu40dk <스케치 경로>
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

| 펌웨어 | 광고 이름 | 확인할 페이지 |
| --- | --- | --- |
| NU-40 PET 데모 | `NU-40 PET` | 데모 페이지 |
| 지갑 | `NuWallet-XXXXXX` (기기 고유) | `/` 와 `/setup` |

Chrome 또는 Edge 계열 브라우저에서 프로젝트 웹 앱을 열고 보드 연결 기능을 사용해
동작을 확인할 수 있다. 연결이 안 되면 `docs/BLUETOOTH.md` 를 본다.

Web Bluetooth는 보안상 HTTPS 또는 `localhost` 환경에서 사용해야 한다.

## 보안 주의

이 보드의 Adafruit nRF52 부트로더는 UF2/DFU를 통해 임의 펌웨어를 설치할 수 있으며, 이 프로젝트는 상용 하드웨어 지갑 수준의 보안 장치를 갖춘 제품이 아니다. 실제 자금이나 중요한 개인키를 저장하지 않는다. 자세한 내용은 저장소 루트의 `SECURITY.md`를 참고한다.
