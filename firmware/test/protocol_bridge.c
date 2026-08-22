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
 *   a         진행 중인 랜덤 챌린지를 승인한다 (LED 표시를 넘기고 시퀀스대로 누름)
 *   pHEX      PIN 을 누른다. 각 니블이 버튼 번호 (예: p0312)
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
#include "../zephyr/src/app/wallet.h"
#include "../zephyr/src/port/host/hal_host.h"

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

/* LED 표시가 끝날 때까지 시간을 밀고, 코어가 들고 있는 시퀀스대로 눌러 준다.
 * 실기기에서는 사람이 LED 를 보고 하는 일이다. */
static void approve(void) {
    for (int i = 0; i < 400 && W.req.showing; i++) {
        H.now += 50;
        nu_wallet_tick(&W, H.now);
    }
    uint8_t seq[NU_PIN_MAX];
    const uint8_t n = W.req.seq_len;
    memcpy(seq, W.req.seq, n);
    for (uint8_t i = 0; i < n; i++) nu_wallet_button(&W, seq[i]);
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
        case 't':
            H.now += (uint32_t)atoi(p);
            nu_wallet_tick(&W, H.now);
            break;
        case 's':
            printf("# flags=%02x unlocked=%d req=%02x leds=%02x\n",
                   nu_wallet_flags(&W), W.unlocked, W.req.cmd, H.leds);
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
