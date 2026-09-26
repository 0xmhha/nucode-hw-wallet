/* 실기기 테스트 — 브라우저 없이 **진짜 SDK** 로 보드를 상대한다.
 *
 * 다른 테스트와 다른 점: 여기서는 펌웨어도 SDK 도 흉내내지 않는다.
 *   conformance.test.js  펌웨어 코어를 호스트에서 컴파일해 SDK 와 물린다
 *   device.test.js       BLE 로 진짜 보드를 상대한다 (이 파일)
 *
 * `webbluetooth` 가 navigator.bluetooth 를 Node 에 채워 준다. 그래서 웹앱이 쓰는
 * transport.ts / client.ts 를 **그대로** 쓴다 — 프로토콜을 한 번 더 구현하지
 * 않는다. 세 번째 구현을 만들면 그것이 갈라지는 것이 다음 버그가 된다.
 *
 * 보드가 없으면 통째로 건너뛴다. CI 에서는 자동으로 skip 된다.
 *
 *   cd sdk && npm i --no-save webbluetooth
 *   node --test test/device.test.js
 *
 * 버튼을 눌러야 하는 항목은 NUWALLET_DEVICE_INTERACTIVE=1 일 때만 돈다.
 * 사람이 보드 앞에 있어야 하므로 기본은 조회성 검사만 한다.
 */
import { test, before, after, describe } from 'node:test';
import assert from 'node:assert/strict';

const INTERACTIVE = process.env.NUWALLET_DEVICE_INTERACTIVE === '1';
const SCAN_SECONDS = Number(process.env.NUWALLET_DEVICE_SCAN ?? 15);

/* 널리 알려진 테스트 니모닉에서 나오는 값. conformance.test.js 의 GOLDEN 과
 * 같아야 한다 — 같지 않으면 실기기 파생이 호스트와 다르다는 뜻이다. */
const GOLDEN = {
  addresses: {
    "m/44'/60'/0'/0/0": '0x9858EfFD232B4033E47d90003D41EC34EcaEda94',
    "m/44'/60'/0'/0/5": '0xA40cFBFc8534FFC84E20a7d8bBC3729B26a35F6f',
  },
  personalMessage: 'nuwallet golden vector',
  personalSig:
    '0x7cce1719b1374df88d7ee26aee94a18c830d2eacd87a05a6cbe1cb90d3ddfd87'
    + '2a0732e684601b827b62e0c20b33538729be07ecf634b5a579c037697744a0b91b',
};

/* 사람이 버튼을 누를 때까지 기다린다.
 *
 * 기기의 승인 제한은 60초다. 사람이 보드 앞에 앉기까지 그보다 오래 걸릴 수
 * 있으므로, 만료되면 다시 건다. 진행 상황을 그때그때 찍어서 눌린 것이 인식됐는지
 * 바로 보이게 한다. */
async function withButtons(label, run, { rounds = 10 } = {}) {
  const stamp = () => new Date().toTimeString().slice(0, 8);
  let kind = 0, seen = 0;
  const opts = {
    timeoutMs: 130_000,          /* 기기 제한 60초 x 2단계 + 여유 */
    onStart: (i) => { kind = i.kind; seen = 0;
      console.log(`  [${stamp()}] ▶ ${label} — ${KIND[i.kind]}`); },
    onProgress: (i) => {
      /* PIN 설정은 6자리를 채우면 step 이 0 으로 돌아간다 — 재입력 단계다.
       * 그냥 "0자리" 라고만 찍으면 사용자는 무엇을 기다리는지 알 수 없다. */
      if (kind === 2 && i.step === 0 && seen > 0) {
        console.log(`  [${stamp()}]   ↻ 같은 PIN 을 한 번 더 누르세요`
                    + ' (보드 LED 가 번갈아 깜빡입니다)');
      } else {
        console.log(`  [${stamp()}]     ${i.step}자리 인식`);
      }
      seen = i.step;
    },
  };
  for (let i = 0; i < rounds; i++) {
    try {
      return await run(opts);
    } catch (e) {
      const retriable = e?.status === SW.CHALLENGE_TIMEOUT;
      if (!retriable) throw e;
      console.log(`  [${stamp()}] 시간 초과 — 다시 겁니다 (${i + 1}/${rounds})`);
      /* 기기 쪽 요청이 정리되고 늦게 오는 알림이 지나가기를 기다린다.
       * 곧바로 다시 걸면 앞 요청의 응답이 새 요청에 짝지어져 프레이밍 오류가 난다. */
      await new Promise((r) => setTimeout(r, 1500));
    }
  }
  throw new Error(`${label}: 버튼 입력을 받지 못했습니다`);
}

const KIND = {
  0: '켜진 LED 를 한 번 누르세요',
  1: 'PIN 6자리를 누르세요',
  2: '새 PIN 6자리를 누르고, 같은 값을 한 번 더',
};

/* 보드가 있는지는 **모듈 로드 시점에** 정해야 한다. describe 의 skip 옵션은
 * 등록할 때 한 번 읽히므로, before() 에서 정하면 늦는다. (함수를 넘기면
 * 언제나 truthy 라 통째로 건너뛴다 — 실제로 그렇게 당했다.) */
let NuWallet, NuWalletAdmin, WalletError, SW;
let skipReason = false;

try {
  const { Bluetooth } = await import('webbluetooth');
  /* Web Bluetooth 는 사용자 제스처 안에서 기기를 고르게 되어 있다. Node 에는
   * 그런 것이 없으므로 처음 찾은 기기를 고른다.
   *
   * Node 24 의 navigator 는 getter 뿐이라 대입이 안 된다. 정의를 덮어쓴다. */
  const bluetooth = new Bluetooth({ deviceFound: () => true, scanTime: SCAN_SECONDS });
  Object.defineProperty(globalThis, 'navigator', {
    value: { ...globalThis.navigator, bluetooth },
    configurable: true,
    writable: true,
  });
  ({ NuWallet, NuWalletAdmin, WalletError, SW } = await import('../dist/index.js'));

  /* 스캔만 해서 보드가 있는지 본다. 여기서 연결까지 하면, 붙은 뒤에 나는
   * 문제(페어링 불일치 같은)까지 "보드 없음" 으로 덮어 버린다. 그건 감출 게
   * 아니라 테스트가 실패로 알려야 하는 것이다. */
  const { SERVICE_UUID } = await import('../dist/protocol.js');
  await navigator.bluetooth.requestDevice({ filters: [{ services: [SERVICE_UUID] }] });
} catch (e) {
  skipReason = `보드를 찾지 못했습니다: ${e.message}`;
  console.log(`\n  실기기 테스트를 건너뜁니다 — ${e.message}\n`);
}

/* BLE 스택이 연결을 접는 과정에서 늦게 던지는 것이 있다. 스킵하기로 정한
 * 뒤에는 그것 때문에 파일 전체가 실패로 잡히면 안 된다. */
process.on('unhandledRejection', (e) => {
  if (skipReason) return;
  throw e;
});

describe('실기기 (NU-40 DK)', { skip: skipReason }, () => {
  let wallet, admin;

  before(async () => {
    wallet = new NuWallet();
    admin = new NuWalletAdmin(wallet);
    try {
      await wallet.connect();
    } catch (e) {
      /* 가장 흔한 원인은 본딩 키 불일치다 — 펌웨어를 다시 올렸거나 보드에서
       * 공장 초기화를 했으면 보드는 페어링을 잊었는데 호스트는 기억한다. */
      throw new Error(
        `${e.message}\n\n`
        + '  호스트가 옛 페어링을 붙잡고 있을 수 있습니다.\n'
        + '  macOS: 시스템 설정 → Bluetooth → NuWallet-… → ⓘ → "이 기기 잊기"\n'
        + '  펌웨어를 다시 올렸거나 공장 초기화를 했다면 반드시 필요합니다.');
    }
  });
  after(async () => {
    await wallet?.disconnect();
    /* webbluetooth 의 네이티브 백엔드가 프로세스를 접는 과정에서 잡을 수 없는
     * Napi 예외를 던진다 (libc++abi terminating). 테스트는 다 끝난 뒤라
     * 결과에는 영향이 없지만 종료 코드가 더러워진다. 여기서 깔끔히 끝낸다.
     * 실패가 있었으면 그 코드로 나간다. */
    await new Promise((r) => setTimeout(r, 200));
    process.exit(process.exitCode ?? 0);
  });

  test('프로토콜 v2 펌웨어다', async () => {
    const info = await wallet.getInfo();
    assert.equal(info.protocolVersion, 2,
      `보드가 v${info.protocolVersion} 입니다 — 최신 펌웨어를 올리세요`);
    assert.match(info.name, /^NuWallet-[A-Z2-9]{6}$/,
      `기기 이름 형식이 다릅니다: ${info.name}`);
  });

  test('잠긴 상태에서는 주소를 주지 않는다', async () => {
    const s = await admin.getLockState();
    if (!s.initialized) return;              // 지갑이 없으면 이 검사는 의미가 없다
    if (!s.locked) return;                   // 세션이 열려 있으면 건너뛴다
    await assert.rejects(() => wallet.getAddress('ethereum'),
      (e) => e instanceof WalletError && e.status === SW.LOCKED);
  });

  test('셋업 전에는 PIN 이 걸려 있지 않다', async () => {
    const s = await admin.getLockState();
    if (s.initialized) {
      assert.equal(s.hasPin, true, 'v2 는 PIN 없는 지갑을 만들지 않는다');
      assert.equal(s.pinLength, 6, 'PIN 은 6자리 고정이다');
    }
  });

  /* ── 사람이 버튼을 눌러야 하는 것 ─────────────────────────────────────
   *
   * 호스트 테스트가 검증하지 못하는 것만 남긴다 — 하드웨어(버튼·LED·플래시·
   * CryptoCell)와 타이밍(PBKDF2 중 링크 유지)이다. 프로토콜 자체는
   * conformance.test.js 가 이미 본다. */
  describe('버튼 입력 필요', { skip: !INTERACTIVE && 'NUWALLET_DEVICE_INTERACTIVE=1 로 실행하세요' }, () => {
    test('셋업하거나 잠금을 해제하고, 세션이 유지된다', async () => {
      let s = await admin.getLockState();

      if (!s.initialized) {
        /* 지갑이 없으면 알려진 니모닉으로 만든다. 셋업 자체가 기기에서 PIN 을
         * 받으므로, 이 한 번으로 셋업 경로와 PIN 설정 경로를 함께 본다. */
        const { mnemonicToIndices } = await import('../dist/wordlist.js');
        const words = mnemonicToIndices('abandon '.repeat(11) + 'about');
        const addr = await withButtons('셋업', (o) => wallet.restore(words, o));
        assert.equal(addr, GOLDEN.addresses["m/44'/60'/0'/0/0"],
          '셋업 직후 주소가 호스트와 다르다');
        s = await admin.getLockState();
        assert.equal(s.hasPin, true, '셋업이 PIN 을 받지 않았다');
        assert.equal(s.pinLength, 6);
      } else if (s.locked) {
        await withButtons('잠금 해제', (o) => admin.unlock('', o));
      }
      assert.equal((await admin.getLockState()).locked, false);

      /* 시계 스큐 언더플로가 있으면 여기서 다시 잠긴다 (실기기에서 잡힌 버그). */
      for (let i = 0; i < 3; i++) {
        await new Promise((r) => setTimeout(r, 2000));
        assert.equal((await admin.getLockState()).locked, false,
          '세션이 스스로 닫혔다 — 시계 스큐를 의심하라');
      }
    });

    test('주소가 골든 벡터와 일치한다', async () => {
      for (const [path, expected] of Object.entries(GOLDEN.addresses)) {
        assert.equal(await wallet.getAddress('ethereum', path), expected,
          `${path} 파생이 호스트와 다르다`);
      }
    });

    test('Solana 주소가 나온다 (CryptoCell Ed25519)', async () => {
      const acc = await wallet.getAccount('solana');
      assert.ok(acc.address.length >= 32, `base58 주소가 아님: ${acc.address}`);
      assert.equal(acc.publicKey.length, 2 + 32 * 2);
    });

    test('personal_sign 이 골든 벡터와 일치한다', async () => {
      const sig = await withButtons('서명 승인', (o) =>
        wallet.signMessage("m/44'/60'/0'/0/0", GOLDEN.personalMessage, o));
      assert.equal(sig.serialized, GOLDEN.personalSig,
        'RFC 6979 결정론적 서명이 호스트와 다르다');
      assert.equal(sig.v, sig.recid + 27);
    });

    test('RLP 이 아니면 보드가 거부한다', async () => {
      await assert.rejects(
        () => wallet.signTransaction('ethereum', "m/44'/60'/0'/0/0", '0xdeadbeef', 1,
                                     { timeoutMs: 10_000 }),
        (e) => e instanceof WalletError && e.status === SW.BAD_PARAM,
        '보드가 파싱되지 않는 바이트에 서명하려 한다');
    });
  });
});
