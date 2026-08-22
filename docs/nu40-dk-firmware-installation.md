# NU-40 DK 펌웨어 설치 가이드

이 문서는 NU-40 DK(nRF52840) 보드에 `arduino-cli`를 사용하지 않고 펌웨어를 설치하는 방법을 설명한다.

## 권장 방법: UF2 파일 복사

NU-40 DK에는 Adafruit nRF52 UF2/DFU 부트로더가 탑재되어 있다. 따라서 별도의 플래싱 프로그램 없이 보드를 USB 드라이브로 마운트한 뒤 `.uf2` 파일을 복사할 수 있다.

이 저장소에서 바로 설치할 수 있는 펌웨어는 다음과 같다.

```text
firmware/build/NU40PET.UF2
```

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

## Zephyr 대안

Zephyr 개발 환경과 호환 디버거가 준비되어 있다면 다음 방식도 사용할 수 있다.

```sh
west build -b nucode_nu40/nrf52840 firmware
west flash
```

`west flash`는 UF2 파일 복사와 달리 Zephyr 툴체인 및 보드가 지원하는 플래시 러너/JTAG·SWD 디버거 구성이 필요할 수 있다.

## 설치 후 확인

펌웨어가 시작되면 보드는 BLE 장치 이름 `NU-40 PET`으로 광고한다. Chrome 또는 Edge 계열 브라우저에서 프로젝트 웹 앱을 열고 보드 연결 기능을 사용해 동작을 확인할 수 있다.

Web Bluetooth는 보안상 HTTPS 또는 `localhost` 환경에서 사용해야 한다.

## 보안 주의

이 보드의 Adafruit nRF52 부트로더는 UF2/DFU를 통해 임의 펌웨어를 설치할 수 있으며, 이 프로젝트는 상용 하드웨어 지갑 수준의 보안 장치를 갖춘 제품이 아니다. 실제 자금이나 중요한 개인키를 저장하지 않는다. 자세한 내용은 저장소 루트의 `SECURITY.md`를 참고한다.
