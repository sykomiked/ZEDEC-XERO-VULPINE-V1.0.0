/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_swarm_props.c — property tests for the swarm economy: the Fibonacci
 * budget (swarm_budget.c), the token market (swarm_market.c), the witness
 * ledger (swarm_ledger.c) and the emotional reserve (swarm_reserve.c).
 *
 * test_swarm_budget and test_swarm_economy check worked examples. This file
 * runs thousands of random swarms through whole cycles and checks, after
 * every step, the invariants the headers promise:
 *   - money is conserved: Financial holdings + pot + levied = money supply,
 *     summed here, not read from swarm_market_conserved;
 *   - nothing mints: the money supply moves only when a model joins, by
 *     exactly its endowment, never on a cycle, a credit or a settlement;
 *   - settlement pays out exactly the pot, by the M6 rule, and the wealth
 *     cap M7 leaves nobody above it;
 *   - every cycle's allotments sum to exactly T (R5), split as R1-R4 say,
 *     computed here by an independent largest-remainder reference;
 *   - the ledger settles a cycle exactly when every rule holds, and the
 *     reserve audit catches every tampered field.
 * Expected values come from the header rules, computed in 128-bit integers. */
#include <stdio.h>
#include <string.h>
#include "swarm_budget.h"
#include "swarm_emotion.h"
#include "swarm_market.h"
#include "swarm_ledger.h"
#include "swarm_reserve.h"

static int failures = 0;
static unsigned long checks = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            if (failures < 40) printf("  FAIL: %s (line %d)\n", msg, __LINE__);                    \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

typedef unsigned __int128 u128;
static uint64_t rs = 0xD1B54A32D192ED03ULL;
static uint64_t r64(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return rs;
}
static uint64_t rnd(uint64_t n)
{
    return n ? r64() % n : 0;
}

/* -------------------------------------------------- references */

static uint64_t ref_fib(uint32_t n)
{
    uint64_t a = 0, b = 1; /* F(0), F(1) */
    for (uint32_t i = 0; i < n; i++) {
        uint64_t c = a + b;
        a = b;
        b = c;
    }
    return a;
}

/* largest remainder: floor shares, then +1 to the largest remainders, ties
 * to the lower index, zero weights never take one */
static void ref_lr(uint64_t total, const uint64_t *w, uint32_t n, uint64_t *out)
{
    u128 sum = 0;
    for (uint32_t i = 0; i < n; i++) sum += w[i];
    u128 rem[SWARM_MAX_MODELS];
    uint64_t given = 0;
    for (uint32_t i = 0; i < n; i++) {
        out[i] = 0;
        rem[i] = 0;
        if (sum == 0 || total == 0) continue;
        u128 p = (u128) total * w[i];
        out[i] = (uint64_t) (p / sum);
        rem[i] = p % sum;
        given += out[i];
    }
    if (sum == 0 || total == 0) return;
    uint64_t left = total - given;
    int taken[SWARM_MAX_MODELS] = {0};
    while (left--) {
        uint32_t best = n;
        for (uint32_t i = 0; i < n; i++)
            if (!taken[i] && w[i] && (best == n || rem[i] > rem[best])) best = i;
        out[best]++;
        taken[best] = 1;
    }
}

static uint64_t md(uint64_t a, uint64_t b, uint64_t c)
{
    return (uint64_t) ((u128) a * b / c);
}

/* R1-R5 for a budget whose real total is real_total */
static void ref_allot(const swarm_budget_t *b, uint64_t real_total, uint64_t *allot)
{
    uint64_t w[SWARM_MAX_LEVELS] = {0}, lb[SWARM_MAX_LEVELS];
    for (uint32_t i = 0; i < b->num_slots; i++)
        if (b->slots[i].active)
            w[b->slots[i].level] = ref_fib(b->num_levels - b->slots[i].level + 1);
    ref_lr(real_total, w, b->num_levels, lb);
    for (uint32_t i = 0; i < b->num_slots; i++) allot[i] = 0;
    for (uint32_t d = 0; d < b->num_levels; d++) {
        uint32_t idx[SWARM_MAX_MODELS], n = 0;
        for (uint32_t i = 0; i < b->num_slots; i++)
            if (b->slots[i].level == d && b->slots[i].active) idx[n++] = i;
        for (uint32_t k = 0; k < n; k++) allot[idx[k]] = lb[d] / n + (k < lb[d] % n ? 1 : 0);
    }
}

static uint64_t money_sum(const swarm_market_t *m)
{
    uint64_t s = m->pot + m->levied;
    for (uint32_t k = 0; k < m->n; k++) s += m->t[k].cap[SWARM_CAP_FINANCIAL];
    return s;
}

/* -------------------------------------------------- budget */

static void test_arith(void)
{
    for (uint32_t n = 0; n <= 95; n++)
        CHECK(swarm_fib(n) == (n == 0 || n > 93 ? 0 : ref_fib(n)), "fib = F(n), 0 outside 1..93");
    static const uint32_t capw[SWARM_MAX_LEVELS] = {1, 2, 3, 5, 8, 13, 21, 34};
    for (uint32_t d = 0; d < SWARM_MAX_LEVELS; d++)
        CHECK(swarm_level_capacity(d) == capw[d], "R1 capacity F(d+2)");
    CHECK(swarm_level_capacity(SWARM_MAX_LEVELS) == 0, "no level 8");
    for (uint32_t L = 0; L <= SWARM_MAX_LEVELS + 1; L++)
        for (uint32_t d = 0; d <= SWARM_MAX_LEVELS; d++)
            CHECK(swarm_level_weight(d, L) ==
                      (L >= 1 && L <= SWARM_MAX_LEVELS && d < L ? ref_fib(L - d + 1) : 0),
                  "R2 weight F(L-d+1)");
    CHECK(swarm_level_weight(0, 4) == 5 && swarm_level_weight(3, 4) == 1, "L=4 weights 5..1");

    for (int i = 0; i < 200000; i++) {
        uint64_t a = r64() >> rnd(64), b = r64() >> rnd(64), c = r64() >> rnd(64), r = 7;
        u128 p = (u128) a * b;
        uint64_t q = swarm_muldiv(a, b, c, &r);
        if (c == 0)
            CHECK(q == 0 && r == 0, "muldiv by 0 is 0 rem 0");
        else if (p / c > UINT64_MAX)
            CHECK(q == UINT64_MAX && r == 0, "muldiv overflow saturates");
        else
            CHECK(q == (uint64_t) (p / c) && r == (uint64_t) (p % c), "muldiv = 128-bit a*b/c");
        CHECK(swarm_muldiv(a, b, c, 0) == q, "muldiv without rem");
    }

    for (int i = 0; i < 40000; i++) {
        uint32_t n = (uint32_t) rnd(SWARM_MAX_MODELS + 1);
        uint64_t w[SWARM_MAX_MODELS], got[SWARM_MAX_MODELS], want[SWARM_MAX_MODELS];
        uint64_t total = r64() >> rnd(64);
        if (rnd(4) == 0) total = rnd(n + 3);
        for (uint32_t k = 0; k < n; k++) w[k] = rnd(3) == 0 ? 0 : (r64() >> (34 + rnd(30)));
        swarm_split_lr(total, w, n, got);
        ref_lr(total, w, n, want);
        CHECK(memcmp(got, want, n * sizeof got[0]) == 0, "split_lr = largest-remainder reference");
        uint64_t s = 0, any = 0;
        for (uint32_t k = 0; k < n; k++) {
            s += got[k];
            any |= w[k];
            if (!w[k]) CHECK(got[k] == 0, "zero weight takes nothing");
        }
        CHECK(s == (any ? total : 0), "split_lr gives out exactly the total");
    }
}

static void test_budget(void)
{
    swarm_budget_t b;
    CHECK(swarm_budget_init(0, 1, 1) == SWARM_ERR_ARG, "init NULL");
    CHECK(swarm_budget_init(&b, 0, 1) == SWARM_ERR_ARG, "init 0 levels");
    CHECK(swarm_budget_init(&b, SWARM_MAX_LEVELS + 1, 1) == SWARM_ERR_ARG, "init 9 levels");
    CHECK(swarm_budget_init(&b, 1, SWARM_MAX_TOKENS_PER_CYCLE + 1) == SWARM_ERR_ARG,
          "init T > max");
    CHECK(swarm_budget_init(&b, SWARM_MAX_LEVELS, SWARM_MAX_TOKENS_PER_CYCLE) == SWARM_OK,
          "init max");
    CHECK(swarm_budget_set_rate(&b, SWARM_MAX_TOKENS_PER_CYCLE + 1) == SWARM_ERR_ARG, "rate > max");
    CHECK(swarm_budget_set_rate(0, 1) == SWARM_ERR_ARG, "rate NULL");
    uint64_t g = 9;
    CHECK(swarm_budget_consume(&b, 1, 1, &g) == SWARM_ERR_NO_CYCLE && g == 0, "no cycle open");
    CHECK(swarm_budget_consume(&b, 1, 1, 0) == SWARM_ERR_ARG, "consume NULL granted");
    CHECK(swarm_budget_end_cycle(&b) == SWARM_ERR_NO_CYCLE, "end without a cycle");
    CHECK(swarm_budget_begin_cycle(0) == SWARM_ERR_ARG &&
              swarm_budget_end_cycle(0) == SWARM_ERR_ARG,
          "cycle NULL");
    CHECK(swarm_budget_begin_cycle_real(&b, b.tokens_per_cycle + 1) == SWARM_ERR_ARG,
          "real total above T refused");
    CHECK(swarm_budget_set_active(&b, 77, true) == SWARM_ERR_NO_MODEL, "activate unknown");
    CHECK(swarm_budget_set_active(0, 77, true) == SWARM_ERR_ARG, "activate NULL");
    CHECK(swarm_budget_remaining(&b, 1) == 0 && swarm_budget_remaining(0, 1) == 0,
          "remaining closed");

    /* fill every level to capacity: 1+2+...+34 = 87 > 64 slots, so the slot
     * table fills first */
    uint32_t id = 1, ok = 0;
    for (uint32_t d = 0; d < SWARM_MAX_LEVELS; d++)
        for (uint32_t k = 0; k < swarm_level_capacity(d) + 1; k++) {
            swarm_status_t st = swarm_budget_register(&b, id++, d);
            if (k == swarm_level_capacity(d))
                CHECK(st == SWARM_ERR_FULL, "R1: one past capacity refused");
            else if (ok < SWARM_MAX_MODELS)
                CHECK(st == SWARM_OK && ++ok, "within capacity accepted");
            else
                CHECK(st == SWARM_ERR_FULL, "slot table full");
        }
    CHECK(b.num_slots == SWARM_MAX_MODELS, "64 slots filled");
    CHECK(swarm_budget_register(&b, 1, 0) == SWARM_ERR_DUPLICATE, "duplicate id refused");
    CHECK(swarm_budget_register(&b, 999, SWARM_MAX_LEVELS) == SWARM_ERR_ARG,
          "level past L refused");
    CHECK(swarm_budget_register(0, 999, 0) == SWARM_ERR_ARG, "register NULL");

    for (int round = 0; round < 3000; round++) {
        uint32_t L = 1 + (uint32_t) rnd(SWARM_MAX_LEVELS);
        uint64_t T = rnd(4) ? rnd(1000) : r64() >> (16 + rnd(48));
        swarm_budget_init(&b, L, T);
        uint32_t n = (uint32_t) rnd(40);
        for (uint32_t k = 0; k < n; k++) {
            swarm_budget_register(&b, 100 + k, (uint32_t) rnd(L));
            if (rnd(4) == 0) swarm_budget_set_active(&b, 100 + k, false);
        }
        uint64_t want[SWARM_MAX_MODELS];
        uint64_t real = rnd(3) ? T : rnd(T + 1);
        CHECK(swarm_budget_begin_cycle_real(&b, real) == SWARM_OK, "begin");
        ref_allot(&b, real, want);
        uint64_t sum = 0, any = 0;
        for (uint32_t i = 0; i < b.num_slots; i++) {
            CHECK(b.slots[i].allotted == want[i], "allotment = R1-R5 reference");
            CHECK(b.slots[i].used == 0 && b.slots[i].allotted_im == 0 &&
                      b.slots[i].allotted_mk == 0,
                  "a new cycle starts clean");
            sum += b.slots[i].allotted;
            any |= b.slots[i].active;
        }
        CHECK(sum == (any ? real : 0), "R5: allotments sum to exactly the total");
        uint64_t lbs = 0;
        for (uint32_t d = 0; d < SWARM_MAX_LEVELS; d++) lbs += b.level_budget[d];
        CHECK(lbs == (any ? real : 0), "level budgets sum to the total");
        CHECK(b.cycle_open && b.cycle == 1, "cycle opened and counted");

        uint64_t spent = 0;
        for (int c = 0; c < 20 && b.num_slots; c++) {
            uint32_t i = (uint32_t) rnd(b.num_slots);
            uint64_t left = b.slots[i].allotted - b.slots[i].used, req = rnd(left + 3), gr = 99;
            CHECK(swarm_budget_remaining(&b, b.slots[i].model_id) == left, "remaining");
            CHECK(swarm_budget_consume(&b, b.slots[i].model_id, req, &gr) == SWARM_OK, "consume");
            CHECK(gr == (req < left ? req : left), "granted = min(requested, remaining)");
            spent += gr;
        }
        uint64_t g2 = 5;
        CHECK(swarm_budget_consume(&b, 99999, 1, &g2) == SWARM_ERR_NO_MODEL && g2 == 0,
              "unknown model");
        CHECK(swarm_budget_remaining(&b, 99999) == 0, "remaining of unknown");
        /* beginning again closes the open cycle: R6, nothing carries */
        CHECK(swarm_budget_begin_cycle(&b) == SWARM_OK && b.cycle == 2,
              "begin closes the open one");
        CHECK(b.last_unused == (any ? real : 0) - spent, "R6: the unspent expired and is reported");
        CHECK(swarm_budget_end_cycle(&b) == SWARM_OK && !b.cycle_open, "end");
        CHECK(b.last_unused == (any ? T : 0), "a whole unspent cycle expires");
        for (uint32_t i = 0; i < b.num_slots; i++)
            CHECK(b.slots[i].allotted == 0, "R6: allotments expire at the end");
        CHECK(swarm_budget_set_rate(&b, T / 2) == SWARM_OK && b.tokens_per_cycle == T / 2,
              "set rate");
    }
}

/* -------------------------------------------------- market */

typedef struct {
    swarm_budget_t b;
    swarm_market_t m;
    swarm_emotion_state_t e;
    uint64_t supply; /* our own record: endowments joined, nothing else */
} world_t;

static void check_money(const world_t *w)
{
    CHECK(money_sum(&w->m) == w->m.money_supply, "conservation: holdings + pot + levied = supply");
    CHECK(w->m.money_supply == w->supply, "no minting: the supply moves only on a join");
    CHECK(swarm_market_conserved(&w->m), "swarm_market_conserved agrees");
}

/* the M7 wealth cap, from the header: cap = max(floor(M*8/21), ceil(M/n));
 * the excess above it is spread equally (largest remainder) over those below
 * it until nobody is above */
static void ref_wealth_cap(uint64_t *f, uint32_t n, uint64_t M)
{
    if (!n) return;
    uint64_t cap = md(M, 8, 21), eq = M / n + (M % n ? 1 : 0), excess = 0;
    if (eq > cap) cap = eq;
    for (uint32_t k = 0; k < n; k++)
        if (f[k] > cap) {
            excess += f[k] - cap;
            f[k] = cap;
        }
    while (excess) {
        uint64_t ones[SWARM_MAX_MODELS], sh[SWARM_MAX_MODELS];
        for (uint32_t k = 0; k < n; k++) ones[k] = f[k] < cap;
        ref_lr(excess, ones, n, sh);
        excess = 0;
        for (uint32_t k = 0; k < n; k++) {
            f[k] += sh[k];
            if (f[k] > cap) {
                excess += f[k] - cap;
                f[k] = cap;
            }
        }
    }
}

static void settle_and_check(world_t *w)
{
    swarm_market_t *m = &w->m;
    uint64_t f[SWARM_MAX_MODELS], vw[SWARM_MAX_MODELS], sw[SWARM_MAX_MODELS], pw[SWARM_MAX_MODELS];
    uint64_t share[SWARM_MAX_MODELS], value = 0, social = 0, pot = m->pot, before = 0;
    for (uint32_t k = 0; k < m->n; k++) {
        f[k] = m->t[k].cap[SWARM_CAP_FINANCIAL];
        before += f[k];
        vw[k] = m->t[k].value_cycle;
        sw[k] = m->t[k].social_cycle;
        pw[k] = m->t[k].paid;
        value += vw[k];
        social += sw[k];
    }
    uint64_t income = md(pot, 13, 21);
    if (value && social) {
        ref_lr(income, vw, m->n, share);
        for (uint32_t k = 0; k < m->n; k++) f[k] += share[k];
        ref_lr(pot - income, sw, m->n, share);
    } else if (value) {
        ref_lr(pot, vw, m->n, share);
    } else if (social) {
        ref_lr(pot, sw, m->n, share);
    } else {
        ref_lr(pot, pw, m->n, share); /* refund by what was paid */
    }
    for (uint32_t k = 0; k < m->n; k++) f[k] += share[k];
    ref_wealth_cap(f, m->n, m->money_supply);
    uint64_t paidsum = 0;
    for (uint32_t k = 0; k < m->n; k++) paidsum += pw[k];

    CHECK(swarm_market_settle(m) == SWARM_OK, "settle");
    uint64_t after = 0, cap = md(m->money_supply, 8, 21);
    uint64_t eq = m->n ? m->money_supply / m->n + (m->money_supply % m->n ? 1 : 0) : 0;
    if (eq > cap) cap = eq;
    for (uint32_t k = 0; k < m->n; k++) {
        CHECK(m->t[k].cap[SWARM_CAP_FINANCIAL] == f[k], "settlement = M6 + M7 reference");
        CHECK(m->t[k].cap[SWARM_CAP_FINANCIAL] <= cap, "M7: nobody above the wealth cap");
        CHECK(!m->t[k].value_cycle && !m->t[k].social_cycle && !m->t[k].won && !m->t[k].paid,
              "settle clears the cycle counters");
        after += m->t[k].cap[SWARM_CAP_FINANCIAL];
    }
    if (m->n && (value || social || paidsum))
        CHECK(after == before + pot, "the whole pot is paid out");
    if (m->n && (value || social || paidsum)) CHECK(m->pot == 0, "pot emptied");
    check_money(w);
}

static void test_market_edges(void)
{
    swarm_market_t m;
    swarm_market_init(0);
    swarm_market_init(&m);
    CHECK(m.n == 0 && m.money_supply == 0 && m.pot == 0 && m.levied == 0, "init");
    CHECK(swarm_market_conserved(&m) && !swarm_market_conserved(0), "conserved empty / NULL");
    CHECK(swarm_market_join(0, 1, 1) == SWARM_ERR_ARG, "join NULL");
    CHECK(swarm_market_join(&m, 1, (uint64_t) 1 << 62) == SWARM_OK, "join at the money ceiling");
    CHECK(swarm_market_join(&m, 2, 1) == SWARM_ERR_ARG, "past the ceiling refused");
    CHECK(swarm_market_join(&m, 2, 0) == SWARM_OK, "a zero endowment fits");
    CHECK(swarm_market_join(&m, 2, 0) == SWARM_ERR_DUPLICATE, "duplicate refused");
    swarm_market_init(&m);
    for (uint32_t k = 0; k < SWARM_MAX_MODELS; k++)
        CHECK(swarm_market_join(&m, 10 + k, 5) == SWARM_OK, "join up to 64");
    CHECK(swarm_market_join(&m, 999, 5) == SWARM_ERR_FULL, "65th refused");
    CHECK(m.money_supply == 5 * SWARM_MAX_MODELS, "supply = endowments");
    CHECK(swarm_market_bid(&m, 10, 6) == SWARM_ERR_ARG && m.t[0].bid == 0,
          "no debt: bid > holdings");
    CHECK(swarm_market_bid(&m, 10, 5) == SWARM_OK && m.t[0].bid == 5, "bid all holdings");
    CHECK(swarm_market_bid(&m, 9, 1) == SWARM_ERR_NO_MODEL, "bid unknown");
    CHECK(swarm_market_bid(0, 10, 1) == SWARM_ERR_ARG, "bid NULL");
    CHECK(swarm_market_trader(&m, 73)->model_id == 73 && !swarm_market_trader(&m, 74), "trader");
    CHECK(swarm_market_trader(0, 10) == 0, "trader NULL");

    for (uint32_t f = 0; f <= SWARM_CAP_COUNT; f++) {
        swarm_status_t st = swarm_market_credit(&m, 11, (swarm_cap_t) f, 3);
        CHECK(st == (f == SWARM_CAP_FINANCIAL || f == SWARM_CAP_COUNT ? SWARM_ERR_ARG : SWARM_OK),
              "credit any form but Financial");
    }
    const swarm_trader_t *t = swarm_market_trader(&m, 11);
    CHECK(t->cap[SWARM_CAP_FINANCIAL] == 5, "credit never mints Financial");
    CHECK(t->value_cycle == 12, "value forms: manufactured, intellectual, human, system");
    CHECK(t->social_cycle == 3, "social counts toward the dividend");
    CHECK(swarm_market_credit(&m, 9, SWARM_CAP_HUMAN, 1) == SWARM_ERR_NO_MODEL, "credit unknown");
    CHECK(swarm_market_credit(0, 11, SWARM_CAP_HUMAN, 1) == SWARM_ERR_ARG, "credit NULL");
    CHECK(swarm_market_credit(&m, 11, SWARM_CAP_HUMAN, UINT64_MAX) == SWARM_ERR_ARG,
          "a wrapping credit is refused");
    CHECK(t->cap[SWARM_CAP_HUMAN] == 3 && t->value_cycle == 12, "and credits nothing");
    CHECK(swarm_market_credit(&m, 11, SWARM_CAP_NATURAL, UINT64_MAX - 3) == SWARM_OK,
          "fills natural");
    CHECK(swarm_market_credit(&m, 11, SWARM_CAP_NATURAL, 1) == SWARM_ERR_ARG, "natural would wrap");
    m.t[2].value_cycle = UINT64_MAX;
    CHECK(swarm_market_credit(&m, 12, SWARM_CAP_SYSTEM, 1) == SWARM_ERR_ARG &&
              m.t[2].cap[SWARM_CAP_SYSTEM] == 0,
          "the value counter would wrap: nothing credited");
    m.t[2].value_cycle = 0;
    m.t[2].social_cycle = UINT64_MAX;
    CHECK(swarm_market_credit(&m, 12, SWARM_CAP_SOCIAL, 1) == SWARM_ERR_ARG &&
              m.t[2].cap[SWARM_CAP_SOCIAL] == 0,
          "the social counter would wrap: nothing credited");
    CHECK(swarm_market_credit(&m, 12, SWARM_CAP_CULTURAL, 1) == SWARM_OK && m.t[2].value_cycle == 0,
          "cultural is not a value form");
    m.t[2].social_cycle = 0;

    for (uint32_t f = 0; f < SWARM_CAP_COUNT; f++)
        CHECK(swarm_cap_is_crown((swarm_cap_t) f) ==
                  (f == SWARM_CAP_SOCIAL || f == SWARM_CAP_NATURAL || f == SWARM_CAP_CULTURAL ||
                   f == SWARM_CAP_SPIRITUAL),
              "the four Crown forms");

    m.pot = 10;
    m.t[0].cap[SWARM_CAP_FINANCIAL] -= 10;
    CHECK(swarm_market_conserved(&m), "moving 10 to the pot conserves");
    CHECK(swarm_market_levy(&m, 11) == SWARM_ERR_ARG && m.pot == 10, "levy above the pot refused");
    CHECK(swarm_market_levy(&m, 4) == SWARM_OK && m.pot == 6 && m.levied == 4, "levy");
    CHECK(swarm_market_conserved(&m), "levy conserves");
    CHECK(swarm_market_release(&m, 5) == SWARM_ERR_ARG && m.levied == 4,
          "release above levied refused");
    CHECK(swarm_market_release(&m, 4) == SWARM_OK && m.pot == 10 && m.levied == 0, "release");
    CHECK(swarm_market_levy(0, 1) == SWARM_ERR_ARG && swarm_market_release(0, 1) == SWARM_ERR_ARG,
          "levy/release NULL");
    CHECK(swarm_market_levy(&m, 10) == SWARM_OK && swarm_market_levy(&m, 0) == SWARM_OK,
          "levy the whole pot, then nothing");
    CHECK(swarm_market_release(&m, 10) == SWARM_OK, "release it all");
    m.pot++;
    CHECK(!swarm_market_conserved(&m), "one minted token is caught");
    m.pot--;
    m.levied++;
    CHECK(!swarm_market_conserved(&m), "one minted levied token is caught");
    m.levied--;
    m.t[SWARM_MAX_MODELS - 1].cap[SWARM_CAP_FINANCIAL]--;
    CHECK(!swarm_market_conserved(&m), "one burned token in the last holder is caught");
    m.t[SWARM_MAX_MODELS - 1].cap[SWARM_CAP_FINANCIAL]++;
    CHECK(swarm_market_settle(0) == SWARM_ERR_ARG, "settle NULL");

    /* capped split: water-filling, nobody above the cap, the rest returned */
    for (int i = 0; i < 20000; i++) {
        uint32_t n = (uint32_t) rnd(12);
        uint64_t w[SWARM_MAX_MODELS], out[SWARM_MAX_MODELS], total = rnd(5000), cap = rnd(2000);
        for (uint32_t k = 0; k < n; k++) w[k] = rnd(3) ? rnd(100) : 0;
        uint64_t back = swarm_capped_split(total, w, n, cap, out), s = 0, wsum = 0;
        for (uint32_t k = 0; k < n; k++) {
            CHECK(out[k] <= cap, "capped split: nobody above the cap");
            if (!w[k]) CHECK(out[k] == 0, "capped split: zero weight takes nothing");
            s += out[k];
            wsum += w[k];
        }
        CHECK(s + back == total, "capped split: given + returned = total");
        if (back) {
            for (uint32_t k = 0; k < n; k++)
                if (w[k])
                    CHECK(out[k] == cap, "something returned only when every bidder is capped");
        }
        if (wsum) {
            uint64_t lr[SWARM_MAX_MODELS], mx = 0;
            ref_lr(total, w, n, lr);
            for (uint32_t k = 0; k < n; k++) mx = lr[k] > mx ? lr[k] : mx;
            if (mx <= cap)
                CHECK(back == 0 && memcmp(out, lr, n * sizeof out[0]) == 0,
                      "no cap binding: the plain largest-remainder split");
        } else {
            CHECK(back == total, "no weight: everything returned");
        }
    }
}

static void world_init(world_t *w, uint32_t L, uint64_t T)
{
    swarm_budget_init(&w->b, L, T);
    swarm_market_init(&w->m);
    swarm_emotion_init(&w->e);
    w->supply = 0;
}

static void test_market_random(void)
{
    static world_t w;
    unsigned settled = 0, held = 0;
    for (int round = 0; round < 1200; round++) {
        uint32_t L = 1 + (uint32_t) rnd(SWARM_MAX_LEVELS);
        uint64_t T = rnd(3) ? 1 + rnd(5000) : 1 + (r64() >> (20 + rnd(30)));
        world_init(&w, L, T);
        uint32_t n = 1 + (uint32_t) rnd(30);
        for (uint32_t k = 0; k < n; k++) {
            uint32_t id = 1 + k;
            if (swarm_budget_register(&w.b, id, (uint32_t) rnd(L)) != SWARM_OK) continue;
            if (rnd(5) == 0) swarm_budget_set_active(&w.b, id, false);
            if (rnd(8) != 0) {
                uint64_t endow = rnd(4) ? rnd(10000) : r64() >> 24;
                CHECK(swarm_market_join(&w.m, id, endow) == SWARM_OK, "join");
                w.supply += endow;
            }
            if (rnd(2)) {
                swarm_feeling_t f = {(swarm_emotion_t) rnd(SWARM_EMO_COUNT), (uint8_t) rnd(6)};
                swarm_emotion_set_feeling(&w.e, id, f);
            }
        }
        swarm_feeling_t mood = {(swarm_emotion_t) rnd(SWARM_EMO_COUNT), (uint8_t) rnd(6)};
        swarm_emotion_set_mood(&w.e, mood);
        swarm_ledger_t led;
        swarm_ledger_init(&led);
        check_money(&w);

        for (int cyc = 0; cyc < 6; cyc++) {
            for (uint32_t k = 0; k < w.m.n; k++) {
                uint64_t have = w.m.t[k].cap[SWARM_CAP_FINANCIAL];
                if (rnd(3)) swarm_market_bid(&w.m, w.m.t[k].model_id, rnd(have + 1));
                if (rnd(6) == 0)
                    CHECK(swarm_market_bid(&w.m, w.m.t[k].model_id, have + 1) == SWARM_ERR_ARG,
                          "no debt");
            }
            int use_emo = (int) rnd(2);
            swarm_budget_t pre_b = w.b;
            swarm_market_t pre_m = w.m;
            swarm_emotion_state_t pre_e = w.e;
            uint64_t bids[SWARM_MAX_MODELS], holdings[SWARM_MAX_MODELS];
            for (uint32_t i = 0; i < w.b.num_slots; i++) {
                const swarm_trader_t *t = swarm_market_trader(&w.m, w.b.slots[i].model_id);
                bids[i] = t && w.b.slots[i].active ? t->bid : 0;
                holdings[i] = t ? t->cap[SWARM_CAP_FINANCIAL] : 0;
            }
            uint64_t imag = use_emo ? swarm_emotion_pool(&w.b, &w.e) : 0;
            uint64_t pot0 = w.m.pot;
            CHECK(swarm_market_begin_cycle(&w.b, &w.m, use_emo ? &w.e : 0) == SWARM_OK, "begin");
            check_money(&w);

            /* M1-M5 */
            uint64_t real = T - imag, floor_ = md(real, 8, 21), market = real - floor_;
            uint64_t cap = md(market, 8, 21), won = 0, paid = 0, sum = 0, sum_im = 0, any = 0;
            CHECK(w.m.last_market == market, "M3: market = real - floor");
            CHECK(w.m.last_floor == floor_ + w.m.last_unsold, "M2: unsold falls to the floor");
            for (uint32_t i = 0; i < w.b.num_slots; i++) {
                const swarm_slot_t *s = &w.b.slots[i];
                const swarm_trader_t *t = swarm_market_trader(&w.m, s->model_id);
                CHECK(s->allotted_mk <= cap, "M4: nobody wins above the market cap");
                if (!bids[i]) CHECK(s->allotted_mk == 0, "no bid, no market tokens");
                if (t) {
                    CHECK(t->won == s->allotted_mk, "won recorded on the trader");
                    uint64_t pay = s->allotted_mk ? bids[i] : 0;
                    CHECK(t->paid == pay, "M5: a winner pays its bid, a loser nothing");
                    CHECK(t->cap[SWARM_CAP_FINANCIAL] == holdings[i] - pay,
                          "the bid leaves holdings");
                    CHECK(t->bid == 0, "bids reset after the cycle");
                    paid += pay;
                }
                won += s->allotted_mk;
                sum += s->allotted;
                sum_im += s->allotted_im;
                any |= s->active;
            }
            CHECK(won + w.m.last_unsold == market, "won + unsold = market");
            CHECK(w.m.pot == pot0 + paid, "the pot holds exactly what winners paid");
            if (any) CHECK(sum == T, "R5 with the market and the imaginary pool: sum = T");
            CHECK(sum_im == (any && use_emo ? imag : 0), "imaginary allotments sum to the pool");
            uint64_t ref_real[SWARM_MAX_MODELS];
            ref_allot(&w.b, floor_ + w.m.last_unsold, ref_real);
            for (uint32_t i = 0; i < w.b.num_slots; i++)
                CHECK(w.b.slots[i].allotted - w.b.slots[i].allotted_im - w.b.slots[i].allotted_mk ==
                          ref_real[i],
                      "the floor part follows R1-R4");

            /* the ledger and its witnesses */
            swarm_witness_t wit[SWARM_MAX_MODELS];
            uint32_t nw = swarm_witness_cycle(&w.b, &pre_b, &pre_m, use_emo ? &pre_e : 0, wit);
            uint32_t active = 0;
            for (uint32_t i = 0; i < w.b.num_slots; i++) active += w.b.slots[i].active;
            CHECK(nw == active, "one witness record per active model");
            for (uint32_t k = 0; k < nw; k++) {
                CHECK(wit[k].verdict == (any ? SWARM_WIT_TRUE : SWARM_WIT_FALSE),
                      "an honest cycle witnesses TRUE");
                if (active >= 2)
                    CHECK(wit[k].witness_id && wit[k].witness_id != wit[k].model_id,
                          "nobody witnesses itself");
                else
                    CHECK(wit[k].witness_id == 0, "alone: the kernel witnesses");
            }
            uint64_t posted0 = led.posted;
            bool st = swarm_ledger_post_cycle(&led, &w.b, &w.m, use_emo ? imag : 0, wit, nw);
            CHECK(led.posted == posted0 + 2 * w.b.num_slots + nw,
                  "two entries per slot + witnesses");
            for (uint32_t i = 0; i < w.b.num_slots; i++) {
                const swarm_slot_t *sl = &w.b.slots[i];
                const swarm_ledger_entry_t *fe = &led.e[(posted0 + 2 * i) % SWARM_LEDGER_CAP];
                const swarm_ledger_entry_t *xe = &led.e[(posted0 + 2 * i + 1) % SWARM_LEDGER_CAP];
                CHECK(fe->ledger == SWARM_LEDGER_FINANCIAL && fe->model_id == sl->model_id &&
                          fe->counterparty == 0 && fe->amount == sl->allotted - sl->allotted_im &&
                          fe->cycle == w.b.cycle,
                      "financial entry: the real part of the allotment");
                CHECK(xe->ledger == SWARM_LEDGER_EXTERNALITY && xe->model_id == sl->model_id &&
                          xe->counterparty == 0 && xe->amount == sl->allotted_im && xe->paid == 0,
                      "externality entry: the imaginary part, nothing paid");
            }
            for (uint32_t k = 0; k < nw; k++) {
                const swarm_ledger_entry_t *pe =
                    &led.e[(posted0 + 2 * w.b.num_slots + k) % SWARM_LEDGER_CAP];
                CHECK(pe->ledger == SWARM_LEDGER_PROVENANCE && pe->model_id == wit[k].model_id &&
                          pe->counterparty == wit[k].witness_id && pe->amount == 0 &&
                          pe->paid == 0 && pe->verdict == wit[k].verdict,
                      "provenance entry: who witnessed whom, and the verdict");
            }
            CHECK(st == (any != 0), "an honest cycle settles exactly when tokens were allotted");
            if (st)
                settled++;
            else
                held++;
            if (any && w.b.num_slots) {
                /* tamper: one slot gets one more token; every witness sees it */
                swarm_budget_t bad = w.b;
                bad.slots[rnd(bad.num_slots)].allotted++;
                swarm_witness_t wb[SWARM_MAX_MODELS];
                uint32_t nb = swarm_witness_cycle(&bad, &pre_b, &pre_m, use_emo ? &pre_e : 0, wb);
                for (uint32_t k = 0; k < nb; k++)
                    CHECK(wb[k].verdict == SWARM_WIT_FALSE, "allotments not adding up: FALSE");
                CHECK(!swarm_ledger_post_cycle(&led, &bad, &w.m, use_emo ? imag : 0, wb, nb),
                      "a tampered cycle is held");
                /* move one token between two slots: adds up, but differs */
                if (bad.num_slots >= 2) {
                    bad = w.b;
                    uint32_t a = 0, c = 1;
                    while (a < bad.num_slots && !bad.slots[a].allotted) a++;
                    if (a < bad.num_slots) {
                        c = (a + 1) % bad.num_slots;
                        bad.slots[a].allotted--;
                        bad.slots[c].allotted++;
                        nb = swarm_witness_cycle(&bad, &pre_b, &pre_m, use_emo ? &pre_e : 0, wb);
                        uint32_t glut = 0;
                        for (uint32_t k = 0; k < nb; k++) glut += wb[k].verdict == SWARM_WIT_GLUT;
                        CHECK(glut >= 1 && glut <= 2,
                              "a moved token: GLUT on the slots it touched");
                        CHECK(
                            !swarm_ledger_post_cycle(&led, &bad, &w.m, use_emo ? imag : 0, wb, nb),
                            "and the cycle is held");
                    }
                }
                CHECK(!swarm_ledger_post_cycle(&led, &w.b, &w.m, use_emo ? imag + 1 : 1, wit, nw),
                      "a wrong imaginary pool is held");
                if (nw)
                    CHECK(
                        !swarm_ledger_post_cycle(&led, &w.b, &w.m, use_emo ? imag : 0, wit, nw - 1),
                        "a missing witness is held");
                w.m.pot++;
                CHECK(!swarm_ledger_post_cycle(&led, &w.b, &w.m, use_emo ? imag : 0, wit, nw),
                      "an unconserved market is held");
                w.m.pot--;
            }
            swarm_witness_reward(&w.m, wit, nw);

            /* spend some tokens, credit some value */
            for (uint32_t i = 0; i < w.b.num_slots; i++) {
                uint64_t g;
                swarm_budget_consume(&w.b, w.b.slots[i].model_id, rnd(w.b.slots[i].allotted + 2),
                                     &g);
            }
            uint64_t nat[SWARM_MAX_MODELS];
            for (uint32_t k = 0; k < w.m.n; k++) nat[k] = w.m.t[k].cap[SWARM_CAP_NATURAL];
            swarm_market_credit_frugality(&w.m, &w.b);
            for (uint32_t k = 0; k < w.m.n; k++) {
                uint64_t saved = 0;
                for (uint32_t i = 0; i < w.b.num_slots; i++)
                    if (w.b.slots[i].model_id == w.m.t[k].model_id)
                        saved = w.b.slots[i].allotted - w.b.slots[i].used;
                CHECK(w.m.t[k].cap[SWARM_CAP_NATURAL] == nat[k] + saved,
                      "frugality: unspent tokens become Natural capital");
            }
            for (int c = 0; c < 4 && w.m.n; c++)
                swarm_market_credit(&w.m, w.m.t[rnd(w.m.n)].model_id,
                                    (swarm_cap_t) (1 + rnd(SWARM_CAP_COUNT - 1)), rnd(50));
            if (rnd(3) == 0) swarm_market_levy(&w.m, rnd(w.m.pot + 1));
            if (rnd(3) == 0) swarm_market_release(&w.m, rnd(w.m.levied + 1));
            check_money(&w);
            settle_and_check(&w);
            swarm_budget_end_cycle(&w.b);
            for (uint32_t k = 0; k < w.m.n; k++) nat[k] = w.m.t[k].cap[SWARM_CAP_NATURAL];
            swarm_market_credit_frugality(&w.m, &w.b);
            for (uint32_t k = 0; k < w.m.n; k++)
                CHECK(w.m.t[k].cap[SWARM_CAP_NATURAL] == nat[k], "no frugality outside a cycle");
            swarm_market_credit_frugality(0, &w.b);
            swarm_market_credit_frugality(&w.m, 0);
        }
        CHECK(led.settled_cycles + led.held_cycles >= 6, "every posted cycle is counted");
    }
    CHECK(settled > 1000 && held > 10, "both ledger outcomes exercised");
}

static void test_ledger_reserve_edges(void)
{
    swarm_ledger_t led;
    swarm_budget_t b;
    swarm_market_t m;
    swarm_emotion_state_t e;
    swarm_ledger_init(0);
    swarm_ledger_init(&led);
    CHECK(led.head == 0 && led.posted == 0 && !led.settled_cycles && !led.held_cycles,
          "ledger init");
    swarm_budget_init(&b, 2, 100);
    swarm_market_init(&m);
    swarm_emotion_init(&e);
    CHECK(!swarm_ledger_post_cycle(0, &b, &m, 0, 0, 0), "post NULL ledger");
    CHECK(!swarm_ledger_post_cycle(&led, 0, &m, 0, 0, 0), "post NULL budget");
    CHECK(!swarm_ledger_post_cycle(&led, &b, 0, 0, 0, 0), "post NULL market");
    CHECK(!swarm_ledger_post_cycle(&led, &b, &m, 0, 0, 1), "witnesses claimed, none given");
    CHECK(led.posted == 0 && !led.held_cycles, "a refused post writes nothing");
    CHECK(swarm_witness_cycle(0, &b, &m, 0, 0) == 0, "witness NULL");

    swarm_budget_register(&b, 7, 0);
    swarm_budget_register(&b, 8, 1);
    swarm_budget_register(&b, 9, 1);
    swarm_market_join(&m, 7, 50);
    swarm_market_join(&m, 8, 50);
    swarm_feeling_t joy = {SWARM_EMO_JOY, 5}, worry = {SWARM_EMO_WORRY, 2};
    swarm_emotion_set_feeling(&e, 8, joy);
    swarm_emotion_set_feeling(&e, 9, worry);
    swarm_budget_begin_cycle(&b);
    CHECK(swarm_witness_for(&b, 3) == 0 && swarm_witness_for(0, 0) == 0,
          "witness_for out of range");
    for (uint64_t cyc = 0; cyc < 6; cyc++) {
        b.cycle = cyc;
        for (uint32_t s = 0; s < 3; s++) {
            /* the witness rotates over the others: active[(j + 1 + cycle mod 2) mod 3] */
            uint32_t want = b.slots[(s + 1 + cyc % 2) % 3].model_id;
            CHECK(swarm_witness_for(&b, s) == want, "witness rotation");
        }
    }
    swarm_budget_set_active(&b, 9, false);
    CHECK(swarm_witness_for(&b, 2) == 0, "an inactive model has no witness");
    CHECK(swarm_witness_for(&b, 0) == 8 && swarm_witness_for(&b, 1) == 7,
          "two active witness each other");
    swarm_budget_set_active(&b, 8, false);
    CHECK(swarm_witness_for(&b, 0) == 0, "alone: the kernel witnesses");
    swarm_budget_set_active(&b, 8, true);
    swarm_budget_set_active(&b, 9, true);

    /* the ring: entry fields and the wrap at SWARM_LEDGER_CAP */
    swarm_budget_begin_cycle(&b);
    swarm_witness_t wit[3] = {
        {7, 8, SWARM_WIT_TRUE}, {8, 9, SWARM_WIT_TRUE}, {9, 7, SWARM_WIT_GLUT}};
    swarm_ledger_init(&led);
    CHECK(!swarm_ledger_post_cycle(&led, &b, &m, 0, wit, 3), "a GLUT witness holds the cycle");
    CHECK(led.held_cycles == 1 && led.settled_cycles == 0 && led.posted == 9, "counted as held");
    CHECK(led.e[0].ledger == SWARM_LEDGER_FINANCIAL && led.e[0].model_id == 7 &&
              led.e[0].amount == b.slots[0].allotted && led.e[0].cycle == b.cycle,
          "financial entry");
    CHECK(led.e[1].ledger == SWARM_LEDGER_EXTERNALITY && led.e[1].amount == 0, "externality entry");
    CHECK(led.e[8].ledger == SWARM_LEDGER_PROVENANCE && led.e[8].model_id == 9 &&
              led.e[8].counterparty == 7 && led.e[8].verdict == SWARM_WIT_GLUT,
          "provenance entry");
    wit[2].verdict = SWARM_WIT_TRUE;
    CHECK(swarm_ledger_post_cycle(&led, &b, &m, 0, wit, 3), "all TRUE settles");
    CHECK(led.settled_cycles == 1, "counted as settled");
    for (int k = 0; k < 200; k++) swarm_ledger_post_cycle(&led, &b, &m, 0, wit, 3);
    CHECK(led.posted == 9u * 202u && led.head == (9u * 202u) % SWARM_LEDGER_CAP, "ring wraps");
    m.t[0].paid = 13;
    swarm_ledger_post_cycle(&led, &b, &m, 0, wit, 0);
    CHECK(led.e[(led.head + SWARM_LEDGER_CAP - 6) % SWARM_LEDGER_CAP].paid == 13,
          "financial entry records what was paid");

    /* witness reward: one Social unit to each named witness */
    uint64_t s7 = m.t[0].cap[SWARM_CAP_SOCIAL], s8 = m.t[1].cap[SWARM_CAP_SOCIAL];
    swarm_witness_t rw[3] = {
        {7, 8, SWARM_WIT_TRUE}, {8, 0, SWARM_WIT_TRUE}, {9, 8, SWARM_WIT_TRUE}};
    swarm_witness_reward(&m, rw, 3);
    CHECK(m.t[1].cap[SWARM_CAP_SOCIAL] == s8 + 2 && m.t[0].cap[SWARM_CAP_SOCIAL] == s7,
          "witness reward");
    swarm_witness_reward(0, rw, 3);
    swarm_witness_reward(&m, 0, 3);

    /* the reserve: F1 peers at the owner's level, F2 summary of the others,
     * F3 the total charge of every active model */
    swarm_reserve_t r;
    CHECK(swarm_reserve_build(0, &e, 7, &r) == SWARM_ERR_ARG, "reserve NULL budget");
    CHECK(swarm_reserve_build(&b, 0, 7, &r) == SWARM_ERR_ARG, "reserve NULL emotions");
    CHECK(swarm_reserve_build(&b, &e, 7, 0) == SWARM_ERR_ARG, "reserve NULL out");
    CHECK(swarm_reserve_build(&b, &e, 99, &r) == SWARM_ERR_NO_MODEL, "reserve unknown owner");
    CHECK(swarm_reserve_build(&b, &e, 8, &r) == SWARM_OK, "reserve for 8 (level 1)");
    CHECK(r.owner_id == 8 && r.level == 1 && r.num_peers == 2, "peers: 8 and 9");
    CHECK(r.peer_id[0] == 8 && r.peer_id[1] == 9, "peer ids in slot order");
    CHECK(r.peer[0].emotion == SWARM_EMO_JOY && r.peer[1].intensity == 2, "peer feelings");
    CHECK(r.total == 8 + 2, "F3: joy 5 charges F(6)=8, worry 2 charges F(3)=2, 7 is neutral");
    CHECK(swarm_reserve_audit(&r, &b, &e), "an honest reserve audits");
    CHECK(!swarm_reserve_audit(0, &b, &e) && !swarm_reserve_audit(&r, 0, &e) &&
              !swarm_reserve_audit(&r, &b, 0),
          "audit NULL");
    swarm_feeling_t calm = {SWARM_EMO_CALM, 3};
    swarm_emotion_set_feeling(&e, 7, calm);
    CHECK(swarm_reserve_build(&b, &e, 8, &r) == SWARM_OK && r.summary[0][SWARM_EMO_CALM] == 3 &&
              r.total == 13,
          "F2: level 0 summarised by emotion");
    for (uint32_t d = 0; d < SWARM_MAX_LEVELS; d++)
        for (uint32_t k = 0; k < SWARM_EMO_COUNT; k++) {
            swarm_reserve_t t = r;
            if (d == r.level) continue;
            t.summary[d][k]++;
            CHECK(!swarm_reserve_audit(&t, &b, &e), "a tampered summary cell is caught");
        }
    /* moving charge between two cells keeps every total: only the cell-by-cell
     * comparison can see it */
    for (uint32_t d = 0; d < SWARM_MAX_LEVELS; d++) {
        if (d == r.level || !r.summary[d][SWARM_EMO_CALM]) continue;
        swarm_reserve_t t = r;
        t.summary[d][SWARM_EMO_CALM]--;
        t.summary[d][SWARM_EMO_JOY]++;
        CHECK(!swarm_reserve_audit(&t, &b, &e), "charge moved between emotions is caught");
    }
    {
        swarm_reserve_t t = r;
        t.total++;
        CHECK(!swarm_reserve_audit(&t, &b, &e), "a tampered total is caught");
        t = r;
        t.peer[1].intensity = 3;
        CHECK(!swarm_reserve_audit(&t, &b, &e), "a tampered peer feeling is caught");
        t = r;
        t.num_peers = 1;
        CHECK(!swarm_reserve_audit(&t, &b, &e), "a dropped peer is caught");
        t = r;
        t.summary[r.level][SWARM_EMO_JOY] = 99;
        CHECK(swarm_reserve_audit(&t, &b, &e), "the owner's own level row is not part of F2");
    }
    swarm_budget_set_active(&b, 9, false);
    CHECK(!swarm_reserve_audit(&r, &b, &e), "a model going inactive invalidates the reserve");
    CHECK(swarm_reserve_build(&b, &e, 8, &r) == SWARM_OK && r.num_peers == 1 && r.total == 11,
          "inactive models are not in the reserve");
    CHECK(swarm_reserve_build(&b, &e, 7, &r) == SWARM_OK && r.level == 0 && r.num_peers == 1 &&
              r.summary[1][SWARM_EMO_JOY] == 8 && r.total == 11,
          "reserve seen from level 0");
    CHECK(swarm_reserve_audit(&r, &b, &e), "and it audits");
}

/* -------------------------------------------------- edges found by mutation
 * Each block below kills a surviving mutant that the differential probe saw
 * change behaviour (tests/MUTATION_REPORT.md). */

/* M6 splits the pot by the sum of every trader's cycle value. Two traders
 * near the top of uint64 used to wrap that sum, and settlement then paid out
 * more than the pot held (F9). */
static void test_cycle_value_bound(void)
{
    swarm_budget_t b;
    swarm_market_t m;
    swarm_budget_init(&b, 2, 1000);
    swarm_market_init(&m);
    swarm_budget_register(&b, 1, 0);
    swarm_budget_register(&b, 2, 1);
    swarm_market_join(&m, 1, 1000);
    swarm_market_join(&m, 2, 1000);
    swarm_market_join(&m, 3, 1000); /* holds the M7 cap above both payees */
    swarm_market_bid(&m, 1, 30);
    swarm_market_bid(&m, 2, 20);
    CHECK(swarm_market_begin_cycle(&b, &m, 0) == SWARM_OK && m.pot == 50, "two bids fill the pot");
    const uint64_t top = SWARM_MKT_CYCLE_MAX;
    CHECK(top == UINT64_MAX / SWARM_MAX_MODELS, "cycle cap is UINT64_MAX / SWARM_MAX_MODELS");
    CHECK(swarm_market_credit(&m, 1, SWARM_CAP_INTELLECTUAL, UINT64_MAX) == SWARM_ERR_ARG &&
              m.t[0].value_cycle == 0 && m.t[0].cap[SWARM_CAP_INTELLECTUAL] == 0,
          "a value credit past the cycle cap is refused and credits nothing");
    CHECK(swarm_market_credit(&m, 1, SWARM_CAP_SOCIAL, (uint64_t) 1 << 63) == SWARM_ERR_ARG &&
              m.t[0].social_cycle == 0 && m.t[0].cap[SWARM_CAP_SOCIAL] == 0,
          "a Social credit past the cycle cap is refused and credits nothing");
    CHECK(swarm_market_credit(&m, 1, SWARM_CAP_HUMAN, top) == SWARM_OK && m.t[0].value_cycle == top,
          "a credit up to the cap is taken");
    CHECK(swarm_market_credit(&m, 1, SWARM_CAP_SYSTEM, 1) == SWARM_ERR_ARG &&
              m.t[0].value_cycle == top && m.t[0].cap[SWARM_CAP_SYSTEM] == 0,
          "one past the cap is refused");
    CHECK(swarm_market_credit(&m, 1, SWARM_CAP_NATURAL, UINT64_MAX - 1000) == SWARM_OK,
          "forms outside M6 are not held to the cycle cap");
    CHECK(swarm_market_credit(&m, 2, SWARM_CAP_INTELLECTUAL, 2) == SWARM_OK, "a small credit");
    uint64_t f1 = m.t[0].cap[SWARM_CAP_FINANCIAL], f2 = m.t[1].cap[SWARM_CAP_FINANCIAL];
    swarm_market_settle(&m);
    CHECK(money_sum(&m) == m.money_supply && m.money_supply == 3000, "settlement conserves money");
    uint64_t p1 = m.t[0].cap[SWARM_CAP_FINANCIAL] - f1, p2 = m.t[1].cap[SWARM_CAP_FINANCIAL] - f2;
    uint64_t wv[2] = {top, 2}, want[2];
    ref_lr(50, wv, 2, want);
    CHECK(p1 == want[0] && p2 == want[1] && p1 + p2 == 50, "the pot is split by value, exactly");
    CHECK(swarm_market_credit(&m, 2, SWARM_CAP_SOCIAL, top) == SWARM_OK &&
              m.t[1].social_cycle == top,
          "a Social credit up to the cap is taken");
    CHECK(swarm_market_credit(&m, 2, SWARM_CAP_SOCIAL, 1) == SWARM_ERR_ARG &&
              m.t[1].social_cycle == top,
          "one Social past the cap is refused");

    /* the split's own guard: weights that break the documented precondition
     * (sum fits in 64 bits) still write only out[0..n) and return */
    uint64_t w[3] = {(uint64_t) 1 << 63, (uint64_t) 1 << 63, 1}, out[4] = {0, 0, 0, 0x5EED};
    swarm_split_lr(10, w, 3, out);
    CHECK(out[3] == 0x5EED, "split_lr never writes past n");
}

/* every array loop at its bound: a 64-model swarm over 8 levels, one model
 * with no feeling, lookups for an id nobody has. The sanitizer build (tiers
 * --mode san) turns any read or write one past the end into a failure. */
static void test_full_roster(void)
{
    static swarm_budget_t b, pre_b;
    static swarm_market_t m, pre_m;
    static swarm_emotion_state_t e, pre_e;
    static swarm_ledger_t led;
    /* dirty first: init clears the fields, not the padding after num_slots,
     * which is where a slot one past the end would be read from */
    memset(&b, 0xA5, sizeof b);
    CHECK(swarm_budget_init(&b, SWARM_MAX_LEVELS, 100000) == SWARM_OK, "init 8 levels");
    swarm_market_init(&m);
    swarm_emotion_init(&e);
    uint32_t lvl = 0, in = 0;
    for (uint32_t id = 1; id <= SWARM_MAX_MODELS; id++) {
        if (in == swarm_level_capacity(lvl)) {
            lvl++;
            in = 0;
        }
        CHECK(swarm_budget_register(&b, id, lvl) == SWARM_OK, "register into the full roster");
        in++;
        CHECK(swarm_market_join(&m, id, id == 1 ? 100000 : 10) == SWARM_OK, "join");
        swarm_market_bid(&m, id, id == 1 ? 0 : 1 + id % 3);
        if (id > 1) {
            swarm_feeling_t f = {(swarm_emotion_t) (id % SWARM_EMO_COUNT), (uint8_t) (id % 4)};
            CHECK(swarm_emotion_set_feeling(&e, id, f) == SWARM_OK, "feeling");
        }
    }
    swarm_feeling_t stranger = {SWARM_EMO_JOY, 3};
    CHECK(swarm_emotion_set_feeling(&e, 1000, stranger) == SWARM_OK && e.num == SWARM_MAX_MODELS,
          "64 feelings, model 1 has none");
    CHECK(b.num_slots == SWARM_MAX_MODELS && m.n == SWARM_MAX_MODELS, "full roster");
    CHECK(swarm_budget_register(&b, 999, 7) == SWARM_ERR_FULL, "a 65th model is refused");
    CHECK(swarm_market_join(&m, 999, 1) == SWARM_ERR_FULL, "a 65th trader is refused");
    CHECK(swarm_market_trader(&m, 999) == 0 && swarm_market_bid(&m, 999, 0) == SWARM_ERR_NO_MODEL,
          "an unknown trader is not found");
    CHECK(swarm_budget_set_active(&b, 999, false) == SWARM_ERR_NO_MODEL, "unknown model");

    pre_b = b;
    pre_m = m;
    pre_e = e;
    uint64_t supply = m.money_supply;
    CHECK(swarm_market_begin_cycle(&b, &m, &e) == SWARM_OK, "a full cycle opens");
    uint64_t sum = 0;
    for (uint32_t i = 0; i < b.num_slots; i++) sum += b.slots[i].allotted;
    CHECK(sum == b.tokens_per_cycle, "R5 over 64 models");
    CHECK(swarm_budget_remaining(&b, 999) == 0, "nothing remains for an unknown model");
    uint64_t g = 0;
    CHECK(swarm_budget_consume(&b, 999, 1, &g) == SWARM_ERR_NO_MODEL,
          "unknown model spends nothing");
    for (uint32_t id = 1; id <= SWARM_MAX_MODELS; id += 2) swarm_budget_consume(&b, id, 3, &g);

    swarm_witness_t wit[SWARM_MAX_MODELS];
    uint32_t nw = swarm_witness_cycle(&b, &pre_b, &pre_m, &pre_e, wit);
    CHECK(nw == SWARM_MAX_MODELS, "a witness per model");
    uint32_t honest = 0;
    for (uint32_t k = 0; k < nw; k++) honest += wit[k].verdict == SWARM_WIT_TRUE;
    CHECK(honest == nw, "an honest full cycle witnesses TRUE");
    CHECK(swarm_witness_for(&b, SWARM_MAX_MODELS) == 0, "no slot 64");
    CHECK(swarm_witness_for(&b, SWARM_MAX_MODELS - 1) != 0, "the last slot has a witness");
    swarm_ledger_init(&led);
    uint64_t imag = 0;
    for (uint32_t i = 0; i < b.num_slots; i++) imag += b.slots[i].allotted_im;
    CHECK(swarm_ledger_post_cycle(&led, &b, &m, imag, wit, nw) && led.settled_cycles == 1,
          "an honest full cycle settles");
    CHECK(led.posted == 3u * SWARM_MAX_MODELS, "two entries per slot, one per witness");

    swarm_reserve_t r;
    CHECK(swarm_reserve_build(&b, &e, 999, &r) == SWARM_ERR_NO_MODEL, "unknown owner");
    CHECK(swarm_reserve_build(&b, &e, 1, &r) == SWARM_OK && r.num_peers == 1 &&
              r.peer[0].emotion == SWARM_EMO_NEUTRAL && r.peer[0].intensity == 0,
          "a model with no feeling is neutral at intensity 0");
    CHECK(swarm_reserve_audit(&r, &b, &e), "full reserve audits");
    CHECK(swarm_reserve_build(&b, &e, SWARM_MAX_MODELS, &r) == SWARM_OK &&
              swarm_reserve_audit(&r, &b, &e),
          "reserve from the last level");

    for (uint32_t id = 2; id <= SWARM_MAX_MODELS; id++) {
        swarm_market_credit(&m, id, SWARM_CAP_INTELLECTUAL, id);
        swarm_market_credit(&m, id, SWARM_CAP_SOCIAL, 1);
    }
    swarm_witness_reward(&m, wit, nw);
    swarm_market_credit_frugality(&m, &b);
    CHECK(swarm_market_settle(&m) == SWARM_OK, "settle 64 traders");
    CHECK(money_sum(&m) == supply && m.money_supply == supply, "money conserved over 64 traders");
    uint64_t cap = md(supply, SWARM_MKT_FLOOR_NUM, SWARM_MKT_DEN);
    for (uint32_t k = 0; k < m.n; k++)
        CHECK(m.t[k].cap[SWARM_CAP_FINANCIAL] <= cap, "M7: nobody above the wealth cap");
    CHECK(m.t[0].cap[SWARM_CAP_FINANCIAL] == cap, "the rich model is cut to the cap");
    CHECK(swarm_budget_end_cycle(&b) == SWARM_OK, "end");

    uint64_t w[SWARM_MAX_MODELS], out[SWARM_MAX_MODELS];
    for (uint32_t i = 0; i < SWARM_MAX_MODELS; i++) w[i] = 1 + i % 5;
    uint64_t unsold = swarm_capped_split(1000, w, SWARM_MAX_MODELS, 10, out);
    uint64_t given = 0;
    for (uint32_t i = 0; i < SWARM_MAX_MODELS; i++) {
        given += out[i];
        CHECK(out[i] <= 10, "capped split respects the cap");
    }
    CHECK(given + unsold == 1000, "capped split over 64 weights conserves");
}

/* state the API leaves behind: init over dirty memory, slots a register or
 * end_cycle hands back, two markets' worth of begin_cycle without settling */
static void test_reset_state(void)
{
    swarm_budget_t b;
    swarm_market_t m;
    memset(&b, 0xA5, sizeof b);
    CHECK(swarm_budget_init(&b, 3, 50) == SWARM_OK, "init over dirty memory");
    bool clean = b.last_unused == 0 && b.num_slots == 0 && b.cycle == 0;
    for (uint32_t d = 0; d < SWARM_MAX_LEVELS; d++) clean = clean && b.level_budget[d] == 0;
    for (uint32_t i = 0; i < SWARM_MAX_MODELS; i++) {
        const swarm_slot_t *s = &b.slots[i];
        clean = clean && s->model_id == 0 && s->level == 0 && !s->active && s->allotted == 0 &&
                s->allotted_im == 0 && s->allotted_mk == 0 && s->used == 0;
    }
    CHECK(clean, "budget init clears every field");
    memset(&m, 0xA5, sizeof m);
    swarm_market_init(&m);
    clean = m.n == 0 && m.money_supply == 0 && m.pot == 0 && m.levied == 0 && m.last_floor == 0 &&
            m.last_market == 0 && m.last_unsold == 0;
    for (uint32_t i = 0; i < SWARM_MAX_MODELS; i++) {
        const swarm_trader_t *t = &m.t[i];
        clean = clean && t->model_id == 0 && t->bid == 0 && t->won == 0 && t->paid == 0 &&
                t->value_cycle == 0 && t->social_cycle == 0;
        for (uint32_t f = 0; f < SWARM_CAP_COUNT; f++) clean = clean && t->cap[f] == 0;
    }
    CHECK(clean, "market init clears every field");

    CHECK(swarm_budget_set_rate(&b, SWARM_MAX_TOKENS_PER_CYCLE) == SWARM_OK &&
              b.tokens_per_cycle == SWARM_MAX_TOKENS_PER_CYCLE,
          "the largest rate is accepted");
    CHECK(swarm_budget_set_rate(&b, SWARM_MAX_TOKENS_PER_CYCLE + 1) == SWARM_ERR_ARG,
          "and no more");
    swarm_budget_set_rate(&b, 50);

    swarm_budget_register(&b, 1, 0);
    swarm_budget_register(&b, 2, 1);
    swarm_market_join(&m, 1, 100);
    swarm_market_join(&m, 2, 100);
    swarm_market_join(&m, 3, 100); /* a trader with no budget slot */
    swarm_market_bid(&m, 1, 5);
    swarm_market_bid(&m, 2, 4);
    CHECK(swarm_market_begin_cycle(0, &m, 0) == SWARM_ERR_ARG &&
              swarm_market_begin_cycle(&b, 0, 0) == SWARM_ERR_ARG,
          "begin_cycle needs both");
    CHECK(swarm_market_begin_cycle(&b, &m, 0) == SWARM_OK && m.t[0].won && m.t[0].paid == 5,
          "model 1 wins");
    CHECK(m.t[2].won == 0 && m.t[2].paid == 0, "a trader with no slot wins nothing");
    CHECK(swarm_budget_register(&b, 3, 1) == SWARM_OK && b.slots[2].allotted == 0 &&
              b.slots[2].allotted_im == 0 && b.slots[2].allotted_mk == 0 && b.slots[2].used == 0 &&
              swarm_budget_remaining(&b, 3) == 0,
          "a model joining mid-cycle starts with nothing");
    /* the same market opened by a second budget that model 1 is not in:
     * last cycle's wins and payments do not carry over */
    swarm_budget_t b2;
    swarm_budget_init(&b2, 1, 50);
    swarm_budget_register(&b2, 2, 0);
    CHECK(swarm_market_begin_cycle(&b2, &m, 0) == SWARM_OK, "second budget");
    CHECK(m.t[0].won == 0 && m.t[0].paid == 0 && m.t[1].won == 0 && m.t[1].paid == 0,
          "begin_cycle resets every trader's win and payment");
    CHECK(swarm_budget_end_cycle(&b) == SWARM_OK, "end");
    bool expired = true;
    for (uint32_t i = 0; i < b.num_slots; i++)
        expired = expired && b.slots[i].allotted == 0 && b.slots[i].allotted_im == 0 &&
                  b.slots[i].allotted_mk == 0;
    CHECK(expired, "R6: end_cycle expires every allotment part");
}

/* witness and ledger edges: a forged extra slot, a short witness list, the
 * kernel as witness (id 0), a model with no trader */
static void test_witness_edges(void)
{
    swarm_budget_t b, posted;
    swarm_market_t m;
    swarm_ledger_t led;
    swarm_budget_init(&b, 2, 100);
    swarm_market_init(&m);
    swarm_budget_register(&b, 7, 0);
    swarm_budget_register(&b, 8, 1);
    swarm_budget_register(&b, 9, 1);
    swarm_market_join(&m, 0, 10); /* a trader whose id is 0 */
    swarm_market_join(&m, 7, 10);
    swarm_market_join(&m, 8, 10);
    swarm_budget_t pre = b;
    swarm_market_t pre_m = m;
    swarm_market_begin_cycle(&b, &m, 0);

    /* the posted budget carries an extra active slot the honest replay does
     * not have: model 0, zero tokens, so the totals still add up */
    posted = b;
    posted.slots[3] = (swarm_slot_t){0};
    posted.slots[3].active = true;
    posted.num_slots = 4;
    swarm_witness_t w[SWARM_MAX_MODELS];
    CHECK(swarm_witness_cycle(&posted, &pre, &pre_m, 0, w) == 4, "four verdicts");
    CHECK(w[0].verdict == SWARM_WIT_TRUE && w[3].verdict == SWARM_WIT_GLUT,
          "a slot the replay does not have is a GLUT");

    /* reward reads exactly n witnesses; witness id 0 (the kernel) credits
     * nobody, not even a trader whose id is 0 */
    swarm_witness_t rw[3] = {
        {7, 0, SWARM_WIT_TRUE}, {8, 7, SWARM_WIT_TRUE}, {9, 8, SWARM_WIT_TRUE}};
    swarm_witness_reward(&m, rw, 2);
    CHECK(m.t[0].cap[SWARM_CAP_SOCIAL] == 0, "the kernel's witness credits nobody");
    CHECK(m.t[1].cap[SWARM_CAP_SOCIAL] == 1, "model 7 witnessed once");
    CHECK(m.t[2].cap[SWARM_CAP_SOCIAL] == 0, "w[n] is not read");

    swarm_ledger_init(&led);
    swarm_ledger_post_cycle(&led, &b, &m, 0, 0, 0);
    CHECK(led.e[4].model_id == 9 && led.e[4].ledger == SWARM_LEDGER_FINANCIAL && led.e[4].paid == 0,
          "a model with no trader paid nothing");

    /* the reserve: built over a dirty out, and a charge moved inside one
     * level that is neither 0 nor the owner's */
    swarm_emotion_state_t e;
    swarm_emotion_init(&e);
    swarm_feeling_t joy = {SWARM_EMO_JOY, 5}, calm = {SWARM_EMO_CALM, 3};
    swarm_emotion_set_feeling(&e, 8, joy);
    swarm_emotion_set_feeling(&e, 9, calm);
    swarm_reserve_t clean, dirty;
    memset(&clean, 0, sizeof clean);
    memset(&dirty, 0xA5, sizeof dirty);
    CHECK(swarm_reserve_build(&b, &e, 7, &clean) == SWARM_OK &&
              swarm_reserve_build(&b, &e, 7, &dirty) == SWARM_OK,
          "reserve for 7");
    bool same = clean.total == dirty.total && clean.num_peers == dirty.num_peers;
    for (uint32_t d = 0; d < SWARM_MAX_LEVELS; d++)
        for (uint32_t k = 0; k < SWARM_EMO_COUNT; k++)
            same = same && clean.summary[d][k] == dirty.summary[d][k];
    CHECK(same, "build writes every summary cell");
    CHECK(clean.level == 0 && clean.summary[1][SWARM_EMO_JOY] == 8 &&
              clean.summary[1][SWARM_EMO_CALM] == swarm_feeling_charge(calm),
          "level 1 summarised");
    swarm_reserve_t t = clean;
    t.summary[1][SWARM_EMO_JOY]--;
    t.summary[1][SWARM_EMO_CALM]++;
    CHECK(!swarm_reserve_audit(&t, &b, &e), "charge moved inside level 1 is caught");
}

int main(void)
{
    printf("test_swarm_props: swarm economy invariants over random swarms\n");
    test_arith();
    test_budget();
    test_market_edges();
    test_ledger_reserve_edges();
    test_market_random();
    test_cycle_value_bound();
    test_full_roster();
    test_reset_state();
    test_witness_edges();
    printf("%lu checks, %d failures\n", checks, failures);
    if (failures) {
        printf("[FAIL] test_swarm_props\n");
        return 1;
    }
    printf("[PASS] test_swarm_props\n");
    return 0;
}
