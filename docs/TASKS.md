# 작업 리스트 — 미구현 · 오류 · 정리

전부 코드에서 확인한 것만 적는다. 추측은 "확인 필요" 로 표시했다.
검증 도구: `node scripts/codegraph.mjs`, `python3 scripts/check-protocol.py`,
`cd sdk && npm test`, `cd firmware/test && make test`, `npm run build`.

---

## 남은 일

순서대로 한다. 앞 묶음이 끝나야 뒤 묶음의 결과를 믿을 수 있다.

### 1. 먼저 끝낼 것: 실기기 확인

#### 🔴 T21. PIN 재입력 단계를 실기기에서 끝까지 돌려 보지 못했다
`8e8dc4c` 에서 1차 입력과 재입력의 LED 표시를 나눴다 (1차는 네 개가 같이
깜빡이고, 재입력은 1·3 과 2·4 가 번갈아 깜빡인다). 호스트 테스트 `test_leds`
16건은 통과했지만, 사람이 보드를 보고 두 단계를 구분할 수 있는지는 아직
확인하지 않았다. 직전 실행은 재입력 단계에서 47초간 입력이 없어 시간 초과로
끝났다.

`cd sdk && npm run test:device:interactive` 가 셋업, 주소 골든 벡터, Solana 주소,
personal_sign 골든 벡터, RLP 아닌 서명 거부까지 한 번에 통과하면 닫는다.
같은 실행에서 SDK 가 승인을 기다리며 3초마다 보내는 `GET_RESULT`(T8)가 버튼
입력과 겹쳐도 문제없는지도 본다. 호스트에서는 확인했지만 실제 BLE 에서는 아직이다.
나머지 항목 대부분은 이미 실기기에서 확인했다 (`ETH 주소·서명 바이트 일치`,
`Ed25519 검증`, `공장 초기화`, `연결 해제 시 잠금`).

### 2. 결정이 필요한 것

#### 🟢 T13. 빌드 산출물을 git 에 둘지
지금 추적하는 산출물은 `firmware/nuwallet/build/NUWALLET.UF2`(384KB) 하나다.
설치 문서가 이 파일을 바로 쓰게 안내하므로 편하지만, 빌드할 때마다 바뀌어
커밋마다 384KB 씩 히스토리가 불어난다. GitHub Releases 로 옮기면 이 문제는
없어지고, 대신 문서의 설치 절차가 한 단계 늘어난다.
히스토리에는 예전 elf 3.83MB 와 map 2.14MB 도 남아 있다. 지우려면
`git filter-repo` 로 다시 써야 하고, 그러면 이미 클론한 사람의 히스토리가 갈라진다.

#### 🟢 T14. Zephyr 앱을 살릴지
NU-40 DK 용 Zephyr 보드 정의(`nucode_nu40`)가 저장소에도 업스트림에도 없어서
`firmware/wallet` 은 빌드되지 않는다. 보드에 올라가는 것은 Arduino 스케치다.
코어는 두 포트가 같이 컴파일하므로 Zephyr 쪽 로직이 따로 낡지는 않지만,
Zephyr 포트 고유의 수정(BT RX 스레드 스택을 넘치게 하던 문제)은 실기기에서
확인한 적이 없다. 필요한 보드 값은 `docs/nu40-dk-firmware-installation.md` 에 있다.

### 3. 하드웨어 보안

#### 🟡 T15. `APPROTECT` 를 켠 빌드를 보드에서 확인하지 않았다
`firmware/nuwallet/config.h` 의 `NU_ENABLE_APPROTECT` 로 켤 수 있게 했다. 기본은
0 이다. 켜면 개발 중 SWD 디버깅이 막히므로 릴리스 빌드에서만 켠다.

켠 빌드를 보드에 올려 보지는 않았다. 한 번 켜면 되돌리려면 칩 전체를 지워야
해서, 확인 절차를 정하고 나서 한다. 지금까지 확인한 것은 역어셈블로 UICR
`0x10001208` 에 쓰는 코드가 들어가는지뿐이다. 보드의 칩 리비전도 확인하지 않았다.
리비전에 따라 APPROTECT 를 켜는 방식이 다르다.

켜더라도 PIN 이 약하다는 사실(4⁶ = 4,096 가지, `SECURITY.md` §3-1)과 전압 글리칭으로
우회된다는 사실은 그대로다.

### 4. 고칠 수 없어 기록만 하는 것

#### 🔴 T20. 서명 내용을 사용자가 볼 수 없다
LED 4개로는 "0.5 ETH 를 0xAbC… 로" 를 보여줄 수 없다. 버튼 승인은 사람이
물리적으로 여기 있다는 것만 증명하고, 화면에 뜬 것과 서명되는 것이 같은지는
증명하지 못한다. EIP-712 는 기기가 해시 두 개만 받으므로 특히 그렇다.
`SECURITY.md` §2.

#### 니모닉이 BLE 로 평문으로 오간다
`SETUP_CREATE` 는 기기가 만든 니모닉을 호스트로 보내고, `SETUP_RESTORE` 는
호스트가 니모닉을 기기로 보낸다. 어느 쪽이든 PC 가 니모닉 전체를 본다.
화면이 없어 기기 안에서 보여 줄 방법이 없기 때문이다. `SECURITY.md` §3.

---

## 끝난 일

| | 무엇 | 어떻게 확인했나 |
|---|---|---|
| T1·T2 | 펌웨어 구현이 둘이라 한쪽만 검증받던 것을 하나로 합쳤다 | Arduino 와 Zephyr 가 `firmware/nuwallet/src/core/` 를 같이 컴파일한다. 적합성 브리지도 하나로 줄었다 |
| T3 | Solana 를 코어가 지원한다 | 실기기에서 Ed25519 서명을 `cryptography` 로 검증했다. 호스트 브리지의 ed25519 는 대역이라 값이 보드와 다르다 |
| T4 | SDK 가 EIP-1559(type 2) 를 만든다 | `rlp.test.js` 가 구조를, 적합성 테스트가 **펌웨어 파서(`core/rlp.c`)의 수용 여부**를 본다 |
| T5 | EIP-712 구조체 인코딩을 넣었다 | `eip712.test.js` — EIP 본문의 Mail 예제와 컨트랙트가 상수로 쓰는 typehash 두 개가 모두 맞는다 |
| T6 | 기기 상태 코드를 EIP-1193 오류 코드로 바꾼다 | `provider.test.js` — 사용자 거부가 4001 로 온다 |
| T7 | 예제 DApp 이 EIP-6963 으로 자신을 알린다 | `app/dapp/page.tsx` 가 provider 가 바뀔 때마다 다시 알린다 |
| T9 | `wallet.c` 837줄을 책임별로 나눴다 | 분리 전후로 테스트 수가 같고 골든 벡터가 안 바뀌었다 |
| T8 | 결과 알림을 놓치면 SDK 가 `GET_RESULT` 로 되묻는다 | 적합성 브리지가 결과 알림을 버리게 하고도 골든 벡터 서명을 받는다. 기기 쪽 만료를 5초 타임아웃 대신 22ms 만에 알아낸다 |
| T10 | `protocol.ts` 를 constants·framing·codec·path·bytes 로 나눴다 | `protocol.ts` 는 모아 내보내기만 한다. `check-protocol.py` 는 `constants.ts` 를 읽고 불일치 0건이다 |
| T11 | 웹 페이지를 훅(`app/lib`), 화면 조각(`components/`), 순수 함수(`dapp/tx.ts`)로 나눴다 | tsc, eslint, `npm run build` 통과 |
| T12 | client·provider 에서 `signature.ts`, `base58.ts`, `fees.ts`, `errors.ts` 를 떼어냈다 | 테스트 108개 통과, 계층 위반 0 |
| T18 | DApp 이 수수료를 먼저 정해 트랜잭션에 넣고 같은 상한으로 잔액을 확인한다 | 규칙은 SDK 의 `suggestFees` 하나를 provider 와 DApp 이 같이 쓴다 |
| T19 | `commands.c` 를 공통·셋업·Ethereum·Solana 네 파일로 나눴다 | 펌웨어 148 + 140, 적합성 23개 통과. Arduino 스케치 빌드 통과 |
| T16 | 두 세션이 같은 파일을 고치던 문제 | 지금은 커밋이 한 갈래다 |
| — | 실기기에서만 나던 시계 스큐 언더플로와 연결 해제 시 세션이 안 닫히던 버그 | 회귀 테스트 `test_session_idle` 과 `nu_wallet_disconnected` |
| — | `codegraph.mjs` 의 계층 패턴이 펌웨어 재배치 뒤 낡아 있었다 | 위반 5건이 전부 오탐이었다. 지금은 위반 0 |
| — | `npm test` 가 `provider.test.js` 와 `adapters.test.js` 를 안 돌리고 있었다 | 스크립트에 넣었다. 70개 → 101개 |

---

## 검증되어 있는 것 (회귀 방지선)

| | 개수 | 무엇 |
|---|---|---|
| `firmware/test/test_crypto` + `test_wallet` | 140 + 148 | BIP-39/32 · RFC 6979 · Keccak · EIP-155 공식 벡터 · 프레이밍 · RLP · 저장 · 서명 · PIN · 잠금 · 타임아웃 |
| `sdk/test/conformance` | 23 | **실제 펌웨어 코어를 자식 프로세스로 띄워** SDK 와 주고받는다 |
| `sdk/test/*` 나머지 | 85 | 프레이밍 · 워드리스트 · PIN · 재연결 · provider · EIP-712 · EIP-1559 · 수수료 |
| `scripts/check-protocol.py` | — | 명령·상태·UUID 가 펌웨어/SDK/문서 세 곳에서 같은지 |
| `scripts/codegraph.mjs` | — | 계층 위반 · 순환 |
| `npm run build` | — | 웹 네 페이지가 빌드되는지 |
