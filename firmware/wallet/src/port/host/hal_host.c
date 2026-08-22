#include "hal_host.h"
#include <string.h>

static int h_random(uint8_t *out, size_t n, void *ctx) {
    nu_host *h = (nu_host *)ctx;
    if (h->rng_fail) return 0;
    for (size_t i = 0; i < n; i++) {
        h->rng ^= h->rng << 13; h->rng ^= h->rng >> 17; h->rng ^= h->rng << 5;
        out[i] = (uint8_t)h->rng;
    }
    return 1;
}
static size_t h_store_read(uint8_t *out, size_t max, void *ctx) {
    nu_host *h = (nu_host *)ctx;
    if (!h->store_len) return 0;
    const size_t n = h->store_len < max ? h->store_len : max;
    memcpy(out, h->store, n);
    return n;
}
static int h_store_write(const uint8_t *in, size_t n, void *ctx) {
    nu_host *h = (nu_host *)ctx;
    if (n > sizeof h->store) return 0;
    memcpy(h->store, in, n);
    h->store_len = n;
    return 1;
}
static int h_store_erase(void *ctx) {
    nu_host *h = (nu_host *)ctx;
    memset(h->store, 0, sizeof h->store);
    h->store_len = 0;
    return 1;
}
static void h_leds(uint8_t mask, void *ctx) { ((nu_host *)ctx)->leds = mask; }
static uint32_t h_millis(void *ctx) { return ((nu_host *)ctx)->now; }
static void h_send(uint8_t tag, const uint8_t *msg, size_t len, void *ctx) {
    nu_host *h = (nu_host *)ctx;
    if (h->out_n >= NU_HOST_OUT_MAX || len > NU_HOST_MSG_MAX) return;
    nu_host_msg *m = &h->out[h->out_n++];
    m->tag = tag;
    m->len = len;
    memcpy(m->data, msg, len);
}

void nu_host_init(nu_host *h, nu_hal *hal, uint32_t seed) {
    h->now = 0;
    h->rng = seed ? seed : 1;
    h->leds = 0;
    h->rng_fail = 0;
    h->out_n = 0;
    hal->random = h_random;
    hal->store_read = h_store_read;
    hal->store_write = h_store_write;
    hal->store_erase = h_store_erase;
    hal->leds = h_leds;
    hal->send = h_send;
    hal->millis = h_millis;
    hal->ctx = h;
}

void nu_host_clear_out(nu_host *h) { h->out_n = 0; }
