# 구조

요구된 네 덩어리와 실제 디렉터리의 대응이다. 각 덩어리는 **바로 옆 덩어리하고만**
이야기한다. 그래서 하나를 바꿔도 나머지가 안 무너진다.

```
   ┌──────────────┐   Web Bluetooth   ┌──────────────┐
   │  설정 웹      │◄─────────────────►│              │
   │  /setup       │                   │              │
   ├──────────────┤   @nucode/        │   펌웨어      │   버튼 4개
   │  예제 DApp    │◄──hw-wallet──────►│   (nRF52840) │◄──────────── 사용자
   │  /dapp        │       SDK         │              │   LED 4개
   └──────────────┘                   └──────────────┘
                                              │
                                       개인키는 여기서
                                       나가지 않는다
```

| 요구 | 위치 | 상태 |
|---|---|---|
| 1. nucode 지원 펌웨어 | `firmware/wallet` (Zephyr) | 아래 참고 |
| 2. Bluetooth SDK | `sdk/` | `@nucode/hw-wallet` |
| 3. 펌웨어 설정 웹 | `app/` (생성), `app/setup` (가져오기·PIN·잠금·초기화) | |
| 4. 예제 DApp | `app/dapp` | |

프로토콜은 `docs/protocol.md` 하나뿐이다. 펌웨어 `protocol.h`, SDK `protocol.ts`
가 그 문서에 1:1 대응한다. **셋 중 하나를 바꾸면 나머지 둘도 바꿔야 한다.**

---

## 1. 펌웨어

포트는 Zephyr 하나다. 지갑 로직은 플랫폼을 모르는 순수 C 로 분리되어 있어 보드
없이 호스트에서 전부 테스트한다.

> 초기에는 Arduino(Bluefruit) 포트도 있었지만, 같은 프로토콜을 두 번 구현하게 되어
> 제거했다. 암호 스택은 공유 라이브러리로 남아 Zephyr 빌드와 호스트 테스트가 함께 쓴다.

```
firmware/
  nuwallet/          Arduino 스케치. 보드에 올라가는 것은 이것이다
    nuwallet.ino       HAL 연결, 버튼 폴링, 틱
    src/core/          플랫폼 독립 지갑 코어. Arduino 와 Zephyr 가 같은 파일을 컴파일한다
                       wire(송신) · session(시드) · challenge(승인) · wallet(생명주기)
                       commands(파싱·디스패치) 와 명령 영역별 파일 셋:
                       cmd_setup(셋업·PIN·잠금 해제) · cmd_ethereum · cmd_solana
    src/crypto/        SHA-2 · HMAC · PBKDF2 · Keccak-256 · BIP-39 · BIP-32
    src/micro-ecc/     secp256k1. recovery id 를 꺼내려고 패치했다
    src/chains/        체인별 해시 헬퍼
    src/port/arduino/  HAL (LED · 버튼 · LittleFS · TRNG · CryptoCell Ed25519)
    src/transport/     Bluefruit BLE GATT
  wallet/            Zephyr 포트. 보드 정의가 없어 지금은 빌드되지 않는다 (TASKS T14)
    src/port/zephyr/   BLE GATT · GPIO · NVS · CSPRNG
    src/port/host/     호스트 테스트용 HAL
  test/              호스트에서 도는 테스트. 보드도 SDK 도 필요 없다
```

```sh
cd firmware/test && make test     # 암호 스택 + 지갑 코어 (보드 불필요)
```

보드에 올리는 방법은 `docs/nu40-dk-firmware-installation.md` 를 본다.
**Zephyr 로는 지금 바로 빌드되지 않는다** — `nucode_nu40` 보드 정의가 없다.

핵심 원칙 셋:

- **개인키는 밖으로 나가지 않는다.** 기기가 서명해서 `r‖s‖recid` 만 돌려준다.
- **해시를 받지 않는다.** 트랜잭션 RLP 원문을 받아 기기가 Keccak-256 을 직접
  계산한다. 해시만 받으면 기기가 무엇이든 서명하게 된다.
- **모든 서명에 물리 승인이 필요하다.** 사용자가 보드에서 버튼을
  `1 → 2 → 3 → 4` 순서로 누른다. 프로토타입의 사용성을 우선한 고정 순서다.

## 2. SDK — `sdk/`

```
protocol.ts   상수 · 프레이밍 · 코덱          (문서 §1~§6 그대로)
transport.ts  Web Bluetooth GATT, 요청/응답 짝짓기, 이벤트 배달
client.ts     NuWallet — 셋업 · 주소 · 서명
pin.ts        NuWalletAdmin — PIN · 잠금 · 패스프레이즈 (§8)
provider.ts   NuWalletProvider — EIP-1193. window.ethereum 자리에 꽂는다
rlp.ts        트랜잭션 인코딩
address.ts    Keccak-256 · EIP-55 체크섬
wordlist.ts   BIP-39 영문 2048 단어 (기기는 인덱스만 주고받는다)
```

```sh
cd sdk && npm run build && npm test
```

의존성이 없다. `ethers` 는 선택적 peer 이고 SDK 자체는 쓰지 않는다.

## 3. 설정 웹 — `app/`, `app/setup`

- `/` — 보드 연결, 새 지갑 생성(니모닉 표시 → 백업 확인 → 확정), 주소 파생
- `/setup` — 기존 니모닉 가져오기, PIN 변경, BIP-39 패스프레이즈로 잠금 해제,
  초기화(WIPE)

PIN 은 웹에서 입력하지 않는다. 화면의 패드는 보드의 버튼 1~4 배치를 보여 줄 뿐이고,
사용자는 보드의 버튼으로 6자리를 누른다. 웹이 PIN 을 알면 "호스트가 감염돼도 기기를
못 연다" 는 목적이 무너지기 때문이다. 무엇을 누르라고 안내할지는 보드가 승인 시작
이벤트의 KIND 로 알려 준다 (`app/lib/approval.ts`).

두 페이지가 같이 쓰는 코드는 `app/lib/` 에 있다. 승인 진행 표시(`useApproval`),
작업 실행과 오류 문구(`useTask`), wei 변환이다. 각 페이지의 화면 카드는
`components/` 에 있고, `page.tsx` 는 상태를 들고 버튼과 SDK 호출을 잇기만 한다.

## 4. 예제 DApp — `app/dapp`

`NuWalletProvider` 하나만 쓴다. 체인 설정(RPC · chainId · 파생 경로), 지갑 연결,
잔액·nonce 조회, 트랜잭션 서명/전송, `personal_sign`, EIP-712 Permit 서명, 그리고
오간 호출을 그대로 보여주는 로그 패널이 있다. EIP-6963 으로 자신을 알린다.

전송할 때는 수수료를 먼저 정해 트랜잭션에 넣고(`dapp/fees.ts`), 같은 숫자의 상한으로
잔액을 확인한다. provider 는 호출자가 준 수수료를 그대로 쓰므로 확인한 값과 서명되는
값이 같다. 폼 값으로 트랜잭션과 Permit 을 만드는 순수 함수는 `dapp/tx.ts` 에 있다.

```sh
npm run dev     # http://localhost:3000/dapp
```

Web Bluetooth 는 **HTTPS 또는 localhost** 에서만 되고, Chrome/Edge 계열
데스크톱 브라우저가 필요하다.

---

## 안 되는 것

`SECURITY.md` 에 전부 적어 두었다. 요약하면 **물리 공격 내성**과 **신뢰할 수 있는
표시 장치**가 없다. 이 둘이 하드웨어 지갑을 하드웨어 지갑으로 만드는 부분이고,
이 하드웨어로는 안 된다. 테스트넷만 쓰세요.
