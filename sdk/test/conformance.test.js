/* 프로토콜 적합성 — SDK 와 펌웨어를 직접 물려서 확인한다.
 *
 * 가짜 기기가 아니라 **보드에 올라갈 코드 그대로**를 자식 프로세스로 띄운다
 * (firmware/test/protocol_bridge). 그래서 여기서 통과하면 명령 번호·페이로드
 * 레이아웃·이벤트가 실제로 맞는다는 뜻이다.
 *
 *   cd firmware/test && make protocol_bridge
 *   cd sdk && node --test test/conformance.test.js
 */
import { test, before, after } from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

import { NuWallet } from '../dist/client.js';
import { NuWalletAdmin } from '../dist/pin.js';
import {
  encodeRequest, decodeResponse, decodeEvent, TAG, SW, WalletError,
} from '../dist/protocol.js';
import { mnemonicToIndices } from '../dist/wordlist.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const BIN = resolve(HERE, '../../firmware/test/protocol_bridge');

/* ── 브리지 프로세스 ────────────────────────────────────────────────────── */

class Bridge {
  constructor() {
    this.proc = spawn(BIN, [], { stdio: ['pipe', 'pipe', 'inherit'] });
    this.text = '';
    this.waiters = [];
    this.batch = [];
    this.proc.stdout.setEncoding('utf8');
    this.proc.stdout.on('data', (chunk) => this.#onData(chunk));
  }
  #onData(chunk) {
    this.text += chunk;
    let nl;
    while ((nl = this.text.indexOf('\n')) >= 0) {
      const line = this.text.slice(0, nl);
      this.text = this.text.slice(nl + 1);
      if (line === '!') {
        const batch = this.batch;
        this.batch = [];
        this.waiters.shift()?.(batch);
      } else if (line.startsWith('<')) {
        this.batch.push({
          tag: parseInt(line.slice(1, 3), 16),
          bytes: fromHexStr(line.slice(3)),
        });
      }
    }
  }
  exec(line) {
    return new Promise((res) => {
      this.waiters.push(res);
      this.proc.stdin.write(line + '\n');
    });
  }
  kill() { try { this.proc.stdin.end('q\n'); } catch { /* noop */ } this.proc.kill(); }
}

/* ── 브리지를 BleTransport 처럼 보이게 하는 껍데기 ──────────────────────── */

class BridgeTransport {
  constructor(bridge) {
    this.bridge = bridge;
    this.handlers = new Set();
    this.discHandlers = new Set();
  }
  get isConnected() { return true; }
  get deviceName() { return 'NuWallet-TEST'; }
  async connect() {}
  async disconnect() {}
  onEvent(h) { this.handlers.add(h); return () => this.handlers.delete(h); }
  onDisconnect(h) { this.discHandlers.add(h); return () => this.discHandlers.delete(h); }

  async send(cmd, payload) {
    const msg = encodeRequest(cmd, payload);
    const out = await this.bridge.exec('>' + toHexStr(msg));
    let response = null;
    const events = [];
    for (const m of out) {
      if (m.tag === TAG.MESSAGE && !response) response = decodeResponse(m.bytes);
      else if (m.tag === TAG.EVENT) events.push(decodeEvent(m.bytes));
    }
    if (!response) throw new Error(`기기가 응답하지 않았습니다 (cmd 0x${cmd.toString(16)})`);
    this.#dispatchLater(events);
    return response;
  }

  /** 브리지에 버튼·시간 명령을 보내고, 나온 이벤트를 배달한다. */
  async drive(line) {
    const out = await this.bridge.exec(line);
    this.#dispatchLater(out.filter((m) => m.tag === TAG.EVENT).map((m) => decodeEvent(m.bytes)));
    await settle();
  }

  /* 이벤트는 다음 턴에 배달한다. 호출자가 응답을 받은 뒤에야 리스너를 걸기
   * 때문이다 — 실제 BLE 도 notify 는 별도 이벤트 루프 턴에 온다. */
  #dispatchLater(events) {
    if (!events.length) return;
    setTimeout(() => {
      for (const e of events) for (const h of [...this.handlers]) h(e);
    }, 0);
  }
}

const settle = () => new Promise((r) => setTimeout(r, 5));
const toHexStr = (b) => Array.from(b, (x) => x.toString(16).padStart(2, '0')).join('');
const fromHexStr = (s) => {
  const out = new Uint8Array(s.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(s.substr(i * 2, 2), 16);
  return out;
};

/* 승인이 필요한 명령을 실행하고, 기기 버튼을 눌러 준다. */
async function withApproval(bt, start, driveLine = 'a') {
  const p = start();
  await settle();              // SDK 가 PENDING 을 받고 리스너를 걸 때까지
  await bt.drive(driveLine);
  return p;
}

/* ── 준비 ───────────────────────────────────────────────────────────────── */

let bridge, bt, wallet, admin;

before(() => {
  if (!existsSync(BIN)) {
    throw new Error(`${BIN} 이 없습니다. 먼저 'cd firmware/test && make protocol_bridge' 를 실행하세요.`);
  }
  bridge = new Bridge();
  bt = new BridgeTransport(bridge);
  wallet = new NuWallet();
  wallet.transport = bt;                       // 실제 client.ts 를 그대로 쓴다
  admin = new NuWalletAdmin(wallet);
});
after(() => bridge?.kill());

const ABANDON = mnemonicToIndices('abandon '.repeat(11) + 'about');
const ETH_PATH = "m/44'/60'/0'/0/0";

/* ── 테스트 ─────────────────────────────────────────────────────────────── */

test('0x01 GET_VERSION — 프로토콜 버전과 플래그', async () => {
  const info = await wallet.getInfo();
  assert.equal(info.protocolVersion, 1);
  assert.equal(info.initialized, false);
  assert.equal(info.name, 'NuWallet-TEST');
});

test('0x02 GET_STATE — PIN 정보까지 읽힌다', async () => {
  const s = await admin.getLockState();
  assert.equal(s.initialized, false);
  assert.equal(s.hasPin, false);
  assert.equal(s.pinLength, 0);
});

test('0x12 SETUP_RESTORE — 알려진 니모닉이 알려진 주소를 만든다', async () => {
  const addr = await wallet.restore(ABANDON);
  // 널리 알려진 값. SDK 의 체크섬 계산까지 함께 검증된다.
  assert.equal(addr, '0x9858EfFD232B4033E47d90003D41EC34EcaEda94');
});

test('0x20 GET_ADDRESS — SDK 가 부르는 명령을 펌웨어가 처리한다', async () => {
  const addr = await wallet.getAddress('ethereum', ETH_PATH);
  assert.equal(addr, '0x9858EfFD232B4033E47d90003D41EC34EcaEda94');
});

test('0x20 GET_ADDRESS — 공개키도 함께 온다', async () => {
  const acc = await wallet.getAccount('ethereum', ETH_PATH);
  assert.equal(acc.address, '0x9858EfFD232B4033E47d90003D41EC34EcaEda94');
  assert.ok(acc.publicKey.startsWith('0x04'), '비압축 공개키 65바이트여야 한다');
  assert.equal(acc.publicKey.length, 2 + 65 * 2);
});

test('0x31 SIGN_PERSONAL — 승인 후 r·s·v 가 온전히 온다', async () => {
  const sig = await withApproval(bt, () =>
    wallet.signMessage(ETH_PATH, 'hello', { timeoutMs: 5000 }));
  assert.equal(sig.r.length, 2 + 64);
  assert.equal(sig.s.length, 2 + 64);
  assert.ok(sig.recid === 0 || sig.recid === 1, `recid=${sig.recid}`);
  assert.equal(sig.v, sig.recid + 27);
  assert.equal(sig.serialized.length, 2 + 65 * 2, '65바이트가 온전해야 한다');
});

test('0x30 SIGN_TX — EIP-155 v 계산', async () => {
  // RLP([nonce,gasPrice,gas,to,value,data,chainId,0,0]) — 명세의 예제 트랜잭션
  const tx = 'ec098504a817c800825208943535353535353535353535353535353535353535'
           + '880de0b6b3a764000080018080';
  const sig = await withApproval(bt, () =>
    wallet.signTransaction('ethereum', ETH_PATH, tx, 1, { timeoutMs: 5000 }));
  assert.equal(sig.v, sig.recid + 1 * 2 + 35);
  assert.equal(sig.serialized.length, 2 + 65 * 2);
});

test('0x30 SIGN_TX — RLP 이 아니면 기기가 거부한다', async () => {
  await assert.rejects(
    () => wallet.signTransaction('ethereum', ETH_PATH, '0xdeadbeef', 1, { timeoutMs: 5000 }),
    (e) => e instanceof WalletError && e.status === SW.BAD_PARAM);
});

test('0x14 SET_PIN — 승인 후 PIN 이 걸린다', async () => {
  await withApproval(bt, () => admin.setPin([0, 3, 1, 2], { timeoutMs: 5000 }));
  const s = await admin.getLockState();
  assert.equal(s.hasPin, true);
  assert.equal(s.pinLength, 4);
});

test('0x16 LOCK / 0x15 UNLOCK — PIN 은 기기 버튼으로만 들어간다', async () => {
  await admin.lock();
  assert.equal((await admin.getLockState()).locked, true);

  // 잠긴 상태에서는 주소도 못 준다
  await assert.rejects(() => wallet.getAddress('ethereum', ETH_PATH),
    (e) => e instanceof WalletError && e.status === SW.LOCKED);

  await withApproval(bt, () => admin.unlock('', { timeoutMs: 5000 }), 'p0312');
  assert.equal((await admin.getLockState()).locked, false);
  assert.equal(await wallet.getAddress('ethereum', ETH_PATH),
    '0x9858EfFD232B4033E47d90003D41EC34EcaEda94');
});

test('0x13 WIPE — 승인 결과가 SDK 까지 돌아온다', async () => {
  await withApproval(bt, () => wallet.wipe({ timeoutMs: 5000 }));
  const s = await admin.getLockState();
  assert.equal(s.initialized, false, 'WIPE 후에는 지갑이 없어야 한다');
});

test('0x10 SETUP_GENERATE — 단어 수를 보낸다 (엔트로피 비트가 아니라)', async () => {
  const words = await wallet.generateMnemonic(256);
  assert.equal(words.length, 24);
  assert.ok(words.every((w) => w >= 0 && w < 2048));
  const addr = await wallet.confirmSetup(words);
  assert.match(addr, /^0x[0-9a-fA-F]{40}$/);
});

test('모르는 명령은 UNKNOWN_CMD 로 돌아온다', async () => {
  const r = await bt.send(0x7f);
  assert.equal(r.status, SW.UNKNOWN_CMD);
});

test('Solana 는 아직 펌웨어에 없다 — 정확한 이유로 거절한다', async () => {
  // "모르는 명령" 이 아니라 "모르는 체인" 이어야 한다. 전자는 펌웨어가 낡았다는
  // 뜻이고 후자는 이 펌웨어가 그 체인을 안 다룬다는 뜻이라 조치가 다르다.
  await assert.rejects(() => wallet.getAddress('solana'),
    (e) => e instanceof WalletError && e.status === SW.UNSUPPORTED_CHAIN);

  await assert.rejects(
    () => wallet.signTransaction('solana', "m/44'/501'/0'/0'", '0x00', undefined, { timeoutMs: 3000 }),
    (e) => e instanceof WalletError && e.status === SW.UNSUPPORTED_CHAIN);
});

test('경로 명령에 체인 바이트가 빠지면 형식 오류로 잡힌다', async () => {
  // [DEPTH][PATH] 만 보내면 첫 바이트(=DEPTH 5)가 체인 바이트로 읽힌다.
  const noChain = new Uint8Array([5, 0x80,0,0,44, 0x80,0,0,60, 0x80,0,0,0, 0,0,0,0, 0,0,0,0]);
  const r = await bt.send(0x21, noChain);
  assert.equal(r.status, SW.UNSUPPORTED_CHAIN,
    '옛 형식으로 보내면 UNSUPPORTED_CHAIN 이 떠야 한다 — 조용히 다른 주소를 주면 안 된다');
});

test('0xA2 SIGN_RESULT 는 SIG_LEN 을 싣는다', async () => {
  // 길이 접두사가 없으면 SDK 가 추측해야 하고, r[0]==64 인 서명에서 깨진다.
  let sawSigLen = null;
  const off = bt.onEvent((e) => {
    if (e.evt === 0xa2 && e.payload.length > 6) sawSigLen = e.payload[6];
  });
  await withApproval(bt, () => wallet.signMessage(ETH_PATH, 'x', { timeoutMs: 5000 }));
  off();
  assert.equal(sawSigLen, 65);
});
