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

  /** 기기 선택 다이얼로그를 띄우고 연결한다. 사용자 제스처 안에서 호출해야 한다. */
  async connect(): Promise<void> {
    if (!BleTransport.isSupported) {
      throw new WalletError(SW.DEVICE_ERROR,
        'Web Bluetooth 를 지원하지 않는 브라우저입니다. Chrome 또는 Edge 를 쓰세요.');
    }
    const dev = await navigator.bluetooth.requestDevice({
      filters: [{ services: [SERVICE_UUID] }],
      optionalServices: [SERVICE_UUID],
    });
    dev.addEventListener('gattserverdisconnected', () => this.onDisconnected());
    const server = await dev.gatt!.connect();
    const svc = await server.getPrimaryService(SERVICE_UUID);
    this.rx = await svc.getCharacteristic(RX_UUID);
    this.tx = await svc.getCharacteristic(TX_UUID);
    await this.tx.startNotifications();
    this.tx.addEventListener('characteristicvaluechanged', (ev) => {
      const dv = (ev.target as BluetoothRemoteGATTCharacteristic).value!;
      this.onPacket(new Uint8Array(dv.buffer, dv.byteOffset, dv.byteLength));
    });
    this.device = dev;
  }

  async disconnect(): Promise<void> {
    try { this.device?.gatt?.disconnect(); } catch { /* 이미 끊김 */ }
    this.onDisconnected();
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
      if (!this.rx) throw new WalletError(SW.DEVICE_ERROR, '연결되어 있지 않습니다');
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
        for (const p of packets) {
          // TS 5.7 부터 Uint8Array 가 버퍼 타입에 대해 제네릭이라 BufferSource 로
          // 바로 안 받아준다. frame() 이 만드는 건 항상 ArrayBuffer 기반이다.
          const buf = p as unknown as BufferSource;
          // writeValueWithoutResponse 가 있으면 그쪽이 훨씬 빠르다.
          if (this.rx.writeValueWithoutResponse) await this.rx.writeValueWithoutResponse(buf);
          else await this.rx.writeValue(buf);
        }
        return await result;
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
    catch (e) { /* decodeResponse 실패는 위에서 reject 했어야 하나, 여기선 무시 */ }
  }

  private onDisconnected() {
    this.rx = this.tx = null;
    this.device = null;
    this.asm.reset();
    this.pendingReject?.(new WalletError(SW.DEVICE_ERROR, '기기 연결이 끊겼습니다'));
    this.pending = this.pendingReject = null;
    for (const h of this.disconnectHandlers) { try { h(); } catch { /* noop */ } }
  }
}
