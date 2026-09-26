'use client';
/**
 * 예제 DApp — SDK 만 써서 지갑 연결부터 트랜잭션 전송까지 한 화면에서 보여준다.
 *
 * 여기서 직접 하는 일은 없다. 전부 `NuWalletProvider` (EIP-1193) 를 통한다.
 * 즉 기존 DApp 이 `window.ethereum` 자리에 이걸 꽂으면 그대로 돌아간다는 뜻이다.
 *
 * 개인키는 보드 밖으로 나오지 않는다. 이 페이지는 서명되지 않은 트랜잭션을
 * 만들어 보드에 보내고, 보드가 버튼 승인을 받아 서명한 것을 돌려받아 RPC 로
 * 흘려보낼 뿐이다.
 *
 * 화면 조각은 `components/`, 폼 값으로 트랜잭션을 만드는 일은 `tx.ts`,
 * 수수료는 `fees.ts` 에 있다. 이 파일은 상태를 들고 버튼과 SDK 호출을 잇는다.
 */
import { useCallback, useMemo, useState } from 'react';
import Link from 'next/link';
import { NuWallet, NuWalletProvider, hashTypedData, hex } from '@/sdk/src/index';
import s from './dapp.module.css';
import { WebAppFooter } from '../components/WebAppFooter';
import { formatEther } from '../lib/ether';
import { useApproval } from '../lib/approval';
import { useTask } from '../lib/task';
import { PRESETS } from './presets';
import { maxFeeCost, suggestFees } from './fees';
import { buildPermit, buildTxParams, withFees, type TxForm, type TxParams } from './tx';
import { useDappProvider } from './useDappProvider';
import { ChainCard } from './components/ChainCard';
import { AccountCard } from './components/AccountCard';
import { TransactionCard } from './components/TransactionCard';
import { MessageCard } from './components/MessageCard';
import { LogPanel, type LogLine } from './components/LogPanel';

export default function DappDemo() {
  const wallet = useMemo(() => new NuWallet(), []);

  const [presetIdx, setPresetIdx] = useState(0);
  const [chainId, setChainId] = useState(String(PRESETS[0].chainId));
  const [rpcUrl, setRpcUrl] = useState<string>(PRESETS[0].rpc);
  const [path, setPath] = useState("m/44'/60'/0'/0/0");

  const [account, setAccount] = useState('');
  const [balance, setBalance] = useState('');
  const [nonce, setNonce] = useState('');
  const [status, setStatus] = useState('보드를 연결하고 체인을 고르세요.');

  const [to, setTo] = useState('0x3FFd98B1f972043a824F05E6821e793B26c72794');
  const [value, setValue] = useState('0.001');
  const [data, setData] = useState('');
  const [message, setMessage] = useState('NuWallet 로 서명한 메시지');

  const [log, setLog] = useState<LogLine[]>([]);
  const say = useCallback((dir: LogLine['dir'], text: string) => {
    setLog((l) => [...l.slice(-60), { dir, text }]);
  }, []);

  const { approval, callbacks, clear } = useApproval(setStatus);
  const provider = useDappProvider(wallet, { chainId, rpcUrl, path }, callbacks);

  const onBegin = useCallback((label: string) => say('out', `→ ${label}`), [say]);
  const onError = useCallback((_label: string, msg: string) => {
    say('err', `✗ ${msg}`);
    setStatus(msg);
  }, [say]);
  const { busy, run } = useTask({ onBegin, onError, onSettled: clear });

  const form: TxForm = { chainId, account, to, value, data };
  const preset = PRESETS[presetIdx]!;

  function applyPreset(i: number) {
    const p = PRESETS[i];
    if (!p) return;
    setPresetIdx(i);
    setChainId(String(p.chainId));
    setRpcUrl(p.rpc);
  }

  function providerOrThrow(): NuWalletProvider {
    if (!account) throw new Error('먼저 지갑을 연결하세요.');
    return provider;
  }

  async function refresh(p: NuWalletProvider, addr: string) {
    if (!rpcUrl) { setBalance(''); setNonce(''); return; }
    const [bal, n] = await Promise.all([
      p.request({ method: 'eth_getBalance', params: [addr, 'latest'] }) as Promise<string>,
      p.request({ method: 'eth_getTransactionCount', params: [addr, 'pending'] }) as Promise<string>,
    ]);
    setBalance(formatEther(BigInt(bal)));
    setNonce(String(BigInt(n)));
    say('in', `잔액 ${formatEther(BigInt(bal))} ETH · nonce ${BigInt(n)}`);
  }

  const connect = () => run('지갑 연결', async () => {
    if (!NuWallet.isSupported) {
      throw new Error('Web Bluetooth 를 지원하지 않습니다. Chrome/Edge 의 HTTPS 또는 localhost 에서 열어주세요.');
    }
    const accounts = await provider.request({ method: 'eth_requestAccounts' }) as string[];
    const addr = accounts[0] ?? '';
    setAccount(addr);
    say('in', `계정 ${addr}`);
    setStatus(`연결됨 — ${wallet.deviceName || 'NuWallet'}`);
    await refresh(provider, addr);
  });

  const reload = () => run('상태 갱신', async () => {
    await refresh(providerOrThrow(), account);
    setStatus('최신 상태로 갱신했습니다.');
  });

  /** 서명만 하고 보내지 않는다. 무엇이 서명되는지 눈으로 보라고 넣은 경로다. */
  const signOnly = () => run('eth_signTransaction (전송 안 함)', async () => {
    const raw = await providerOrThrow().request({
      method: 'eth_signTransaction', params: [buildTxParams(form)],
    }) as string;
    say('in', `서명된 raw 트랜잭션\n${raw}`);
    setStatus('서명 완료 — 아직 브로드캐스트하지 않았습니다.');
  });

  /** 수수료를 먼저 정해 트랜잭션에 박고, 그 상한으로 잔액을 확인한 뒤 보낸다.
   *  예전에는 eth_gasPrice 로 어림했는데, type 2 로 나가면 지갑이 잡는 상한
   *  (baseFee × 2 + tip) 이 더 커서 확인을 통과하고도 노드가 거부할 수 있었다. */
  const send = () => run('eth_sendTransaction', async () => {
    const p = providerOrThrow();
    const rpc = (method: string, params: unknown[]) => p.request({ method, params });
    const [balanceHex, fees] = await Promise.all([
      rpc('eth_getBalance', [account, 'pending']) as Promise<string>,
      suggestFees(rpc),
    ]);
    const priced = withFees(buildTxParams(form), fees);
    const gasLimit = BigInt(priced.gas ?? await rpc('eth_estimateGas', [priced]) as string);
    const tx: TxParams = { ...priced, gas: '0x' + gasLimit.toString(16) };
    const available = BigInt(balanceHex);
    const required = BigInt(tx.value ?? '0x0') + maxFeeCost(fees, gasLimit);
    if (available < required) {
      throw new Error(
        `잔액 부족: 보유 ${formatEther(available)} ETH, ` +
        `필요 ${formatEther(required)} ETH (전송액 + 수수료 상한)`);
    }
    say('out', fees.kind === 'eip1559'
      ? `type 2 · maxFeePerGas ${fees.maxFeePerGas} · tip ${fees.maxPriorityFeePerGas}`
      : `legacy · gasPrice ${fees.gasPrice}`);
    const hash = await p.request({ method: 'eth_sendTransaction', params: [tx] }) as string;
    say('in', `트랜잭션 해시 ${hash}`);
    setStatus(`전송됨 — ${hash}`);
    await refresh(p, account);
  });

  const personalSign = () => run('personal_sign', async () => {
    const hexMsg = '0x' + Array.from(new TextEncoder().encode(message),
      (b) => b.toString(16).padStart(2, '0')).join('');
    const sig = await providerOrThrow().request({
      method: 'personal_sign', params: [hexMsg, account],
    }) as string;
    say('in', `서명 ${sig}`);
    setStatus('메시지에 서명했습니다.');
  });

  /** EIP-712. SDK 가 구조체를 두 해시로 줄이고, 보드는 그 둘만 받아 서명한다.
   *  permit 을 쓰는 DApp 이 실제로 거치는 경로가 이것이다. */
  const signTypedData = () => run('eth_signTypedData_v4', async () => {
    const typed = buildPermit(form);
    const hashes = hashTypedData(typed);
    say('out', `domainSeparator ${hex(hashes.domainSeparator)}`);
    say('out', `messageHash     ${hex(hashes.messageHash)}`);
    const sig = await providerOrThrow().request({
      method: 'eth_signTypedData_v4', params: [account, JSON.stringify(typed)],
    }) as string;
    say('in', `서명 ${sig}`);
    setStatus('EIP-712 구조체에 서명했습니다.');
  });

  const connected = !!account;

  return (
    <main className={s.shell}>
      <nav className={s.nav}>
        <div className={s.brand}>
          <span className={s.mark}>NU</span>
          <span>NuWallet 예제 DApp</span>
        </div>
        <div className={s.links}><Link href="/">지갑</Link><Link href="/setup">설정</Link><Link href="/dapp">트랜잭션 테스트</Link><Link href="/debug">BLE 디버그</Link></div>
      </nav>

      <header className={s.hero}>
        <span className={s.kicker}>EXAMPLE DAPP · EIP-1193</span>
        <h1>연결하고,<br /><em>버튼으로 승인하고,</em> 보낸다.</h1>
        <p>
          이 페이지는 <code>@nucode/hw-wallet</code> 의 <code>NuWalletProvider</code> 하나만
          씁니다. 트랜잭션을 만들어 보드에 보내면 보드가 LED 하나를 켜고, 사용자가 그 옆의
          버튼을 누른 뒤에야 서명이 돌아옵니다. 개인키는 보드를 떠나지 않습니다.
        </p>
      </header>

      <p className={s.warn}>
        <strong>테스트넷만 쓰세요.</strong> 프로토타입이라 상용 하드웨어 지갑의 방어 수단이
        없습니다. 기기에 화면이 없어 <code>무엇에 서명하는지</code> 기기가 보여주지
        못합니다 — 화면에 뜬 내용과 보드가 서명하는 내용이 같다는 보장이 없습니다.
        자세한 것은 <code>SECURITY.md</code> 를 읽으세요.
      </p>

      <div className={s.grid}>
        <ChainCard
          presetIdx={presetIdx} onPreset={applyPreset}
          chainId={chainId} onChainId={setChainId}
          path={path} onPath={setPath}
          rpcUrl={rpcUrl} onRpcUrl={setRpcUrl}
          busy={busy} connected={connected}
          onConnect={connect} onReload={reload} />
        <AccountCard
          account={account} balance={balance} nonce={nonce} deviceName={wallet.deviceName}
          symbol={preset.symbol} hasRpc={!!rpcUrl} approval={approval} />
        <TransactionCard
          to={to} onTo={setTo} value={value} onValue={setValue} data={data} onData={setData}
          symbol={preset.symbol} busy={busy} connected={connected} hasRpc={!!rpcUrl}
          onSignOnly={signOnly} onSend={send} />
        <MessageCard
          message={message} onMessage={setMessage} busy={busy} connected={connected}
          onPersonalSign={personalSign} onSignTypedData={signTypedData} />
        <LogPanel log={log} />
      </div>

      <aside className={s.status} aria-live="polite">
        <span className={`${s.led} ${connected ? s.online : ''}`} />
        <strong>{status}</strong>
      </aside>
      <WebAppFooter />
    </main>
  );
}
