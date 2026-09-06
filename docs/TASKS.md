# 작업 리스트 — 미구현 · 오류 · 정리

전부 코드에서 확인한 것만 적는다. 추측은 "확인 필요" 로 표시했다.
검증 도구: `node scripts/codegraph.mjs`, `python3 scripts/check-protocol.py`,
`cd sdk && npm test`, `cd firmware/test && make test`, `npm run build`.

---

## 남은 일

### 🟠 T8. `GET_RESULT(0x40)` 가 죽은 명령이다
펌웨어는 구현했고 (`core/commands.c:465`) 문서에도 있는데 SDK 는 상수만
가지고 있다 (`sdk/src/protocol.ts:33`). 이벤트를 놓쳤을 때의 복구 경로인데,
지금은 이벤트를 놓치면 그냥 타임아웃까지 기다린다.

BLE notify 는 구독 직후나 연결이 불안할 때 실제로 빠진다. `awaitApproval` 의
타임아웃 직전에 한 번 `GET_RESULT` 로 물어보면 살아날 요청이 있다.

### 🟠 T18. 전송 전 잔액 확인이 legacy 수수료로 계산된다
`app/dapp/page.tsx` 의 `send` 가 `eth_gasPrice` 로 필요 금액을 어림한다.
그런데 type 2 로 나갈 때 지갑이 잡는 상한은 `baseFee*2 + tip` 이라 더 크다.
잔액이 아슬아슬하면 이 확인을 통과하고도 노드가 거부한다.
→ 화면 쪽도 `maxFeePerGas` 를 쓰거나, 확인을 provider 안으로 옮긴다.

### 🟡 T10. `protocol.ts` 심볼 35개
상수 + 프레이밍 + 코덱 + 경로 + hex 유틸이 한 파일(287줄)에 있다.
→ `constants.ts` / `framing.ts` / `codec.ts` / `path.ts` / `hex.ts`.
   `index.ts` 배럴이 있으니 밖에서 보는 이름은 안 바뀐다.

### 🟡 T11. 웹 페이지가 한 파일에 전부
`app/dapp/page.tsx` 445줄, `app/setup/page.tsx` 285줄. 상태·RPC 호출·폼·렌더가
섞여 있다. → 훅(`useNuWallet`, `useApproval`)과 표현 컴포넌트로 나눈다.

### 🟡 T12. `client.ts` 380줄 · `provider.ts` 378줄
`client.ts` 는 기기 정보 + 셋업 + 주소 + 서명 3종 + 승인 대기 + 서명/base58
헬퍼를 함께 들고 있다. 최소한 `signature.ts`(v 계산·SIG_LEN)와 `base58.ts` 는
뺀다. `provider.ts` 는 수수료 채우기가 늘어 커졌다 → `fees.ts` 로 뺄 수 있다.

### 🟡 T19. `commands.c` 473줄 · `challenge.c` 384줄
코어를 다섯으로 나눈 뒤 다시 자란 파일들이다. `commands.c` 는 체인별 분기가
늘면서 커졌다 → 이더리움·Solana 핸들러를 나눌 수 있다.
당장 급하지는 않다. 계층 위반은 없다 (`codegraph.mjs`).

### 🟢 T13. 빌드 산출물이 git 히스토리에 남아 있다
지금 추적하는 것은 `firmware/nuwallet/build/NUWALLET.UF2` 하나뿐이고 나머지는
`.gitignore` 가 막는다. 그러나 히스토리에는 예전 elf 3.83MB · map 2.14MB 가
영구히 남아 있어 클론이 그만큼 무겁다. 지우려면 `git filter-repo` 로 다시 써야
하고, 그러면 이미 클론한 사람들의 히스토리가 갈라진다.

### 🟢 T14. NU-40 DK 용 Zephyr 보드 정의가 없다
`nucode_nu40` 은 저장소에도 Zephyr 업스트림에도 없다. 그래서 `firmware/wallet`
의 Zephyr 앱은 **지금 빌드되지 않는다.** 보드에 올라가는 것은 Arduino 스케치다
(`firmware/nuwallet`). 필요한 값은 `docs/nu40-dk-firmware-installation.md` 에
정리해 뒀다 (LED P0.13~16, 버튼 P0.11/12/24/25, S140 v6 뒤 파티션,
`CONFIG_BUILD_OUTPUT_UF2`).

### 🟢 T15. `APPROTECT` 가 꺼져 있다
SWD 로 플래시를 통째로 덤프할 수 있다. 켜도 글리칭은 못 막지만 캐주얼한
덤프는 막는다. `SECURITY.md` §7.

### 🔴 T20. 서명 내용을 사용자가 볼 수 없다 (구조적 한계)
LED 4개로는 "0.5 ETH 를 0xAbC… 로" 를 보여줄 수 없다. 버튼 승인은 **사람이
물리적으로 여기 있다**만 증명하고, 화면에 뜬 것과 서명되는 것이 같은지는
증명하지 못한다. EIP-712 는 기기가 해시 두 개만 받으므로 특히 그렇다.
고칠 수 있는 문제가 아니라 기록해 두는 항목이다. `SECURITY.md` §2.

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
| T16 | 두 세션이 같은 파일을 고치던 문제 | 지금은 커밋이 한 갈래다 |
| T17 | 미푸시 작업 | `origin/protocol-v2-unified-core` 와 같다 |
| — | 실기기에서만 나던 시계 스큐 언더플로와 연결 해제 시 세션이 안 닫히던 버그 | 회귀 테스트 `test_session_idle` 과 `nu_wallet_disconnected` |
| — | `codegraph.mjs` 의 계층 패턴이 펌웨어 재배치 뒤 낡아 있었다 | 위반 5건이 전부 오탐이었다. 지금은 위반 0 |
| — | `npm test` 가 `provider.test.js` 와 `adapters.test.js` 를 안 돌리고 있었다 | 스크립트에 넣었다. 70개 → 101개 |

---

## 검증되어 있는 것 (회귀 방지선)

| | 개수 | 무엇 |
|---|---|---|
| `firmware/test/test_crypto` + `test_wallet` | 148 | BIP-39/32 · RFC 6979 · Keccak · EIP-155 공식 벡터 · 프레이밍 · RLP · 저장 · 서명 · PIN · 잠금 · 타임아웃 |
| `sdk/test/conformance` | 21 | **실제 펌웨어 코어를 자식 프로세스로 띄워** SDK 와 주고받는다 |
| `sdk/test/*` 나머지 | 80 | 프레이밍 · 워드리스트 · PIN · 재연결 · provider · EIP-712 · EIP-1559 |
| `scripts/check-protocol.py` | — | 명령·상태·UUID 가 펌웨어/SDK/문서 세 곳에서 같은지 |
| `scripts/codegraph.mjs` | — | 계층 위반 · 순환 |
| `npm run build` | — | 웹 네 페이지가 빌드되는지 |
