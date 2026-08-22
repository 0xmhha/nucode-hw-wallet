# 블루투스 연결이 안 될 때

증상이 전부 "페어링이 안 된다" 로 보이지만 원인은 여럿이다. 위에서부터 확인한다.

## 1. 기기 선택 창이 비어 있다

**보드가 이미 연결되어 있다.** BLE 주변 장치는 연결되면 광고를 멈춘다. 다른 탭,
다른 브라우저 창, 또는 OS 가 잡고 있으면 선택 창에 아무것도 안 뜬다.

- 다른 탭을 닫는다
- `chrome://bluetooth-internals/#devices` 에서 연결 상태를 확인한다
- 보드 전원을 껐다 켠다

**펌웨어가 지갑 펌웨어가 아니다.** 옛 NU-40 PET 데모는 다른 서비스 UUID
(`7d2a0001-…`) 를 광고한다. 지갑 웹은 `6e754000-7761-4c4c-4554-000000000001`
로만 거른다.

## 2. 연결은 되는데 바로 끊긴다 / `NetworkError`

**본딩 키가 어긋났다.** 가장 흔하다. 펌웨어를 다시 올리면 보드는 저장된 본딩
키를 잊는데, PC 는 옛 키를 그대로 쓴다. 암호화가 실패하고 링크가 끊긴다.

→ **OS 블루투스 설정에서 `NuWallet-…` 기기를 삭제(잊기)한 뒤 다시 연결한다.**

| | |
|---|---|
| macOS | 시스템 설정 → Bluetooth → 기기 옆 ⓘ → 기기 지우기 |
| Windows | 설정 → Bluetooth 및 장치 → 장치 제거 |
| Linux | `bluetoothctl remove <MAC>` |

## 3. 연결은 되는데 아무 명령도 응답이 없다

RX 특성은 암호화된 링크를 요구한다. 페어링이 끝나지 않았으면 쓰기가 조용히
버려진다 — 응답 없는 쓰기(write-without-response)에는 오류를 돌려줄 경로가 없기
때문이다.

SDK 는 **각 메시지의 첫 패킷을 응답 있는 쓰기로** 보내서 이 오류가 즉시
드러나게 한다 (`sdk/src/transport.ts`). 그래도 응답이 없다면 보드 쪽 로그를 본다.

```sh
cd firmware && make monitor      # Arduino
west attach                      # Zephyr
```

## 4. `SecurityError` 또는 선택 창이 아예 안 뜬다

Web Bluetooth 는 **HTTPS 또는 localhost** 에서만, 그리고 **버튼 클릭 같은 사용자
동작 안에서만** 기기를 선택할 수 있다.

- `npm run dev` 는 `http://localhost:3000` 이라 괜찮다
- LAN IP (`http://192.168.…`) 로 열면 안 된다
- 연결 버튼 핸들러에서 `wallet.connect()` 전에 `await` 를 넣으면 제스처가 끊겨
  실패한다. 반드시 첫 `await` 가 `connect()` 여야 한다

## 5. 브라우저

데스크톱 Chrome / Edge / Opera 만 된다. Safari 와 Firefox 는 Web Bluetooth 를
구현하지 않는다. iOS 는 전부 안 된다 (Bluefy 같은 전용 브라우저 제외).

## 6. 그래도 안 되면

`chrome://bluetooth-internals/#devices` 에서 스캔해 `NuWallet-…` 가 보이는지,
서비스 목록에 `6e754000-…-0001` 이 있는지 확인한다. 거기서 안 보이면 웹 문제가
아니라 펌웨어 광고 문제다.
