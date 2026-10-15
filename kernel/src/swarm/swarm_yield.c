/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* swarm_yield.c — token yield. See swarm_yield.h. */
#include "swarm_yield.h"
#include "swarm_quality.h"

/* ---- exact wide arithmetic (Y5), no __int128 so 32-bit builds work ---- */

typedef struct {
    uint64_t w[3]; /* little-endian 64-bit words: up to 192 bits */
} wide_t;

static void mul64(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo)
{
    uint64_t a0 = a & 0xFFFFFFFFu, a1 = a >> 32, b0 = b & 0xFFFFFFFFu, b1 = b >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
    *lo = (mid << 32) | (p00 & 0xFFFFFFFFu);
    *hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

/* a * b * c, exact in 192 bits */
static wide_t mul3(uint64_t a, uint64_t b, uint64_t c)
{
    uint64_t hi, lo, h1, l1, h2, l2;
    mul64(a, b, &hi, &lo);
    mul64(lo, c, &h1, &l1);
    mul64(hi, c, &h2, &l2);
    wide_t r;
    r.w[0] = l1;
    r.w[1] = h1 + l2;
    r.w[2] = h2 + (r.w[1] < h1);
    return r;
}

static int cmp_wide(wide_t a, wide_t b)
{
    for (int i = 2; i >= 0; i--) {
        if (a.w[i] < b.w[i]) return -1;
        if (a.w[i] > b.w[i]) return 1;
    }
    return 0;
}

/* floor((hi:lo) / d), d > 0; UINT64_MAX when the quotient does not fit */
static uint64_t div128(uint64_t hi, uint64_t lo, uint64_t d)
{
    if (hi >= d) return UINT64_MAX;
    uint64_t rem = hi, q = 0;
    for (int i = 0; i < 64; i++) {
        uint64_t top = rem >> 63;
        rem = (rem << 1) | (lo >> 63);
        lo <<= 1;
        q <<= 1;
        if (top || rem >= d) {
            rem -= d;
            q |= 1;
        }
    }
    return q;
}

static bool add_ok(uint64_t a, uint64_t b)
{
    return a <= UINT64_MAX - b;
}

/* ---- Y1-Y4 ---- */

void swarm_yield_init(swarm_yield_t *y)
{
    if (!y) return;
    y->tokens = 0;
    y->wasted = 0;
    y->work_milli = 0;
    y->tasks = 0;
    y->passed = 0;
    y->first_try = 0;
}

swarm_status_t swarm_yield_task(swarm_yield_t *y, uint64_t tokens, uint32_t attempts,
                                uint32_t r_milli, uint32_t l_milli)
{
    if (!y || tokens == 0 || attempts == 0 || l_milli > 1000u) return SWARM_ERR_ARG;
    bool pass = swarm_quality_passes(r_milli, l_milli);
    uint64_t work = pass ? (uint64_t) r_milli * l_milli / 1000u : 0;
    if (!add_ok(y->tokens, tokens) || !add_ok(y->work_milli, work) || y->tasks == UINT64_MAX)
        return SWARM_ERR_ARG;
    if (!pass && !add_ok(y->wasted, tokens)) return SWARM_ERR_ARG;
    y->tokens += tokens;
    y->tasks++;
    if (pass) {
        y->work_milli += work;
        y->passed++;
        if (attempts == 1) y->first_try++;
    } else {
        y->wasted += tokens;
    }
    return SWARM_OK;
}

swarm_status_t swarm_yield_merge(swarm_yield_t *into, const swarm_yield_t *from)
{
    if (!into || !from) return SWARM_ERR_ARG;
    if (!add_ok(into->tokens, from->tokens) || !add_ok(into->wasted, from->wasted) ||
        !add_ok(into->work_milli, from->work_milli) || !add_ok(into->tasks, from->tasks) ||
        !add_ok(into->passed, from->passed) || !add_ok(into->first_try, from->first_try))
        return SWARM_ERR_ARG;
    into->tokens += from->tokens;
    into->wasted += from->wasted;
    into->work_milli += from->work_milli;
    into->tasks += from->tasks;
    into->passed += from->passed;
    into->first_try += from->first_try;
    return SWARM_OK;
}

uint64_t swarm_yield_per_k(const swarm_yield_t *y)
{
    if (!y || y->tokens == 0) return 0;
    uint64_t hi, lo;
    mul64(y->work_milli, 1000u, &hi, &lo);
    return div128(hi, lo, y->tokens);
}

uint64_t swarm_yield_rate(const swarm_yield_t *y, uint64_t tokens_per_s_milli)
{
    if (!y || y->tokens == 0) return 0;
    uint64_t hi, lo;
    mul64(y->work_milli, tokens_per_s_milli, &hi, &lo);
    /* floor(x / 1000) first, in 128 bits, then by the tokens */
    uint64_t qlo = div128(hi % 1000u, lo, 1000u), qhi = hi / 1000u;
    return div128(qhi, qlo, y->tokens); /* saturates when it does not fit */
}

static uint32_t share_milli(uint64_t part, uint64_t whole)
{
    if (whole == 0) return 0;
    uint64_t hi, lo;
    mul64(part, 1000u, &hi, &lo);
    return (uint32_t) div128(hi, lo, whole);
}

uint32_t swarm_yield_first_try_milli(const swarm_yield_t *y)
{
    return y ? share_milli(y->first_try, y->tasks) : 0;
}

uint32_t swarm_yield_waste_milli(const swarm_yield_t *y)
{
    return y ? share_milli(y->wasted, y->tokens) : 0;
}

int swarm_yield_cmp(const swarm_yield_t *a, const swarm_yield_t *b)
{
    return swarm_yield_cmp_rate(a, 1, b, 1);
}

int swarm_yield_cmp_rate(const swarm_yield_t *a, uint64_t a_tps_milli, const swarm_yield_t *b,
                         uint64_t b_tps_milli)
{
    /* no tokens: yield 0 (work 0 over 1 token) */
    uint64_t wa = (a && a->tokens) ? a->work_milli : 0, ta = (a && a->tokens) ? a->tokens : 1;
    uint64_t wb = (b && b->tokens) ? b->work_milli : 0, tb = (b && b->tokens) ? b->tokens : 1;
    /* wa * sa / ta  vs  wb * sb / tb, cross-multiplied */
    return cmp_wide(mul3(wa, a_tps_milli, tb), mul3(wb, b_tps_milli, ta));
}
