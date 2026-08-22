export { NuWallet } from './client.js';
export { NuWalletProvider, type ProviderOptions } from './provider.js';
export { BleTransport, type TransportOptions } from './transport.js';
export {
  SERVICE_UUID, RX_UUID, TX_UUID, CMD, EVT, SW, WalletError,
  DEFAULT_PATH, HARDENED, parsePath, encodePath, hex, fromHex, concat,
  frame, Reassembler, encodeRequest, decodeResponse, decodeEvent,
} from './protocol.js';
export { keccak256, toChecksumAddress } from './address.js';
export {
  rlpEncode, toRlpInt, stripZeros,
  encodeLegacyUnsigned, encodeLegacySigned, type LegacyTx,
} from './rlp.js';
export type {
  DeviceInfo, DeviceState, AccountInfo, Signature,
  SignOptions, ChallengeCallbacks, Eip1193Provider,
} from './types.js';
