/**
 * 프로토콜 모음. 예전에 한 파일이던 것을 책임별로 나눴고, 여기서는 다시
 * 모아 내보내기만 한다 — 기존 import 경로('./protocol.js')를 깨지 않으려고.
 *
 *   constants.ts  명령·상태·이벤트·UUID·플래그 (펌웨어 protocol.h 와 1:1)
 *   framing.ts    BLE 패킷 쪼개기와 재조립
 *   codec.ts      요청·응답·이벤트 메시지
 *   path.ts       BIP-32 경로와 CHAIN_PATH
 *   bytes.ts      hex·concat
 */
export * from './constants.js';
export * from './framing.js';
export * from './codec.js';
export * from './path.js';
export * from './bytes.js';
