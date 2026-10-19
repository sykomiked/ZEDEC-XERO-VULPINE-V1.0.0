/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* axioms_rational.c — Tier 1 root axioms for exact rational arithmetic:
 * kernel/src/rational (rat_*) and kernel/src/rmag/rmag_core (rmag_*).
 *
 * Every arithmetic entry point is driven over a GENERATED grid of operands
 * (tier.h: machine-type edges and calculated domain boundaries) and checked
 * against an independent oracle that computes in 128 bits (hosted test code
 * only; the module under test never sees __int128 on its freestanding path).
 * The oracle decides validity by the documented contract: a valid rat_t has
 * |num| <= 2^63-1 and 0 < den <= 2^63-1, reduced.
 */
#include "tier.h"
#include "rational.h"
#include "rmag_core.h"

static const tier_known_t KNOWN_FAILURES[] = {
};

typedef __int128 i128;
typedef unsigned __int128 u128;
#define I64MAX ((i128) INT64_MAX)

static u128 ugcd(u128 a, u128 b)
{
    while (b) {
        u128 t = a % b;
        a = b;
        b = t;
    }
    return a;
}

/* Oracle: reduce num/den exactly; valid iff it fits the rat_t contract. */
static rat_t oracle(i128 num, i128 den)
{
    rat_t r = {0, 1, false};
    if (den == 0) return r;
    if (den < 0) {
        num = -num;
        den = -den;
    }
    if (num == 0) {
        r.valid = true;
        return r;
    }
    u128 an = num < 0 ? (u128) -num : (u128) num;
    u128 g = ugcd(an, (u128) den);
    num /= (i128) g;
    den /= (i128) g;
    if (num > I64MAX || num < -I64MAX || den > I64MAX) return r;
    r.num = (int64_t) num;
    r.den = (int64_t) den;
    r.valid = true;
    return r;
}

static bool same(rat_t a, rat_t b)
{
    if (!a.valid || !b.valid) return a.valid == b.valid;
    return a.num == b.num && a.den == b.den;
}

/* The generated operand set: every i64 edge numerator over a set of
 * denominators that includes 1, small primes and the den bound. */
static const int64_t DENS[] = {1, 2, 3, 7, INT64_MAX - 1, INT64_MAX};
#define NV (TIER_N(TIER_I64_EDGES) * TIER_N(DENS))
static rat_t V[NV];

static void build_values(void)
{
    unsigned k = 0;
    for (unsigned i = 0; i < TIER_N(TIER_I64_EDGES); i++)
        for (unsigned j = 0; j < TIER_N(DENS); j++) V[k++] = rat_make(TIER_I64_EDGES[i], DENS[j]);
}

static void axiom_make_and_from_int(void)
{
    /* rat_make over the full edge x edge grid (incl. den = 0, den < 0) */
    for (unsigned i = 0; i < TIER_N(TIER_I64_EDGES); i++)
        for (unsigned j = 0; j < TIER_N(TIER_I64_EDGES); j++) {
            int64_t n = TIER_I64_EDGES[i], d = TIER_I64_EDGES[j];
            rat_t got = rat_make(n, d), want = oracle(n, d);
            CHECK(same(got, want), "rat_make(%lld, %lld)", (long long) n, (long long) d);
            if (got.valid)
                CHECK(got.den > 0, "rat_make den > 0 (%lld/%lld)", (long long) n, (long long) d);
        }
    for (unsigned i = 0; i < TIER_N(TIER_I64_EDGES); i++) {
        int64_t n = TIER_I64_EDGES[i];
        rat_t got = rat_from_int(n);
        CHECK(same(got, oracle(n, 1)), "rat_from_int(%lld)", (long long) n);
    }
    CHECK(rat_is_zero(rat_zero()) && rat_zero().den == 1, "rat_zero is 0/1");
}

static void axiom_arith_grid(void)
{
    for (unsigned i = 0; i < NV; i++)
        for (unsigned j = 0; j < NV; j++) {
            rat_t a = V[i], b = V[j];
            if (!a.valid || !b.valid) {
                CHECK(!rat_add(a, b).valid && !rat_sub(a, b).valid && !rat_mul(a, b).valid &&
                          !rat_div(a, b).valid,
                      "invalid operand propagates (%u,%u)", i, j);
                continue;
            }
            i128 an = a.num, ad = a.den, bn = b.num, bd = b.den;
            CHECK(same(rat_add(a, b), oracle(an * bd + bn * ad, ad * bd)), "add %u %u", i, j);
            CHECK(same(rat_sub(a, b), oracle(an * bd - bn * ad, ad * bd)), "sub %u %u", i, j);
            CHECK(same(rat_mul(a, b), oracle(an * bn, ad * bd)), "mul %u %u", i, j);
            CHECK(same(rat_div(a, b), oracle(an * bd, ad * bn)), "div %u %u", i, j);
            /* comparison: exact, total, and agrees with eq */
            i128 l = an * bd, r = bn * ad;
            int want = l < r ? -1 : l > r ? 1 : 0;
            CHECK(rat_cmp(a, b) == want, "cmp %u %u", i, j);
            CHECK(rat_eq(a, b) == (want == 0), "eq %u %u", i, j);
        }
    /* zero denominators and invalid ordering */
    rat_t inv = rat_make(1, 0), one = rat_from_int(1), zero = rat_zero();
    CHECK(!rat_div(one, zero).valid, "1 / 0 is invalid");
    CHECK(!rat_div(zero, zero).valid, "0 / 0 is invalid");
    CHECK(rat_cmp(inv, one) == 1 && rat_cmp(one, inv) == -1 && rat_cmp(inv, inv) == 0,
          "invalid sorts after every valid value");
    CHECK(!rat_eq(inv, inv), "invalid is never eq");
    CHECK(!rat_neg(inv).valid && !rat_abs(inv).valid, "neg/abs propagate invalid");
    for (unsigned i = 0; i < NV; i++) {
        if (!V[i].valid) continue;
        CHECK(rat_neg(V[i]).valid && rat_neg(V[i]).num == -V[i].num, "neg %u total", i);
        rat_t ab = rat_abs(V[i]);
        CHECK(ab.valid && ab.den == V[i].den && ab.num == (V[i].num < 0 ? -V[i].num : V[i].num),
              "abs %u is valid and exact", i);
        CHECK(rat_is_int(V[i]) == (V[i].den == 1), "is_int %u", i);
    }
}

static void axiom_parse(void)
{
    static const struct {
        const char *s;
        bool valid;
        int64_t num, den;
    } P[] = {
        {"9223372036854775807", true, INT64_MAX, 1},     /* MAX */
        {"9223372036854775806", true, INT64_MAX - 1, 1}, /* MAX-1 */
        {"9223372036854775808", false, 0, 1},            /* MAX+1 */
        {"-9223372036854775807", true, -INT64_MAX, 1},   /* -MAX */
        {"-9223372036854775808", false, 0, 1},           /* -MAX-1 */
        {"0", true, 0, 1},
        {"-0", true, 0, 1},
        {"1", true, 1, 1},
        {"-1", true, -1, 1},
        {"", false, 0, 1},
        {" ", false, 0, 1},
        {"-", false, 0, 1},
        {".", false, 0, 1},
        {"1.", true, 1, 1},
        {".5", true, 1, 2},
        {"1/0", false, 0, 1}, /* zero denominator */
        {"0/1", true, 0, 1},
        {"1/9223372036854775807", true, 1, INT64_MAX},
        {"1/9223372036854775808", false, 0, 1},
        {"0.000000000000000001", true, 1, 1000000000000000000LL}, /* 18 places */
        {"0.0000000000000000001", false, 0, 1},                   /* 19 places */
        {"1.5x", false, 0, 1},
        {"3/4junk", false, 0, 1},
        {"--1", false, 0, 1},
        {"+-1", false, 0, 1},
        {"/4", false, 0, 1},
        {"4/", false, 0, 1},
        {"-12.75", true, -51, 4},
        {"+5", true, 5, 1},
        {"+3/4", true, 3, 4},
        {"+.5", true, 1, 2},
        {"99999999999999999999", false, 0, 1},   /* 20 digits: wraps if unchecked */
        {"18446744073709551617", false, 0, 1},   /* 2^64+1 wraps to 1 if unchecked */
        {"1/18446744073709551617", false, 0, 1}, /* the same, as a denominator */
        {"1/99999999999999999999", false, 0, 1},
    };
    for (unsigned i = 0; i < TIER_N(P); i++) {
        rat_t r = rat_from_string(P[i].s);
        rat_t want = {P[i].num, P[i].den, P[i].valid};
        CHECK(same(r, want), "parse \"%s\"", P[i].s);
    }
    CHECK(!rat_from_string(NULL).valid, "parse NULL");
}

/* ===== generated parse grid against an independent parser oracle =====
 * Strings are built from {sign} x {integer digits} x {"", ".", "/"} x
 * {fraction/denominator digits}, with digit counts on the boundaries the
 * parser has to get right: 0, 1, 17, 18, 19, 20 and 25 digits (a 64-bit
 * value has at most 19). Digits are all-9s, all-0s, "1 then 0s" or random. */
static rat_t parse_oracle(const char *s)
{
    rat_t bad = {0, 1, false};
    int sign = 1;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+')
        s++;
    u128 ip = 0;
    int nint = 0;
    while (*s >= '0' && *s <= '9') {
        ip = ip * 10 + (u128) (*s - '0');
        if (ip > (u128) INT64_MAX) return bad;
        s++;
        nint++;
    }
    if (*s == '/') {
        s++;
        u128 d = 0;
        int nd = 0;
        while (*s >= '0' && *s <= '9') {
            d = d * 10 + (u128) (*s - '0');
            if (d > (u128) INT64_MAX) return bad;
            s++;
            nd++;
        }
        if (!nint || !nd || d == 0 || *s) return bad;
        return oracle(sign * (i128) ip, (i128) d);
    }
    if (*s == '.') {
        s++;
        u128 frac = 0, scale = 1;
        int nf = 0;
        while (*s >= '0' && *s <= '9') {
            if (nf >= 18) { /* past 18 places only zeros are exact */
                if (*s != '0') return bad;
            } else {
                frac = frac * 10 + (u128) (*s - '0');
                scale *= 10;
            }
            s++;
            nf++;
        }
        if ((!nint && !nf) || *s) return bad;
        return oracle(sign * (i128) (ip * scale + frac), (i128) scale);
    }
    if (!nint || *s) return bad;
    return oracle(sign * (i128) ip, 1);
}

static void digits(char *out, int n, int style, uint64_t *seed)
{
    for (int i = 0; i < n; i++) {
        char c = style == 0   ? '9'
                 : style == 1 ? '0'
                 : style == 2 ? (i == 0 ? '1' : '0')
                              : (char) ('0' + (int) (tier_rand(seed) % 10));
        out[i] = c;
    }
    out[n] = 0;
}

static void axiom_parse_grid(void)
{
    static const int LEN[] = {0, 1, 17, 18, 19, 20, 25};
    static const char *SIGN[] = {"", "-", "+"};
    static const char *SEP[] = {"", ".", "/"};
    uint64_t seed = 0x1234;
    char a[32], b[32], s[96];
    unsigned n = 0;
    for (unsigned sg = 0; sg < 3; sg++)
        for (unsigned li = 0; li < TIER_N(LEN); li++)
            for (unsigned sp = 0; sp < 3; sp++)
                for (unsigned lj = 0; lj < (sp ? TIER_N(LEN) : 1); lj++)
                    for (int st = 0; st < 4; st++)
                        for (int st2 = 0; st2 < (sp ? 4 : 1); st2++) {
                            digits(a, LEN[li], st, &seed);
                            digits(b, sp ? LEN[lj] : 0, st2, &seed);
                            snprintf(s, sizeof s, "%s%s%s%s", SIGN[sg], a, SEP[sp], b);
                            rat_t got = rat_from_string(s), want = parse_oracle(s);
                            CHECK(same(got, want),
                                  "parse \"%s\": got %d %lld/%lld want %d %lld/%lld", s, got.valid,
                                  (long long) got.num, (long long) got.den, want.valid,
                                  (long long) want.num, (long long) want.den);
                            n++;
                        }
    CHECK(n > 1000, "parse grid size %u", n);
}

/* ===== exact rendering against an independent renderer ===== */
static void render_oracle(rat_t a, char *w)
{
    if (!a.valid) {
        strcpy(w, "#OVERFLOW");
        return;
    }
    int p = 0;
    if (a.num < 0) w[p++] = '-';
    u128 n = a.num < 0 ? (u128) - (i128) a.num : (u128) a.num, d = (u128) a.den;
    u128 dd = d;
    int tw = 0, fv = 0;
    while (dd % 2 == 0) {
        dd /= 2;
        tw++;
    }
    while (dd % 5 == 0) {
        dd /= 5;
        fv++;
    }
    int places = tw > fv ? tw : fv;
    char tmp[64];
    int t = 0;
    if (dd == 1 && places <= 18) {
        u128 scale = 1;
        for (int i = 0; i < places; i++) scale *= 10;
        u128 sc = n * (scale / d), ip = sc / scale, fp = sc % scale;
        do {
            tmp[t++] = (char) ('0' + (int) (ip % 10));
            ip /= 10;
        } while (ip);
        while (t) w[p++] = tmp[--t];
        if (fp) {
            w[p++] = '.';
            char f[32];
            for (int i = places - 1; i >= 0; i--) {
                f[i] = (char) ('0' + (int) (fp % 10));
                fp /= 10;
            }
            int len = places;
            while (len > 0 && f[len - 1] == '0') len--;
            memcpy(w + p, f, (size_t) len);
            p += len;
        }
    } else {
        do {
            tmp[t++] = (char) ('0' + (int) (n % 10));
            n /= 10;
        } while (n);
        while (t) w[p++] = tmp[--t];
        w[p++] = '/';
        do {
            tmp[t++] = (char) ('0' + (int) (d % 10));
            d /= 10;
        } while (d);
        while (t) w[p++] = tmp[--t];
    }
    w[p] = 0;
}

static void axiom_render_exact(void)
{
    rat_t R[400];
    unsigned n = 0;
    for (unsigned i = 0; i < NV; i++) R[n++] = V[i];
    const int64_t NUMS[] = {1, -1, 3, -7, 12345, INT64_MAX, -INT64_MAX};
    for (int k = 0; k < 63; k += 3)
        for (unsigned j = 0; j < TIER_N(NUMS) && n < TIER_N(R); j++)
            R[n++] = rat_make(NUMS[j], INT64_C(1) << k);
    int64_t p5 = 1;
    for (int k = 0; k < 28 && n + 3 < TIER_N(R); k++, p5 = k < 28 ? p5 * 5 : p5) {
        R[n++] = rat_make(1, p5);
        R[n++] = rat_make(-3, p5);
        if (p5 <= INT64_MAX / 1024) R[n++] = rat_make(7, p5 * 1024);
    }
    R[n++] = rat_make(1, 3);
    R[n++] = rat_make(-22, 7);
    R[n++] = rat_make(1, 0);
    for (unsigned i = 0; i < n; i++) {
        char got[96], want[96];
        uint32_t len = rat_to_string(R[i], got, sizeof got);
        render_oracle(R[i], want);
        CHECK(strcmp(got, want) == 0 && len == strlen(want), "render %lld/%lld: got %s want %s",
              (long long) R[i].num, (long long) R[i].den, got, want);
    }
}

/* Render into a buffer with a canary tail; every byte past max must survive. */
static void axiom_render_bounds(void)
{
    for (unsigned i = 0; i < NV; i++) {
        char full[128];
        uint32_t need = rat_to_string(V[i], full, sizeof full);
        CHECK(need < sizeof full && strlen(full) == need, "to_string %u full length", i);
        /* round trip: the exact rendering parses back to the same value */
        if (V[i].valid) CHECK(same(rat_from_string(full), V[i]), "round trip \"%s\"", full);
        uint32_t maxes[] = {0, 1, 2, need, need + 1};
        for (unsigned m = 0; m < TIER_N(maxes); m++) {
            char buf[160];
            memset(buf, 0x5A, sizeof buf);
            uint32_t mx = maxes[m];
            uint32_t got = rat_to_string(V[i], mx ? buf : buf, mx);
            if (mx == 0) {
                CHECK(got == 0 && (unsigned char) buf[0] == 0x5A, "max=0 writes nothing (%u)", i);
                continue;
            }
            CHECK(got == need, "to_string %u max=%u returns full length", i, mx);
            size_t used = 0;
            while (used < mx && buf[used]) used++;
            CHECK(used < mx, "to_string %u max=%u NUL-terminated", i, mx);
            CHECK(memcmp(buf, full, used) == 0, "to_string %u max=%u prefix", i, mx);
            bool canary = true;
            for (uint32_t k = mx; k < sizeof buf; k++) canary &= (unsigned char) buf[k] == 0x5A;
            CHECK(canary, "to_string %u max=%u never writes past max", i, mx);
        }
    }
    CHECK(rat_to_string(rat_zero(), NULL, 10) == 0, "to_string NULL out");
}

/* rat_to_fixed: width = min(places, 18), round half away from zero. */
static void axiom_fixed(void)
{
    uint32_t places[] = {0, 1, 2, 17, 18, 19, UINT32_MAX};
    rat_t vals[] = {rat_make(1, 3),         rat_make(2, 3),          rat_make(-1, 2),
                    rat_make(1, 2),         rat_make(-5, 1),         rat_make(999, 1000),
                    rat_make(INT64_MAX, 1), rat_make(-INT64_MAX, 7), rat_make(1, INT64_MAX),
                    rat_make(1, 100),       rat_make(-7, 1000),      rat_make(1, 1024),
                    rat_make(3, 50000),     rat_make(-1, 3000),      rat_make(0, 1)};
    for (unsigned v = 0; v < TIER_N(vals); v++)
        for (unsigned p = 0; p < TIER_N(places); p++) {
            char buf[64];
            bool exact = true;
            uint32_t n = rat_to_fixed(vals[v], places[p], buf, sizeof buf, &exact);
            uint32_t eff = places[p] > 18 ? 18 : places[p];
            const char *dot = strchr(buf, '.');
            CHECK(n == strlen(buf), "to_fixed %u/%u returns length", v, places[p]);
            if (eff == 0)
                CHECK(dot == NULL, "to_fixed %u places=0 has no point", v);
            else
                CHECK(dot && strlen(dot + 1) == eff, "to_fixed %u places=%u width %u", v, places[p],
                      eff);
            /* oracle: round half away from zero of |num|*10^eff/den */
            u128 sc = 1;
            for (uint32_t k = 0; k < eff; k++) sc *= 10;
            u128 an = vals[v].num < 0 ? (u128) - (i128) vals[v].num : (u128) vals[v].num;
            u128 q = an * sc / (u128) vals[v].den, rem = an * sc % (u128) vals[v].den;
            CHECK(exact == (rem == 0), "to_fixed %u places=%u exact flag", v, places[p]);
            if (rem * 2 >= (u128) vals[v].den) q++;
            char want[64];
            int w = 0;
            if (vals[v].num < 0 && q != 0) want[w++] = '-';
            u128 ip = q / sc, fp = q % sc;
            char tmp[48];
            int t = 0;
            do {
                tmp[t++] = (char) ('0' + (int) (ip % 10));
                ip /= 10;
            } while (ip);
            while (t) want[w++] = tmp[--t];
            if (eff) {
                want[w++] = '.';
                for (int k = (int) eff - 1; k >= 0; k--) {
                    u128 d = fp;
                    for (int z = 0; z < k; z++) d /= 10;
                    want[w++] = (char) ('0' + (int) (d % 10));
                }
            }
            want[w] = 0;
            CHECK(strcmp(buf, want) == 0, "to_fixed %u places=%u: got %s want %s", v, places[p],
                  buf, want);
        }
    char b[4];
    bool ex = true;
    CHECK(rat_to_fixed(rat_make(1, 0), 2, b, 1, &ex) == 1 && b[0] == 0 && !ex,
          "to_fixed invalid max=1 terminates and reports inexact");
    CHECK(rat_to_fixed(rat_zero(), 2, NULL, 4, &ex) == 0, "to_fixed NULL out");
    /* a NULL `exact` is allowed on every path */
    CHECK(rat_to_fixed(rat_make(1, 3), 4, b, sizeof b, NULL) == 6 &&
              rat_to_fixed(rat_make(1, 0), 4, b, sizeof b, NULL) == 1,
          "to_fixed with exact == NULL");
    /* an invalid value renders "#" whenever there is room for it */
    for (uint32_t mx = 2; mx <= 4; mx++) {
        char ib[8];
        memset(ib, 0x5A, sizeof ib);
        CHECK(rat_to_fixed(rat_make(1, 0), 2, ib, mx, &ex) == 1 && strcmp(ib, "#") == 0 &&
                  (unsigned char) ib[mx] == 0x5A,
              "to_fixed invalid max=%u -> \"#\"", mx);
    }
    /* truncation: every max from 1 to the full length + 1 terminates inside
     * the buffer, keeps the prefix and never writes past max */
    rat_t tv[] = {rat_make(-22, 7), rat_make(1, 100), rat_make(INT64_MAX, 3)};
    for (unsigned v = 0; v < TIER_N(tv); v++) {
        char full[96];
        uint32_t need = rat_to_fixed(tv[v], 18, full, sizeof full, &ex);
        for (uint32_t mx = 1; mx <= need + 1; mx++) {
            char buf[128];
            memset(buf, 0x5A, sizeof buf);
            uint32_t got = rat_to_fixed(tv[v], 18, buf, mx, &ex);
            size_t used = strnlen(buf, mx);
            bool canary = true;
            for (uint32_t k = mx; k < sizeof buf; k++) canary &= (unsigned char) buf[k] == 0x5A;
            CHECK(got == need && used < mx && memcmp(buf, full, used) == 0 && canary,
                  "to_fixed %u max=%u: length, terminator, prefix, nothing past max", v, mx);
        }
    }
}

/* rat_split: parts in {0, 1, 2, 3, 2^16}; out[] untouched on refusal. */
static void axiom_split(void)
{
    const uint32_t PARTS[] = {0, 1, 2, 3, 65536};
    rat_t amts[] = {rat_make(1, 1),         rat_make(-7, 3), rat_make(INT64_MAX, 1),
                    rat_make(1, INT64_MAX), rat_make(1, 0),  rat_zero()};
    rat_t *out = calloc(65536 + 1, sizeof *out);
    if (!out) {
        CHECK(0, "calloc");
        return;
    }
    for (unsigned a = 0; a < TIER_N(amts); a++)
        for (unsigned p = 0; p < TIER_N(PARTS); p++) {
            uint32_t n = PARTS[p];
            for (uint32_t k = 0; k <= 65536; k++) out[k] = (rat_t){42, 43, true};
            bool ok = rat_split(amts[a], n, out);
            i128 sd = (i128) amts[a].den * n;
            rat_t want = amts[a].valid && n ? oracle(amts[a].num, sd) : (rat_t){0, 1, false};
            CHECK(ok == want.valid, "split %u by %u accepted iff the share fits", a, n);
            CHECK(out[n].num == 42 && out[n].den == 43, "split %u by %u: out[%u] untouched", a, n,
                  n);
            if (!ok) {
                CHECK(out[0].num == 42, "split %u by %u refused: out untouched", a, n);
                continue;
            }
            CHECK(same(out[0], want) && same(out[n - 1], want), "split %u by %u share", a, n);
        }
    CHECK(!rat_split(rat_from_int(1), 2, NULL), "split NULL out");
    free(out);
}

/* ===== rmag_core: quota table and quota arithmetic ===== */
#define RMAG_SLOTS 8

static void axiom_rmag(void)
{
    rmag_init(RMAG_SLOTS);
    rational_t z = rmag_get_quota(0);
    CHECK(z.num == 0 && z.den == 1, "fresh slot is 0/1, not 0/0");
    /* off-by-one at the table bound, generated: {0, 1, N-1, N, N+1, MAX} */
    uint64_t b[5];
    unsigned nb = tier_bounds_u64(RMAG_SLOTS - 1, b);
    for (unsigned i = 0; i < nb; i++) {
        rational_t q = {(int64_t) b[i] + 5, 3};
        rmag_set_quota(b[i], q);
        rational_t g = rmag_get_quota(b[i]);
        if (b[i] < RMAG_SLOTS)
            CHECK(g.num == q.num && g.den == 3, "slot %llu stores", (unsigned long long) b[i]);
        else
            CHECK(g.num == 0 && g.den == 1, "slot %llu out of range reads 0/1",
                  (unsigned long long) b[i]);
    }
    rmag_set_quota(UINT64_MAX, (rational_t){9, 1});
    CHECK(rmag_get_quota(UINT64_MAX).num == 0, "slot UINT64_MAX ignored");

    /* exact arithmetic on a generated grid that cannot overflow int64 */
    const int64_t SM[] = {-3, -1, 0, 1, 2, 5, 7};
    const int64_t SD[] = {1, 2, 3, 6};
    for (unsigned i = 0; i < TIER_N(SM); i++)
        for (unsigned j = 0; j < TIER_N(SD); j++)
            for (unsigned k = 0; k < TIER_N(SM); k++)
                for (unsigned l = 0; l < TIER_N(SD); l++) {
                    rational_t a = {SM[i], SD[j]}, c = {SM[k], SD[l]};
                    i128 an = a.num, ad = a.den, cn = c.num, cd = c.den;
                    rat_t w;
                    rational_t g = rmag_add_quotas(a, c);
                    w = oracle(an * cd + cn * ad, ad * cd);
                    CHECK(g.num == w.num && g.den == w.den, "rmag add");
                    g = rmag_sub_quotas(a, c);
                    w = oracle(an * cd - cn * ad, ad * cd);
                    CHECK(g.num == w.num && g.den == w.den, "rmag sub");
                    g = rmag_mul_quotas(a, c);
                    w = oracle(an * cn, ad * cd);
                    CHECK(g.num == w.num && g.den == w.den, "rmag mul");
                    if (c.num != 0) {
                        g = rmag_div_quotas(a, c);
                        w = oracle(an * cd, ad * cn);
                        CHECK(g.num == w.num && g.den == w.den, "rmag div");
                        rational_t q = {7, 7};
                        CHECK(rmag_div_quotas_checked(a, c, &q) && q.num == w.num && q.den == w.den,
                              "rmag div checked");
                    }
                }
    /* division by zero (F-RMAG-DIV0, fixed in 209c3d5): the unchecked call
     * returns the documented 0/1 (never sign(x)/1), the checked call refuses */
    for (unsigned i = 0; i < TIER_N(SM); i++) {
        rational_t x = {SM[i] ? SM[i] : 1, 1};
        const rational_t bad[][2] = {{x, {0, 1}}, {{x.num, 0}, {1, 1}}, {x, {1, 0}}};
        for (unsigned k = 0; k < TIER_N(bad); k++) {
            rational_t g = rmag_div_quotas(bad[k][0], bad[k][1]), q = {7, 7};
            CHECK(g.num == 0 && g.den == 1, "rmag_div(%lld, case %u) -> %lld/%lld, want 0/1",
                  (long long) SM[i], k, (long long) g.num, (long long) g.den);
            CHECK(!rmag_div_quotas_checked(bad[k][0], bad[k][1], &q) && q.num == 7 && q.den == 7,
                  "rmag_div_quotas_checked refuses case %u, output untouched", k);
        }
    }
    {
        rational_t q = {7, 7};
        CHECK(!rmag_div_quotas_checked((rational_t){1, 2}, (rational_t){1, 3}, NULL),
              "checked div: NULL output refused");
        const rational_t refuse[][2] = {
            {{INT64_MIN, 1}, {1, 1}}, /* INT64_MIN operand fields */
            {{1, INT64_MIN}, {1, 1}},           {{1, 1}, {INT64_MIN, 1}}, {{1, 1}, {1, INT64_MIN}},
            {{INT64_MAX, 1}, {1, 2}},           /* a.num * b.den overflows */
            {{1, INT64_MAX}, {2, 1}},           /* a.den * b.num overflows */
            {{-(INT64_C(1) << 62), 1}, {1, 2}}, /* product is exactly INT64_MIN */
            {{1, INT64_C(1) << 62}, {-2, 1}},
        };
        for (unsigned k = 0; k < TIER_N(refuse); k++)
            CHECK(!rmag_div_quotas_checked(refuse[k][0], refuse[k][1], &q) && q.num == 7 &&
                      q.den == 7,
                  "checked div refuses unrepresentable case %u", k);
        CHECK(rmag_div_quotas_checked((rational_t){3, 4}, (rational_t){-3, 2}, &q) && q.num == -1 &&
                  q.den == 2,
              "checked div (3/4)/(-3/2) = -1/2 (got %lld/%lld)", (long long) q.num,
              (long long) q.den);
        CHECK(rmag_div_quotas_checked((rational_t){INT64_MAX, 1}, (rational_t){INT64_MAX, 1}, &q) &&
                  q.num == 1 && q.den == 1,
              "checked div at the int64 edge: MAX/MAX = 1");
    }
    /* MAX+1: the sum of MAX and 1 cannot be represented and must not wrap */
    {
        bool ok = false;
        rational_t g = rmag_add_quotas((rational_t){INT64_MAX, 1}, (rational_t){1, 1});
        ok = g.num == 0 && g.den == 0; /* the invalid value, never a wrapped negative */
        CHECK(ok, "F-RMAG-OVF fixed: checked quota arithmetic, no wrap");
    }
    {
        bool ok = false;
        rational_t g = rmag_mul_quotas((rational_t){INT64_C(1) << 62, 1}, (rational_t){4, 1});
        ok = g.num == 0 && g.den == 0;
        CHECK(ok, "F-RMAG-OVF fixed: checked quota arithmetic, no wrap");
    }
    /* the checked forms refuse every input or result that has no int64
     * rational: INT64_MIN fields (rational_normalize negates them), zero
     * denominators, overflowing cross products and sums, and results whose
     * numerator or denominator is INT64_MIN; out is left untouched */
    {
        const rational_t one = {1, 1}, mn = {INT64_MIN, 1}, z0 = {1, 0};
        const rational_t mxp = {INT64_MAX, 1}, nmx = {-INT64_MAX, 1};
        const rational_t bigden = {1, -(INT64_C(1) << 62)}, two = {1, 2};
        rational_t o = {77, 78};
#define UNTOUCHED (o.num == 77 && o.den == 78)
        CHECK(!rmag_add_quotas_checked(mn, one, &o) && UNTOUCHED, "checked add refuses an INT64_MIN numerator");
        CHECK(!rmag_add_quotas_checked(one, (rational_t){1, INT64_MIN}, &o) && UNTOUCHED,
              "checked add refuses an INT64_MIN denominator");
        CHECK(!rmag_add_quotas_checked(z0, one, &o) && !rmag_add_quotas_checked(one, z0, &o) && UNTOUCHED,
              "checked add refuses a zero denominator on either side");
        CHECK(!rmag_add_quotas_checked(mxp, one, &o) && UNTOUCHED, "checked add refuses MAX + 1 (sum overflow)");
        CHECK(!rmag_sub_quotas_checked(nmx, (rational_t){2, 1}, &o) && UNTOUCHED,
              "checked sub refuses -MAX - 2 (difference overflow)");
        CHECK(!rmag_sub_quotas_checked(nmx, one, &o) && UNTOUCHED,
              "checked sub refuses a result of exactly INT64_MIN");
        CHECK(!rmag_add_quotas_checked(bigden, two, &o) && UNTOUCHED,
              "checked add refuses a denominator product of exactly INT64_MIN");
        CHECK(!rmag_add_quotas_checked(one, one, 0), "checked add refuses a NULL out");
        CHECK(rmag_sub_quotas_checked(mxp, one, &o) && o.num == INT64_MAX - 1 && o.den == 1,
              "checked sub at the edge: MAX - 1");
        o = (rational_t){77, 78};
        CHECK(!rmag_mul_quotas_checked(mn, one, &o) && UNTOUCHED, "checked mul refuses an INT64_MIN field");
        CHECK(!rmag_mul_quotas_checked(one, z0, &o) && UNTOUCHED, "checked mul refuses a zero denominator");
        CHECK(!rmag_mul_quotas_checked((rational_t){-(INT64_C(1) << 62), 1}, (rational_t){2, 1}, &o) &&
                  UNTOUCHED,
              "checked mul refuses a product of exactly INT64_MIN");
        CHECK(!rmag_mul_quotas_checked((rational_t){1, -(INT64_C(1) << 62)}, (rational_t){1, 2}, &o) &&
                  UNTOUCHED,
              "checked mul refuses a denominator product of exactly INT64_MIN");
        CHECK(!rmag_mul_quotas_checked(one, one, 0), "checked mul refuses a NULL out");
        CHECK(rmag_mul_quotas_checked((rational_t){INT64_MAX, 2}, (rational_t){2, 1}, &o) &&
                  o.num == INT64_MAX && o.den == 1,
              "checked mul at the edge: MAX/2 * 2 = MAX");
        o = (rational_t){77, 78};
        CHECK(!rmag_div_quotas_checked(mn, one, &o) && !rmag_div_quotas_checked(one, mn, &o) && UNTOUCHED,
              "checked div refuses INT64_MIN fields");
        CHECK(!rmag_div_quotas_checked((rational_t){1, INT64_MIN}, one, &o) &&
                  !rmag_div_quotas_checked(one, (rational_t){1, INT64_MIN}, &o) && UNTOUCHED,
              "checked div refuses INT64_MIN denominators");
        CHECK(!rmag_add_quotas_checked(mxp, two, &o) && UNTOUCHED,
              "checked add refuses MAX/1 + 1/2 (cross product overflow)");
        CHECK(!rmag_div_quotas_checked((rational_t){0, 1}, (rational_t){1, INT64_MIN}, &o) && UNTOUCHED,
              "checked div refuses an INT64_MIN field even when the product would fit");
        CHECK(rmag_mul_quotas_checked((rational_t){2, 1}, (rational_t){INT64_MAX, 2}, &o) &&
                  o.num == INT64_MAX && o.den == 1,
              "checked mul at the edge, other order: 2 * MAX/2 = MAX");
        o = (rational_t){77, 78};
        rational_t u = rmag_sub_quotas(nmx, one);
        CHECK(u.num == 0 && u.den == 0, "unchecked sub returns {0, 0} for an INT64_MIN result");
#undef UNTOUCHED
    }
}

int main(void)
{
    tier_begin("tier1/axioms_rational", KNOWN_FAILURES, TIER_N(KNOWN_FAILURES));
    build_values();
    axiom_make_and_from_int();
    axiom_arith_grid();
    axiom_parse();
    axiom_parse_grid();
    axiom_render_bounds();
    axiom_render_exact();
    axiom_fixed();
    axiom_split();
    axiom_rmag();
    return tier_end();
}
