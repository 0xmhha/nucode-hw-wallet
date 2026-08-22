/* PIN · 잠금 API 테스트 — 하드웨어 없이 가짜 전송 계층으로 돌린다.
 *   node --test test/pin.test.js                                            */
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { NuWalletAdmin, validatePin } from '../dist/pin.js';
import { CMD, EVT, SW, WalletError } from '../dist/protocol.js';

/** 기기 흉내. 보낸 명령을 기록하고, 정해 둔 응답과 이벤트를 돌려준다. */
function fakeDevice({ responses = {}, script = () => {} } = {}) {
  const sent = [];
  const handlers = new Set();
  const discHandlers = new Set();
  const transport = {
    async send(cmd, payload) {
      sent.push({ cmd, payload: payload ? Array.from(payload) : [] });
      const r = responses[cmd] ?? { status: SW.OK, payload: new Uint8Array() };
      // 응답을 돌려준 뒤 기기가 이벤트를 흘리기 시작한다.
      // 마이크로태스크로 쏘면 호출자가 아직 리스너를 달기 전이라 놓친다.
      // 실제 BLE 도 notify 는 별도 이벤트 루프 턴에 온다.
      setTimeout(() => script({ emit, disconnect, cmd }), 0);
      return r;
    },
    onEvent(h) { handlers.add(h); return () => handlers.delete(h); },
    onDisconnect(h) { discHandlers.add(h); return () => discHandlers.delete(h); },
  };
  const emit = (evt, payload) => { for (const h of [...handlers]) h({ evt, payload }); };
  const disconnect = () => { for (const h of [...discHandlers]) h(); };
  return { admin: new NuWalletAdmin({ transport }), sent, emit, disconnect };
}

const pending = (id) => ({ status: SW.PENDING, payload: be32(id) });
function be32(v) {
  const b = new Uint8Array(4);
  new DataView(b.buffer).setUint32(0, v, false);
  return b;
}
/** 0xA4 REQUEST_RESULT 페이로드: reqId(4) ‖ cmd(1) ‖ status(2) */
function requestResult(id, cmd, status) {
  const p = new Uint8Array(7);
  const dv = new DataView(p.buffer);
  dv.setUint32(0, id, false);
  p[4] = cmd;
  dv.setUint16(5, status, false);
  return p;
}
/** 0xA0 CHALLENGE_STARTED: reqId(4) ‖ steps(1) ‖ cmd(1) */
function challengeStarted(id, steps, cmd) {
  const p = new Uint8Array(6);
  new DataView(p.buffer).setUint32(0, id, false);
  p[4] = steps; p[5] = cmd;
  return p;
}

test('PIN 검증: 길이와 버튼 번호', () => {
  assert.doesNotThrow(() => validatePin([0, 1, 2, 3]));
  assert.doesNotThrow(() => validatePin([]));                 // 제거
  assert.doesNotThrow(() => validatePin([0, 0, 0, 0, 0, 0, 0, 0]));
  assert.throws(() => validatePin([0, 1, 2]), WalletError);    // 너무 짧다
  assert.throws(() => validatePin([0, 1, 2, 3, 0, 1, 2, 3, 0]), WalletError);
  assert.throws(() => validatePin([0, 1, 2, 4]), WalletError); // 버튼은 0..3
  assert.throws(() => validatePin([0, 1, 2, -1]), WalletError);
});

test('setPin: 승인 이벤트를 기다린다', async () => {
  const { admin, sent } = fakeDevice({
    responses: { [CMD.SET_PIN]: pending(7) },
    script: ({ emit }) => {
      emit(EVT.CHALLENGE_STARTED, challengeStarted(7, 4, CMD.SET_PIN));
      emit(EVT.REQUEST_RESULT, requestResult(7, CMD.SET_PIN, SW.OK));
    },
  });

  const steps = [];
  await admin.setPin([1, 2, 3, 0], { onStart: (i) => steps.push(i.steps) });

  assert.equal(sent.length, 1);
  assert.equal(sent[0].cmd, CMD.SET_PIN);
  assert.deepEqual(sent[0].payload, [4, 1, 2, 3, 0]);   // 길이 접두 + 버튼 번호
  assert.deepEqual(steps, [4]);
});

test('setPin: 기기가 거부하면 그 상태로 실패한다', async () => {
  const { admin } = fakeDevice({
    responses: { [CMD.SET_PIN]: pending(9) },
    script: ({ emit }) => emit(EVT.REQUEST_RESULT,
      requestResult(9, CMD.SET_PIN, SW.CHALLENGE_FAILED)),
  });
  await assert.rejects(() => admin.setPin([0, 0, 0, 0]),
    (e) => e instanceof WalletError && e.status === SW.CHALLENGE_FAILED && e.isUserRejection);
});

test('setPin: 다른 요청의 이벤트는 무시한다', async () => {
  const { admin } = fakeDevice({
    responses: { [CMD.SET_PIN]: pending(11) },
    script: ({ emit }) => {
      emit(EVT.REQUEST_RESULT, requestResult(999, CMD.SET_PIN, SW.OK));   // 남의 결과
      emit(EVT.REQUEST_RESULT, requestResult(11, CMD.SET_PIN, SW.OK));
    },
  });
  await admin.setPin([3, 3, 3, 3]);   // 완료되면 성공
});

test('clearPin: 길이 0 을 보낸다', async () => {
  const { admin, sent } = fakeDevice({
    responses: { [CMD.SET_PIN]: pending(1) },
    script: ({ emit }) => emit(EVT.REQUEST_RESULT, requestResult(1, CMD.SET_PIN, SW.OK)),
  });
  await admin.clearPin();
  assert.deepEqual(sent[0].payload, [0]);
});

test('unlock: PIN 이 없으면 즉시 주소를 돌려준다', async () => {
  const addr = new Uint8Array(20).fill(0xab);
  const { admin, sent } = fakeDevice({
    responses: { [CMD.UNLOCK]: { status: SW.OK, payload: addr } },
  });
  const got = await admin.unlock();
  assert.equal(got.toLowerCase(), '0x' + 'ab'.repeat(20));
  assert.deepEqual(sent[0].payload, []);      // 패스프레이즈 없음 = 빈 페이로드
});

test('unlock: 패스프레이즈는 UTF-8 로 실려 나간다', async () => {
  const { admin, sent } = fakeDevice({
    responses: { [CMD.UNLOCK]: { status: SW.OK, payload: new Uint8Array(20) } },
  });
  await admin.unlock('한글pass');
  assert.deepEqual(sent[0].payload, Array.from(new TextEncoder().encode('한글pass')));
});

test('unlock: 64바이트를 넘는 패스프레이즈는 보내기 전에 막는다', async () => {
  const { admin, sent } = fakeDevice();
  await assert.rejects(() => admin.unlock('x'.repeat(65)),
    (e) => e instanceof WalletError && e.status === SW.BAD_PARAM);
  assert.equal(sent.length, 0);
});

test('unlock: PIN 이 있으면 버튼 입력을 기다린다', async () => {
  const progress = [];
  const { admin } = fakeDevice({
    responses: { [CMD.UNLOCK]: pending(21) },
    script: ({ emit }) => {
      emit(EVT.CHALLENGE_STARTED, challengeStarted(21, 4, CMD.UNLOCK));
      for (let step = 1; step <= 4; step++) {
        const p = new Uint8Array(6);
        new DataView(p.buffer).setUint32(0, 21, false);
        p[4] = step; p[5] = 5;
        emit(EVT.CHALLENGE_PROGRESS, p);
      }
      emit(EVT.REQUEST_RESULT, requestResult(21, CMD.UNLOCK, SW.OK));
    },
  });
  await admin.unlock('', { onProgress: (i) => progress.push(i.step) });
  assert.deepEqual(progress, [1, 2, 3, 4]);
});

test('unlock: 연결이 끊기면 대기를 끝낸다', async () => {
  const { admin } = fakeDevice({
    responses: { [CMD.UNLOCK]: pending(31) },
    script: ({ disconnect }) => disconnect(),
  });
  await assert.rejects(() => admin.unlock(),
    (e) => e instanceof WalletError && e.status === SW.DEVICE_ERROR);
});

test('unlock: 타임아웃되면 CANCEL 을 보낸다', async () => {
  const { admin, sent } = fakeDevice({
    responses: { [CMD.UNLOCK]: pending(41), [CMD.CANCEL]: { status: SW.OK, payload: new Uint8Array() } },
  });
  await assert.rejects(() => admin.unlock('', { timeoutMs: 10 }),
    (e) => e instanceof WalletError && e.status === SW.CHALLENGE_TIMEOUT);
  await new Promise((r) => setTimeout(r, 20));
  assert.ok(sent.some((s) => s.cmd === CMD.CANCEL), 'CANCEL 이 나가야 한다');
});

test('unlock: AbortSignal 로 취소할 수 있다', async () => {
  const ac = new AbortController();
  const { admin, sent } = fakeDevice({
    responses: { [CMD.UNLOCK]: pending(51), [CMD.CANCEL]: { status: SW.OK, payload: new Uint8Array() } },
    script: () => ac.abort(),
  });
  await assert.rejects(() => admin.unlock('', { signal: ac.signal }),
    (e) => e instanceof WalletError && e.status === SW.USER_REJECTED);
  assert.ok(sent.some((s) => s.cmd === CMD.CANCEL));
});

test('getLockState: 플래그와 PIN 길이를 읽는다', async () => {
  const p = new Uint8Array([0x01 | 0x02 | 0x08, 0, 0, 0, 0, 3, 6]);
  const { admin } = fakeDevice({ responses: { [CMD.GET_STATE]: { status: SW.OK, payload: p } } });
  const s = await admin.getLockState();
  assert.deepEqual(s, {
    initialized: true, locked: true, challengeActive: false, hasPin: true,
    requestId: 0, attemptsLeft: 3, pinLength: 6,
  });
});

test('lock: OK 가 아니면 던진다', async () => {
  const { admin } = fakeDevice({
    responses: { [CMD.LOCK]: { status: SW.NOT_INITIALIZED, payload: new Uint8Array() } },
  });
  await assert.rejects(() => admin.lock(),
    (e) => e instanceof WalletError && e.status === SW.NOT_INITIALIZED);
});
