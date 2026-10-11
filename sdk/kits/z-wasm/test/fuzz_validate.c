/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_validate.c — fuzzing the validator, the meter and the host.
 *
 * One entry point, LLVMFuzzerTestOneInput, checks for every input:
 *   - zw_validate never crashes, is deterministic, reports an offset inside
 *     the input and a reason in range, and NUL-terminates its message;
 *   - an accepted module meters, and the metered module passes the profile
 *     again (fuel export allowed);
 *   - an accepted module loads into the host (or wasm3 refuses it with
 *     ZW_E_LINK) and each export runs to a result, a trap or out of fuel,
 *     never crashing and never poisoning the host's context.
 *
 * Built two ways:
 *   - with -DZW_FUZZ_MAIN: a deterministic mutation fuzzer (fixed-seed
 *     xorshift, seeds assembled in C plus any .wasm files given), used by
 *     `make test` / `make fuzz` and under ASan/UBSan in CI;
 *   - with clang -fsanitize=fuzzer: libFuzzer drives the same function
 *     (`make fuzz-libfuzzer`).
 *
 *   fuzz_validate ITERATIONS [seed.wasm ...]
 * ZW_FUZZ_SEED=n picks another deterministic mutation stream; ZW_FUZZ_CORPUS=dir
 * writes the seeds there (a libFuzzer starting corpus). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wasm_build.h"
#include "zw_wasm.h"
#include "zwasm.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static zxv_ctx *g_ctx;
static zxv_cap *g_root;
static zxv_rmag *g_rm;
static uint32_t g_bud;
static zw_host *g_host;
static zw_policy *g_pol, *g_relaxed;

static unsigned long long g_stat[20], g_accepted, g_loaded, g_link, g_calls, g_ok, g_trap,
    g_fuel;

static void die(const char *what, const uint8_t *d, size_t n) {
    fprintf(stderr, "fuzz_validate: INVARIANT BROKEN: %s (input %zu bytes)\n", what, n);
    FILE *f = fopen("fuzz-crash.wasm", "wb");
    if (f) {
        fwrite(d, 1, n, f);
        fclose(f);
        fprintf(stderr, "  input written to fuzz-crash.wasm\n");
    }
    abort();
}

static void init(void) {
    if (g_host) return;
    if (zxv_ctx_create(&g_ctx) || zxv_cap_create(g_ctx, 1, NULL, ZXV_CAP_ALL, &g_root) ||
        zxv_rmag_create_cap(g_root, &g_rm) ||
        zxv_rmag_budget(g_root, g_rm, "fuzz", ZXV_RES_COMPUTE_CYCLES, INT64_C(1) << 60, 1, 1,
                        &g_bud) ||
        zw_host_create(g_root, g_rm, g_bud, &g_host) ||
        zw_host_grant(g_host, ZW_GRANT_RATIONAL | ZW_GRANT_LOGIC | ZW_GRANT_PROPOSE) ||
        zw_host_set_call_fuel(g_host, 20000) || zw_policy_create(&g_pol) ||
        zw_policy_create(&g_relaxed))
        abort();
    static const struct { const char *n, *s; } grants[] = {
        {"rat_add", "i(IIIIi)"}, {"rat_sub", "i(IIIIi)"}, {"rat_mul", "i(IIIIi)"},
        {"rat_div", "i(IIIIi)"}, {"rat_cmp", "i(IIIIi)"}, {"rat_normalize", "i(IIi)"},
        {"lp_not", "i(i)"},      {"lp_and", "i(ii)"},     {"lp_or", "i(ii)"},
        {"lp_from_evidence", "i(ii)"}, {"propose", "i(iIIii)"}};
    for (size_t i = 0; i < sizeof grants / sizeof grants[0]; i++) {
        (void) zw_policy_allow_import(g_pol, "zxv", grants[i].n, grants[i].s);
        (void) zw_policy_allow_import(g_relaxed, "zxv", grants[i].n, grants[i].s);
    }
    (void) zw_policy_set_limit(g_relaxed, ZW_LIM_MODULE_BYTES, UINT64_C(64) << 20);
    (void) zw_policy_set_limit(g_relaxed, ZW_LIM_BODY_BYTES, UINT64_C(8) << 20);
    (void) zw_policy_set_limit(g_relaxed, ZW_LIM_NESTING, 990);
    (void) zw_policy_set_limit(g_relaxed, ZW_LIM_GLOBALS, 4096);
    (void) zw_policy_set_limit(g_relaxed, ZW_LIM_EXPORTS, 4096);
}

/* Exported function names of a module the validator accepted. */
static int export_names(const uint8_t *w, size_t len, char names[8][64]) {
    size_t pos = 8;
    int k = 0;
    while (pos < len) {
        uint8_t id = w[pos++];
        uint32_t size, count;
        if (zw_rd_u32(w, len, &pos, &size) || size > len - pos) return k;
        if (id == SEC_EXPORT) {
            size_t q = pos, end = pos + size;
            if (zw_rd_u32(w, end, &q, &count)) return k;
            for (uint32_t i = 0; i < count && k < 8; i++) {
                uint32_t l, idx;
                uint8_t kind;
                if (zw_rd_u32(w, end, &q, &l) || l > end - q) return k;
                size_t at = q;
                q += l;
                if (zw_rd_u8(w, end, &q, &kind) || zw_rd_u32(w, end, &q, &idx)) return k;
                if (kind == 0 && l < 63) {
                    memcpy(names[k], w + at, l);
                    names[k][l] = '\0';
                    if (memchr(names[k], 0, l) == NULL) k++;
                }
            }
            return k;
        }
        pos += size;
    }
    return k;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    int32_t r1 = -1, r2 = -1, st1, st2;
    uint32_t s1 = 0, f1 = 0;
    uint64_t o1 = 0;
    char msg[300];
    size_t mlen = 0;
    init();
    memset(msg, 'X', sizeof msg);
    st1 = zw_validate(g_pol, data, size, &r1, &s1, &f1, &o1, msg, sizeof msg, &mlen);
    st2 = zw_validate(g_pol, data, size, &r2, NULL, NULL, NULL, NULL, 0, NULL);
    if (st1 != st2 || r1 != r2) die("validate is not deterministic", data, size);
    if (r1 < 0 || r1 > ZW_R_INDEX) die("reason out of range", data, size);
    if ((st1 == ZW_OK) != (r1 == ZW_R_OK)) die("status/reason mismatch", data, size);
    if (o1 > size) die("offset outside the input", data, size);
    if (!memchr(msg, 0, sizeof msg)) die("message not NUL-terminated", data, size);
    g_stat[r1]++;
    if (st1 != ZW_OK) return 0;
    g_accepted++;

    uint8_t *m = NULL;
    size_t ml = 0;
    if (zw_meter(data, size, &m, &ml) != ZW_OK) die("accepted module failed to meter", data, size);
    int32_t mr = -1;
    char mmsg[300];
    if (zw_validate_ex(g_relaxed, m, ml, 1, &mr, NULL, NULL, NULL, mmsg, sizeof mmsg, NULL) != ZW_OK) {
        fprintf(stderr, "metered: %s\n", mmsg);
        die("metered module failed the profile", data, size);
    }
    free(m);

    zw_plugin *p = NULL;
    int32_t st = zw_plugin_load(g_host, data, size, "fuzz", &p);
    if (st == ZW_E_LINK) {
        g_link++;
        return 0;
    }
    if (st != ZXV_OK) return 0; /* e.g. a policy difference; not a bug */
    g_loaded++;
    char names[8][64];
    int nn = export_names(data, size, names);
    for (int i = 0; i < nn; i++) {
        int64_t args[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        for (uint32_t na = 0; na <= 8; na++) {
            int32_t state = 0;
            st = zw_plugin_call(p, names[i], args, na, NULL, NULL, NULL);
            if (st == ZW_E_EXPORT) continue;
            g_calls++;
            if (st == ZXV_OK) g_ok++;
            if (st == ZW_E_TRAP) g_trap++;
            if (st == ZW_E_FUEL) g_fuel++;
            (void) zw_plugin_state(p, &state);
            break;
        }
        int32_t state = 0;
        (void) zw_plugin_state(p, &state);
        if (state == ZW_PLUGIN_DEAD) break;
    }
    zw_plugin_destroy(p);
    /* drain the queue so it never fills */
    uint64_t id;
    while (zw_queue_next(g_host, &id) == ZXV_OK) (void) zw_proposal_reject(g_host, g_root, id);
    int32_t poisoned = 1;
    if (zxv_ctx_is_poisoned(g_ctx, &poisoned) != ZXV_OK || poisoned)
        die("a plugin poisoned the host context", data, size);
    return 0;
}

#ifdef ZW_FUZZ_MAIN

static uint64_t g_rng = UINT64_C(0x9E3779B97F4A7C15);

static uint64_t rnd(void) {
    g_rng ^= g_rng >> 12;
    g_rng ^= g_rng << 25;
    g_rng ^= g_rng >> 27;
    return g_rng * UINT64_C(2685821657736338717);
}

typedef struct {
    uint8_t *b;
    size_t n;
} blob;

static blob g_seeds[512];
static int g_nseeds;

static void add_seed(const uint8_t *b, size_t n) {
    if (g_nseeds >= (int) (sizeof g_seeds / sizeof g_seeds[0])) return;
    g_seeds[g_nseeds].b = (uint8_t *) malloc(n ? n : 1);
    memcpy(g_seeds[g_nseeds].b, b, n);
    g_seeds[g_nseeds].n = n;
    g_nseeds++;
}

static void builtin_seeds(void) {
    wb m;
    wt t;
    static const uint8_t bodies[][24] = {
        {0x20, 0x00, 0x20, 0x01, 0x7C},                         /* i64.add            */
        {0x03, 0x40, 0x41, 0x01, 0x0D, 0x00, 0x0B},             /* loop br_if         */
        {0x41, 0x00, 0x28, 0x02, 0x00, 0x1A},                   /* i32.load           */
        {0x41, 0x05, 0x40, 0x00, 0x1A},                         /* memory.grow        */
        {0x20, 0x00, 0x04, 0x40, 0x01, 0x05, 0x01, 0x0B},       /* if else            */
        {0x02, 0x40, 0x41, 0x00, 0x0E, 0x01, 0x00, 0x00, 0x0B}, /* br_table           */
        {0x41, 0x01, 0x42, 0x02, 0x42, 0x03, 0x41, 0x00, 0x41, 0x00, 0x10, 0x00, 0x1A},
        {0x41, 0x01, 0x41, 0x00, 0x6D, 0x1A},                   /* div by zero        */
        {0x00},                                                 /* unreachable        */
        {0x10, 0x01},                                           /* recursion          */
    };
    static const size_t lens[] = {5, 7, 6, 5, 8, 9, 13, 6, 1, 2};
    for (size_t i = 0; i < sizeof lens / sizeof lens[0]; i++) {
        wt_one(&t, bodies[i], lens[i]);
        t.types[0] = i == 0 ? "II:I" : "i:";
        t.funcs[0].locals = "iI";
        t.memory = 1;
        t.mem_min = 1;
        t.mem_max = 1;
        t.globals = "iI";
        t.exports[0] = "f";
        t.nexports = 1;
        if (i == 6 || i == 9) {
            t.types[1] = "iIIii:i";
            t.ntypes = 2;
            t.fimports[0].mod = "zxv";
            t.fimports[0].name = "propose";
            t.fimports[0].type = 1;
            t.nfimports = 1;
        }
        wt_build(&m, &t);
        add_seed(m.b, m.n);
    }
    /* one per opcode class: a float op (rejected) and an integer op */
    static const uint8_t f[] = {0x41, 0x01, 0xB2, 0x1A};
    wt_one(&t, f, sizeof f);
    wt_build(&m, &t);
    add_seed(m.b, m.n);
}

static void mutate(uint8_t *b, size_t *n, size_t cap) {
    static const uint8_t interesting[] = {0x00, 0x01, 0x40, 0x7F, 0x80, 0xFF, 0x7D, 0x7C,
                                          0x7B, 0xFC, 0xFD, 0x0B, 0x60, 0x70};
    int k = 1 + (int) (rnd() % 4u);
    while (k--) {
        size_t len = *n;
        switch (rnd() % 9u) {
        case 0: if (len) b[rnd() % len] ^= (uint8_t) (1u << (rnd() % 8u)); break;
        case 1: if (len) b[rnd() % len] = (uint8_t) rnd(); break;
        case 2: if (len) b[rnd() % len] = interesting[rnd() % sizeof interesting]; break;
        case 3: /* insert */
            if (len < cap) {
                size_t at = len ? rnd() % (len + 1u) : 0;
                memmove(b + at + 1, b + at, len - at);
                b[at] = (uint8_t) rnd();
                (*n)++;
            }
            break;
        case 4: /* delete */
            if (len > 1) {
                size_t at = rnd() % len;
                memmove(b + at, b + at + 1, len - at - 1u);
                (*n)--;
            }
            break;
        case 5: /* truncate */
            if (len) *n = rnd() % len;
            break;
        case 6: /* duplicate a short range */
            if (len > 2) {
                size_t at = rnd() % len, l = 1 + rnd() % 8u;
                if (at + l > len) l = len - at;
                if (len + l <= cap) {
                    size_t to = rnd() % (len + 1u);
                    uint8_t tmp[8];
                    memcpy(tmp, b + at, l);
                    memmove(b + to + l, b + to, len - to);
                    memcpy(b + to, tmp, l);
                    *n += l;
                }
            }
            break;
        case 7: /* splice the tail of another seed */
            if (g_nseeds) {
                const blob *s = &g_seeds[rnd() % (uint64_t) g_nseeds];
                if (s->n) {
                    size_t from = rnd() % s->n, at = len ? rnd() % len : 0;
                    size_t l = s->n - from;
                    if (at + l > cap) l = cap - at;
                    memcpy(b + at, s->b + from, l);
                    *n = at + l;
                }
            }
            break;
        default: /* bump a byte (LEB values) */
            if (len) b[rnd() % len] += (uint8_t) (1 + rnd() % 3u);
            break;
        }
    }
}

int main(int argc, char **argv) {
    unsigned long long iters = argc > 1 ? strtoull(argv[1], NULL, 10) : 10000;
    static uint8_t buf[1 << 16];
    const char *seed = getenv("ZW_FUZZ_SEED");
    if (seed && strtoull(seed, NULL, 10)) g_rng ^= strtoull(seed, NULL, 10) * UINT64_C(0xD1B54A32D192ED03);
    builtin_seeds();
    for (int i = 2; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (!f) {
            fprintf(stderr, "fuzz_validate: cannot read %s\n", argv[i]);
            return 2;
        }
        size_t n = fread(buf, 1, sizeof buf, f);
        fclose(f);
        add_seed(buf, n);
    }
    const char *corpus = getenv("ZW_FUZZ_CORPUS");
    if (corpus) {
        for (int i = 0; i < g_nseeds; i++) {
            char path[512];
            snprintf(path, sizeof path, "%s/seed-%03d.wasm", corpus, i);
            FILE *f = fopen(path, "wb");
            if (f) {
                fwrite(g_seeds[i].b, 1, g_seeds[i].n, f);
                fclose(f);
            }
        }
    }
    for (int i = 0; i < g_nseeds; i++) LLVMFuzzerTestOneInput(g_seeds[i].b, g_seeds[i].n);
    for (unsigned long long it = 0; it < iters; it++) {
        const blob *s = &g_seeds[rnd() % (uint64_t) g_nseeds];
        size_t n = s->n < sizeof buf ? s->n : sizeof buf;
        memcpy(buf, s->b, n);
        mutate(buf, &n, sizeof buf);
        LLVMFuzzerTestOneInput(buf, n);
    }
    printf("fuzz_validate: %llu mutants + %d seeds, 0 invariant failures\n", iters, g_nseeds);
    printf("  accepted %llu, loaded %llu, wasm3 refused %llu, calls %llu (ok %llu, trap %llu, "
           "fuel %llu)\n", g_accepted, g_loaded, g_link, g_calls, g_ok, g_trap, g_fuel);
    printf("  rejections:");
    for (int r = 1; r <= ZW_R_INDEX; r++)
        if (g_stat[r]) printf(" %s %llu", zw_reason_name(r), g_stat[r]);
    printf("\n");
    zw_host_destroy(g_host);
    zw_policy_destroy(g_pol);
    zw_policy_destroy(g_relaxed);
    zxv_rmag_destroy(g_rm);
    zxv_cap_destroy(g_root);
    zxv_ctx_destroy(g_ctx);
    for (int i = 0; i < g_nseeds; i++) free(g_seeds[i].b);
    return 0;
}

#endif /* ZW_FUZZ_MAIN */
