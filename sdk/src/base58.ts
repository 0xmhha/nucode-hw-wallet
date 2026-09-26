/** Solana 주소는 공개키의 base58 이다. 의존성 없이 짧게 구현한다. */
export function base58(b: Uint8Array): string {
  const A = '123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz';
  const digits: number[] = [0];
  for (const byte of b) {
    let carry = byte;
    for (let i = 0; i < digits.length; i++) {
      carry += digits[i]! << 8;
      digits[i] = carry % 58;
      carry = (carry / 58) | 0;
    }
    while (carry) { digits.push(carry % 58); carry = (carry / 58) | 0; }
  }
  let out = '';
  for (const byte of b) { if (byte === 0) out += A[0]; else break; }
  for (let i = digits.length - 1; i >= 0; i--) out += A[digits[i]!];
  return out;
}
