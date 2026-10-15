/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_qubo.c — host tests for the QUBO / Ising workbench.
 *
 * WHAT IT DOES: hand-computed tiny problems, Ising <-> QUBO energy equality
 * over every assignment, branch and bound == brute force on hundreds of
 * fixed-seed random instances, gap-certificate validity (bound <= every
 * energy), heuristics never beating the optimum, determinism, parser
 * four-valued tags, overflow refusal and every error path.
 *
 * HONEST LIMITS: random instances are synthetic and small enough for brute
 * force (n <= 18); larger-n behaviour is measured in bench_qubo.c.
 */
#include <stdio.h>
#include <string.h>
#include "qubo.h"

#define GOLD_PT_X  0xf9bee7b2cull
#define GOLD_PT_E  (-3658)
#define GOLD_PT_S  0x236267522dfd0903ull
#define GOLD_PT2_X 0x9e1ffe7f2aull
#define GOLD_PT2_E (-4741)
#define GOLD_PT2_S 0x62838ac4f4327c78ull
#define GOLD_LS_X  0x9e1ffe7f2aull
#define GOLD_LS_E  (-4741)
#define GOLD_LS_W  82ull
#define GOLD_BB_E  (-1828)
#define GOLD_BB_W  1623ull

static int g_pass, g_fail;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            g_pass++;                                                                              \
        else {                                                                                     \
            g_fail++;                                                                              \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
        }                                                                                          \
    } while (0)

static qubo_t P, P2;
static qubo_ising_t M, M2;
static qubo_ws_t WS;
static uint64_t trng = 0x1234567887654321ull;

static uint64_t tr(void)
{
    trng += 0x9E3779B97F4A7C15ull;
    uint64_t z = trng;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
static int64_t trange(int64_t lo, int64_t hi)
{
    return lo + (int64_t) (tr() % (uint64_t) (hi - lo + 1));
}

/* Slow reference energy straight from the definition. */
static int64_t ref_energy(const qubo_t *p, uint64_t x)
{
    int64_t e = p->offset;
    for (uint32_t i = 0; i < p->n; i++)
        for (uint32_t j = i; j < p->n; j++)
            if (((x >> i) & 1u) && ((x >> j) & 1u)) e += p->q[i][j];
    return e;
}
static int64_t ref_min(const qubo_t *p)
{
    int64_t m = ref_energy(p, 0);
    for (uint64_t x = 1; x < (1ull << p->n); x++) {
        int64_t e = ref_energy(p, x);
        if (e < m) m = e;
    }
    return m;
}

/* family: 0 dense +-, 1 sparse, 2 all positive off-diagonal (max-cut-like),
 * 3 all negative, 4 tiny range (many ties) */
static void rand_qubo(qubo_t *p, uint32_t n, int fam)
{
    qubo_init(p, n);
    p->offset = trange(-50, 50);
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t j = i; j < n; j++) {
            int64_t v = 0;
            switch (fam) {
            case 0:
                v = trange(-100, 100);
                break;
            case 1:
                v = (tr() % 4u == 0) ? trange(-100, 100) : 0;
                break;
            case 2:
                v = (i == j) ? trange(-60, 0) : trange(0, 20);
                break;
            case 3:
                v = trange(-30, 0);
                break;
            default:
                v = trange(-2, 2);
                break;
            }
            p->q[i][j] = v;
        }
}

static void test_basics(void)
{
    int64_t v, e;
    CHECK(qubo_init(NULL, 3) == QUBO_ERR_NULL);
    CHECK(qubo_init(&P, 0) == QUBO_ERR_RANGE);
    CHECK(qubo_init(&P, 65) == QUBO_ERR_RANGE);
    CHECK(qubo_init(&P, 64) == QUBO_OK);
    CHECK(qubo_init(&P, 3) == QUBO_OK && P.n == 3 && P.offset == 0);
    CHECK(qubo_set(NULL, 0, 0, 1) == QUBO_ERR_NULL);
    CHECK(qubo_set(&P, 3, 0, 1) == QUBO_ERR_RANGE);
    CHECK(qubo_set(&P, 0, 3, 1) == QUBO_ERR_RANGE);
    CHECK(qubo_set(&P, 2, 0, 7) == QUBO_OK && P.q[0][2] == 7 && P.q[2][0] == 0);
    CHECK(qubo_get(&P, 0, 2, &v) == QUBO_OK && v == 7);
    CHECK(qubo_get(&P, 2, 0, &v) == QUBO_OK && v == 7);
    CHECK(qubo_get(&P, 2, 3, &v) == QUBO_ERR_RANGE);
    CHECK(qubo_get(NULL, 0, 0, &v) == QUBO_ERR_NULL);
    CHECK(qubo_get(&P, 0, 0, NULL) == QUBO_ERR_NULL);
    CHECK(qubo_add(&P, 0, 2, 5) == QUBO_OK && P.q[0][2] == 12);
    CHECK(qubo_add(NULL, 0, 2, 5) == QUBO_ERR_NULL);
    CHECK(qubo_add(&P, 0, 5, 5) == QUBO_ERR_RANGE);
    CHECK(qubo_set(&P, 1, 1, INT64_MAX) == QUBO_OK);
    CHECK(qubo_add(&P, 1, 1, 1) == QUBO_ERR_OVERFLOW && P.q[1][1] == INT64_MAX);
    CHECK(qubo_add(&P, 1, 1, -1) == QUBO_OK && P.q[1][1] == INT64_MAX - 1);
    CHECK(qubo_check(&P) == QUBO_ERR_OVERFLOW);
    P.n = 0;
    CHECK(qubo_set(&P, 0, 0, 1) == QUBO_ERR_RANGE);
    CHECK(qubo_check(&P) == QUBO_ERR_RANGE);
    P.n = 65;
    CHECK(qubo_check(&P) == QUBO_ERR_RANGE);
    CHECK(qubo_check(NULL) == QUBO_ERR_NULL);

    /* abs-sum limit, exact boundary */
    qubo_init(&P, 2);
    P.q[0][0] = QUBO_MAX_ABS_SUM;
    CHECK(qubo_check(&P) == QUBO_OK);
    P.q[1][1] = -1;
    CHECK(qubo_check(&P) == QUBO_ERR_OVERFLOW);
    P.q[1][1] = 0;
    P.offset = 1;
    CHECK(qubo_check(&P) == QUBO_ERR_OVERFLOW);
    P.offset = 0;
    P.q[0][0] = INT64_MIN;
    CHECK(qubo_check(&P) == QUBO_ERR_OVERFLOW);
    P.q[0][0] = -QUBO_MAX_ABS_SUM;
    CHECK(qubo_check(&P) == QUBO_OK);
    P.q[0][0] = 0;
    P.q[1][0] = 1; /* lower triangle */
    CHECK(qubo_check(&P) == QUBO_ERR_RANGE);
    P.q[1][0] = 0;
    P.q[0][2] = 1; /* beyond n */
    CHECK(qubo_check(&P) == QUBO_ERR_RANGE);
    P.q[0][2] = 0;
    P.q[5][5] = 1;
    CHECK(qubo_check(&P) == QUBO_ERR_RANGE);
    P.q[5][5] = 0;
    CHECK(qubo_check(&P) == QUBO_OK);

    /* hand computed: E = 3 - x0 - x1 + 2 x0 x1 + 5 x2 - 4 x1 x2 */
    qubo_init(&P, 3);
    P.offset = 3;
    qubo_set(&P, 0, 0, -1);
    qubo_set(&P, 1, 1, -1);
    qubo_set(&P, 0, 1, 2);
    qubo_set(&P, 2, 2, 5);
    qubo_set(&P, 2, 1, -4);
    static const int64_t want[8] = {3, 2, 2, 3, 8, 7, 3, 4};
    for (uint64_t x = 0; x < 8; x++) CHECK(qubo_energy(&P, x, &e) == QUBO_OK && e == want[x]);
    CHECK(qubo_energy(&P, 8, &e) == QUBO_ERR_RANGE);
    CHECK(qubo_energy(NULL, 0, &e) == QUBO_ERR_NULL);
    CHECK(qubo_energy(&P, 0, NULL) == QUBO_ERR_NULL);
    int64_t lb;
    /* rows: min(0,-1+0)=-1, min(0,-1-4)=-5, min(0,5)=0 -> 3-6 = -3 */
    CHECK(qubo_root_bound(&P, &lb) == QUBO_OK && lb == -3);
    CHECK(qubo_root_bound(NULL, &lb) == QUBO_ERR_NULL);
    CHECK(qubo_root_bound(&P, NULL) == QUBO_ERR_NULL);

    qubo_result_t r;
    CHECK(qubo_brute_force(&P, 7, &WS, &r) == QUBO_OK);
    CHECK(r.energy == 2 && r.x == 1 && r.lower_bound == 2 && r.gap == 0);
    CHECK(r.cert == QUBO_CERT_OPTIMAL && r.work == 7);
    CHECK(qubo_verify(&P, &r) == QUBO_OK);
    CHECK(qubo_bnb(&P, 1000, NULL, &WS, &r) == QUBO_OK && r.energy == 2 && r.gap == 0);
    CHECK(r.cert == QUBO_CERT_OPTIMAL && r.lower_bound == 2);
    CHECK(qubo_verify(&P, &r) == QUBO_OK);

    /* n = 64 full mask energy */
    qubo_init(&P, 64);
    for (uint32_t i = 0; i < 64; i++) P.q[i][i] = 1;
    P.q[0][63] = -3;
    CHECK(qubo_energy(&P, ~0ull, &e) == QUBO_OK && e == 61);
    CHECK(qubo_energy(&P, 1ull << 63, &e) == QUBO_OK && e == 1);

    CHECK(qubo_exp_q24(0) == 16777216u);
    CHECK(qubo_exp_q24(16) == 6171993u); /* exp(-1) * 2^24 */
    CHECK(qubo_exp_q24(255) == 2u);
    CHECK(qubo_exp_q24(256) == 0u);
    uint64_t s = 0;
    CHECK(qubo_rng_next(NULL) == 0);
    uint64_t a = qubo_rng_next(&s);
    CHECK(a != 0 && s == a);
    s = 1;
    CHECK(qubo_rng_next(&s) == ((1ull ^ (1ull << 13)) ^ ((1ull ^ (1ull << 13)) >> 7) ^
                                (((1ull ^ (1ull << 13)) ^ ((1ull ^ (1ull << 13)) >> 7)) << 17)));
}

static void test_ising(void)
{
    int64_t e, e2;
    CHECK(qubo_ising_init(NULL, 2) == QUBO_ERR_NULL);
    CHECK(qubo_ising_init(&M, 0) == QUBO_ERR_RANGE);
    CHECK(qubo_ising_init(&M, 65) == QUBO_ERR_RANGE);
    /* H = 5 + s0 - 2 s1 + 3 s0 s1 */
    qubo_ising_init(&M, 2);
    M.offset = 5;
    M.h[0] = 1;
    M.h[1] = -2;
    M.j[0][1] = 3;
    /* s bits: 00 -> (-1,-1): 5-1+2+3=9; 01 -> (+1,-1): 5+1+2-3=5;
     * 10 -> (-1,+1): 5-1-2-3=-1; 11: 5+1-2+3=7 */
    static const int64_t want[4] = {9, 5, -1, 7};
    for (uint64_t s = 0; s < 4; s++) CHECK(qubo_ising_energy(&M, s, &e) == QUBO_OK && e == want[s]);
    CHECK(qubo_ising_energy(&M, 4, &e) == QUBO_ERR_RANGE);
    CHECK(qubo_ising_energy(NULL, 0, &e) == QUBO_ERR_NULL);
    CHECK(qubo_ising_energy(&M, 0, NULL) == QUBO_ERR_NULL);
    CHECK(qubo_from_ising(&M, &P) == QUBO_OK);
    /* Q00 = 2 - 6 = -4, Q11 = -4 - 6 = -10, Q01 = 12, off = 5 - (1-2) + 3 = 9 */
    CHECK(P.q[0][0] == -4 && P.q[1][1] == -10 && P.q[0][1] == 12 && P.offset == 9);
    CHECK(qubo_to_ising(&P, &M2) == QUBO_OK);
    for (uint64_t s = 0; s < 4; s++) {
        CHECK(qubo_ising_energy(&M2, s, &e) == QUBO_OK && e == 4 * want[s]);
        CHECK(M2.j[0][1] == 12);
    }
    CHECK(qubo_from_ising(NULL, &P) == QUBO_ERR_NULL);
    CHECK(qubo_from_ising(&M, NULL) == QUBO_ERR_NULL);
    CHECK(qubo_to_ising(NULL, &M2) == QUBO_ERR_NULL);
    CHECK(qubo_to_ising(&P, NULL) == QUBO_ERR_NULL);

    /* check errors */
    M.j[1][0] = 1;
    CHECK(qubo_ising_check(&M) == QUBO_ERR_RANGE);
    M.j[1][0] = 0;
    M.j[1][1] = 1;
    CHECK(qubo_ising_check(&M) == QUBO_ERR_RANGE);
    M.j[1][1] = 0;
    M.h[2] = 1;
    CHECK(qubo_ising_check(&M) == QUBO_ERR_RANGE);
    M.h[2] = 0;
    M.j[0][2] = 1;
    CHECK(qubo_ising_check(&M) == QUBO_ERR_RANGE);
    M.j[0][2] = 0;
    CHECK(qubo_ising_check(&M) == QUBO_OK);
    CHECK(qubo_ising_check(NULL) == QUBO_ERR_NULL);
    M.n = 0;
    CHECK(qubo_ising_check(&M) == QUBO_ERR_RANGE);
    M.n = 2;
    M.offset = INT64_MIN;
    CHECK(qubo_ising_check(&M) == QUBO_ERR_OVERFLOW);
    CHECK(qubo_from_ising(&M, &P) == QUBO_ERR_OVERFLOW);
    M.offset = 0;
    M.h[1] = QUBO_MAX_ABS_SUM;
    CHECK(qubo_ising_check(&M) == QUBO_ERR_OVERFLOW);
    M.h[1] = 0;
    M.j[0][1] = QUBO_MAX_ABS_SUM;
    CHECK(qubo_ising_check(&M) == QUBO_ERR_OVERFLOW);

    /* conversion overflow: 4J and 9|J| total exceed the limit */
    M.j[0][1] = QUBO_MAX_ABS_SUM / 4;
    M.h[0] = 0;
    CHECK(qubo_ising_check(&M) == QUBO_OK);
    CHECK(qubo_from_ising(&M, &P) == QUBO_ERR_OVERFLOW);
    M.j[0][1] = QUBO_MAX_ABS_SUM / 16;
    CHECK(qubo_from_ising(&M, &P) == QUBO_OK); /* 9/16 of the limit fits */
    qubo_init(&P, 2);
    P.q[0][1] = QUBO_MAX_ABS_SUM / 2;
    CHECK(qubo_to_ising(&P, &M2) == QUBO_ERR_OVERFLOW);
    P.q[0][1] = QUBO_MAX_ABS_SUM / 4;
    CHECK(qubo_to_ising(&P, &M2) == QUBO_OK); /* |J|+|h|*2+|off| = 4/4 */
    qubo_init(&P, 1);
    P.offset = QUBO_MAX_ABS_SUM / 2;
    CHECK(qubo_to_ising(&P, &M2) == QUBO_ERR_OVERFLOW);
    P.offset = 0;
    P.q[0][0] = QUBO_MAX_ABS_SUM / 2 + 1;
    CHECK(qubo_to_ising(&P, &M2) == QUBO_ERR_OVERFLOW);

    /* random round trips: every assignment, n = 1..10 */
    int ok_rt = 1;
    for (int t = 0; t < 120; t++) {
        uint32_t n = 1u + (uint32_t) (t % 10);
        qubo_ising_init(&M, n);
        M.offset = trange(-1000, 1000);
        for (uint32_t i = 0; i < n; i++) {
            M.h[i] = trange(-50, 50);
            for (uint32_t j = i + 1; j < n; j++) M.j[i][j] = trange(-50, 50);
        }
        if (qubo_from_ising(&M, &P) != QUBO_OK || qubo_to_ising(&P, &M2) != QUBO_OK) {
            ok_rt = 0;
            continue;
        }
        if (qubo_from_ising(&M2, &P2) != QUBO_OK) ok_rt = 0;
        for (uint64_t s = 0; s < (1ull << n); s++) {
            int64_t h, q, h2, q2;
            qubo_ising_energy(&M, s, &h);
            qubo_energy(&P, s, &q);
            qubo_ising_energy(&M2, s, &h2);
            qubo_energy(&P2, s, &q2);
            if (h != q || h2 != 4 * h || q2 != 4 * q) ok_rt = 0;
        }
    }
    CHECK(ok_rt);
    ok_rt = 1;
    for (int t = 0; t < 120; t++) {
        uint32_t n = 1u + (uint32_t) (t % 10);
        rand_qubo(&P, n, t % 5);
        if (qubo_to_ising(&P, &M) != QUBO_OK) {
            ok_rt = 0;
            continue;
        }
        for (uint64_t s = 0; s < (1ull << n); s++) {
            qubo_energy(&P, s, &e);
            qubo_ising_energy(&M, s, &e2);
            if (e2 != 4 * e) ok_rt = 0;
        }
    }
    CHECK(ok_rt);
}

static qubo_status_t parse(const char *s, qubo_merge_t m, qubo_parse_report_t *r)
{
    return qubo_parse(s, (uint32_t) strlen(s), m, &P, r);
}

static void test_parser(void)
{
    qubo_parse_report_t r;
    const char *good = "# a comment\n"
                       "n 3   # three vars\n"
                       "\n"
                       "offset -7\n"
                       "0 0 -1\r\n"
                       "\t1 0   +2\n"
                       "2 2 5\n"
                       "1 2 -4"; /* no trailing newline */
    CHECK(parse(good, QUBO_MERGE_REFUSE, &r) == QUBO_OK);
    CHECK(r.tag == QUBO_LP_TRUE && r.line == 0 && r.entries == 4 && r.duplicates == 0);
    CHECK(P.n == 3 && P.offset == -7 && P.q[0][0] == -1 && P.q[0][1] == 2 && P.q[2][2] == 5);
    CHECK(P.q[1][2] == -4 && P.q[1][0] == 0 && P.q[1][1] == 0);

    /* agreeing duplicate (also via the swapped pair): TRUE, kept once */
    CHECK(parse("n 2\n0 1 3\n1 0 3\n", QUBO_MERGE_REFUSE, &r) == QUBO_OK);
    CHECK(r.tag == QUBO_LP_TRUE && r.duplicates == 1 && r.entries == 2 && P.q[0][1] == 3);
    /* conflicting duplicate: BOTH, refused, line reported */
    CHECK(parse("n 2\n0 1 3\n\n1 0 4\n", QUBO_MERGE_REFUSE, &r) == QUBO_ERR_CONFLICT);
    CHECK(r.tag == QUBO_LP_BOTH && r.line == 4);
    /* caller picks SUM: accepted and added */
    CHECK(parse("n 2\n0 1 3\n1 0 4\n0 1 -1\n", QUBO_MERGE_SUM, &r) == QUBO_OK);
    CHECK(r.tag == QUBO_LP_TRUE && P.q[0][1] == 6 && r.duplicates == 2 && r.entries == 3);
    CHECK(parse("n 2\noffset 1\noffset 1\n", QUBO_MERGE_REFUSE, &r) == QUBO_OK);
    CHECK(P.offset == 1 && r.duplicates == 1);
    CHECK(parse("n 2\noffset 1\noffset 2\n", QUBO_MERGE_REFUSE, &r) == QUBO_ERR_CONFLICT);
    CHECK(r.tag == QUBO_LP_BOTH && r.line == 3);
    CHECK(parse("n 2\noffset 1\noffset 2\n", QUBO_MERGE_SUM, &r) == QUBO_OK && P.offset == 3);
    CHECK(parse("n 2\nn 2\n0 0 1\n", QUBO_MERGE_REFUSE, &r) == QUBO_OK && P.q[0][0] == 1);
    CHECK(parse("n 2\nn 3\n", QUBO_MERGE_SUM, &r) == QUBO_ERR_CONFLICT && r.tag == QUBO_LP_BOTH);
    CHECK(r.line == 2);
    /* SUM overflow */
    CHECK(parse("n 1\n0 0 9223372036854775807\n0 0 1\n", QUBO_MERGE_SUM, &r) == QUBO_ERR_OVERFLOW);
    CHECK(r.tag == QUBO_LP_FALSE && r.line == 3);
    CHECK(parse("n 1\noffset 9223372036854775807\noffset 1\n", QUBO_MERGE_SUM, &r) ==
          QUBO_ERR_OVERFLOW);

    /* missing size: NEITHER */
    CHECK(parse("", QUBO_MERGE_REFUSE, &r) == QUBO_ERR_NO_SIZE && r.tag == QUBO_LP_NEITHER);
    CHECK(r.line == 0);
    CHECK(parse("# only comments\n\n", QUBO_MERGE_REFUSE, &r) == QUBO_ERR_NO_SIZE);
    CHECK(parse("0 0 1\nn 1\n", QUBO_MERGE_REFUSE, &r) == QUBO_ERR_NO_SIZE);
    CHECK(r.tag == QUBO_LP_NEITHER && r.line == 1);
    CHECK(parse("offset 3\nn 1\n", QUBO_MERGE_REFUSE, &r) == QUBO_ERR_NO_SIZE && r.line == 1);

    /* malformed: FALSE */
    static const struct {
        const char *s;
        qubo_status_t st;
        uint32_t line;
    } bad[] = {
        {"n 0\n", QUBO_ERR_RANGE, 1},
        {"n 65\n", QUBO_ERR_RANGE, 1},
        {"n -1\n", QUBO_ERR_RANGE, 1},
        {"n 99999999999999999999\n", QUBO_ERR_RANGE, 1},
        {"n x\n", QUBO_ERR_SYNTAX, 1},
        {"n\n", QUBO_ERR_SYNTAX, 1},
        {"n 2 3\n", QUBO_ERR_SYNTAX, 1},
        {"n 2\noffset\n", QUBO_ERR_SYNTAX, 2},
        {"n 2\noffset 1 2\n", QUBO_ERR_SYNTAX, 2},
        {"n 2\noffset z\n", QUBO_ERR_SYNTAX, 2},
        {"n 2\noffset 9223372036854775808\n", QUBO_ERR_OVERFLOW, 2},
        {"n 2\n0 1\n", QUBO_ERR_SYNTAX, 2},
        {"n 2\n0 1 2 3\n", QUBO_ERR_SYNTAX, 2},
        {"n 2\n0 1 2 3 4\n", QUBO_ERR_SYNTAX, 2},
        {"n 2\n0 1 x\n", QUBO_ERR_SYNTAX, 2},
        {"n 2\n0 1 2x\n", QUBO_ERR_SYNTAX, 2},
        {"n 2\n0 1 -\n", QUBO_ERR_SYNTAX, 2},
        {"n 2\n0 1 +\n", QUBO_ERR_SYNTAX, 2},
        {"n 2\na 1 2\n", QUBO_ERR_SYNTAX, 2},
        {"n 2\n0 b 2\n", QUBO_ERR_SYNTAX, 2},
        {"n 2\n0 2 1\n", QUBO_ERR_RANGE, 2},
        {"n 2\n2 0 1\n", QUBO_ERR_RANGE, 2},
        {"n 2\n-1 0 1\n", QUBO_ERR_RANGE, 2},
        {"n 2\n0 -1 1\n", QUBO_ERR_RANGE, 2},
        {"n 2\n99999999999999999999 0 1\n", QUBO_ERR_RANGE, 2},
        {"n 2\n0 99999999999999999999 1\n", QUBO_ERR_RANGE, 2},
        {"n 2\n0 0 9223372036854775808\n", QUBO_ERR_OVERFLOW, 2},
        {"n 2\n0 0 -9223372036854775809\n", QUBO_ERR_OVERFLOW, 2},
        {"n 2\nfoo 1\n", QUBO_ERR_SYNTAX, 2},
        {"n 2\nnn 1\n", QUBO_ERR_SYNTAX, 2},
        {"n 2\noffsetx 1\n", QUBO_ERR_SYNTAX, 2},
        {"n 2\noff 1\n", QUBO_ERR_SYNTAX, 2},
        {"n 2\n0\n", QUBO_ERR_SYNTAX, 2},
        /* parses, then the abs-sum check refuses (line 0) */
        {"n 2\n0 0 -9223372036854775808\n", QUBO_ERR_OVERFLOW, 0},
        {"n 2\n0 0 1152921504606846976\n1 1 1\n", QUBO_ERR_OVERFLOW, 0},
    };
    for (unsigned k = 0; k < sizeof bad / sizeof bad[0]; k++) {
        qubo_status_t st = parse(bad[k].s, QUBO_MERGE_REFUSE, &r);
        if (st != bad[k].st || r.tag != QUBO_LP_FALSE || r.line != bad[k].line)
            printf("parser case %u: st=%d tag=%d line=%u\n", k, (int) st, (int) r.tag, r.line);
        CHECK(st == bad[k].st && r.tag == QUBO_LP_FALSE && r.line == bad[k].line);
    }
    /* boundary values accepted */
    CHECK(parse("n 64\n63 0 -9223372036854775807\n", QUBO_MERGE_REFUSE, &r) == QUBO_ERR_OVERFLOW);
    CHECK(parse("n 64\n63 0 -1152921504606846976\n", QUBO_MERGE_REFUSE, &r) == QUBO_OK);
    CHECK(P.n == 64 && P.q[0][63] == -1152921504606846976ll);
    CHECK(parse("n 1\n0 0 9223372036854775807\n", QUBO_MERGE_SUM, &r) == QUBO_ERR_OVERFLOW);
    /* length bounds the text: the second line is ignored */
    CHECK(qubo_parse("n 1\nn 2\n", 4, QUBO_MERGE_REFUSE, &P, &r) == QUBO_OK && P.n == 1);
    CHECK(qubo_parse("n 1\0zz", 6, QUBO_MERGE_REFUSE, &P, &r) == QUBO_ERR_SYNTAX);
    /* NULL / param */
    CHECK(qubo_parse("n 1", 3, QUBO_MERGE_REFUSE, &P, NULL) == QUBO_ERR_NULL);
    CHECK(qubo_parse(NULL, 3, QUBO_MERGE_REFUSE, &P, &r) == QUBO_ERR_NULL);
    CHECK(qubo_parse("n 1", 3, QUBO_MERGE_REFUSE, NULL, &r) == QUBO_ERR_NULL);
    CHECK(qubo_parse("n 1", 3, (qubo_merge_t) 2, &P, &r) == QUBO_ERR_PARAM);
    CHECK(r.tag == QUBO_LP_FALSE);
}

static void test_exact(void)
{
    qubo_result_t r, b;
    uint64_t warm;
    /* error paths */
    qubo_init(&P, 3);
    CHECK(qubo_brute_force(NULL, 10, &WS, &r) == QUBO_ERR_NULL);
    CHECK(qubo_brute_force(&P, 10, NULL, &r) == QUBO_ERR_NULL);
    CHECK(qubo_brute_force(&P, 10, &WS, NULL) == QUBO_ERR_NULL);
    CHECK(qubo_brute_force(&P, 6, &WS, &r) == QUBO_ERR_BUDGET);
    CHECK(qubo_brute_force(&P, 7, &WS, &r) == QUBO_OK && r.energy == 0 && r.x == 0);
    CHECK(qubo_bnb(NULL, 10, NULL, &WS, &r) == QUBO_ERR_NULL);
    CHECK(qubo_bnb(&P, 10, NULL, NULL, &r) == QUBO_ERR_NULL);
    CHECK(qubo_bnb(&P, 10, NULL, &WS, NULL) == QUBO_ERR_NULL);
    CHECK(qubo_bnb(&P, 0, NULL, &WS, &r) == QUBO_ERR_PARAM);
    warm = 8;
    CHECK(qubo_bnb(&P, 10, &warm, &WS, &r) == QUBO_ERR_RANGE);
    P.q[2][0] = 1;
    CHECK(qubo_brute_force(&P, 10, &WS, &r) == QUBO_ERR_RANGE);
    CHECK(qubo_bnb(&P, 10, NULL, &WS, &r) == QUBO_ERR_RANGE);
    P.q[2][0] = 0;
    P.q[0][0] = INT64_MIN;
    CHECK(qubo_brute_force(&P, 10, &WS, &r) == QUBO_ERR_OVERFLOW);
    CHECK(qubo_bnb(&P, 10, NULL, &WS, &r) == QUBO_ERR_OVERFLOW);
    qubo_init(&P, 33);
    CHECK(qubo_brute_force(&P, ~0ull, &WS, &r) == QUBO_ERR_RANGE);

    /* single variable */
    qubo_init(&P, 1);
    P.q[0][0] = -5;
    P.offset = 2;
    CHECK(qubo_bnb(&P, 5, NULL, &WS, &r) == QUBO_OK && r.energy == -3 && r.x == 1);
    CHECK(r.cert == QUBO_CERT_OPTIMAL);
    P.q[0][0] = 5;
    CHECK(qubo_bnb(&P, 5, NULL, &WS, &r) == QUBO_OK && r.energy == 2 && r.x == 0);

    /* frustrated triangle (no persistency applies): needs real branching */
    qubo_init(&P, 3);
    for (uint32_t i = 0; i < 3; i++) {
        P.q[i][i] = -2;
        for (uint32_t j = i + 1; j < 3; j++) P.q[i][j] = 3;
    }
    CHECK(qubo_bnb(&P, 100, NULL, &WS, &r) == QUBO_OK && r.energy == -2);
    CHECK(r.cert == QUBO_CERT_OPTIMAL && r.work > 1);
    CHECK(qubo_verify(&P, &r) == QUBO_OK);

    /* B&B == brute force == slow reference on many random instances */
    int agree = 1, cnt = 0, warm_agree = 1;
    for (int t = 0; t < 600; t++) {
        uint32_t n = 1u + (uint32_t) (t % 14);
        rand_qubo(&P, n, t % 5);
        if (qubo_brute_force(&P, 1ull << 20, &WS, &b) != QUBO_OK) agree = 0;
        if (qubo_bnb(&P, 1ull << 30, NULL, &WS, &r) != QUBO_OK) agree = 0;
        int64_t m = ref_min(&P);
        if (b.energy != m || r.energy != m || ref_energy(&P, r.x) != m) agree = 0;
        if (ref_energy(&P, b.x) != m || r.cert != QUBO_CERT_OPTIMAL || r.lower_bound != m)
            agree = 0;
        if (qubo_verify(&P, &r) != QUBO_OK || qubo_verify(&P, &b) != QUBO_OK) agree = 0;
        int64_t lb;
        qubo_root_bound(&P, &lb);
        if (lb > m) agree = 0;
        warm = tr() & ((1ull << n) - 1u);
        if (qubo_bnb(&P, 1ull << 30, &warm, &WS, &r) != QUBO_OK || r.energy != m) warm_agree = 0;
        cnt++;
    }
    CHECK(agree);
    CHECK(warm_agree);
    CHECK(cnt == 600);

    /* larger n vs brute force (Gray code) */
    agree = 1;
    for (int t = 0; t < 40; t++) {
        uint32_t n = 15u + (uint32_t) (t % 4);
        rand_qubo(&P, n, t % 5);
        qubo_brute_force(&P, 1ull << 20, &WS, &b);
        qubo_bnb(&P, 1ull << 30, NULL, &WS, &r);
        if (b.energy != r.energy || ref_energy(&P, b.x) != b.energy) agree = 0;
    }
    CHECK(agree);

    /* warm start that is already optimal is kept when nothing beats it */
    rand_qubo(&P, 10, 0);
    qubo_brute_force(&P, 1ull << 20, &WS, &b);
    warm = b.x;
    CHECK(qubo_bnb(&P, 1, &warm, &WS, &r) == QUBO_OK && r.energy == b.energy);

    /* gap certificates: tiny node budgets; bound <= every energy */
    int valid = 1, saw_gap = 0, saw_better = 0;
    for (int t = 0; t < 200; t++) {
        uint32_t n = 10u + (uint32_t) (t % 9);
        rand_qubo(&P, n, t % 5);
        qubo_brute_force(&P, 1ull << 20, &WS, &b); /* b.energy = true minimum */
        uint64_t budget = 1u + (uint64_t) (t % 23);
        if (qubo_bnb(&P, budget, NULL, &WS, &r) != QUBO_OK) valid = 0;
        if (r.lower_bound > b.energy || r.energy < b.energy) valid = 0;
        if (r.work > budget) valid = 0;
        if (qubo_verify(&P, &r) != QUBO_OK) valid = 0;
        if (r.cert == QUBO_CERT_OPTIMAL && r.energy != b.energy) valid = 0;
        if (r.cert == QUBO_CERT_GAP) saw_gap = 1;
        int64_t lb;
        qubo_root_bound(&P, &lb);
        if (r.lower_bound < lb) valid = 0;
        if (r.cert == QUBO_CERT_GAP && r.lower_bound > lb) saw_better++;
    }
    CHECK(valid);
    CHECK(saw_gap);
    CHECK(saw_better > 0); /* the B&B certificate beats the root bound */

    /* planted frustration-free instance, n = 40: optimum 0 proven */
    qubo_init(&P, 40);
    uint64_t plant = 0;
    for (uint32_t i = 0; i < 40; i++)
        if (tr() & 1u) plant |= 1ull << i;
    for (uint32_t i = 0; i < 40; i++)
        for (int d = 0; d < 3; d++) {
            uint32_t j = (uint32_t) (tr() % 40u);
            int64_t w = trange(1, 9);
            if (j == i) continue;
            int same = ((plant >> i) & 1u) == ((plant >> j) & 1u);
            if (same) { /* w (xi - xj)^2 = w xi + w xj - 2w xi xj */
                qubo_add(&P, i, i, w);
                qubo_add(&P, j, j, w);
                qubo_add(&P, i, j, -2 * w);
            } else { /* w (1 - xi - xj + 2 xi xj) */
                P.offset += w;
                qubo_add(&P, i, i, -w);
                qubo_add(&P, j, j, -w);
                qubo_add(&P, i, j, 2 * w);
            }
        }
    P.offset += 5; /* w (x0 - plant0)^2 pins the global flip */
    if ((plant >> 0) & 1u) {
        P.offset += 0;
        qubo_add(&P, 0, 0, -5);
    } else {
        P.offset -= 5;
        qubo_add(&P, 0, 0, 5);
    }
    int64_t ep;
    CHECK(qubo_energy(&P, plant, &ep) == QUBO_OK && ep == 0);
    CHECK(qubo_bnb(&P, 1ull << 22, NULL, &WS, &r) == QUBO_OK);
    CHECK(r.energy == 0 && r.cert == QUBO_CERT_OPTIMAL && r.lower_bound == 0);
}

static int local_min(const qubo_t *p, uint64_t x)
{
    int64_t e = ref_energy(p, x);
    for (uint32_t i = 0; i < p->n; i++)
        if (ref_energy(p, x ^ (1ull << i)) < e) return 0;
    return 1;
}

static void test_heuristics(void)
{
    qubo_result_t r, r2, b;
    qubo_anneal_params_t ap = {8, 200, 0};
    qubo_ls_params_t lp = {20, 1u << 20};
    uint64_t s = 42, s2;
    qubo_init(&P, 4);
    CHECK(qubo_anneal(NULL, &ap, &s, &WS, &r) == QUBO_ERR_NULL);
    CHECK(qubo_anneal(&P, NULL, &s, &WS, &r) == QUBO_ERR_NULL);
    CHECK(qubo_anneal(&P, &ap, NULL, &WS, &r) == QUBO_ERR_NULL);
    CHECK(qubo_anneal(&P, &ap, &s, NULL, &r) == QUBO_ERR_NULL);
    CHECK(qubo_anneal(&P, &ap, &s, &WS, NULL) == QUBO_ERR_NULL);
    qubo_anneal_params_t bad = ap;
    bad.replicas = 0;
    CHECK(qubo_anneal(&P, &bad, &s, &WS, &r) == QUBO_ERR_PARAM);
    bad.replicas = QUBO_MAX_REPLICAS + 1u;
    CHECK(qubo_anneal(&P, &bad, &s, &WS, &r) == QUBO_ERR_PARAM);
    bad.replicas = QUBO_MAX_REPLICAS;
    CHECK(qubo_anneal(&P, &bad, &s, &WS, &r) == QUBO_OK);
    bad = ap;
    bad.sweeps = 0;
    CHECK(qubo_anneal(&P, &bad, &s, &WS, &r) == QUBO_ERR_PARAM);
    bad = ap;
    bad.t_max_q8 = -1;
    CHECK(qubo_anneal(&P, &bad, &s, &WS, &r) == QUBO_ERR_PARAM);
    bad.t_max_q8 = QUBO_MAX_TEMP + 1;
    CHECK(qubo_anneal(&P, &bad, &s, &WS, &r) == QUBO_ERR_PARAM);
    bad.t_max_q8 = QUBO_MAX_TEMP;
    CHECK(qubo_anneal(&P, &bad, &s, &WS, &r) == QUBO_OK);
    qubo_init(&P, 64);
    bad = ap;
    bad.replicas = 16;
    bad.sweeps = 0xFFFFFFFFu; /* 2^32 * 1024 > 2^40 moves */
    CHECK(qubo_anneal(&P, &bad, &s, &WS, &r) == QUBO_ERR_PARAM);
    bad.sweeps = (uint32_t) (QUBO_MAX_MOVES / 1024u) + 1u;
    CHECK(qubo_anneal(&P, &bad, &s, &WS, &r) == QUBO_ERR_PARAM);
    qubo_init(&P, 4);
    s2 = 0;
    CHECK(qubo_anneal(&P, &ap, &s2, &WS, &r) == QUBO_ERR_PARAM);
    CHECK(qubo_local_search(&P, &lp, &s2, &WS, &r) == QUBO_ERR_PARAM);
    P.q[1][0] = 1;
    CHECK(qubo_anneal(&P, &ap, &s, &WS, &r) == QUBO_ERR_RANGE);
    CHECK(qubo_local_search(&P, &lp, &s, &WS, &r) == QUBO_ERR_RANGE);
    P.q[1][0] = 0;
    CHECK(qubo_local_search(NULL, &lp, &s, &WS, &r) == QUBO_ERR_NULL);
    CHECK(qubo_local_search(&P, NULL, &s, &WS, &r) == QUBO_ERR_NULL);
    CHECK(qubo_local_search(&P, &lp, NULL, &WS, &r) == QUBO_ERR_NULL);
    CHECK(qubo_local_search(&P, &lp, &s, NULL, &r) == QUBO_ERR_NULL);
    CHECK(qubo_local_search(&P, &lp, &s, &WS, NULL) == QUBO_ERR_NULL);
    qubo_ls_params_t lb = lp;
    lb.restarts = 0;
    CHECK(qubo_local_search(&P, &lb, &s, &WS, &r) == QUBO_ERR_PARAM);
    lb = lp;
    lb.max_flips = 0;
    CHECK(qubo_local_search(&P, &lb, &s, &WS, &r) == QUBO_ERR_PARAM);

    /* flip budget is respected exactly */
    qubo_init(&P, 8);
    for (uint32_t i = 0; i < 8; i++) P.q[i][i] = -1; /* optimum: all ones */
    lb = lp;
    lb.max_flips = 1;
    s = 7;
    CHECK(qubo_local_search(&P, &lb, &s, &WS, &r) == QUBO_OK && r.work == 1);
    CHECK(qubo_verify(&P, &r) == QUBO_OK);
    s = 7;
    CHECK(qubo_local_search(&P, &lp, &s, &WS, &r) == QUBO_OK && r.energy == -8);
    CHECK(r.x == 0xFF && r.cert == QUBO_CERT_OPTIMAL && r.lower_bound == -8);
    s = 7;
    CHECK(qubo_anneal(&P, &ap, &s, &WS, &r) == QUBO_OK && r.energy == -8);
    CHECK(r.cert == QUBO_CERT_OPTIMAL && r.work == 8u * 200u * 8u);

    /* never better than the optimum; certificate fields consistent;
     * local search ends in a 1-flip local minimum; PT mostly finds opt */
    int never_better = 1, lsmin = 1, pt_hits = 0, ls_hits = 0, okv = 1, tot = 0;
    for (int t = 0; t < 150; t++) {
        uint32_t n = 4u + (uint32_t) (t % 11);
        rand_qubo(&P, n, t % 5);
        qubo_brute_force(&P, 1ull << 20, &WS, &b);
        s = 1000u + (uint64_t) t;
        qubo_anneal(&P, &ap, &s, &WS, &r);
        if (r.energy < b.energy || qubo_verify(&P, &r) != QUBO_OK) never_better = 0;
        if (r.energy == b.energy) pt_hits++;
        if (r.cert == QUBO_CERT_OPTIMAL && r.energy != b.energy) okv = 0;
        s = 2000u + (uint64_t) t;
        qubo_local_search(&P, &lp, &s, &WS, &r2);
        if (r2.energy < b.energy || qubo_verify(&P, &r2) != QUBO_OK) never_better = 0;
        if (!local_min(&P, r2.x)) lsmin = 0;
        if (r2.energy == b.energy) ls_hits++;
        if (r2.cert == QUBO_CERT_HEURISTIC_ONLY && r2.lower_bound > b.energy) okv = 0;
        tot++;
    }
    CHECK(never_better);
    CHECK(lsmin);
    CHECK(okv);
    CHECK(pt_hits == tot); /* small n, 200 sweeps x 8 replicas: always found */
    CHECK(ls_hits >= tot * 9 / 10);
    printf("heuristic hit rate on n<=14: PT %d/%d, LS %d/%d\n", pt_hits, tot, ls_hits, tot);

    /* determinism: same seed, same everything; rng advanced identically */
    rand_qubo(&P, 30, 0);
    s = 99;
    s2 = 99;
    qubo_anneal(&P, &ap, &s, &WS, &r);
    qubo_anneal(&P, &ap, &s2, &WS, &r2);
    CHECK(r.x == r2.x && r.energy == r2.energy && r.work == r2.work && s == s2 && s != 99);
    s = 99;
    s2 = 99;
    qubo_local_search(&P, &lp, &s, &WS, &r);
    qubo_local_search(&P, &lp, &s2, &WS, &r2);
    CHECK(r.x == r2.x && r.energy == r2.energy && r.work == r2.work && s == s2);
    qubo_bnb(&P, 5000, NULL, &WS, &r);
    qubo_bnb(&P, 5000, NULL, &WS, &r2);
    CHECK(r.x == r2.x && r.energy == r2.energy && r.lower_bound == r2.lower_bound);
    CHECK(r.work == r2.work);
    /* a single sweep with one cold replica at T = 1 is greedy-ish but valid */
    qubo_anneal_params_t one = {1, 1, 1};
    s = 5;
    CHECK(qubo_anneal(&P, &one, &s, &WS, &r) == QUBO_OK && qubo_verify(&P, &r) == QUBO_OK);
    CHECK(r.work == 30);
}

static void test_exp_index(void)
{
    CHECK(qubo_exp_index(0, 5) == QUBO_EXP_TABLE_N);
    CHECK(qubo_exp_index(-3, 5) == QUBO_EXP_TABLE_N);
    CHECK(qubo_exp_index(QUBO_MAX_TEMP + 1, 5) == QUBO_EXP_TABLE_N);
    CHECK(qubo_exp_index(2560, -1) == QUBO_EXP_TABLE_N);
    CHECK(qubo_exp_index(256, 0) == 0); /* T = 1 */
    CHECK(qubo_exp_index(256, 1) == 16);
    CHECK(qubo_exp_index(256, 15) == 240);
    CHECK(qubo_exp_index(256, 16) == QUBO_EXP_TABLE_N);
    CHECK(qubo_exp_index(4096, 1) == 1); /* T = 16 */
    CHECK(qubo_exp_index(4097, 1) == 0);
    CHECK(qubo_exp_index(128, 1) == 32); /* T = 1/2 */
    CHECK(qubo_exp_index(1, 0) == 0);
    CHECK(qubo_exp_index(17, 1) == 240);
    CHECK(qubo_exp_index(16, 1) == QUBO_EXP_TABLE_N);
    CHECK(qubo_exp_index(25600, 255) == 40); /* T = 100 */
    CHECK(qubo_exp_index(25600, 1599) == 255);
    CHECK(qubo_exp_index(25600, 1600) == QUBO_EXP_TABLE_N);
    CHECK(qubo_exp_index(QUBO_MAX_TEMP, QUBO_MAX_TEMP >> 8) == 16);
    CHECK(qubo_exp_index(QUBO_MAX_TEMP, (QUBO_MAX_TEMP >> 4) - 1) == 255);
    CHECK(qubo_exp_index(QUBO_MAX_TEMP, QUBO_MAX_TEMP >> 4) == QUBO_EXP_TABLE_N);
    CHECK(qubo_exp_index(QUBO_MAX_TEMP, INT64_MAX) == QUBO_EXP_TABLE_N);
    int ok = 1;
    for (int k = 0; k < 20000; k++) {
        int64_t t = 1 + (int64_t) (tr() % (k % 2 ? 1000u : (uint64_t) QUBO_MAX_TEMP));
        int64_t d = (int64_t) (tr() % (uint64_t) (18 * t / 256 + 2));
        uint64_t want = ((uint64_t) d * 4096u) / (uint64_t) t;
        if (want > QUBO_EXP_TABLE_N) want = QUBO_EXP_TABLE_N;
        if (qubo_exp_index(t, d) != (uint32_t) want) ok = 0;
    }
    CHECK(ok);
}

/* Golden regression: exact trajectories for fixed seeds. Any change to the
 * move order, acceptance test, ladder, exchange rule or branching order
 * changes these numbers; update them only for a deliberate change. */
static void test_golden(void)
{
    qubo_result_t r;
    qubo_anneal_params_t ap = {8, 4, 0};
    qubo_ls_params_t lp = {5, 1u << 20};
    uint64_t s;
    trng = 0xC0FFEEull;
    rand_qubo(&P, 40, 0);
    s = 77;
    qubo_anneal(&P, &ap, &s, &WS, &r);
    printf("golden anneal: x=%llx e=%lld s=%llx\n", (unsigned long long) r.x, (long long) r.energy,
           (unsigned long long) s);
    CHECK(r.x == GOLD_PT_X && r.energy == GOLD_PT_E && s == GOLD_PT_S);
    ap.t_max_q8 = 50 * 256;
    s = 77;
    qubo_anneal(&P, &ap, &s, &WS, &r);
    printf("golden anneal t50: x=%llx e=%lld s=%llx\n", (unsigned long long) r.x,
           (long long) r.energy, (unsigned long long) s);
    CHECK(r.x == GOLD_PT2_X && r.energy == GOLD_PT2_E && s == GOLD_PT2_S);
    s = 78;
    qubo_local_search(&P, &lp, &s, &WS, &r);
    printf("golden ls: x=%llx e=%lld w=%llu\n", (unsigned long long) r.x, (long long) r.energy,
           (unsigned long long) r.work);
    CHECK(r.x == GOLD_LS_X && r.energy == GOLD_LS_E && r.work == GOLD_LS_W);
    rand_qubo(&P, 22, 0);
    qubo_bnb(&P, 1ull << 30, NULL, &WS, &r);
    printf("golden bnb: e=%lld nodes=%llu\n", (long long) r.energy, (unsigned long long) r.work);
    CHECK(r.energy == GOLD_BB_E && r.work == GOLD_BB_W);
}

static void test_verify(void)
{
    qubo_result_t r, t;
    rand_qubo(&P, 8, 0);
    qubo_brute_force(&P, 1u << 10, &WS, &r);
    CHECK(qubo_verify(&P, &r) == QUBO_OK);
    CHECK(qubo_verify(NULL, &r) == QUBO_ERR_NULL);
    CHECK(qubo_verify(&P, NULL) == QUBO_ERR_NULL);
    t = r;
    t.energy--;
    CHECK(qubo_verify(&P, &t) == QUBO_ERR_INVALID);
    t = r;
    t.x = 1ull << 9;
    CHECK(qubo_verify(&P, &t) == QUBO_ERR_RANGE);
    t = r;
    t.lower_bound++;
    t.gap = -1;
    CHECK(qubo_verify(&P, &t) == QUBO_ERR_INVALID);
    t = r;
    t.lower_bound--;
    CHECK(qubo_verify(&P, &t) == QUBO_ERR_INVALID); /* gap not updated */
    t.gap = 1;
    CHECK(qubo_verify(&P, &t) == QUBO_ERR_INVALID); /* OPTIMAL with gap */
    t.cert = QUBO_CERT_GAP;
    CHECK(qubo_verify(&P, &t) == QUBO_OK);
    t.cert = QUBO_CERT_HEURISTIC_ONLY;
    CHECK(qubo_verify(&P, &t) == QUBO_OK);
    t = r;
    t.cert = QUBO_CERT_GAP;
    CHECK(qubo_verify(&P, &t) == QUBO_ERR_INVALID); /* GAP with gap 0 */
    t.cert = QUBO_CERT_HEURISTIC_ONLY;
    CHECK(qubo_verify(&P, &t) == QUBO_ERR_INVALID);
    t.cert = (qubo_cert_t) 7;
    CHECK(qubo_verify(&P, &t) == QUBO_ERR_INVALID);
}

int main(void)
{
    test_basics();
    test_ising();
    test_parser();
    test_exact();
    test_heuristics();
    test_verify();
    test_exp_index();
    test_golden();
    printf("%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
