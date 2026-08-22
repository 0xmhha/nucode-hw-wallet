# NuWallet BLE 프로토콜 v1

펌웨어 · SDK · 웹이 모두 이 문서를 구현한다. **한쪽만 바꾸면 안 된다.**

---

## 1. GATT

| 항목 | UUID |
|---|---|
| Service | `6e754000-7761-4c4c-4554-000000000001` |
| RX (Write / Write No Response) — 호스트 → 기기 | `6e754000-7761-4c4c-4554-000000000002` |
| TX (Notify) — 기기 → 호스트 | `6e754000-7761-4c4c-4554-000000000003` |

- 광고 이름: `NuWallet-XXXXXX` (기기 고유, `NRF_FICR->DEVICEID` 유래, 재부팅해도 동일)
- 광고에 Service UUID 를 포함한다 — Web Bluetooth 가 `filters.services` 로 잡는다.
- **본딩 필수.** 페어링 없이 들어온 연결은 어떤 명령도 처리하지 않는다.

## 2. 프레이밍

BLE ATT MTU 는 협상 결과에 따라 23~247 바이트다. 한 패킷에 안 들어가는 메시지를
쪼개기 위해 Ledger 의 BLE 프레이밍과 같은 구조를 쓴다.

```
첫 패킷 (seq == 0)
  0       TAG        0x05 = 메시지, 0x06 = 이벤트
  1..2    SEQ        big-endian u16, 0
  3..4    TOTAL_LEN  big-endian u16, 이어붙였을 때의 전체 페이로드 길이
  5..     PAYLOAD

이후 패킷 (seq > 0)
  0       TAG
  1..2    SEQ        1, 2, 3 ...
  3..     PAYLOAD
```

- 수신 측은 `TOTAL_LEN` 만큼 모일 때까지 이어붙인다.
- `SEQ` 가 건너뛰면 즉시 조립 버퍼를 버리고 `0x6F01 FRAMING_ERROR` 를 낸다.
- 최대 메시지 길이 **2048 바이트**. 넘으면 `0x6A84 TOO_LARGE`.

## 3. 메시지

### 요청 (TAG 0x05, 호스트 → 기기)

```
0       CMD        u8
1..2    LEN        big-endian u16
3..     PAYLOAD
```

### 응답 (TAG 0x05, 기기 → 호스트)

```
0..1    STATUS     big-endian u16
2..3    LEN        big-endian u16
4..     PAYLOAD
```

### 이벤트 (TAG 0x06, 기기 → 호스트, 요청 없이 발생)

```
0       EVT        u8
1..2    LEN        big-endian u16
3..     PAYLOAD
```

## 3.5 체인 선택자

지갑은 멀티체인이다. **경로를 받는 모든 명령은 경로 앞에 체인 바이트를 하나 둔다.**

```
CHAIN_PATH  =  [CHAIN:1] [DEPTH:1] [PATH: u32 x DEPTH]
```

| 값 | 체인 | 곡선 | 파생 | 기본 경로 |
|---|---|---|---|---|
| `0x01` | Ethereum 계열 | secp256k1 | BIP-32 | `m/44'/60'/0'/0/0` |
| `0x02` | Solana | ed25519 | SLIP-0010 (하드닝 전용) | `m/44'/501'/0'/0'` |

### 대상 네트워크 — 테스트넷 전용

**기기는 네트워크를 모른다.** 체인 바이트는 곡선과 파생 규칙만 고른다. 어느
네트워크인지는 페이로드가 결정한다 — Ethereum 은 RLP 안의 `chainId`, Solana 는
호스트가 붙이는 recent blockhash 다. 그래서 **네트워크를 바꿔도 펌웨어는 그대로**다.

| 체인 | 네트워크 | 식별자 | RPC |
|---|---|---|---|
| Ethereum | **Base Sepolia** | `chainId 84532` (`0x14a34`) | `https://sepolia.base.org` |
| Solana | **Testnet** | genesis `4uhcVJyU9pJkvQyS88uRDiswHXSCkY3zQawwpjk2NsNY` | `https://api.testnet.solana.com` |

- Base Sepolia 는 BIP-44 coin type 이 Ethereum 과 같은 **60** 이다. 파생 경로가
  메인넷과 같으므로 경로만 보고 네트워크를 구분할 수 없다.
- Solana 도 testnet/devnet/mainnet 이 coin type **501** 을 공유한다.
- `v = recid + chainId*2 + 35` 이므로 Base Sepolia 에서 `v` 는 **169099 또는 169100** 이다.
- 값의 출처: Base 는 `ethereum-lists/chains`, Solana 는 각 클러스터 RPC 의
  `getGenesisHash` 응답. SDK `src/networks.ts` 가 단일 출처이고 회귀 테스트가 있다.

> Solana 는 **testnet 과 devnet 이 다르다.** 개발 도구와 에어드랍은 대부분 devnet 을
> 가정하므로, testnet 에서 잔액이 안 잡히면 이걸 먼저 의심해야 한다.
> `SOLANA_DEVNET` 도 SDK 에 정의해 뒀다.

체인별로 명령을 따로 두지 않는다. **같은 명령이 체인 바이트로 갈린다.** 체인을
추가할 때 명령 번호를 늘리지 않아도 되고, SDK 와 펌웨어의 분기점이 한 곳에 모인다.

- Solana 는 SLIP-0010 규칙상 **모든 경로 요소가 하드닝**이어야 한다. 비하드닝
  요소가 들어오면 `0x6A80 BAD_PARAM`.
- 기기가 모르는 체인 바이트는 `0x6A81 UNSUPPORTED_CHAIN`.

## 4. 상태 코드

| 코드 | 이름 | 의미 |
|---|---|---|
| `0x9000` | OK | 성공 |
| `0x9001` | PENDING | 접수됨. 결과는 이벤트로 온다 |
| `0x5515` | LOCKED | 잠김 |
| `0x6501` | CHALLENGE_TIMEOUT | 승인 시간 초과 |
| `0x6502` | CHALLENGE_FAILED | 버튼 시퀀스 3회 불일치 |
| `0x6503` | PIN_REQUIRED | PIN 이 설정되어 있고 아직 잠김 |
| `0x6982` | NOT_INITIALIZED | 지갑이 아직 생성되지 않음 |
| `0x6983` | ALREADY_INITIALIZED | 이미 생성됨. 먼저 WIPE |
| `0x6985` | USER_REJECTED | 사용자가 거부 |
| `0x6A80` | BAD_PARAM | 페이로드 형식 오류 |
| `0x6A81` | UNSUPPORTED_CHAIN | 기기가 모르는 체인 바이트 |
| `0x6A84` | TOO_LARGE | 메시지가 2048 초과 |
| `0x6D00` | UNKNOWN_CMD | 모르는 명령 |
| `0x6F00` | DEVICE_ERROR | 내부 오류 |
| `0x6F01` | FRAMING_ERROR | 프레이밍 오류 |

## 5. 명령

### `0x01 GET_VERSION`

요청 없음. 응답:

```
0       PROTOCOL_VERSION  u8   = 1
1       FW_MAJOR          u8
2       FW_MINOR          u8
3       FLAGS             u8   bit0 initialized, bit1 locked, bit3 has_pin
4..     DEVICE_NAME       UTF-8 (남은 길이 전부)
```

### `0x02 GET_STATE`

응답:

```
0       FLAGS        u8   bit0 initialized, bit1 locked, bit2 challenge_active,
                          bit3 has_pin
1..4    REQUEST_ID   big-endian u32   진행 중인 요청. 없으면 0
5       ATTEMPTS     u8   남은 시도 횟수
6       PIN_LEN      u8   설정된 PIN 길이. 미설정이면 0
```

---

### `0x10 SETUP_GENERATE` — 새 니모닉 생성

**이 명령은 지갑이 초기화되지 않은 상태에서만 동작한다.**

```
요청  0   WORD_COUNT  u8   12 또는 24
응답  0   COUNT     u8
      1.. WORD_IDX  big-endian u16 x COUNT   BIP-39 영문 워드리스트 인덱스
```

> **니모닉이 BLE 로 평문 전송된다.** 프로토타입이라 그렇다. 실제 제품은 기기
> 화면에만 표시해야 한다. `SECURITY.md` 참고.

생성 직후에는 **아직 저장되지 않는다.** `SETUP_CONFIRM` 이 와야 확정된다.

### `0x11 SETUP_CONFIRM` — 백업 확인 후 확정

```
요청  0     COUNT       u8
      1..   WORD_IDX    big-endian u16 x COUNT
      ..    PASSPHRASE  UTF-8, 0~64바이트 (선택)
응답  0..19  ADDRESS   m/44'/60'/0'/0/0 의 주소
```

기기가 들고 있던 임시 니모닉과 일치해야 저장된다. 다르면 `0x6A80`.
패스프레이즈는 **저장되지 않는다.** 이번 세션의 시드에만 반영되며, 다음에
`UNLOCK` 할 때 같은 값을 다시 넣어야 같은 주소가 나온다.

### `0x12 SETUP_RESTORE` — 기존 니모닉 복구

```
요청  0     COUNT       u8   12 / 15 / 18 / 21 / 24
      1..   WORD_IDX    big-endian u16 x COUNT
      ..    PASSPHRASE  UTF-8, 0~64바이트 (선택)
응답  0..19  ADDRESS
```

BIP-39 체크섬을 검증한다. 틀리면 `0x6A80`.

### `0x13 WIPE`

챌린지 승인이 필요하다. `0x9001 PENDING` 을 반환하고 결과는 `0xA4 REQUEST_RESULT`
이벤트로 온다.

---

### `0x14 SET_PIN` — PIN 설정 / 변경

PIN 은 **버튼 4개의 조합**이다. 각 자리는 버튼 번호 `0..3`, 길이는 4~8 자리.

```
요청  0    LEN   u8   4..8
      1..  PIN   u8 x LEN   각 값 0..3
응답  0..1  STATUS = 0x9001 PENDING
      2..5  REQUEST_ID
```

기기는 임의 챌린지를 띄우고, 사용자가 **기기 버튼으로 승인**해야 실제로 바뀐다.
웹에서 PIN 을 보내는 것만으로는 바뀌지 않는다. 결과는 `0xA4 REQUEST_RESULT`.

- 지갑이 없으면 `0x6982 NOT_INITIALIZED`.
- 잠겨 있으면 `0x5515 LOCKED`. 먼저 `UNLOCK` 해야 한다.
- `LEN == 0` 이면 PIN 을 제거한다 (승인 절차는 동일).
- PIN 이 바뀌면 저장된 니모닉을 새 PIN 으로 다시 암호화한다.

> PIN 이 BLE 로 평문 전송된다. 니모닉과 같은 이유다 — 기기에 입력 UI 가 없다.
> 잠금 해제할 때의 PIN 입력은 **기기 버튼으로만** 받으며 BLE 로 나가지 않는다.
> `SECURITY.md` §3 참고.

### `0x15 UNLOCK` — 잠금 해제

```
요청  0..   PASSPHRASE  UTF-8, 0~64바이트 (BIP-39 패스프레이즈, 없으면 빈 페이로드)
응답  PIN 이 설정된 경우   0..1 STATUS = 0x9001 PENDING
                          2..5 REQUEST_ID
      PIN 이 없는 경우     0..1 STATUS = 0x9000, 0..19 ADDRESS (기본 경로)
```

PIN 이 설정되어 있으면 기기가 사용자에게 **PIN 입력**을 요구한다. 이때
`0xA0 CHALLENGE_STARTED` 의 `STEPS` 는 PIN 길이이고, **LED 는 시퀀스를 보여주지
않는다** — 사용자가 외우고 있는 값이기 때문이다. 서명 챌린지와 반대다.

- 남은 시도 횟수는 **플래시에 저장된다.** 전원을 껐다 켜도 되살아나지 않는다.
- 5회 틀리면 `0x6502 CHALLENGE_FAILED` 를 내고 **지갑 레코드를 지운다.** 그 뒤에는
  `GET_STATE` 가 미초기화로 보이고, 복구 문구로 다시 만들어야 한다.
- 패스프레이즈는 **저장되지 않는다.** 잠금이 풀려 있는 동안만 RAM 에 있는 시드에
  반영된다. 패스프레이즈가 다르면 다른 지갑이 되며, 기기는 그것을 구분하지 못한다.

### `0x16 LOCK` — 잠금

```
요청 없음
응답 0x9000
```

RAM 의 시드를 지운다. BLE 연결이 끊겨도 자동으로 잠긴다.

---

### `0x20 GET_ADDRESS`

```
요청  CHAIN_PATH        (§3.5)
응답  0       ADDR_LEN   u8
      1..     ADDRESS    ADDR_LEN 바이트
      ..      PUBKEY_LEN u8
      ..      PUBKEY
```

| 체인 | ADDRESS | PUBKEY |
|---|---|---|
| Ethereum | 20바이트 (체크섬 없는 원시 바이트) | 65바이트 비압축 `0x04 ‖ X ‖ Y` |
| Solana | 32바이트 (= 공개키. base58 은 SDK 가 입힌다) | 32바이트 |

주소 조회는 챌린지가 필요 없다. 공개 정보다.

> 이전 초안의 `0x21 GET_CHAIN_ADDRESS` 는 **삭제됐다.** 체인 바이트가 `0x20` 에
> 들어가면서 존재 이유가 없어졌다. 다만 시드가 있어야 하므로
**잠금은 풀려 있어야 한다** — 잠겨 있으면 `0x5515 LOCKED`.

### `0x21 GET_CHAIN_ADDRESS` — 멀티체인 주소

```
요청  0      CHAIN   u8   0x01 Ethereum, 0x02 Solana
      1      DEPTH   u8
      2..    PATH    big-endian u32 x DEPTH
응답 Ethereum  20바이트 주소
     Solana    32바이트 Ed25519 공개키 (표시는 base58 인코딩)
```

개인키와 chain code는 반환하지 않는다. Ethereum은 BIP-32/secp256k1,
Solana는 SLIP-0010/Ed25519를 사용한다. Solana 경로의 모든 단계는 hardened이며
기본 경로는 `m/44'/501'/0'/0'`이다.

---

### `0x30 SIGN_TX` — Ethereum 트랜잭션

```
요청  CHAIN_PATH        (§3.5)
      ..   PAYLOAD      체인별 서명 대상 원문
응답  0..1  STATUS = 0x9001 PENDING
      2..5  REQUEST_ID  big-endian u32
```

| 체인 | PAYLOAD | 기기가 하는 일 |
|---|---|---|
| Ethereum | 서명 전 트랜잭션 RLP 전체<br>legacy: `RLP([nonce,gasPrice,gas,to,value,data,chainId,0,0])`<br>typed: `0x02 ‖ RLP([...])` | `keccak256(PAYLOAD)` 후 secp256k1 서명 |
| Solana | 직렬화된 메시지 전체 | ed25519 로 메시지에 직접 서명 (사전 해시 없음) |

**어느 체인이든 기기가 해시를 직접 계산한다.** 호스트가 준 해시를 그대로 서명하지
않는다 — 그러면 기기가 무엇이든 서명하게 된다.

> 이전 초안의 `0x33 SIGN_SOLANA` 는 **삭제됐다.** `0x30` 이 체인 바이트로 갈린다.

### `0x31 SIGN_PERSONAL` — `personal_sign` (EIP-191)  · **Ethereum 전용**

```
요청  CHAIN_PATH        (CHAIN 은 0x01 이어야 한다)
      ..   MESSAGE      원문 바이트
```

체인 바이트가 `0x01` 이 아니면 `0x6A81 UNSUPPORTED_CHAIN`.

기기가 `"\x19Ethereum Signed Message:\n" ‖ len ‖ message` 를 만들어 해시한다.

### `0x32 SIGN_TYPED` — EIP-712  · **Ethereum 전용**

```
요청  CHAIN_PATH        (CHAIN 은 0x01 이어야 한다)
      ..    DOMAIN_SEPARATOR  32바이트
      ..    MESSAGE_HASH      32바이트
```

`"\x19\x01" ‖ domainSeparator ‖ messageHash` 를 해시한다.

> EIP-712 구조체 인코딩은 SDK 가 한다. 기기는 두 해시만 받는다. 기기에 화면이
> 없어 어차피 내용을 보여줄 수 없으므로 온디바이스 파싱의 보안 이득이 없다.
> `SECURITY.md` 의 "블라인드 서명" 절 참고.

### `0x33 SIGN_SOLANA` — Solana 직렬화 메시지

```
요청  0    DEPTH
      1..  PATH       모든 단계 hardened
      ..   MESSAGE    Solana 트랜잭션의 직렬화된 message 바이트
응답  STATUS = 0x9001, REQUEST_ID
결과  Ed25519 detached signature 64바이트
```

기기는 받은 message 원문에 Ed25519 서명한다. Ethereum과 동일하게 4버튼 순서
승인을 통과해야 하며 개인키는 응답하지 않는다.

### `0x40 GET_RESULT`

```
요청  0..3  REQUEST_ID  big-endian u32
응답  0..1  STATUS      해당 요청의 최종 상태
      2..   PAYLOAD     완료된 경우 서명
```

이벤트를 놓쳤을 때의 복구 경로다.

### `0x41 CANCEL`

```
요청  0..3  REQUEST_ID
응답  0x9000
```

---

## 6. 이벤트

### `0xA0 CHALLENGE_STARTED`

```
0..3   REQUEST_ID  big-endian u32
4      STEPS       u8   버튼을 눌러야 하는 횟수
5      CMD         u8   승인 대상 명령
```

> **시퀀스 자체는 절대 보내지 않는다.** 기기 LED 로만 표시한다. 보내는 순간
> 악성 DApp 이 그대로 읽어 사용자를 속일 수 있다.

### `0xA1 CHALLENGE_PROGRESS`

```
0..3   REQUEST_ID
4      STEP        u8   지금까지 맞게 누른 횟수
5      ATTEMPTS    u8   남은 시도
```

### `0xA2 SIGN_RESULT`

```
0..3   REQUEST_ID
4..5   STATUS      0x9000 이면 성공
6      SIG_LEN     u8   서명 바이트 수
7..    SIGNATURE
```

| 체인 | SIG_LEN | SIGNATURE |
|---|---|---|
| Ethereum | 65 | `R(32) ‖ S(32) ‖ RECID(1)` — S 는 EIP-2 low-s |
| Solana | 64 | ed25519 서명 |

실패면 페이로드가 6바이트(REQUEST_ID + STATUS)뿐이다.

`SIGN_SOLANA` 성공 이벤트도 동일하게 `SIG_LEN(1)`을 포함하며 총 71바이트다.
Solana 서명에는 recovery id가 없다.

**`v` 는 기기가 계산해서 보낸다.** micro-ecc 의 `EccPoint_mult` 가 이미 R = k·G 를
구해 두므로 y 좌표의 패리티만 꺼내면 된다 (`micro-ecc/uECC.c` 의 NuWallet 패치).
SDK 에서 `ecrecover` 를 돌릴 필요가 없어져 SDK 가 EC 라이브러리에 의존하지 않는다.

Ethereum 의 `v` 는 `RECID` 로부터 만든다.

| 용도 | 계산 |
|---|---|
| legacy (EIP-155) | `v = RECID + chainId*2 + 35` |
| typed (EIP-1559 등) | `v = RECID` (yParity) |
| `personal_sign` / EIP-712 | `v = RECID + 27` |

`s` 는 기기가 EIP-2 low-s 로 정규화하며, 그때 `RECID` 의 bit0 도 함께 뒤집는다.

### `0xA4 REQUEST_RESULT`

서명이 아닌 승인 요청(`WIPE`, `SET_PIN`, `UNLOCK`)의 결과.

```
0..3   REQUEST_ID
4      CMD         u8   승인 대상 명령
5..6   STATUS      big-endian u16
```

### `0xA3 DEVICE_STATE`

```
0      FLAGS   u8   GET_STATE 와 동일
```

## 7. 서명 승인 절차

```
DApp                    SDK                  기기                  사용자
 |  signTransaction()    |                    |                     |
 |---------------------->|  SIGN_TX           |                     |
 |                       |------------------->|                     |
 |                       |  0x9001 + reqId    |                     |
 |                       |<-------------------|                     |
 |                       |                    |  1→2→3→4 승인 준비  |
 |                       |  EVT CHALLENGE_STARTED (steps=4)         |
 |                       |<-------------------|                     |
 |  onChallenge(4)       |                    |  LED 로 순서 표시    |
 |<----------------------|                    |-------------------->|
 |                       |                    |  버튼 입력           |
 |                       |  EVT PROGRESS      |<--------------------|
 |                       |<-------------------|                     |
 |                       |  EVT SIGN_RESULT   |                     |
 |                       |<-------------------|                     |
 |  r,s,v                |                    |                     |
 |<----------------------|                    |                     |
```

- 챌린지 제한 시간 **60초**. 넘으면 `0x6501`.
- 시퀀스 불일치 시 처음부터 다시. **3회 실패하면 요청 폐기** (`0x6502`).
- 동시에 처리하는 요청은 **1개**. 진행 중에 새 요청이 오면 `0x6985`.

## 8. 잠금 · PIN · 챌린지

버튼 4개가 두 가지 역할을 한다. **서로 다른 것이므로 섞지 않는다.**

| | PIN 입력 | 서명 챌린지 |
|---|---|---|
| 언제 | `UNLOCK` | `SIGN_*`, `WIPE`, `SET_PIN` |
| 값 | 사용자가 정한 고정 시퀀스 | 서명은 `1 → 2 → 3 → 4`, 관리 작업은 임의 순서 |
| LED | **표시하지 않는다** (사용자가 아는 값) | 시퀀스를 표시한다 |
| 목적 | 기기 도난 시 사용 차단 | 사람이 물리적으로 여기 있다는 증명 |
| 실패 허용 | 5회 → 지갑 삭제 (횟수는 플래시에 남는다) | 3회 → 요청 폐기 |

서명의 고정 승인 순서는 입력 편의성을 우선한 프로토타입 정책이다. 임의 순서를 LED에서
확인하는 방식보다 사용자 관여 확인 강도가 낮으므로 상용 지갑의 승인 방식으로
간주해서는 안 된다. WIPE와 PIN 변경은 임의 챌린지를 유지한다.

- PIN 이 설정되어 있으면 부팅 직후와 BLE 연결 해제 후 **자동으로 잠긴다.**
- PIN 이 없으면 `initialized` 상태에서 항상 잠금 해제 상태다. 이때도 서명에는
  챌린지가 필요하다.
- 저장된 니모닉은 PIN 에서 파생한 키로 암호화되어 있다. 상세와 한계는
  `SECURITY.md` §3 참고.

## 9. 상태 기계

```
                 SETUP_GENERATE → SETUP_CONFIRM
   uninitialized ─────────────────────────────→ unlocked
        ↑        SETUP_RESTORE                    │  │
        │                                         │  │ LOCK / 연결 해제
        │ WIPE (챌린지 승인)                       │  ↓
        └─────────────────────────────────────────┴ locked
                                                  ↑  │
                                                  └──┘ UNLOCK (PIN 챌린지)
```

## 10. 구현 현황

세 곳이 같은 문서를 구현한다. 어긋나면 `sdk/test/conformance.test.js` 가 잡는다 —
가짜 기기가 아니라 **보드에 올라갈 펌웨어 코어를 그대로 컴파일해서** SDK 를 물린다.

```sh
python3 scripts/check-protocol.py   # 명령 번호·상태 코드·UUID 가 세 곳에서 같은지
cd sdk && npm test                  # 펌웨어 코어를 빌드해 실제로 주고받아 본다
```

| 명령 | 펌웨어 | SDK | 비고 |
|---|---|---|---|
| `0x01` `0x02` | ✅ | ✅ | |
| `0x10`~`0x12` | ✅ | ✅ | 패스프레이즈는 저장하지 않는다 |
| `0x13` WIPE | ✅ | ✅ | 결과는 `0xA4` |
| `0x14`~`0x16` PIN·잠금 | ✅ | ✅ | `NuWalletAdmin` |
| `0x20` `0x21` 주소 | ✅ Ethereum | ✅ | Solana 체인 바이트는 `0x6A81` |
| `0x30`~`0x32` 서명 | ✅ Ethereum | ✅ | |
| `0x33` SIGN_SOLANA | ❌ **미구현** | ✅ | 기기가 `0x6A81 UNSUPPORTED_CHAIN` 을 돌려준다 |
| `0x40` `0x41` | ✅ | `0x41` 만 사용 | |

**Solana 는 펌웨어에 아직 없다.** ed25519(SLIP-0010) 파생과 서명이 들어가기 전까지
`0x21` 의 `CHAIN=0x02` 와 `0x33` 은 `0x6A81 UNSUPPORTED_CHAIN` 으로 답한다.
`0x6D00 UNKNOWN_CMD` 가 아니다 — 펌웨어가 낡은 것과 체인을 못 다루는 것은
사용자가 취할 조치가 다르다.
