/* 프로토콜 적합성 브리지.
 *
 * 지갑 코어를 그대로 컴파일해 놓고, 표준입력으로 받은 요청 바이트를 먹인 뒤
 * 기기가 내보내는 바이트를 표준출력으로 돌려준다. SDK 테스트가 이걸 자식
 * 프로세스로 띄워서 **진짜 펌웨어 코드**를 상대로 인코딩·디코딩을 검증한다.
 *
 * 문서를 눈으로 대조하는 것보다 확실하다. 문서는 틀릴 수 있지만 이건 실제로
 * 보드에 올라갈 코드다.
 *
 * 입력 (한 줄에 하나)
 *   >HEX      요청 메시지 (CMD ‖ LEN ‖ PAYLOAD) 를 코어에 넣는다
 *   a         진행 중인 승인을 통과시킨다
 *               CONFIRM  — LED 표시를 넘기고 켜진 버튼을 누른다
 *               PIN_NEW  — 기본 PIN(031201)을 두 번 누른다
 *   pHEX      PIN 을 누른다. 각 자리가 버튼 번호 (예: p031201)
 *   fMS       공장 초기화 조합(버튼 0+3)을 MS 동안 붙잡았다 떼고 확인까지 누른다
 *   bN        버튼 N 을 한 번 누른다
 *   tMS       시간을 MS 만큼 밀고 틱을 돌린다
 *   s         현재 상태를 주석으로 뱉는다 (디버깅용)
 *   q         종료
 *
 * 출력
 *   <TTHEX    기기가 내보낸 메시지. TT 는 프레이밍 태그 (05 메시지 / 06 이벤트)
 *   #...      주석
 *   !         이번 입력에 대한 출력 끝                                      */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../nuwallet/src/core/wallet.h"
#include "../wallet/src/port/host/hal_host.h"

static nu_host   H;
static nu_hal    HAL;
static nu_wallet W;

static size_t unhex(const char *h, uint8_t *out, size_t max) {
    size_t n = 0;
    while (h[0] && h[1] && n < max) {
        unsigned v;
        if (sscanf(h, "%2x", &v) != 1) break;
        out[n++] = (uint8_t)v;
        h += 2;
    }
    return n;
}

static void flush_out(void) {
    for (int i = 0; i < H.out_n; i++) {
        printf("<%02x", H.out[i].tag);
        for (size_t j = 0; j < H.out[i].len; j++) printf("%02x", H.out[i].data[j]);
        printf("\n");
    }
    nu_host_clear_out(&H);
    printf("!\n");
    fflush(stdout);
}

/* 기본 PIN. 셋업에서 사용자가 정하는 것을 테스트가 대신 정해 준다. */
static const uint8_t DEFAULT_PIN[NU_PIN_LEN] = { 0, 3, 1, 2, 0, 1 };

static void press(const uint8_t *seq, uint8_t n) {
    for (uint8_t i = 0; i < n; i++) nu_wallet_button(&W, seq[i]);
}

/* LED 표시가 끝날 때까지 시간을 민다. 실기기에서는 사람이 기다리는 시간이다. */
static void settle_leds(void) {
    for (int i = 0; i < 400 && W.req.showing; i++) {
        H.now += 50;
        nu_wallet_tick(&W, H.now);
    }
}

/* 진행 중인 승인을 통과시킨다. 종류에 따라 눌러야 하는 것이 다르다. */
static void approve(void) {
    settle_leds();
    if (W.req.kind == NU_APPROVAL_PIN_NEW) {
        press(DEFAULT_PIN, NU_PIN_LEN);        /* 1차 */
        press(DEFAULT_PIN, NU_PIN_LEN);        /* 재입력 */
        return;
    }
    if (W.req.kind == NU_APPROVAL_PIN) {
        press(DEFAULT_PIN, NU_PIN_LEN);
        return;
    }
    /* CONFIRM — 코어가 고른 버튼을 그대로 누른다 (실기기에선 켜진 LED). */
    uint8_t seq[NU_PIN_LEN];
    const uint8_t n = W.req.seq_len;
    memcpy(seq, W.req.seq, n);
    press(seq, n);
}

/* 공장 초기화: 조합을 hold_ms 동안 붙잡았다 떼고 확인 시퀀스를 누른다. */
static void factory(uint32_t hold_ms) {
    H.btn_held = (uint8_t)((1u << NU_FACTORY_BTN_A) | (1u << NU_FACTORY_BTN_B));
    for (uint32_t t = 0; t <= hold_ms; t += 100) {
        nu_wallet_tick(&W, H.now);
        H.now += 100;
    }
    H.btn_held = 0;
    nu_wallet_tick(&W, H.now);
    nu_wallet_button(&W, NU_FACTORY_CONFIRM_A);
    nu_wallet_button(&W, NU_FACTORY_CONFIRM_B);
}

int main(void) {
    memset(&H, 0, sizeof H);
    nu_host_init(&H, &HAL, 0xC0FFEE);
    nu_wallet_init(&W, &HAL, "NuWallet-TEST");

    static char line[16384];
    static uint8_t buf[NU_MAX_MESSAGE];

    while (fgets(line, sizeof line, stdin)) {
        char *p = line;
        while (*p == ' ') p++;
        const char c = *p++;
        char *nl = strpbrk(p, "\r\n");
        if (nl) *nl = 0;

        switch (c) {
        case '>': {
            const size_t n = unhex(p, buf, sizeof buf);
            nu_wallet_handle(&W, buf, n);
            break;
        }
        case 'a':
            approve();
            break;
        case 'p':
            for (const char *d = p; *d; d++) {
                if (*d >= '0' && *d <= '9') nu_wallet_button(&W, (uint8_t)(*d - '0'));
            }
            break;
        case 'b':
            nu_wallet_button(&W, (uint8_t)atoi(p));
            break;
        case 'f':
            factory((uint32_t)atoi(p));
            break;
        case 't':
            H.now += (uint32_t)atoi(p);
            nu_wallet_tick(&W, H.now);
            break;
        case 's':
            printf("# flags=%02x unlocked=%d req=%02x kind=%d leds=%02x fac=%d bonds=%d\n",
                   nu_wallet_flags(&W), W.unlocked, W.req.cmd, W.req.kind,
                   H.leds, W.fac_stage, H.bonds_erased);
            break;
        case 'q':
            return 0;
        default:
            break;
        }
        flush_out();
    }
    return 0;
}
