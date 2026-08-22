#ifndef NUWALLET_FRAMING_H
#define NUWALLET_FRAMING_H
/* docs/protocol.md §2 — BLE 패킷 <-> 메시지.
 *
 * 이 파일은 플랫폼 독립이며 동적 할당을 하지 않는다.                        */
#include <stdint.h>
#include <stddef.h>
#include "protocol.h"
#ifdef __cplusplus
extern "C" {
#endif

/* ── 수신: 패킷을 순서대로 먹여 메시지를 조립한다 ───────────────────────── */
typedef struct {
    uint8_t  tag;
    uint16_t total;
    uint16_t seq;
    uint16_t len;
    uint8_t  buf[NU_MAX_MESSAGE];
} nu_reasm;

void nu_reasm_init(nu_reasm *r);

/* 반환값:
 *    1  메시지 완성. *out_msg / *out_len 이 채워진다 (r->buf 를 가리킨다)
 *    0  아직 더 필요함
 *   <0  프레이밍 오류. 조립 버퍼는 버려진다. 값은 상태 코드의 음수. */
int nu_reasm_push(nu_reasm *r, const uint8_t *pkt, size_t len,
                  const uint8_t **out_msg, size_t *out_len);

/* ── 송신: 메시지를 MTU 크기 패킷으로 쪼갠다 ────────────────────────────── */
typedef void (*nu_frame_out)(const uint8_t *pkt, size_t len, void *ctx);

/* mtu 는 한 패킷에 실을 수 있는 총 바이트(헤더 포함). 20 이면 안전하다. */
void nu_frame(uint8_t tag, const uint8_t *msg, size_t len, size_t mtu,
              nu_frame_out out, void *ctx);

#ifdef __cplusplus
}
#endif
#endif
