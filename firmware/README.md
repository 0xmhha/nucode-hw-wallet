# NU-40 PET 펌웨어

NU-40 DK(nRF52840)를 BLE 주변 장치로 만들며, SW0 버튼을 누르면 웹에 먹이 이벤트를 보냅니다. 웹에서 받은 기분에 따라 LED0~2를 RGB 채널처럼 사용합니다.

| 기분 | LED 색 | 점멸 주기 |
| --- | --- | --- |
| happy | 초록 | 1,400 ms |
| okay | 노랑(빨강+초록) | 760 ms |
| hungry | 빨강 | 260 ms |

Zephyr 워크스페이스에서 아래처럼 빌드하고 UF2/J-Link로 보드에 플래시하세요.

```sh
west build -b nucode_nu40/nrf52840 firmware
west flash
```

브라우저는 보안상 HTTPS 또는 localhost에서만 Web Bluetooth를 허용하며, Chrome/Edge 계열 데스크톱 브라우저를 권장합니다.
