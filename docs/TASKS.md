# 작업 리스트 — 미구현 · 오류 · 정리

전부 코드에서 확인한 것만 적는다. 추측은 "확인 필요" 로 표시했다.
검증 도구: `node scripts/codegraph.mjs`, `python3 scripts/check-protocol.py`,
`cd sdk && npm test`, `cd firmware/test && make test`.

---

## 🔴 P0 — 자금이 걸린 오류

### T1. Arduino 컨트롤러가 SDK 와 다른 키로 서명한다
`firmware/nuwallet/src/controller/protocol.cpp`

- `:240` 서명 명령이 `read_path(p, len, …)` — **체인 바이트를 안 읽는다.**
  SDK 가 보내는 `0x01` 을 `DEPTH=1` 로 해석해 **완전히 다른 경로의 키로 서명**한다
- SIGN_RESULT 조립이 `out[6+65]` 에 `memcpy(out+6, sig, 64)` — `SIG_LEN` 이 없어
  SDK 가 서명을 한 바이트 밀려 읽는다
- `:189` `0x20 GET_ADDRESS` 도 체인 바이트 없음

**조용히 틀리는 종류라 제일 위험하다.** 주소는 정상으로 보이는데 서명만 어긋난다.

→ 선택지 둘. `docs/nu40-dk-firmware-installation.md` 참고.
  - **(권장)** Arduino 컨트롤러를 지우고, 검증된 코어(`firmware/wallet/src/app/`,
    플랫폼 독립 순수 C)에 `port/arduino/` HAL 다섯 함수만 붙인다
  - 또는 컨트롤러에 위 세 가지를 반영하고 전용 브리지로 적합성 16개를 통과시킨다

### T2. 펌웨어가 둘인데 검증은 하나만 받았다
`firmware/wallet/` (검증됨) vs `firmware/nuwallet/src/controller/` (미검증)

같은 프로토콜을 두 번 구현했다. `scripts/check-protocol.py` 와
`sdk/test/conformance.test.js` 는 Zephyr 코어만 본다. T1 과 함께 해소한다.

### T3. Solana 를 SDK 는 지원하는데 펌웨어는 못 한다
- `sdk/src/solana.ts` — 어댑터 있음
- `firmware/nuwallet/src/crypto/slip10.c` — SLIP-0010 있음. **Zephyr 코어에 연결 안 됨**
- `firmware/wallet/src/app/wallet.c` — `0x33 SIGN_SOLANA` 는 `UNSUPPORTED_CHAIN` 반환

지금은 정직하게 실패한다(웹도 `ready: false` 로 막아 둠). 지원하려면
ed25519 서명 + `0x21` 의 `CHAIN=0x02` 경로를 코어에 붙여야 한다.

---

## 🟠 P1 — 지갑으로 쓰기 어려운 부분

### T4. EIP-1559(type 2) 트랜잭션을 만들지 못한다
`sdk/src/provider.ts:129,156` 이 `encodeLegacyUnsigned` 만 쓴다.

기기는 이미 파싱한다 (`rlp.c:105`). **SDK 만 고치면 된다.**
지금은 수수료를 과다 지불하고, legacy 를 안 받는 체인에서는 아예 못 보낸다.

### T5. EIP-712 구조체 인코딩이 없다
`sdk/src/provider.ts:118` 의 `eth_signTypedData_v4` 가 그냥 던진다.

기기는 `domainSeparator`/`messageHash` 두 해시만 받아 서명할 준비가 돼 있다
(`0x32`). SDK 에 `encodeType`/`hashStruct`/`domainSeparator` 를 넣으면 된다.
permit 류를 쓰는 DApp 을 지금은 하나도 못 쓴다.

### T6. EIP-1193 오류 코드를 안 준다
DApp 이 "사용자가 거부(4001)"와 "기기 오류"를 구분하지 못한다.
`WalletError.status` → `{code, message}` 매핑이 필요하다.

### T7. EIP-6963 을 아무도 호출하지 않는다
`sdk/src/browser.ts` 의 `announceNuWalletProvider()` 가 구현돼 있는데
웹 어디서도 부르지 않는다. 예제 DApp 에 붙이면 끝.

### T8. `GET_RESULT(0x40)` 가 죽은 명령이다
펌웨어는 구현했고 문서에도 있는데 SDK 가 안 쓴다. 이벤트를 놓쳤을 때의
복구 경로인데, 지금은 이벤트를 놓치면 그냥 타임아웃이다.

---

## 🟡 P2 — 구조 · 단일 책임

`node scripts/codegraph.mjs` 기준 **계층 위반 0, 순환 0**. 구조는 깨끗하다.
문제는 파일 *안*이다 (`--symbols`).

### ~~T9. `wallet.c` 837줄 — 책임 5개~~ ✅ 완료

책임별로 나눴다. 경계는 `internal.h` 에 있다.

| 파일 | 줄 | 책임 |
|---|---|---|
| `wire.c` | 73 | 호스트로 나가는 바이트 — 응답·이벤트 인코딩, LED 출력 |
| `session.c` | 94 | 잠금 해제된 세션 — 시드·주소 파생·플래시 레코드 |
| `challenge.c` | 274 | 사람의 승인 — 랜덤 챌린지, PIN 입력, 타임아웃, LED |
| `commands.c` | 405 | 프로토콜 — 요청 파싱과 디스패치 |
| `wallet.c` | 38 | 생명주기 — 부팅과 연결 해제 |

동작이 안 바뀌었다는 근거: 분리 전후로 펌웨어 254개, SDK 71개(적합성 16개 포함)가
모두 통과. 적합성 테스트는 **분리한 코어를 그대로 컴파일해서** SDK 를 물린다.

### T10. `protocol.ts` 심볼 33개
`sdk/src/protocol.ts` 가 상수 + 프레이밍 + 코덱 + 경로 + hex 유틸을 한 파일에 담았다.
→ `constants.ts` / `framing.ts` / `codec.ts` / `path.ts` / `hex.ts`.
   `index.ts` 배럴이 있으니 외부 영향은 없다.

### T11. 웹 페이지가 한 파일에 전부
`app/dapp/page.tsx` 383줄, `app/setup/page.tsx` 302줄.
상태·RPC 호출·폼·렌더가 섞여 있다.
→ 훅(`useNuWallet`, `useApproval`)과 표현 컴포넌트로 분리.

### T12. `client.ts` 337줄
기기 정보 + 셋업 + 주소 + 서명 3종 + 승인 대기 흐름 + 서명/base58 헬퍼.
→ 최소한 `signature.ts`(v 계산·SIG_LEN) 와 `base58.ts` 는 뺀다.

---

## 🟢 P3 — 위생

### T13. 빌드 산출물 ~7MB 가 git 히스토리에 있다
`firmware/nuwallet/build/` 의 elf 3.83MB · map 2.14MB · hex 0.51MB · zip 0.18MB.
지금은 `.gitignore` 로 막았지만 히스토리에는 영구히 남는다.
저장소를 방금 공개했으니 지우려면 지금이 가장 싸다 (`git filter-repo`).

### T14. NU-40 DK 용 Zephyr 보드 정의가 없다
`nucode_nu40` 은 저장소에도 Zephyr 업스트림에도 없다. 그래서 Zephyr 앱은
**지금 빌드되지 않는다.** 필요한 값은 `docs/nu40-dk-firmware-installation.md` 에
정리해 뒀다 (LED P0.13~16, 버튼 P0.11/12/24/25, S140 v6 뒤 파티션,
`CONFIG_BUILD_OUTPUT_UF2`).

### T15. `APPROTECT` 가 꺼져 있다
SWD 로 플래시를 통째로 덤프할 수 있다. 켜도 글리칭은 못 막지만
캐주얼한 덤프는 막는다. `SECURITY.md` §7.

### T16. 두 세션이 같은 파일을 동시에 고치고 있다
작업 중 `sdk/src/*`, `app/*`, `firmware/nuwallet/*` 이 계속 바뀌었고 커밋도
따로 올라갔다. 한쪽을 멈추지 않으면 서로 덮어쓴다.

### T17. 미푸시 작업
프로토콜 3자 정렬, BLE 재연결 수정, 적합성 테스트 16개, 문서 정정이
전부 커밋되지 않은 상태다.

---

## 검증되어 있는 것 (회귀 방지선)

| | 개수 | 무엇 |
|---|---|---|
| `firmware/test/test_crypto` | 140 | BIP-39/32 · RFC 6979 · Keccak · EIP-155 공식 벡터 |
| `firmware/test/test_wallet` | 114 | 프레이밍 · RLP · 저장 · 니모닉 · 서명 · PIN · 잠금 · 타임아웃 |
| `sdk/test/conformance` | 16 | **실제 펌웨어 코어를 자식 프로세스로 띄워** SDK 와 주고받음 |
| `sdk/test/*` 나머지 | 55 | 프레이밍 · 워드리스트 · PIN · transport 재연결 · provider |
| `scripts/check-protocol.py` | — | 명령·상태·UUID 가 펌웨어/SDK/문서 세 곳에서 같은지 |
| `scripts/codegraph.mjs` | — | 계층 위반 · 순환 |
