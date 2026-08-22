'use client';
import { useMemo, useState } from 'react';
import Link from 'next/link';
import { BleTransport, NuWallet, WalletError, SW, indicesToMnemonic, TESTNET, type Chain } from '@/sdk/src/index';
import { WebAppFooter } from './components/WebAppFooter';

/* `ready: false` 인 체인은 펌웨어가 아직 처리하지 못한다 (UNSUPPORTED_CHAIN 을
 * 돌려준다). 버튼을 눌러 보고 오류를 만나는 대신 카드에 미리 표시한다.
 * Solana 는 파생 경로만 있고 Ed25519 서명 구현이 펌웨어에 없다. */
const CHAINS: Array<{ id: Chain; name: string; network: string; curve: string; path: string; tone: string; ready: boolean }> = [
  { id: 'ethereum', name: 'Ethereum', network: `Base Sepolia · chainId ${TESTNET.ethereum.chainId}`, curve: 'secp256k1 · ECDSA', path: "m/44'/60'/0'/0/0", tone: '#6574c4', ready: true },
  { id: 'solana', name: 'Solana', network: 'Testnet · cluster testnet', curve: 'Ed25519 · SLIP-0010', path: "m/44'/501'/0'/0'", tone: '#14f195', ready: false },
];

const TX_EXAMPLES: Record<Chain, { title: string; fields: Array<[string, string]> }> = {
  ethereum: {
    title: 'Base Sepolia 전송 예시',
    fields: [['chainId', String(TESTNET.ethereum.chainId)], ['to', '0x3FFd98B1f972043a824F05E6821e793B26c72794'], ['value', '0.001 ETH'], ['gas', '21,000']],
  },
  solana: {
    title: 'Solana Testnet 전송 예시',
    fields: [['cluster', 'testnet'], ['to', '11111111111111111111111111111111'], ['amount', '0.01 SOL'], ['program', 'SystemProgram.transfer']],
  },
};

export default function Home() {
  const wallet = useMemo(() => new NuWallet(), []);
  const [connected, setConnected] = useState(false), [busy, setBusy] = useState(false);
  const [initialized, setInitialized] = useState(false);
  const [status, setStatus] = useState('보드를 연결해 설정을 시작하세요.');
  const [words, setWords] = useState<number[] | null>(null);
  const [addresses, setAddresses] = useState<Partial<Record<Chain, string>>>({});
  const [signatures, setSignatures] = useState<Partial<Record<Chain, string>>>({});
  async function run(task: () => Promise<void>) { setBusy(true); try { await task(); } catch (e) { if (e instanceof WalletError && e.status === SW.NOT_INITIALIZED) setStatus('먼저 복구 문구를 생성하고 “백업 확인 및 보드에 저장”을 눌러주세요.'); else setStatus(e instanceof Error ? e.message : '요청 실패'); } finally { setBusy(false); } }
  const connect = () => run(async () => { if (!BleTransport.isSupported) throw new Error('Chrome 또는 Edge의 HTTPS 페이지에서 열어주세요.'); await wallet.connect(); setConnected(true); /* 링크가 살아 있다는 사실을 먼저 반영한다. 아래 조회가 실패해도 보드는 연결된 채이고, 화면만 '미연결' 로 남으면 사용자는 다시 연결하려 하지만 연결된 보드는 광고를 멈춘 상태라 선택 창이 비어 보인다. */ const info = await wallet.getInfo(); setInitialized(info.initialized); setStatus(info.initialized ? `${info.name} 연결됨 · 기존 지갑을 불러왔습니다.` : `${info.name} 연결됨 · 새 지갑을 생성하세요.`); });
  const generate = () => run(async () => { setWords(await wallet.generateMnemonic(256)); setStatus('복구 문구를 안전하게 백업한 뒤 보드에 저장하세요.'); });
  const confirm = () => run(async () => { if (!words) return; const eth = await wallet.confirmSetup(words); setInitialized(true); setAddresses(v => ({ ...v, ethereum: eth })); setWords(null); setStatus('보드 저장과 Ethereum 주소 생성이 완료됐습니다.'); });
  const loadAddress = (chain: Chain) => run(async () => { const address = await wallet.getAddress(chain); setAddresses(v => ({ ...v, [chain]: address })); setStatus(`${chain === 'ethereum' ? 'Base Sepolia' : 'Solana Testnet'} 주소를 보드에서 파생했습니다.`); });
  const signTest = (chain: Chain) => run(async () => { const message = new TextEncoder().encode(`NuWallet ${chain} testnet verification`); setStatus('버튼을 1 → 2 → 3 → 4 순서로 눌러 승인하세요.'); const sig = chain === 'ethereum' ? await wallet.signMessage(CHAINS[0].path, message) : await wallet.signTransaction('solana', CHAINS[1].path, message); setSignatures(v => ({ ...v, [chain]: sig.serialized })); setStatus(`${chain === 'ethereum' ? 'Base Sepolia' : 'Solana Testnet'} 테스트 서명이 완료됐습니다.`); });

  return <main className="wallet-shell"><nav><div className="brand"><span className="brand-mark">NU</span><span>NuWallet Setup</span></div><div className="nav-links"><Link href="/">지갑</Link><Link href="/setup">설정</Link><Link href="/dapp">트랜잭션 테스트</Link><Link href="/debug">BLE 디버그</Link></div><button className={`connect ${connected ? 'is-connected' : ''}`} onClick={connect} disabled={busy || connected}>{connected ? '보드 연결됨' : busy ? '처리 중…' : 'Bluetooth 연결'}</button></nav>
    <header className="wallet-hero"><span className="kicker">NU-40 DK · MULTI-CHAIN HARDWARE WALLET</span><h1>키는 보드 안에.<br /><em>서명만 밖으로.</em></h1><p>하나의 복구 문구에서 체인 규격에 맞는 키를 독립적으로 파생합니다.</p></header>
    <section className="setup-panel"><div><span className="step">01</span><h2>{initialized ? '지갑 준비 완료' : words ? '복구 문구 백업 확인' : '지갑 초기화'}</h2><p>{initialized ? '개인키는 보드 안에 저장되어 있습니다.' : words ? '주소 생성 전에 반드시 아래 버튼으로 보드 저장을 완료하세요.' : 'BIP-39 엔트로피는 NU-40 DK의 CSPRNG가 생성합니다.'}</p></div><button onClick={generate} disabled={!connected || busy || !!words || initialized}>{initialized ? '지갑 생성됨' : '24단어 지갑 생성'}</button>{words && <div className="recovery"><strong>복구 문구 · 절대 공유하지 마세요</strong><ol>{indicesToMnemonic(words).split(' ').map((word, i) => <li key={i}><small>{i + 1}</small>{word}</li>)}</ol><button onClick={confirm} disabled={busy}>백업 확인 및 보드에 저장</button></div>}</section>
    <section className="chain-grid">{CHAINS.map(c => <article className="chain-card" key={c.id} style={{ '--chain': c.tone } as React.CSSProperties}><div className="chain-head"><span className="chain-dot" /><div><h2>{c.name}{!c.ready && <em className="chain-badge">펌웨어 준비 중</em>}</h2><p>{c.curve}</p></div></div><dl><div><dt>테스트 네트워크</dt><dd>{c.network}</dd></div><div><dt>키 파생 경로 · chainId 아님</dt><dd>{c.path}</dd></div><div><dt>개인키</dt><dd>보드 외부 반출 금지</dd></div></dl><div className="address"><small>PRIVATE KEY ACCOUNT ADDRESS</small><code>{addresses[c.id] ?? (!c.ready ? '이 체인은 아직 펌웨어가 지원하지 않습니다' : initialized ? '계정 주소 생성 버튼을 누르세요' : '지갑 저장이 먼저 필요합니다')}</code><p>보드 내부 개인키에서 파생한 공개 계정 주소입니다. 개인키 자체는 전송되지 않습니다.</p></div><div className="actions"><button onClick={() => loadAddress(c.id)} disabled={!c.ready || !connected || !initialized || busy}>계정 주소 생성</button><button onClick={() => signTest(c.id)} disabled={!c.ready || !connected || !initialized || busy}>테스트 서명</button></div>{signatures[c.id] && <div className="signature"><small>TESTNET SIGNATURE</small><code>{signatures[c.id]}</code></div>}<section className="tx-example"><small>TRANSACTION EXAMPLE</small><h3>{TX_EXAMPLES[c.id].title}</h3><dl>{TX_EXAMPLES[c.id].fields.map(([label, value]) => <div key={label}><dt>{label}</dt><dd>{value}</dd></div>)}<div><dt>from</dt><dd>{addresses[c.id] ?? '위에서 계정 주소 생성'}</dd></div></dl><p>트랜잭션 구성 → 보드에 서명 요청 → LED 순서대로 버튼 승인 → 서명된 트랜잭션을 테스트넷 RPC로 전송</p></section></article>)}</section>
    <aside className="device-status" aria-live="polite"><span className={connected ? 'online' : ''} /><strong>{status}</strong></aside><WebAppFooter /></main>;
}
