#ifndef NUWALLET_ETH_H
#define NUWALLET_ETH_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

#define ETH_ADDR_LEN 20

/* 비압축 공개키(0x04 ‖ X ‖ Y, 65바이트) -> 주소.
 * keccak256(X‖Y) 의 하위 20바이트다. */
void eth_address_from_pubkey(const uint8_t pub65[65], uint8_t addr[ETH_ADDR_LEN]);

/* 트랜잭션 서명 해시.
 * payload 는 호스트가 보낸 서명 전 트랜잭션 바이트열 그대로다.
 *   legacy : RLP([nonce,gasPrice,gas,to,value,data,chainId,0,0])
 *   typed  : 0x02 ‖ RLP([...])   (EIP-1559 등)
 * 어느 쪽이든 keccak256(payload) 가 서명 대상이다.
 *
 * 중요: 호스트가 준 "해시"를 그대로 서명하지 않는다. 그러면 기기가 무엇이든
 * 서명하게 된다. 반드시 원문을 받아 여기서 해시한다. */
void eth_tx_hash(const uint8_t *payload, size_t len, uint8_t out32[32]);

/* personal_sign (EIP-191).
 *   keccak256("\x19Ethereum Signed Message:\n" ‖ dec(len) ‖ message) */
void eth_personal_hash(const uint8_t *msg, size_t len, uint8_t out32[32]);

/* EIP-712.
 *   keccak256("\x19\x01" ‖ domainSeparator ‖ messageHash) */
void eth_typed_hash(const uint8_t domain32[32], const uint8_t message32[32], uint8_t out32[32]);

/* ── 최소 RLP 파서 — 트랜잭션 요약용 ──────────────────────────────────────
 * 기기에 화면이 없어 사용자에게 보여줄 수는 없지만, 정책 판단과 로그에 쓴다.
 *
 * **파싱에 실패하면 서명하지 않는다.** 해시 자체는 원문 전체로 계산하므로
 * 파싱이 서명의 정확성에 필요하지는 않다. 그러나 형식 검증마저 없으면 기기가
 * 아무 바이트뭉치에나 서명하게 된다 — 하드웨어 지갑이 하면 안 되는 일이다.
 * 거르는 쪽은 controller/protocol.cpp 의 accept_sign 이다. SECURITY.md §2. */
typedef struct {
    int      ok;                 /* 파싱 성공 여부 */
    int      typed;              /* 0 = legacy, 그 외 = 타입 바이트 값 */
    uint64_t chain_id;
    uint64_t nonce;
    uint8_t  to[ETH_ADDR_LEN];
    int      has_to;             /* 0 이면 컨트랙트 생성 */
    uint8_t  value[32];          /* big-endian, 좌측 0 패딩 */
    size_t   value_len;
    size_t   data_len;
} eth_tx_summary;

void eth_parse_tx(const uint8_t *payload, size_t len, eth_tx_summary *out);

#ifdef __cplusplus
}
#endif
#endif
