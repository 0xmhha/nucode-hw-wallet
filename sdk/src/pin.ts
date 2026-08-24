/**
 * PIN · 잠금 — docs/protocol.md §8.
 *
 * v2 부터 PIN 값은 **BLE 로 오가지 않는다.** 사용자가 기기 버튼으로 6번 누르고,
 * 설정할 때는 한 번 더 눌러 확인한다. 호스트는 PIN 을 모른다 — PIN 의 목적이
 * "호스트가 감염돼도 기기를 못 연다" 이므로 호스트가 알면 앞뒤가 맞지 않는다.
 *
 * 그래서 이 파일의 API 는 PIN 값을 받지 않는다. 절차를 시작시키고, 기기가 보내는
 * 진행 이벤트를 콜백으로 전달할 뿐이다.
 */
import { CMD, EVT, SW, FLAG, PIN, APPROVAL, WalletError,
         type ApprovalKind } from './protocol.js';
import type { NuWallet } from './client.js';
import type { ChallengeCallbacks } from './types.js';

const DEFAULT_TIMEOUT = 70_000;   // 기기 챌린지 제한 60초 + 여유

export interface ApprovalOptions extends ChallengeCallbacks {
  timeoutMs?: number;
  signal?: AbortSignal;
}

export interface LockState {
  initialized: boolean;
  locked: boolean;
  challengeActive: boolean;
  hasPin: boolean;
  /** 설정된 PIN 길이. 0 이면 미설정. 값 자체는 기기 밖으로 나오지 않는다. */
  pinLength: number;
  attemptsLeft: number;
  requestId: number;
}

/**
 * 기기 설정 API. 서명 경로(NuWallet)와 분리해 둔다 — 설정 웹만 이걸 쓰고,
 * DApp 은 쓸 일이 없다.
 */
export class NuWalletAdmin {
  constructor(readonly wallet: NuWallet) {}

  private get transport() { return this.wallet.transport; }

  /** GET_STATE 를 PIN 정보까지 포함해 읽는다. */
  async getLockState(): Promise<LockState> {
    const r = await this.transport.send(CMD.GET_STATE);
    if (r.status !== SW.OK) throw new WalletError(r.status);
    const p = r.payload;
    if (p.length < 6) throw new WalletError(SW.DEVICE_ERROR, 'GET_STATE 응답이 짧습니다');
    const dv = new DataView(p.buffer, p.byteOffset, p.byteLength);
    return {
      initialized:     (p[0]! & FLAG.INITIALIZED) !== 0,
      locked:          (p[0]! & FLAG.LOCKED) !== 0,
      challengeActive: (p[0]! & FLAG.CHALLENGE_ACTIVE) !== 0,
      hasPin:          (p[0]! & FLAG.HAS_PIN) !== 0,
      requestId:       dv.getUint32(1, false),
      attemptsLeft:    p[5]!,
      pinLength:       p.length >= 7 ? p[6]! : 0,
    };
  }

  /**
   * PIN 을 바꾼다. 잠금이 풀려 있어야 한다.
   *
   * 값은 넘기지 않는다 — 사용자가 기기에서 6번 누르고, 오타를 잡기 위해 한 번
   * 더 누른다. 두 입력이 다르면 `SW.PIN_MISMATCH` 로 실패한다.
   *
   * PIN 은 없앨 수 없다. v2 는 셋업 때 반드시 정하게 하며, PIN 없는 레코드는
   * 봉인 키가 공개값이라 플래시만 뜨면 열린다.
   */
  async changePin(opts: ApprovalOptions = {}): Promise<void> {
    await this.approve(CMD.SET_PIN, new Uint8Array(), opts);
  }

  /**
   * 잠금을 해제한다. 사용자가 **기기 버튼으로** PIN 6자리를 눌러야 하며,
   * 그동안 `onStart`/`onProgress` 가 호출된다.
   *
   * 열린 세션은 조용하면 기기가 스스로 닫는다 (5분). 연결이 끊겨도 닫힌다.
   *
   * @param passphrase BIP-39 패스프레이즈. 저장되지 않고 이번 세션에만 쓰인다.
   *                   값이 다르면 다른 지갑이 되며 기기는 그것을 구분하지 못한다.
   */
  async unlock(passphrase = '', opts: ApprovalOptions = {}): Promise<void> {
    const bytes = new TextEncoder().encode(passphrase);
    if (bytes.length > 64) {
      throw new WalletError(SW.BAD_PARAM, '패스프레이즈는 64바이트를 넘을 수 없습니다');
    }
    const r = await this.transport.send(CMD.UNLOCK, bytes);
    // v2 는 PIN 이 항상 있으므로 즉시 열리는 경우가 없다.
    if (r.status !== SW.PENDING) throw new WalletError(r.status);
    await this.waitForApproval(requestIdOf(r.payload), opts);
  }

  /** 즉시 잠근다. RAM 의 시드가 지워진다. */
  async lock(): Promise<void> {
    const r = await this.transport.send(CMD.LOCK);
    if (r.status !== SW.OK) throw new WalletError(r.status);
  }

  /** PENDING 을 받고 REQUEST_RESULT 이벤트를 기다리는 명령. */
  private async approve(cmd: number, payload: Uint8Array, opts: ApprovalOptions): Promise<void> {
    const r = await this.transport.send(cmd, payload);
    if (r.status !== SW.PENDING) throw new WalletError(r.status);
    await this.waitForApproval(requestIdOf(r.payload), opts);
  }

  /**
   * 기기가 승인 결과를 이벤트로 알려줄 때까지 기다린다.
   * 서명과 달리 결과 페이로드가 없으므로 성공/실패만 본다.
   */
  private waitForApproval(requestId: number, opts: ApprovalOptions): Promise<void> {
    return new Promise<void>((resolve, reject) => {
      let done = false;
      const finish = (fn: () => void) => {
        if (done) return;
        done = true;
        clearTimeout(timer);
        off();
        offDisc();
        opts.signal?.removeEventListener('abort', onAbort);
        fn();
      };
      const cancel = () => {
        void this.transport.send(CMD.CANCEL, be32(requestId)).catch(() => undefined);
      };
      const onAbort = () => {
        cancel();
        finish(() => reject(new WalletError(SW.USER_REJECTED, '호출자가 취소했습니다')));
      };
      const timer = setTimeout(() => {
        cancel();
        finish(() => reject(new WalletError(SW.CHALLENGE_TIMEOUT)));
      }, opts.timeoutMs ?? DEFAULT_TIMEOUT);

      const off = this.transport.onEvent((e) => {
        const p = e.payload;
        if (p.length < 4) return;
        const dv = new DataView(p.buffer, p.byteOffset, p.byteLength);
        if (dv.getUint32(0, false) !== requestId) return;

        if (e.evt === EVT.CHALLENGE_STARTED && p.length >= 6) {
          // v2 는 KIND 를 함께 준다. 호스트가 "PIN 을 누르세요" 와 "LED 를 보고
          // 누르세요" 중 무엇을 띄울지 추측하지 않아도 된다.
          opts.onStart?.({
            requestId, steps: p[4]!, command: p[5]!,
            kind: p.length >= 7 ? (p[6]! as ApprovalKind) : APPROVAL.CONFIRM,
          });
        } else if (e.evt === EVT.CHALLENGE_PROGRESS && p.length >= 6) {
          opts.onProgress?.({ requestId, step: p[4]!, attemptsLeft: p[5]! });
        } else if (e.evt === EVT.REQUEST_RESULT && p.length >= 7) {
          const status = dv.getUint16(5, false);
          if (status === SW.OK) finish(resolve);
          else finish(() => reject(new WalletError(status)));
        }
      });
      const offDisc = this.transport.onDisconnect(() => {
        finish(() => reject(new WalletError(SW.DEVICE_ERROR, '기기 연결이 끊겼습니다')));
      });
      opts.signal?.addEventListener('abort', onAbort);
    });
  }
}

// ── 헬퍼 ────────────────────────────────────────────────────────────────────

/**
 * 화면에서 사용자가 고른 패턴이 기기 규칙에 맞는지 본다.
 *
 * ⚠️  이 값은 기기로 보내지 않는다 — v2 의 PIN 은 버튼으로만 들어간다.
 *     설정 화면이 "6자리를 채웠는지" 를 확인하는 용도로만 쓴다.
 */
export function validatePin(pin: number[]): void {
  if (pin.length !== PIN.LEN) {
    throw new WalletError(SW.BAD_PARAM,
      `PIN 은 ${PIN.LEN}자리여야 합니다 (지금 ${pin.length}자리)`);
  }
  for (const b of pin) {
    if (!Number.isInteger(b) || b < 0 || b >= PIN.BUTTONS) {
      throw new WalletError(SW.BAD_PARAM, `버튼 번호는 0~${PIN.BUTTONS - 1} 입니다: ${b}`);
    }
  }
}

function requestIdOf(payload: Uint8Array): number {
  if (payload.length < 4) throw new WalletError(SW.DEVICE_ERROR, 'requestId 가 없습니다');
  return new DataView(payload.buffer, payload.byteOffset, payload.byteLength).getUint32(0, false);
}

function be32(v: number): Uint8Array {
  const b = new Uint8Array(4);
  new DataView(b.buffer).setUint32(0, v >>> 0, false);
  return b;
}
