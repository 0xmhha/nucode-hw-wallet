#ifndef NUWALLET_RLP_H
#define NUWALLET_RLP_H
/* 서명 대상 트랜잭션의 RLP 를 훑는다.
 *
 * 목적은 두 가지다.
 *   1. 형식 검증 — 기기가 아무 바이트 뭉치나 해시해서 서명하면 안 된다.
 *      RLP 로 파싱되지 않거나 항목 수가 안 맞으면 거부한다.
 *   2. 요약 추출 — chainId / to / value / data 길이. 화면이 없어 사용자에게
 *      보여주지는 못하지만 로그와 정책 판단(예: 컨트랙트 생성 차단)에 쓴다.
 *
 * 완전한 RLP 디코더가 아니다. 서명 경로에 필요한 만큼만 한다.              */
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  type;        /* 0 = legacy(EIP-155), 1 = EIP-2930, 2 = EIP-1559 */
    uint64_t chain_id;
    int      has_to;      /* 0 이면 컨트랙트 생성 */
    uint8_t  to[20];
    uint8_t  value[32];   /* big-endian, 앞의 0 은 잘려 있다 */
    uint8_t  value_len;
    uint32_t data_len;
} nu_tx_summary;

/* 성공 1, 형식 오류 0. */
int nu_tx_parse(const uint8_t *buf, size_t len, nu_tx_summary *out);

#ifdef __cplusplus
}
#endif
#endif
