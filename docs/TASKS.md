# 작업 리스트 — 미구현 · 오류 · 정리

전부 코드에서 확인한 것만 적는다. 추측은 "확인 필요" 로 표시했다.
검증 도구: `node scripts/codegraph.mjs`, `python3 scripts/check-protocol.py`,
`cd sdk && npm test`, `cd firmware/test && make test`, `npm run build`.

---

## 남은 일

순서대로 한다. 앞 묶음이 끝나야 뒤 묶음의 결과를 믿을 수 있다.

### 1. 다음 작업

#### 🟠 T14. Zephyr 포트를 실기기에서 확인한다
빌드까지는 끝났다 (2026-09-26, `feat/zephyr-port`). 보드 정의(`nucode_nu40`)는
Zephyr `main` 에 이미 들어가 있어서 저장소 밖 트리를 가리킬 필요가 없었다.
`firmware/wallet/west.yml` 이 검증한 커밋에 고정하고 `scripts/build-zephyr.sh` 로
빌드한다. 빌드하면서 보드에서 틀렸을 것 다섯 가지를 고쳤다 (연결 해제를 BT
스레드에서 처리하던 것, 연결 포인터 경합, 끊긴 뒤 재광고 안 함, LED 극성,
공장 초기화용 HAL 누락).

남은 일은 실기기다. `zephyr.uf2` 를 올리고 `npm run test:device:interactive` 를
통과시킨다. Solana 항목은 이 포트에서 건너뛴다 (Ed25519 없음). 올리면 SoftDevice
자리를 덮어쓰므로 설치 문서의 되돌리기 절차도 이때 처음 확인하게 된다.

### 2. 하지 않기로 했거나 고칠 수 없어 기록만 하는 것

#### T15. `APPROTECT` 는 켜지 않는다
SWD 디버그 포트를 잠그는 설정이다. 2026-09-26 에 켜지 않기로 정했다. 켜면
디버거로 플래시를 읽고 쓰는 길이 막히고, 되살리려면 칩 전체를 지워 부트로더까지
SWD 로 다시 올려야 한다. 지금 가진 장비로는 디버깅이 어려워진다.

한때 넣었던 `NU_ENABLE_APPROTECT` 스위치와 UICR 에 쓰는 코드는 지웠다. 누가 그
스위치를 빌드 옵션으로 넘기면 `config.h` 가 빌드를 멈춘다. 그 결과 SWD 로 플래시를
읽을 수 있다는 사실은 그대로 남는다 (`SECURITY.md` §1). 켰더라도 부트로더가 서명 없는
펌웨어를 받아 주므로 그 길로 시드를 읽는 것은 막지 못했다.

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
| T21 | 실기기에서 PIN 설정(1차 입력과 재입력)부터 서명까지 끝까지 통과했다 | 2026-09-26, PR #2 머리 `dd4cd70` 빌드. `npm run test:device:interactive` 8개 통과. 서명 승인을 5.6초 기다리는 동안 SDK 의 3초 간격 `GET_RESULT` 가 겹쳤지만 서명은 골든 벡터와 같았다 |
| T13 | 펌웨어 바이너리를 저장소에서 빼고, 빈 머신에서 빌드하는 가이드와 `scripts/build-firmware.sh` 로 바꿨다 | 빈 arduino-cli 환경과 빈 Python 가상환경에서 새로 클론한 저장소를 빌드하고 보드에 올려 실기기 테스트를 통과했다. 그 과정에서 코어의 macOS 용 nrfutil 이 실행 권한 없이 풀려 빌드가 멈추는 것을 찾았다. 히스토리의 옛 elf·map 은 지우지 않았다 (`git filter-repo` 가 필요하고, 앞으로 더 커지지는 않는다) |
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
