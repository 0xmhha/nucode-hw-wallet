#include "framing.h"
#include <string.h>

void nu_reasm_init(nu_reasm *r) {
    r->tag = 0; r->total = 0; r->seq = 0; r->len = 0;
}

static int fail(nu_reasm *r) { nu_reasm_init(r); return -NU_SW_FRAMING_ERROR; }

int nu_reasm_push(nu_reasm *r, const uint8_t *pkt, size_t len,
                  const uint8_t **out_msg, size_t *out_len) {
    if (len < 3) return fail(r);
    const uint8_t tag = pkt[0];
    const uint16_t seq = (uint16_t)((pkt[1] << 8) | pkt[2]);

    if (seq == 0) {
        if (len < 5) return fail(r);
        const uint16_t total = (uint16_t)((pkt[3] << 8) | pkt[4]);
        if (total > NU_MAX_MESSAGE) { nu_reasm_init(r); return -NU_SW_TOO_LARGE; }
        r->tag = tag;
        r->total = total;
        r->seq = 1;
        r->len = 0;
        len -= 5; pkt += 5;
    } else {
        /* seq 가 건너뛰면 조립 버퍼를 버린다. 중간 패킷을 놓친 상태로
         * 이어붙이면 엉뚱한 메시지가 만들어진다. */
        if (r->seq == 0 || seq != r->seq) return fail(r);
        r->seq++;
        len -= 3; pkt += 3;
    }

    if (len > (size_t)(r->total - r->len)) len = (size_t)(r->total - r->len);
    if (len) { memcpy(r->buf + r->len, pkt, len); r->len = (uint16_t)(r->len + len); }

    if (r->len >= r->total) {
        *out_msg = r->buf;
        *out_len = r->total;
        r->seq = 0;              /* 다음 메시지는 seq 0 부터 */
        return 1;
    }
    return 0;
}

void nu_frame(uint8_t tag, const uint8_t *msg, size_t len, size_t mtu,
              nu_frame_out out, void *ctx) {
    uint8_t pkt[247];
    if (mtu > sizeof pkt) mtu = sizeof pkt;
    if (mtu < 6) mtu = 6;

    size_t off = 0;
    uint16_t seq = 0;
    do {
        const size_t head = (seq == 0) ? 5u : 3u;
        size_t room = mtu - head;
        size_t n = (len - off < room) ? (len - off) : room;

        pkt[0] = tag;
        pkt[1] = (uint8_t)(seq >> 8);
        pkt[2] = (uint8_t)seq;
        if (seq == 0) { pkt[3] = (uint8_t)(len >> 8); pkt[4] = (uint8_t)len; }
        if (n) memcpy(pkt + head, msg + off, n);
        out(pkt, head + n, ctx);

        off += n;
        seq++;
    } while (off < len);
}
