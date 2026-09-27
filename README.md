# NuWallet

NU-40 DK(nRF52840) 보드를 BLE 하드웨어 지갑으로 만드는 오픈소스 프로젝트다.
개인키는 보드 안에서만 쓰이고, 브라우저의 DApp 은 Web Bluetooth 로 보드에 서명을
요청한다. 사용자는 보드의 버튼을 눌러 승인한다.

펌웨어, 브라우저용 SDK, 지갑 설정 웹, 예제 DApp 이 한 저장소에 있다.

> [!WARNING]
> **실제 자금을 넣지 마세요.** 학습과 실험을 위한 프로토타입입니다. nRF52840 에는
> 보안 요소가 없어 보드를 손에 넣은 사람은 키를 꺼낼 수 있고, 화면이 없어 무엇에
> 서명하는지 보드가 보여 주지 못합니다. 기본 네트워크는 테스트넷(Base Sepolia,
> Solana Testnet)입니다. 쓰기 전에 [`SECURITY.md`](SECURITY.md) 를 읽어 주세요.

## 목차

- [무엇을 하나](#무엇을-하나)
- [어떻게 동작하나](#어떻게-동작하나)
- [저장소 구조](#저장소-구조)
- [시작하기](#시작하기)
- [SDK 사용 예](#sdk-사용-예)
- [함께 쓰는 프로젝트: WalletPet](#함께-쓰는-프로젝트-walletpet)
- [테스트](#테스트)
- [문서](#문서)
- [알려진 한계](#알려진-한계)
- [기여하기](#기여하기)
- [라이선스](#라이선스)
- [감사의 말](#감사의-말)

## 무엇을 하나

- **Ethereum 계열 서명.** 트랜잭션(legacy, EIP-1559), `personal_sign`(EIP-191),
  EIP-712 구조체 서명을 한다. 보드는 해시가 아니라 직렬화된 원문을 받아 직접
  Keccak 한다. RLP 로 해석되지 않는 데이터에는 서명하지 않는다.
- **Solana 서명.** Ed25519 로 서명한다. Arduino 포트에서만 된다 (아래 한계 참고).
- **표준 DApp 인터페이스.** SDK 가 EIP-1193 provider 를 내주고, EIP-6963 으로 지갑
  선택기에 자신을 알린다. `window.ethereum` 을 쓰던 DApp 이 그대로 붙는다.
- **보드에서만 입력하는 PIN.** 버튼 4개로 6자리 PIN 을 누른다. PIN 은 BLE 로
  나가지 않는다. 서명할 때마다 보드가 LED 하나를 켜고, 사용자가 그 옆 버튼을 눌러야
  서명이 나간다.
- **표준 키 파생.** BIP-39 니모닉, BIP-32(secp256k1), SLIP-0010(Ed25519).
- **공장 초기화.** PIN 을 잊으면 버튼 조합으로 지갑과 BLE 본딩을 지운다.

## 어떻게 동작하나

```
   ┌──────────────┐                    ┌──────────────┐
   │ 설정 웹       │   Web Bluetooth    │              │   버튼 4개
   │ 예제 DApp     │◄──────────────────►│   펌웨어      │◄──────────── 사용자
   │ (브라우저)    │  @nucode/hw-wallet │  (nRF52840)  │   LED 4개
   └──────────────┘        SDK         └──────────────┘
                                              │
                                     개인키는 여기서 나가지 않는다
```

브라우저와 보드는 BLE GATT 서비스 하나로 이야기한다. 요청과 응답은 패킷으로
쪼개져 오가고, 서명처럼 사람의 승인이 필요한 요청은 보드가 버튼 입력을 기다린 뒤
결과를 알림으로 보낸다. 바이트 단위 규격은 [`docs/protocol.md`](docs/protocol.md)
하나에만 있고, 펌웨어와 SDK 는 그 문서를 그대로 따른다.

지갑 로직(프로토콜, 승인, 저장, 서명)은 펌웨어 **코어** 한 곳에 있다. 코어는
보드도 운영체제도 모르고, 포트가 채워 주는 작은 HAL 만 안다. 그래서 같은 코어가
세 곳에서 컴파일된다.

| 포트 | 위치 | 쓰임 |
|---|---|---|
| Arduino | `firmware/nuwallet/` | 보드에 올려 쓰는 기본 펌웨어. Solana 까지 된다 |
| Zephyr | `firmware/wallet/` | 두 번째 보드 포트. Ethereum 만 된다 |
| 호스트 | `firmware/wallet/src/port/host/` | PC 에서 코어를 테스트할 때 쓴다 |

## 저장소 구조

```
firmware/
  nuwallet/          Arduino 스케치와 공유 코어
    src/core/          지갑 코어 (프로토콜 · 승인 · 저장 · 서명)
    src/crypto/        SHA-2 · HMAC · PBKDF2 · Keccak · BIP-39 · BIP-32 · SLIP-0010
    src/micro-ecc/     secp256k1 (micro-ecc, 복구 id 를 꺼내려고 패치했다)
  wallet/            Zephyr 포트와 west 매니페스트
  test/              호스트 테스트와 SDK 적합성 테스트용 브리지
sdk/                 @nucode/hw-wallet — Web Bluetooth SDK (TypeScript, 런타임 의존성 없음)
app/                 웹 앱 (지갑 만들기 /, 설정 /setup, 예제 DApp /dapp, BLE 디버그 /debug)
docs/                프로토콜, 구조, 설치, EIP 지원, 작업 목록
scripts/             펌웨어 빌드, 프로토콜 대조, 의존 그래프 검사
```

`firmware/arduino/`, `firmware/src/`, `firmware/CMakeLists.txt` 는 이 프로젝트 이전의
NU-40 데모(다른 BLE 서비스를 쓰는 펫 데모)다. 지갑과는 관계없다.

## 시작하기

### 필요한 것

- NU-40 DK 보드와 USB 케이블
- Node.js 22.13 이상
- Chrome 또는 Edge 계열 데스크톱 브라우저. Web Bluetooth 는 HTTPS 나 `localhost`
  에서만 된다.
- 펌웨어를 빌드하려면 `arduino-cli` 와 제조사 보드 코어, `adafruit-nrfutil`.
  설치 방법은 [`docs/nu40-dk-firmware-installation.md`](docs/nu40-dk-firmware-installation.md) 에 있다.

### 1. 펌웨어를 빌드하고 보드에 올린다

빌드 산출물은 저장소에 없다. 소스에서 만든다.

```sh
scripts/build-firmware.sh                        # firmware/nuwallet/build/NUWALLET.UF2
scripts/build-firmware.sh /dev/cu.usbmodem1101   # 빌드하고 시리얼 DFU 로 바로 올린다
```

UF2 로 올릴 수도 있다. 보드의 RESET 을 빠르게 두 번 누르면 USB 드라이브가 뜨고,
`NUWALLET.UF2` 를 그 드라이브에 복사하면 된다.

> 펌웨어를 다시 올렸거나 공장 초기화를 했다면 OS 블루투스 설정에서 기존
> `NuWallet-…` 기기를 먼저 삭제한다. 보드와 PC 중 한쪽만 페어링 키를 기억하면
> 연결되지 않는다.

Zephyr 포트는 같은 문서의 "Zephyr 로 빌드하기" 를 본다. Zephyr 를 올리면 Arduino 가
쓰는 SoftDevice 자리를 덮어쓰므로 거기 적힌 주의부터 읽는다.

### 2. 웹 앱을 실행한다

```sh
npm install
npm run dev
```

터미널에 나오는 주소를 Chrome 으로 연다.

1. `/` 에서 보드를 연결하고 지갑을 만든다. 니모닉을 적어 두고, 보드에서 PIN
   6자리를 두 번 누른다.
2. `/dapp` 에서 테스트넷 트랜잭션과 메시지 서명을 해 본다. 서명할 때 보드에 켜진
   LED 옆의 버튼을 누른다.

`/setup` 에서는 기존 니모닉 가져오기, PIN 변경, 잠금과 초기화를 한다. `/debug` 는
BLE 로 오가는 패킷을 그대로 보여 준다. 연결이 안 되면 [`docs/BLUETOOTH.md`](docs/BLUETOOTH.md) 를 본다.

## SDK 사용 예

SDK 는 저장소의 `sdk/` 에 있고 런타임 의존성이 없다 (`ethers` 는 선택적 peer
의존성이다). 아직 npm 에 배포하지 않았다.

```js
import { NuWallet, NuWalletProvider, announceNuWalletProvider } from '@nucode/hw-wallet';

const wallet = new NuWallet();
const provider = new NuWalletProvider(wallet, {
  chainId: 84532,                        // Base Sepolia
  rpcUrl: 'https://sepolia.base.org',
});
announceNuWalletProvider(provider);      // EIP-6963 지갑 선택기에 나타난다

const [from] = await provider.request({ method: 'eth_requestAccounts' });
const hash = await provider.request({
  method: 'eth_sendTransaction',
  params: [{ from, to: '0x…', value: '0x2386f26fc10000' }],
});
```

provider 는 체인이 EIP-1559 를 지원하면 type 2 트랜잭션을, 아니면 legacy 를 만든다.
EIP-712, 오류 코드, Solana 어댑터는 [`sdk/README.md`](sdk/README.md) 에 있다.

## 함께 쓰는 프로젝트: WalletPet

[WalletPet (toyton_agent_wallet)](https://github.com/hex-aragon/toyton_agent_wallet) 은
AI 에이전트의 투자·구매 결과를 온체인 펫으로 기록하는 프로젝트다. 결과가 좋으면 펫이
자라고 나쁘면 야윈다. Solana 프로그램과 Base Sepolia 컨트랙트 두 가지로 구현되어 있다.
NuWallet 은 여기서 **물리 버튼으로 승인하는 서명자** 역할을 한다.

WalletPet 의 Base Sepolia 웹 클라이언트는 EIP-6963 으로 지갑을 찾고, NuWallet SDK 는
EIP-6963 으로 자신을 알린다. 체인도 같은 Base Sepolia(chainId 84532)다. 그래서
WalletPet 페이지에서 SDK 를 불러 provider 를 알리기만 하면, WalletPet 의 지갑 목록에
NuWallet 이 나타난다. 펫 만들기, 상태 조회, 트랜잭션 서명이 그대로 NU-40 으로 간다.

```js
import { NuWallet, NuWalletProvider, announceNuWalletProvider } from '@nucode/hw-wallet';

announceNuWalletProvider(new NuWalletProvider(new NuWallet(), {
  chainId: 84532,
  rpcUrl: 'https://sepolia.base.org',
}));
```

WalletPet 은 결과 보고를 펫 주인이 등록한 **에이전트 주소**만 할 수 있게 한다.
NuWallet 의 주소를 에이전트로 등록하면, 에이전트가 결과를 보고할 때마다 보드에서
버튼으로 승인해야 한다. WalletPet 이 로드맵에 적어 둔 "에이전트 서명자를 NU-40
승인으로 바꾸기" 가 이 경로다.

알아 둘 것:

- WalletPet 저장소에 있는 지금의 NU-40 데모는 **다른 펌웨어**를 쓴다. 광고 이름이
  `WalletPet NU40` 이고 Nordic UART 서비스로 메시지를 주고받는다. NuWallet 펌웨어를
  올린 보드는 그 데모의 "보드 연결" 버튼으로는 붙지 않는다. 위의 지갑 목록 경로를 쓴다.
- WalletPet 의 Solana 클라이언트는 Phantom 을 직접 찾는다. NuWallet 의 Solana
  어댑터(`NuWalletSolanaAdapter`)로 서명하려면 그 클라이언트를 고쳐야 한다.
- 두 프로젝트를 실제로 함께 돌려 보지는 않았다. 위 내용은 두 저장소의 코드를 읽고
  인터페이스가 맞물리는 것을 확인한 것이다.

## 테스트

보드 없이 도는 테스트가 기본이다. 저장소 루트에서:

```sh
(cd firmware/test && make test)   # 암호 공식 벡터, 지갑 코어 전 구간
(cd sdk && npm test)              # SDK 단위 테스트와 펌웨어 코어를 상대로 한 적합성 테스트
python3 scripts/check-protocol.py # 명령·상태 코드가 펌웨어, SDK, 문서에서 같은지
node scripts/codegraph.mjs        # 계층 위반과 순환 의존
npm run build                     # 웹 앱 빌드
```

적합성 테스트는 실제 펌웨어 코어를 PC 용으로 컴파일해 자식 프로세스로 띄우고,
SDK 가 그것과 바이트를 주고받게 한다. 프로토콜이 한쪽만 바뀌면 여기서 걸린다.

보드를 연결했다면 실기기 테스트도 돌릴 수 있다. PIN 과 서명 승인을 보드에서 눌러야 한다.

```sh
(cd sdk && npm run test:device:interactive)
```

## 문서

| 문서 | 내용 |
|---|---|
| [`SECURITY.md`](SECURITY.md) | 이 기기가 막지 못하는 것. 먼저 읽는다 |
| [`docs/protocol.md`](docs/protocol.md) | BLE 프로토콜 v2 전체 규격, LED 가 뜻하는 것 |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | 구성 요소와 그 사이의 경계 |
| [`docs/nu40-dk-firmware-installation.md`](docs/nu40-dk-firmware-installation.md) | 개발 환경 준비, 빌드, 보드에 올리기 |
| [`docs/BLUETOOTH.md`](docs/BLUETOOTH.md) | 연결과 페어링 문제 해결 |
| [`docs/EIP.md`](docs/EIP.md) | 지원하는 EIP 와 지원하지 않는 EIP |
| [`docs/TASKS.md`](docs/TASKS.md) | 남은 일과 끝난 일 |
| [`firmware/README.md`](firmware/README.md) | 펌웨어 구성과 포트 |
| [`sdk/README.md`](sdk/README.md) | SDK API |

## 알려진 한계

자세한 내용은 [`SECURITY.md`](SECURITY.md) 와 [`docs/TASKS.md`](docs/TASKS.md) 에 있다.

- **키 추출.** 보안 요소가 없고, SWD 디버그 포트를 잠그지 않는다(개발 편의를 위해
  일부러 켜지 않았다). PIN 은 4⁶ = 4,096 가지라 플래시를 덤프하면 금방 풀린다.
- **블라인드 서명.** 화면이 없어 보드가 서명 내용을 보여 주지 못한다. 버튼 승인은
  사람이 보드 앞에 있다는 것만 증명한다. EIP-712 는 보드가 해시 두 개만 받는다.
- **니모닉이 BLE 로 오간다.** 지갑을 만들거나 가져올 때 PC 가 니모닉 전체를 본다.
- **부트로더가 서명을 검증하지 않는다.** 누구나 다른 펌웨어를 올릴 수 있다.
- **Zephyr 포트에는 Solana 가 없다.** Ed25519 를 CryptoCell 하드웨어로 하는 경로가
  Arduino 에만 있다 (`docs/TASKS.md` T22).

## 기여하기

이슈와 풀 리퀘스트를 환영한다. 풀 리퀘스트를 열기 전에 다음을 지켜 주면 좋다.

- 위 [테스트](#테스트)의 다섯 명령이 모두 통과해야 한다.
- 프로토콜을 바꾸면 `docs/protocol.md`, 펌웨어 `core/protocol.h`, SDK
  `sdk/src/constants.ts` 세 곳을 같이 바꾼다. `check-protocol.py` 가 어긋남을 잡는다.
- 펌웨어 동작을 바꾸면 `firmware/test` 에 테스트를 더한다. 보드에서만 확인할 수 있는
  것은 풀 리퀘스트에 무엇을 실기기에서 확인했는지 적는다.
- 커밋 메시지는 [Conventional Commits](https://www.conventionalcommits.org/) 형식을 따른다
  (`feat:`, `fix:`, `docs:` …).

보안 문제를 찾았다면 공개 이슈로 올리기 전에 저장소 관리자에게 먼저 알려 주세요.

## 라이선스

[MIT](LICENSE). 포함된 서드파티 코드는 각자의 라이선스를 따른다.

- `firmware/nuwallet/src/micro-ecc/` — BSD 2-Clause, Kenneth MacKay
  ([`LICENSE.txt`](firmware/nuwallet/src/micro-ecc/LICENSE.txt))

## 감사의 말

- [micro-ecc](https://github.com/kmackay/micro-ecc) — secp256k1 서명
- [Adafruit nRF52 Arduino 코어와 Bluefruit](https://github.com/adafruit/Adafruit_nRF52_Arduino),
  [Adafruit nRF52 부트로더](https://github.com/adafruit/Adafruit_nRF52_Bootloader)
- [Zephyr RTOS](https://www.zephyrproject.org/) — NU-40 보드 정의(`nucode_nu40`)가 업스트림에 있다
