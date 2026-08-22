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
