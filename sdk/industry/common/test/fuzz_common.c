/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_common.c — a deterministic fuzzer for the shared industry layer.
 * Each iteration picks one of:
 *   - rational arithmetic: (a + b) - b == a, a * b == b * a, cmp antisymmetry
 *     (whenever the operations fit);
 *   - zind_qty_to_minor in every rounding mode: minor/10^d + residual == q
 *     exactly, |residual| < 10^-d, the residual's sign matches the mode, the
 *     half modes stay within half a unit, EXACT succeeds iff nothing is left;
 *   - zind_qty_parse of random text: whatever parses formats and re-parses
 *     to the same value;
 *   - a seen-set against a naive model;
 *   - a mutated export of a signed chain (flips, truncation, insertion):
 *     import never crashes, and a mutated chain never verifies.
 *
 *   fuzz_common ITERATIONS        ZIND_FUZZ_SEED=n: another stream */
#include "zind_test.h"

static zxv_cap *g_cap;
static uint8_t *g_exp;
static size_t g_exp_len;
static const uint8_t *g_trust[2];

static void die(const char *what) {
    fprintf(stderr, "fuzz_common: INVARIANT BROKEN: %s [%s]\n", what, zt_last_err());
    abort();
}

static int64_t rnd_i64(uint64_t *s) {
    switch (zt_rand(s) % 4) {
    case 0: return (int64_t) (zt_rand(s) % 2001) - 1000;
    case 1: return (int64_t) (zt_rand(s) % 2000001) - 1000000;
    case 2: {
        int64_t mag = (int64_t) (zt_rand(s) >> 1); /* one draw per statement: deterministic */
        return mag * ((zt_rand(s) & 1) ? 1 : -1);
    }
    default: return (int64_t) (zt_rand(s) >> 34) - (1LL << 29);
    }
}

static zind_qty rnd_qty(uint64_t *s) {
    zind_qty q = ZIND_QTY_ZERO;
    int64_t n = rnd_i64(s), d = rnd_i64(s);
    if (d == 0) d = 1;
    if (n == INT64_MIN) n = 0;
    if (d == INT64_MIN) d = 1;
    if (zind_qty_make(n, d, &q) != ZXV_OK) die("make of a non-zero denominator failed");
    if (!zind_qty_valid(q)) die("make gave a non-canonical quantity");
    return q;
}

static void op_arith(uint64_t *s) {
    zind_qty a = rnd_qty(s), b = rnd_qty(s), c, d, e;
    int32_t x = 0, y = 0;
    if (zind_qty_add(a, b, &c) == ZXV_OK && zind_qty_sub(c, b, &d) == ZXV_OK) {
        if (d.num != a.num || d.den != a.den) die("(a + b) - b != a");
    }
    if (zind_qty_mul(a, b, &c) == ZXV_OK) {
        if (zind_qty_mul(b, a, &e) != ZXV_OK || c.num != e.num || c.den != e.den)
            die("a * b != b * a");
    }
    if (zind_qty_cmp(a, b, &x) != ZXV_OK || zind_qty_cmp(b, a, &y) != ZXV_OK || x != -y)
        die("cmp not antisymmetric");
}

static void op_round(uint64_t *s) {
    zind_qty q = rnd_qty(s), res, mq, back, unit, half;
    uint8_t dg = (uint8_t) (zt_rand(s) % 7);
    int mode = (int) (zt_rand(s) % 6);
    int64_t m = 0;
    int32_t st = zind_qty_to_minor(q, dg, mode, &m, &res), c;
    if (st == ZXV_E_RANGE) return;
    if (mode == ZIND_ROUND_EXACT && st == ZIND_E_INEXACT) {
        int64_t m2 = 0;
        zind_qty r2;
        if (zind_qty_to_minor(q, dg, ZIND_ROUND_FLOOR, &m2, &r2) == ZXV_OK && r2.num == 0)
            die("EXACT refused a value FLOOR represents exactly");
        return;
    }
    if (st != ZXV_OK) die("to_minor failed unexpectedly");
    if (zind_qty_from_minor(m, dg, &mq) != ZXV_OK) die("from_minor");
    if (zind_qty_add(mq, res, &back) != ZXV_OK || back.num != q.num || back.den != q.den)
        die("minor + residual != q");
    if (mode == ZIND_ROUND_EXACT && res.num != 0) die("EXACT left a residual");
    if (zind_qty_from_minor(1, dg, &unit) != ZXV_OK) die("unit");
    {
        zind_qty ar = res.num < 0 ? (zind_qty){-res.num, res.den} : res;
        if (zind_qty_cmp(ar, unit, &c) != ZXV_OK || c >= 0) die("|residual| >= one minor unit");
        if (mode == ZIND_ROUND_HALF_UP || mode == ZIND_ROUND_HALF_EVEN) {
            if (zind_qty_mul(unit, (zind_qty){1, 2}, &half) != ZXV_OK) die("half");
            if (zind_qty_cmp(ar, half, &c) != ZXV_OK || c > 0) die("half mode beyond half a unit");
        }
    }
    if (mode == ZIND_ROUND_FLOOR && res.num < 0) die("FLOOR residual negative");
    if (mode == ZIND_ROUND_CEIL && res.num > 0) die("CEIL residual positive");
    if (mode == ZIND_ROUND_TRUNC && res.num != 0 && (res.num > 0) != (q.num > 0))
        die("TRUNC residual sign differs from q");
}

static void op_parse(uint64_t *s) {
    static const char alpha[] = "0123456789-./ e+";
    char t[40], buf[64];
    size_t n = 1 + zt_rand(s) % 30, len = 0;
    zind_qty q, r;
    for (size_t i = 0; i < n; i++) t[i] = alpha[zt_rand(s) % (sizeof alpha - 1)];
    t[n] = 0;
    if (zind_qty_parse(t, &q) != ZXV_OK) return;
    if (!zind_qty_valid(q)) die("parse gave a non-canonical quantity");
    if (zind_qty_format(q, buf, sizeof buf, &len) != ZXV_OK) die("format of a parsed value");
    if (zind_qty_parse(buf, &r) != ZXV_OK || r.num != q.num || r.den != q.den)
        die("format/parse round trip");
}

static void op_seen(uint64_t *s) {
    zind_seen *set = NULL;
    uint8_t model[64] = {0};
    uint32_t cap = 1 + (uint32_t) (zt_rand(s) % 40), n = 0, cnt = 0;
    int ops = 1 + (int) (zt_rand(s) % 100);
    if (zind_seen_create(cap, &set) != ZXV_OK) die("seen create");
    for (int i = 0; i < ops; i++) {
        uint8_t k = (uint8_t) (zt_rand(s) % 64), d[32];
        int32_t st, present = 0;
        zind_subject("fz", &k, 1, d);
        st = zind_seen_add(set, d);
        if (model[k]) {
            if (st != ZIND_E_REPLAY) die("seen: a duplicate was accepted");
        } else if (n >= cap) {
            if (st != ZXV_E_CAPACITY) die("seen: over capacity accepted");
        } else {
            if (st != ZXV_OK) die("seen: new key refused");
            model[k] = 1;
            n++;
        }
        if (zind_seen_has(set, d, &present) != ZXV_OK || present != model[k]) die("seen: has");
    }
    if (zind_seen_count(set, &cnt) != ZXV_OK || cnt != n) die("seen: count");
    zind_seen_destroy(set);
}

static void op_chain(uint64_t *s) {
    size_t len = g_exp_len;
    uint8_t *b = (uint8_t *) malloc(len + 16);
    zind_chain *c = NULL;
    int32_t st;
    int kind = (int) (zt_rand(s) % 4);
    memcpy(b, g_exp, len);
    if (kind == 0 || kind == 3) {
        int flips = 1 + (int) (zt_rand(s) % 3);
        for (int i = 0; i < flips; i++) {
            size_t at = zt_rand(s) % len;
            b[at] ^= (uint8_t) (1u << (zt_rand(s) % 8));
        }
    } else if (kind == 1) {
        len = zt_rand(s) % len;
    } else {
        size_t at = zt_rand(s) % len, k = 1 + zt_rand(s) % 16;
        memmove(b + at + k, b + at, len - at);
        for (size_t i = 0; i < k; i++) b[at + i] = (uint8_t) zt_rand(s);
        len += k;
    }
    st = zind_chain_import("fuzz/chain", b, len, &c);
    if (st == ZXV_OK) {
        int same = len == g_exp_len && memcmp(b, g_exp, len) == 0;
        uint32_t bad = 0;
        st = zind_chain_verify(g_cap, c, g_trust, 2, &bad);
        if (same && st != ZXV_OK) die("unmodified export does not verify");
        if (!same && st == ZXV_OK) die("a mutated chain verified");
        zind_chain_destroy(c);
    } else if (st != ZXV_E_FORMAT) {
        die("import returned something other than OK or FORMAT");
    }
    free(b);
}

int main(int argc, char **argv) {
    zt_env e;
    zind_signer *a, *b;
    zind_chain *c = NULL;
    unsigned long iters = argc > 1 ? strtoul(argv[1], NULL, 10) : 2000;
    uint64_t seed = zt_seed_from_env(0x5eed1234ULL), st = seed;
    unsigned long counts[5] = {0};
    if (zt_env_open(&e, NULL) != 0) return 2;
    g_cap = e.cap;
    a = zt_signer(e.cap, 11);
    b = zt_signer(e.cap, 12);
    if (!a || !b || zind_chain_create("fuzz/chain", 16, &c) != ZXV_OK) return 2;
    for (int i = 0; i < 3; i++) {
        uint8_t p[8] = {(uint8_t) i, 1, 2, 3};
        if (zind_chain_append(e.cap, c, i == 1 ? b : a, (uint16_t) i, p, (size_t) (4 + i), NULL,
                              NULL) != ZXV_OK)
            return 2;
    }
    zind_chain_export(c, NULL, 0, &g_exp_len);
    g_exp = (uint8_t *) malloc(g_exp_len);
    if (zind_chain_export(c, g_exp, g_exp_len, &g_exp_len) != ZXV_OK) return 2;
    g_trust[0] = zind_signer_pk(a);
    g_trust[1] = zind_signer_pk(b);
    for (unsigned long i = 0; i < iters; i++) {
        int op = (int) (zt_rand(&st) % 16);
        if (op < 4) op_arith(&st), counts[0]++;
        else if (op < 9) op_round(&st), counts[1]++;
        else if (op < 12) op_parse(&st), counts[2]++;
        else if (op < 14) op_seen(&st), counts[3]++;
        else op_chain(&st), counts[4]++;
    }
    printf("fuzz_common: %lu iterations (arith %lu, round %lu, parse %lu, seen %lu, chain %lu), "
           "seed %llu, 0 invariant failures\n",
           iters, counts[0], counts[1], counts[2], counts[3], counts[4],
           (unsigned long long) seed);
    free(g_exp);
    zind_chain_destroy(c);
    zind_signer_destroy(a);
    zind_signer_destroy(b);
    zt_env_close(&e);
    return 0;
}
