/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_frame.c — the Tri-Isolate wire format: every message round-trips,
 * every truncation and every broken field is refused. */
#include <stdlib.h>
#include <string.h>

#include "ti_frame.h"
#include "ti_test.h"
#include "zwasm.h"

/* The body of the only frame in `b` (after the 5-byte header). */
static const uint8_t *body_of(const ti_buf *b, size_t *len, uint32_t *type) {
    uint32_t n = 0;
    if (b->n < 5 || ti_frame_header(b->b, TI_REQ_MAX, &n) != ZXV_OK || n + 4u != b->n) return NULL;
    *type = b->b[4];
    *len = n - 1u;
    return b->b + 5;
}

typedef int32_t (*dec_fn)(const uint8_t *, size_t, void *);

static int32_t d_hello(const uint8_t *p, size_t n, void *m) { return ti_dec_hello(p, n, (ti_hello *) m); }
static int32_t d_load(const uint8_t *p, size_t n, void *m) { return ti_dec_load(p, n, (ti_load *) m); }
static int32_t d_loaded(const uint8_t *p, size_t n, void *m) { return ti_dec_loaded(p, n, (ti_loaded *) m); }
static int32_t d_call(const uint8_t *p, size_t n, void *m) { return ti_dec_call(p, n, (ti_call *) m); }
static int32_t d_result(const uint8_t *p, size_t n, void *m) { return ti_dec_result(p, n, (ti_result *) m); }

/* Every proper prefix of a valid body, and the body plus one byte, are
 * refused. One check per message. */
static void truncations(const char *what, const uint8_t *body, size_t len, dec_fn dec) {
    void *m = malloc(sizeof(ti_result) + sizeof(ti_load));
    uint8_t *copy = (uint8_t *) malloc(len + 1u);
    int bad = 0;
    if (!m || !copy) abort();
    memcpy(copy, body, len);
    for (size_t k = 0; k < len; k++)
        if (dec(copy, k, m) == ZXV_OK) bad++;
    copy[len] = 0;
    if (dec(copy, len + 1u, m) == ZXV_OK) bad++;
    CHECK(bad == 0, "%s: %d truncated or extended bodies accepted", what, bad);
    free(copy);
    free(m);
}

static void test_header(void) {
    uint8_t h[4] = {0, 0, 0, 0};
    uint32_t n = 99;
    CHECK(ti_frame_header(h, 100, &n) == ZXV_E_FORMAT, "length 0 refused");
    h[0] = 101;
    CHECK(ti_frame_header(h, 100, &n) == ZXV_E_FORMAT, "length above the limit refused");
    h[0] = 100;
    CHECK(ti_frame_header(h, 100, &n) == ZXV_OK && n == 100, "length at the limit accepted");
    memset(h, 0xFF, sizeof h);
    CHECK(ti_frame_header(h, TI_REPLY_MAX, &n) == ZXV_E_FORMAT, "0xFFFFFFFF refused");
}

static void test_hello(void) {
    ti_buf b = {NULL, 0, 0};
    ti_hello h = {TI_PROTO_VERSION, TI_HELLO_SECCOMP}, g;
    const uint8_t *p;
    size_t n;
    uint32_t t;
    CHECK(ti_enc_hello(&b, &h) == ZXV_OK, "encode hello");
    p = body_of(&b, &n, &t);
    CHECK(p && t == TI_MSG_HELLO && n == 4, "hello frame");
    CHECK(p && ti_dec_hello(p, n, &g) == ZXV_OK && g.version == TI_PROTO_VERSION &&
              g.flags == TI_HELLO_SECCOMP, "hello round trip");
    if (p) truncations("hello", p, n, d_hello);
    uint8_t bad[4] = {1, 0, 2, 0};
    CHECK(ti_dec_hello(bad, 4, &g) == ZXV_E_FORMAT, "unknown hello flag refused");
    ti_buf_free(&b);
}

static void test_load(void) {
    ti_buf b = {NULL, 0, 0};
    ti_load m, g;
    uint8_t wasm[300];
    const uint8_t *p;
    size_t n;
    uint32_t t;
    memset(&m, 0, sizeof m);
    for (size_t i = 0; i < sizeof wasm; i++) wasm[i] = (uint8_t) (i * 7u);
    m.grants = 5;
    for (uint32_t i = 0; i < TI_LIMITS_COUNT; i++) m.limits[i] = (uint64_t) i * 1000u + 1u;
    strcpy(m.name, "fees v1");
    m.name_len = 7;
    m.wasm = wasm;
    m.wasm_len = sizeof wasm;
    CHECK(ti_enc_load(&b, &m) == ZXV_OK, "encode load");
    p = body_of(&b, &n, &t);
    CHECK(p && t == TI_MSG_LOAD, "load frame");
    CHECK(p && ti_dec_load(p, n, &g) == ZXV_OK && g.grants == 5 && g.limits[14] == 14001u &&
              strcmp(g.name, "fees v1") == 0 && g.wasm_len == sizeof wasm &&
              memcmp(g.wasm, wasm, sizeof wasm) == 0, "load round trip");
    if (p) truncations("load", p, n, d_load);

    ti_load bad = m;
    bad.name_len = 32;
    CHECK(ti_enc_load(&b, &bad) == ZXV_E_ARG, "a 32-byte name is refused");
    bad = m;
    bad.name[0] = '\n';
    CHECK(ti_enc_load(&b, &bad) == ZXV_E_ARG, "a non-printable name is refused");
    bad = m;
    bad.wasm_len = 0;
    CHECK(ti_enc_load(&b, &bad) == ZXV_E_ARG, "an empty module is refused");
    bad = m;
    bad.wasm_len = TI_WASM_MAX + 1u;
    CHECK(ti_enc_load(&b, &bad) == ZXV_E_ARG, "a module over 4 MiB is refused");
    /* a decoded name with a control byte */
    if (p) {
        uint8_t *c = (uint8_t *) malloc(n);
        memcpy(c, p, n);
        c[4 + 8 * TI_LIMITS_COUNT + 1] = 0x01;
        CHECK(ti_dec_load(c, n, &g) == ZXV_E_FORMAT, "control byte in the name refused");
        memcpy(c, p, n);
        c[4 + 8 * TI_LIMITS_COUNT] = 32; /* name length 32 */
        CHECK(ti_dec_load(c, n, &g) == ZXV_E_FORMAT, "name length 32 refused");
        free(c);
    }
    ti_buf_free(&b);
}

static void test_loaded(void) {
    ti_buf b = {NULL, 0, 0};
    ti_loaded m, g;
    const uint8_t *p;
    size_t n;
    uint32_t t;
    memset(&m, 0, sizeof m);
    m.status = ZW_E_REJECTED;
    m.msg_len = ti_msg_clean(m.msg, "FLOAT_OP: f64.add\tin function 0");
    CHECK(strchr(m.msg, '\t') == NULL && m.msg_len == 31, "ti_msg_clean replaces control bytes");
    CHECK(ti_enc_loaded(&b, &m) == ZXV_OK, "encode loaded");
    p = body_of(&b, &n, &t);
    CHECK(p && t == TI_MSG_LOADED && ti_dec_loaded(p, n, &g) == ZXV_OK && g.status == ZW_E_REJECTED &&
              strcmp(g.msg, m.msg) == 0, "loaded round trip");
    if (p) truncations("loaded", p, n, d_loaded);
    m.status = ZW_E_TRAP;
    CHECK(ti_enc_loaded(&b, &m) == ZXV_E_ARG, "a status LOAD cannot give is refused");
    if (p) {
        uint8_t c[64];
        memcpy(c, p, n < sizeof c ? n : sizeof c);
        c[0] = 0x39; /* -1001 + ... no: any value outside the set */
        c[1] = 0x30;
        CHECK(ti_dec_loaded(c, n, &g) == ZXV_E_FORMAT, "a decoded unknown status is refused");
    }
    char big[700];
    memset(big, 'a', sizeof big - 1u);
    big[sizeof big - 1u] = 0;
    CHECK(ti_msg_clean(m.msg, big) == TI_MSGTXT_MAX && strlen(m.msg) == TI_MSGTXT_MAX,
          "ti_msg_clean truncates to 512");
    ti_buf_free(&b);
}

static void test_call(void) {
    ti_buf b = {NULL, 0, 0};
    ti_call m, g;
    const uint8_t *p;
    size_t n;
    uint32_t t;
    memset(&m, 0, sizeof m);
    m.fuel = UINT64_C(123456789012);
    strcpy(m.export_name, "fee");
    m.export_len = 3;
    m.nargs = 3;
    m.args[0] = INT64_MIN;
    m.args[1] = -1;
    m.args[2] = INT64_MAX;
    CHECK(ti_enc_call(&b, &m) == ZXV_OK, "encode call");
    p = body_of(&b, &n, &t);
    CHECK(p && t == TI_MSG_CALL && ti_dec_call(p, n, &g) == ZXV_OK && g.fuel == m.fuel &&
              strcmp(g.export_name, "fee") == 0 && g.nargs == 3 && g.args[0] == INT64_MIN &&
              g.args[1] == -1 && g.args[2] == INT64_MAX, "call round trip (extreme int64s)");
    if (p) truncations("call", p, n, d_call);
    ti_call bad = m;
    bad.fuel = 0;
    CHECK(ti_enc_call(&b, &bad) == ZXV_E_ARG, "zero fuel refused");
    bad = m;
    bad.nargs = 9;
    CHECK(ti_enc_call(&b, &bad) == ZXV_E_ARG, "9 arguments refused");
    bad = m;
    bad.export_len = 0;
    CHECK(ti_enc_call(&b, &bad) == ZXV_E_ARG, "empty export refused");
    bad = m;
    bad.export_len = TI_EXPORT_MAX + 1u;
    CHECK(ti_enc_call(&b, &bad) == ZXV_E_ARG, "129-byte export refused");
    bad = m;
    bad.export_name[1] = '\0';
    CHECK(ti_enc_call(&b, &bad) == ZXV_E_ARG, "NUL inside the export refused");
    if (p) {
        uint8_t c[128];
        memcpy(c, p, n);
        c[9] = 0; /* NUL in the name */
        CHECK(ti_dec_call(c, n, &g) == ZXV_E_FORMAT, "decoded NUL in the export refused");
        memcpy(c, p, n);
        memset(c, 0, 8); /* fuel 0 */
        CHECK(ti_dec_call(c, n, &g) == ZXV_E_FORMAT, "decoded zero fuel refused");
    }
    ti_buf_free(&b);
}

static ti_result *sample_result(void) {
    ti_result *r = (ti_result *) calloc(1, sizeof *r);
    if (!r) abort();
    r->status = ZXV_OK;
    r->has_result = 1;
    r->result = -42;
    r->fuel_used = 999;
    r->nprops = 3;
    r->props[0] = (ti_prop){2, 6, 1, 3, "fee"};
    r->props[1] = (ti_prop){0, -1, 3, 0, ""};
    r->props[2] = (ti_prop){UINT32_MAX, INT64_MIN + 2, INT64_MAX, TI_LABEL_MAX, ""}; /* -(2^63-2) / (2^63-1): coprime */
    memset(r->props[2].label, 0xFF, TI_LABEL_MAX);
    return r;
}

static void test_result(void) {
    ti_buf b = {NULL, 0, 0};
    ti_result *m = sample_result(), *g = (ti_result *) calloc(1, sizeof *g), *bad;
    const uint8_t *p;
    size_t n;
    uint32_t t;
    if (!g) abort();
    CHECK(ti_enc_result(&b, m) == ZXV_OK, "encode result");
    p = body_of(&b, &n, &t);
    CHECK(p && t == TI_MSG_RESULT && n <= TI_REPLY_MAX, "result frame");
    CHECK(p && ti_dec_result(p, n, g) == ZXV_OK && g->result == -42 && g->fuel_used == 999 &&
              g->nprops == 3 && g->props[0].num == 6 && g->props[1].num == -1 && g->props[1].den == 3 &&
              g->props[2].kind == UINT32_MAX && g->props[2].label_len == 64 && g->props[2].label[63] == 0xFF,
          "result round trip");
    if (p) truncations("result", p, n, d_result);

    /* a full RESULT fits the reply limit */
    bad = sample_result();
    bad->nprops = TI_PROPS_MAX;
    for (uint32_t i = 0; i < TI_PROPS_MAX; i++) bad->props[i] = m->props[2];
    bad->msg_len = TI_MSGTXT_MAX;
    memset(bad->msg, 'm', TI_MSGTXT_MAX);
    ti_buf_free(&b);
    CHECK(ti_enc_result(&b, bad) == ZXV_OK && b.n - 4u <= TI_REPLY_MAX, "the largest RESULT fits TI_REPLY_MAX");

    struct {
        const char *what;
        void (*edit)(ti_result *);
    } rules[] = {
        {"den 0", NULL}, {"not normalised", NULL}, {"0/2", NULL}, {"negative den", NULL},
        {"has_result 2", NULL}, {"result without has_result", NULL}, {"error with proposals", NULL},
        {"error with a result", NULL}, {"fuel on nothing-ran", NULL}, {"unknown status", NULL},
        {"65 proposals", NULL}, {"label 65", NULL}, {"control byte in msg", NULL},
    };
    for (size_t k = 0; k < sizeof rules / sizeof rules[0]; k++) {
        free(bad);
        bad = sample_result();
        switch (k) {
        case 0: bad->props[0].den = 0; break;
        case 1: bad->props[0].num = 2, bad->props[0].den = 4; break;
        case 2: bad->props[0].num = 0, bad->props[0].den = 2; break;
        case 3: bad->props[0].den = -1; break;
        case 4: bad->has_result = 2; break;
        case 5: bad->has_result = 0; break;
        case 6: bad->status = ZW_E_TRAP, bad->has_result = 0, bad->result = 0; break;
        case 7: bad->status = ZW_E_TRAP, bad->nprops = 0; break;
        case 8: bad->status = ZW_E_EXPORT, bad->has_result = 0, bad->result = 0, bad->nprops = 0; break;
        case 9: bad->status = -12345, bad->has_result = 0, bad->result = 0, bad->nprops = 0; break;
        case 10: bad->nprops = TI_PROPS_MAX + 1u; break;
        case 11: bad->props[0].label_len = TI_LABEL_MAX + 1u; break;
        default: bad->msg_len = 1, bad->msg[0] = '\x7F'; break;
        }
        CHECK(ti_enc_result(&b, bad) == ZXV_E_ARG, "encoder refuses: %s", rules[k].what);
    }
    /* the same rules on the decoder, by editing a valid body */
    free(bad);
    bad = sample_result();
    bad->nprops = 1;
    ti_buf_free(&b);
    CHECK(ti_enc_result(&b, bad) == ZXV_OK, "encode one-proposal result");
    p = body_of(&b, &n, &t);
    if (p) {
        uint8_t c[256];
        const size_t pr = 4 + 1 + 8 + 8 + 1; /* first proposal */
        memcpy(c, p, n);
        memset(c + pr + 12, 0, 8); /* den 0 */
        CHECK(ti_dec_result(c, n, g) == ZXV_E_FORMAT, "decoder refuses den 0");
        memcpy(c, p, n);
        c[pr + 4] = 4;  /* num 4 */
        c[pr + 12] = 2; /* den 2 */
        CHECK(ti_dec_result(c, n, g) == ZXV_E_FORMAT, "decoder refuses 4/2");
        memcpy(c, p, n);
        c[4] = 2; /* has_result 2 */
        CHECK(ti_dec_result(c, n, g) == ZXV_E_FORMAT, "decoder refuses has_result 2");
        memcpy(c, p, n);
        c[0] = 0x17, c[1] = 0xFC, c[2] = 0xFF, c[3] = 0xFF; /* -1001 = ZW_E_REJECTED */
        CHECK(ti_dec_result(c, n, g) == ZXV_E_FORMAT, "decoder refuses a LOAD-only status");
        memcpy(c, p, n);
        c[21] = 65; /* nprops 65 */
        CHECK(ti_dec_result(c, n, g) == ZXV_E_FORMAT, "decoder refuses 65 proposals");
    }
    /* statuses */
    CHECK(ti_result_status_ok(ZW_E_FUEL) && ti_result_status_ok(ZXV_E_BUDGET) &&
              !ti_result_status_ok(ZW_E_REJECTED) && !ti_result_status_ok(ZXV_E_INTERNAL),
          "RESULT status set");
    CHECK(ti_loaded_status_ok(ZW_E_LINK) && !ti_loaded_status_ok(ZW_E_FUEL), "LOADED status set");
    free(bad);
    free(m);
    free(g);
    ti_buf_free(&b);
}

static void test_quit(void) {
    ti_buf b = {NULL, 0, 0};
    const uint8_t *p;
    size_t n;
    uint32_t t;
    CHECK(ti_enc_quit(&b) == ZXV_OK, "encode quit");
    p = body_of(&b, &n, &t);
    CHECK(p && t == TI_MSG_QUIT && n == 0 && ti_dec_quit(p, n) == ZXV_OK, "quit round trip");
    CHECK(ti_dec_quit(p, 1) == ZXV_E_FORMAT, "quit with a body refused");
    /* several frames append to one buffer */
    ti_hello h = {1, 0};
    CHECK(ti_enc_hello(&b, &h) == ZXV_OK && b.n == 5u + 9u, "frames append");
    ti_buf_free(&b);
}

int main(void) {
    printf("test_frame: the Tri-Isolate wire format\n");
    test_header();
    test_hello();
    test_load();
    test_loaded();
    test_call();
    test_result();
    test_quit();
    printf("test_frame: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
