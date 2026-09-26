/**
 * 기기가 준 서명 바이트를 체인별 서명으로 푼다.
 *
 * 기기는 SIG_LEN ‖ SIGNATURE 를 준다 (docs/protocol.md §6). Ethereum 은
 * r ‖ s ‖ recid 65바이트이고, v 는 서명 종류에 따라 호스트가 계산한다.
 */
import { SW, WalletError, concat, hex } from './protocol.js';
import type { Signature } from './types.js';

/** SIG_LEN ‖ SIGNATURE 에서 서명만 꺼낸다. */
export function takeSig(raw: Uint8Array, expectedLength: number): Uint8Array {
  /* 초기 Arduino 펌웨어는 SIG_LEN 없이 서명만 보냈다. 전체 길이가 체인별
   * 고정 길이와 정확히 같을 때만 레거시 응답으로 인정한다. */
  if (raw.length === expectedLength) return raw;
  if (raw.length < 1) throw new WalletError(SW.DEVICE_ERROR, '서명이 비어 있습니다');
  const n = raw[0]!;
  if (n !== expectedLength || raw.length !== 1 + n) {
    throw new WalletError(SW.DEVICE_ERROR,
      `서명 길이가 맞지 않습니다 (SIG_LEN=${n}, 실제 ${raw.length - 1})`);
  }
  return raw.subarray(1, 1 + n);
}

export type VMode = 'legacy' | 'eip155' | 'typed';

/**
 * 기기가 준 r ‖ s ‖ recid 를 Ethereum 서명으로 만든다.
 * v 계산 규칙은 docs/protocol.md §6 참고.
 */
export function makeSignature(raw: Uint8Array, mode: VMode, chainId?: number): Signature {
  /* docs/protocol.md §6: SIG_LEN(1) ‖ SIGNATURE
   *
   * 길이 접두사를 "있으면 쓰고 없으면 만다" 식으로 추측하면 안 된다.
   * r 의 첫 바이트가 우연히 64 인 서명이 256개 중 하나꼴로 나오는데, 그때
   * 한 바이트를 잘라내고 엉뚱한 서명을 만들어 낸다. */
  const body = takeSig(raw, 65);
  if (body.length !== 65) {
    throw new WalletError(SW.DEVICE_ERROR,
      `Ethereum 서명은 65바이트여야 합니다 (받은 길이 ${body.length})`);
  }
  const r = body.subarray(0, 32);
  const s = body.subarray(32, 64);
  const recid = body[64]!;

  let v: number;
  if (mode === 'typed') v = recid;                       // yParity
  else if (mode === 'eip155') {
    if (chainId === undefined) {
      throw new WalletError(SW.BAD_PARAM, 'EIP-155 서명에는 chainId 가 필요합니다');
    }
    v = recid + chainId * 2 + 35;
  } else v = recid + 27;

  return {
    r: hex(r), s: hex(s), recid, v,
    serialized: hex(concat(r, s, new Uint8Array([v & 0xff]))),
  };
}
