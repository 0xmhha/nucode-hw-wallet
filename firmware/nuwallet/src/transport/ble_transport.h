#line 1 "/Users/0xtopaz/work/github/0xmhha/nucode-hw-wallet/firmware/nuwallet/src/transport/ble_transport.h"
#ifndef NUWALLET_BLE_TRANSPORT_H
#define NUWALLET_BLE_TRANSPORT_H
#include <stdint.h>

/* NuWallet GATT 서비스 + 프레이밍.
 * UUID 와 프레이밍은 docs/protocol.md §1, §2 참고. */

void nuble_begin(const char *device_name);
void nuble_task(void);
int  nuble_connected(void);
int  nuble_paired(void);

/* 프로토콜 계층이 응답/이벤트를 내보낼 때 호출한다. 프레이밍은 여기서 한다. */
void nuble_send(uint8_t tag, const uint8_t *payload, uint16_t len);

#endif
