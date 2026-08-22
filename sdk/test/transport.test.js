/* 전송 계층 — 연결 · 재연결 · 페어링 실패 처리.
 *
 * 가짜 Web Bluetooth 로 돌린다. 여기서 잡으려는 것은 하드웨어가 아니라
 * "연결이 반쯤 성공한 상태" 와 "재연결 시 리스너 중복" 이다. 둘 다 실기기에서는
 * 페어링이 안 되는 것처럼 보인다.
 *   node --test test/transport.test.js                                      */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { BleTransport } from '../dist/transport.js';
import { SERVICE_UUID, RX_UUID, TX_UUID, TAG, SW, frame, WalletError } from '../dist/protocol.js';

/* ── 가짜 보드 ──────────────────────────────────────────────────────────── */

class FakeChar extends EventTarget {
  constructor(uuid, board) { super(); this.uuid = uuid; this.board = board; this.value = undefined; }
  async startNotifications() {
    if (this.board.failNotify) throw this.board.failNotify;
    this.board.notifying = true;
    return this;
  }
  async writeValue(buf) { return this.#write(buf, true); }
  async writeValueWithoutResponse(buf) { return this.#write(buf, false); }
  #write(buf, withResponse) {
    const bytes = new Uint8Array(buf.buffer ?? buf, buf.byteOffset ?? 0, buf.byteLength ?? buf.length);
    this.board.writes.push({ bytes: Array.from(bytes), withResponse });
    if (this.board.failWrite && withResponse) throw this.board.failWrite;
    if (this.board.failWrite && !withResponse) return;   // 응답 없는 쓰기는 조용히 삼켜진다
    this.board.onWrite?.(bytes);
  }
  emit(packet) {
    this.value = new DataView(packet.buffer, packet.byteOffset, packet.byteLength);
    this.dispatchEvent(new Event('characteristicvaluechanged'));
  }
}

class FakeDevice extends EventTarget {
  constructor(board) {
    super();
    this.name = 'NuWallet-ABCDEF';
    this.board = board;
    const self = this;
    this.gatt = {
      get connected() { return board.linkUp; },
      async connect() { board.linkUp = true; return this; },
      disconnect() {
        if (!board.linkUp) return;
        board.linkUp = false;
        board.disconnects++;
        self.dispatchEvent(new Event('gattserverdisconnected'));
      },
      async getPrimaryService(uuid) {
        assert.equal(uuid, SERVICE_UUID);
        return { async getCharacteristic(u) { return board.chars[u]; } };
      },
    };
  }
}

function makeBoard() {
  const board = {
    linkUp: false, notifying: false, disconnects: 0, chooserCalls: 0,
    writes: [], failNotify: null, failWrite: null, chooserError: null,
  };
  board.chars = { [RX_UUID]: new FakeChar(RX_UUID, board), [TX_UUID]: new FakeChar(TX_UUID, board) };
  board.device = new FakeDevice(board);

  /* 호스트가 보낸 메시지를 조립해 응답을 돌려주는 최소 기기. */
  let total = 0, buf = [];
  board.onWrite = (bytes) => {
    const seq = (bytes[1] << 8) | bytes[2];
    if (seq === 0) { total = (bytes[3] << 8) | bytes[4]; buf = Array.from(bytes.slice(5)); }
    else buf.push(...bytes.slice(3));
    if (buf.length < total) return;
    const cmd = buf[0];
    buf = [];
    board.reply(cmd);
  };
  /* 응답 페이로드 길이는 명령마다 다르다. 여러 패킷으로 쪼개지는 경우를 만든다. */
  board.reply = (cmd) => {
    const payloadLen = cmd === 0x20 ? 117 : 4;          // GET_ADDRESS 는 117바이트
    const payload = new Uint8Array(payloadLen).map((_, i) => (i * 3) & 0xff);
    const msg = new Uint8Array(4 + payloadLen);
    msg[0] = 0x90; msg[1] = 0x00;
    msg[2] = payloadLen >> 8; msg[3] = payloadLen & 0xff;
    msg.set(payload, 4);
    for (const pkt of frame(TAG.MESSAGE, msg, 20)) board.chars[TX_UUID].emit(pkt);
  };

  // Node 의 globalThis.navigator 는 getter 라 대입이 안 된다. 정의를 갈아끼운다.
  Object.defineProperty(globalThis, 'navigator', {
    configurable: true,
    value: {
      bluetooth: {
        async requestDevice() {
          board.chooserCalls++;
          if (board.chooserError) throw board.chooserError;
          return board.device;
        },
      },
    },
  });
  return board;
}

function domError(name, message) {
  const e = new Error(message);
  e.name = name;
  return e;
}

/* ── 테스트 ─────────────────────────────────────────────────────────────── */

test('연결하고 명령 하나를 주고받는다', async () => {
  const board = makeBoard();
  const t = new BleTransport();
  await t.connect();
  assert.equal(t.isConnected, true);
  assert.equal(t.deviceName, 'NuWallet-ABCDEF');
  const r = await t.send(0x01);
  assert.equal(r.status, SW.OK);
});

test('첫 패킷은 응답 있는 쓰기로 나간다 — 인증 오류가 묻히면 안 된다', async () => {
  const board = makeBoard();
  const t = new BleTransport();
  await t.connect();
  await t.send(0x20);                                   // 여러 패킷짜리 응답
  assert.ok(board.writes.length >= 1);
  assert.equal(board.writes[0].withResponse, true,
    '첫 패킷이 write-without-response 면 ATT 인증 오류를 알 수 없다');
});

test('쓰기가 인증 오류로 거부되면 타임아웃을 기다리지 않고 즉시 실패한다', async () => {
  const board = makeBoard();
  const t = new BleTransport({ timeoutMs: 30_000 });
  await t.connect();
  board.failWrite = domError('NotSupportedError', 'GATT operation not permitted');
  const started = Date.now();
  await assert.rejects(() => t.send(0x01));
  assert.ok(Date.now() - started < 1000, '타임아웃까지 기다리면 안 된다');
});

test('재연결해도 알림이 한 번만 배달된다', async () => {
  const board = makeBoard();
  const t = new BleTransport();
  await t.connect();
  await t.disconnect();
  await t.connect();

  // 여러 패킷으로 쪼개지는 응답. 리스너가 중복 등록되어 있으면 같은 패킷이 두 번
  // 들어와 SEQ 가 어긋나고 조립이 깨진다.
  const r = await t.send(0x20);
  assert.equal(r.status, SW.OK);
  assert.equal(r.payload.length, 117);
});

test('알림 구독에 실패하면 링크를 닫고 상태를 남기지 않는다', async () => {
  const board = makeBoard();
  const t = new BleTransport();
  board.failNotify = domError('NetworkError', 'GATT Error Unknown');

  await assert.rejects(() => t.connect(), (e) => e instanceof WalletError);
  assert.equal(t.isConnected, false);
  assert.equal(board.linkUp, false,
    '실패한 연결을 열어 두면 보드가 광고를 멈춘 채 남아 다음 시도에서 안 보인다');

  // 반쯤 연결된 상태로 명령이 나가면 안 된다.
  await assert.rejects(() => t.send(0x01), (e) => e instanceof WalletError);
});

test('실패 후 다시 연결할 수 있다', async () => {
  const board = makeBoard();
  const t = new BleTransport();
  board.failNotify = domError('NetworkError', 'GATT Error Unknown');
  await assert.rejects(() => t.connect());

  board.failNotify = null;
  await t.connect();
  assert.equal(t.isConnected, true);
  const r = await t.send(0x20);
  assert.equal(r.payload.length, 117);
});

test('기기 선택 창을 닫으면 알아볼 수 있는 오류가 난다', async () => {
  const board = makeBoard();
  const t = new BleTransport();
  board.chooserError = domError('NotFoundError', 'User cancelled the requestDevice() chooser.');
  await assert.rejects(() => t.connect(), (e) => e instanceof WalletError && e.isUserRejection);
});

test('이미 연결되어 있으면 선택 창을 다시 띄우지 않는다', async () => {
  const board = makeBoard();
  const t = new BleTransport();
  await t.connect();
  await t.connect();
  assert.equal(board.chooserCalls, 1);
});

test('연결이 끊기면 대기 중이던 요청이 풀린다', async () => {
  const board = makeBoard();
  const t = new BleTransport();
  await t.connect();
  board.onWrite = () => {};                             // 기기가 응답하지 않는다
  const p = t.send(0x01);
  board.device.gatt.disconnect();
  await assert.rejects(() => p, (e) => e instanceof WalletError);
});
