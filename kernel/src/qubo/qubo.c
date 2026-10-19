/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* qubo.c — classical QUBO / Ising workbench with optimality certificates.
 *
 * WHAT IT DOES: see qubo.h. Model checks, Ising <-> QUBO conversion, a
 * strict four-valued text parser, a cheap proven lower bound, and four
 * solvers (Gray-code brute force, branch and bound, parallel tempering,
 * steepest-descent local search), each reporting energy, proven bound, gap
 * and certificate status.
 *
 * HONEST LIMITS
 *   - Classical heuristics and classical exact search; nothing is quantum.
 *   - The branch-and-bound bound is simple; dense problems past a few dozen
 *     variables end with a GAP certificate, not a proof.
 *   - qubo_check caps sum |coefficients| + |offset| at 2^60 so every
 *     internal sum is a sum of distinct terms and cannot overflow; this is
 *     why most inner loops use plain int64 arithmetic after the check.
 *   - Integer only, no allocation, no libc, no 64-bit division (so i386
 *     needs no libgcc helpers).
 */
#include "qubo.h"

static const uint32_t qubo_exp_tab[QUBO_EXP_TABLE_N] = {
    16777216u, 15760736u, 14805841u, 13908801u, 13066109u, 12274473u, 11530801u, 10832185u,
    10175896u, 9559370u,  8980197u,  8436114u,  7924996u,  7444844u,  6993784u,  6570052u,
    6171993u,  5798051u,  5446765u,  5116762u,  4806753u,  4515526u,  4241945u,  3984938u,
    3743503u,  3516696u,  3303630u,  3103473u,  2915443u,  2738805u,  2572869u,  2416987u,
    2270549u,  2132984u,  2003753u,  1882351u,  1768306u,  1661169u,  1560524u,  1465977u,
    1377158u,  1293720u,  1215337u,  1141704u,  1072532u,  1007550u,  946506u,   889160u,
    835288u,   784681u,   737139u,   692478u,   650523u,   611110u,   574085u,   539303u,
    506628u,   475933u,   447098u,   420009u,   394562u,   370657u,   348200u,   327104u,
    307285u,   288668u,   271178u,   254749u,   239314u,   224815u,   211194u,   198398u,
    186378u,   175086u,   164478u,   154513u,   145151u,   136357u,   128096u,   120335u,
    113044u,   106195u,   99761u,    93717u,    88039u,    82705u,    77694u,    72987u,
    68565u,    64411u,    60508u,    56842u,    53398u,    50163u,    47124u,    44269u,
    41587u,    39067u,    36700u,    34476u,    32388u,    30425u,    28582u,    26850u,
    25224u,    23695u,    22260u,    20911u,    19644u,    18454u,    17336u,    16286u,
    15299u,    14372u,    13501u,    12683u,    11915u,    11193u,    10515u,    9878u,
    9279u,     8717u,     8189u,     7693u,     7227u,     6789u,     6378u,     5991u,
    5628u,     5287u,     4967u,     4666u,     4383u,     4118u,     3868u,     3634u,
    3414u,     3207u,     3013u,     2830u,     2659u,     2497u,     2346u,     2204u,
    2070u,     1945u,     1827u,     1716u,     1612u,     1515u,     1423u,     1337u,
    1256u,     1180u,     1108u,     1041u,     978u,      919u,      863u,      811u,
    762u,      716u,      672u,      631u,      593u,      557u,      523u,      492u,
    462u,      434u,      408u,      383u,      360u,      338u,      318u,      298u,
    280u,      263u,      247u,      232u,      218u,      205u,      193u,      181u,
    170u,      160u,      150u,      141u,      132u,      124u,      117u,      110u,
    103u,      97u,       91u,       85u,       80u,       75u,       71u,       67u,
    63u,       59u,       55u,       52u,       49u,       46u,       43u,       40u,
    38u,       36u,       33u,       31u,       30u,       28u,       26u,       24u,
    23u,       22u,       20u,       19u,       18u,       17u,       16u,       15u,
    14u,       13u,       12u,       12u,       11u,       10u,       10u,       9u,
    8u,        8u,        7u,        7u,        7u,        6u,        6u,        5u,
    5u,        5u,        5u,        4u,        4u,        4u,        4u,        3u,
    3u,        3u,        3u,        3u,        2u,        2u,        2u,        2u,
};

uint32_t qubo_exp_q24(uint32_t i)
{
    return i < QUBO_EXP_TABLE_N ? qubo_exp_tab[i] : 0u;
}

uint64_t qubo_rng_next(uint64_t *state)
{
    uint64_t x;
    if (!state) return 0;
    x = *state;
    if (x == 0) x = 0x9E3779B97F4A7C15ull;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

/* ------------------------------------------------------------------ */
/* helpers                                                             */

static uint64_t qubo__mask(uint32_t n)
{
    return n >= 64u ? ~0ull : ((1ull << n) - 1u);
}

/* acc += |v| with the abs-sum limit; returns 0 on success. */
static int qubo__abs_acc(int64_t *acc, int64_t v)
{
    int64_t a;
    if (v == INT64_MIN) return 1;
    a = v < 0 ? -v : v;
    if (__builtin_add_overflow(*acc, a, acc)) return 1;
    return *acc > QUBO_MAX_ABS_SUM;
}

/* *acc += v, checked; returns 0 on success. */
static int qubo__add(int64_t *acc, int64_t v)
{
    return __builtin_add_overflow(*acc, v, acc) ? 1 : 0;
}

static void qubo__res_set(qubo_result_t *res, uint64_t x, int64_t e, int64_t lb, uint64_t work,
                          int heuristic)
{
    res->x = x;
    res->energy = e;
    res->lower_bound = lb;
    res->gap = e - lb;
    res->work = work;
    if (e == lb)
        res->cert = QUBO_CERT_OPTIMAL;
    else
        res->cert = heuristic ? QUBO_CERT_HEURISTIC_ONLY : QUBO_CERT_GAP;
}

/* ------------------------------------------------------------------ */
/* model                                                               */

qubo_status_t qubo_init(qubo_t *p, uint32_t n)
{
    uint32_t i, j;
    if (!p) return QUBO_ERR_NULL;
    if (n == 0 || n > QUBO_MAX_VARS) return QUBO_ERR_RANGE;
    p->n = n;
    p->offset = 0;
    for (i = 0; i < QUBO_MAX_VARS; i++)
        for (j = 0; j < QUBO_MAX_VARS; j++) p->q[i][j] = 0;
    return QUBO_OK;
}

static qubo_status_t qubo__index(const qubo_t *p, uint32_t *i, uint32_t *j)
{
    uint32_t t;
    if (p->n == 0 || p->n > QUBO_MAX_VARS) return QUBO_ERR_RANGE;
    if (*i >= p->n || *j >= p->n) return QUBO_ERR_RANGE;
    if (*i > *j) {
        t = *i;
        *i = *j;
        *j = t;
    }
    return QUBO_OK;
}

qubo_status_t qubo_set(qubo_t *p, uint32_t i, uint32_t j, int64_t v)
{
    qubo_status_t st;
    if (!p) return QUBO_ERR_NULL;
    st = qubo__index(p, &i, &j);
    if (st != QUBO_OK) return st;
    p->q[i][j] = v;
    return QUBO_OK;
}

qubo_status_t qubo_add(qubo_t *p, uint32_t i, uint32_t j, int64_t v)
{
    qubo_status_t st;
    int64_t s;
    if (!p) return QUBO_ERR_NULL;
    st = qubo__index(p, &i, &j);
    if (st != QUBO_OK) return st;
    s = p->q[i][j];
    if (qubo__add(&s, v)) return QUBO_ERR_OVERFLOW;
    p->q[i][j] = s;
    return QUBO_OK;
}

qubo_status_t qubo_get(const qubo_t *p, uint32_t i, uint32_t j, int64_t *v)
{
    qubo_status_t st;
    if (!p || !v) return QUBO_ERR_NULL;
    st = qubo__index(p, &i, &j);
    if (st != QUBO_OK) return st;
    *v = p->q[i][j];
    return QUBO_OK;
}

qubo_status_t qubo_check(const qubo_t *p)
{
    uint32_t i, j;
    int64_t acc = 0;
    if (!p) return QUBO_ERR_NULL;
    if (p->n == 0 || p->n > QUBO_MAX_VARS) return QUBO_ERR_RANGE;
    for (i = 0; i < QUBO_MAX_VARS; i++)
        for (j = 0; j < QUBO_MAX_VARS; j++)
            if (p->q[i][j] != 0 && (i > j || j >= p->n)) return QUBO_ERR_RANGE;
    if (qubo__abs_acc(&acc, p->offset)) return QUBO_ERR_OVERFLOW;
    for (i = 0; i < p->n; i++)
        for (j = i; j < p->n; j++)
            if (qubo__abs_acc(&acc, p->q[i][j])) return QUBO_ERR_OVERFLOW;
    return QUBO_OK;
}

/* Energy of a checked problem (bounded, cannot overflow). */
static int64_t qubo__energy(const qubo_t *p, uint64_t x)
{
    uint32_t i, j, n = p->n;
    int64_t e = p->offset;
    for (i = 0; i < n; i++) {
        if (!((x >> i) & 1u)) continue;
        e += p->q[i][i];
        for (j = i + 1u; j < n; j++)
            if ((x >> j) & 1u) e += p->q[i][j];
    }
    return e;
}

qubo_status_t qubo_energy(const qubo_t *p, uint64_t x, int64_t *e)
{
    qubo_status_t st;
    if (!p || !e) return QUBO_ERR_NULL;
    st = qubo_check(p);
    if (st != QUBO_OK) return st;
    if (x & ~qubo__mask(p->n)) return QUBO_ERR_RANGE;
    *e = qubo__energy(p, x);
    return QUBO_OK;
}

/* ------------------------------------------------------------------ */
/* Ising                                                               */

qubo_status_t qubo_ising_init(qubo_ising_t *m, uint32_t n)
{
    uint32_t i, j;
    if (!m) return QUBO_ERR_NULL;
    if (n == 0 || n > QUBO_MAX_VARS) return QUBO_ERR_RANGE;
    m->n = n;
    m->offset = 0;
    for (i = 0; i < QUBO_MAX_VARS; i++) {
        m->h[i] = 0;
        for (j = 0; j < QUBO_MAX_VARS; j++) m->j[i][j] = 0;
    }
    return QUBO_OK;
}

qubo_status_t qubo_ising_check(const qubo_ising_t *m)
{
    uint32_t i, j;
    int64_t acc = 0;
    if (!m) return QUBO_ERR_NULL;
    if (m->n == 0 || m->n > QUBO_MAX_VARS) return QUBO_ERR_RANGE;
    for (i = 0; i < QUBO_MAX_VARS; i++) {
        if (m->h[i] != 0 && i >= m->n) return QUBO_ERR_RANGE;
        for (j = 0; j < QUBO_MAX_VARS; j++)
            if (m->j[i][j] != 0 && (i >= j || j >= m->n)) return QUBO_ERR_RANGE;
    }
    if (qubo__abs_acc(&acc, m->offset)) return QUBO_ERR_OVERFLOW;
    for (i = 0; i < m->n; i++) {
        if (qubo__abs_acc(&acc, m->h[i])) return QUBO_ERR_OVERFLOW;
        for (j = i + 1u; j < m->n; j++)
            if (qubo__abs_acc(&acc, m->j[i][j])) return QUBO_ERR_OVERFLOW;
    }
    return QUBO_OK;
}

qubo_status_t qubo_ising_energy(const qubo_ising_t *m, uint64_t s, int64_t *e)
{
    qubo_status_t st;
    uint32_t i, j, n;
    int64_t acc;
    if (!m || !e) return QUBO_ERR_NULL;
    st = qubo_ising_check(m);
    if (st != QUBO_OK) return st;
    n = m->n;
    if (s & ~qubo__mask(n)) return QUBO_ERR_RANGE;
    acc = m->offset;
    for (i = 0; i < n; i++) {
        uint64_t si = (s >> i) & 1u;
        acc += si ? m->h[i] : -m->h[i];
        for (j = i + 1u; j < n; j++) {
            uint64_t sj = (s >> j) & 1u;
            acc += (si == sj) ? m->j[i][j] : -m->j[i][j];
        }
    }
    *e = acc;
    return QUBO_OK;
}

/* s = 2x - 1:  h s = 2h x - h;  J s_i s_j = 4J x_i x_j - 2J x_i - 2J x_j + J. */
qubo_status_t qubo_from_ising(const qubo_ising_t *m, qubo_t *out)
{
    qubo_status_t st;
    uint32_t i, j, n;
    int64_t off, d;
    if (!m || !out) return QUBO_ERR_NULL;
    st = qubo_ising_check(m);
    if (st != QUBO_OK) return st;
    n = m->n;
    st = qubo_init(out, n);
    if (st != QUBO_OK) return st;
    off = m->offset;
    for (i = 0; i < n; i++) {
        d = m->h[i];
        if (qubo__add(&d, m->h[i])) return QUBO_ERR_OVERFLOW; /* 2h */
        if (qubo__add(&off, -m->h[i])) return QUBO_ERR_OVERFLOW;
        out->q[i][i] = d;
    }
    for (i = 0; i < n; i++)
        for (j = i + 1u; j < n; j++) {
            int64_t J = m->j[i][j], j2 = J, j4;
            if (J == 0) continue;
            if (qubo__add(&j2, J)) return QUBO_ERR_OVERFLOW; /* 2J */
            j4 = j2;
            if (qubo__add(&j4, j2)) return QUBO_ERR_OVERFLOW; /* 4J */
            out->q[i][j] = j4;
            if (qubo__add(&out->q[i][i], -j2)) return QUBO_ERR_OVERFLOW;
            if (qubo__add(&out->q[j][j], -j2)) return QUBO_ERR_OVERFLOW;
            if (qubo__add(&off, J)) return QUBO_ERR_OVERFLOW;
        }
    out->offset = off;
    return qubo_check(out);
}

/* 4x = 2(s+1):  4 Q_ii x_i = 2Q_ii s_i + 2Q_ii;
 * 4 Q_ij x_i x_j = Q_ij (s_i s_j + s_i + s_j + 1). */
qubo_status_t qubo_to_ising(const qubo_t *p, qubo_ising_t *out)
{
    qubo_status_t st;
    uint32_t i, j, n;
    int64_t off;
    if (!p || !out) return QUBO_ERR_NULL;
    st = qubo_check(p);
    if (st != QUBO_OK) return st;
    n = p->n;
    st = qubo_ising_init(out, n);
    if (st != QUBO_OK) return st;
    off = p->offset; /* times 4, by repeated checked adds */
    if (qubo__add(&off, off)) return QUBO_ERR_OVERFLOW;
    if (qubo__add(&off, off)) return QUBO_ERR_OVERFLOW;
    for (i = 0; i < n; i++) {
        int64_t d = p->q[i][i];
        if (qubo__add(&d, p->q[i][i])) return QUBO_ERR_OVERFLOW; /* 2Q_ii */
        out->h[i] = d;
        if (qubo__add(&off, d)) return QUBO_ERR_OVERFLOW;
    }
    for (i = 0; i < n; i++)
        for (j = i + 1u; j < n; j++) {
            int64_t v = p->q[i][j];
            if (v == 0) continue;
            out->j[i][j] = v;
            if (qubo__add(&out->h[i], v)) return QUBO_ERR_OVERFLOW;
            if (qubo__add(&out->h[j], v)) return QUBO_ERR_OVERFLOW;
            if (qubo__add(&off, v)) return QUBO_ERR_OVERFLOW;
        }
    out->offset = off;
    return qubo_ising_check(out);
}

/* ------------------------------------------------------------------ */
/* parser                                                              */

static int qubo__space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';
}

/* Parses [s, e) as a decimal int64. 0 ok, -1 syntax, -2 overflow. */
static int qubo__int(const char *s, uint32_t a, uint32_t e, int64_t *out)
{
    uint64_t mag = 0, lim;
    int neg = 0;
    if (a < e && (s[a] == '-' || s[a] == '+')) {
        neg = s[a] == '-';
        a++;
    }
    if (a >= e) return -1;
    lim = neg ? 9223372036854775808ull : 9223372036854775807ull;
    for (; a < e; a++) {
        uint64_t d;
        if (s[a] < '0' || s[a] > '9') return -1;
        d = (uint64_t) (s[a] - '0');
        /* mag * 10 + d <= lim  <=>  mag <= (lim - d) / 10 (no division:
         * compare against the precomputed quotient and remainder). */
        if (mag > 922337203685477580ull) return -2;
        if (mag == 922337203685477580ull && d > (lim - 9223372036854775800ull)) return -2;
        mag = (mag << 3) + (mag << 1) + d;
    }
    if (neg) {
        *out = (mag == 9223372036854775808ull) ? INT64_MIN : -(int64_t) mag;
    } else {
        *out = (int64_t) mag;
    }
    return 0;
}

static int qubo__tok_is(const char *s, uint32_t a, uint32_t e, const char *kw)
{
    uint32_t k = 0;
    for (; a < e; a++, k++)
        if (kw[k] == 0 || kw[k] != s[a]) return 0;
    return kw[k] == 0;
}

static qubo_status_t qubo__fail(qubo_parse_report_t *r, qubo_status_t st, uint32_t line)
{
    r->line = line;
    if (st == QUBO_ERR_CONFLICT)
        r->tag = QUBO_LP_BOTH;
    else if (st == QUBO_ERR_NO_SIZE)
        r->tag = QUBO_LP_NEITHER;
    else
        r->tag = QUBO_LP_FALSE;
    return st;
}

/* Applies a duplicate-aware write of v into *slot. */
static qubo_status_t qubo__merge(int64_t *slot, int seen, int64_t v, qubo_merge_t merge,
                                 qubo_parse_report_t *r)
{
    if (!seen) {
        *slot = v;
        return QUBO_OK;
    }
    if (merge == QUBO_MERGE_SUM) {
        if (qubo__add(slot, v)) return QUBO_ERR_OVERFLOW;
        r->duplicates++;
        return QUBO_OK;
    }
    if (*slot != v) return QUBO_ERR_CONFLICT;
    r->duplicates++;
    return QUBO_OK;
}

qubo_status_t qubo_parse(const char *text, uint32_t len, qubo_merge_t merge, qubo_t *out,
                         qubo_parse_report_t *report)
{
    uint64_t seen[QUBO_MAX_VARS];
    uint32_t pos = 0, line = 0, k, n = 0;
    int have_n = 0, off_seen = 0;
    qubo_status_t st;

    if (!report) return QUBO_ERR_NULL;
    report->tag = QUBO_LP_NEITHER;
    report->line = 0;
    report->entries = 0;
    report->duplicates = 0;
    if (!text || !out) return QUBO_ERR_NULL;
    if (merge != QUBO_MERGE_REFUSE && merge != QUBO_MERGE_SUM)
        return qubo__fail(report, QUBO_ERR_PARAM, 0);
    for (k = 0; k < QUBO_MAX_VARS; k++) seen[k] = 0;

    while (pos < len) {
        uint32_t ts[4], te[4], nt = 0, end = pos, stop;
        line++;
        while (end < len && text[end] != '\n') end++;
        stop = pos;
        while (stop < end && text[stop] != '#') stop++;
        k = pos;
        while (k < stop) {
            if (qubo__space(text[k])) {
                k++;
                continue;
            }
            if (nt == 4) return qubo__fail(report, QUBO_ERR_SYNTAX, line);
            ts[nt] = k;
            while (k < stop && !qubo__space(text[k])) k++;
            te[nt++] = k;
        }
        pos = end + 1u;
        if (nt == 0) continue;

        if (qubo__tok_is(text, ts[0], te[0], "n")) {
            int64_t v;
            int rc;
            if (nt != 2) return qubo__fail(report, QUBO_ERR_SYNTAX, line);
            rc = qubo__int(text, ts[1], te[1], &v);
            if (rc == -1) return qubo__fail(report, QUBO_ERR_SYNTAX, line);
            if (rc == -2 || v < 1 || v > (int64_t) QUBO_MAX_VARS)
                return qubo__fail(report, QUBO_ERR_RANGE, line);
            if (have_n) {
                if ((uint32_t) v != n) return qubo__fail(report, QUBO_ERR_CONFLICT, line);
                continue;
            }
            n = (uint32_t) v;
            have_n = 1;
            st = qubo_init(out, n);
            if (st != QUBO_OK) return qubo__fail(report, st, line);
        } else if (qubo__tok_is(text, ts[0], te[0], "offset")) {
            int64_t v;
            int rc;
            if (nt != 2) return qubo__fail(report, QUBO_ERR_SYNTAX, line);
            rc = qubo__int(text, ts[1], te[1], &v);
            if (rc == -1) return qubo__fail(report, QUBO_ERR_SYNTAX, line);
            if (rc == -2) return qubo__fail(report, QUBO_ERR_OVERFLOW, line);
            if (!have_n) return qubo__fail(report, QUBO_ERR_NO_SIZE, line);
            st = qubo__merge(&out->offset, off_seen, v, merge, report);
            if (st != QUBO_OK) return qubo__fail(report, st, line);
            off_seen = 1;
        } else if (nt == 3) {
            int64_t a, b, v;
            int r1 = qubo__int(text, ts[0], te[0], &a);
            int r2 = qubo__int(text, ts[1], te[1], &b);
            int r3 = qubo__int(text, ts[2], te[2], &v);
            uint32_t i, j;
            if (r1 == -1 || r2 == -1 || r3 == -1) return qubo__fail(report, QUBO_ERR_SYNTAX, line);
            if (r1 == -2 || r2 == -2) return qubo__fail(report, QUBO_ERR_RANGE, line);
            if (r3 == -2) return qubo__fail(report, QUBO_ERR_OVERFLOW, line);
            if (!have_n) return qubo__fail(report, QUBO_ERR_NO_SIZE, line);
            if (a < 0 || b < 0 || a >= (int64_t) n || b >= (int64_t) n)
                return qubo__fail(report, QUBO_ERR_RANGE, line);
            i = (uint32_t) (a < b ? a : b);
            j = (uint32_t) (a < b ? b : a);
            st = qubo__merge(&out->q[i][j], (int) ((seen[i] >> j) & 1u), v, merge, report);
            if (st != QUBO_OK) return qubo__fail(report, st, line);
            seen[i] |= 1ull << j;
            report->entries++;
        } else {
            return qubo__fail(report, QUBO_ERR_SYNTAX, line);
        }
    }
    if (!have_n) return qubo__fail(report, QUBO_ERR_NO_SIZE, 0);
    st = qubo_check(out);
    if (st != QUBO_OK) return qubo__fail(report, st, 0);
    report->tag = QUBO_LP_TRUE;
    return QUBO_OK;
}

/* ------------------------------------------------------------------ */
/* bounds and verification                                             */

static int64_t qubo__root_bound(const qubo_t *p)
{
    uint32_t i, j, n = p->n;
    int64_t lb = p->offset;
    for (i = 0; i < n; i++) {
        int64_t r = p->q[i][i];
        for (j = i + 1u; j < n; j++)
            if (p->q[i][j] < 0) r += p->q[i][j];
        if (r < 0) lb += r;
    }
    return lb;
}

qubo_status_t qubo_root_bound(const qubo_t *p, int64_t *lb)
{
    qubo_status_t st;
    if (!p || !lb) return QUBO_ERR_NULL;
    st = qubo_check(p);
    if (st != QUBO_OK) return st;
    *lb = qubo__root_bound(p);
    return QUBO_OK;
}

qubo_status_t qubo_verify(const qubo_t *p, const qubo_result_t *res)
{
    qubo_status_t st;
    int64_t e;
    if (!p || !res) return QUBO_ERR_NULL;
    st = qubo_energy(p, res->x, &e);
    if (st != QUBO_OK) return st;
    if (e != res->energy) return QUBO_ERR_INVALID;
    if (res->lower_bound > e) return QUBO_ERR_INVALID;
    if (res->gap != e - res->lower_bound) return QUBO_ERR_INVALID;
    switch (res->cert) {
    case QUBO_CERT_OPTIMAL:
        if (res->gap != 0) return QUBO_ERR_INVALID;
        break;
    case QUBO_CERT_GAP:
    case QUBO_CERT_HEURISTIC_ONLY:
        if (res->gap == 0) return QUBO_ERR_INVALID;
        break;
    default:
        return QUBO_ERR_INVALID;
    }
    return QUBO_OK;
}

/* ------------------------------------------------------------------ */
/* shared solver plumbing                                              */

static void qubo__build_sym(const qubo_t *p, qubo_ws_t *ws)
{
    uint32_t i, j, n = p->n;
    for (i = 0; i < n; i++) {
        ws->sym[i][i] = p->q[i][i];
        for (j = i + 1u; j < n; j++) {
            ws->sym[i][j] = p->q[i][j];
            ws->sym[j][i] = p->q[i][j];
        }
    }
}

/* f_i = Q_ii + sum_{j != i, x_j = 1} Qsym_ij: energy change of x_i 0 -> 1. */
static void qubo__fields(const qubo_ws_t *ws, uint32_t n, uint64_t x, int64_t *f)
{
    uint32_t i, j;
    for (i = 0; i < n; i++) {
        int64_t s = ws->sym[i][i];
        for (j = 0; j < n; j++)
            if (j != i && ((x >> j) & 1u)) s += ws->sym[i][j];
        f[i] = s;
    }
}

/* Flips bit i of *x, updating energy and fields. */
static void qubo__flip(const qubo_ws_t *ws, uint32_t n, uint64_t *x, int64_t *e, int64_t *f,
                       uint32_t i)
{
    uint32_t j;
    if ((*x >> i) & 1u) {
        *e -= f[i];
        *x &= ~(1ull << i);
        for (j = 0; j < n; j++)
            if (j != i) f[j] -= ws->sym[j][i];
    } else {
        *e += f[i];
        *x |= 1ull << i;
        for (j = 0; j < n; j++)
            if (j != i) f[j] += ws->sym[j][i];
    }
}

/* ------------------------------------------------------------------ */
/* exact: brute force                                                  */

qubo_status_t qubo_brute_force(const qubo_t *p, uint64_t step_budget, qubo_ws_t *ws,
                               qubo_result_t *res)
{
    qubo_status_t st;
    uint32_t n;
    uint64_t steps, k, x = 0, bx = 0;
    int64_t e, be;
    if (!p || !ws || !res) return QUBO_ERR_NULL;
    st = qubo_check(p);
    if (st != QUBO_OK) return st;
    n = p->n;
    if (n > QUBO_BRUTE_MAX_VARS) return QUBO_ERR_RANGE;
    steps = (1ull << n) - 1u;
    if (steps > step_budget) return QUBO_ERR_BUDGET;
    qubo__build_sym(p, ws);
    qubo__fields(ws, n, 0, ws->field[0]);
    e = p->offset;
    be = e;
    for (k = 1; k <= steps; k++) {
        uint32_t b = (uint32_t) __builtin_ctz((uint32_t) k);
        qubo__flip(ws, n, &x, &e, ws->field[0], b);
        if (e < be) {
            be = e;
            bx = x;
        }
    }
    qubo__res_set(res, bx, be, be, steps, 0);
    return QUBO_OK;
}

/* ------------------------------------------------------------------ */
/* exact: branch and bound                                             */

/* Evaluates the node (fixed set *F, values *V among it). Applies
 * first-order persistencies (each one keeps at least one optimum of the
 * subproblem), then returns cst = E(V with free vars at 0) and a proven
 * lower bound over all completions. ws->lin[i] holds, for each free i, the
 * linear coefficient Q_ii + sum_{j in V} Qsym_ij. */
static void qubo__node(const qubo_t *p, qubo_ws_t *ws, uint64_t *F, uint64_t *V, int64_t *cst,
                       int64_t *lb)
{
    uint32_t i, j, n = p->n;
    uint64_t fx = *F, vx = *V;
    int64_t c = qubo__energy(p, vx), b;
    int changed;
    for (i = 0; i < n; i++) {
        int64_t s;
        if ((fx >> i) & 1u) continue;
        s = ws->sym[i][i];
        for (j = 0; j < n; j++)
            if ((vx >> j) & 1u) s += ws->sym[i][j];
        ws->lin[i] = s;
    }
    do {
        changed = 0;
        for (i = 0; i < n; i++) {
            int64_t neg = 0, pos = 0;
            if ((fx >> i) & 1u) continue;
            for (j = 0; j < n; j++) {
                int64_t s;
                if (j == i || ((fx >> j) & 1u)) continue;
                s = ws->sym[i][j];
                if (s < 0)
                    neg += s;
                else
                    pos += s;
            }
            if (ws->lin[i] + neg >= 0) {
                fx |= 1ull << i; /* x_i = 1 never helps: fix 0 */
                changed = 1;
            } else if (ws->lin[i] + pos <= 0) {
                fx |= 1ull << i; /* x_i = 1 never hurts: fix 1 */
                vx |= 1ull << i;
                c += ws->lin[i];
                for (j = 0; j < n; j++)
                    if (!((fx >> j) & 1u)) ws->lin[j] += ws->sym[j][i];
                changed = 1;
            }
        }
    } while (changed);
    b = c;
    for (i = 0; i < n; i++) {
        int64_t r;
        if ((fx >> i) & 1u) continue;
        r = ws->lin[i];
        for (j = i + 1u; j < n; j++)
            if (!((fx >> j) & 1u) && p->q[i][j] < 0) r += p->q[i][j];
        if (r < 0) b += r;
    }
    *F = fx;
    *V = vx;
    *cst = c;
    *lb = b;
}

qubo_status_t qubo_bnb(const qubo_t *p, uint64_t node_budget, const uint64_t *warm, qubo_ws_t *ws,
                       qubo_result_t *res)
{
    qubo_status_t st;
    uint32_t n, i, j, sp = 0;
    uint64_t full, inc_x = 0, nodes = 0;
    int64_t inc_e, root_lb, rowabs[QUBO_MAX_VARS];
    if (!p || !ws || !res) return QUBO_ERR_NULL;
    st = qubo_check(p);
    if (st != QUBO_OK) return st;
    if (node_budget == 0) return QUBO_ERR_PARAM;
    n = p->n;
    full = qubo__mask(n);
    if (warm && (*warm & ~full)) return QUBO_ERR_RANGE;
    qubo__build_sym(p, ws);

    /* branching order: descending sum of |Qsym| in the row, ties by index */
    for (i = 0; i < n; i++) {
        int64_t s = 0;
        for (j = 0; j < n; j++) s += ws->sym[i][j] < 0 ? -ws->sym[i][j] : ws->sym[i][j];
        rowabs[i] = s;
        ws->ord[i] = (uint8_t) i;
    }
    for (i = 1; i < n; i++) {
        uint8_t v = ws->ord[i];
        j = i;
        while (j > 0 && rowabs[ws->ord[j - 1u]] < rowabs[v]) {
            ws->ord[j] = ws->ord[j - 1u];
            j--;
        }
        ws->ord[j] = v;
    }

    inc_e = p->offset;
    if (warm) {
        int64_t we = qubo__energy(p, *warm);
        if (we < inc_e) {
            inc_e = we;
            inc_x = *warm;
        }
    }
    root_lb = qubo__root_bound(p);
    ws->st_fixed[0] = 0;
    ws->st_val[0] = 0;
    ws->st_lb[0] = root_lb;
    sp = 1;
    while (sp > 0) {
        uint64_t F, V;
        int64_t cst, lb, plb;
        uint32_t v = 0;
        if (nodes >= node_budget) break;
        sp--;
        F = ws->st_fixed[sp];
        V = ws->st_val[sp];
        plb = ws->st_lb[sp];
        nodes++;
        if (plb >= inc_e) continue;
        qubo__node(p, ws, &F, &V, &cst, &lb);
        if (cst < inc_e) { /* free vars at 0 is itself a full assignment */
            inc_e = cst;
            inc_x = V;
        }
        if (F == full || lb >= inc_e) continue;
        for (i = 0; i < n; i++) {
            v = ws->ord[i];
            if (!((F >> v) & 1u)) break;
        }
        F |= 1ull << v;
        if (ws->lin[v] < 0) { /* prefer x_v = 1: push 0 first */
            ws->st_fixed[sp] = F;
            ws->st_val[sp] = V;
            ws->st_lb[sp++] = lb;
            ws->st_fixed[sp] = F;
            ws->st_val[sp] = V | (1ull << v);
            ws->st_lb[sp++] = lb;
        } else {
            ws->st_fixed[sp] = F;
            ws->st_val[sp] = V | (1ull << v);
            ws->st_lb[sp++] = lb;
            ws->st_fixed[sp] = F;
            ws->st_val[sp] = V;
            ws->st_lb[sp++] = lb;
        }
    }
    if (sp == 0) {
        qubo__res_set(res, inc_x, inc_e, inc_e, nodes, 0);
    } else {
        int64_t m = inc_e;
        for (i = 0; i < sp; i++)
            if (ws->st_lb[i] < m) m = ws->st_lb[i];
        if (m < root_lb) m = root_lb;
        qubo__res_set(res, inc_x, inc_e, m, nodes, 0);
    }
    return QUBO_OK;
}

/* ------------------------------------------------------------------ */
/* heuristics                                                          */

/* Temperatures are in 1/256 energy units (Q8), so integer problems with
 * small coefficients can still be annealed below one energy unit.
 * thr[k] = ceil(k T / 4096) for k <= 256, hence
 * d >= thr[k]  <=>  4096 d / T >= k  <=>  16 d / (T / 256) >= k. */
static void qubo__thr_fill(int64_t *thr, int64_t t)
{
    uint32_t k;
    for (k = 0; k <= QUBO_EXP_TABLE_N; k++)
        thr[k] = (int64_t) (((uint64_t) k * (uint64_t) t + 4095u) >> 12);
}

/* min(QUBO_EXP_TABLE_N, floor(4096 d / T)) for d >= 0, by binary search. */
static uint32_t qubo__xindex(const int64_t *thr, int64_t d)
{
    uint32_t lo = 0, hi = QUBO_EXP_TABLE_N;
    while (lo < hi) {
        uint32_t mid = (lo + hi + 1u) >> 1;
        if (thr[mid] <= d)
            lo = mid;
        else
            hi = mid - 1u;
    }
    return lo;
}

uint32_t qubo_exp_index(int64_t t_q8, int64_t d)
{
    int64_t thr[QUBO_EXP_TABLE_N + 1u];
    if (t_q8 < 1 || t_q8 > QUBO_MAX_TEMP || d < 0) return QUBO_EXP_TABLE_N;
    qubo__thr_fill(thr, t_q8);
    return qubo__xindex(thr, d);
}

/* Metropolis test for an uphill move d > 0 at temperature slot r. */
static int qubo__accept(qubo_ws_t *ws, uint32_t r, int64_t d, uint64_t *rng)
{
    uint32_t k = qubo__xindex(ws->thr[r], d);
    if (k >= QUBO_EXP_TABLE_N) return 0;
    return (qubo_rng_next(rng) >> 40) < (uint64_t) qubo_exp_tab[k];
}

static void qubo__heur_finish(const qubo_t *p, qubo_result_t *res, uint64_t bx, int64_t be,
                              uint64_t work)
{
    qubo__res_set(res, bx, be, qubo__root_bound(p), work, 1);
}

qubo_status_t qubo_anneal(const qubo_t *p, const qubo_anneal_params_t *prm, uint64_t *rng,
                          qubo_ws_t *ws, qubo_result_t *res)
{
    qubo_status_t st;
    uint32_t n, R, r, i, k, sw;
    uint64_t mask, bx = 0, moves = 0;
    int64_t tmax, be;
    if (!p || !prm || !rng || !ws || !res) return QUBO_ERR_NULL;
    st = qubo_check(p);
    if (st != QUBO_OK) return st;
    n = p->n;
    R = prm->replicas;
    if (R == 0 || R > QUBO_MAX_REPLICAS || prm->sweeps == 0) return QUBO_ERR_PARAM;
    if (prm->t_max_q8 < 0 || prm->t_max_q8 > QUBO_MAX_TEMP) return QUBO_ERR_PARAM;
    if ((uint64_t) prm->sweeps * R * n > QUBO_MAX_MOVES) return QUBO_ERR_PARAM;
    if (*rng == 0) return QUBO_ERR_PARAM;
    mask = qubo__mask(n);
    qubo__build_sym(p, ws);

    tmax = prm->t_max_q8;
    if (tmax == 0) {
        for (i = 0; i < n; i++) {
            int64_t s = 0;
            for (k = 0; k < n; k++) s += ws->sym[i][k] < 0 ? -ws->sym[i][k] : ws->sym[i][k];
            if (s > tmax) tmax = s;
        }
        /* the largest possible |delta| of a single flip, in Q8 */
        tmax = tmax > (QUBO_MAX_TEMP >> 8) ? QUBO_MAX_TEMP : (tmax << 8);
        if (tmax < 1) tmax = 1;
    }
    for (r = 0; r < R; r++) {
        int64_t t = r == 0 ? tmax : ws->temp[r - 1u] - (ws->temp[r - 1u] >> 2);
        if (t < 1) t = 1;
        ws->temp[r] = t;
        qubo__thr_fill(ws->thr[r], t);
        ws->perm[r] = r;
        ws->xs[r] = qubo_rng_next(rng) & mask;
        ws->e[r] = qubo__energy(p, ws->xs[r]);
        qubo__fields(ws, n, ws->xs[r], ws->field[r]);
    }
    be = ws->e[0];
    bx = ws->xs[0];
    for (r = 1; r < R; r++)
        if (ws->e[r] < be) {
            be = ws->e[r];
            bx = ws->xs[r];
        }

    for (sw = 0; sw < prm->sweeps; sw++) {
        for (r = 0; r < R; r++) {
            uint32_t id = ws->perm[r];
            int64_t *f = ws->field[id];
            for (i = 0; i < n; i++) {
                int64_t d = ((ws->xs[id] >> i) & 1u) ? -f[i] : f[i];
                moves++;
                if (d > 0 && !qubo__accept(ws, r, d, rng)) continue;
                qubo__flip(ws, n, &ws->xs[id], &ws->e[id], f, i);
                if (ws->e[id] < be) {
                    be = ws->e[id];
                    bx = ws->xs[id];
                }
            }
        }
        for (r = 0; r + 1u < R; r++) {
            uint32_t a = ws->perm[r], b = ws->perm[r + 1u];
            int swap;
            if (ws->e[a] <= ws->e[b] || ws->temp[r] == ws->temp[r + 1u])
                swap = 1;
            else
                swap = qubo__accept(ws, r + 1u, (ws->e[a] - ws->e[b]) >> 2, rng);
            if (swap) {
                ws->perm[r] = b;
                ws->perm[r + 1u] = a;
            }
        }
    }
    qubo__heur_finish(p, res, bx, be, moves);
    return QUBO_OK;
}

qubo_status_t qubo_local_search(const qubo_t *p, const qubo_ls_params_t *prm, uint64_t *rng,
                                qubo_ws_t *ws, qubo_result_t *res)
{
    qubo_status_t st;
    uint32_t n, r, i;
    uint64_t mask, bx = 0, flips = 0;
    int64_t be = 0, *f = ws ? ws->field[0] : (int64_t *) 0;
    int have = 0;
    if (!p || !prm || !rng || !ws || !res) return QUBO_ERR_NULL;
    st = qubo_check(p);
    if (st != QUBO_OK) return st;
    if (prm->restarts == 0 || prm->max_flips == 0 || *rng == 0) return QUBO_ERR_PARAM;
    n = p->n;
    mask = qubo__mask(n);
    qubo__build_sym(p, ws);
    for (r = 0; r < prm->restarts && flips < prm->max_flips; r++) {
        uint64_t x = qubo_rng_next(rng) & mask;
        int64_t e = qubo__energy(p, x);
        qubo__fields(ws, n, x, f);
        for (;;) {
            int64_t bd = 0;
            uint32_t bi = n;
            for (i = 0; i < n; i++) {
                int64_t d = ((x >> i) & 1u) ? -f[i] : f[i];
                if (d < bd) {
                    bd = d;
                    bi = i;
                }
            }
            if (bi == n || flips >= prm->max_flips) break;
            qubo__flip(ws, n, &x, &e, f, bi);
            flips++;
        }
        if (!have || e < be) {
            be = e;
            bx = x;
            have = 1;
        }
    }
    qubo__heur_finish(p, res, bx, be, flips);
    return QUBO_OK;
}
