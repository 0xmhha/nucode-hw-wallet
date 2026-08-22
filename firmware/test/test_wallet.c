/* 지갑 코어 통합 테스트 — 보드 없이 프로토콜 전체를 돌린다.
 *
 *   cd firmware/test && make test
 *
 * 호스트 HAL(port/host)을 물려 놓고, 실제 BLE 에 나갈 바이트를 그대로 만들어
 * 넣고 그대로 받아서 확인한다. 버튼은 코어의 챌린지 시퀀스를 들여다보고
 * 눌러 준다 — 실기기에서는 사람이 LED 를 보고 하는 일이다.               */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../zephyr/src/app/wallet.h"
#include "../zephyr/src/app/framing.h"
#include "../zephyr/src/app/rlp.h"
#include "../zephyr/src/app/store.h"
#include "../zephyr/src/port/host/hal_host.h"
#include "../nuwallet/src/crypto/keccak.h"
#include "../nuwallet/src/crypto/bip32.h"
#include "../nuwallet/src/crypto/ecdsa.h"

static int fails = 0, total = 0;

static void ok(const char *name, int cond) {
    total++;
    if (cond) printf("  ok   %s\n", name);
    else { printf("  FAIL %s\n", name); fails++; }
}
static void hexs(const uint8_t *b, size_t n, char *out) {
    for (size_t i = 0; i < n; i++) sprintf(out + i * 2, "%02x", b[i]);
    out[n * 2] = 0;
}
static void check_hex(const char *name, const uint8_t *got, size_t n, const char *want) {
    char g[600]; hexs(got, n, g);
    total++;
    if (strcmp(g, want) == 0) printf("  ok   %s\n", name);
    else { printf("  FAIL %s\n       got  %s\n       want %s\n", name, g, want); fails++; }
}
static size_t unhex(const char *h, uint8_t *out) {
    size_t n = strlen(h) / 2;
    for (size_t i = 0; i < n; i++) { unsigned v; sscanf(h + i * 2, "%2x", &v); out[i] = (uint8_t)v; }
    return n;
}

/* ── 테스트용 호스트 측 헬퍼 ────────────────────────────────────────────── */

static nu_host  H;
static nu_hal   HAL;
static nu_wallet W;

static void boot(uint32_t seed, int keep_flash) {
    uint8_t saved[NU_STORE_MAX];
    size_t saved_len = 0;
    if (keep_flash) { memcpy(saved, H.store, H.store_len); saved_len = H.store_len; }
    memset(&H, 0, sizeof H);
    nu_host_init(&H, &HAL, seed);
    if (keep_flash) { memcpy(H.store, saved, saved_len); H.store_len = saved_len; }
    nu_wallet_init(&W, &HAL, "NuWallet-TEST");
}

/* 요청을 만들어 코어에 밀어넣는다. */
static void request(uint8_t cmd, const uint8_t *payload, size_t len) {
    uint8_t msg[NU_MAX_MESSAGE];
    msg[0] = cmd;
    msg[1] = (uint8_t)(len >> 8);
    msg[2] = (uint8_t)len;
    if (len) memcpy(msg + 3, payload, len);
    nu_host_clear_out(&H);
    nu_wallet_handle(&W, msg, 3 + len);
}

/* 마지막 응답(TAG_MESSAGE)을 꺼낸다. 없으면 0. */
static int last_response(uint16_t *status, const uint8_t **payload, size_t *len) {
    for (int i = H.out_n - 1; i >= 0; i--) {
        if (H.out[i].tag != NU_TAG_MESSAGE) continue;
        const uint8_t *d = H.out[i].data;
        *status = (uint16_t)((d[0] << 8) | d[1]);
        *len = (size_t)((d[2] << 8) | d[3]);
        *payload = d + 4;
        return 1;
    }
    return 0;
}

/* 특정 이벤트를 찾는다. */
static const nu_host_msg *find_event(uint8_t evt) {
    for (int i = 0; i < H.out_n; i++) {
        if (H.out[i].tag == NU_TAG_EVENT && H.out[i].data[0] == evt) return &H.out[i];
    }
    return NULL;
}

/* LED 표시가 끝날 때까지 시간을 밀고, 코어가 들고 있는 시퀀스대로 눌러 준다. */
static void approve_challenge(void) {
    for (int i = 0; i < 400 && W.req.showing; i++) {
        H.now += 50;
        nu_wallet_tick(&W, H.now);
    }
    uint8_t seq[NU_PIN_MAX];
    const uint8_t n = W.req.seq_len;
    memcpy(seq, W.req.seq, n);
    for (uint8_t i = 0; i < n; i++) nu_wallet_button(&W, seq[i]);
}

static void press_pin(const uint8_t *pin, uint8_t n) {
    for (int i = 0; i < 400 && W.req.showing; i++) { H.now += 50; nu_wallet_tick(&W, H.now); }
    for (uint8_t i = 0; i < n; i++) nu_wallet_button(&W, pin[i]);
}

/* 12단어 "abandon ... about" 의 워드 인덱스 (abandon=0, about=3). */
static void abandon_words(uint8_t *out, uint8_t *count) {
    out[0] = 12;
    for (int i = 0; i < 11; i++) { out[1 + i * 2] = 0; out[2 + i * 2] = 0; }
    out[1 + 11 * 2] = 0; out[2 + 11 * 2] = 3;
    *count = 12;
}

/* ── 테스트 ─────────────────────────────────────────────────────────────── */

static uint8_t pkts[64][247];
static size_t  plen[64];
static int     n_pkt;

static void collect(const uint8_t *p, size_t l, void *ctx) {
    (void)ctx;
    if (n_pkt >= 64) return;
    memcpy(pkts[n_pkt], p, l); plen[n_pkt] = l; n_pkt++;
}

static void test_framing(void) {
    puts("프레이밍  왕복");
    uint8_t msg[600];
    for (size_t i = 0; i < sizeof msg; i++) msg[i] = (uint8_t)(i * 7 + 3);

    n_pkt = 0;
    nu_frame(NU_TAG_MESSAGE, msg, sizeof msg, 20, collect, NULL);
    ok("600바이트가 여러 패킷으로 쪼개진다", n_pkt > 1);

    nu_reasm r;
    nu_reasm_init(&r);
    const uint8_t *out = NULL; size_t out_len = 0;
    int done = 0;
    for (int i = 0; i < n_pkt; i++) done = nu_reasm_push(&r, pkts[i], plen[i], &out, &out_len);
    ok("마지막 패킷에서 조립 완료", done == 1);
    ok("길이 보존", out_len == sizeof msg);
    ok("내용 보존", out && memcmp(out, msg, sizeof msg) == 0);

    /* 시퀀스를 건너뛰면 오류 */
    nu_reasm_init(&r);
    nu_reasm_push(&r, pkts[0], plen[0], &out, &out_len);
    const int bad = nu_reasm_push(&r, pkts[2], plen[2], &out, &out_len);
    ok("SEQ 건너뛰면 FRAMING_ERROR", bad == -NU_SW_FRAMING_ERROR);

    /* 한 패킷에 들어가는 짧은 메시지 */
    nu_reasm_init(&r);
    n_pkt = 0;
    uint8_t tiny[3] = { 0x01, 0x00, 0x00 };
    nu_frame(NU_TAG_MESSAGE, tiny, 3, 20, collect, NULL);
    ok("짧은 메시지는 패킷 1개", n_pkt == 1);
    done = nu_reasm_push(&r, pkts[0], plen[0], &out, &out_len);
    ok("짧은 메시지 조립", done == 1 && out_len == 3 && memcmp(out, tiny, 3) == 0);
}

static void test_rlp(void) {
    puts("RLP  트랜잭션 검증");
    nu_tx_summary tx;
    uint8_t buf[256];
    size_t n;

    /* EIP-155 예제: nonce 9, gasPrice 20gwei, gas 21000, to 0x3535..., 1 ETH, chainId 1 */
    n = unhex("ec098504a817c800825208943535353535353535353535353535353535353535880de0b6b3a764000080018080", buf);
    ok("legacy 파싱", nu_tx_parse(buf, n, &tx));
    ok("legacy chainId=1", tx.chain_id == 1);
    ok("legacy type=0", tx.type == 0);
    ok("legacy to 있음", tx.has_to);
    check_hex("legacy to", tx.to, 20, "3535353535353535353535353535353535353535");
    ok("legacy value=1e18", tx.value_len == 8 && tx.value[0] == 0x0d);

    /* EIP-1559 */
    n = unhex("02ef0180843b9aca008509502f9000825208943535353535353535353535353535353535353535872386f26fc1000080c0", buf);
    ok("1559 파싱", nu_tx_parse(buf, n, &tx));
    ok("1559 type=2", tx.type == 2);
    ok("1559 chainId=1", tx.chain_id == 1);

    /* 컨트랙트 생성 (to 가 빈 문자열) */
    n = unhex("d3098504a817c800825208808083606060018080", buf);
    ok("컨트랙트 생성 파싱", nu_tx_parse(buf, n, &tx) && tx.has_to == 0);

    /* 거부해야 하는 것들 */
    n = unhex("deadbeef", buf);
    ok("RLP 아닌 바이트 거부", !nu_tx_parse(buf, n, &tx));
    n = unhex("c3010203", buf);
    ok("항목 수 부족 거부", !nu_tx_parse(buf, n, &tx));
    n = unhex("ec098504a817c800825208943535353535353535353535353535353535353535880de0b6b3a76400008001808000", buf);
    ok("뒤에 남는 바이트 거부", !nu_tx_parse(buf, n, &tx));
}

static void test_store(void) {
    puts("저장  PIN 봉인");
    boot(0xC0FFEE, 0);
    uint16_t words[24];
    for (int i = 0; i < 12; i++) words[i] = (uint16_t)(i * 37);
    const uint8_t pin[4] = { 1, 2, 3, 0 };

    uint8_t raw[NU_STORE_MAX]; size_t raw_len = 0;
    ok("봉인", nu_store_seal(&HAL, words, 12, pin, 4, raw, &raw_len));
    ok("레코드 유효", nu_store_valid(raw, raw_len));
    ok("PIN 길이 기록", nu_store_pin_len(raw) == 4);
    ok("워드가 평문으로 남지 않는다",
       memcmp(raw + NU_STORE_OFF_CT, "\x00\x00\x00\x25", 4) != 0);

    uint16_t got[24]; uint8_t count = 0;
    ok("맞는 PIN 으로 열림", nu_store_open(raw, raw_len, pin, 4, got, &count));
    ok("워드 복원", count == 12 && memcmp(got, words, 24) == 0);

    const uint8_t bad[4] = { 1, 2, 3, 1 };
    ok("틀린 PIN 거부", !nu_store_open(raw, raw_len, bad, 4, got, &count));

    raw[NU_STORE_OFF_CT] ^= 0xff;
    ok("암호문 변조 감지", !nu_store_open(raw, raw_len, pin, 4, got, &count));
}

static void test_lifecycle(void) {
    puts("지갑  생성 → 주소 → 서명");
    boot(0x12345, 0);

    uint16_t status; const uint8_t *p; size_t len;

    request(NU_CMD_GET_VERSION, NULL, 0);
    ok("GET_VERSION OK", last_response(&status, &p, &len) && status == NU_SW_OK);
    ok("프로토콜 버전 1", p[0] == NU_PROTOCOL_VERSION);
    ok("초기화 안 됨", (p[3] & NU_FLAG_INITIALIZED) == 0);

    /* 지갑이 없으면 서명도 주소도 안 된다 */
    request(NU_CMD_GET_ADDRESS, (const uint8_t *)"\x01\x01\x00\x00\x00\x00", 6);
    ok("지갑 없으면 NOT_INITIALIZED",
       last_response(&status, &p, &len) && status == NU_SW_NOT_INITIALIZED);

    /* 니모닉 생성 */
    const uint8_t strength[1] = { 12 };   /* 단어 수 */
    request(NU_CMD_SETUP_GENERATE, strength, 1);
    ok("SETUP_GENERATE OK", last_response(&status, &p, &len) && status == NU_SW_OK);
    ok("12단어", p[0] == 12 && len == 25);
    uint8_t gen[25];
    memcpy(gen, p, len);

    /* 다른 단어로 확정하면 거부 */
    uint8_t wrong[25];
    memcpy(wrong, gen, 25);
    wrong[1] ^= 0x01;
    request(NU_CMD_SETUP_CONFIRM, wrong, 25);
    ok("틀린 백업 거부", last_response(&status, &p, &len) && status == NU_SW_BAD_PARAM);

    /* 제대로 확정 */
    request(NU_CMD_SETUP_CONFIRM, gen, 25);
    ok("SETUP_CONFIRM OK", last_response(&status, &p, &len) && status == NU_SW_OK && len == 20);

    /* 이미 있으면 다시 못 만든다 */
    request(NU_CMD_SETUP_GENERATE, strength, 1);
    ok("중복 생성 거부", last_response(&status, &p, &len) && status == NU_SW_ALREADY_INITIALIZED);

    puts("지갑  알려진 니모닉으로 복구");
    boot(0x999, 0);
    uint8_t restore[64]; uint8_t count;
    abandon_words(restore, &count);
    request(NU_CMD_SETUP_RESTORE, restore, 25);
    ok("SETUP_RESTORE OK", last_response(&status, &p, &len) && status == NU_SW_OK);
    /* 널리 알려진 값: abandon x11 about, 패스프레이즈 없음, m/44'/60'/0'/0/0 */
    check_hex("기본 경로 주소", p, 20, "9858effd232b4033e47d90003d41ec34ecaeda94");

    /* 체크섬이 틀린 니모닉은 거부 */
    boot(0x998, 0);
    uint8_t badsum[25];
    abandon_words(badsum, &count);
    badsum[24] = 4;                       /* about(3) -> above(4) */
    request(NU_CMD_SETUP_RESTORE, badsum, 25);
    ok("BIP-39 체크섬 위반 거부",
       last_response(&status, &p, &len) && status == NU_SW_BAD_PARAM);

    /* 다시 복구해 두고 주소 조회 */
    boot(0x997, 0);
    abandon_words(restore, &count);
    request(NU_CMD_SETUP_RESTORE, restore, 25);
    last_response(&status, &p, &len);

    /* CHAIN_PATH = [CHAIN][DEPTH][PATH...]  — docs/protocol.md §3.5 */
    uint8_t path_req[22] = { NU_CHAIN_ETHEREUM, 5,
        0x80,0,0,44, 0x80,0,0,60, 0x80,0,0,0, 0,0,0,0, 0,0,0,0 };
    request(NU_CMD_GET_ADDRESS, path_req, 22);
    ok("GET_ADDRESS OK", last_response(&status, &p, &len) && status == NU_SW_OK
       && len == 1 + 20 + 1 + 65);
    ok("ADDR_LEN = 20", p[0] == 20);
    check_hex("같은 주소", p + 1, 20, "9858effd232b4033e47d90003d41ec34ecaeda94");
    ok("PUBKEY_LEN = 65", p[21] == 65);
    ok("비압축 공개키 접두 0x04", p[22] == 0x04);

    uint8_t addr[20], pub[65];
    memcpy(addr, p + 1, 20);
    memcpy(pub, p + 22, 65);

    /* 서명 */
    puts("지갑  트랜잭션 서명");
    uint8_t rlp[128];
    const size_t rlp_len = unhex(
        "ec098504a817c800825208943535353535353535353535353535353535353535880de0b6b3a764000080018080", rlp);
    uint8_t sign_req[160];
    memcpy(sign_req, path_req, 22);
    memcpy(sign_req + 22, rlp, rlp_len);
    request(NU_CMD_SIGN_TX, sign_req, 22 + rlp_len);
    ok("SIGN_TX PENDING", last_response(&status, &p, &len) && status == NU_SW_PENDING && len == 4);
    const uint32_t req_id = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                            ((uint32_t)p[2] << 8) | p[3];
    const nu_host_msg *ev = find_event(NU_EVT_CHALLENGE_STARTED);
    ok("CHALLENGE_STARTED 이벤트", ev != NULL);
    ok("STEPS = 4", ev && ev->data[7] == NU_CHALLENGE_STEPS);
    ok("승인 대상 CMD 전달", ev && ev->data[8] == NU_CMD_SIGN_TX);
    ok("시퀀스는 BLE 로 나가지 않는다", ev && ev->len == 3 + 6);

    /* 승인 중에는 새 요청을 안 받는다 */
    nu_host_clear_out(&H);
    request(NU_CMD_GET_ADDRESS, path_req, 22);
    ok("진행 중 새 요청 거부",
       last_response(&status, &p, &len) && status == NU_SW_USER_REJECTED);

    nu_host_clear_out(&H);
    approve_challenge();
    ev = find_event(NU_EVT_SIGN_RESULT);
    ok("SIGN_RESULT 이벤트", ev != NULL);
    ok("서명 성공", ev && ev->data[7] == 0x90 && ev->data[8] == 0x00);
    ok("SIG_LEN ‖ r‖s‖recid", ev && ev->len == 3 + 72 && ev->data[9] == 65);

    uint8_t sig[64];
    int recid = 0;
    if (ev) { memcpy(sig, ev->data + 10, 64); recid = ev->data[74]; }
    uint8_t hash[32];
    keccak256(rlp, rlp_len, hash);
    ok("서명이 주소의 공개키로 검증된다",
       ecdsa_verify_secp256k1(pub + 1, hash, sig));
    ok("recid 는 0 또는 1", recid == 0 || recid == 1);

    /* 결과 재조회 */
    uint8_t idbuf[4] = { (uint8_t)(req_id >> 24), (uint8_t)(req_id >> 16),
                         (uint8_t)(req_id >> 8), (uint8_t)req_id };
    request(NU_CMD_GET_RESULT, idbuf, 4);
    ok("GET_RESULT 로 같은 서명 재조회",
       last_response(&status, &p, &len) && status == NU_SW_OK &&
       len == 2 + 66 && p[0] == 0x90 && p[2] == 65 && memcmp(p + 3, sig, 64) == 0);

    /* personal_sign */
    puts("지갑  personal_sign / EIP-712");
    uint8_t ps[64];
    memcpy(ps, path_req, 22);
    memcpy(ps + 22, "hello", 5);
    request(NU_CMD_SIGN_PERSONAL, ps, 27);
    ok("SIGN_PERSONAL PENDING", last_response(&status, &p, &len) && status == NU_SW_PENDING);
    nu_host_clear_out(&H);
    approve_challenge();
    ev = find_event(NU_EVT_SIGN_RESULT);
    ok("personal_sign 결과", ev && ev->data[7] == 0x90);
    if (ev) {
        /* 기기가 만든 해시와 같은 것을 호스트에서 만들어 검증 */
        keccak_ctx c; keccak256_init(&c);
        keccak256_update(&c, (const uint8_t *)"\x19" "Ethereum Signed Message:\n5", 27);
        keccak256_update(&c, (const uint8_t *)"hello", 5);
        uint8_t h2[32]; keccak256_final(&c, h2);
        ok("EIP-191 해시로 검증", ecdsa_verify_secp256k1(pub + 1, h2, ev->data + 10));
    }

    /* EIP-712: 32+32 바이트가 아니면 거부 */
    uint8_t typed[22 + 63];
    memcpy(typed, path_req, 22);
    memset(typed + 22, 0xab, 63);
    request(NU_CMD_SIGN_TYPED, typed, 22 + 63);
    ok("EIP-712 길이 오류 거부", last_response(&status, &p, &len) && status == NU_SW_BAD_PARAM);

    /* 취소 */
    puts("지갑  취소 · 타임아웃");
    memcpy(sign_req, path_req, 22);
    memcpy(sign_req + 22, rlp, rlp_len);
    request(NU_CMD_SIGN_TX, sign_req, 22 + rlp_len);
    last_response(&status, &p, &len);
    uint8_t cancel_id[4];
    memcpy(cancel_id, p, 4);
    nu_host_clear_out(&H);
    request(NU_CMD_CANCEL, cancel_id, 4);
    ok("CANCEL OK", last_response(&status, &p, &len) && status == NU_SW_OK);
    ev = find_event(NU_EVT_SIGN_RESULT);
    ok("취소 시 USER_REJECTED", ev && ev->data[7] == 0x69 && ev->data[8] == 0x85);

    /* 타임아웃 */
    request(NU_CMD_SIGN_TX, sign_req, 22 + rlp_len);
    nu_host_clear_out(&H);
    H.now += NU_CHALLENGE_TIMEOUT_MS + 100;
    nu_wallet_tick(&W, H.now);
    ev = find_event(NU_EVT_SIGN_RESULT);
    ok("60초 초과 시 CHALLENGE_TIMEOUT", ev && ev->data[7] == 0x65 && ev->data[8] == 0x01);

    /* 3회 틀리면 폐기 */
    request(NU_CMD_SIGN_TX, sign_req, 22 + rlp_len);
    nu_host_clear_out(&H);
    for (int attempt = 0; attempt < 3; attempt++) {
        for (int i = 0; i < 400 && W.req.showing; i++) { H.now += 50; nu_wallet_tick(&W, H.now); }
        const uint8_t want = W.req.seq[0];
        nu_wallet_button(&W, (uint8_t)((want + 1) % NU_BUTTON_COUNT));
    }
    ev = find_event(NU_EVT_SIGN_RESULT);
    ok("3회 실패 시 CHALLENGE_FAILED", ev && ev->data[7] == 0x65 && ev->data[8] == 0x02);
    ok("실패 후 유휴 상태", W.req.cmd == 0);
}

static void test_pin(void) {
    puts("지갑  PIN 설정 · 잠금 · 해제");
    boot(0x4242, 0);

    uint16_t status; const uint8_t *p; size_t len;
    uint8_t restore[64]; uint8_t count;
    abandon_words(restore, &count);
    request(NU_CMD_SETUP_RESTORE, restore, 25);
    last_response(&status, &p, &len);

    const uint8_t pin[4] = { 0, 3, 1, 2 };
    uint8_t setpin[5] = { 4, 0, 3, 1, 2 };
    request(NU_CMD_SET_PIN, setpin, 5);
    ok("SET_PIN PENDING", last_response(&status, &p, &len) && status == NU_SW_PENDING);
    nu_host_clear_out(&H);
    approve_challenge();
    const nu_host_msg *ev = find_event(NU_EVT_REQUEST_RESULT);
    ok("REQUEST_RESULT 이벤트", ev != NULL);
    ok("SET_PIN 성공", ev && ev->data[7] == NU_CMD_SET_PIN &&
                       ev->data[8] == 0x90 && ev->data[9] == 0x00);

    request(NU_CMD_GET_STATE, NULL, 0);
    ok("has_pin 플래그", last_response(&status, &p, &len) && (p[0] & NU_FLAG_HAS_PIN));
    ok("PIN 길이 노출", p[6] == 4);

    /* 잠갔다가 잘못된 PIN */
    request(NU_CMD_LOCK, NULL, 0);
    ok("LOCK OK", last_response(&status, &p, &len) && status == NU_SW_OK);
    request(NU_CMD_GET_ADDRESS, (const uint8_t *)"\x01\x05\x80\x00\x00\x2c\x80\x00\x00\x3c\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 22);
    ok("잠긴 상태에서 주소 거부", last_response(&status, &p, &len) && status == NU_SW_LOCKED);

    request(NU_CMD_UNLOCK, NULL, 0);
    ok("UNLOCK PENDING", last_response(&status, &p, &len) && status == NU_SW_PENDING);
    ok("PIN 입력은 LED 로 시퀀스를 보여주지 않는다", W.req.showing == 0);
    const uint8_t wrongpin[4] = { 1, 1, 1, 1 };
    nu_host_clear_out(&H);
    press_pin(wrongpin, 4);
    ok("틀린 PIN 은 잠금을 풀지 못한다", W.unlocked == 0);
    ok("요청은 살아 있다 (재시도 가능)", W.req.cmd == NU_CMD_UNLOCK);
    ok("시도 횟수 감소", W.pin_attempts == NU_PIN_ATTEMPTS - 1);

    nu_host_clear_out(&H);
    press_pin(pin, 4);
    ok("맞는 PIN 으로 해제", W.unlocked == 1);
    ev = find_event(NU_EVT_REQUEST_RESULT);
    ok("UNLOCK 결과 이벤트", ev && ev->data[7] == NU_CMD_UNLOCK && ev->data[8] == 0x90);
    ok("시도 횟수 복원", W.pin_attempts == NU_PIN_ATTEMPTS);

    request(NU_CMD_GET_ADDRESS, (const uint8_t *)"\x01\x05\x80\x00\x00\x2c\x80\x00\x00\x3c\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 22);
    ok("해제 후 주소 조회", last_response(&status, &p, &len) && status == NU_SW_OK);
    check_hex("PIN 을 걸어도 같은 주소", p + 1, 20, "9858effd232b4033e47d90003d41ec34ecaeda94");

    /* 재부팅해도 유지되고, 부팅 직후에는 잠겨 있다 */
    boot(0x4243, 1);
    request(NU_CMD_GET_STATE, NULL, 0);
    ok("재부팅 후에도 지갑 유지",
       last_response(&status, &p, &len) && (p[0] & NU_FLAG_INITIALIZED));
    ok("재부팅 직후 잠김", p[0] & NU_FLAG_LOCKED);
    ok("PIN 길이 유지", p[6] == 4);

    request(NU_CMD_UNLOCK, NULL, 0);
    last_response(&status, &p, &len);
    press_pin(pin, 4);
    ok("재부팅 후 PIN 으로 해제", W.unlocked == 1);

    /* 연결이 끊기면 다시 잠긴다 */
    nu_wallet_disconnected(&W);
    ok("연결 해제 시 자동 잠금", W.unlocked == 0);
}

/* 시도 횟수가 RAM 에만 있으면 전원을 껐다 켜는 것만으로 제한이 사라진다.
 * 카운터는 플래시에 있어야 하고, 바닥나면 시드를 지워야 한다. */
static void test_pin_attempts_survive_reboot(void) {
    puts("지갑  PIN 시도 제한");
    boot(0x5150, 0);

    uint16_t status; const uint8_t *p; size_t len;
    uint8_t restore[64]; uint8_t count;
    abandon_words(restore, &count);
    request(NU_CMD_SETUP_RESTORE, restore, 25);
    last_response(&status, &p, &len);

    const uint8_t setpin[5] = { 4, 0, 3, 1, 2 };
    const uint8_t wrong[4] = { 1, 1, 1, 1 };
    request(NU_CMD_SET_PIN, setpin, 5);
    approve_challenge();
    ok("PIN 설정", nu_store_has_pin(W.rec));
    ok("새 레코드는 시도 횟수가 만수",
       nu_store_tries(W.rec, W.rec_len) == NU_PIN_ATTEMPTS);

    /* 한 번 틀린다 */
    request(NU_CMD_LOCK, NULL, 0);
    request(NU_CMD_UNLOCK, NULL, 0);
    last_response(&status, &p, &len);
    press_pin(wrong, 4);
    ok("틀리면 카운터가 준다", W.pin_attempts == NU_PIN_ATTEMPTS - 1);
    ok("줄어든 값이 플래시에 남는다",
       nu_store_tries(H.store, H.store_len) == NU_PIN_ATTEMPTS - 1);

    /* 전원을 껐다 켠다 — 예전에는 여기서 카운터가 되살아났다 */
    boot(0x5151, 1);
    ok("재부팅해도 카운터가 살아 있다", W.pin_attempts == NU_PIN_ATTEMPTS - 1);

    /* 남은 횟수를 전부 소진시킨다 */
    for (int i = NU_PIN_ATTEMPTS - 1; i > 0; i--) {
        request(NU_CMD_UNLOCK, NULL, 0);
        last_response(&status, &p, &len);
        press_pin(wrong, 4);
    }
    ok("소진되면 카운터가 0", W.pin_attempts == 0);
    ok("소진되면 지갑을 지운다", W.rec_len == 0 && H.store_len == 0);

    request(NU_CMD_GET_STATE, NULL, 0);
    ok("지워진 뒤에는 미초기화 상태",
       last_response(&status, &p, &len) && !(p[0] & NU_FLAG_INITIALIZED));

    /* 맞는 PIN 을 알아도 이제 열 것이 없다 */
    request(NU_CMD_UNLOCK, NULL, 0);
    ok("지워진 뒤 UNLOCK 은 NOT_INITIALIZED",
       last_response(&status, &p, &len) && status == NU_SW_NOT_INITIALIZED);
}

static void test_passphrase_and_wipe(void) {
    puts("지갑  패스프레이즈 · WIPE");
    boot(0x777, 0);

    uint16_t status; const uint8_t *p; size_t len;
    uint8_t req[128]; uint8_t count;
    abandon_words(req, &count);
    memcpy(req + 25, "TREZOR", 6);
    request(NU_CMD_SETUP_RESTORE, req, 31);
    ok("패스프레이즈로 복구", last_response(&status, &p, &len) && status == NU_SW_OK);
    uint8_t addr_pass[20];
    memcpy(addr_pass, p, 20);
    ok("패스프레이즈가 있으면 다른 지갑",
       memcmp(addr_pass, "\x98\x58\xef\xfd", 4) != 0);

    /* 잠갔다 패스프레이즈 없이 열면 다른 주소가 나온다 — 기기는 구분하지 못한다 */
    request(NU_CMD_LOCK, NULL, 0);
    request(NU_CMD_UNLOCK, NULL, 0);
    ok("PIN 없으면 UNLOCK 즉시 성공",
       last_response(&status, &p, &len) && status == NU_SW_OK && len == 20);
    check_hex("패스프레이즈를 빼면 원래 지갑", p, 20, "9858effd232b4033e47d90003d41ec34ecaeda94");

    /* WIPE */
    request(NU_CMD_WIPE, NULL, 0);
    ok("WIPE PENDING", last_response(&status, &p, &len) && status == NU_SW_PENDING);
    nu_host_clear_out(&H);
    approve_challenge();
    const nu_host_msg *ev = find_event(NU_EVT_REQUEST_RESULT);
    ok("WIPE 결과 이벤트", ev && ev->data[7] == NU_CMD_WIPE && ev->data[8] == 0x90);
    ok("플래시 비워짐", H.store_len == 0);

    request(NU_CMD_GET_STATE, NULL, 0);
    ok("초기화 안 된 상태로 복귀",
       last_response(&status, &p, &len) && (p[0] & NU_FLAG_INITIALIZED) == 0);

    /* 이제 새로 만들 수 있다 */
    const uint8_t strength[1] = { 24 };
    request(NU_CMD_SETUP_GENERATE, strength, 1);
    ok("WIPE 후 재생성 가능", last_response(&status, &p, &len) && status == NU_SW_OK && p[0] == 24);
}

static void test_bad_input(void) {
    puts("지갑  잘못된 입력");
    boot(0x555, 0);
    uint16_t status; const uint8_t *p; size_t len;

    request(0x7f, NULL, 0);
    ok("모르는 명령", last_response(&status, &p, &len) && status == NU_SW_UNKNOWN_CMD);

    request(NU_CMD_SETUP_GENERATE, (const uint8_t *)"\x0d", 1);
    ok("잘못된 단어 수", last_response(&status, &p, &len) && status == NU_SW_BAD_PARAM);

    uint8_t restore[64]; uint8_t count;
    abandon_words(restore, &count);
    request(NU_CMD_SETUP_RESTORE, restore, 25);
    last_response(&status, &p, &len);

    request(NU_CMD_GET_ADDRESS, (const uint8_t *)"\x01\x09\x00", 3);
    ok("경로 depth 초과", last_response(&status, &p, &len) && status == NU_SW_BAD_PARAM);

    request(NU_CMD_GET_ADDRESS, (const uint8_t *)"\x01\x05\x00", 3);
    ok("경로 길이 부족", last_response(&status, &p, &len) && status == NU_SW_BAD_PARAM);

    uint8_t sign_req[64];
    const uint8_t path_req[22] = { NU_CHAIN_ETHEREUM, 5,
        0x80,0,0,44, 0x80,0,0,60, 0x80,0,0,0, 0,0,0,0, 0,0,0,0 };
    memcpy(sign_req, path_req, 22);
    memcpy(sign_req + 22, "\xde\xad\xbe\xef", 4);
    request(NU_CMD_SIGN_TX, sign_req, 25);
    ok("RLP 아닌 서명 요청 거부",
       last_response(&status, &p, &len) && status == NU_SW_BAD_PARAM);
    ok("거부된 요청은 챌린지를 걸지 않는다", W.req.cmd == 0);

    uint8_t setpin[4] = { 2, 0, 1, 0 };
    request(NU_CMD_SET_PIN, setpin, 3);
    ok("PIN 이 너무 짧으면 거부",
       last_response(&status, &p, &len) && status == NU_SW_BAD_PARAM);

    uint8_t badbtn[5] = { 4, 0, 1, 2, 9 };
    request(NU_CMD_SET_PIN, badbtn, 5);
    ok("버튼 번호 범위 밖 거부",
       last_response(&status, &p, &len) && status == NU_SW_BAD_PARAM);
}

int main(void) {
    puts("");
    test_framing();
    test_rlp();
    test_store();
    test_lifecycle();
    test_pin();
    test_pin_attempts_survive_reboot();
    test_passphrase_and_wipe();
    test_bad_input();
    printf("\n%d개 중 %d개 실패\n", total, fails);
    return fails ? 1 : 0;
}
