# 하드웨어 지갑이 지원해야 하는 EIP

각 항목이 **왜** 필요한지, 이 저장소의 어디에 있는지, 지금 어디까지 됐는지를 적는다.
"지원함"은 **테스트로 확인된 것**만 뜻한다 (`sdk/test/conformance.test.js`,
`firmware/test/`).

범례 — ✅ 구현·검증 · 🟡 부분 · ❌ 미구현 · ➖ 해당 없음

---

## 1. 서명 정확성 — 틀리면 자금을 잃는다

| EIP | 무엇 | 왜 지갑에 필요한가 | 상태 | 위치 |
|---|---|---|---|---|
| **EIP-155** | 트랜잭션에 `chainId` 를 넣고 `v = recid + chainId*2 + 35` | 없으면 **한 체인의 서명이 다른 체인에서 그대로 재생**된다. 테스트넷 서명이 메인넷에서 통한다 | ✅ | `rlp.c` (9항목 검증) · `client.ts:309` |
| **EIP-2** | `s ≤ n/2` (low-s) 정규화 | 안 하면 같은 트랜잭션에 유효 서명이 두 개 생겨 **해시가 달라진다**(가변성). 노드가 거부하기도 한다 | ✅ | `ecdsa.c:89` — 뒤집을 때 `recid` bit0 도 함께 뒤집는다 |
| **EIP-191** | `personal_sign` 접두사 `\x19Ethereum Signed Message:\n<len>` | 접두사가 없으면 **서명 요청이 트랜잭션으로 둔갑**할 수 있다. 로그인 서명이 송금 서명이 되는 공격 | ✅ | `wallet.c:634` — 기기가 접두사를 직접 붙인다 |
| **EIP-712** | 구조체 데이터 서명 `\x19\x01 ‖ domainSeparator ‖ hashStruct` | 사람이 읽을 수 있는 서명. permit·주문 서명이 전부 이걸 쓴다 | 🟡 | 기기는 두 해시를 받아 서명 (`0x32`). **SDK 에 구조체 인코딩이 없다** |
| **EIP-2718** | 타입 있는 트랜잭션 봉투 `type ‖ payload` | 이후의 모든 새 트랜잭션 형식이 이 위에 올라간다 | 🟡 | 기기는 `0x01`/`0x02` 를 파싱·검증 (`rlp.c:78`). **SDK 가 만들지 않는다** |
| **EIP-1559** | type 2, `maxFeePerGas`/`maxPriorityFeePerGas` | 2021년 이후 **사실상 표준 트랜잭션**. legacy 만 지원하면 수수료를 과다 지불한다 | 🟡 | 기기는 파싱 가능 (`rlp.c:105`). **SDK provider 는 legacy 만 만든다** |
| **EIP-2930** | type 1, access list | 1559 의 앞 세대. 실사용은 적다 | 🟡 | 기기 파싱만 |
| **EIP-7702** | type 4, EOA 에 코드 위임 | 2025 Pectra. 지갑이 모르면 **사용자가 서명하는 위임을 막을 수 없다** | ❌ | 기기가 `BAD_PARAM` 으로 거부 — 모르는 걸 서명하지 않으므로 안전한 실패 |
| EIP-4844 | type 3, blob | 롤업 제출자용. 일반 지갑엔 불필요 | ➖ | |

> **왜 기기가 RLP 원문을 받는가.** 호스트가 준 해시를 그대로 서명하면 기기가
> *무엇이든* 서명하게 된다. 그래서 `SIGN_TX` 는 서명 전 트랜잭션 바이트 전체를
> 받아 **기기가 Keccak-256 을 직접 계산**한다. `rlp.c` 의 형식 검증은 그 원문이
> 정말 트랜잭션인지 보는 최소한의 방어다. `SECURITY.md` §2.

## 2. 주소·키

| 표준 | 무엇 | 상태 | 위치 |
|---|---|---|---|
| **EIP-55** | 대소문자 체크섬 주소 | ✅ | `address.ts:69` |
| BIP-32 / BIP-39 / BIP-44 | 계층 결정적 파생, 니모닉 | ✅ | `crypto/` — 공식 벡터로 검증 |
| SLIP-44 | coin type 60 = Ethereum (`m/44'/60'/0'/0/0`) | ✅ | `protocol.ts` `DEFAULT_PATHS` |
| SLIP-0010 | ed25519 파생 (Solana) | 🟡 | `crypto/slip10.c` 는 있으나 **지갑 코어에 연결되지 않음** |

## 3. DApp 연결 — 지갑이 "쓸 수 있는" 것이 되는 부분

| EIP | 무엇 | 왜 필요한가 | 상태 | 위치 |
|---|---|---|---|---|
| **EIP-1193** | provider JS API (`request`/`on`) | 이게 없으면 DApp 이 지갑에 붙는 표준 방법이 없다 | ✅ | `provider.ts:44` |
| **EIP-1102** | `eth_requestAccounts` — 사용자 동의 후 계정 노출 | 동의 없이 주소를 주면 추적된다 | ✅ | `provider.ts:77` |
| **EIP-695** | `eth_chainId` | DApp 이 체인을 확인하는 표준 | ✅ | `provider.ts:88` |
| **EIP-6963** | 여러 지갑을 DApp 이 발견하는 방법 | 없으면 `window.ethereum` 을 두고 다른 지갑과 **덮어쓰기 싸움**을 한다 | 🟡 | `browser.ts` 에 `announceNuWalletProvider()` 가 있으나 **웹 어디서도 호출하지 않는다** |
| **EIP-3085 / 3326** | `wallet_addEthereumChain` / `wallet_switchEthereumChain` | DApp 이 네트워크 전환을 요청하는 표준 | ❌ | 지금은 웹 UI 에서 수동 설정 |
| **EIP-1474** | JSON-RPC 오류 코드 (4001 사용자 거부 등) | DApp 이 "사용자가 거부"와 "기기 오류"를 구분해야 한다 | 🟡 | `WalletError.status` 는 있으나 **1193 오류 코드로 변환하지 않는다** |
| **EIP-4361** | Sign-In with Ethereum | `personal_sign` 위에서 동작 — 별도 구현 불필요 | ✅ | EIP-191 경로 그대로 |

## 4. 이 하드웨어로는 못 하는 것

| EIP | 무엇 | 왜 안 되는가 |
|---|---|---|
| **EIP-7730** | clear signing 메타데이터 — 트랜잭션을 사람 말로 표시 | **기기에 화면이 없다.** LED 4개로는 "0.5 ETH 를 0xAbC…" 를 못 보여준다 |
| ERC-20/721 data 디코딩 | `transfer(to,amount)` 를 해석해 표시 | 같은 이유. 디코딩해도 보여줄 데가 없다 |

이게 이 프로젝트의 근본 한계다. 버튼 챌린지는 **"사람이 물리적으로 여기 있다"만
증명**하고, 사용자가 승인하는 내용이 화면에 뜬 것과 같은지는 증명하지 못한다.
`SECURITY.md` §2 에 정확히 적어 두었다.

## 5. 우선순위

지금 상태에서 실제로 막히는 순서대로.

1. **EIP-1559 (type 2) 트랜잭션 생성** — 기기는 이미 파싱한다. SDK provider 만
   고치면 된다. 지금은 legacy 라 수수료가 비싸고, 일부 체인은 legacy 를 아예 안 받는다
2. **EIP-712 구조체 인코딩** — 기기는 준비됐다. SDK 에 `hashStruct`/`encodeType`
   가 없어 `eth_signTypedData_v4` 가 그냥 던진다. permit 류를 아예 못 쓴다
3. **EIP-1474 오류 코드 매핑** — DApp 이 사용자 거부(4001)를 구분 못 한다
4. **EIP-6963 연결** — 구현은 있는데 아무도 부르지 않는다. 예제 DApp 에서
   `announceNuWalletProvider()` 를 호출하고 발견 흐름을 보여주면 끝
5. EIP-3085/3326 — 네트워크 전환

자세한 작업 항목은 `docs/TASKS.md`.
