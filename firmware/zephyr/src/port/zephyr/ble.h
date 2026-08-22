#ifndef NUWALLET_BLE_H
#define NUWALLET_BLE_H
/* NuWallet GATT 서비스 (docs/protocol.md §1).
 *
 * 들어온 패킷은 조립해서 nu_wallet_handle 로 넘기고, 코어가 보내는 메시지는
 * MTU 에 맞게 쪼개 notify 로 내보낸다.                                     */
#include "../../app/wallet.h"

/* 광고를 시작한다. hal->send 를 여기서 채운다. 성공 0. */
int nu_ble_start(nu_wallet *w, nu_hal *hal, char *name_out, size_t name_cap);

/* 연결 여부. */
int nu_ble_connected(void);

/* 조립이 끝난 요청 하나를 코어에 넘긴다. 없으면 최대 timeout_ms 기다린다.
 *
 * **반드시 메인 스레드에서 불러야 한다.** 지갑 코어는 BIP-32 파생과 PBKDF2 로
 * 2KB 넘는 스택을 쓰는데, GATT 콜백이 도는 BT RX 스레드 스택은 그보다 훨씬
 * 작아서 콜백 안에서 코어를 부르면 스택이 넘친다. */
void nu_ble_rx_poll(int timeout_ms);

#endif
