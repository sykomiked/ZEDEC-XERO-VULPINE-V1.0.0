/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_gridlock.c — host tests for gridlock resolution.
 *
 * WHAT IT CHECKS: the textbook three-way gridlock (all settle together,
 * none alone), hand-built cases where the removal heuristic is provably
 * suboptimal (both LAST and LARGEST rules) and where FIFO costs value,
 * lexicographic priority versus plain value, four-valued status handling
 * (BOTH and NEITHER held and reported, FALSE dropped, FIFO blocking),
 * exact boundaries (balance exactly zero / exactly -credit afterwards),
 * agreement of branch and bound and the module's brute force with an
 * independent test-side brute force on hundreds of fixed-seed random tiny
 * instances in all four modes, validity of the proven upper bound under tiny
 * node budgets, determinism, larger random instances, overflow and every
 * error path.
 *
 * HONEST LIMITS: random instances are synthetic; the independent reference
 * enumerates all subsets, so it is limited to <= 10 payments.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "gridlock.h"

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

static gl_instance_t I;
static gl_workspace_t ws;
static gl_result_t rh, rs, rb;
static gl_opts_t O;

static uint64_t trng = 0x9E3779B97F4A7C15ull;
static uint32_t rnd(uint32_t n)
{
    trng ^= trng << 13;
    trng ^= trng >> 7;
    trng ^= trng << 17;
    return (uint32_t) (trng >> 33) % n;
}

static uint32_t pay(uint32_t f, uint32_t t, int64_t a, uint32_t prio, uint32_t seq)
{
    uint32_t idx = 999;
    CHECK(gl_add_payment(&I, f, t, a, prio, seq, &idx) == GL_OK);
    return idx;
}

/* Random amount 1..maxamt, then random class: sequenced, so every compiler
 * builds the same instances. */
static void rpay(uint32_t a, uint32_t b, uint32_t maxamt, uint32_t seq)
{
    int64_t amt = 1 + (int64_t) rnd(maxamt);
    uint32_t prio = rnd(GL_MAX_CLASSES);
    pay(a, b, amt, prio, seq);
}

static void opts(uint32_t fifo, uint32_t obj, uint32_t rm)
{
    CHECK(gl_opts_default(&O) == GL_OK);
    O.fifo = fifo;
    O.objective = obj;
    O.removal = rm;
}

/* Re-checks every invariant of a returned result independently. */
static void check_result(const gl_result_t *r)
{
    int64_t b[GL_MAX_PART], v = 0, s0 = 0, s1 = 0, key[GL_MAX_CLASSES] = {0}, kv[GL_MAX_CLASSES],
                            vv = -1;
    uint32_t i, p, cnt = 0, ok = 1;
    for (p = 0; p < I.num_parts; p++) b[p] = I.balance[p];
    for (i = 0; i < I.num_pay; i++)
        if (r->settle[i]) {
            b[I.from[i]] -= I.amount[i];
            b[I.to[i]] += I.amount[i];
            v += I.amount[i];
            key[O.objective == GL_OBJ_LEX_PRIORITY ? I.prio[i] : 0] += I.amount[i];
            cnt++;
            if (I.status[i] != GL_TRUE || r->disp[i] != GL_D_SETTLED) ok = 0;
        } else if (r->disp[i] == GL_D_SETTLED)
            ok = 0;
    for (p = 0; p < I.num_parts; p++) {
        if (b[p] < -I.credit[p] || b[p] != r->balance_after[p]) ok = 0;
        s0 += I.balance[p];
        s1 += b[p];
    }
    CHECK(ok);
    CHECK(s0 == s1);
    CHECK(v == r->value);
    CHECK(cnt == r->settled);
    CHECK(memcmp(key, r->key, sizeof key) == 0);
    CHECK(gl_key_cmp(r->ub_key, r->key, GL_MAX_CLASSES) >= 0);
    CHECK(r->ub_value >= r->value);
    CHECK(gl_verify(&I, &O, r->settle, NULL, kv, &vv) == GL_OK);
    CHECK(vv == r->value);
}

static void run3(void)
{
    CHECK(gl_heuristic(&I, &O, &ws, &rh) == GL_OK);
    check_result(&rh);
    CHECK(rh.quality == GL_HEURISTIC);
    CHECK(gl_solve(&I, &O, &ws, &rs) == GL_OK);
    check_result(&rs);
    CHECK(gl_brute_force(&I, &O, &ws, &rb) == GL_OK);
    check_result(&rb);
    CHECK(rb.quality == GL_OPTIMAL);
}

/* ---------------- independent reference ---------------- */

static int ref_elig(uint32_t i, uint32_t fifo)
{
    uint32_t j;
    if (I.status[i] != GL_TRUE) return 0;
    if (!fifo) return 1;
    for (j = 0; j < I.num_pay; j++)
        if (j != i && I.from[j] == I.from[i] &&
            (I.seq[j] < I.seq[i] || (I.seq[j] == I.seq[i] && j < i)) &&
            (I.status[j] == GL_BOTH || I.status[j] == GL_NEITHER))
            return 0;
    return 1;
}

static int ref_feasible(uint32_t mask, uint32_t fifo)
{
    int64_t b[GL_MAX_PART];
    uint32_t i, j, p;
    for (i = 0; i < I.num_pay; i++)
        if ((mask >> i) & 1u) {
            if (!ref_elig(i, fifo)) return 0;
            if (fifo)
                for (j = 0; j < I.num_pay; j++)
                    if (!((mask >> j) & 1u) && ref_elig(j, 1) && I.from[j] == I.from[i] &&
                        (I.seq[j] < I.seq[i] || (I.seq[j] == I.seq[i] && j < i)))
                        return 0;
        }
    for (p = 0; p < I.num_parts; p++) b[p] = I.balance[p] + I.credit[p];
    for (i = 0; i < I.num_pay; i++)
        if ((mask >> i) & 1u) {
            b[I.from[i]] -= I.amount[i];
            b[I.to[i]] += I.amount[i];
        }
    for (p = 0; p < I.num_parts; p++)
        if (b[p] < 0) return 0;
    return 1;
}

static void ref_opt(int64_t *best, int64_t *maxv)
{
    uint32_t m, i, c;
    for (c = 0; c < GL_MAX_CLASSES; c++) best[c] = 0;
    *maxv = 0;
    for (m = 0; m < (1u << I.num_pay); m++) {
        int64_t k[GL_MAX_CLASSES] = {0}, v = 0;
        if (!ref_feasible(m, O.fifo)) continue;
        for (i = 0; i < I.num_pay; i++)
            if ((m >> i) & 1u) {
                k[O.objective == GL_OBJ_LEX_PRIORITY ? I.prio[i] : 0] += I.amount[i];
                v += I.amount[i];
            }
        if (v > *maxv) *maxv = v;
        if (gl_key_cmp(k, best, GL_MAX_CLASSES) > 0) memcpy(best, k, sizeof k);
    }
}

/* ---------------- tests ---------------- */

static void test_textbook(void)
{
    uint32_t f, i, j;
    uint8_t s[GL_MAX_PAY];
    for (f = 0; f < 2; f++) {
        CHECK(gl_init(&I, 3) == GL_OK);
        pay(0, 1, 100, 0, 0);
        pay(1, 2, 100, 0, 0);
        pay(2, 0, 100, 0, 0);
        opts(f, GL_OBJ_VALUE, GL_RM_LAST);
        run3();
        CHECK(rh.value == 300 && rs.value == 300 && rb.value == 300);
        CHECK(rs.quality == GL_OPTIMAL && rs.ub_value == 300 && rs.ub_key[0] == 300);
        CHECK(rs.settled == 3 && rs.eligible == 3);
        CHECK(rs.balance_after[0] == 0 && rs.balance_after[1] == 0 && rs.balance_after[2] == 0);
        CHECK(rb.nodes == 8);
        /* no single payment and no pair can settle */
        for (i = 0; i < 3; i++) {
            memset(s, 0, sizeof s);
            s[i] = 1;
            CHECK(gl_verify(&I, &O, s, NULL, NULL, NULL) == GL_ERR_INFEASIBLE);
            for (j = i + 1; j < 3; j++) {
                s[j] = 1;
                CHECK(gl_verify(&I, &O, s, NULL, NULL, NULL) == GL_ERR_INFEASIBLE);
                s[j] = 0;
            }
        }
    }
}

static void test_heuristic_suboptimal(void)
{
    /* LAST: A(10) pays 6, 3, 4. Remove last (4) -> 9; optimum 6+4 = 10. */
    CHECK(gl_init(&I, 4) == GL_OK);
    CHECK(gl_set_balance(&I, 0, 10, 0) == GL_OK);
    pay(0, 1, 6, 0, 0);
    pay(0, 2, 3, 0, 1);
    pay(0, 3, 4, 0, 2);
    opts(0, GL_OBJ_VALUE, GL_RM_LAST);
    run3();
    CHECK(rh.value == 9 && rs.value == 10 && rb.value == 10);
    CHECK(rs.settle[0] == 1 && rs.settle[1] == 0 && rs.settle[2] == 1);
    CHECK(rh.settle[2] == 0 && rh.disp[2] == GL_D_QUEUED);
    CHECK(rh.nodes == 1);
    CHECK(rs.balance_after[0] == 0); /* exactly zero is allowed */
    /* FIFO: prefixes 6, 9, 13 -> 9, and the heuristic is optimal there */
    opts(1, GL_OBJ_VALUE, GL_RM_LAST);
    run3();
    CHECK(rh.value == 9 && rs.value == 9 && rb.value == 9);
    /* LARGEST: A(10) pays 6, 5, 4. Remove largest (6) -> 9; optimum 10. */
    CHECK(gl_init(&I, 4) == GL_OK);
    CHECK(gl_set_balance(&I, 0, 10, 0) == GL_OK);
    pay(0, 1, 6, 0, 0);
    pay(0, 2, 5, 0, 1);
    pay(0, 3, 4, 0, 2);
    opts(0, GL_OBJ_VALUE, GL_RM_LARGEST);
    run3();
    CHECK(rh.value == 9 && rs.value == 10 && rb.value == 10);
    CHECK(rh.settle[0] == 0 && rh.settle[1] == 1 && rh.settle[2] == 1);
    opts(0, GL_OBJ_VALUE, GL_RM_LAST); /* LAST removes 4 -> 11 > 10, then 5 -> 6 */
    run3();
    CHECK(rh.value == 6 && rs.value == 10);
    CHECK(rh.nodes == 2);
    /* the exact search's incumbent: LAST (6) then re-add of 4 (A ends at
     * exactly 0) = 10, better than LARGEST (9); the root bound min(15, 10)
     * then proves it optimal with one node */
    O.node_budget = 1;
    CHECK(gl_solve(&I, &O, &ws, &rs) == GL_OK);
    check_result(&rs);
    CHECK(rs.value == 10 && rs.quality == GL_OPTIMAL && rs.nodes == 1);
    CHECK(rs.settle[0] == 1 && rs.settle[1] == 0 && rs.settle[2] == 1);
}

static void test_cycle_trap(void)
{
    uint32_t rm;
    /* A->C 1 (first in A's queue), A->B 5, B->A 5, zero balances. */
    for (rm = 0; rm < 2; rm++) {
        CHECK(gl_init(&I, 3) == GL_OK);
        pay(0, 2, 1, 0, 0);
        pay(0, 1, 5, 0, 1);
        pay(1, 0, 5, 0, 0);
        opts(0, GL_OBJ_VALUE, rm);
        run3();
        CHECK(rh.value == 0 && rs.value == 10 && rb.value == 10);
        CHECK(rs.settle[0] == 0 && rs.settle[1] == 1 && rs.settle[2] == 1);
        CHECK(rh.nodes == 3);
        /* FIFO: A must pay C first, so nothing can settle */
        opts(1, GL_OBJ_VALUE, rm);
        run3();
        CHECK(rh.value == 0 && rs.value == 0 && rb.value == 0);
        CHECK(rs.quality == GL_OPTIMAL && rs.ub_value == 0);
    }
    /* with 1 unit of balance for A everything settles in both modes */
    CHECK(gl_set_balance(&I, 0, 1, 0) == GL_OK);
    opts(1, GL_OBJ_VALUE, GL_RM_LAST);
    run3();
    CHECK(rh.value == 11 && rs.value == 11);
    CHECK(rs.balance_after[0] == 0 && rs.balance_after[2] == 1);
}

static void test_lex(void)
{
    /* A(10): 6 (class 1), 5 (class 0), 4 (class 1). VALUE -> 10 (6+4);
     * LEX -> class 0 first: 5, then 4 in class 1 -> key (5,4). */
    CHECK(gl_init(&I, 4) == GL_OK);
    CHECK(gl_set_balance(&I, 0, 10, 0) == GL_OK);
    pay(0, 1, 6, 1, 0);
    pay(0, 2, 5, 0, 1);
    pay(0, 3, 4, 1, 2);
    opts(0, GL_OBJ_VALUE, GL_RM_LAST);
    run3();
    CHECK(rs.value == 10 && rs.key[0] == 10 && rs.key[1] == 0);
    opts(0, GL_OBJ_LEX_PRIORITY, GL_RM_LAST);
    run3();
    CHECK(rs.value == 9 && rs.key[0] == 5 && rs.key[1] == 4 && rs.key[2] == 0);
    CHECK(rb.key[0] == 5 && rb.key[1] == 4);
    CHECK(rb.ub_value == 10); /* brute: max feasible value regardless of class */
    CHECK(rs.ub_value >= 10);
    /* lex heuristic removes the lowest class first, latest first: 4, then 6 */
    CHECK(rh.key[0] == 5 && rh.key[1] == 0 && rh.value == 5 && rh.nodes == 2);
    /* FIFO + lex: prefixes 6 | 11 | 15 -> only (0,6) */
    opts(1, GL_OBJ_LEX_PRIORITY, GL_RM_LAST);
    run3();
    CHECK(rs.key[0] == 0 && rs.key[1] == 6 && rb.key[1] == 6);
}

static void test_boundaries(void)
{
    uint8_t s[GL_MAX_PAY] = {0};
    int64_t b[GL_MAX_PART], k[GL_MAX_CLASSES], v;
    CHECK(gl_init(&I, 2) == GL_OK);
    CHECK(gl_set_balance(&I, 0, 5, 0) == GL_OK);
    pay(0, 1, 5, 0, 0);
    opts(0, GL_OBJ_VALUE, GL_RM_LAST);
    run3();
    CHECK(rs.value == 5 && rs.balance_after[0] == 0 && rs.balance_after[1] == 5);
    CHECK(gl_set_balance(&I, 0, 4, 0) == GL_OK);
    run3();
    CHECK(rs.value == 0 && rh.value == 0 && rs.disp[0] == GL_D_QUEUED);
    s[0] = 1;
    CHECK(gl_verify(&I, &O, s, NULL, NULL, NULL) == GL_ERR_INFEASIBLE);
    CHECK(gl_set_balance(&I, 0, 4, 1) == GL_OK); /* credit exactly closes it */
    run3();
    CHECK(rs.value == 5 && rs.balance_after[0] == -1);
    CHECK(gl_verify(&I, &O, s, b, k, &v) == GL_OK);
    CHECK(b[0] == -1 && b[1] == 5 && k[0] == 5 && v == 5);
    CHECK(gl_set_balance(&I, 0, 0, 4) == GL_OK);
    run3();
    CHECK(rs.value == 0);
    CHECK(gl_set_balance(&I, 0, 0, 5) == GL_OK);
    run3();
    CHECK(rs.value == 5 && rs.balance_after[0] == -5);
    /* largest representable values */
    CHECK(gl_init(&I, 2) == GL_OK);
    CHECK(gl_set_balance(&I, 0, INT64_MAX - 10, 0) == GL_OK);
    pay(0, 1, 10, 0, 0);
    run3();
    CHECK(rs.value == 10 && rs.balance_after[0] == INT64_MAX - 20);
    CHECK(gl_set_balance(&I, 1, 1, 0) == GL_OK);
    CHECK(gl_check(&I) == GL_ERR_OVERFLOW);
    CHECK(gl_solve(&I, &O, &ws, &rs) == GL_ERR_OVERFLOW);
    CHECK(gl_set_balance(&I, 1, 0, 1) == GL_OK);
    CHECK(gl_check(&I) == GL_ERR_OVERFLOW);
    CHECK(gl_set_balance(&I, 1, 0, 0) == GL_OK);
    CHECK(gl_check(&I) == GL_OK);
    pay(1, 0, 1, 0, 0);
    CHECK(gl_check(&I) == GL_ERR_OVERFLOW);
    /* empty queue */
    CHECK(gl_init(&I, 1) == GL_OK);
    run3();
    CHECK(rs.value == 0 && rs.quality == GL_OPTIMAL && rb.nodes == 1 && rs.eligible == 0);
}

static void test_paraconsistent(void)
{
    uint8_t s[GL_MAX_PAY] = {0};
    uint32_t a, b, p0, p1;
    /* join table: knowledge-order join == bitwise OR */
    for (a = 0; a < 4; a++)
        for (b = 0; b < 4; b++) CHECK(gl_lp_join((gl_lp_t) a, (gl_lp_t) b) == (gl_lp_t) (a | b));
    CHECK(gl_lp_join(GL_TRUE, GL_FALSE) == GL_BOTH);
    CHECK(gl_lp_join(GL_NEITHER, GL_FALSE) == GL_FALSE);
    CHECK(GL_NEITHER == 0 && GL_TRUE == 1 && GL_FALSE == 2 && GL_BOTH == 3);

    /* two-way gridlock; one source says A->B was cancelled -> BOTH */
    CHECK(gl_init(&I, 2) == GL_OK);
    p0 = pay(0, 1, 5, 0, 0);
    p1 = pay(1, 0, 5, 0, 0);
    opts(0, GL_OBJ_VALUE, GL_RM_LAST);
    run3();
    CHECK(rs.value == 10);
    CHECK(gl_report_status(&I, p0, GL_TRUE) == GL_OK); /* agreeing source */
    CHECK(I.status[p0] == GL_TRUE);
    CHECK(gl_report_status(&I, p0, GL_FALSE) == GL_OK);
    CHECK(I.status[p0] == GL_BOTH);
    run3();
    CHECK(rs.value == 0 && rh.value == 0 && rb.value == 0);
    CHECK(rs.disp[p0] == GL_D_HELD_BOTH && rs.held_both == 1 && rs.eligible == 1);
    CHECK(rs.disp[p1] == GL_D_QUEUED);
    s[p0] = 1;
    s[p1] = 1;
    CHECK(gl_verify(&I, &O, s, NULL, NULL, NULL) == GL_ERR_HELD); /* never by a guess */
    /* NEITHER: no evidence -> held */
    CHECK(gl_set_status(&I, p0, GL_NEITHER) == GL_OK);
    run3();
    CHECK(rs.disp[p0] == GL_D_HELD_NEITHER && rs.held_neither == 1 && rs.held_both == 0);
    CHECK(gl_verify(&I, &O, s, NULL, NULL, NULL) == GL_ERR_HELD);
    /* FALSE: cancelled, dropped */
    CHECK(gl_set_status(&I, p0, GL_FALSE) == GL_OK);
    run3();
    CHECK(rs.disp[p0] == GL_D_CANCELLED && rs.cancelled == 1);
    CHECK(gl_verify(&I, &O, s, NULL, NULL, NULL) == GL_ERR_HELD);

    /* FIFO blocking: A(10) queue = [held 3, ok 4]; free mode settles 4,
     * FIFO mode holds both (4 would jump a disputed payment). */
    CHECK(gl_init(&I, 3) == GL_OK);
    CHECK(gl_set_balance(&I, 0, 10, 0) == GL_OK);
    p0 = pay(0, 1, 3, 0, 0);
    p1 = pay(0, 2, 4, 0, 1);
    CHECK(gl_report_status(&I, p0, GL_FALSE) == GL_OK);
    opts(0, GL_OBJ_VALUE, GL_RM_LAST);
    run3();
    CHECK(rs.value == 4 && rs.disp[p1] == GL_D_SETTLED);
    opts(1, GL_OBJ_VALUE, GL_RM_LAST);
    run3();
    CHECK(rs.value == 0 && rs.disp[p1] == GL_D_BLOCKED_FIFO && rs.blocked_fifo == 1);
    CHECK(rs.eligible == 0 && rh.value == 0 && rb.value == 0);
    memset(s, 0, sizeof s);
    s[p1] = 1;
    CHECK(gl_verify(&I, &O, s, NULL, NULL, NULL) == GL_ERR_HELD);
    /* cancelled ahead does NOT block */
    CHECK(gl_set_status(&I, p0, GL_FALSE) == GL_OK);
    run3();
    CHECK(rs.value == 4 && rs.cancelled == 1 && rs.blocked_fifo == 0);
    /* a held payment BEHIND does not block the one ahead */
    CHECK(gl_set_status(&I, p0, GL_TRUE) == GL_OK);
    CHECK(gl_set_status(&I, p1, GL_BOTH) == GL_OK);
    run3();
    CHECK(rs.value == 3 && rs.disp[p0] == GL_D_SETTLED && rs.disp[p1] == GL_D_HELD_BOTH);
}

static void test_fifo_order(void)
{
    uint8_t s[GL_MAX_PAY] = {0};
    uint32_t p0, p1, p2;
    /* seq decides queue order, not insertion order; equal seq -> index */
    CHECK(gl_init(&I, 4) == GL_OK);
    CHECK(gl_set_balance(&I, 0, 7, 0) == GL_OK);
    p0 = pay(0, 1, 6, 0, 5); /* last in queue */
    p1 = pay(0, 2, 3, 0, 1);
    p2 = pay(0, 3, 4, 0, 1); /* same seq as p1, later index */
    opts(1, GL_OBJ_VALUE, GL_RM_LAST);
    run3();
    CHECK(rs.value == 7 && rs.settle[p1] && rs.settle[p2] && !rs.settle[p0]);
    CHECK(rh.value == 7);
    s[p2] = 1;
    CHECK(gl_verify(&I, &O, s, NULL, NULL, NULL) == GL_ERR_FIFO);
    s[p1] = 1;
    CHECK(gl_verify(&I, &O, s, NULL, NULL, NULL) == GL_OK);
    s[p1] = 0;
    s[p0] = 1;
    CHECK(gl_verify(&I, &O, s, NULL, NULL, NULL) == GL_ERR_FIFO);
    opts(0, GL_OBJ_VALUE, GL_RM_LAST); /* free: 6 alone or 3+4 = 7 */
    run3();
    CHECK(rs.value == 7);
}

static uint32_t subopt_count, fifo_less_count;

static void random_tiny(uint32_t fifo, uint32_t obj, uint32_t rm, uint32_t rounds)
{
    uint32_t r, i, p;
    for (r = 0; r < rounds; r++) {
        int64_t ref[GL_MAX_CLASSES], maxv, fv = 0;
        uint32_t np = 2 + rnd(5), n = 1 + rnd(10), budget;
        CHECK(gl_init(&I, np) == GL_OK);
        for (p = 0; p < np; p++) {
            int64_t bal = rnd(4) ? (int64_t) rnd(16) : 0;
            int64_t cr = rnd(4) == 0 ? (int64_t) rnd(6) : 0;
            CHECK(gl_set_balance(&I, p, bal, cr) == GL_OK);
        }
        for (i = 0; i < n; i++) {
            uint32_t a = rnd(np), b = rnd(np - 1u), st = rnd(12);
            if (b >= a) b++;
            rpay(a, b, 20, rnd(6));
            if (st == 0) CHECK(gl_set_status(&I, i, GL_BOTH) == GL_OK);
            if (st == 1) CHECK(gl_set_status(&I, i, GL_FALSE) == GL_OK);
            if (st == 2) CHECK(gl_set_status(&I, i, GL_NEITHER) == GL_OK);
        }
        opts(fifo, obj, rm);
        ref_opt(ref, &maxv);
        run3();
        CHECK(memcmp(rb.key, ref, sizeof ref) == 0);
        CHECK(memcmp(rs.key, ref, sizeof ref) == 0);
        CHECK(rs.quality == GL_OPTIMAL);
        CHECK(gl_key_cmp(rh.key, ref, GL_MAX_CLASSES) <= 0);
        CHECK(rb.ub_value == maxv);
        CHECK(rs.ub_value >= maxv);
        if (obj == GL_OBJ_VALUE) CHECK(rs.value == maxv && rs.ub_value == maxv);
        if (gl_key_cmp(rh.key, ref, GL_MAX_CLASSES) < 0) subopt_count++;
        if (fifo) CHECK(memcmp(rh.key, ref, sizeof ref) == 0 && rh.value == maxv); /* theorem */
        /* proven bound under tiny budgets */
        for (budget = 1; budget <= 6; budget++) {
            O.node_budget = budget;
            CHECK(gl_solve(&I, &O, &ws, &rs) == GL_OK);
            check_result(&rs);
            CHECK(gl_key_cmp(rs.ub_key, ref, GL_MAX_CLASSES) >= 0);
            CHECK(gl_key_cmp(rs.key, ref, GL_MAX_CLASSES) <= 0);
            CHECK(rs.ub_value >= maxv);
            if (fifo) { /* greatest fixpoint: no search, budget unused */
                CHECK(rs.quality == GL_OPTIMAL && rs.nodes == rh.nodes);
                CHECK(memcmp(rs.settle, rh.settle, sizeof rs.settle) == 0);
                CHECK(rs.ub_value == maxv && rs.value == maxv);
                continue;
            }
            CHECK(rs.nodes <= budget + 1u);
            if (rs.quality == GL_OPTIMAL)
                CHECK(memcmp(rs.key, ref, sizeof ref) == 0);
            else
                CHECK(rs.quality == GL_FEASIBLE && rs.nodes == budget + 1u);
            CHECK(gl_key_cmp(rs.key, rh.key, GL_MAX_CLASSES) >= 0);
        }
        /* FIFO can never beat free mode */
        if (fifo && obj == GL_OBJ_VALUE) {
            O.fifo = 0;
            O.node_budget = 1000000u;
            CHECK(gl_solve(&I, &O, &ws, &rs) == GL_OK);
            fv = rs.value;
            CHECK(fv >= maxv);
            if (fv > maxv) fifo_less_count++;
        }
    }
}

/* 11..15 payments: branch and bound against the module brute force (which
 * random_tiny checks against the independent reference), with a sweep of
 * node budgets for the proven bound. */
static void random_mid(uint32_t rounds)
{
    uint32_t r, i, p, budget;
    for (r = 0; r < rounds; r++) {
        uint32_t np = 3 + rnd(6), n = 11 + rnd(5);
        CHECK(gl_init(&I, np) == GL_OK);
        for (p = 0; p < np; p++)
            CHECK(gl_set_balance(&I, p, rnd(3) ? (int64_t) rnd(12) : 0, 0) == GL_OK);
        for (i = 0; i < n; i++) {
            uint32_t a = rnd(np), b = rnd(np - 1u);
            if (b >= a) b++;
            rpay(a, b, 15, rnd(8));
        }
        opts(r & 1u, (r >> 1) & 1u, (r >> 2) & 1u);
        run3();
        CHECK(memcmp(rs.key, rb.key, sizeof rb.key) == 0 && rs.quality == GL_OPTIMAL);
        CHECK(rs.ub_value >= rb.ub_value);
        if (O.fifo) CHECK(memcmp(rh.key, rb.key, sizeof rb.key) == 0 && rh.value == rb.ub_value);
        for (budget = 1; budget < 200; budget += 1 + budget / 4) {
            O.node_budget = budget;
            CHECK(gl_solve(&I, &O, &ws, &rs) == GL_OK);
            CHECK(gl_key_cmp(rs.ub_key, rb.key, GL_MAX_CLASSES) >= 0);
            CHECK(rs.ub_value >= rb.ub_value);
            if (rs.quality == GL_OPTIMAL) CHECK(memcmp(rs.key, rb.key, sizeof rb.key) == 0);
        }
    }
}

static void random_large(void)
{
    uint32_t r, i, p, s0 = 0;
    for (r = 0; r < 24; r++) {
        uint32_t np = 10 + rnd(55), n = 30 + rnd(227);
        uint8_t keep[GL_MAX_PAY];
        uint64_t nodes;
        CHECK(gl_init(&I, np) == GL_OK);
        for (p = 0; p < np; p++) CHECK(gl_set_balance(&I, p, rnd(300), 0) == GL_OK);
        for (i = 0; i < n; i++) {
            uint32_t a = rnd(np), b = rnd(np - 1u);
            if (b >= a) b++;
            rpay(a, b, 1000, i);
            if (rnd(40) == 0) CHECK(gl_report_status(&I, i, GL_FALSE) == GL_OK);
        }
        opts(r & 1u, (r >> 1) & 1u, (r >> 2) & 1u);
        O.node_budget = 20000;
        CHECK(gl_heuristic(&I, &O, &ws, &rh) == GL_OK);
        check_result(&rh);
        CHECK(gl_solve(&I, &O, &ws, &rs) == GL_OK);
        check_result(&rs);
        CHECK(gl_key_cmp(rs.key, rh.key, GL_MAX_CLASSES) >= 0);
        CHECK(rs.quality == GL_OPTIMAL || rs.quality == GL_FEASIBLE);
        if (rs.quality == GL_OPTIMAL) s0++;
        memcpy(keep, rs.settle, sizeof keep);
        nodes = rs.nodes;
        CHECK(gl_solve(&I, &O, &ws, &rs) == GL_OK); /* determinism */
        CHECK(memcmp(keep, rs.settle, sizeof keep) == 0 && nodes == rs.nodes);
        CHECK(gl_brute_force(&I, &O, &ws, &rb) == GL_ERR_TOO_BIG);
    }
    printf("large random: %u of 24 proven optimal within 20000 nodes\n", s0);
}

/* Golden regression values for the free-mode search (node counts pin the
 * branching order, dominance rule and bounds, which do not change answers). */
static void test_golden(void)
{
    static const int64_t gold_val[2] = {840, 828};
    static const uint64_t gold_nodes[2] = {9437, 3147};
    uint32_t obj, i, p;
    for (obj = 0; obj < 2; obj++) {
        trng = 0xD1B54A32D192ED03ull;
        CHECK(gl_init(&I, 12) == GL_OK);
        for (p = 0; p < 12; p++) CHECK(gl_set_balance(&I, p, rnd(40), 0) == GL_OK);
        for (i = 0; i < 40; i++) {
            uint32_t a = rnd(12), b = rnd(11);
            if (b >= a) b++;
            rpay(a, b, 60, i);
        }
        opts(0, obj, GL_RM_LAST);
        CHECK(gl_solve(&I, &O, &ws, &rs) == GL_OK);
        CHECK(gl_heuristic(&I, &O, &ws, &rh) == GL_OK);
        printf("golden obj %u: value %lld nodes %llu (heuristic %lld)\n", obj, (long long) rs.value,
               (unsigned long long) rs.nodes, (long long) rh.value);
        CHECK(rs.quality == GL_OPTIMAL);
        CHECK(rs.value == gold_val[obj] && rs.nodes == gold_nodes[obj]);
        CHECK(rh.value == (obj ? 425 : 472));
    }
}

static void test_errors(void)
{
    uint8_t s[GL_MAX_PAY] = {0};
    uint32_t i;
    int64_t k[GL_MAX_CLASSES] = {1, 2, 3, 4}, k2[GL_MAX_CLASSES] = {1, 2, 3, 4};
    CHECK(gl_init(NULL, 3) == GL_ERR_NULL);
    CHECK(gl_init(&I, 0) == GL_ERR_RANGE);
    CHECK(gl_init(&I, GL_MAX_PART + 1u) == GL_ERR_RANGE);
    CHECK(gl_init(&I, GL_MAX_PART) == GL_OK);
    CHECK(gl_init(&I, 3) == GL_OK);
    CHECK(gl_set_balance(NULL, 0, 1, 0) == GL_ERR_NULL);
    CHECK(gl_set_balance(&I, 3, 1, 0) == GL_ERR_RANGE);
    CHECK(gl_set_balance(&I, 0, -1, 0) == GL_ERR_BALANCE);
    CHECK(gl_set_balance(&I, 0, 0, -1) == GL_ERR_BALANCE);
    CHECK(gl_add_payment(NULL, 0, 1, 1, 0, 0, NULL) == GL_ERR_NULL);
    CHECK(gl_add_payment(&I, 3, 1, 1, 0, 0, NULL) == GL_ERR_RANGE);
    CHECK(gl_add_payment(&I, 0, 3, 1, 0, 0, NULL) == GL_ERR_RANGE);
    CHECK(gl_add_payment(&I, 1, 1, 1, 0, 0, NULL) == GL_ERR_SELF);
    CHECK(gl_add_payment(&I, 0, 1, 0, 0, 0, NULL) == GL_ERR_AMOUNT);
    CHECK(gl_add_payment(&I, 0, 1, -5, 0, 0, NULL) == GL_ERR_AMOUNT);
    CHECK(gl_add_payment(&I, 0, 1, 1, GL_MAX_CLASSES, 0, NULL) == GL_ERR_PARAM);
    CHECK(gl_add_payment(&I, 0, 1, 1, GL_MAX_CLASSES - 1u, 0, NULL) == GL_OK); /* NULL idx ok */
    CHECK(I.num_pay == 1);
    CHECK(gl_report_status(NULL, 0, GL_TRUE) == GL_ERR_NULL);
    CHECK(gl_report_status(&I, 1, GL_TRUE) == GL_ERR_PARAM);
    CHECK(gl_report_status(&I, 0, (gl_lp_t) 4) == GL_ERR_PARAM);
    CHECK(gl_set_status(NULL, 0, GL_TRUE) == GL_ERR_NULL);
    CHECK(gl_set_status(&I, 1, GL_TRUE) == GL_ERR_PARAM);
    CHECK(gl_set_status(&I, 0, (gl_lp_t) 4) == GL_ERR_PARAM);
    for (i = 1; i < GL_MAX_PAY; i++) CHECK(gl_add_payment(&I, 0, 1, 1, 0, i, NULL) == GL_OK);
    CHECK(gl_add_payment(&I, 0, 1, 1, 0, 0, NULL) == GL_ERR_FULL);
    CHECK(gl_check(&I) == GL_OK);
    opts(0, GL_OBJ_VALUE, GL_RM_LAST);
    CHECK(gl_brute_force(&I, &O, &ws, &rb) == GL_ERR_TOO_BIG);
    CHECK(gl_init(&I, 3) == GL_OK);
    for (i = 0; i <= GL_BRUTE_MAX; i++) pay(0, 1, 1, 0, i);
    I.status[GL_BRUTE_MAX] = GL_FALSE;
    CHECK(gl_brute_force(&I, &O, &ws, &rb) == GL_OK); /* exactly 20 eligible */
    CHECK(rb.nodes == (1u << GL_BRUTE_MAX) && rb.value == 0);
    I.status[GL_BRUTE_MAX] = GL_TRUE;
    CHECK(gl_brute_force(&I, &O, &ws, &rb) == GL_ERR_TOO_BIG);

    /* gl_check on corrupted instances */
    CHECK(gl_check(NULL) == GL_ERR_NULL);
    CHECK(gl_init(&I, 3) == GL_OK);
    pay(0, 1, 5, 0, 0);
    I.num_parts = 0;
    CHECK(gl_check(&I) == GL_ERR_RANGE);
    CHECK(gl_set_balance(&I, 0, 1, 0) == GL_ERR_RANGE);
    CHECK(gl_add_payment(&I, 0, 1, 1, 0, 0, NULL) == GL_ERR_RANGE);
    I.num_parts = GL_MAX_PART + 1u;
    CHECK(gl_check(&I) == GL_ERR_RANGE);
    I.num_parts = 3;
    I.num_pay = GL_MAX_PAY + 1u;
    CHECK(gl_check(&I) == GL_ERR_RANGE);
    CHECK(gl_report_status(&I, GL_MAX_PAY, GL_TRUE) == GL_ERR_PARAM);
    CHECK(gl_set_status(&I, GL_MAX_PAY, GL_TRUE) == GL_ERR_PARAM);
    I.num_pay = 1;
    I.balance[2] = -1;
    CHECK(gl_check(&I) == GL_ERR_BALANCE);
    I.balance[2] = 0;
    I.credit[2] = -1;
    CHECK(gl_check(&I) == GL_ERR_BALANCE);
    I.credit[2] = 0;
    I.to[0] = 3;
    CHECK(gl_check(&I) == GL_ERR_RANGE);
    I.to[0] = 1;
    I.from[0] = 3;
    CHECK(gl_check(&I) == GL_ERR_RANGE);
    I.from[0] = 1;
    CHECK(gl_check(&I) == GL_ERR_SELF);
    I.from[0] = 0;
    I.amount[0] = 0;
    CHECK(gl_check(&I) == GL_ERR_AMOUNT);
    I.amount[0] = 5;
    I.prio[0] = GL_MAX_CLASSES;
    CHECK(gl_check(&I) == GL_ERR_PARAM);
    I.prio[0] = 0;
    I.status[0] = 4;
    CHECK(gl_check(&I) == GL_ERR_PARAM);
    CHECK(gl_heuristic(&I, &O, &ws, &rh) == GL_ERR_PARAM);
    I.status[0] = GL_TRUE;
    CHECK(gl_check(&I) == GL_OK);

    /* options */
    CHECK(gl_opts_default(NULL) == GL_ERR_NULL);
    CHECK(gl_opts_check(NULL) == GL_ERR_NULL);
    opts(0, 0, 0);
    CHECK(O.node_budget == 1000000u && gl_opts_check(&O) == GL_OK);
    O.fifo = 2;
    CHECK(gl_opts_check(&O) == GL_ERR_PARAM);
    CHECK(gl_solve(&I, &O, &ws, &rs) == GL_ERR_PARAM);
    CHECK(gl_verify(&I, &O, s, NULL, NULL, NULL) == GL_ERR_PARAM);
    opts(0, 2, 0);
    CHECK(gl_opts_check(&O) == GL_ERR_PARAM);
    opts(0, 0, 2);
    CHECK(gl_opts_check(&O) == GL_ERR_PARAM);
    opts(0, 0, 0);
    O.node_budget = 0;
    CHECK(gl_opts_check(&O) == GL_ERR_PARAM);
    CHECK(gl_brute_force(&I, &O, &ws, &rb) == GL_ERR_PARAM);
    opts(1, 1, 1);
    CHECK(gl_opts_check(&O) == GL_OK);

    /* NULL arguments to solvers and verify */
    CHECK(gl_heuristic(NULL, &O, &ws, &rh) == GL_ERR_NULL);
    CHECK(gl_heuristic(&I, NULL, &ws, &rh) == GL_ERR_NULL);
    CHECK(gl_heuristic(&I, &O, NULL, &rh) == GL_ERR_NULL);
    CHECK(gl_heuristic(&I, &O, &ws, NULL) == GL_ERR_NULL);
    CHECK(gl_solve(NULL, &O, &ws, &rs) == GL_ERR_NULL);
    CHECK(gl_solve(&I, &O, &ws, NULL) == GL_ERR_NULL);
    CHECK(gl_brute_force(&I, &O, NULL, &rb) == GL_ERR_NULL);
    CHECK(gl_verify(NULL, &O, s, NULL, NULL, NULL) == GL_ERR_NULL);
    CHECK(gl_verify(&I, NULL, s, NULL, NULL, NULL) == GL_ERR_NULL);
    CHECK(gl_verify(&I, &O, NULL, NULL, NULL, NULL) == GL_ERR_NULL);
    s[0] = 2;
    CHECK(gl_verify(&I, &O, s, NULL, NULL, NULL) == GL_ERR_PARAM);
    s[0] = 0;
    CHECK(gl_verify(&I, &O, s, NULL, NULL, NULL) == GL_OK);
    I.amount[0] = 0;
    CHECK(gl_verify(&I, &O, s, NULL, NULL, NULL) == GL_ERR_AMOUNT);

    /* key comparison */
    CHECK(gl_key_cmp(k, k2, 4) == 0);
    k2[3] = 5;
    CHECK(gl_key_cmp(k, k2, 4) == -1 && gl_key_cmp(k2, k, 4) == 1);
    CHECK(gl_key_cmp(k, k2, 3) == 0);
    k2[0] = 0;
    CHECK(gl_key_cmp(k, k2, 4) == 1 && gl_key_cmp(k2, k, 1) == -1);
    CHECK(gl_key_cmp(k, k2, 0) == 0);
}

int main(void)
{
    uint32_t f, obj, rm;
    test_textbook();
    test_heuristic_suboptimal();
    test_cycle_trap();
    test_lex();
    test_boundaries();
    test_paraconsistent();
    test_fifo_order();
    for (f = 0; f < 2; f++)
        for (obj = 0; obj < 2; obj++)
            for (rm = 0; rm < 2; rm++) random_tiny(f, obj, rm, 75);
    printf("tiny random: heuristic strictly suboptimal in %u of 300 free-mode runs; FIFO below "
           "free in %u of 150\n",
           subopt_count, fifo_less_count);
    CHECK(subopt_count > 0);
    CHECK(fifo_less_count > 0);
    test_golden();
    random_mid(240);
    random_large();
    test_errors();
    printf("%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
