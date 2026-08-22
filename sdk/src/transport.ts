/**
 * Web Bluetooth 전송 계층.
 *
 * 프레이밍은 protocol.ts 가 하고, 여기서는 GATT 연결과 요청/응답 짝짓기,
 * 이벤트 배달만 다룬다.
 */
import {
  SERVICE_UUID, RX_UUID, TX_UUID, TAG, SW,
  frame, Reassembler, encodeRequest, decodeResponse, decodeEvent,
  WalletError, type Response, type DeviceEvent,
} from './protocol.js';

export interface TransportOptions {
  /** ATT MTU 에서 헤더를 뺀 유효 페이로드. 보수적으로 20 이 안전하다. */
  mtu?: number;
  /** 요청 하나의 타임아웃 (ms). 챌린지는 별도 타임아웃을 쓴다. */
  timeoutMs?: number;
}

type EventHandler = (e: DeviceEvent) => void;

export class BleTransport {
  private device: BluetoothDevice | null = null;
  private rx: BluetoothRemoteGATTCharacteristic | null = null;
  private tx: BluetoothRemoteGATTCharacteristic | null = null;
  private asm = new Reassembler();
  private pending: ((r: Response) => void) | null = null;
  private pendingReject: ((e: unknown) => void) | null = null;
  private queue: Promise<unknown> = Promise.resolve();
  private handlers = new Set<EventHandler>();
  private disconnectHandlers = new Set<() => void>();

  /* 리스너는 한 번만 만들어 두고 붙였다 뗀다. connect() 마다 새 익명 함수를
   * 붙이면 removeEventListener 로 뗄 수가 없다. Web Bluetooth 는 같은 기기에
   * 대해 같은 BluetoothDevice 객체를 돌려주므로, 재연결할 때마다 리스너가
   * 쌓여 알림 한 개가 두 번 배달되고 조립이 깨진다. */
  private readonly onValueChanged = (ev: Event) => {
    const dv = (ev.target as BluetoothRemoteGATTCharacteristic).value;
    if (!dv) return;
    this.onPacket(new Uint8Array(dv.buffer, dv.byteOffset, dv.byteLength));
  };
  private readonly onGattDisconnected = () => this.onDisconnected();

  readonly mtu: number;
  readonly timeoutMs: number;

  constructor(opts: TransportOptions = {}) {
    this.mtu = opts.mtu ?? 20;
    this.timeoutMs = opts.timeoutMs ?? 10_000;
  }

  static get isSupported(): boolean {
    return typeof navigator !== 'undefined' && !!navigator.bluetooth;
  }

  get isConnected(): boolean {
    return !!this.device?.gatt?.connected;
  }

  get deviceName(): string {
    return this.device?.name ?? '';
  }

  /**
   * 기기 선택 다이얼로그를 띄우고 연결한다. **사용자 제스처 안에서** 호출해야
   * 한다 — 클릭 핸들러에서 첫 await 전에 여기까지 도달해야 한다.
   *
   * 알림 구독(CCCD 쓰기)이 암호화를 요구하므로, OS 페어링 창은 그 시점에 뜬다.
   */
  async connect(): Promise<void> {
    if (!BleTransport.isSupported) {
      throw new WalletError(SW.DEVICE_ERROR,
        'Web Bluetooth 를 지원하지 않는 브라우저입니다. Chrome 또는 Edge 를 쓰세요.');
    }
    // 이미 붙어 있으면 선택 창을 다시 띄우지 않는다. 연결된 보드는 광고를
    // 멈추므로, 다시 띄워 봐야 목록이 비어 있어 사용자만 헷갈린다.
    if (this.isConnected) return;

    let dev: BluetoothDevice;
    try {
      dev = await navigator.bluetooth.requestDevice({
        filters: [{ services: [SERVICE_UUID] }],
        optionalServices: [SERVICE_UUID],
      });
    } catch (e) {
      throw chooserError(e);
    }

    try {
      const server = await dev.gatt!.connect();
      const svc = await server.getPrimaryService(SERVICE_UUID);
      const rx = await svc.getCharacteristic(RX_UUID);
      const tx = await svc.getCharacteristic(TX_UUID);
      // CCCD 쓰기는 암호화된 링크를 요구한다. 페어링이 여기서 일어난다.
      await tx.startNotifications();

      tx.addEventListener('characteristicvaluechanged', this.onValueChanged);
      dev.addEventListener('gattserverdisconnected', this.onGattDisconnected);
      this.device = dev;
      this.rx = rx;
      this.tx = tx;
      this.asm.reset();
    } catch (e) {
      /* 중간에 실패했으면 링크를 반드시 닫는다.
       * 열어 둔 채로 두면 보드는 "연결됨" 상태라 광고를 멈추고, 다음에 다시
       * 연결하려 해도 선택 창에 아무것도 뜨지 않는다. 페어링이 안 되는 것처럼
       * 보이는 증상의 대부분이 이것이다. */
      try { dev.gatt?.disconnect(); } catch { /* 이미 끊김 */ }
      this.detach(dev);
      this.device = null;
      this.rx = this.tx = null;
      throw connectError(e);
    }
  }

  async disconnect(): Promise<void> {
    try { this.device?.gatt?.disconnect(); } catch { /* 이미 끊김 */ }
    this.onDisconnected();
  }

  /** 붙여 둔 리스너를 뗀다. 재연결 시 중복 배달을 막는 핵심. */
  private detach(dev: BluetoothDevice | null) {
    try { this.tx?.removeEventListener('characteristicvaluechanged', this.onValueChanged); } catch { /* noop */ }
    try { dev?.removeEventListener('gattserverdisconnected', this.onGattDisconnected); } catch { /* noop */ }
  }

  onEvent(h: EventHandler): () => void {
    this.handlers.add(h);
    return () => this.handlers.delete(h);
  }

  onDisconnect(h: () => void): () => void {
    this.disconnectHandlers.add(h);
    return () => this.disconnectHandlers.delete(h);
  }

  /**
   * 명령 하나를 보내고 응답을 기다린다.
   * BLE 는 요청/응답을 짝지어주지 않으므로 직렬화한다 — 동시에 하나만 in-flight.
   */
  async send(cmd: number, payload?: Uint8Array, timeoutMs?: number): Promise<Response> {
    const run = async (): Promise<Response> => {
      if (!this.rx || !this.isConnected) {
        throw new WalletError(SW.DEVICE_ERROR, '연결되어 있지 않습니다');
      }
      const msg = encodeRequest(cmd, payload);
      const packets = frame(TAG.MESSAGE, msg, this.mtu);

      const result = new Promise<Response>((resolve, reject) => {
        this.pending = resolve;
        this.pendingReject = reject;
      });
      const timer = setTimeout(() => {
        this.pendingReject?.(new WalletError(SW.DEVICE_ERROR, '기기 응답 시간 초과'));
        this.pending = this.pendingReject = null;
      }, timeoutMs ?? this.timeoutMs);

      try {
        for (let i = 0; i < packets.length; i++) {
          // TS 5.7 부터 Uint8Array 가 버퍼 타입에 대해 제네릭이라 BufferSource 로
          // 바로 안 받아준다. frame() 이 만드는 건 항상 ArrayBuffer 기반이다.
          const buf = packets[i] as unknown as BufferSource;

          /* 첫 패킷만 응답 있는 쓰기로 보낸다.
           *
           * RX 특성은 암호화를 요구한다. 링크가 아직 암호화되지 않았다면 ATT 가
           * "insufficient authentication" 으로 거절하는데, write-without-response
           * 에는 응답이 없어서 그 오류가 어디에도 나타나지 않는다. 요청은 조용히
           * 사라지고 타임아웃만 난다 — 페어링 문제인지 알 길이 없다.
           * 첫 패킷을 응답 있는 쓰기로 보내면 오류가 즉시 올라온다.
           * 나머지는 속도를 위해 응답 없는 쓰기를 쓴다. */
          if (i === 0 || !this.rx.writeValueWithoutResponse) await this.rx.writeValue(buf);
          else await this.rx.writeValueWithoutResponse(buf);
        }
        return await result;
      } catch (e) {
        this.pending = this.pendingReject = null;
        throw writeError(e);
      } finally {
        clearTimeout(timer);
      }
    };
    // 직렬화: 앞의 요청이 끝나야 다음이 나간다.
    const next = this.queue.then(run, run);
    this.queue = next.catch(() => undefined);
    return next;
  }

  private onPacket(pkt: Uint8Array) {
    let asm;
    try {
      asm = this.asm.push(pkt);
    } catch (e) {
      this.pendingReject?.(e);
      this.pending = this.pendingReject = null;
      return;
    }
    if (!asm) return;

    if (asm.tag === TAG.EVENT) {
      const e = decodeEvent(asm.payload);
      for (const h of this.handlers) { try { h(e); } catch { /* 핸들러 오류 무시 */ } }
      return;
    }
    const resolve = this.pending;
    this.pending = this.pendingReject = null;
    if (!resolve) return;                      // 짝 없는 응답 — 버린다
    try { resolve(decodeResponse(asm.payload)); }
    catch { /* 형식이 깨진 응답. 요청은 타임아웃으로 정리된다 */ }
  }

  private onDisconnected() {
    this.detach(this.device);
    this.rx = this.tx = null;
    this.device = null;
    this.asm.reset();
    this.pendingReject?.(new WalletError(SW.DEVICE_ERROR, '기기 연결이 끊겼습니다'));
    this.pending = this.pendingReject = null;
    for (const h of this.disconnectHandlers) { try { h(); } catch { /* noop */ } }
  }
}

// ── 오류 변환 ───────────────────────────────────────────────────────────────
//
// Web Bluetooth 는 무슨 일이 있었는지 DOMException 이름으로만 알려준다.
// 그대로 화면에 띄우면 "GATT Error Unknown" 같은 것이 보여서 사용자가 무엇을
// 해야 할지 알 수 없다. 실제로 할 수 있는 조치로 바꿔 준다.

function nameOf(e: unknown): string {
  return (e as { name?: string } | null)?.name ?? '';
}

/** 기기 선택 다이얼로그 단계의 오류. */
function chooserError(e: unknown): WalletError {
  if (e instanceof WalletError) return e;
  switch (nameOf(e)) {
    case 'NotFoundError':
      // 창을 닫았거나, 조건에 맞는 기기가 하나도 없었다. 구분이 불가능하다.
      return new WalletError(SW.USER_REJECTED,
        '기기를 선택하지 않았습니다. 목록이 비어 있었다면 보드에 전원이 들어와 있는지, ' +
        '이미 다른 탭이나 OS 에 연결되어 있지 않은지 확인하세요 — 연결된 보드는 광고를 멈춥니다.');
    case 'SecurityError':
      return new WalletError(SW.DEVICE_ERROR,
        'Web Bluetooth 는 HTTPS 또는 localhost 에서, 그리고 버튼 클릭 같은 사용자 동작 ' +
        '안에서만 기기를 선택할 수 있습니다.');
    case 'NotSupportedError':
      return new WalletError(SW.DEVICE_ERROR,
        '이 브라우저는 Web Bluetooth 를 지원하지 않습니다. 데스크톱 Chrome 또는 Edge 를 쓰세요.');
    default:
      return new WalletError(SW.DEVICE_ERROR, messageOf(e, '기기를 선택하지 못했습니다'));
  }
}

/** GATT 연결 · 서비스 탐색 · 알림 구독 단계의 오류. */
function connectError(e: unknown): WalletError {
  if (e instanceof WalletError) return e;
  switch (nameOf(e)) {
    case 'NetworkError':
      /* 알림 구독은 암호화된 링크를 요구한다. 여기서 끊긴다는 것은 보통
       * 페어링이 실패했다는 뜻이고, 가장 흔한 원인은 본딩 키 불일치다 —
       * 보드에 펌웨어를 다시 올리면 보드는 키를 잊지만 PC 는 기억한다. */
      return new WalletError(SW.DEVICE_ERROR,
        '페어링에 실패했습니다. 펌웨어를 다시 올렸다면 OS 블루투스 설정에서 ' +
        'NuWallet 기기를 삭제(잊기)한 뒤 다시 연결하세요 — 보드는 본딩 키를 잊었는데 ' +
        'PC 는 옛 키를 그대로 쓰고 있으면 링크가 끊깁니다.');
    case 'SecurityError':
      return new WalletError(SW.DEVICE_ERROR,
        '기기가 접근을 거부했습니다. 페어링이 완료되었는지 확인하세요.');
    case 'NotFoundError':
      return new WalletError(SW.DEVICE_ERROR,
        'NuWallet 서비스를 찾지 못했습니다. 지갑 펌웨어가 올라가 있는지 확인하세요 ' +
        '(옛 데모 펌웨어는 다른 서비스를 광고합니다).');
    default:
      return new WalletError(SW.DEVICE_ERROR, messageOf(e, '연결하지 못했습니다'));
  }
}

/** 명령 전송 단계의 오류. */
function writeError(e: unknown): WalletError {
  if (e instanceof WalletError) return e;
  if (nameOf(e) === 'NotSupportedError' || nameOf(e) === 'SecurityError') {
    return new WalletError(SW.DEVICE_ERROR,
      '기기가 명령을 거부했습니다. 링크가 암호화되지 않았을 수 있습니다 — ' +
      'OS 블루투스 설정에서 기기를 삭제한 뒤 다시 페어링해 보세요.');
  }
  return new WalletError(SW.DEVICE_ERROR, messageOf(e, '명령을 보내지 못했습니다'));
}

function messageOf(e: unknown, fallback: string): string {
  const m = e instanceof Error ? e.message : '';
  return m ? `${fallback}: ${m}` : fallback;
}
