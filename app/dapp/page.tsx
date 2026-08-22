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
 */
import { useCallback, useMemo, useRef, useState } from 'react';
import Link from 'next/link';
import { NuWallet, NuWalletProvider, WalletError } from '@/sdk/src/index';
import s from './dapp.module.css';

/* 체인 프리셋. RPC 는 사용자가 직접 넣게 둔다 — 공개 엔드포인트를 코드에
 * 박아 두면 금방 죽고, 데모용 키를 넣어 두면 그게 곧 유출이다. */
const PRESETS = [
  { name: 'Sepolia 테스트넷', chainId: 11155111, rpc: 'https://ethereum-sepolia-rpc.publicnode.com', symbol: 'ETH' },
  { name: 'Holesky 테스트넷', chainId: 17000, rpc: 'https://ethereum-holesky-rpc.publicnode.com', symbol: 'ETH' },
  { name: '로컬 노드 (anvil/hardhat)', chainId: 31337, rpc: 'http://127.0.0.1:8545', symbol: 'ETH' },
  { name: 'Ethereum 메인넷', chainId: 1, rpc: '', symbol: 'ETH' },
];

type LogLine = { dir: 'in' | 'out' | 'err'; text: string };

export default function DappDemo() {
  const wallet = useMemo(() => new NuWallet(), []);

  const [presetIdx, setPresetIdx] = useState(0);
  const [chainId, setChainId] = useState(String(PRESETS[0]!.chainId));
  const [rpcUrl, setRpcUrl] = useState(PRESETS[0]!.rpc);
  const [path, setPath] = useState("m/44'/60'/0'/0/0");

  const [account, setAccount] = useState('');
  const [balance, setBalance] = useState('');
  const [nonce, setNonce] = useState('');
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState('보드를 연결하고 체인을 고르세요.');

  const [to, setTo] = useState('');
  const [value, setValue] = useState('0.001');
  const [data, setData] = useState('');
  const [message, setMessage] = useState('NuWallet 로 서명한 메시지');

  const [approval, setApproval] = useState<{ steps: number; done: number } | null>(null);
  const [log, setLog] = useState<LogLine[]>([]);

  const providerRef = useRef<NuWalletProvider | null>(null);

  const say = useCallback((dir: LogLine['dir'], text: string) => {
    setLog((l) => [...l.slice(-60), { dir, text }]);
  }, []);

  /** 설정이 바뀌면 provider 를 새로 만든다. chainId 는 서명에 들어가므로
   *  옛 설정으로 서명하는 일이 없어야 한다. */
  const provider = useCallback(() => {
    const p = new NuWalletProvider(wallet, {
      chainId: Number(chainId) || 0,
      rpcUrl: rpcUrl || undefined,
      path,
      onStart: (i) => {
        setApproval({ steps: i.steps, done: 0 });
        setStatus(`보드가 LED 로 ${i.steps}자리 시퀀스를 보여줍니다. 그대로 누르세요.`);
      },
      onProgress: (i) => setApproval((a) => (a ? { ...a, done: i.step } : a)),
    });
    providerRef.current = p;
    return p;
  }, [wallet, chainId, rpcUrl, path]);

  const run = useCallback(async (label: string, task: () => Promise<void>) => {
    setBusy(true);
    say('out', `→ ${label}`);
    try {
      await task();
    } catch (e) {
      const msg = e instanceof WalletError ? `${e.message} (0x${e.status.toString(16)})`
                : e instanceof Error ? e.message : String(e);
      say('err', `✗ ${msg}`);
      setStatus(msg);
    } finally {
      setBusy(false);
      setApproval(null);
    }
  }, [say]);

  function applyPreset(i: number) {
    const p = PRESETS[i];
    if (!p) return;
    setPresetIdx(i);
    setChainId(String(p.chainId));
    setRpcUrl(p.rpc);
  }

  const connect = () => run('지갑 연결', async () => {
    if (!NuWallet.isSupported) {
      throw new Error('Web Bluetooth 를 지원하지 않습니다. Chrome/Edge 의 HTTPS 또는 localhost 에서 열어주세요.');
    }
    const p = provider();
    const accounts = await p.request({ method: 'eth_requestAccounts' }) as string[];
    const addr = accounts[0] ?? '';
    setAccount(addr);
    say('in', `계정 ${addr}`);
    setStatus(`연결됨 — ${wallet.deviceName || 'NuWallet'}`);
    await refresh(p, addr);
  });

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

  const reload = () => run('상태 갱신', async () => {
    const p = providerRef.current ?? provider();
    if (!account) throw new Error('먼저 지갑을 연결하세요.');
    await refresh(p, account);
    setStatus('최신 상태로 갱신했습니다.');
  });

  /** 서명만 하고 보내지 않는다. 무엇이 서명되는지 눈으로 보라고 넣은 경로다. */
  const signOnly = () => run('eth_signTransaction (전송 안 함)', async () => {
    const raw = await providerOrThrow().request({
      method: 'eth_signTransaction', params: [txParams()],
    }) as string;
    say('in', `서명된 raw 트랜잭션\n${raw}`);
    setStatus('서명 완료 — 아직 브로드캐스트하지 않았습니다.');
  });

  const send = () => run('eth_sendTransaction', async () => {
    const hash = await providerOrThrow().request({
      method: 'eth_sendTransaction', params: [txParams()],
    }) as string;
    say('in', `트랜잭션 해시 ${hash}`);
    setStatus(`전송됨 — ${hash}`);
    if (account) await refresh(providerOrThrow(), account);
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

  function providerOrThrow(): NuWalletProvider {
    if (!account) throw new Error('먼저 지갑을 연결하세요.');
    return providerRef.current ?? provider();
  }

  function txParams() {
    const t: Record<string, string> = { from: account };
    if (to.trim()) t.to = to.trim();
    if (data.trim()) t.data = data.trim();
    t.value = '0x' + parseEther(value).toString(16);
    return t;
  }

  const preset = PRESETS[presetIdx]!;

  return (
    <main className={s.shell}>
      <nav className={s.nav}>
        <div className={s.brand}>
          <span className={s.mark}>NU</span>
          <span>NuWallet 예제 DApp</span>
        </div>
        <Link href="/" className={s.back}>← 기기 설정으로</Link>
      </nav>

      <header className={s.hero}>
        <span className={s.kicker}>EXAMPLE DAPP · EIP-1193</span>
        <h1>연결하고,<br /><em>버튼으로 승인하고,</em> 보낸다.</h1>
        <p>
          이 페이지는 <code>@nucode/hw-wallet</code> 의 <code>NuWalletProvider</code> 하나만
          씁니다. 트랜잭션을 만들어 보드에 보내면, 보드가 LED 로 승인 시퀀스를 띄우고
          사용자가 버튼을 누른 뒤에야 서명이 돌아옵니다. 개인키는 보드를 떠나지 않습니다.
        </p>
      </header>

      <p className={s.warn}>
        <strong>테스트넷만 쓰세요.</strong> 프로토타입이라 상용 하드웨어 지갑의 방어 수단이
        없습니다. 기기에 화면이 없어 <code>무엇에 서명하는지</code> 기기가 보여주지
        못합니다 — 화면에 뜬 내용과 보드가 서명하는 내용이 같다는 보장이 없습니다.
        자세한 것은 <code>SECURITY.md</code> 를 읽으세요.
      </p>

      <div className={s.grid}>
        <section className={s.card}>
          <h2>1. 체인 설정</h2>
          <p className={s.hint}>
            chainId 는 EIP-155 서명에 그대로 들어갑니다. 잘못 넣으면 다른 체인에서
            재생 가능한 서명이 됩니다.
          </p>
          <label className={s.field}>
            <span>프리셋</span>
            <select value={presetIdx} onChange={(e) => applyPreset(Number(e.target.value))}>
              {PRESETS.map((p, i) => <option key={p.chainId} value={i}>{p.name} · {p.chainId}</option>)}
            </select>
          </label>
          <div className={s.row}>
            <label className={s.field}>
              <span>CHAIN ID</span>
              <input value={chainId} onChange={(e) => setChainId(e.target.value)} inputMode="numeric" />
            </label>
            <label className={s.field}>
              <span>파생 경로</span>
              <input value={path} onChange={(e) => setPath(e.target.value)} />
            </label>
          </div>
          <label className={s.field}>
            <span>RPC URL</span>
            <input value={rpcUrl} onChange={(e) => setRpcUrl(e.target.value)}
                   placeholder="https://… (서명 외 호출을 여기로 넘깁니다)" />
          </label>
          <div className={s.buttons}>
            <button className={`${s.btn} ${s.primary}`} onClick={connect} disabled={busy || !!account}>
              {account ? '연결됨' : '지갑 연결'}
            </button>
            <button className={s.btn} onClick={reload} disabled={busy || !account}>상태 갱신</button>
          </div>
        </section>

        <section className={s.card}>
          <h2>2. 계정</h2>
          <p className={s.hint}>주소는 보드가 파생합니다. 조회에는 버튼 승인이 필요 없습니다.</p>
          <dl className={s.kv}>
            <div><dt>주소</dt><dd>{account || '미연결'}</dd></div>
            <div><dt>잔액</dt><dd>{balance ? `${balance} ${preset.symbol}` : (rpcUrl ? '—' : 'RPC 미설정')}</dd></div>
            <div><dt>nonce</dt><dd>{nonce || '—'}</dd></div>
            <div><dt>기기</dt><dd>{wallet.deviceName || '—'}</dd></div>
          </dl>
          {approval && (
            <div className={s.approval}>
              <strong>보드에서 승인을 기다립니다 ({approval.done}/{approval.steps})</strong>
              <div className={s.dots}>
                {Array.from({ length: approval.steps }, (_, i) => (
                  <span key={i} className={`${s.dot} ${i < approval.done ? s.on : ''}`} />
                ))}
              </div>
            </div>
          )}
        </section>

        <section className={s.card}>
          <h2>3. 트랜잭션 전송</h2>
          <p className={s.hint}>
            nonce · gasPrice · gas 는 비워 두면 RPC 에서 채웁니다.
            서명 대상은 <code>RLP([nonce,gasPrice,gas,to,value,data,chainId,0,0])</code> 원문이고,
            <strong> 해시는 보드가 직접 계산</strong>합니다.
          </p>
          <label className={s.field}>
            <span>받는 주소</span>
            <input value={to} onChange={(e) => setTo(e.target.value)} placeholder="0x… (비우면 컨트랙트 생성)" />
          </label>
          <div className={s.row}>
            <label className={s.field}>
              <span>보낼 양 ({preset.symbol})</span>
              <input value={value} onChange={(e) => setValue(e.target.value)} inputMode="decimal" />
            </label>
          </div>
          <label className={s.field}>
            <span>DATA (선택)</span>
            <textarea value={data} onChange={(e) => setData(e.target.value)} placeholder="0x…" />
          </label>
          <div className={s.buttons}>
            <button className={s.btn} onClick={signOnly} disabled={busy || !account}>서명만 (전송 안 함)</button>
            <button className={`${s.btn} ${s.primary}`} onClick={send} disabled={busy || !account || !rpcUrl}>
              서명하고 전송
            </button>
          </div>
        </section>

        <section className={s.card}>
          <h2>4. 메시지 서명</h2>
          <p className={s.hint}>
            <code>personal_sign</code> (EIP-191). 보드가 접두사를 붙여 해시하므로 호스트가
            준 해시를 그대로 서명하지 않습니다.
          </p>
          <label className={s.field}>
            <span>메시지</span>
            <textarea value={message} onChange={(e) => setMessage(e.target.value)} />
          </label>
          <div className={s.buttons}>
            <button className={s.btn} onClick={personalSign} disabled={busy || !account}>
              메시지 서명
            </button>
          </div>
        </section>

        <section className={`${s.card} ${s.wide}`}>
          <h2>주고받은 것</h2>
          <p className={s.hint}>DApp ↔ SDK ↔ 보드 사이에 실제로 오간 호출입니다.</p>
          <pre className={s.log}>
            {log.length === 0 ? '아직 아무것도 하지 않았습니다.' : log.map((l, i) => (
              <span key={i} className={l.dir === 'err' ? s.err : l.dir === 'in' ? s.out : undefined}>
                {l.text}{'\n'}
              </span>
            ))}
          </pre>
        </section>
      </div>

      <aside className={s.status} aria-live="polite">
        <span className={`${s.led} ${account ? s.online : ''}`} />
        <strong>{status}</strong>
      </aside>
    </main>
  );
}

/* ── 단위 변환 ────────────────────────────────────────────────────────────
   ethers 를 끌어오지 않는다. 이 페이지가 필요한 건 wei 왕복 하나뿐이고,
   SDK 를 의존성 없이 쓸 수 있다는 걸 보여주는 것도 예제의 목적이다.        */

function parseEther(v: string): bigint {
  const t = v.trim();
  if (!t) return 0n;
  if (!/^\d*\.?\d*$/.test(t)) throw new Error(`숫자가 아닙니다: ${v}`);
  const [whole = '0', frac = ''] = t.split('.');
  if (frac.length > 18) throw new Error('wei 보다 작은 단위는 없습니다 (소수점 18자리까지)');
  return BigInt(whole || '0') * 10n ** 18n + BigInt((frac + '0'.repeat(18)).slice(0, 18) || '0');
}

function formatEther(wei: bigint): string {
  const neg = wei < 0n;
  const abs = neg ? -wei : wei;
  const whole = abs / 10n ** 18n;
  const frac = (abs % 10n ** 18n).toString().padStart(18, '0').replace(/0+$/, '');
  return `${neg ? '-' : ''}${whole}${frac ? '.' + frac : ''}`;
}
