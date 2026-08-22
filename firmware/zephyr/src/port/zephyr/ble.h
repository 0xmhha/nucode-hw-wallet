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

#endif
