/* ── 단위 변환 ────────────────────────────────────────────────────────────
   ethers 를 끌어오지 않는다. 웹이 필요한 건 wei 왕복 하나뿐이고,
   SDK 를 의존성 없이 쓸 수 있다는 걸 보여주는 것도 예제의 목적이다.        */

export function parseEther(v: string): bigint {
  const t = v.trim();
  if (!t) return 0n;
  if (!/^\d*\.?\d*$/.test(t)) throw new Error(`숫자가 아닙니다: ${v}`);
  const [whole = '0', frac = ''] = t.split('.');
  if (frac.length > 18) throw new Error('wei 보다 작은 단위는 없습니다 (소수점 18자리까지)');
  return BigInt(whole || '0') * 10n ** 18n + BigInt((frac + '0'.repeat(18)).slice(0, 18) || '0');
}

export function formatEther(wei: bigint): string {
  const neg = wei < 0n;
  const abs = neg ? -wei : wei;
  const whole = abs / 10n ** 18n;
  const frac = (abs % 10n ** 18n).toString().padStart(18, '0').replace(/0+$/, '');
  return `${neg ? '-' : ''}${whole}${frac ? '.' + frac : ''}`;
}
