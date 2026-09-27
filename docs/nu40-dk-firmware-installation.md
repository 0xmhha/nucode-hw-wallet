# NU-40 DK 펌웨어 빌드와 설치

지갑 펌웨어(`firmware/nuwallet/`)를 소스에서 빌드해 NU-40 DK(nRF52840)에 올리는
방법이다. 개발 도구가 하나도 없는 머신에서 시작한다고 가정한다.

펌웨어 바이너리(`.uf2`, `.hex`, `.zip`)는 저장소에 두지 않는다. 빌드할 때마다
내용 전체가 바뀌어 커밋할 때마다 저장소가 수백 KB 씩 커지고, 누군가 다시 빌드해
커밋하지 않으면 소스와 어긋난다. 그래서 받는 사람이 직접 만든다.

> 이 절차는 macOS(Apple Silicon, arduino-cli 1.5.1, Python 3.10)에서 빈 환경부터
> 따라 해서 확인했다. Linux 명령은 같은 도구의 공식 설치 방법을 옮긴 것이고 직접
> 돌려 보지는 않았다. Windows 는 다루지 않는다.

## 전체 흐름

한 번만 하는 준비가 세 가지 있다. arduino-cli 를 설치하고, 보드 제조사의 코어를
받고, 업로드 도구 `adafruit-nrfutil` 을 설치한다. 그다음부터는
`scripts/build-firmware.sh` 하나로 빌드하고 올린다.

빌드는 두 단계다. 먼저 `arduino-cli` 가 스케치를 컴파일해 `.hex` 와 DFU 용 `.zip` 을
만들고, 이어서 코어에 든 변환기가 `.hex` 를 `.uf2` 로 바꾼다. 보드에 올리는 방법도
두 가지인데, USB 케이블 하나로 명령 한 줄이면 되는 시리얼 DFU 를 권한다.

## 1. 한 번만 할 준비

필요한 것은 git, Python 3.8 이상, 인터넷 연결, **USB 데이터 케이블**이다. 충전
전용 케이블로는 보드가 보이지 않는다. 디스크는 약 1GB 가 필요하다 (대부분
ARM 컴파일러다).

### 1-1. arduino-cli

macOS:

```sh
brew install arduino-cli
```

Linux (직접 돌려 보지 않았다):

```sh
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | BINDIR=~/.local/bin sh
```

`arduino-cli version` 이 버전을 출력하면 된다.

### 1-2. 보드 코어

NU-40 DK 의 보드 정의, 핀 배치, ARM 컴파일러, BLE 스택(Bluefruit), 암호 가속
라이브러리(nRFCrypto), 파일 시스템(LittleFS)이 모두 제조사 코어 하나에 들어 있다.
따로 설치할 Arduino 라이브러리는 없다.

```sh
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://raw.githubusercontent.com/Nucode01/Adafruit_nRF52_Arduino/refs/heads/master/package_nuduino_index.json
arduino-cli core update-index
arduino-cli core install nucode:nrf52
```

마지막 명령이 약 1GB 를 받는다. 끝나면 `arduino-cli core list` 에
`nucode:nrf52  1.0.2` 가 보인다. `config init` 이 "이미 있다" 고 하면 건너뛴다.

### 1-3. adafruit-nrfutil

컴파일 마지막 단계에서 DFU 패키지(`.zip`)를 만들 때와 보드에 올릴 때 쓴다.
코어에도 들어 있지만 **macOS 용은 그대로 쓸 수 없다.** 실행 권한 없이 풀리고,
x86_64 바이너리라 Apple Silicon 에서는 Rosetta 까지 필요하다. pip 로 설치한 것을
쓴다.

시스템 Python 을 건드리지 않도록 가상환경에 넣는다.

```sh
python3 -m venv ~/.venvs/nuwallet
~/.venvs/nuwallet/bin/pip install adafruit-nrfutil
```

빌드하는 셸마다 이 가상환경을 켠다.

```sh
source ~/.venvs/nuwallet/bin/activate
adafruit-nrfutil version        # adafruit-nrfutil version 0.5.3... 이 나오면 된다
```

Linux 는 시리얼 포트를 쓰려면 `dialout` 그룹에 들어가 있어야 한다
(`sudo usermod -aG dialout $USER` 후 다시 로그인).

## 2. 빌드

저장소 루트에서 한다.

```sh
source ~/.venvs/nuwallet/bin/activate
scripts/build-firmware.sh
```

끝나면 `firmware/nuwallet/build/` 에 다음이 생긴다. 이 폴더는 git 이 무시한다.

| 파일 | 쓰는 곳 |
| --- | --- |
| `NUWALLET.UF2` | `NRF52BOOT` 드라이브에 복사해 올린다 |
| `nuwallet.ino.zip` | 시리얼 DFU 로 올린다 |
| `nuwallet.ino.hex` | SWD 디버거로 올린다. `.uf2` 는 이것을 변환한 것이다 |

스크립트는 두 명령을 돌린다. 직접 치고 싶으면 이렇게 한다.

```sh
cd firmware/nuwallet
arduino-cli compile --fqbn nucode:nrf52:nu40dk \
  --build-property "tools.nrfutil.cmd.macosx=adafruit-nrfutil" \
  --output-dir ./build .
CORE=$(arduino-cli config get directories.data)/packages/nucode/hardware/nrf52/1.0.2
python3 "$CORE/tools/uf2conv/uf2conv.py" -f 0xADA52840 -c \
  -o build/NUWALLET.UF2 build/nuwallet.ino.hex
```

`--build-property` 는 1-3 에서 말한 macOS 문제 때문에 붙인다. 코어의 것 대신 PATH 에
있는 `adafruit-nrfutil` 을 쓰게 한다. Linux 에서는 원래 PATH 의 것을 쓰므로 붙여도
영향이 없다. UF2 변환을 따로 하는 이유는 코어의 `platform.txt` 가 UF2 생성 규칙을
주석으로 막아 두었기 때문이다. `0xADA52840` 은 nRF52840 의 UF2 family id 다.

## 3. 보드에 올리기

### 방법 A. 시리얼 DFU (권장)

보드를 USB 로 연결하고 포트 이름을 찾는다.

```sh
ls /dev/cu.usbmodem*      # macOS
ls /dev/ttyACM*           # Linux
```

포트를 넘기면 빌드한 뒤 바로 올린다.

```sh
scripts/build-firmware.sh /dev/cu.usbmodem1101
```

1200bps 신호로 보드를 부트로더에 넣기 때문에 버튼을 누를 필요가 없다. 약 12초
걸린다. 첫 시도가 부트로더가 뜨기 전에 붙어 실패하는 일이 있는데, 스크립트가
한 번 더 시도한다. 끝에 `Device programmed.` 가 나오면 된다.

### 방법 B. UF2 복사

개발 도구 없이 올릴 때 쓰는 방식이다. 다른 머신에서 빌드한 `NUWALLET.UF2` 만
받아서 올릴 수도 있다.

1. 보드의 `RESET` 버튼을 빠르게 두 번 누른다.
2. `NRF52BOOT` 라는 USB 드라이브가 나타난다 (`ls /Volumes`).
3. `NUWALLET.UF2` 를 그 드라이브에 복사한다.

   ```sh
   cp firmware/nuwallet/build/NUWALLET.UF2 /Volumes/NRF52BOOT/
   ```

4. 복사가 끝나면 드라이브가 사라지고 보드가 새 펌웨어로 재부팅된다. macOS 가
   "디스크를 제대로 꺼내지 않았습니다" 경고를 띄우는데 정상이다.

1200bps 신호로는 이 드라이브가 뜨지 않는다. RESET 두 번이 필요하다.

> ⚠️ **옛 NU-40 PET 데모를 지갑 대신 굽지 않는다.** 두 펌웨어는 서로 다른 BLE
> 서비스 UUID 를 광고한다 (지갑 `6e754000-…`, 데모 `7d2a0001-…`). 데모가 올라간
> 보드는 지갑 웹앱의 기기 선택 창에 아예 나타나지 않는다.

## 잘 안 될 때

| 증상 | 원인과 해결 |
| --- | --- |
| `fork/exec …/adafruit-nrfutil/macos/adafruit-nrfutil: permission denied` | 코어의 macOS 용 nrfutil 을 쓰려 한 것이다. `scripts/build-firmware.sh` 를 쓰거나 `--build-property "tools.nrfutil.cmd.macosx=adafruit-nrfutil"` 을 붙이고, 가상환경을 켰는지 확인한다 |
| `adafruit-nrfutil 이 없습니다` | 1-3 의 가상환경을 켜지 않은 셸이다. `source ~/.venvs/nuwallet/bin/activate` |
| `Failed to upgrade target` 가 두 번 연속 | 포트 이름이 맞는지, 다른 프로그램(시리얼 모니터 등)이 포트를 잡고 있지 않은지 본다. 안 되면 RESET 두 번으로 부트로더에 넣고 방법 B 로 올린다 |
| `NRF52BOOT` 가 안 나타남 | RESET 을 천천히가 아니라 빠르게 연속 두 번 누른다. 데이터 케이블인지, USB 허브 없이 직접 꽂았는지 본다 |
| `/dev/cu.usbmodem*` 가 없음 | 데이터 케이블인지 본다. 펌웨어가 멈춰 USB 가 안 뜨면 RESET 두 번 → 방법 B |

## Zephyr 로 빌드하기

지금 보드에 올려 쓰는 것은 위의 Arduino 스케치다. `firmware/wallet` 의 Zephyr 앱은
같은 코어를 쓰는 두 번째 포트이고, 빌드는 되지만 실기기 확인은 아직이다
(`docs/TASKS.md` 의 T14).

NU-40 보드 정의(`nucode_nu40/nrf52840`)는 Zephyr `main` 에 들어가 있다. 아직 어느
릴리스에도 없어서 `firmware/wallet/west.yml` 이 검증한 커밋 하나에 고정한다.

### 한 번만 할 준비

Zephyr `main` 의 빌드 스크립트는 **Python 3.12 이상**이 필요하다. 3.10 으로 만들면
`relative_to() got an unexpected keyword argument 'walk_up'` 으로 멈춘다.

```sh
brew install ninja dtc                       # macOS. cmake 도 없으면 같이 설치한다

# Zephyr SDK — ARM 툴체인만 받는다 (전체 번들은 1.8GB)
cd ~
gh release download v1.0.1 -R zephyrproject-rtos/sdk-ng \
  -p 'zephyr-sdk-1.0.1_macos-aarch64_minimal.tar.xz' \
  -p 'toolchain_gnu_macos-aarch64_arm-zephyr-eabi.tar.xz'
tar xf zephyr-sdk-1.0.1_macos-aarch64_minimal.tar.xz
mkdir -p zephyr-sdk-1.0.1/gnu
tar xf toolchain_gnu_macos-aarch64_arm-zephyr-eabi.tar.xz -C zephyr-sdk-1.0.1/gnu
zephyr-sdk-1.0.1/setup.sh -c                 # CMake 가 SDK 를 찾게 등록한다

# west 작업 공간 — 모듈은 west.yml 이 고른 다섯 개만 받는다 (약 1.3GB)
python3.13 -m venv ~/zephyr-nu/.venv
. ~/zephyr-nu/.venv/bin/activate
pip install west
west init -m https://github.com/0xmhha/nucode-hw-wallet --mf firmware/wallet/west.yml ~/zephyr-nu
cd ~/zephyr-nu && west update
pip install -r zephyr/scripts/requirements-base.txt
```

Linux 는 SDK 파일 이름의 `macos-aarch64` 를 `linux-x86_64` 로 바꾼다.

### 빌드

저장소 루트에서:

```sh
ZEPHYR_WS=~/zephyr-nu scripts/build-zephyr.sh            # firmware/wallet/build/zephyr/zephyr.uf2
ZEPHYR_WS=~/zephyr-nu scripts/build-zephyr.sh pristine   # 처음부터 다시
```

작업 공간의 Zephyr 커밋이 `west.yml` 의 고정값과 다르면 경고를 낸다.

### Zephyr 펌웨어 올리기 — 먼저 읽을 것

**올리면 Arduino 로 돌아가는 데 절차가 필요하다.** Zephyr 앱은 `0x1000` 부터
올라가서 Arduino 가 쓰는 S140 SoftDevice 자리(`0x1000`~`0x26000`)를 덮어쓴다.
부트로더는 이것을 막지 않는다 (Adafruit 부트로더의 `USER_FLASH_START` 가 `0x1000`).

**지갑도 옮겨지지 않는다.** Arduino 는 지갑 레코드를 LittleFS 에, Zephyr 는 NVS
(`0xDC000`~`0xE0000`)에 둔다. Zephyr 를 올린 뒤에는 복구 문구로 지갑을 다시 만든다.

올리는 법:

1. OS 블루투스 설정에서 기존 `NuWallet-…` 을 지운다 (아래 "설치 후 확인" 참고).
2. RESET 을 빠르게 두 번 누른다. 부트로더 드라이브가 뜬다. 이름은 부트로더
   빌드마다 다르다 (시험한 보드는 `NRF52BOOT`, 업스트림 문서는 `BARAM-NU40`).
3. **드라이브의 `CURRENT.UF2` 를 먼저 복사해 둔다.** 지금 플래시 내용 그대로
   (SoftDevice 와 Arduino 앱)라서, 되돌릴 때 이 파일을 다시 복사하면 된다.
4. `firmware/wallet/build/zephyr/zephyr.uf2` 를 그 드라이브에 복사한다.

**보드와 호스트의 본딩을 같이 지운다.** 펌웨어를 새로 올려도 Zephyr 의 저장 영역은
지워지지 않아 보드는 예전 본딩을 기억한다. 호스트 쪽만 지우면 보드가 "Encryption is
insufficient" 로 답하고, 호스트는 다시 페어링하지 않아 연결이 쓸 수 없는 채로 남는다.
보드 쪽은 공장 초기화로 지운다 (`docs/protocol.md` §8).

첫 연결의 페어링은 4초쯤 걸린다. Zephyr 는 LESC 의 P-256 을 소프트웨어로 한다.
그동안 첫 요청이 거절되는데, SDK 가 기다렸다가 결과를 받는다.

Zephyr 앱은 USB 시리얼로 로그를 낸다. Arduino 와 달리 부팅 로그도 보인다.

```sh
screen /dev/cu.usbmodem* 115200
```

Arduino 로 돌아가려면 RESET 을 두 번 누르고, 3단계에서 백업한 `CURRENT.UF2` 를
드라이브에 복사한다. SoftDevice 와 Arduino 앱이 함께 돌아온다. Arduino 의 지갑
레코드(LittleFS, `0xED000`~)는 Zephyr 가 쓰지 않는 영역이라 남아 있다.
**이 되돌리기는 아직 실기기에서 해 보지 않았다.**

백업이 없으면 SoftDevice 와 부트로더를 함께 다시 올린 뒤 스케치를 올린다.
패키지는 Arduino 코어 안에 있다. 이 절차도 실기기에서 해 보지 않았다.

```sh
# RESET 두 번으로 부트로더에 넣은 뒤
CORE=$(ls -d "$(arduino-cli config get directories.data)"/packages/nucode/hardware/nrf52/* | sort -V | tail -1)
adafruit-nrfutil --verbose dfu serial -b 115200 --singlebank -p /dev/cu.usbmodem1101 \
  --package "$CORE/bootloader/NU40DK_nRF52840/NU40DK_nRF52840_bootloader-0.9.2_s140_6.1.1.zip"
scripts/build-firmware.sh /dev/cu.usbmodem1101
```

### 업스트림 보드 정의와 실물이 다른 곳

- **LED 극성.** 업스트림 DTS 는 `GPIO_ACTIVE_HIGH` 인데 이 보드의 LED 는 LOW 에서
  켜진다. `firmware/wallet/boards/nucode_nu40_nrf52840.overlay` 가 뒤집는다.
- **부트로더 위치.** 업스트림 DTS 주석은 UF2 부트로더가 `0xE0000` 에 있다고 적지만,
  Arduino 코어가 싣고 오는 부트로더(`…bootloader-0.9.2_s140_6.1.1.hex`)는 `0xF4000`
  에 있다. 업스트림 배치는 앱과 저장 영역을 `0xE0000` 아래에 두므로 둘 다에서
  안전하다. 보드에 실제로 어느 부트로더가 있는지는 부트로더 드라이브의
  `INFO_UF2.TXT` 로 확인한다. 시험한 보드에는 NU-40 전용이 아니라 Nordic
  nRF52840 DK 용 Adafruit 부트로더 0.9.1 (`Board-ID: nRF52840-pca10056-v1`,
  SoftDevice S140 6.1.1)이 올라가 있었다. 두 보드는 LED·버튼 핀이 같아 동작한다.

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
