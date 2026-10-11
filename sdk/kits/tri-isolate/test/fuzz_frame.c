/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_frame.c — fuzzing the Tri-Isolate frame decoders, the code that
 * reads what a (possibly compromised) worker sends.
 *
 * An input is [u8 type][frame body]. One entry point,
 * LLVMFuzzerTestOneInput, checks for every input:
 *   - the decoder for that type returns ZXV_OK or ZXV_E_FORMAT, nothing
 *     else, and never reads outside the body (ASan);
 *   - a body it accepts is canonical: encoding the decoded message gives
 *     back exactly the same bytes;
 *   - an accepted RESULT obeys the host's rules (status in the allowed
 *     set, normalised proposals, labels <= 64, fits TI_REPLY_MAX).
 *
 * Built two ways, like the other kits' fuzzers:
 *   - with -DTI_FUZZ_MAIN: a deterministic mutation fuzzer (fixed-seed
 *     xorshift over valid frames of every type), used by `make test` /
 *     `make fuzz` and under ASan + UBSan in CI;
 *   - with clang -fsanitize=fuzzer (`make fuzz-libfuzzer`).
 *
 *   fuzz_frame ITERATIONS
 * TI_FUZZ_SEED=n picks another mutation stream; TI_FUZZ_CORPUS=dir writes
 * the seeds there (a libFuzzer starting corpus). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ti_frame.h"
#include "zwasm.h"
#include "zxv_api.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static unsigned long g_accepted, g_refused, g_bad;

static void fail(const char *why) {
    fprintf(stderr, "fuzz_frame: invariant broken: %s\n", why);
    g_bad++;
#if !defined(TI_FUZZ_MAIN)
    abort();
#endif
}

/* Encode a decoded message and compare with the body it came from. */
static void canonical(int32_t enc_st, ti_buf *b, const uint8_t *body, size_t len, uint32_t type) {
    if (enc_st != ZXV_OK) {
        fail("an accepted message does not encode");
        return;
    }
    if (b->n != 5u + len || b->b[4] != type || (len && memcmp(b->b + 5, body, len) != 0)) fail("not canonical");
}

static int32_t check_result(const ti_result *r) {
    if (!ti_result_status_ok(r->status) || r->nprops > TI_PROPS_MAX) return 0;
    for (uint32_t i = 0; i < r->nprops; i++)
        if (r->props[i].den <= 0 || r->props[i].label_len > TI_LABEL_MAX) return 0;
    return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    ti_buf b = {NULL, 0, 0};
    const uint8_t *body;
    size_t len;
    uint32_t type;
    int32_t st;
    if (size < 1 || size > TI_REQ_MAX) return 0;
    type = (uint32_t) (data[0] % 6u) + 1u;
    body = data + 1;
    len = size - 1u;
    /* the frame header check on whatever the first 4 body bytes are */
    if (len >= 4) {
        uint32_t n = 0;
        st = ti_frame_header(body, TI_REPLY_MAX, &n);
        if (st != ZXV_OK && st != ZXV_E_FORMAT) fail("header status");
        if (st == ZXV_OK && (n == 0 || n > TI_REPLY_MAX)) fail("header range");
    }
    switch (type) {
    case TI_MSG_HELLO: {
        ti_hello m;
        st = ti_dec_hello(body, len, &m);
        if (st == ZXV_OK) canonical(ti_enc_hello(&b, &m), &b, body, len, type);
        break;
    }
    case TI_MSG_LOAD: {
        ti_load m;
        st = ti_dec_load(body, len, &m);
        if (st == ZXV_OK) canonical(ti_enc_load(&b, &m), &b, body, len, type);
        break;
    }
    case TI_MSG_LOADED: {
        ti_loaded m;
        st = ti_dec_loaded(body, len, &m);
        if (st == ZXV_OK) canonical(ti_enc_loaded(&b, &m), &b, body, len, type);
        break;
    }
    case TI_MSG_CALL: {
        ti_call m;
        st = ti_dec_call(body, len, &m);
        if (st == ZXV_OK) canonical(ti_enc_call(&b, &m), &b, body, len, type);
        break;
    }
    case TI_MSG_RESULT: {
        ti_result *m = (ti_result *) malloc(sizeof *m);
        if (!m) return 0;
        st = ti_dec_result(body, len, m);
        if (st == ZXV_OK) {
            if (!check_result(m)) fail("an accepted RESULT breaks the host's rules");
            if (len + 1u > TI_REPLY_MAX) fail("an accepted RESULT is larger than TI_REPLY_MAX");
            canonical(ti_enc_result(&b, m), &b, body, len, type);
        }
        free(m);
        break;
    }
    default:
        st = ti_dec_quit(body, len);
        if (st == ZXV_OK) canonical(ti_enc_quit(&b), &b, body, len, type);
        break;
    }
    if (st != ZXV_OK && st != ZXV_E_FORMAT) fail("a decoder returned neither OK nor FORMAT");
    if (st == ZXV_OK) g_accepted++;
    else g_refused++;
    ti_buf_free(&b);
    return 0;
}

#if defined(TI_FUZZ_MAIN)
static uint64_t g_rng = UINT64_C(0x9E3779B97F4A7C15);

static uint64_t rnd(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

typedef struct {
    uint8_t *b;
    size_t n;
} seed;

/* One valid frame of each type, as [type][body]. */
static size_t make_seeds(seed *s) {
    ti_buf b = {NULL, 0, 0};
    size_t k = 0;
    ti_hello h = {TI_PROTO_VERSION, TI_HELLO_SECCOMP};
    ti_load l;
    ti_loaded ld;
    ti_call c;
    ti_result *r = (ti_result *) calloc(1, sizeof *r);
    static const uint8_t wasm[] = {0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00};
    if (!r) abort();
    memset(&l, 0, sizeof l);
    memset(&ld, 0, sizeof ld);
    memset(&c, 0, sizeof c);
    l.grants = 7;
    l.limits[1] = 16;
    strcpy(l.name, "seed");
    l.name_len = 4;
    l.wasm = wasm;
    l.wasm_len = sizeof wasm;
    ld.status = ZW_E_REJECTED;
    ld.msg_len = ti_msg_clean(ld.msg, "FLOAT_OP in function 0");
    c.fuel = 100000;
    strcpy(c.export_name, "fee");
    c.export_len = 3;
    c.nargs = 2;
    c.args[0] = 48000;
    c.args[1] = -125;
    r->status = ZXV_OK;
    r->has_result = 1;
    r->result = 600;
    r->fuel_used = 412;
    r->nprops = 2;
    r->props[0] = (ti_prop){2, 600, 1, 3, "fee"};
    r->props[1] = (ti_prop){0, -7, 3, 0, ""};
    if (ti_enc_hello(&b, &h) != ZXV_OK || ti_enc_load(&b, &l) != ZXV_OK || ti_enc_loaded(&b, &ld) != ZXV_OK ||
        ti_enc_call(&b, &c) != ZXV_OK || ti_enc_result(&b, r) != ZXV_OK || ti_enc_quit(&b) != ZXV_OK)
        abort();
    r->status = ZW_E_FUEL;
    r->has_result = 0;
    r->result = 0;
    r->nprops = 0;
    r->msg_len = ti_msg_clean(r->msg, "out of fuel");
    if (ti_enc_result(&b, r) != ZXV_OK) abort();
    free(r);
    /* split the stream into frames */
    for (size_t at = 0; at + 5u <= b.n && k < 8; k++) {
        uint32_t n = (uint32_t) b.b[at] | ((uint32_t) b.b[at + 1] << 8) | ((uint32_t) b.b[at + 2] << 16) |
                     ((uint32_t) b.b[at + 3] << 24);
        s[k].n = n; /* type + body */
        s[k].b = (uint8_t *) malloc(n);
        if (!s[k].b) abort();
        memcpy(s[k].b, b.b + at + 4, n);
        s[k].b[0] = (uint8_t) (s[k].b[0] - 1u); /* type byte -> selector (type = sel % 6 + 1) */
        at += 4u + n;
    }
    ti_buf_free(&b);
    return k;
}

int main(int argc, char **argv) {
    unsigned long iters = argc > 1 ? strtoul(argv[1], NULL, 10) : 20000ul;
    const char *seed_env = getenv("TI_FUZZ_SEED"), *corpus = getenv("TI_FUZZ_CORPUS");
    seed s[8];
    size_t ns = make_seeds(s);
    uint8_t *buf = (uint8_t *) malloc(70000);
    if (!buf) return 2;
    if (seed_env) g_rng ^= (uint64_t) strtoull(seed_env, NULL, 10) * UINT64_C(0x2545F4914F6CDD1D);
    if (!g_rng) g_rng = 1;
    for (size_t i = 0; i < ns; i++) {
        LLVMFuzzerTestOneInput(s[i].b, s[i].n);
        if (corpus) {
            char path[1024];
            snprintf(path, sizeof path, "%s/seed-%zu", corpus, i);
            FILE *f = fopen(path, "wb");
            if (f) {
                fwrite(s[i].b, 1, s[i].n, f);
                fclose(f);
            }
        }
    }
    if (g_accepted != ns) {
        fprintf(stderr, "fuzz_frame: a seed was refused\n");
        g_bad++;
    }
    for (unsigned long it = 0; it < iters; it++) {
        const seed *sd = &s[rnd() % ns];
        size_t n = sd->n;
        memcpy(buf, sd->b, n);
        unsigned edits = 1u + (unsigned) (rnd() % 4u);
        for (unsigned e = 0; e < edits; e++) {
            switch (rnd() % 6u) {
            case 0: if (n) buf[rnd() % n] ^= (uint8_t) (1u << (rnd() % 8u)); break;
            case 1: if (n) buf[rnd() % n] = (uint8_t) rnd(); break;
            case 2: if (n > 1) n = 1u + (size_t) (rnd() % (n - 1u)); break; /* truncate */
            case 3: if (n + 8u < 70000u) { for (int k = 0; k < 8; k++) buf[n++] = (uint8_t) rnd(); } break;
            case 4: if (n) buf[rnd() % n] = (uint8_t) ((rnd() & 1u) ? 0xFF : 0x00); break;
            default: if (n) buf[0] = (uint8_t) rnd(); break; /* another decoder */
            }
        }
        LLVMFuzzerTestOneInput(buf, n);
    }
    printf("fuzz_frame: %lu mutants + %zu seeds, %lu invariant failures\n", iters, ns, g_bad);
    printf("  accepted %lu, refused %lu\n", g_accepted, g_refused);
    for (size_t i = 0; i < ns; i++) free(s[i].b);
    free(buf);
    return g_bad ? 1 : 0;
}
#endif
