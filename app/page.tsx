'use client';
import { useMemo, useState } from 'react';
import { BleTransport, NuWallet, WalletError, SW, indicesToMnemonic, TESTNET, type Chain } from '@/sdk/src/index';

const CHAINS: Array<{ id: Chain; name: string; network: string; curve: string; path: string; tone: string }> = [
  { id: 'ethereum', name: 'Ethereum', network: `Sepolia · chainId ${TESTNET.ethereum.chainId}`, curve: 'secp256k1 · ECDSA', path: "m/44'/60'/0'/0/0", tone: '#6574c4' },
  { id: 'solana', name: 'Solana', network: 'Devnet · cluster devnet', curve: 'Ed25519 · SLIP-0010', path: "m/44'/501'/0'/0'", tone: '#14f195' },
];

export default function Home() {
  const wallet = useMemo(() => new NuWallet(), []);
  const [connected, setConnected] = useState(false), [busy, setBusy] = useState(false);
  const [initialized, setInitialized] = useState(false);
  const [status, setStatus] = useState('보드를 연결해 설정을 시작하세요.');
  const [words, setWords] = useState<number[] | null>(null);
  const [addresses, setAddresses] = useState<Partial<Record<Chain, string>>>({});
  const [signatures, setSignatures] = useState<Partial<Record<Chain, string>>>({});
  async function run(task: () => Promise<void>) { setBusy(true); try { await task(); } catch (e) { if (e instanceof WalletError && e.status === SW.NOT_INITIALIZED) setStatus('먼저 복구 문구를 생성하고 “백업 확인 및 보드에 저장”을 눌러주세요.'); else setStatus(e instanceof Error ? e.message : '요청 실패'); } finally { setBusy(false); } }
  const connect = () => run(async () => { if (!BleTransport.isSupported) throw new Error('Chrome 또는 Edge의 HTTPS 페이지에서 열어주세요.'); await wallet.connect(); const info = await wallet.getInfo(); setConnected(true); setInitialized(info.initialized); setStatus(info.initialized ? `${info.name} 연결됨 · 기존 지갑을 불러왔습니다.` : `${info.name} 연결됨 · 새 지갑을 생성하세요.`); });
  const generate = () => run(async () => { setWords(await wallet.generateMnemonic(256)); setStatus('복구 문구를 안전하게 백업한 뒤 보드에 저장하세요.'); });
  const confirm = () => run(async () => { if (!words) return; const eth = await wallet.confirmSetup(words); setInitialized(true); setAddresses(v => ({ ...v, ethereum: eth })); setWords(null); setStatus('보드 저장과 Ethereum 주소 생성이 완료됐습니다. 이제 Solana 주소도 생성할 수 있습니다.'); });
  const loadAddress = (chain: Chain) => run(async () => { const address = await wallet.getAddress(chain); setAddresses(v => ({ ...v, [chain]: address })); setStatus(`${chain === 'ethereum' ? 'Sepolia' : 'Solana Devnet'} 주소를 보드에서 파생했습니다.`); });
  const signTest = (chain: Chain) => run(async () => { const message = new TextEncoder().encode(`NuWallet ${chain} testnet verification`); setStatus('LED 순서대로 버튼을 눌러 승인하세요.'); const sig = chain === 'ethereum' ? await wallet.signMessage(CHAINS[0].path, message) : await wallet.signTransaction('solana', CHAINS[1].path, message); setSignatures(v => ({ ...v, [chain]: sig.serialized })); setStatus(`${chain === 'ethereum' ? 'Sepolia' : 'Solana Devnet'} 테스트 서명이 완료됐습니다.`); });

  return <main className="wallet-shell"><nav><div className="brand"><span className="brand-mark">NU</span><span>NuWallet Setup</span></div><button className={`connect ${connected ? 'is-connected' : ''}`} onClick={connect} disabled={busy || connected}>{connected ? '보드 연결됨' : busy ? '처리 중…' : 'Bluetooth 연결'}</button></nav>
    <header className="wallet-hero"><span className="kicker">NU-40 DK · MULTI-CHAIN HARDWARE WALLET</span><h1>키는 보드 안에.<br /><em>서명만 밖으로.</em></h1><p>하나의 복구 문구에서 체인 규격에 맞는 키를 독립적으로 파생합니다.</p></header>
    <section className="setup-panel"><div><span className="step">01</span><h2>{initialized ? '지갑 준비 완료' : words ? '복구 문구 백업 확인' : '지갑 초기화'}</h2><p>{initialized ? '개인키는 보드 안에 저장되어 있습니다.' : words ? '주소 생성 전에 반드시 아래 버튼으로 보드 저장을 완료하세요.' : 'BIP-39 엔트로피는 NU-40 DK의 CSPRNG가 생성합니다.'}</p></div><button onClick={generate} disabled={!connected || busy || !!words || initialized}>{initialized ? '지갑 생성됨' : '24단어 지갑 생성'}</button>{words && <div className="recovery"><strong>복구 문구 · 절대 공유하지 마세요</strong><ol>{indicesToMnemonic(words).split(' ').map((word, i) => <li key={i}><small>{i + 1}</small>{word}</li>)}</ol><button onClick={confirm} disabled={busy}>백업 확인 및 보드에 저장</button></div>}</section>
    <section className="chain-grid">{CHAINS.map(c => <article className="chain-card" key={c.id} style={{ '--chain': c.tone } as React.CSSProperties}><div className="chain-head"><span className="chain-dot" /><div><h2>{c.name}</h2><p>{c.curve}</p></div></div><dl><div><dt>테스트 네트워크</dt><dd>{c.network}</dd></div><div><dt>키 파생 경로 · chainId 아님</dt><dd>{c.path}</dd></div><div><dt>개인키</dt><dd>보드 외부 반출 금지</dd></div></dl><div className="address"><small>TESTNET ADDRESS</small><code>{addresses[c.id] ?? (initialized ? '주소 생성 버튼을 누르세요' : '지갑 저장이 먼저 필요합니다')}</code></div><div className="actions"><button onClick={() => loadAddress(c.id)} disabled={!connected || !initialized || busy}>주소 생성</button><button onClick={() => signTest(c.id)} disabled={!connected || !initialized || busy || !addresses[c.id]}>테스트 서명</button></div>{signatures[c.id] && <div className="signature"><small>TESTNET SIGNATURE</small><code>{signatures[c.id]}</code></div>}</article>)}</section>
    <aside className="device-status" aria-live="polite"><span className={connected ? 'online' : ''} /><strong>{status}</strong></aside><footer><span>NuWallet protocol v1</span><span>4-button physical approval</span><span>Prototype · 실제 자금 사용 금지</span></footer></main>;
}
