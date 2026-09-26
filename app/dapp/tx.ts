/**
 * 폼 값으로 트랜잭션과 EIP-712 Permit 을 만든다. 화면과 떼어 둔 순수 함수들이다.
 */
import { parseEther } from '../lib/ether';
import type { FeeSuggestion } from './fees';

export type TxForm = { chainId: string; account: string; to: string; value: string; data: string };
export type TxParams = Record<string, string>;

const ADDRESS = /^0x[0-9a-fA-F]{40}$/u;
const SIMPLE_TRANSFER_GAS = '0x5208';       // 21,000

export function validateTxForm(f: TxForm): void {
  if (!Number.isSafeInteger(Number(f.chainId)) || Number(f.chainId) <= 0) {
    throw new Error('chainId는 0보다 큰 정수여야 합니다.');
  }
  if (!ADDRESS.test(f.account)) throw new Error('발신 주소 형식이 올바르지 않습니다.');
  const to = f.to.trim();
  const data = f.data.trim();
  if (to && !ADDRESS.test(to)) {
    throw new Error('받는 주소는 0x로 시작하는 20바이트 Ethereum 주소여야 합니다.');
  }
  if (!to && !data) throw new Error('받는 주소가 없으면 컨트랙트 생성 bytecode가 DATA에 필요합니다.');
  if (data && !/^0x(?:[0-9a-fA-F]{2})*$/u.test(data)) {
    throw new Error('DATA는 0x로 시작하는 짝수 길이의 HEX여야 합니다.');
  }
  if (parseEther(f.value) < 0n) throw new Error('전송액은 0 이상이어야 합니다.');
}

export function buildTxParams(f: TxForm): TxParams {
  validateTxForm(f);
  const t: TxParams = { from: f.account };
  if (f.to.trim()) t.to = f.to.trim();
  if (f.data.trim()) t.data = f.data.trim();
  t.value = '0x' + parseEther(f.value).toString(16);
  // 단순 ETH 전송은 gas를 고정해 잔액이 없는 계정에서도
  // eth_signTransaction 자체를 테스트할 수 있게 한다.
  if (!f.data.trim()) t.gas = SIMPLE_TRANSFER_GAS;
  return t;
}

/** 수수료를 트랜잭션에 박는다. provider 는 호출자가 준 수수료를 그대로 쓰므로,
 *  잔액 확인에 쓴 숫자와 서명되는 숫자가 같아진다. */
export function withFees(tx: TxParams, fees: FeeSuggestion): TxParams {
  const h = (n: bigint) => '0x' + n.toString(16);
  return fees.kind === 'eip1559'
    ? { ...tx, type: '0x2', maxFeePerGas: h(fees.maxFeePerGas), maxPriorityFeePerGas: h(fees.maxPriorityFeePerGas) }
    : { ...tx, type: '0x0', gasPrice: h(fees.gasPrice) };
}

/** ERC-2612 Permit. 폼의 받는 주소를 spender 로, 보낼 양을 value 로 쓴다. */
export function buildPermit(f: { chainId: string; account: string; to: string; value: string }) {
  return {
    types: {
      EIP712Domain: [
        { name: 'name', type: 'string' },
        { name: 'version', type: 'string' },
        { name: 'chainId', type: 'uint256' },
        { name: 'verifyingContract', type: 'address' },
      ],
      Permit: [
        { name: 'owner', type: 'address' },
        { name: 'spender', type: 'address' },
        { name: 'value', type: 'uint256' },
        { name: 'nonce', type: 'uint256' },
        { name: 'deadline', type: 'uint256' },
      ],
    },
    primaryType: 'Permit',
    domain: {
      name: 'NuWallet Demo Token',
      version: '1',
      chainId: Number(f.chainId) || 0,
      verifyingContract: '0x0000000000000000000000000000000000000001',
    },
    message: {
      owner: f.account,
      spender: f.to.trim() || '0x0000000000000000000000000000000000000002',
      value: parseEther(f.value || '0').toString(),
      nonce: '0',
      deadline: String(Math.floor(Date.now() / 1000) + 3600),
    },
  };
}
