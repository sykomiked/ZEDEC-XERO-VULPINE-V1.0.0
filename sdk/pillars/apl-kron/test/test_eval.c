/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_eval.c — the evaluator: random programs compared with direct
 * libzxv arithmetic (values and refusals), algebraic properties, the
 * value calls, bindings, the work limit, kr_run_text and four threads. */
#include <pthread.h>

#include "kr_test.h"

static uint64_t g_rng = 0x9E3779B97F4A7C15ull;

static uint64_t rnd(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return g_rng;
}

static int64_t pick(int64_t lo, int64_t hi) { return lo + (int64_t) (rnd() % (uint64_t) (hi - lo + 1)); }

static void rnd_rat(int64_t *n, int64_t *d) {
    uint64_t r = rnd() % 100u;
    static const int64_t big[] = {INT64_MAX, INT64_MAX - 1, INT64_C(1) << 62, INT64_C(3037000499),
                                  INT64_C(3037000500), INT64_C(4294967296)};
    if (r < 55) {
        *n = pick(-9, 9);
        *d = 1;
    } else if (r < 85) {
        *n = pick(-20, 20);
        *d = pick(1, 12);
    } else if (r < 95) {
        *n = big[rnd() % 6u] * (rnd() & 1u ? 1 : -1);
        *d = 1;
    } else {
        *n = rnd() & 1u ? 1 : -1;
        *d = big[rnd() % 6u];
    }
    zxv_rat_normalize(*n, *d, n, d);
}

static kr_value *vec(uint64_t len, int64_t *num, int64_t *den) {
    kr_value *v = NULL;
    int64_t sh[2] = {(int64_t) len, 0};
    if (kr_value_make(KR_KIND_NUM, 1, sh, num, den, NULL, &v) != ZXV_OK) return NULL;
    return v;
}

/* ===== The reference: direct libzxv calls, in KRON's order ===== */

enum { R_ADD, R_SUB, R_MUL, R_DIV, R_MAX, R_MIN };

static int32_t ref2(int op, int64_t an, int64_t ad, int64_t bn, int64_t bd, int64_t *rn, int64_t *rd) {
    int32_t c = 0;
    switch (op) {
    case R_ADD: return zxv_rat_add(an, ad, bn, bd, rn, rd);
    case R_SUB: return zxv_rat_sub(an, ad, bn, bd, rn, rd);
    case R_MUL: return zxv_rat_mul(an, ad, bn, bd, rn, rd);
    case R_DIV:
        if (bn == 0) return KR_E_DIVZERO;
        return zxv_rat_div(an, ad, bn, bd, rn, rd);
    default:
        zxv_rat_cmp(an, ad, bn, bd, &c);
        if ((c >= 0) == (op == R_MAX)) {
            *rn = an;
            *rd = ad;
        } else {
            *rn = bn;
            *rd = bd;
        }
        return ZXV_OK;
    }
}

static const char *const k_apl[] = {"+", "-", "×", "÷", "⌈", "⌊"};
static const char *const k_k[] = {"+", "-", "*", "%", "|", "&"};

#define MAXN 8

typedef struct {
    int32_t st;
    uint64_t n;
    int64_t num[MAXN * MAXN], den[MAXN * MAXN];
} result;

static void compare(const char *what, const kr_program *p, kr_value *x, kr_value *y, const result *want) {
    kr_binding b[2] = {{"x", x}, {"y", y}};
    kr_value *v = NULL;
    kr_diag d;
    uint32_t nf = 0, nb;
    int32_t st;
    kr_free_count(p, &nf);
    nb = nf;
    st = kr_eval(p, b, nb, 0, &v, &d);
    CHECK(st == want->st, "%s: status %s, expected %s (%s)", what, kr_status_name(st), kr_status_name(want->st),
          d.msg);
    if (st == ZXV_OK && want->st == ZXV_OK) {
        uint64_t cnt = 0;
        int64_t num[MAXN * MAXN], den[MAXN * MAXN];
        kr_value_info(v, NULL, NULL, NULL, &cnt);
        CHECK(cnt == want->n, "%s: %llu elements, expected %llu", what, (unsigned long long) cnt,
              (unsigned long long) want->n);
        if (cnt == want->n && kr_value_rats(v, num, den, MAXN * MAXN) == ZXV_OK)
            for (uint64_t i = 0; i < cnt; i++)
                CHECK(num[i] == want->num[i] && den[i] == want->den[i], "%s: element %llu is %lld/%lld, expected %lld/%lld",
                      what, (unsigned long long) i, (long long) num[i], (long long) den[i], (long long) want->num[i],
                      (long long) want->den[i]);
    }
    if (st != ZXV_OK) CHECK(v == NULL, "%s: no value on refusal", what);
    kr_value_destroy(v);
}

static void differential(int cases) {
    for (int c = 0; c < cases; c++) {
        uint64_t n = (uint64_t) pick(1, MAXN), m = (uint64_t) pick(1, 4);
        int64_t xn[MAXN], xd[MAXN], yn[MAXN], yd[MAXN];
        kr_value *x, *y;
        int op = (int) (rnd() % 6u);
        int form = (int) (rnd() % 7u);
        char apl[64], kt[64];
        result want;
        kr_program *pa = NULL, *pk = NULL;
        memset(&want, 0, sizeof want);
        for (uint64_t i = 0; i < n; i++) {
            rnd_rat(&xn[i], &xd[i]);
            rnd_rat(&yn[i], &yd[i]);
        }
        x = vec(n, xn, xd);
        y = vec(form == 5 ? m : n, yn, yd);
        if (!x || !y) {
            CHECK(0, "vectors");
            kr_value_destroy(x);
            kr_value_destroy(y);
            continue;
        }
        kt[0] = '\0';
        switch (form) {
        case 0: /* elementwise */
            snprintf(apl, sizeof apl, "x%sy", k_apl[op]);
            snprintf(kt, sizeof kt, "x%sy", k_k[op]);
            want.n = n;
            for (uint64_t i = 0; i < n && want.st == ZXV_OK; i++)
                want.st = ref2(op, xn[i], xd[i], yn[i], yd[i], &want.num[i], &want.den[i]);
            break;
        case 1: /* APL reduce: right fold */
        case 2: /* K fold: left fold (assoc ops are right folds, see the README) */
        {
            int left = form == 2 && (op == R_SUB || op == R_DIV);
            int64_t an = 0, ad = 1;
            if (op == R_MAX || op == R_MIN) op = R_ADD;
            snprintf(apl, sizeof apl, "%s/x", k_apl[op]);
            snprintf(kt, sizeof kt, "%s/x", k_k[op]);
            want.n = 1;
            if (left) {
                an = xn[0];
                ad = xd[0];
                for (uint64_t i = 1; i < n && want.st == ZXV_OK; i++) want.st = ref2(op, an, ad, xn[i], xd[i], &an, &ad);
            } else {
                an = xn[n - 1];
                ad = xd[n - 1];
                for (uint64_t i = n - 1; i-- > 0 && want.st == ZXV_OK;) want.st = ref2(op, xn[i], xd[i], an, ad, &an, &ad);
            }
            want.num[0] = an;
            want.den[0] = ad;
            if (form == 1) kt[0] = '\0';
            else apl[0] = '\0';
            break;
        }
        case 3: /* running sum / product */
        {
            int64_t an = xn[0], ad = xd[0];
            op = op % 2 ? R_MUL : R_ADD;
            snprintf(apl, sizeof apl, "%s\\x", k_apl[op]);
            snprintf(kt, sizeof kt, "%s\\x", k_k[op]);
            want.n = n;
            want.num[0] = an;
            want.den[0] = ad;
            for (uint64_t i = 1; i < n && want.st == ZXV_OK; i++) {
                want.st = ref2(op, an, ad, xn[i], xd[i], &an, &ad);
                want.num[i] = an;
                want.den[i] = ad;
            }
            break;
        }
        case 4: /* dot product: elementwise, then a right fold */
        {
            int64_t t[MAXN][2], an, ad;
            snprintf(apl, sizeof apl, "+/x×y");
            snprintf(kt, sizeof kt, "+/x*y");
            want.n = 1;
            for (uint64_t i = 0; i < n && want.st == ZXV_OK; i++) want.st = ref2(R_MUL, xn[i], xd[i], yn[i], yd[i], &t[i][0], &t[i][1]);
            an = t[n - 1][0];
            ad = t[n - 1][1];
            for (uint64_t i = n - 1; i-- > 0 && want.st == ZXV_OK;) want.st = ref2(R_ADD, t[i][0], t[i][1], an, ad, &an, &ad);
            want.num[0] = an;
            want.den[0] = ad;
            break;
        }
        case 5: /* outer product */
            snprintf(apl, sizeof apl, "x∘.%sy", k_apl[op]);
            snprintf(kt, sizeof kt, "x%s\\:y", k_k[op]);
            want.n = n * m;
            for (uint64_t i = 0; i < n && want.st == ZXV_OK; i++)
                for (uint64_t j = 0; j < m && want.st == ZXV_OK; j++)
                    want.st = ref2(op, xn[i], xd[i], yn[j], yd[j], &want.num[i * m + j], &want.den[i * m + j]);
            break;
        default: /* exactness: (x÷y)×y is x whenever every step fits */
        {
            snprintf(apl, sizeof apl, "(x÷y)×y");
            snprintf(kt, sizeof kt, "(x%%y)*y");
            want.n = n;
            for (uint64_t i = 0; i < n && want.st == ZXV_OK; i++) {
                int64_t qn, qd;
                want.st = ref2(R_DIV, xn[i], xd[i], yn[i], yd[i], &qn, &qd);
                if (want.st == ZXV_OK) want.st = ref2(R_MUL, qn, qd, yn[i], yd[i], &want.num[i], &want.den[i]);
                if (want.st == ZXV_OK)
                    CHECK(want.num[i] == xn[i] && want.den[i] == xd[i], "libzxv: (x/y)*y is x");
            }
            break;
        }
        }
        if (want.st == ZXV_E_ARG) want.st = KR_E_DIVZERO;
        if (apl[0]) {
            pa = prog_s(apl, KR_SPELL_APL);
            CHECK(pa != NULL, "%s parses", apl);
            if (pa) compare(apl, pa, x, y, &want);
        }
        if (kt[0]) {
            pk = prog_s(kt, KR_SPELL_K);
            CHECK(pk != NULL, "%s parses", kt);
            if (pk) compare(kt, pk, x, y, &want);
        }
        kr_program_destroy(pa);
        kr_program_destroy(pk);
        kr_value_destroy(x);
        kr_value_destroy(y);
    }
}

/* ===== Properties on random small data ===== */

static void properties(int cases) {
    for (int c = 0; c < cases; c++) {
        uint64_t n = (uint64_t) pick(0, 9);
        int64_t xn[16], xd[16];
        kr_value *x;
        kr_binding b[1];
        for (uint64_t i = 0; i < n; i++) {
            xn[i] = pick(-30, 30);
            xd[i] = pick(1, 6);
            zxv_rat_normalize(xn[i], xd[i], &xn[i], &xd[i]);
        }
        x = vec(n, xn, xd);
        if (!x) continue;
        b[0].name = "x";
        b[0].value = x;
        {
            static const char *const props[] = {
                "(⌽⌽x)≡x",
                "s←x[⍋x] ⋄ s≡s[⍋s]",
                "(≢x)≡≢x[⍋x]",
                "(+/x)=+/x[⍋x]",
                "(+/x)=+/⌽x",
                "x≡x",
                "(,x)≡x",
                "(⍉⍉((≢x),1)⍴x)≡((≢x),1)⍴x",
                "s←x[⍋x] ⋄ ∧/(¯1↓s)≤1↓s",
                "((≢x)⍴x)≡x",
                "(x[x⍳x])≡x",
                "(3↑x),3↓x",
                "(x,x)≡2×⍳0",
                "(¯2↓x),¯2↑x",
                "(+\\x)≡+\\x",
                "(2⌽¯2⌽x)≡x",
                "(-x)≡0-x",
                "(x×x)≡|x×x",
            };
            for (size_t k = 0; k < sizeof props / sizeof props[0]; k++) {
                kr_program *pr = prog_s(props[k], KR_SPELL_APL);
                kr_value *v = NULL;
                kr_diag d;
                int32_t st;
                if (!pr) {
                    CHECK(0, "%s parses", props[k]);
                    continue;
                }
                st = kr_eval(pr, b, 1, 0, &v, &d);
                if (k == 11 || k == 13) { /* take/drop then catenate is x again */
                    CHECK(st == ZXV_OK, "%s: %s", props[k], d.msg);
                    if (st == ZXV_OK && n >= 3) {
                        int64_t vn[32], vd[32];
                        uint64_t cnt = 0;
                        kr_value_info(v, NULL, NULL, NULL, &cnt);
                        CHECK(cnt == n, "%s: length", props[k]);
                        if (cnt == n && kr_value_rats(v, vn, vd, 32) == ZXV_OK)
                            for (uint64_t i = 0; i < n; i++) CHECK(vn[i] == xn[i] && vd[i] == xd[i], "%s: element", props[k]);
                    }
                } else if (k == 12) {
                    CHECK(st == ZXV_OK, "%s", props[k]);
                    if (st == ZXV_OK) {
                        int32_t tv = -1;
                        kr_value_truths(v, &tv, 1);
                        CHECK(tv == (n == 0 ? ZXV_LP_TRUE : ZXV_LP_FALSE), "%s: x,x is empty only for empty x", props[k]);
                    }
                } else if (k == 8) {
                    CHECK(st == ZXV_OK, "%s: %s", props[k], d.msg);
                    if (st == ZXV_OK) {
                        int32_t tv = -1;
                        kr_value_truths(v, &tv, 1);
                        CHECK(tv == ZXV_LP_TRUE, "sorted: x[⍋x] is non-decreasing (n=%llu)", (unsigned long long) n);
                    }
                } else {
                    CHECK(st == ZXV_OK, "%s: %s (n=%llu)", props[k], d.msg, (unsigned long long) n);
                    if (st == ZXV_OK) {
                        int32_t kind = 0;
                        int32_t tv[16];
                        uint64_t cnt = 0;
                        kr_value_info(v, &kind, NULL, NULL, &cnt);
                        CHECK(kind == KR_KIND_TRUTH && cnt == 1, "%s gives one truth value", props[k]);
                        if (kind == KR_KIND_TRUTH && cnt == 1) {
                            kr_value_truths(v, tv, 16);
                            CHECK(tv[0] == ZXV_LP_TRUE, "%s holds (n=%llu)", props[k], (unsigned long long) n);
                        }
                    }
                }
                kr_value_destroy(v);
                kr_program_destroy(pr);
            }
        }
        kr_value_destroy(x);
    }
}

/* ===== Values ===== */

static void values(void) {
    kr_value *v = NULL;
    int64_t sh[2] = {2, 3};
    int64_t num[6] = {1, 2, 3, 4, 5, 6}, den[6] = {2, 4, -6, 1, 10, 3};
    int64_t on[6], od[6];
    int32_t truth[3] = {ZXV_LP_TRUE, ZXV_LP_BOTH, ZXV_LP_NEITHER}, ot[3];
    int32_t kind = 0;
    uint32_t rank = 9;
    int64_t osh[2];
    uint64_t cnt = 0;
    char b[256];
    size_t n = 0;
    CHECK(kr_value_make(KR_KIND_NUM, 2, sh, num, den, NULL, &v) == ZXV_OK, "make a matrix");
    CHECK(kr_value_info(v, &kind, &rank, osh, &cnt) == ZXV_OK && kind == KR_KIND_NUM && rank == 2 &&
              osh[0] == 2 && osh[1] == 3 && cnt == 6,
          "info");
    CHECK(kr_value_rats(v, on, od, 6) == ZXV_OK && on[0] == 1 && od[0] == 2 && on[1] == 1 && od[1] == 2 &&
              on[2] == -1 && od[2] == 2 && on[4] == 1 && od[4] == 2,
          "normalized: 3/-6 is -1/2");
    CHECK(kr_value_rats(v, on, od, 5) == ZXV_E_BUFFER, "rats: room");
    CHECK(kr_value_truths(v, ot, 3) == KR_E_DOMAIN, "numbers are not truth");
    CHECK(kr_value_text(v, b, sizeof b, &n) == ZXV_OK && strcmp(b, "(mat 2 3 1r2 1r2 -1r2 4 1r2 2)") == 0,
          "value text %s", b);
    CHECK(kr_value_text(v, b, 5, &n) == ZXV_E_BUFFER && n == strlen("(mat 2 3 1r2 1r2 -1r2 4 1r2 2)"), "R4");
    CHECK(kr_value_display(v, b, sizeof b, &n) == ZXV_OK && strcmp(b, "1r2 1r2 -1r2\n4 1r2 2") == 0, "display %s", b);
    kr_value_destroy(v);
    v = NULL;
    {
        int64_t s1[2] = {3, 0};
        CHECK(kr_value_make(KR_KIND_TRUTH, 1, s1, NULL, NULL, truth, &v) == ZXV_OK, "truth vector");
        CHECK(kr_value_truths(v, ot, 3) == ZXV_OK && ot[1] == ZXV_LP_BOTH, "truths");
        CHECK(kr_value_text(v, b, sizeof b, &n) == ZXV_OK && strcmp(b, "(tvec true both neither)") == 0, "%s", b);
        CHECK(kr_value_rats(v, on, od, 6) == KR_E_DOMAIN, "truth is not numbers");
        kr_value_destroy(v);
        v = NULL;
        truth[1] = 7;
        CHECK(kr_value_make(KR_KIND_TRUTH, 1, s1, NULL, NULL, truth, &v) == ZXV_E_ARG && !v, "a bad truth code");
        truth[1] = ZXV_LP_BOTH;
    }
    {
        int64_t z = 0, one = 1, mn = INT64_MIN;
        CHECK(kr_value_make(KR_KIND_NUM, 0, NULL, &one, &z, NULL, &v) == ZXV_E_ARG && !v, "denominator 0");
        CHECK(kr_value_make(KR_KIND_NUM, 0, NULL, &mn, &one, NULL, &v) != ZXV_OK && !v, "INT64_MIN refused");
        CHECK(kr_value_make(KR_KIND_NUM, 0, NULL, &one, &mn, NULL, &v) != ZXV_OK && !v, "INT64_MIN denominator refused");
        CHECK(kr_value_make(KR_KIND_NUM, 3, sh, num, den, NULL, &v) == ZXV_E_ARG, "rank 3");
        CHECK(kr_value_make(9, 0, NULL, num, den, NULL, &v) == ZXV_E_ARG, "bad kind");
        {
            int64_t hs[2] = {1 << 20, 1 << 20};
            CHECK(kr_value_make(KR_KIND_NUM, 2, hs, num, den, NULL, &v) == KR_E_LIMIT, "element limit");
        }
        {
            int64_t es[2] = {0, 5};
            CHECK(kr_value_make(KR_KIND_NUM, 2, es, NULL, NULL, NULL, &v) == ZXV_OK, "an empty 0x5 matrix");
            CHECK(kr_value_text(v, b, sizeof b, &n) == ZXV_OK && strcmp(b, "(mat 0 5)") == 0, "%s", b);
            kr_value_destroy(v);
            v = NULL;
        }
        CHECK(kr_value_make(KR_KIND_NUM, 0, NULL, &one, &one, NULL, &v) == ZXV_OK, "a scalar");
        CHECK(kr_value_text(v, b, sizeof b, &n) == ZXV_OK && strcmp(b, "1") == 0, "%s", b);
        kr_value_destroy(v);
        v = NULL;
    }
    kr_value_destroy(NULL);
    CHECK(kr_value_info(NULL, NULL, NULL, NULL, NULL) == ZXV_E_ARG, "NULL value");
}

/* ===== Bindings and limits ===== */

static void bindings(void) {
    kr_program *p = prog_s("+/x×y", KR_SPELL_APL);
    kr_value *x = NULL, *v = NULL;
    int64_t n3[3] = {1, 2, 3}, d3[3] = {1, 1, 1}, sh[2] = {3, 0};
    kr_diag d;
    uint64_t w = 0;
    if (!p) return;
    kr_value_make(KR_KIND_NUM, 1, sh, n3, d3, NULL, &x);
    {
        kr_binding b[2] = {{"x", x}, {"y", x}};
        CHECK(kr_eval(p, b, 2, 0, &v, &d) == ZXV_OK && strcmp(vtext(v), "14") == 0, "x·x = 14");
        kr_value_destroy(v);
        v = NULL;
        CHECK(kr_last_work(&w) == ZXV_OK && w > 0 && w < 40, "work counted: %llu", (unsigned long long) w);
        CHECK(kr_eval(p, b, 1, 0, &v, &d) == KR_E_NAME && !v, "y unbound: %s", d.msg);
        CHECK(strstr(d.msg, "'y'") != NULL, "the message names y");
    }
    {
        kr_binding b[3] = {{"x", x}, {"y", x}, {"z", x}};
        CHECK(kr_eval(p, b, 3, 0, &v, &d) == KR_E_NAME && d.line == 0, "z is not a free name: %s", d.msg);
    }
    {
        kr_binding b[2] = {{"x", x}, {"x", x}};
        CHECK(kr_eval(p, b, 2, 0, &v, &d) == KR_E_NAME, "x bound twice");
    }
    {
        kr_binding b[2] = {{"x", x}, {NULL, x}};
        CHECK(kr_eval(p, b, 2, 0, &v, &d) == ZXV_E_ARG, "a binding with no name");
    }
    CHECK(kr_eval(p, NULL, 1, 0, &v, &d) == ZXV_E_ARG, "count without bindings");
    CHECK(kr_eval(NULL, NULL, 0, 0, &v, &d) == ZXV_E_ARG, "NULL program");
    CHECK(kr_eval(p, NULL, 0, 0, NULL, &d) == ZXV_E_ARG, "NULL out");
    kr_program_destroy(p);
    /* an assigned name cannot be bound: it is not free */
    p = prog_s("x←1 ⋄ x", KR_SPELL_APL);
    if (p) {
        kr_binding b[1] = {{"x", x}};
        CHECK(kr_eval(p, b, 1, 0, &v, &d) == KR_E_NAME, "x is assigned before it is read");
        kr_program_destroy(p);
    }
    /* the work limit */
    p = prog_s("+/⍳100000", KR_SPELL_APL);
    if (p) {
        CHECK(kr_eval(p, NULL, 0, 1000, &v, &d) == KR_E_LIMIT && !v, "work limit 1000: %s", d.msg);
        CHECK(kr_last_work(&w) == ZXV_OK && w == 1000, "work stops at the limit: %llu", (unsigned long long) w);
        CHECK(kr_eval(p, NULL, 0, 0, &v, &d) == ZXV_OK && strcmp(vtext(v), "4999950000") == 0, "default limit");
        kr_value_destroy(v);
        v = NULL;
        kr_program_destroy(p);
    }
    /* the element limit */
    CHECK(strcmp(ev("⍳4194304", KR_SPELL_APL), "KR_E_LIMIT") != 0, "⍳2^22 is allowed");
    CHECK(strcmp(ev("⍳4194305", KR_SPELL_APL), "KR_E_LIMIT") == 0, "⍳(2^22+1) is not");
    CHECK(strcmp(ev("(⍳3000)∘.+⍳3000", KR_SPELL_APL), "KR_E_LIMIT") == 0, "a 9M outer product");
    kr_value_destroy(x);
}

/* ===== kr_run_text ===== */

static void run_text(void) {
    char b[512];
    size_t n = 0;
    const char *t = "2 2⍴1r2 3 ¯4 5";
    CHECK(kr_run_text(t, strlen(t), KR_SPELL_AUTO, KR_TEXT_VALUE, b, sizeof b, &n) == ZXV_OK &&
              strcmp(b, "(mat 2 2 1r2 3 -4 5)") == 0,
          "value %s", b);
    CHECK(kr_run_text(t, strlen(t), KR_SPELL_AUTO, KR_TEXT_DISPLAY, b, sizeof b, &n) == ZXV_OK &&
              strcmp(b, "1r2 3\n-4 5") == 0,
          "display %s", b);
    CHECK(kr_run_text(t, strlen(t), KR_SPELL_AUTO, KR_TEXT_ND, b, sizeof b, &n) == ZXV_OK &&
              strcmp(b, "(2 2;1 3 -4 5;2 1 1 1)") == 0,
          "nd %s", b);
    CHECK(kr_run_text(t, strlen(t), KR_SPELL_AUTO, KR_TEXT_PROGRAM, b, sizeof b, &n) == ZXV_OK &&
              strcmp(b, "kron\n(reshape (vec 2 2) (vec 1r2 3 -4 5))\n") == 0,
          "program %s", b);
    CHECK(kr_run_text(t, strlen(t), KR_SPELL_AUTO, KR_TEXT_DIGEST, b, sizeof b, &n) == ZXV_OK && n == 64, "digest");
    CHECK(kr_run_text("5", 1, KR_SPELL_K, KR_TEXT_ND, b, sizeof b, &n) == ZXV_OK && strcmp(b, "(!0;5;1)") == 0, "nd scalar %s", b);
    CHECK(kr_run_text("1#5", 3, KR_SPELL_K, KR_TEXT_ND, b, sizeof b, &n) == ZXV_OK && strcmp(b, "(,1;,5;,1)") == 0, "nd one %s", b);
    CHECK(kr_run_text("!0", 2, KR_SPELL_K, KR_TEXT_ND, b, sizeof b, &n) == ZXV_OK && strcmp(b, "(,0;!0;!0)") == 0, "nd empty %s", b);
    CHECK(kr_run_text("1 2<2", 5, KR_SPELL_K, KR_TEXT_ND, b, sizeof b, &n) == ZXV_OK && strcmp(b, "(,2;1 2;0 0)") == 0, "nd truth %s", b);
    CHECK(kr_run_text("1%0", 3, KR_SPELL_K, KR_TEXT_VALUE, b, sizeof b, &n) == KR_E_DIVZERO && strstr(b, "KR_E_DIVZERO"),
          "a refusal writes the diagnostic: %s", b);
    CHECK(kr_run_text("1%0", 3, KR_SPELL_K, KR_TEXT_VALUE, b, 4, &n) == ZXV_E_BUFFER && n > 4, "diagnostic R4");
    CHECK(kr_run_text(t, strlen(t), KR_SPELL_AUTO, KR_TEXT_VALUE, b, 3, &n) == ZXV_E_BUFFER &&
              n == strlen("(mat 2 2 1r2 3 -4 5)"),
          "value R4");
    CHECK(kr_run_text(t, strlen(t), KR_SPELL_AUTO, 9, b, sizeof b, &n) == ZXV_E_ARG, "bad what");
}

/* ===== Threads ===== */

static kr_program *g_tp;

static void *worker(void *arg) {
    int bad = 0;
    int64_t seed = (int64_t) (intptr_t) arg;
    for (int i = 0; i < 300; i++) {
        int64_t num[4] = {seed + i, 2, 3, 4}, den[4] = {1, 1, 1, 7}, sh[2] = {4, 0};
        kr_value *x = NULL, *v = NULL;
        kr_diag d;
        kr_binding b[1];
        int64_t on, od, wn, wd = 1;
        kr_value_make(KR_KIND_NUM, 1, sh, num, den, NULL, &x);
        b[0].name = "x";
        b[0].value = x;
        if (kr_eval(g_tp, b, 1, 0, &v, &d) != ZXV_OK) bad++;
        else {
            /* +/x×x = (s+i)^2 + 4 + 9 + 16/49 */
            wn = (seed + i) * (seed + i) * 49 + 13 * 49 + 16;
            wd = 49;
            zxv_rat_normalize(wn, wd, &wn, &wd);
            if (kr_value_rats(v, &on, &od, 1) != ZXV_OK || on != wn || od != wd) bad++;
        }
        kr_value_destroy(v);
        kr_value_destroy(x);
    }
    return (void *) (intptr_t) bad;
}

static void threads(void) {
    pthread_t th[4];
    g_tp = prog_s("+/x×x", KR_SPELL_APL);
    if (!g_tp) return;
    for (int i = 0; i < 4; i++) CHECK(pthread_create(&th[i], NULL, worker, (void *) (intptr_t) (i * 1000)) == 0, "thread");
    for (int i = 0; i < 4; i++) {
        void *r = NULL;
        pthread_join(th[i], &r);
        CHECK(r == NULL, "thread %d: %ld wrong results", i, (long) (intptr_t) r);
    }
    kr_program_destroy(g_tp);
}

int main(void) {
    differential(6000);
    properties(400);
    values();
    bindings();
    run_text();
    threads();
    return report("test_eval");
}
