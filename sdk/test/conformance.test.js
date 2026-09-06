/* 프로토콜 적합성 — SDK 와 펌웨어를 직접 물려서 확인한다.
 *
 * 가짜 기기가 아니라 **보드에 올라갈 코드 그대로**를 자식 프로세스로 띄운다
 * (firmware/test/protocol_bridge). 그래서 여기서 통과하면 명령 번호·페이로드
 * 레이아웃·이벤트가 실제로 맞는다는 뜻이다.
 *
 *   cd firmware/test && make protocol_bridge
 *   cd sdk && node --test test/conformance.test.js
 */
import { describe, test, before, after } from 'node:test';
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
import { encode1559Unsigned, encode1559Signed, encodeAccessList } from '../dist/rlp.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const FW = resolve(HERE, '../../firmware/test');

/* ── 브리지 프로세스 ────────────────────────────────────────────────────── */

class Bridge {
  constructor(bin) {
    this.proc = spawn(bin, [], { stdio: ['pipe', 'pipe', 'inherit'] });
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

/* ── 스위트 ──────────────────────────────────────────────────────────────
 *
 * 같은 프로토콜의 구현이 둘이라 브리지도 둘이다. 둘 다 돌려야 한쪽만 갈라지는
 * 것을 잡는다 — 실제로 Arduino 쪽 `accept_sign` 이 CHAIN_PATH 의 체인 바이트를
 * 건너뛰지 않아 서명이 전부 깨져 있었는데, Zephyr 만 보던 시절에는 전부 초록이었다.
 *
 * caps 는 "아직 구현 안 됨" 이 아니라 **의도된 차이**만 적는다. 의도치 않은
 * 차이는 여기서 실패로 드러나야 한다.                                        */
const SUITES = [
  {
    /* 브리지가 하나인 이유: Arduino 와 Zephyr 가 **같은 코어**를
     * (`firmware/nuwallet/src/core/`) 컴파일한다. 포트만 다르고 프로토콜
     * 로직은 한 벌이라 갈라질 수가 없다.
     *
     * 예전에는 컨트롤러가 두 벌이었고 스위트도 둘이었다. 그때 이 하네스가
     * 보드 쪽에서만 5건의 버그를 잡았다 — 체인 바이트 미소비, RLP 검증 없는
     * 서명, chain code 유출 등. 포트가 늘면 다시 여기에 항목을 추가한다. */
    name: '지갑 코어 — nuwallet/src/core (Arduino·Zephyr 공용)',
    bin: 'protocol_bridge',
    caps: { protocol: 2, pin: true, solana: true },
  },
];

const ABANDON = mnemonicToIndices('abandon '.repeat(11) + 'about');
const ETH_PATH = "m/44'/60'/0'/0/0";
const ABANDON_ADDR = '0x9858EfFD232B4033E47d90003D41EC34EcaEda94';

/* ── 골든 벡터 ───────────────────────────────────────────────────────────
 *
 * "abandon x11 about" 에서 두 구현이 실제로 내는 값이다. 컨트롤러를 하나로
 * 합치는 동안 이 값이 바뀌면 파생이나 해싱을 잘못 건드린 것이다. 통합 후에도
 * 같은 값이 나와야 한다 — 그게 통합이 안전했다는 유일한 증거다.
 *
 * ECDSA 는 RFC 6979 결정론적 서명이라 값이 고정된다. 두 구현이 같은 값을 내는
 * 것은 암호 스택이 실제로 공유되고 있다는 뜻이기도 하다.                    */
const GOLDEN = {
  addresses: {
    "m/44'/60'/0'/0/0": '0x9858EfFD232B4033E47d90003D41EC34EcaEda94',
    "m/44'/60'/0'/0/5": '0xA40cFBFc8534FFC84E20a7d8bBC3729B26a35F6f',
    "m/44'/60'/1'/0/0": '0x78839F6054d7ed13918bAe0473BA31b1Ca9D7265',
  },
  personalMessage: 'nuwallet golden vector',
  personalSig:
    '0x7cce1719b1374df88d7ee26aee94a18c830d2eacd87a05a6cbe1cb90d3ddfd87'
    + '2a0732e684601b827b62e0c20b33538729be07ecf634b5a579c037697744a0b91b',
  /* docs/protocol.md 의 예제 트랜잭션, chainId=1 */
  tx: 'ec098504a817c800825208943535353535353535353535353535353535353535'
    + '880de0b6b3a764000080018080',
  txSig:
    '0x119c10a087377a1845bc0dbab4db97372316650ee8aa6e0c62c9cc1f307de20f'
    + '7aed856495a3303f3260b5975bb2cf20313b42eedbbcbfff9fbfaead4735ffe525',
  /* ⚠️ 호스트 브리지의 Ed25519 는 대역이라 이 값은 **보드와 다르다.**
   *    그래도 고정할 값어치는 있다 — 대역은 파생된 키의 함수이므로, 이 값이
   *    바뀌면 SLIP-0010 파생이 바뀐 것이다. 보드 값은 실기기로만 확인한다. */
  solanaAddressHostOnly: 'G1W8r1h7nV6CRG3t165DAZQncY43smwDt7GFrj2DETku',
};

for (const suite of SUITES) describe(suite.name, () => {
  const BIN = resolve(FW, suite.bin);
  let bridge, bt, wallet, admin;

  before(() => {
    if (!existsSync(BIN)) {
      throw new Error(`${BIN} 이 없습니다. 먼저 'cd firmware/test && make ${suite.bin}' 를 실행하세요.`);
    }
    bridge = new Bridge(BIN);
    bt = new BridgeTransport(bridge);
    wallet = new NuWallet();
    wallet.transport = bt;                     // 실제 client.ts 를 그대로 쓴다
    admin = new NuWalletAdmin(wallet);
  });
  after(() => bridge?.kill());

  /* v2 는 셋업 끝에 사용자가 기기에서 PIN 을 정한다. v1 은 바로 저장된다. */
  const setupWallet = async () => (suite.caps.protocol >= 2
    ? withApproval(bt, () => wallet.restore(ABANDON, { timeoutMs: 5000 }))
    : wallet.restore(ABANDON));

  test('0x01 GET_VERSION — 프로토콜 버전과 플래그', async () => {
    const info = await wallet.getInfo();
    assert.equal(info.protocolVersion, suite.caps.protocol);
    assert.equal(info.initialized, false);
    assert.equal(info.name, 'NuWallet-TEST');
  });

  test('0x02 GET_STATE — 잠금 상태를 읽는다', async () => {
    const s = await admin.getLockState();
    assert.equal(s.initialized, false);
    assert.equal(s.hasPin, false);
    assert.equal(s.pinLength, 0);
  });

  test('0x12 SETUP_RESTORE — 알려진 니모닉이 알려진 주소를 만든다', async () => {
    // 널리 알려진 값. SDK 의 체크섬 계산까지 함께 검증된다.
    assert.equal(await setupWallet(), ABANDON_ADDR);
  });

  test('0x21 GET_CHAIN_ADDRESS — SDK 가 부르는 명령을 펌웨어가 처리한다', async () => {
    assert.equal(await wallet.getAddress('ethereum', ETH_PATH), ABANDON_ADDR);
  });

  test('0x20 GET_ADDRESS — 주소와 공개키가 길이 접두사로 온다', async () => {
    // 응답은 [ADDR_LEN][ADDRESS][PUBKEY_LEN][PUBKEY]. 고정 오프셋으로 읽으면
    // 체인이 늘어날 때 깨지고, chain code 를 뒤에 붙여 보내던 시절과도 섞인다.
    const acc = await wallet.getAccount('ethereum', ETH_PATH);
    assert.equal(acc.address, ABANDON_ADDR);
    assert.ok(acc.publicKey.startsWith('0x04'), `비압축 공개키가 아님: ${acc.publicKey}`);
    assert.equal(acc.publicKey.length, 2 + 65 * 2, '공개키는 65바이트다');
  });

  test('골든 벡터 — 주소가 바뀌지 않았다', async () => {
    for (const [path, expected] of Object.entries(GOLDEN.addresses)) {
      assert.equal(await wallet.getAddress('ethereum', path), expected, `경로 ${path}`);
    }
  });

  test('골든 벡터 — 서명이 바뀌지 않았다', async () => {
    const msg = await withApproval(bt, () =>
      wallet.signMessage(ETH_PATH, GOLDEN.personalMessage, { timeoutMs: 5000 }));
    assert.equal(msg.serialized, GOLDEN.personalSig, 'personal_sign 이 달라졌다');

    const tx = await withApproval(bt, () =>
      wallet.signTransaction('ethereum', ETH_PATH, GOLDEN.tx, 1, { timeoutMs: 5000 }));
    assert.equal(tx.serialized, GOLDEN.txSig, 'signTransaction 이 달라졌다');
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

  test('0x30 SIGN_TX — SDK 가 만든 EIP-1559 를 펌웨어 파서가 받아들인다', async () => {
    // SDK 의 인코더(rlp.ts)와 기기의 파서(core/rlp.c)는 따로 짠 코드다. 둘이
    // 같은 형식을 뜻하는지 확인할 방법은 실제로 물려 보는 것뿐이다. 기기는
    // 항목 수·순서·accessList 가 리스트인지까지 본다 (rlp.c 의 type 2 가지).
    const tx = encode1559Unsigned({
      chainId: 1, nonce: 9n,
      maxPriorityFeePerGas: 1_500_000_000n, maxFeePerGas: 30_000_000_000n,
      gas: 21000n,
      to: fromHexStr('3535353535353535353535353535353535353535'),
      value: 1_000_000_000_000_000_000n,
      data: new Uint8Array(),
      accessList: encodeAccessList([{
        address: '0x' + 'ab'.repeat(20),
        storageKeys: ['0x' + '01'.repeat(32)],
      }]),
    });

    const sig = await withApproval(bt, () =>
      wallet.signTransaction('ethereum', ETH_PATH, tx, undefined, { timeoutMs: 5000 }));

    // typed 트랜잭션의 v 는 yParity 다. EIP-155 처럼 chainId 를 섞으면 안 된다.
    assert.ok(sig.v === 0 || sig.v === 1, `typed v 는 yParity 여야 한다 (v=${sig.v})`);
    assert.equal(sig.v, sig.recid);
    assert.equal(sig.serialized.length, 2 + 65 * 2);

    // 최종 바이트도 기기가 서명한 것과 같은 본문을 담아야 한다.
    const raw = encode1559Signed(
      {
        chainId: 1, nonce: 9n,
        maxPriorityFeePerGas: 1_500_000_000n, maxFeePerGas: 30_000_000_000n,
        gas: 21000n,
        to: fromHexStr('3535353535353535353535353535353535353535'),
        value: 1_000_000_000_000_000_000n,
        data: new Uint8Array(),
        accessList: encodeAccessList([{
          address: '0x' + 'ab'.repeat(20),
          storageKeys: ['0x' + '01'.repeat(32)],
        }]),
      },
      sig.v, fromHexStr(sig.r.slice(2)), fromHexStr(sig.s.slice(2)));
    assert.equal(raw[0], 0x02);
    // 서명 세 항목이 더 붙으므로 반드시 길어진다. 바깥 리스트 길이 접두사가
    // 달라져서 앞부분 바이트를 그대로 비교할 수는 없다.
    assert.ok(raw.length > tx.length + 64, `서명본이 짧다 (${raw.length} vs ${tx.length})`);
  });

  test('0x30 SIGN_TX — 항목이 모자란 type 2 는 거부한다', async () => {
    // accessList 를 빼면 여덟 항목이 된다. 기기가 형식을 실제로 보는지 확인.
    const short = new Uint8Array([0x02, 0xc4, 0x01, 0x09, 0x01, 0x02]);
    await assert.rejects(
      () => wallet.signTransaction('ethereum', ETH_PATH, short, undefined, { timeoutMs: 5000 }),
      (e) => e instanceof WalletError && e.status === SW.BAD_PARAM);
  });

  test('0x30 SIGN_TX — RLP 이 아니면 기기가 거부한다', async () => {
    await assert.rejects(
      () => wallet.signTransaction('ethereum', ETH_PATH, '0xdeadbeef', 1, { timeoutMs: 5000 }),
      (e) => e instanceof WalletError && e.status === SW.BAD_PARAM);
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

  /* ── 체인 지원 여부 ─────────────────────────────────────────────────── */

  if (suite.caps.solana) {
    test('Solana 주소와 서명이 나온다', async () => {
      // ⚠️ 호스트 브리지의 Ed25519 는 대역이라 값은 검증하지 못한다.
      //    모양과 프로토콜 흐름만 본다 — host_arduino/host_shims.cpp 참고.
      const acc = await wallet.getAccount('solana');
      assert.equal(acc.chain, 'solana');
      assert.ok(acc.address.length >= 32, `base58 주소가 아님: ${acc.address}`);
      // 대역이 파생된 키의 함수라, 이 값이 바뀌면 SLIP-0010 파생이 바뀐 것이다.
      assert.equal(acc.address, GOLDEN.solanaAddressHostOnly,
        'SLIP-0010 파생이 달라졌다 (호스트 대역 기준)');

      const sig = await withApproval(bt, () =>
        wallet.signTransaction('solana', "m/44'/501'/0'/0'", '0x0102', undefined,
                               { timeoutMs: 5000 }));
      assert.equal(sig.serialized.length, 2 + 64 * 2, 'Ed25519 서명은 64바이트다');
    });
  } else {
    test('Solana 는 이 펌웨어에 없다 — 정확한 이유로 거절한다', async () => {
      // "모르는 명령" 이 아니라 "모르는 체인" 이어야 한다. 전자는 펌웨어가 낡았다는
      // 뜻이고 후자는 이 펌웨어가 그 체인을 안 다룬다는 뜻이라 조치가 다르다.
      await assert.rejects(() => wallet.getAddress('solana'),
        (e) => e instanceof WalletError && e.status === SW.UNSUPPORTED_CHAIN);
    });
  }

  /* ── PIN · 잠금 ─────────────────────────────────────────────────────── */

  if (suite.caps.pin) {
    test('셋업이 PIN 을 강제한다 — PIN 없는 레코드는 만들어지지 않는다', async () => {
      // PIN 없는 레코드는 봉인 키가 PBKDF2("", salt) 이고 salt 는 평문이라
      // 플래시만 뜨면 열린다. v2 는 셋업 절차 안에서 반드시 PIN 을 받는다.
      const s = await admin.getLockState();
      assert.equal(s.hasPin, true, '셋업 직후 PIN 이 걸려 있어야 한다');
      assert.equal(s.pinLength, 6, 'PIN 은 6자리 고정이다');
    });

    test('0x14 SET_PIN — 값을 보내지 않는다. 기기에서 두 번 받는다', async () => {
      await withApproval(bt, () => admin.changePin({ timeoutMs: 5000 }));
      assert.equal((await admin.getLockState()).pinLength, 6);
    });

    test('0x16 LOCK / 0x15 UNLOCK — PIN 은 기기 버튼으로만 들어간다', async () => {
      await admin.lock();
      assert.equal((await admin.getLockState()).locked, true);

      // 잠긴 상태에서는 주소도 못 준다
      await assert.rejects(() => wallet.getAddress('ethereum', ETH_PATH),
        (e) => e instanceof WalletError && e.status === SW.LOCKED);

      await withApproval(bt, () => admin.unlock('', { timeoutMs: 5000 }));
      assert.equal((await admin.getLockState()).locked, false);
      assert.equal(await wallet.getAddress('ethereum', ETH_PATH), ABANDON_ADDR);
    });
  } else {
    test('PIN·잠금 명령이 없다는 것을 명시적으로 못박는다', async () => {
      // 이 펌웨어에는 PIN 도 잠금도 없다. 웹 /setup 의 해당 버튼은 이 보드에서
      // 동작하지 않는다. 조용히 성공하는 것보다 UNKNOWN_CMD 가 낫다 —
      // 잠긴 줄 알았는데 안 잠겨 있는 것이 최악이다.
      for (const [name, cmd] of [['SET_PIN', 0x14], ['UNLOCK', 0x15], ['LOCK', 0x16]]) {
        const r = await bt.send(cmd, new Uint8Array([4, 0, 3, 1, 2]));
        assert.equal(r.status, SW.UNKNOWN_CMD, `${name} 은 UNKNOWN_CMD 여야 한다`);
      }
      assert.equal((await admin.getLockState()).hasPin, false);
    });
  }

  /* ── 마무리 ─────────────────────────────────────────────────────────── */

  test('0x13 WIPE — 승인 결과가 SDK 까지 돌아온다', async () => {
    await withApproval(bt, () => wallet.wipe({ timeoutMs: 5000 }));
    const s = await admin.getLockState();
    assert.equal(s.initialized, false, 'WIPE 후에는 지갑이 없어야 한다');
  });

  test('0x10 SETUP_GENERATE — 단어 수를 보낸다 (엔트로피 비트가 아니라)', async () => {
    const words = await wallet.generateMnemonic(256);
    assert.equal(words.length, 24);
    assert.ok(words.every((w) => w >= 0 && w < 2048));
    const addr = suite.caps.protocol >= 2
      ? await withApproval(bt, () => wallet.confirmSetup(words, { timeoutMs: 5000 }))
      : await wallet.confirmSetup(words);
    assert.match(addr, /^0x[0-9a-fA-F]{40}$/);
  });

  test('모르는 명령은 UNKNOWN_CMD 로 돌아온다', async () => {
    const r = await bt.send(0x7f);
    assert.equal(r.status, SW.UNKNOWN_CMD);
  });
});
