#ifndef NUWALLET_INTERNAL_H
#define NUWALLET_INTERNAL_H
/* 지갑 코어 내부 경계.
 *
 * 코어는 네 조각으로 나뉜다. 각 조각은 하나만 책임진다.
 *
 *   wire.c       호스트로 나가는 바이트 — 응답·이벤트 인코딩, LED 출력
 *   session.c    잠금 해제된 세션 — 시드·주소 파생·플래시 레코드
 *   challenge.c  사람의 승인 — 랜덤 챌린지, PIN 입력, 타임아웃, LED 표시
 *   commands.c   프로토콜 — 요청 파싱과 디스패치
 *   wallet.c     생명주기 — 부팅과 연결 해제
 *
 * 이 헤더는 그 조각들 사이에서만 쓴다. 밖으로는 wallet.h 만 노출한다.  */
#include "wallet.h"

/* ── wire.c ─────────────────────────────────────────────────────────────── */
void nu_be16(uint8_t *p, uint16_t v);
void nu_be32(uint8_t *p, uint32_t v);
uint32_t nu_rd32(const uint8_t *p);

void nu_leds(nu_wallet *w, uint8_t mask);

/** now 가 since 보다 앞선 것처럼 보이면 0 을 돌려준다.
 *
 * 호출자가 캐시해 둔 시각을 넘기면(.ino 의 loop() 가 그렇다) now 가 since 보다
 * **과거**일 수 있다. 그냥 빼면 uint32 가 언더플로해서 49일이 지난 것처럼 보이고,
 * 세션이 즉시 닫힌다. 실기기에서 이것 때문에 잠금 해제 직후 다시 잠겼다.
 * 49일 진짜 랩어라운드도 같은 규칙으로 처리된다. */
uint32_t nu_elapsed(uint32_t now, uint32_t since);

/** 응답 하나를 내보낸다 (STATUS ‖ LEN ‖ PAYLOAD). */
void nu_reply(nu_wallet *w, uint16_t status, const uint8_t *payload, size_t len);
/** COUNT ‖ WORD_IDX* 형태의 응답. */
void nu_reply_words(nu_wallet *w, const uint16_t *words, uint8_t count);
/** 이벤트 하나를 내보낸다 (EVT ‖ LEN ‖ PAYLOAD). */
void nu_event(nu_wallet *w, uint8_t evt, const uint8_t *payload, size_t len);
/** 0xA3 DEVICE_STATE. */
void nu_emit_state(nu_wallet *w);

/* ── session.c ──────────────────────────────────────────────────────────── */
extern const uint32_t NU_DEFAULT_PATH[5];      /* m/44'/60'/0'/0/0 */

/** RAM 의 시드·워드·PIN 을 지운다. 플래시는 건드리지 않는다. */
void nu_session_lock(nu_wallet *w);
/** 워드 + 패스프레이즈 -> 시드. 실패 0. */
int  nu_seed_from_words(nu_wallet *w, const uint16_t *words, uint8_t count,
                        const char *passphrase);
/** 경로 파생. 필요 없는 출력은 NULL 로 둔다. 실패 0. */
int  nu_derive(const nu_wallet *w, const uint32_t *path, uint8_t depth,
               uint8_t addr[20], uint8_t pub65[65], uint8_t chain[32],
               uint8_t priv[32]);
/** SLIP-0010 Ed25519 파생 (Solana). 실패 0. */
int  nu_derive_ed25519(const nu_wallet *w, const uint32_t *path, uint8_t depth,
                       uint8_t key[32]);
/** 워드를 PIN 으로 봉인해 플래시에 쓴다. 실패 0. */
int  nu_persist(nu_wallet *w, const uint16_t *words, uint8_t count,
                const uint8_t *pin, uint8_t pin_len);
/** 남은 시도 횟수만 갱신해 다시 쓴다. */
void nu_persist_tries(nu_wallet *w, uint8_t tries);
/** 플래시 레코드를 지우고 세션도 잠근다. */
void nu_wipe_record(nu_wallet *w);

/* ── challenge.c ────────────────────────────────────────────────────────── */
/** 승인 절차를 시작하고 PENDING 을 응답한다. kind 0 = 랜덤, 1 = PIN 입력. */
void nu_challenge_start(nu_wallet *w, uint8_t cmd, uint8_t kind, uint8_t steps);
/** 진행 중인 요청을 끝낸다. 결과 이벤트까지 여기서 보낸다. */
void nu_challenge_finish(nu_wallet *w, uint16_t status,
                         const uint8_t *sig64, int recid);
/** 진행 중인 요청을 버린다 (이벤트를 보내지 않는다). */
void nu_request_clear(nu_wallet *w);
/** GET_RESULT 용으로 마지막 결과를 기억해 둔다. */
void nu_remember(nu_wallet *w, uint32_t id, uint16_t status,
                 const uint8_t *payload, uint8_t len);

/* ── wallet.c (공장 초기화) ─────────────────────────────────────────────── */
/** 확인 시퀀스를 기다리는 중이면 이 눌림을 소비하고 1 을 반환한다. */
int  nu_factory_button(nu_wallet *w, uint8_t idx);
/** 매 틱마다 버튼 홀드를 본다. 카운트다운 중이면 LED 를 잡고 1 을 반환한다. */
int  nu_factory_tick(nu_wallet *w, uint32_t now_ms);

/* ── commands.c ─────────────────────────────────────────────────────────── */
/* nu_wallet_handle / nu_wallet_framing_error 는 wallet.h 에 있다. */

#endif
