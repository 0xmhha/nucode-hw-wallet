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
| 1. nucode 지원 펌웨어 | `firmware/zephyr` (Zephyr) | 아래 참고 |
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
  nuwallet/src/      공유 라이브러리 (보드용 코드 없음)
    src/crypto/        SHA-2 · HMAC · PBKDF2 · Keccak-256 · BIP-39 · BIP-32
    src/micro-ecc/     secp256k1. recovery id 를 꺼내려고 패치했다
    src/chains/        체인별 해시 헬퍼
  zephyr/            Zephyr 앱  →  firmware/zephyr/README.md
    src/app/           플랫폼 독립 코어 (프레이밍 · RLP 검증 · 저장 · 상태 기계)
    src/port/zephyr/   BLE GATT · GPIO · NVS · CSPRNG
    src/port/host/     테스트용
  test/              호스트에서 도는 테스트. 보드도 SDK 도 필요 없다
```

```sh
cd firmware/test && make test     # 암호 스택 + 지갑 코어
west build -b nucode_nu40/nrf52840 firmware/zephyr && west flash
```

핵심 원칙 셋:

- **개인키는 밖으로 나가지 않는다.** 기기가 서명해서 `r‖s‖recid` 만 돌려준다.
- **해시를 받지 않는다.** 트랜잭션 RLP 원문을 받아 기기가 Keccak-256 을 직접
  계산한다. 해시만 받으면 기기가 무엇이든 서명하게 된다.
- **모든 서명에 물리 승인이 필요하다.** 기기가 랜덤 시퀀스를 LED 로 보여주고
  사용자가 버튼으로 따라 누른다. 시퀀스는 BLE 로 절대 나가지 않는다.

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
- `/setup` — 기존 니모닉 가져오기, **버튼 4개 조합으로 PIN 설정**,
  BIP-39 패스프레이즈로 잠금 해제, 초기화(WIPE)

PIN 패드는 보드의 버튼 1~4 를 그대로 옮겨 놓은 것이다. 웹에서 PIN 을 보내는 것
만으로는 바뀌지 않는다 — 보드에서 랜덤 챌린지를 한 번 통과해야 저장된다.

## 4. 예제 DApp — `app/dapp`

`NuWalletProvider` 하나만 쓴다. 체인 설정(RPC · chainId · 파생 경로), 지갑 연결,
잔액·nonce 조회, 트랜잭션 서명/전송, `personal_sign`, 그리고 오간 호출을 그대로
보여주는 로그 패널.

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
