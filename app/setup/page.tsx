'use client';
/**
 * 펌웨어 설정 웹 — 니모닉 가져오기, PIN 변경, 잠금 해제, 초기화.
 *
 * 지갑을 새로 만드는 것은 첫 화면(/)에서 하고, 여기서는 그 뒤에 필요한 것들을
 * 다룬다. PIN 은 **버튼 4개의 조합**이며 보드에서만 입력한다.
 *
 * PIN 과 서명 확인은 다른 것이다 (docs/protocol.md §8).
 *   PIN     사용자가 정한 고정 시퀀스. 보드는 누른 개수만 LED 로 보여준다.
 *   확인    보드가 LED 하나를 켜고, 사용자가 그 옆 버튼을 한 번 누른다.
 * 어느 쪽인지는 보드가 알려 주므로 (`lib/approval.ts`) 여기서 정하지 않는다.
 *
 * 화면 조각은 `components/` 에 있다. 이 파일은 상태를 들고 버튼과 SDK 호출을 잇는다.
 */
import { useCallback, useMemo, useRef, useState } from 'react';
import Link from 'next/link';
import { NuWallet, NuWalletAdmin, mnemonicToIndices, type LockState } from '@/sdk/src/index';
import s from './setup.module.css';
import { WebAppFooter } from '../components/WebAppFooter';
import { useApproval } from '../lib/approval';
import { useTask } from '../lib/task';
import { DeviceCard } from './components/DeviceCard';
import { UnlockCard } from './components/UnlockCard';
import { PinCard } from './components/PinCard';
import { ImportCard } from './components/ImportCard';

export default function SetupPage() {
  const wallet = useMemo(() => new NuWallet(), []);
  const admin = useMemo(() => new NuWalletAdmin(wallet), [wallet]);

  const offDisconnect = useRef<(() => void) | null>(null);
  const [connected, setConnected] = useState(false);
  const [status, setStatus] = useState('보드를 연결하세요.');
  const [state, setState] = useState<LockState | null>(null);

  const [passphrase, setPassphrase] = useState('');
  const [mnemonic, setMnemonic] = useState('');
  const [address, setAddress] = useState('');

  const { approval, callbacks, clear } = useApproval(setStatus);
  const onError = useCallback((label: string, msg: string) => setStatus(`${label}: ${msg}`), []);
  const { busy, run } = useTask({ onError, onSettled: clear });

  const refresh = useCallback(async () => {
    setState(await admin.getLockState());
  }, [admin]);

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
    await admin.unlock(passphrase, callbacks());
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
    const addr = await wallet.restore(indices, callbacks());
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
    await admin.changePin(callbacks());
    await refresh();
    setStatus('PIN 을 바꿨습니다.');
  });

  const wipe = () => run('초기화', async () => {
    if (!window.confirm('보드의 지갑을 지웁니다. 니모닉이 없으면 복구할 수 없습니다. 계속할까요?')) return;
    await wallet.wipe(callbacks());
    setAddress('');
    await refresh();
    setStatus('보드를 초기화했습니다.');
  });

  const locked = !!state?.locked;
  const initialized = !!state?.initialized;

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
          기존 니모닉 가져오기, 버튼 4개 조합으로 PIN 변경, BIP-39 패스프레이즈,
          초기화까지. 모든 위험한 동작은 보드에서 물리 버튼으로 한 번 더 승인해야
          실행됩니다 — 웹에서 보내는 것만으로는 아무것도 바뀌지 않습니다.
        </p>
      </header>

      <p className={s.warn}>
        <strong>니모닉이 BLE 로 평문 전송됩니다.</strong> 보드에 입력 UI 가 없어
        달리 방법이 없습니다. 감염된 PC 에서는 안전하지 않습니다.
        PIN 은 <code>보드 버튼으로만</code> 받으며 밖으로 나가지 않습니다.
        <code>SECURITY.md</code> 를 읽으세요.
      </p>

      <div className={s.grid}>
        <DeviceCard
          connected={connected} deviceName={wallet.deviceName} state={state} address={address}
          busy={busy} onConnect={connect} onRefresh={() => run('상태 조회', refresh)} onLock={lock} />
        <UnlockCard
          passphrase={passphrase} onPassphrase={setPassphrase}
          canUnlock={connected && initialized} busy={busy} onUnlock={unlock} approval={approval} />
        <PinCard canChange={connected && !locked && initialized} busy={busy} onChange={changePin} />
        <ImportCard
          mnemonic={mnemonic} onMnemonic={setMnemonic}
          connected={connected} initialized={initialized} busy={busy}
          onImport={importMnemonic} onWipe={wipe} />
      </div>

      <aside className={s.status} aria-live="polite">
        <span className={`${s.led} ${connected ? s.online : ''}`} />
        <strong>{status}</strong>
      </aside>
      <WebAppFooter />
    </main>
  );
}
