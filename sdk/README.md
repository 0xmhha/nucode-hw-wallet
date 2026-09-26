# @nucode/hw-wallet

Web Bluetooth로 NuWallet을 DApp 표준 지갑 인터페이스에 연결한다.

## Base Sepolia · EIP-1193/EIP-6963

```js
import {
  NuWallet, NuWalletProvider, announceNuWalletProvider,
} from '@nucode/hw-wallet';

const wallet = new NuWallet();
const provider = new NuWalletProvider(wallet, {
  chainId: 84532,
  rpcUrl: 'https://sepolia.base.org',
  onStart: () => showMessage('보드 버튼을 1 → 2 → 3 → 4 순서로 누르세요'),
});

announceNuWalletProvider(provider);
```

이후 DApp은 MetaMask와 같은 방식으로 호출한다.

```js
const [from] = await provider.request({ method: 'eth_requestAccounts' });
const txHash = await provider.request({
  method: 'eth_sendTransaction',
  params: [{ from, to, data, value: '0x0' }],
});
```

`announceNuWalletProvider()`를 쓰면 기존 EIP-6963 지갑 선택기에
`NuWallet (NU-40 DK)`가 나타난다. DApp이 `window.ethereum`에 직접 의존하지
않아도 되며 MetaMask와 NuWallet을 같은 목록에서 선택할 수 있다.

## 트랜잭션 형식 — legacy 와 EIP-1559

provider 가 알아서 고른다. 최신 블록에 `baseFeePerGas` 가 있으면 type 2 로,
없으면 legacy 로 낸다. 확인은 한 번만 하고 기억한다.

호출자가 뜻을 밝히면 그쪽을 따른다. `gasPrice` 를 주면 legacy 로,
`maxFeePerGas` 계열이나 `type: '0x2'` 를 주면 type 2 로 낸다.

```js
// 노드가 정하게 둔다
await provider.request({ method: 'eth_sendTransaction', params: [{ from, to, value }] });

// type 2 로 직접 지정
await provider.request({
  method: 'eth_sendTransaction',
  params: [{ from, to, value, maxFeePerGas: '0x77359400', maxPriorityFeePerGas: '0x3b9aca00' }],
});
```

기기는 해시가 아니라 **직렬화된 트랜잭션 자체**를 받아 스스로 Keccak 한다.
그래서 `0x02 ‖ RLP([…])` 의 항목 수와 순서가 틀리면 서명되지 않고 거부된다.

## EIP-712 구조체 서명

```js
const sig = await provider.request({
  method: 'eth_signTypedData_v4',
  params: [from, JSON.stringify(typedData)],
});
```

`domain.chainId` 가 연결된 체인과 다르면 서명하지 않고 4901 로 거절한다.
기기는 해시 두 개만 보므로, 이 확인을 여기서 하지 않으면 아무 데서도 못 한다.

해시만 필요하면 따로 쓸 수 있다.

```js
import { hashTypedData } from '@nucode/hw-wallet';
const { domainSeparator, messageHash, digest } = hashTypedData(typedData);
```

## 오류 — EIP-1193 코드

`provider.request()` 가 던지는 것은 `ProviderRpcError` 이고 `code` 로 분기한다.

| code | 언제 |
|---|---|
| `4001` | 사용자가 거부했거나, 버튼 순서를 틀렸거나, 승인 시간이 지났다 |
| `4100` | 기기가 잠겨 있거나 PIN 이 필요하거나 지갑이 아직 없다 |
| `4200` | 펌웨어가 모르는 명령이거나 지원하지 않는 체인이다 |
| `4901` | 서명 요청의 체인이 연결된 체인과 다르다 |
| `4902` | DApp 이 다른 체인으로 바꾸라고 했다 |
| `-32602` | 요청 형식이 잘못됐다 |
| `-32603` | 기기 내부 오류 |

원래 기기 오류는 `error.data` 에 `WalletError` 로 남는다.

## Solana web3.js

```js
import { NuWallet, NuWalletSolanaAdapter } from '@nucode/hw-wallet';
import * as web3 from '@solana/web3.js';

const provider = new NuWalletSolanaAdapter(new NuWallet(), web3, {
  onStart: () => showMessage('보드에서 서명을 승인하세요'),
});

const { publicKey } = await provider.connect();
transaction.feePayer = publicKey;
const signed = await provider.signTransaction(transaction);
const signature = await connection.sendRawTransaction(signed.serialize());
```

> 현재 Zephyr 펌웨어는 Solana 서명을 아직 지원하지 않는다. SDK 어댑터는 준비돼
> 있지만 `0x33 SIGN_SOLANA`가 구현된 펌웨어가 올라간 보드에서만 동작한다.

## toyton_agent_wallet 적용 지점

- `web/base.js`: 별도 NUS UUID/JSON `connectBoard()`를 제거하고
  `NuWalletProvider`를 EIP-6963에 등록한다. 기존 `connectWallet()`,
  `eth_sendTransaction`, 영수증 조회 코드는 그대로 쓴다.
- `web/app.js`: Phantom 대신 `NuWalletSolanaAdapter`를 선택 가능한 provider로
  추가한다. 기존 `provider.connect()`와 `provider.signTransaction(tx)` 흐름은
  그대로 쓴다.
- 정적 HTML에서 SDK를 쓰려면 SDK를 ESM 번들로 빌드해 배포하거나 DApp에 Vite 같은
  번들 단계를 추가해야 한다. 현재 SDK `dist`는 브라우저 ESM이므로 CDN의 단일 IIFE
  스크립트처럼 전역 변수로 자동 노출되지는 않는다.
