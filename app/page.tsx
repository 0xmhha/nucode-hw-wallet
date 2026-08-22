'use client';

import { useCallback, useEffect, useRef, useState } from 'react';

const SERVICE_UUID = '7d2a0001-7b7a-4f45-8d68-36f1a4d9c101';
const BOARD_TO_WEB_UUID = '7d2a0002-7b7a-4f45-8d68-36f1a4d9c101';
const WEB_TO_BOARD_UUID = '7d2a0003-7b7a-4f45-8d68-36f1a4d9c101';
type Mood = 'happy' | 'okay' | 'hungry';
type Char = { startNotifications(): Promise<Char>; addEventListener(type: string, listener: EventListener): void; writeValue(value: BufferSource): Promise<void> };
type Device = { name?: string; gatt?: { connect(): Promise<{ getPrimaryService(uuid: string): Promise<{ getCharacteristic(uuid: string): Promise<Char> }> }> }; addEventListener(type: string, listener: EventListener): void };
const moodInfo = {
  happy: { label: '아주 신나요!', note: '배가 든든해요', color: '#b7ef5b', interval: 1400, speed: '천천히' },
  okay: { label: '기분이 괜찮아요', note: '조금 출출해요', color: '#ffd658', interval: 760, speed: '보통으로' },
  hungry: { label: '배가 고파요!', note: '버튼을 눌러 밥을 주세요', color: '#ff7164', interval: 260, speed: '빠르게' },
} satisfies Record<Mood, { label: string; note: string; color: string; interval: number; speed: string }>;
const getMood = (hunger: number): Mood => hunger >= 65 ? 'happy' : hunger >= 30 ? 'okay' : 'hungry';

export default function Home() {
  const [hunger, setHunger] = useState(42);
  const [connected, setConnected] = useState(false);
  const [deviceName, setDeviceName] = useState('NU-40 DK');
  const [connecting, setConnecting] = useState(false);
  const [message, setMessage] = useState('보드의 버튼을 누르면 밥을 먹어요');
  const [feedPulse, setFeedPulse] = useState(0);
  const txRef = useRef<Char | null>(null);
  const lastMood = useRef<Mood | null>(null);
  const mood = getMood(hunger);
  const info = moodInfo[mood];

  const feed = useCallback(() => {
    setHunger((value) => Math.min(100, value + 28));
    setMessage('냠냠! 맛있게 먹었어요');
    setFeedPulse((value) => value + 1);
    window.setTimeout(() => setMessage('보드의 버튼을 누르면 밥을 먹어요'), 1800);
  }, []);

  useEffect(() => { const timer = window.setInterval(() => setHunger((v) => Math.max(0, v - 1)), 6000); return () => clearInterval(timer); }, []);
  useEffect(() => {
    if (!connected || !txRef.current || lastMood.current === mood) return;
    lastMood.current = mood;
    const payload = JSON.stringify({ type: 'mood', mood, blinkMs: info.interval, color: info.color }) + '\n';
    txRef.current.writeValue(new TextEncoder().encode(payload)).catch(() => setMessage('LED 명령을 보내지 못했어요'));
  }, [connected, info.color, info.interval, mood]);

  async function connect() {
    const nav = navigator as Navigator & { bluetooth?: { requestDevice(options: unknown): Promise<Device> } };
    if (!nav.bluetooth) { setMessage('Chrome 또는 Edge에서 HTTPS로 열어주세요'); return; }
    setConnecting(true);
    try {
      const device = await nav.bluetooth.requestDevice({ filters: [{ namePrefix: 'NU-40' }], optionalServices: [SERVICE_UUID] });
      const server = await device.gatt?.connect();
      if (!server) throw new Error('GATT unavailable');
      const service = await server.getPrimaryService(SERVICE_UUID);
      const rx = await service.getCharacteristic(BOARD_TO_WEB_UUID);
      txRef.current = await service.getCharacteristic(WEB_TO_BOARD_UUID);
      await rx.startNotifications();
      rx.addEventListener('characteristicvaluechanged', ((event: Event) => {
        const text = new TextDecoder().decode((event.target as unknown as { value: DataView }).value).trim();
        if (text === 'FEED' || text.includes('"type":"feed"')) feed();
      }) as EventListener);
      device.addEventListener('gattserverdisconnected', (() => { setConnected(false); txRef.current = null; setMessage('연결이 끊어졌어요. 다시 연결해 주세요'); }) as EventListener);
      setDeviceName(device.name || 'NU-40 DK'); setConnected(true); setMessage('연결됐어요! 보드의 버튼을 눌러보세요');
    } catch (error) {
      if ((error as Error).name !== 'NotFoundError') setMessage('연결하지 못했어요. 보드 전원을 확인해 주세요');
    } finally { setConnecting(false); }
  }

  return <main className="app-shell" style={{ '--mood': info.color, '--blink': `${info.interval}ms` } as React.CSSProperties}>
    <nav><div className="brand"><span className="brand-mark">NU</span><span>NU-40 PET</span></div><button className={`connect ${connected ? 'is-connected' : ''}`} onClick={connect} disabled={connecting || connected}><span className="signal">ᛒ</span>{connecting ? '연결 중…' : connected ? `${deviceName} 연결됨` : '보드 연결하기'}</button></nav>
    <section className="hero"><div className="eyebrow"><span className="live-dot" /> NU-40 DK × TAMAGOTCHI</div><h1>오늘도 잘 먹고,<br /><em>반짝이는 하루!</em></h1><p>보드의 버튼으로 다마고치에게 밥을 주세요.<br />기분은 RGB LED의 반짝임으로 바로 알려드려요.</p></section>
    <section className="pet-card"><div className="pixel-corner corner-one" /><div className="pixel-corner corner-two" />
      <div className="pet-stage"><div className={`food-bite ${feedPulse ? 'animate' : ''}`} key={feedPulse}>♥</div><div className={`pet pet-${mood}`} aria-label={`${info.label} 상태의 다마고치`}><i className="ear left" /><i className="ear right" /><div className="face"><b className="eye left" /><b className="eye right" /><span className="mouth" /></div><i className="foot left" /><i className="foot right" /></div><div className="shadow" /></div>
      <div className="status-panel"><span className="status-label">지금 기분</span><h2>{info.label}</h2><p>{info.note}</p><div className="meter-head"><span>배부름</span><strong>{hunger}%</strong></div><div className="meter"><span style={{ width: `${hunger}%` }} /></div><div className="led-readout"><span className="rgb-led" /><div><small>RGB LED</small><strong>{info.speed} 깜빡여요</strong></div><span className="speed-bars"><i /><i /><i /></span></div></div>
    </section>
    <section className="instruction" aria-live="polite"><span className="button-icon"><i /></span><div><small>HOW TO PLAY</small><strong>{message}</strong></div><button onClick={feed} className="test-feed" title="브라우저에서 먹이 주기 테스트">테스트 먹이</button></section>
    <footer><span>© 2026 NU-40 PET LAB</span><span className="footer-pixels">■ □ ■</span><span>BLE READY · BE KIND TO YOUR PET</span></footer>
  </main>;
}
