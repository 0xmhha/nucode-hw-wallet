# NuWallet BLE 프로토콜 v2

펌웨어 · SDK · 웹이 모두 이 문서를 구현한다. **한쪽만 바꾸면 안 된다.**
`python3 scripts/check-protocol.py` 가 세 곳의 상수가 같은지 확인한다.

> **v1 과 호환되지 않는다.** 바뀐 것은 셋이다.
> 1. **셋업에 PIN 이 필수**가 됐고, PIN 값은 BLE 로 오가지 않는다 (기기 버튼으로만).
>    v1 은 PIN 없이 저장했는데, 그 레코드는 봉인 키가 `PBKDF2("", salt)` 이고
>    salt 가 평문이라 플래시만 뜨면 열렸다.
> 2. **서명 승인이 랜덤 시퀀스에서 "버튼 1회"로** 바뀌었다 (PIN 으로 연 세션 안에서).
>    잃은 것이 있다 — §8 참고.
> 3. Solana(Ed25519)를 지원한다.

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
| `0x6502` | CHALLENGE_FAILED | 승인 3회 실패, 또는 PIN 시도 소진 |
| `0x6503` | PIN_REQUIRED | PIN 이 설정되어 있고 아직 잠김 |
| `0x6504` | PIN_MISMATCH | PIN 설정 시 두 번 입력한 값이 다름 |
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
응답  0..1  STATUS = 0x9001 PENDING
      2..5  REQUEST_ID
```

기기가 들고 있던 임시 니모닉과 일치해야 한다. 다르면 `0x6A80`.

**여기서 바로 저장되지 않는다.** 기기가 PIN 설정 절차(`KIND=2`)를 띄우고,
사용자가 버튼으로 6자리를 두 번 누른 뒤에야 봉인·저장된다. 결과는
`0xA4 REQUEST_RESULT`. 끝나면 지갑은 잠금 해제 상태이고, 주소는 이어서
`0x21` 로 조회한다.

> v1 은 여기서 PIN 없이 저장했다. 그 레코드는 봉인 키가 `PBKDF2("", salt)` 이고
> salt 가 레코드에 평문으로 들어 있어서, 플래시를 뜨면 그냥 열렸다.

패스프레이즈는 **저장되지 않는다.** 이번 세션의 시드에만 반영되며, 다음에
`UNLOCK` 할 때 같은 값을 다시 넣어야 같은 주소가 나온다.

### `0x12 SETUP_RESTORE` — 기존 니모닉 복구

```
요청  0     COUNT       u8   12 / 15 / 18 / 21 / 24
      1..   WORD_IDX    big-endian u16 x COUNT
      ..    PASSPHRASE  UTF-8, 0~64바이트 (선택)
응답  0..1  STATUS = 0x9001 PENDING
      2..5  REQUEST_ID
```

BIP-39 체크섬을 검증한다. 틀리면 `0x6A80`.
`SETUP_CONFIRM` 과 마찬가지로 PIN 설정 절차를 거쳐야 저장된다.

### `0x13 WIPE`

승인이 필요하다. `0x9001 PENDING` 을 반환하고 결과는 `0xA4 REQUEST_RESULT`
이벤트로 온다.

**잠금이 풀린 세션에서만 된다.** 잠긴 상태면 `0x5515 LOCKED`.
PIN 을 잊어서 열 수 없다면 기기의 **공장 초기화**를 쓴다 (§8) — BLE 명령이 아니다.

---

### `0x14 SET_PIN` — PIN 변경

PIN 은 **버튼 4개의 조합**이고 길이는 **6자리 고정**이다. 각 자리는 버튼 번호 `0..3`.

```
요청  (페이로드 없음)
응답  0..1  STATUS = 0x9001 PENDING
      2..5  REQUEST_ID
```

**PIN 값은 BLE 로 오가지 않는다.** 사용자가 기기에서 6자리를 누르고, 오타를 잡기
위해 같은 값을 한 번 더 누른다. 두 입력이 다르면 `0x6504 PIN_MISMATCH`.
결과는 `0xA4 REQUEST_RESULT`.

> v1 은 새 PIN 을 호스트가 실어 보냈다. PIN 의 목적이 "호스트가 감염돼도 기기를
> 못 연다" 이므로, 호스트가 PIN 을 알면 앞뒤가 맞지 않는다.

- 지갑이 없으면 `0x6982 NOT_INITIALIZED`.
- 잠겨 있으면 `0x5515 LOCKED`. 먼저 `UNLOCK` 해야 한다.
- PIN 이 바뀌면 저장된 니모닉을 새 PIN 으로 다시 봉인한다.
- **PIN 은 없앨 수 없다.** PIN 없는 레코드는 봉인 키가 공개값이다.

> 길이가 고정인 이유: "입력 끝" 신호가 필요 없다. 버튼 4개짜리 기기에서 확인
> 버튼을 따로 두는 것은 낭비다. 대신 조합은 4⁶ = **4,096가지**뿐이다 —
> 이것은 암호가 아니라 **기기 접근 제어**다. `SECURITY.md` §3 참고.

### `0x15 UNLOCK` — 잠금 해제

```
요청  0..   PASSPHRASE  UTF-8, 0~64바이트 (BIP-39 패스프레이즈, 없으면 빈 페이로드)
응답  0..1 STATUS = 0x9001 PENDING
      2..5 REQUEST_ID
```

기기가 사용자에게 **PIN 입력**(`KIND=1`)을 요구한다. v2 는 PIN 이 항상 있으므로
즉시 열리는 경우가 없다. `0xA0 CHALLENGE_STARTED` 의 `STEPS` 는 6이고,
**LED 는 값을 보여주지 않는다** — 누른 자릿수만 표시한다.

잠금이 풀리면 세션이 열린다. 그 안에서는 서명마다 PIN 을 다시 받지 않고
**버튼 1회**만 받는다 (§7). 세션은 **5분 유휴** 또는 **연결 해제**에 닫힌다.

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

시드가 있어야 하므로 **잠금은 풀려 있어야 한다** — 잠겨 있으면 `0x5515 LOCKED`.

**chain code 는 반환하지 않는다.** chain code 와 자식 개인키 하나가 있으면 형제
키를 전부 파생할 수 있다. 주소를 물어본 대가로 줄 것이 아니다.

### `0x21 GET_CHAIN_ADDRESS` — 주소만

```
요청  CHAIN_PATH        (§3.5)
응답 Ethereum  20바이트 주소
     Solana    32바이트 Ed25519 공개키 (base58 은 SDK 가 입힌다)
```

`0x20` 과 같지만 공개키를 싣지 않아 응답이 짧다. 연결 직후처럼 자주 부르는
경로에서 쓴다. SDK 의 `getAddress()` 가 이쪽, `getAccount()` 가 `0x20` 이다.

Ethereum 은 BIP-32/secp256k1, Solana 는 SLIP-0010/Ed25519 를 쓴다.
**Solana 경로는 모든 단계가 하드닝이어야 한다** — Ed25519 에는 공개키 파생이
없어서 SLIP-0010 이 하드닝만 정의한다. 기본 경로는 `m/44'/501'/0'/0'`.

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

`0x30` 은 Ethereum 전용이다. 체인 바이트가 `0x01` 이 아니면 `0x6A80`.
Solana 는 `0x33 SIGN_SOLANA` 를 쓴다.

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
요청  CHAIN_PATH        (CHAIN 은 0x02 여야 한다. 모든 단계 하드닝)
      ..   MESSAGE      Solana 트랜잭션의 직렬화된 message 바이트 (최대 1232)
응답  0..1  STATUS = 0x9001 PENDING
      2..5  REQUEST_ID
결과  Ed25519 detached signature 64바이트
```

기기가 받은 원문에 Ed25519 로 직접 서명한다 — **사전 해시가 없다.** Ed25519 가
내부에서 해시하기 때문이다. 그래서 secp256k1 명령과 달리 승인을 기다리는 동안
해시가 아니라 **원문 전체를 들고 있어야 한다** (`NU_MAX_SIGN_PAYLOAD` = 1232,
Solana 트랜잭션 최대 크기). 넘으면 `0x6A84 TOO_LARGE`.

체인 바이트가 `0x02` 가 아니면 `0x6A80`. 승인은 다른 서명과 같다 (§7).

> Ed25519 는 nRF52840 의 CryptoCell 하드웨어에서 돈다. 코어가 아니라 HAL 에
> 있는 유일한 곡선이다 — secp256k1 은 micro-ecc 로 코어가 직접 한다.
> HAL 이 그것을 못 하는 포트에서는 `0x6A81 UNSUPPORTED_CHAIN` 이 나온다.

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
6      KIND        u8   승인 종류 (v2 에서 추가)
```

| KIND | 이름 | STEPS | 화면이 띄워야 할 것 |
|---|---|---|---|
| `0` | CONFIRM | 1 | "보드에서 **켜진 LED** 의 버튼을 누르세요" |
| `1` | PIN | 6 | "보드에서 **PIN 6자리**를 누르세요" |
| `2` | PIN_NEW | 6 | "새 PIN 6자리를 누르고, 같은 값을 한 번 더 누르세요" |

> KIND 가 없던 v1 에서는 호스트가 명령 번호로 추측했고, 그래서 어긋났다.
> 기기가 알려주면 추측할 일이 없다.

> **시퀀스도 PIN 도 절대 보내지 않는다.** 기기 LED 로만 표시한다. 보내는 순간
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
성공은 Ethereum 72바이트, Solana 71바이트다 — Solana 서명에는 recovery id 가 없다.
**길이 접두사가 있으므로 호스트는 고정 오프셋으로 읽으면 안 된다.**

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

서명이 아닌 승인 요청(`SETUP_CONFIRM`, `SETUP_RESTORE`, `WIPE`, `SET_PIN`, `UNLOCK`)의 결과.

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
 |                       |                    |  LED 하나를 무작위로 |
 |                       |  EVT CHALLENGE_STARTED (KIND=0, steps=1) |
 |                       |<-------------------|                     |
 |  onStart(CONFIRM)     |                    |  그 LED 를 켠다      |
 |<----------------------|                    |-------------------->|
 |                       |                    |  그 버튼을 누른다    |
 |                       |  EVT PROGRESS      |<--------------------|
 |                       |<-------------------|                     |
 |                       |  EVT SIGN_RESULT   |                     |
 |                       |<-------------------|                     |
 |  r,s,v                |                    |                     |
 |<----------------------|                    |                     |
```

- **잠금이 풀린 세션이어야 한다.** 잠겨 있으면 `0x5515 LOCKED` — 먼저 `UNLOCK`.
- 제한 시간 **60초**. 넘으면 `0x6501`.
- 틀린 버튼을 누르면 LED 를 다시 켜고 다시 받는다. **3회 실패하면 요청 폐기** (`0x6502`).
- 동시에 처리하는 요청은 **1개**. 진행 중에 새 요청이 오면 `0x6985`.
- 세션은 **5분 유휴** 또는 **연결 해제**에 닫힌다. 닫히면 다시 `UNLOCK` 해야 한다.

왜 버튼 한 번인가: PIN 으로 세션을 한 번 열고, 서명마다는 값싼 확인만 받는다.
서명마다 PIN 6자리를 누르게 하면 어깨너머 노출이 서명 횟수만큼 늘어난다.
어느 LED 가 켜질지는 매번 무작위다 — 고정하면 기기를 보지 않고도 누를 수 있어서,
감염된 호스트가 "아무 버튼이나 누르세요" 라고 유도해도 사용자가 구분하지 못한다.

## 8. 잠금 · PIN · 승인 · 공장 초기화

버튼 4개가 세 가지 역할을 한다. **서로 다른 것이므로 섞지 않는다.**

| | PIN 입력 (KIND=1) | PIN 설정 (KIND=2) | 서명 확인 (KIND=0) |
|---|---|---|---|
| 언제 | `UNLOCK` | 셋업, `SET_PIN` | `SIGN_*`, `WIPE` |
| 누르는 횟수 | 6 | 6 + 재입력 6 | 1 |
| 값 | 사용자가 아는 고정 값 | 사용자가 정한다 | 기기가 무작위로 고른 LED |
| LED | 누른 **개수만** | 누른 **개수만** | 눌러야 할 버튼 하나 |
| 목적 | 기기 도난 시 사용 차단 | 오타 방지 (화면이 없다) | 사람이 지금 여기 있다는 증명 |
| 실패 허용 | 5회 → **지갑 삭제** (횟수는 플래시에 남는다) | 3회 → `0x6504` | 3회 → 요청 폐기 |

- PIN 은 **6자리 고정**이고 셋업에서 **반드시** 정한다. 없앨 수 없다.
- 부팅 직후와 BLE 연결 해제 후 **자동으로 잠긴다.** 5분 유휴에도 잠긴다.
- 저장된 니모닉은 PIN 에서 파생한 키로 봉인되어 있다. 상세와 한계는 `SECURITY.md` §3.

> **v1 에서 무엇을 잃었는지.** v1 은 서명마다 기기가 새로 뽑은 4자리 랜덤 시퀀스를
> LED 로 보여주고 그대로 누르게 했다. 그것은 "사용자가 **기기를 보고 있다**" 를
> 증명했다. v2 의 버튼 한 번은 "사람이 여기 있다" 까지만 증명한다. 켜진 LED 를
> 누르게 해서 최소한의 주시는 남겼지만, 같은 강도가 아니다. 사용성을 위해
> 의도적으로 내린 결정이다 — 상용 지갑의 승인 방식으로 간주하면 안 된다.

### 공장 초기화

**BLE 명령이 아니다.** PIN 을 잊으면 `0x13 WIPE` 를 쓸 수 없다 (잠금 해제가 필요하다).
그때 보드를 되살리는 유일한 길이다.

```
1) 버튼 1 과 4 를 함께 5초 동안 누른다   → LED 4개가 하나씩 꺼지며 카운트다운
2) 손을 뗀 뒤 버튼 2 → 3 을 누른다        → 지운다
```

인증을 요구하지 않아도 안전하다 — 공장 초기화는 잠금을 우회하는 게 아니라 시드를
**파괴**한다. 자금을 훔치는 경로가 되지 않는다. 2단계인 이유는 주머니에서 눌리는
것을 막기 위해서다. 한 번의 홀드로 지우면 사고가 난다. 되돌릴 수 없다.

**BLE 본딩 키도 함께 지운다.** 시드만 지우고 본딩을 남기면 보드는 초기화됐는데
호스트는 옛 페어링 키를 계속 써서, 다음 연결이 알림 구독 단계에서 끊긴다.
사용자는 원인을 찾을 수 없다.

> 그 대가로 **호스트도 페어링을 지워야 한다.** 공장 초기화 뒤 다시 연결하려 하면
> macOS 는 `Peer removed pairing information` 으로 거절한다 — 보드는 잊었는데
> 호스트는 기억하고 있기 때문이다. 시스템 설정 → Bluetooth → 해당 기기 → i →
> "이 기기 잊기" 를 한 뒤 다시 연결한다. 실기기 검증에서 확인된 동작이다.

### LED

| 상황 | LED |
|---|---|
| 지갑 없음 | LED1 느린 점멸 — "셋업 필요" |
| 잠김 | 전부 꺼짐 |
| 잠금 해제됨 | LED4 점등 |
| PIN 입력·설정 중 | 누른 개수 (5·6번째는 순환) |
| 서명 확인 대기 | 눌러야 할 LED 하나 |
| 공장 초기화 | 카운트다운 4→3→2→1, 확인 대기 중에는 4개 빠른 점멸 |

## 9. 상태 기계

```
              SETUP_CONFIRM / SETUP_RESTORE
                    + PIN 설정 (KIND=2)
   uninitialized ───────────────────────────────→ unlocked
        ↑                                          │  │
        │  WIPE (확인 승인, 잠금 해제 상태에서만)   │  │ LOCK / 연결 해제 / 5분 유휴
        │  공장 초기화 (버튼, 언제든)               │  ↓
        └──────────────────────────────────────────┴ locked
                                                   ↑  │
                                                   └──┘ UNLOCK (PIN 6자리)
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
| `0x10`~`0x12` 셋업 | ✅ | ✅ | PIN 설정을 거쳐야 저장된다. 패스프레이즈는 저장하지 않는다 |
| `0x13` WIPE | ✅ | ✅ | 잠금 해제 상태에서만. 결과는 `0xA4` |
| `0x14` SET_PIN | ✅ | ✅ | 페이로드 없음. 값은 기기 버튼으로만 |
| `0x15` `0x16` 잠금 | ✅ | ✅ | `NuWalletAdmin` |
| `0x20` `0x21` 주소 | ✅ Ethereum · Solana | ✅ | |
| `0x30`~`0x32` 서명 | ✅ Ethereum | ✅ | |
| `0x33` SIGN_SOLANA | ✅ | ✅ | HAL 이 Ed25519 를 못 하면 `0x6A81` |
| `0x40` `0x41` | ✅ | `0x41` 만 사용 | |

펌웨어는 **한 벌**이다 (`firmware/nuwallet/src/core/`). Arduino 와 Zephyr 는 포트만
다르고 같은 코어를 컴파일한다 — 프로토콜 로직이 갈라질 수 없다. `firmware/README.md` 참고.

### 검증되지 않는 것

`sdk/test/conformance.test.js` 는 호스트에서 돈다. **Ed25519 만 대역(가짜)이다** —
CryptoCell 하드웨어가 호스트에 없기 때문이다. 그래서 Solana 는 프로토콜 흐름과
SLIP-0010 파생까지만 검증되고, **서명 값의 정확성은 실기기로만 확인할 수 있다.**
Ethereum 은 공유 암호 스택을 그대로 쓰므로 공식 벡터와 대조된다.
