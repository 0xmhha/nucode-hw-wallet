/**
 * PIN · 잠금 — docs/protocol.md §8.
 *
 * 버튼 4개의 조합이 두 가지 역할을 한다. 섞으면 안 된다.
 *
 *   PIN       사용자가 정한 고정 시퀀스. 기기를 열 때 쓴다.
 *             LED 로 보여주지 않는다 — 사용자가 아는 값이다.
 *   챌린지     기기가 매번 새로 뽑는 랜덤 시퀀스. 서명을 승인할 때 쓴다.
 *             LED 로 보여준다. 목적은 비밀이 아니라 "사람이 여기 있다"의 증명.
 *
 * PIN 을 설정·변경하는 것 자체도 챌린지 승인이 필요하다. 웹에서 PIN 을 보내는
 * 것만으로는 바뀌지 않는다.
 */
import { CMD, EVT, SW, FLAG, PIN, WalletError, hex } from './protocol.js';
import { toChecksumAddress } from './address.js';
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
   * PIN 을 설정하거나 바꾼다. 빈 배열이면 PIN 을 없앤다.
   *
   * @param pin 버튼 번호 배열. 각 값 0..3, 길이 4..8.
   *
   * ⚠️  PIN 이 BLE 로 평문 전송된다. 기기에 입력 UI 가 없어서다. 잠금을 풀
   *     때의 PIN 입력은 기기 버튼으로만 받으며 밖으로 나가지 않는다.
   */
  async setPin(pin: number[], opts: ApprovalOptions = {}): Promise<void> {
    validatePin(pin);
    const payload = new Uint8Array(1 + pin.length);
    payload[0] = pin.length;
    payload.set(pin, 1);
    await this.approve(CMD.SET_PIN, payload, opts);
  }

  /** PIN 을 제거한다. 승인 절차는 설정과 같다. */
  async clearPin(opts: ApprovalOptions = {}): Promise<void> {
    await this.approve(CMD.SET_PIN, new Uint8Array([0]), opts);
  }

  /**
   * 잠금을 해제한다.
   *
   * PIN 이 없으면 즉시 열리고 기본 경로의 주소를 돌려준다.
   * PIN 이 있으면 사용자가 **기기 버튼으로** PIN 을 눌러야 하며, 그동안
   * `onStart`/`onProgress` 가 호출된다.
   *
   * @param passphrase BIP-39 패스프레이즈. 저장되지 않고 이번 세션에만 쓰인다.
   *                   값이 다르면 다른 지갑이 되며 기기는 그것을 구분하지 못한다.
   */
  async unlock(passphrase = '', opts: ApprovalOptions = {}): Promise<string | null> {
    const bytes = new TextEncoder().encode(passphrase);
    if (bytes.length > 64) {
      throw new WalletError(SW.BAD_PARAM, '패스프레이즈는 64바이트를 넘을 수 없습니다');
    }
    const r = await this.transport.send(CMD.UNLOCK, bytes);

    if (r.status === SW.OK) {
      // PIN 이 없어 바로 열린 경우 — 주소가 함께 온다.
      return r.payload.length >= 20 ? toChecksumAddress(hex(r.payload.subarray(0, 20))) : null;
    }
    if (r.status !== SW.PENDING) throw new WalletError(r.status);

    await this.waitForApproval(requestIdOf(r.payload), opts);
    return null;
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
          opts.onStart?.({ requestId, steps: p[4]!, command: p[5]! });
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

export function validatePin(pin: number[]): void {
  if (pin.length === 0) return;                      // 제거는 허용
  if (pin.length < PIN.MIN || pin.length > PIN.MAX) {
    throw new WalletError(SW.BAD_PARAM,
      `PIN 은 ${PIN.MIN}~${PIN.MAX} 자리여야 합니다 (지금 ${pin.length}자리)`);
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
