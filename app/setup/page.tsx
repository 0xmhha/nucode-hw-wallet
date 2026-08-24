'use client';
/**
 * 펌웨어 설정 웹 — 니모닉 가져오기, PIN 설정, 잠금 해제, 초기화.
 *
 * 지갑을 새로 만드는 것은 첫 화면(/)에서 하고, 여기서는 그 뒤에 필요한 것들을
 * 다룬다. PIN 은 **버튼 4개의 조합**이며, 아래 패드는 보드의 버튼 배치를 그대로
 * 옮겨 놓은 것이다.
 *
 * PIN 과 서명 챌린지는 다른 것이다 (docs/protocol.md §8).
 *   PIN    사용자가 정한 고정 시퀀스. 보드는 LED 로 보여주지 않는다.
 *   챌린지  보드가 매번 새로 뽑는 랜덤 시퀀스. LED 로 보여준다.
 */
import { useCallback, useMemo, useRef, useState } from 'react';
import Link from 'next/link';
import {
  NuWallet, NuWalletAdmin, WalletError, PIN,
  mnemonicToIndices, type LockState,
} from '@/sdk/src/index';
import s from './setup.module.css';
import { WebAppFooter } from '../components/WebAppFooter';

const BUTTON_LABELS = ['1', '2', '3', '4'];   // 화면 표기는 1~4, 프로토콜 값은 0~3

export default function SetupPage() {
  const wallet = useMemo(() => new NuWallet(), []);
  const admin = useMemo(() => new NuWalletAdmin(wallet), [wallet]);

  const offDisconnect = useRef<(() => void) | null>(null);
  const [connected, setConnected] = useState(false);
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState('보드를 연결하세요.');
  const [state, setState] = useState<LockState | null>(null);
  const [approval, setApproval] = useState<{ steps: number; done: number; kind: 'pin' | 'challenge' } | null>(null);

  const [passphrase, setPassphrase] = useState('');
  const [mnemonic, setMnemonic] = useState('');
  const [address, setAddress] = useState('');

  const [pin, setPin] = useState<number[]>([]);
  const [pinConfirm, setPinConfirm] = useState<number[]>([]);
  const [confirming, setConfirming] = useState(false);

  const run = useCallback(async (label: string, task: () => Promise<void>) => {
    setBusy(true);
    try {
      await task();
    } catch (e) {
      const msg = e instanceof WalletError ? `${label}: ${e.message}`
                : e instanceof Error ? `${label}: ${e.message}` : `${label} 실패`;
      setStatus(msg);
    } finally {
      setBusy(false);
      setApproval(null);
    }
  }, []);

  const refresh = useCallback(async () => {
    setState(await admin.getLockState());
  }, [admin]);

  const approvalCbs = (kind: 'pin' | 'challenge') => ({
    onStart: (i: { steps: number }) => {
      setApproval({ steps: i.steps, done: 0, kind });
      setStatus(kind === 'pin'
        ? `보드에서 PIN ${i.steps}자리를 누르세요. LED 는 시퀀스를 보여주지 않습니다.`
        : `보드가 LED 로 ${i.steps}자리를 보여줍니다. 그대로 누르세요.`);
    },
    onProgress: (i: { step: number }) => setApproval((a) => (a ? { ...a, done: i.step } : a)),
  });

  const connect = () => run('연결', async () => {
    if (!NuWallet.isSupported) {
      throw new Error('Web Bluetooth 를 지원하지 않습니다. Chrome/Edge 의 HTTPS 또는 localhost 에서 열어주세요.');
    }
    await wallet.connect();
    setConnected(true);          // 아래 조회가 실패해도 링크는 살아 있다
    if (!offDisconnect.current) {
      // connect() 마다 등록하면 재연결할 때마다 핸들러가 쌓인다.
      offDisconnect.current = wallet.onDisconnect(() => {
        setConnected(false); setState(null);
        setStatus('연결이 끊겼습니다. 보드는 자동으로 잠깁니다.');
      });
    }
    setStatus(`${wallet.deviceName || 'NuWallet'} 연결됨`);
    await refresh();
  });

  const unlock = () => run('잠금 해제', async () => {
    await admin.unlock(passphrase, approvalCbs('pin'));
    await refresh();
    setAddress(await wallet.getAddress('ethereum'));
    setStatus('잠금 해제됨');
  });

  const lock = () => run('잠금', async () => {
    await admin.lock();
    setAddress('');
    await refresh();
    setStatus('잠갔습니다. RAM 의 시드를 지웠습니다.');
  });

  const importMnemonic = () => run('니모닉 가져오기', async () => {
    const indices = mnemonicToIndices(mnemonic);     // 체크섬은 보드가 검증한다
    // 보드가 이어서 PIN 설정을 요구한다 — 6자리를 두 번 누른다.
    const addr = await wallet.restore(indices, approvalCbs('pin'));
    setAddress(addr);
    setMnemonic('');
    await refresh();
    setStatus(`복구 완료 — ${addr}`);
  });

  /* PIN 은 이 화면에서 입력하지 않는다.
   *
   * 값을 웹에서 받아 보내면 호스트가 PIN 을 알게 되는데, PIN 의 목적이
   * "호스트가 감염돼도 기기를 못 연다" 이므로 앞뒤가 맞지 않는다. v2 부터
   * 사용자는 보드의 버튼으로 6자리를 두 번 누른다. 여기서는 절차를 시작시키고
   * 진행 상황만 보여준다. */
  const changePin = () => run('PIN 변경', async () => {
    setStatus('보드에서 새 PIN 6자리를 누르고, 같은 값을 한 번 더 누르세요.');
    await admin.changePin(approvalCbs('pin'));
    await refresh();
    setStatus('PIN 을 바꿨습니다.');
  });

  const wipe = () => run('초기화', async () => {
    if (!window.confirm('보드의 지갑을 지웁니다. 니모닉이 없으면 복구할 수 없습니다. 계속할까요?')) return;
    await wallet.wipe(approvalCbs('challenge'));
    setAddress('');
    await refresh();
    setStatus('보드를 초기화했습니다.');
  });

  const locked = !!state?.locked;
  const words = mnemonic.trim() ? mnemonic.trim().split(/\s+/u).length : 0;

  return (
    <main className={s.shell}>
      <nav className={s.nav}>
        <div className={s.brand}><span className={s.mark}>NU</span><span>NuWallet 기기 설정</span></div>
        <div className={s.links}>
          <Link href="/">지갑</Link>
          <Link href="/setup">설정</Link>
          <Link href="/dapp">트랜잭션 테스트</Link>
          <Link href="/debug">BLE 디버그</Link>
        </div>
      </nav>

      <header className={s.hero}>
        <span className={s.kicker}>DEVICE CONFIGURATION</span>
        <h1>가져오고,<br /><em>PIN 을 걸고,</em> 잠근다.</h1>
        <p>
          기존 니모닉 가져오기, 버튼 4개 조합으로 PIN 설정, BIP-39 패스프레이즈,
          초기화까지. 모든 위험한 동작은 보드에서 물리 버튼으로 한 번 더 승인해야
          실행됩니다 — 웹에서 보내는 것만으로는 아무것도 바뀌지 않습니다.
        </p>
      </header>

      <p className={s.warn}>
        <strong>니모닉과 PIN 이 BLE 로 평문 전송됩니다.</strong> 보드에 입력 UI 가 없어
        달리 방법이 없습니다. 감염된 PC 에서는 안전하지 않습니다.
        잠금 해제 시의 PIN 입력만은 <code>보드 버튼으로만</code> 받으며 밖으로 나가지
        않습니다. <code>SECURITY.md</code> 를 읽으세요.
      </p>

      <div className={s.grid}>
        <section className={s.card}>
          <h2>기기 상태</h2>
          <p className={s.hint}>보드가 알려주는 값 그대로입니다.</p>
          <dl className={s.kv}>
            <div><dt>연결</dt><dd className={connected ? s.yes : s.no}>{connected ? wallet.deviceName || '연결됨' : '미연결'}</dd></div>
            <div><dt>지갑</dt><dd className={state?.initialized ? s.yes : s.no}>{state ? (state.initialized ? '있음' : '없음') : '—'}</dd></div>
            <div><dt>잠금</dt><dd className={locked ? s.no : s.yes}>{state ? (locked ? '잠김' : '해제됨') : '—'}</dd></div>
            <div><dt>PIN</dt><dd>{state ? (state.hasPin ? `설정됨 · ${state.pinLength}자리` : '없음') : '—'}</dd></div>
            <div><dt>남은 시도</dt><dd>{state ? state.attemptsLeft : '—'}</dd></div>
            <div><dt>주소</dt><dd>{address || '—'}</dd></div>
          </dl>
          <div className={s.buttons}>
            <button className={`${s.btn} ${s.primary}`} onClick={connect} disabled={busy || connected}>
              {connected ? '연결됨' : 'Bluetooth 연결'}
            </button>
            <button className={s.btn} onClick={() => run('상태 조회', refresh)} disabled={busy || !connected}>상태 갱신</button>
            <button className={s.btn} onClick={lock} disabled={busy || !connected || locked}>잠그기</button>
          </div>
        </section>

        <section className={s.card}>
          <h2>잠금 해제</h2>
          <p className={s.hint}>
            PIN 이 설정되어 있으면 보드에서 직접 눌러야 합니다. 패스프레이즈는 보드에
            저장되지 않고 이번 세션에만 쓰입니다 — 값이 다르면 다른 지갑이 됩니다.
          </p>
          <label className={s.field}>
            <span>BIP-39 패스프레이즈 (선택)</span>
            <input type="password" value={passphrase} onChange={(e) => setPassphrase(e.target.value)}
                   placeholder="비워 두면 패스프레이즈 없음" autoComplete="off" />
          </label>
          <div className={s.buttons}>
            <button className={`${s.btn} ${s.primary}`} onClick={unlock} disabled={busy || !connected || !state?.initialized}>
              잠금 해제
            </button>
          </div>
          {approval && (
            <div className={s.approval}>
              <strong>
                {approval.kind === 'pin' ? 'PIN 입력을 기다립니다' : '보드에서 승인을 기다립니다'}
                {' '}({approval.done}/{approval.steps})
              </strong>
              <div className={s.dots}>
                {Array.from({ length: approval.steps }, (_, i) => (
                  <span key={i} className={`${s.dot} ${i < approval.done ? s.on : ''}`} />
                ))}
              </div>
            </div>
          )}
        </section>

        <section className={s.card}>
          <h2>PIN · 버튼 6번</h2>
          <p className={s.hint}>
            PIN 은 <strong>보드에서만</strong> 입력합니다. 이 화면은 값을 받지 않습니다 —
            웹이 PIN 을 알면 &ldquo;호스트가 감염돼도 기기를 못 연다&rdquo;는 목적이 무너지기 때문입니다.
            아래 패드는 보드의 버튼 배치를 보여줄 뿐, 누르는 곳은 보드입니다.
          </p>
          <div className={s.pad}>
            {BUTTON_LABELS.map((label, i) => (
              <div key={label} className={s.key} aria-hidden="true">
                {label}<small>SW{i}</small>
              </div>
            ))}
          </div>
          <p className={s.hint}>
            PIN 은 {PIN.LEN}자리 고정입니다. 바꿀 때는 보드에서 새 값을 누르고, 오타를 잡기 위해
            같은 값을 한 번 더 누릅니다. PIN 은 없앨 수 없습니다.
          </p>
          <div className={s.buttons}>
            <button className={`${s.btn} ${s.primary}`} onClick={changePin}
                    disabled={busy || !connected || locked || !state?.initialized}>
              PIN 변경
            </button>
          </div>
          <p className={s.hint}>
            PIN 을 잊었다면 보드에서 <strong>공장 초기화</strong>를 합니다 — 버튼 1과 4를 5초 동안
            함께 누른 뒤, LED 카운트다운이 끝나면 손을 떼고 버튼 2 → 3 을 누릅니다.
            지갑과 페어링 정보가 지워지며, 복구 문구로만 되살릴 수 있습니다.
          </p>
        </section>

        <section className={s.card}>
          <h2>기존 니모닉 가져오기</h2>
          <p className={s.hint}>
            보드에 지갑이 없을 때만 됩니다. 체크섬은 보드가 검증합니다.
            오프라인에서 만든 니모닉을 넣는 편이 <code>새로 생성</code>보다 안전합니다 —
            PC 가 니모닉 전체를 보지 않기 때문입니다.
          </p>
          <label className={s.field}>
            <span>니모닉 12 / 15 / 18 / 21 / 24 단어 {words > 0 && `· 지금 ${words}개`}</span>
            <textarea value={mnemonic} onChange={(e) => setMnemonic(e.target.value)}
                      placeholder="abandon abandon … about" autoComplete="off" spellCheck={false} />
          </label>
          <div className={s.buttons}>
            <button className={`${s.btn} ${s.primary}`} onClick={importMnemonic}
                    disabled={busy || !connected || !mnemonic.trim() || state?.initialized}>
              가져오기
            </button>
            <button className={`${s.btn} ${s.danger}`} onClick={wipe}
                    disabled={busy || !connected || !state?.initialized}>
              보드 초기화 (WIPE)
            </button>
          </div>
          {state?.initialized && (
            <p className={s.hint} style={{ marginTop: 14, marginBottom: 0 }}>
              이미 지갑이 있습니다. 다른 니모닉을 넣으려면 먼저 초기화하세요.
            </p>
          )}
        </section>
      </div>

      <aside className={s.status} aria-live="polite">
        <span className={`${s.led} ${connected ? s.online : ''}`} />
        <strong>{status}</strong>
      </aside>
      <WebAppFooter />
    </main>
  );
}
