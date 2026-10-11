/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_audit.c — fuzzing z-audit.
 *
 * An input is [u16le contract length][contract text][module bytes]. One
 * entry point, LLVMFuzzerTestOneInput, checks for every input:
 *   - zau_audit returns a report, twice the same (JSON and text);
 *   - the JSON report is valid JSON;
 *   - the verdict agrees with the counts, and a pass means the Z-Wasm
 *     validator accepted the module;
 *   - the walk agrees with the validator: when zw_validate accepts a
 *     module, z-audit finds no float or SIMD use and no refused import;
 *   - every finding's byte offset is inside the module.
 *
 * Built two ways, like the other kits' fuzzers:
 *   - with -DZAU_FUZZ_MAIN: a deterministic mutation fuzzer (fixed-seed
 *     xorshift; seeds assembled in C plus any .wasm files given, each
 *     paired with a contract), used by `make test` / `make fuzz` and under
 *     ASan + UBSan in CI;
 *   - with clang -fsanitize=fuzzer (`make fuzz-libfuzzer`).
 *
 *   fuzz_audit ITERATIONS [plugin.wasm ...]
 * ZAU_FUZZ_SEED=n picks another mutation stream; ZAU_FUZZ_CORPUS=dir writes
 * the seeds there (a libFuzzer starting corpus). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wasm_build.h"
#include "zau_test.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static zau_options *g_opt;
static unsigned long long g_npass, g_nfail, g_findings, g_analyzed, g_valid;

static void die(const char *what, const uint8_t *d, size_t n) {
    fprintf(stderr, "fuzz_audit: INVARIANT BROKEN: %s (input %zu bytes)\n", what, n);
    FILE *f = fopen("fuzz-crash.bin", "wb");
    if (f) {
        fwrite(d, 1, n, f);
        fclose(f);
        fprintf(stderr, "  input written to fuzz-crash.bin\n");
    }
    abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    zau_report *r1 = NULL, *r2 = NULL;
    size_t clen, n1 = 0, n2 = 0, t1 = 0, t2 = 0;
    const char *contract;
    const uint8_t *w;
    size_t wlen;
    if (!g_opt) {
        if (zau_options_create(&g_opt) != ZXV_OK ||
            zau_options_grant(g_opt, ZW_GRANT_RATIONAL | ZW_GRANT_LOGIC | ZW_GRANT_PROPOSE) != ZXV_OK)
            abort();
    }
    if (size < 2) return 0;
    clen = (size_t) data[0] | ((size_t) data[1] << 8);
    if (clen > size - 2u) clen = size - 2u;
    contract = (const char *) data + 2;
    w = data + 2 + clen;
    wlen = size - 2u - clen;
    if (zau_audit(g_opt, w, wlen, contract, clen, &r1) != ZXV_OK || !r1) die("zau_audit failed", data, size);
    if (zau_audit(g_opt, w, wlen, contract, clen, &r2) != ZXV_OK || !r2) die("zau_audit failed", data, size);
    char *j1 = report_json(r1, "m", "c", &n1), *j2 = report_json(r2, "m", "c", &n2);
    char *x1 = report_text(r1, &t1), *x2 = report_text(r2, &t2);
    if (!j1 || !j2 || !x1 || !x2) die("a report could not be rendered", data, size);
    if (n1 != n2 || memcmp(j1, j2, n1) != 0 || t1 != t2 || memcmp(x1, x2, t1) != 0)
        die("the report is not deterministic", data, size);
    if (!json_ok(j1, n1)) die("the JSON report is not valid JSON", data, size);

    int32_t pass = -1;
    uint32_t errors = 0, warnings = 0, nf = 0, sev_err = 0;
    (void) zau_report_verdict(r1, &pass, &errors, &warnings);
    if ((pass == 1) != (errors == 0)) die("verdict and error count disagree", data, size);
    int32_t vst = check_status(r1, 0);
    if (pass == 1 && vst != ZAU_PASS) die("a pass without the validator's acceptance", data, size);
    if (vst == ZAU_PASS) {
        g_valid++;
        if (check_status(r1, 1) == ZAU_FAIL) die("the validator accepted a module z-audit lists floats in", data, size);
        if (check_status(r1, 2) == ZAU_FAIL) die("the validator accepted a module with a refused import", data, size);
    }
    (void) zau_report_finding_count(r1, &nf);
    for (uint32_t i = 0; i < nf; i++) {
        int32_t sev = 0;
        int64_t off = 0;
        uint32_t chk = 99;
        const char *code = NULL;
        if (zau_report_finding(r1, i, &chk, &sev, &code, NULL, NULL, NULL, &off) != ZXV_OK || chk >= 7)
            die("a finding cannot be read", data, size);
        if (off >= 0 && (uint64_t) off > wlen && chk != 3 && chk != 5) die("a finding's offset is outside the module", data, size);
        if (sev == ZAU_ERROR) sev_err++;
        if (strcmp(code, "ANALYZED") == 0) g_analyzed++;
    }
    if (sev_err > errors) die("more error findings than errors", data, size);
    g_findings += nf;
    if (pass == 1) g_npass++;
    else g_nfail++;
    free(j1);
    free(j2);
    free(x1);
    free(x2);
    zau_report_destroy(r1);
    zau_report_destroy(r2);
    return 0;
}

#ifdef ZAU_FUZZ_MAIN

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

static blob g_seeds[64];
static int g_nseeds;

static void add_seed(const char *contract, const uint8_t *w, size_t wn) {
    size_t cl = strlen(contract);
    if (g_nseeds >= 64) return;
    blob *s = &g_seeds[g_nseeds];
    s->b = (uint8_t *) malloc(2u + cl + wn);
    if (!s->b) return;
    s->b[0] = (uint8_t) cl;
    s->b[1] = (uint8_t) (cl >> 8);
    memcpy(s->b + 2, contract, cl);
    memcpy(s->b + 2 + cl, w, wn);
    s->n = 2u + cl + wn;
    g_nseeds++;
}

static const char k_contract[] =
    "fn f(x: int[0..10], y: int[-5..5]) -> int[0..100] proposes rat[0..1] den|100 max 2;\n"
    "fn fee(amount: int[0..10^12], bps: int[0..10000]) -> int[0..0] proposes rat[0..10^12] den|100 max 1;\n"
    "fn run(n: int[1..1000]) -> int[0..3] proposes rat[0..1] max 1;\n"
    "fn checksum(n: int[0..10^6]) -> int;\nfn average_percent(total: int, count: int[1..*]) -> int;\n"
    "fn level(pct: int[0..100]) -> int[1..2];\nfn bucket(pct: int[0..100]) -> int[0..1];\n"
    "fn half() -> int proposes rat[0..1] max 1;\nfn hash(n: int[0..1000]) -> int;\n"
    "fn t(a: truth{true, false}, g: tag{*/neutral/*}) -> truth;\n";

static void builtin_seeds(void) {
    static wb m;
    wt t;
    /* straight-line arithmetic with a propose call */
    static const uint8_t c1[] = {0x20, 0x00, 0x20, 0x01, 0x6A, 0x41, 0x03, 0x6C, 0x41, 0x01, 0x42, 0x01,
                                 0x42, 0x02, 0x41, 0x00, 0x41, 0x00, 0x10, 0x00, 0x1A};
    wt_one(&t, c1, sizeof c1);
    t.types[0] = "ii:i";
    t.types[1] = "iIIii:i";
    t.ntypes = 2;
    t.fimports[0].mod = "zxv";
    t.fimports[0].name = "propose";
    t.fimports[0].type = 1;
    t.nfimports = 1;
    t.exports[0] = "f";
    t.nexports = 1;
    t.memory = 1;
    t.mem_min = 1;
    t.mem_max = 2;
    wt_build(&m, &t);
    add_seed(k_contract, m.b, m.n);
    /* constants, folding, select and a loop */
    static const uint8_t c2[] = {0x41, 0x05, 0x41, 0x07, 0x20, 0x00, 0x45, 0x1B, 0x42, 0x7F, 0xA7, 0x6A,
                                 0x03, 0x40, 0x0B};
    wt_one(&t, c2, sizeof c2);
    t.types[0] = "i:i";
    t.exports[0] = "level";
    t.nexports = 1;
    wt_build(&m, &t);
    add_seed(k_contract, m.b, m.n);
    /* a float body */
    static const uint8_t c3[] = {0x44, 0, 0, 0, 0, 0, 0, 0xF0, 0x3F, 0x1A, 0x41, 0x00};
    wt_one(&t, c3, sizeof c3);
    t.types[0] = "II:i";
    t.exports[0] = "average_percent";
    t.nexports = 1;
    wt_build(&m, &t);
    add_seed(k_contract, m.b, m.n);
    /* truth / tag parameters */
    static const uint8_t c4[] = {0x20, 0x00};
    wt_one(&t, c4, sizeof c4);
    t.types[0] = "ii:i";
    t.exports[0] = "t";
    t.nexports = 1;
    wt_build(&m, &t);
    add_seed(k_contract, m.b, m.n);
}

static void mutate(uint8_t *b, size_t *n, size_t cap) {
    int rounds = 1 + (int) (rnd() % 4u);
    for (int k = 0; k < rounds; k++) {
        size_t len = *n;
        switch (rnd() % 8u) {
        case 0:
            if (len) b[rnd() % len] ^= (uint8_t) (1u << (rnd() % 8u));
            break;
        case 1:
            if (len) b[rnd() % len] = (uint8_t) rnd();
            break;
        case 2: { /* interesting bytes */
            static const uint8_t v[] = {0x00, 0x01, 0x7F, 0x80, 0xFF, 0x0B, 0x10, 0x20, 0x41, 0x42,
                                        0x44, 0x6A, 0x7E, 0xFD, 0x03, 0x1B, '[', ']', '.', '*', '/'};
            if (len) b[rnd() % len] = v[rnd() % sizeof v];
            break;
        }
        case 3:
            if (len > 3) *n = 3 + rnd() % (len - 3);
            break;
        case 4:
            if (len && len < cap) {
                size_t at = rnd() % len;
                memmove(b + at + 1, b + at, len - at);
                b[at] = (uint8_t) rnd();
                *n = len + 1;
            }
            break;
        case 5:
            if (len > 3) {
                size_t at = 2 + rnd() % (len - 2);
                memmove(b + at, b + at + 1, len - at - 1);
                *n = len - 1;
            }
            break;
        case 6: /* splice the tail of another seed */
            if (g_nseeds) {
                const blob *s = &g_seeds[rnd() % (uint64_t) g_nseeds];
                size_t from = rnd() % s->n, at = len ? rnd() % len : 0;
                size_t l = s->n - from;
                if (at + l > cap) l = cap - at;
                memcpy(b + at, s->b + from, l);
                *n = at + l;
            }
            break;
        default:
            if (len) b[rnd() % len] += (uint8_t) (1 + rnd() % 3u);
            break;
        }
    }
}

int main(int argc, char **argv) {
    unsigned long long iters = argc > 1 ? strtoull(argv[1], NULL, 10) : 10000;
    static uint8_t buf[1 << 17];
    const char *seed = getenv("ZAU_FUZZ_SEED");
    if (seed && strtoull(seed, NULL, 10)) g_rng ^= strtoull(seed, NULL, 10) * UINT64_C(0xD1B54A32D192ED03);
    builtin_seeds();
    for (int i = 2; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (!f) {
            fprintf(stderr, "fuzz_audit: cannot read %s\n", argv[i]);
            return 2;
        }
        size_t n = fread(buf, 1, sizeof buf - 4096u, f);
        fclose(f);
        add_seed(k_contract, buf, n);
    }
    const char *corpus = getenv("ZAU_FUZZ_CORPUS");
    if (corpus) {
        for (int i = 0; i < g_nseeds; i++) {
            char path[512];
            snprintf(path, sizeof path, "%s/seed-%03d.bin", corpus, i);
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
    printf("fuzz_audit: %llu mutants + %d seeds, 0 invariant failures\n", iters, g_nseeds);
    printf("  passed %llu, failed %llu; validator accepted %llu; %llu findings; %llu bodies analysed\n",
           g_npass, g_nfail, g_valid, g_findings, g_analyzed);
    zau_options_destroy(g_opt);
    for (int i = 0; i < g_nseeds; i++) free(g_seeds[i].b);
    return 0;
}

#endif /* ZAU_FUZZ_MAIN */
