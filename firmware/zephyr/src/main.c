/* NuWallet — nRF52840 하드웨어 지갑 펌웨어 (Zephyr).
 *
 * 하는 일은 셋뿐이다.
 *   1. HAL 과 BLE 를 올린다
 *   2. 버튼 입력과 BLE 요청을 코어에 넘긴다
 *   3. 10ms 마다 코어를 틱 시킨다 (타임아웃 · LED)
 *
 * 코어는 **오직 이 스레드에서만** 실행된다. 재진입을 가정하지 않기도 하고,
 * BIP-32 파생과 PBKDF2 가 2KB 넘는 스택을 쓰기 때문이기도 하다 (아래
 * CONFIG_MAIN_STACK_SIZE). 버튼은 워크큐에서, BLE 요청은 BT RX 스레드에서
 * 오는데, 둘 다 큐를 거쳐 여기로 모인다.
 *
 * 프로토콜 처리는 전부 app/wallet.c 에 있다. 그쪽은 Zephyr 를 모른다.     */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app/wallet.h"
#include "port/zephyr/hal_zephyr.h"
#include "port/zephyr/ble.h"

LOG_MODULE_REGISTER(nuwallet, LOG_LEVEL_INF);

static nu_hal    hal;
static nu_wallet wallet;
static char      name[24];

/* 버튼은 워크큐 컨텍스트에서 온다. 코어는 재진입을 가정하지 않으므로
 * 메시지 큐로 넘겨 메인 루프 한 곳에서만 만진다. */
K_MSGQ_DEFINE(btn_q, sizeof(uint8_t), 8, 1);

static void on_button(uint8_t idx) {
    k_msgq_put(&btn_q, &idx, K_NO_WAIT);
}

int main(void) {
    if (nu_zephyr_hal_init(&hal)) {
        LOG_ERR("HAL 초기화 실패 — 멈춥니다");
        return 0;
    }
    nu_zephyr_set_button_cb(on_button);

    /* BLE 를 먼저 올려야 hal.send 가 채워진다. 이름도 여기서 정해진다. */
    nu_wallet_init(&wallet, &hal, "NuWallet");
    if (nu_ble_start(&wallet, &hal, name, sizeof name)) {
        LOG_ERR("BLE 시작 실패 — 멈춥니다");
        return 0;
    }
    /* 광고 이름이 정해진 뒤 코어를 다시 초기화해 GET_VERSION 이 같은 이름을
     * 돌려주게 한다. 플래시 레코드도 이때 다시 읽힌다. */
    nu_wallet_init(&wallet, &hal, name);

    LOG_INF("NuWallet 준비됨 (%s)", name);

    for (;;) {
        /* 요청이 오면 즉시 깨고, 없으면 10ms 뒤에 돌아온다. */
        nu_ble_rx_poll(10);

        uint8_t idx;
        while (k_msgq_get(&btn_q, &idx, K_NO_WAIT) == 0) {
            nu_wallet_button(&wallet, idx);
        }
        nu_wallet_tick(&wallet, k_uptime_get_32());
    }
    return 0;
}
