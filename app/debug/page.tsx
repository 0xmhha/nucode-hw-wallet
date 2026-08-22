'use client';

import Link from 'next/link';
import { useEffect, useMemo, useState } from 'react';
import { CMD, NuWallet, hex, type BleTraceEntry } from '@/sdk/src/index';
import s from './debug.module.css';
import { WebAppFooter } from '../components/WebAppFooter';

const COMMANDS = Object.entries(CMD).map(([name, value]) => ({ name, value }));

export default function BluetoothDebugPage() {
  const wallet = useMemo(() => new NuWallet(), []);
  const [entries, setEntries] = useState<BleTraceEntry[]>([]);
  const [connected, setConnected] = useState(false);
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState('추적을 시작하려면 NU-40 DK를 연결하세요.');
  const [command, setCommand] = useState<number>(CMD.GET_VERSION);
  const [payload, setPayload] = useState('');
  const [showPackets, setShowPackets] = useState(true);

  useEffect(() => {
    const offTrace = wallet.transport.onTrace((entry) => {
      setEntries((current) => [...current.slice(-499), entry]);
    });
    const offDisconnect = wallet.onDisconnect(() => {
      setConnected(false);
      setStatus('보드 연결이 끊겼습니다.');
    });
    return () => { offTrace(); offDisconnect(); };
  }, [wallet]);

  async function run(task: () => Promise<void>) {
    setBusy(true);
    try { await task(); }
    catch (error) { setStatus(error instanceof Error ? error.message : '요청 실패'); }
    finally { setBusy(false); }
  }

  const connect = () => run(async () => {
    await wallet.connect();
    setConnected(true);
    setStatus(`${wallet.deviceName || 'NuWallet'} 연결됨 · BLE 추적 중`);
  });

  const getInfo = () => run(async () => {
    const info = await wallet.getInfo();
    setStatus(`protocol ${info.protocolVersion} · firmware ${info.firmware} · ${info.initialized ? '초기화됨' : '초기화 안 됨'}`);
  });

  const getState = () => run(async () => {
    const state = await wallet.getState();
    setStatus(`initialized=${state.initialized} · locked=${state.locked} · challenge=${state.challengeActive}`);
  });

  const sendRaw = () => run(async () => {
    const response = await wallet.transport.send(command, parseHex(payload));
    setStatus(`응답 0x${response.status.toString(16).padStart(4, '0')} · payload ${hex(response.payload) || '(없음)'}`);
  });

  const visible = showPackets ? entries : entries.filter((entry) => entry.layer !== 'packet');

  return (
    <main className={s.shell}>
      <nav className={s.nav}>
        <div className={s.brand}><span className={s.mark}>NU</span><span>Bluetooth Inspector</span></div>
        <div className={s.links}><Link href="/">지갑</Link><Link href="/setup">설정</Link><Link href="/dapp">트랜잭션 테스트</Link><Link href="/debug">BLE 디버그</Link></div>
      </nav>

      <header className={s.hero}>
        <span className={s.kicker}>NUWALLET PROTOCOL · LIVE TRACE</span>
        <h1>보드와 오가는 메시지를<br /><em>바이트 단위로 확인합니다.</em></h1>
        <p>GATT 연결 상태, 분할된 BLE 패킷, 조립된 요청·응답·이벤트를 시간순으로 표시합니다.</p>
      </header>

      <p className={s.warn}><strong>민감 정보 주의:</strong> 지갑 생성·복구 요청을 실행하면 니모닉 인덱스가 로그에 나타날 수 있습니다. 로그를 외부에 공유하지 마세요.</p>

      <div className={s.grid}>
        <section className={s.card}>
          <h2>연결 및 빠른 검사</h2>
          <p className={s.hint}>연결 버튼부터 발생하는 모든 BLE 단계가 오른쪽 로그에 기록됩니다.</p>
          <div className={s.buttons}>
            <button className={`${s.btn} ${s.primary}`} onClick={connect} disabled={busy || connected}>{connected ? '연결됨' : 'Bluetooth 연결'}</button>
            <button className={s.btn} onClick={getInfo} disabled={busy || !connected}>버전 조회</button>
            <button className={s.btn} onClick={getState} disabled={busy || !connected}>상태 조회</button>
            <button className={s.btn} onClick={() => wallet.disconnect()} disabled={!connected}>연결 해제</button>
          </div>
          <div className={s.status}><span className={connected ? s.online : ''} /><strong>{status}</strong></div>
        </section>

        <section className={s.card}>
          <h2>Raw 명령 전송</h2>
          <p className={s.hint}>프로토콜 명령과 payload를 직접 보내 오류 응답과 프레이밍을 검사합니다.</p>
          <label className={s.field}><span>COMMAND</span><select value={command} onChange={(e) => setCommand(Number(e.target.value))}>{COMMANDS.map((item) => <option value={item.value} key={item.name}>{item.name} · 0x{item.value.toString(16).padStart(2, '0')}</option>)}</select></label>
          <label className={s.field}><span>PAYLOAD · HEX</span><textarea value={payload} onChange={(e) => setPayload(e.target.value)} placeholder="예: 0c 또는 000102ff" spellCheck={false} /></label>
          <button className={`${s.btn} ${s.primary}`} onClick={sendRaw} disabled={busy || !connected}>명령 전송</button>
        </section>

        <section className={`${s.card} ${s.console}`}>
          <div className={s.consoleHead}><div><h2>실시간 BLE 로그</h2><p className={s.hint}>최대 500개 항목을 보관합니다.</p></div><div className={s.buttons}><label className={s.toggle}><input type="checkbox" checked={showPackets} onChange={(e) => setShowPackets(e.target.checked)} /> 패킷 표시</label><button className={s.btn} onClick={() => setEntries([])}>지우기</button></div></div>
          <div className={s.log} role="log" aria-live="polite">
            {visible.length === 0 ? <p className={s.empty}>아직 기록된 메시지가 없습니다.</p> : visible.map((entry, index) => (
              <article className={`${s.line} ${s[entry.direction]}`} key={`${entry.timestamp}-${index}`}>
                <time>{new Date(entry.timestamp).toLocaleTimeString('ko-KR', { hour12: false, fractionalSecondDigits: 3 })}</time>
                <span className={s.direction}>{entry.direction.toUpperCase()}</span>
                <span className={s.layer}>{entry.layer}</span>
                <strong>{entry.label}</strong>
                {entry.data && <code>{entry.data}</code>}
              </article>
            ))}
          </div>
        </section>
      </div>
      <WebAppFooter />
    </main>
  );
}

function parseHex(value: string): Uint8Array {
  const clean = value.trim().replace(/^0x/u, '').replace(/\s+/gu, '');
  if (!clean) return new Uint8Array();
  if (!/^[0-9a-f]+$/iu.test(clean) || clean.length % 2 !== 0) throw new Error('payload는 짝수 길이의 16진수여야 합니다.');
  return Uint8Array.from(clean.match(/.{2}/gu)!, (byte) => Number.parseInt(byte, 16));
}
