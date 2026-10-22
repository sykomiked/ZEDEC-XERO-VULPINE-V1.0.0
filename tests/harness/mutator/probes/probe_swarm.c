/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* probe_swarm.c: differential probe for the swarm economy (classify.py):
 * swarm_budget.c, swarm_market.c, swarm_ledger.c, swarm_reserve.c. Runs whole
 * cycles over seeded rosters (full capacity included) and prints every
 * return value and a digest of every public struct after every step. */
#include <string.h>
#include "probe_common.h"
#include "swarm_budget.h"
#include "swarm_emotion.h"
#include "swarm_market.h"
#include "swarm_ledger.h"
#include "swarm_reserve.h"

static swarm_budget_t B;
static swarm_market_t M;
static swarm_emotion_state_t E;
static swarm_ledger_t L;
static swarm_witness_t W[SWARM_MAX_MODELS]; /* kept across cycles, as a caller would */

static uint64_t dig_b(const swarm_budget_t *b) {
    uint64_t h = 0xcbf29ce484222325ull;
    probe_mix(&h, b->num_levels); probe_mix(&h, b->tokens_per_cycle); probe_mix(&h, b->cycle);
    probe_mix(&h, *(const uint8_t *)&b->cycle_open); probe_mix(&h, b->num_slots); probe_mix(&h, b->last_unused);
    for (uint32_t d = 0; d < SWARM_MAX_LEVELS; d++) probe_mix(&h, b->level_budget[d]);
    for (uint32_t i = 0; i < SWARM_MAX_MODELS; i++) {
        const swarm_slot_t *s = &b->slots[i];
        probe_mix(&h, s->model_id); probe_mix(&h, s->level); probe_mix(&h, *(const uint8_t *)&s->active);
        probe_mix(&h, s->allotted); probe_mix(&h, s->allotted_im); probe_mix(&h, s->allotted_mk);
        probe_mix(&h, s->used);
    }
    return h;
}
static uint64_t dig_m(const swarm_market_t *m) {
    uint64_t h = 0xcbf29ce484222325ull;
    probe_mix(&h, m->n); probe_mix(&h, m->money_supply); probe_mix(&h, m->pot); probe_mix(&h, m->levied);
    probe_mix(&h, m->last_floor); probe_mix(&h, m->last_market); probe_mix(&h, m->last_unsold);
    for (uint32_t i = 0; i < SWARM_MAX_MODELS; i++) {
        const swarm_trader_t *t = &m->t[i];
        probe_mix(&h, t->model_id);
        for (uint32_t f = 0; f < SWARM_CAP_COUNT; f++) probe_mix(&h, t->cap[f]);
        probe_mix(&h, t->bid); probe_mix(&h, t->won); probe_mix(&h, t->paid);
        probe_mix(&h, t->value_cycle); probe_mix(&h, t->social_cycle);
    }
    return h;
}
static uint64_t dig_e(const swarm_emotion_state_t *e) {
    uint64_t h = 0xcbf29ce484222325ull;
    probe_mix(&h, e->mood.emotion); probe_mix(&h, e->mood.intensity); probe_mix(&h, e->num);
    probe_mix(&h, e->last_imag_pool);
    for (uint32_t i = 0; i < SWARM_MAX_MODELS; i++) {
        probe_mix(&h, e->model_id[i]); probe_mix(&h, e->feeling[i].emotion); probe_mix(&h, e->feeling[i].intensity);
    }
    return h;
}
static uint64_t dig_l(const swarm_ledger_t *l) {
    uint64_t h = 0xcbf29ce484222325ull;
    probe_mix(&h, l->head); probe_mix(&h, l->posted); probe_mix(&h, l->settled_cycles); probe_mix(&h, l->held_cycles);
    for (uint32_t i = 0; i < SWARM_LEDGER_CAP; i++) {
        const swarm_ledger_entry_t *x = &l->e[i];
        probe_mix(&h, x->cycle); probe_mix(&h, x->ledger); probe_mix(&h, x->model_id); probe_mix(&h, x->counterparty);
        probe_mix(&h, x->amount); probe_mix(&h, x->paid); probe_mix(&h, x->verdict);
    }
    return h;
}
static uint64_t dig_r(const swarm_reserve_t *r) {
    uint64_t h = 0xcbf29ce484222325ull;
    probe_mix(&h, r->owner_id); probe_mix(&h, r->level); probe_mix(&h, r->num_peers); probe_mix(&h, r->total);
    for (uint32_t i = 0; i < SWARM_RESERVE_LEVEL_MAX; i++) {
        probe_mix(&h, r->peer_id[i]); probe_mix(&h, r->peer[i].emotion); probe_mix(&h, r->peer[i].intensity);
    }
    for (uint32_t d = 0; d < SWARM_MAX_LEVELS; d++)
        for (uint32_t k = 0; k < SWARM_EMO_COUNT; k++) probe_mix(&h, r->summary[d][k]);
    return h;
}

static void st(const char *tag) {
    printf("%s b%016llx m%016llx e%016llx l%016llx\n", tag, (unsigned long long)dig_b(&B),
           (unsigned long long)dig_m(&M), (unsigned long long)dig_e(&E), (unsigned long long)dig_l(&L));
}

static void pure(void) {
    for (uint32_t n = 0; n <= 95; n++) printf("fib %u %llu\n", n, (unsigned long long)swarm_fib(n));
    for (uint32_t d = 0; d <= SWARM_MAX_LEVELS + 1; d++) {
        printf("cap %u %u w", d, swarm_level_capacity(d));
        for (uint32_t l = 0; l <= SWARM_MAX_LEVELS + 1; l++) printf(" %llu", (unsigned long long)swarm_level_weight(d, l));
        printf("\n");
    }
    static const uint64_t v[] = {0, 1, 2, 3, 7, 21, 0xFFFFFFFFull, 0x100000000ull, 0x123456789ABCDEFull,
                                 UINT64_MAX - 1, UINT64_MAX};
    for (unsigned i = 0; i < 11; i++)
        for (unsigned j = 0; j < 11; j++)
            for (unsigned k = 0; k < 11; k += 2) {
                uint64_t r = 0xEEEE;
                uint64_t q = swarm_muldiv(v[i], v[j], v[k], &r);
                printf("md %u %u %u %llx %llx\n", i, j, k, (unsigned long long)q, (unsigned long long)r);
            }
    printf("md nullrem %llu %llu\n", (unsigned long long)swarm_muldiv(7, 3, 2, NULL),
           (unsigned long long)swarm_muldiv(7, 3, 0, NULL));
    uint64_t w[SWARM_MAX_MODELS], out[SWARM_MAX_MODELS + 1];
    for (uint64_t seed = 1; seed <= 60; seed++) {
        probe_seed(seed);
        uint32_t n = seed % 7 == 0 ? SWARM_MAX_MODELS : 1 + probe_below(SWARM_MAX_MODELS);
        for (uint32_t i = 0; i < n; i++) w[i] = probe_below(4) ? probe_below(30) : 0;
        uint64_t total = probe_below(3) ? probe_below(1000) : probe_below(5);
        out[n] = 0xABCD;
        swarm_split_lr(total, w, n, out);
        printf("lr %llu n%u", (unsigned long long)total, n);
        for (uint32_t i = 0; i <= n; i++) printf(" %llu", (unsigned long long)out[i]);
        printf("\n");
        uint64_t cap = probe_below(400);
        out[n] = 0xABCD;
        uint64_t un = swarm_capped_split(total, w, n, cap, out);
        printf("cs %llu cap%llu -> %llu", (unsigned long long)total, (unsigned long long)cap, (unsigned long long)un);
        for (uint32_t i = 0; i <= n; i++) printf(" %llu", (unsigned long long)out[i]);
        printf("\n");
    }
    for (int f = -1; f <= SWARM_CAP_COUNT; f++) printf("crown %d %d\n", f, (int)swarm_cap_is_crown((swarm_cap_t)f));
}

/* a roster: model ids 100+i, levels spread; `full` registers to capacity */
static void roster(uint64_t seed, uint32_t levels, uint64_t T, int full) {
    probe_seed(seed);
    memset(&B, 0x5A, sizeof B);
    memset(&M, 0x5A, sizeof M);
    memset(&E, 0x5A, sizeof E);
    memset(&L, 0x5A, sizeof L);
    printf("init %d\n", (int)swarm_budget_init(&B, levels, T));
    swarm_market_init(&M);
    swarm_emotion_init(&E);
    swarm_ledger_init(&L);
    st("fresh");
    uint32_t want = full ? 200 : 1 + probe_below(30);
    for (uint32_t i = 0; i < want; i++) {
        uint32_t lv = full ? i % levels : probe_below(levels + 1);
        uint32_t id = 100 + (probe_below(9) ? i : probe_below(i + 1));
        if (i == 0 && seed % 4 == 1) id = 0; /* model id 0 is not reserved */
        int r = (int)swarm_budget_register(&B, id, lv);
        int j = (int)swarm_market_join(&M, id, probe_below(3) ? probe_below(500) : 0);
        swarm_feeling_t f = {(swarm_emotion_t)probe_below(SWARM_EMO_COUNT + 1), (uint8_t)probe_below(7)};
        int e = (int)swarm_emotion_set_feeling(&E, id, f);
        if (!full || r != 0) printf("reg %u l%u -> %d %d %d\n", id, lv, r, j, e);
    }
    printf("slots %u traders %u feelings %u\n", B.num_slots, M.n, E.num);
    swarm_feeling_t mood = {(swarm_emotion_t)probe_below(SWARM_EMO_COUNT), (uint8_t)probe_below(6)};
    printf("mood %d\n", (int)swarm_emotion_set_mood(&E, mood));
    st("roster");
}

static void cycle(int c, int use_emo) {
    /* some models sleep, some bid */
    for (uint32_t i = 0; i < B.num_slots; i++) {
        if (probe_below(6) == 0) swarm_budget_set_active(&B, B.slots[i].model_id, probe_below(2));
        const swarm_trader_t *t = swarm_market_trader(&M, B.slots[i].model_id);
        if (t && probe_below(2)) {
            uint64_t amt = probe_below(4) ? probe_below((uint32_t)(t->cap[SWARM_CAP_FINANCIAL] + 1)) : t->cap[SWARM_CAP_FINANCIAL] + 1;
            printf(" bid %u %llu %d\n", t->model_id, (unsigned long long)amt, (int)swarm_market_bid(&M, t->model_id, amt));
        }
    }
    swarm_budget_t pre_b = B;
    swarm_market_t pre_m = M;
    swarm_emotion_state_t pre_e = E;
    int r = (int)swarm_market_begin_cycle(&B, &M, use_emo ? &E : NULL);
    printf("c%d begin %d floor %llu market %llu unsold %llu pool %llu\n", c, r, (unsigned long long)M.last_floor,
           (unsigned long long)M.last_market, (unsigned long long)M.last_unsold, (unsigned long long)E.last_imag_pool);
    st(" begun");
    for (uint32_t i = 0; i < B.num_slots; i++)
        printf("  s%u id%u l%u a%d al%llu im%llu mk%llu u%llu rem%llu\n", i, B.slots[i].model_id, B.slots[i].level,
               (int)B.slots[i].active, (unsigned long long)B.slots[i].allotted,
               (unsigned long long)B.slots[i].allotted_im, (unsigned long long)B.slots[i].allotted_mk,
               (unsigned long long)B.slots[i].used,
               (unsigned long long)swarm_budget_remaining(&B, B.slots[i].model_id));
    for (uint32_t k = 0; k < M.n; k++) {
        printf("  t%u id%u", k, M.t[k].model_id);
        for (uint32_t f = 0; f < SWARM_CAP_COUNT; f++) printf(" %llu", (unsigned long long)M.t[k].cap[f]);
        printf(" bid%llu won%llu paid%llu\n", (unsigned long long)M.t[k].bid, (unsigned long long)M.t[k].won,
               (unsigned long long)M.t[k].paid);
    }
    /* the witnesses see the posted budget against a replay of the cycle */
    swarm_witness_t *w = W;
    swarm_budget_t posted = B;
    if (c % 4 == 3 && posted.num_slots) posted.slots[probe_below(posted.num_slots)].allotted_mk ^= 1; /* a forger */
    if (c % 4 == 1 && posted.num_slots < SWARM_MAX_MODELS) { /* a forger claims an extra, empty model */
        memset(&posted.slots[posted.num_slots], 0, sizeof posted.slots[0]);
        posted.slots[posted.num_slots].active = true;
        posted.num_slots++;
    }
    uint32_t nw = swarm_witness_cycle(&posted, &pre_b, &pre_m, use_emo ? &pre_e : NULL, w);
    printf("  nw %u", nw);
    for (uint32_t k = 0; k < nw; k++) printf(" %u/%u/%d", w[k].model_id, w[k].witness_id, (int)w[k].verdict);
    printf("\n");
    for (uint32_t i = 0; i <= B.num_slots + 1; i++) printf(" wf%u", swarm_witness_for(&B, i));
    printf("\n");
    /* reserves for every model, and one forged */
    for (uint32_t i = 0; i < B.num_slots; i++) {
        swarm_reserve_t rs;
        memset(&rs, 0x5A, sizeof rs);
        int rr = (int)swarm_reserve_build(&B, &E, B.slots[i].model_id, &rs);
        printf("  rsv %u %d n%u tot%llu audit %d", B.slots[i].model_id, rr, rs.num_peers,
               (unsigned long long)rs.total, (int)swarm_reserve_audit(&rs, &B, &E));
        printf(" %016llx\n", (unsigned long long)dig_r(&rs));
        if (rr == 0 && i % 3 == 0) {
            uint32_t d = probe_below(SWARM_MAX_LEVELS), k = probe_below(SWARM_EMO_COUNT);
            rs.summary[d][k] += 1;
            printf("   forged d%u k%u audit %d\n", d, k, (int)swarm_reserve_audit(&rs, &B, &E));
            rs.summary[d][k] -= 1;
            for (uint32_t dd = 0; dd < SWARM_MAX_LEVELS; dd++) /* move charge: totals still agree */
                for (uint32_t kk = 1; kk + 1 < SWARM_EMO_COUNT; kk++)
                    if (dd != rs.level && rs.summary[dd][kk]) {
                        rs.summary[dd][kk]--;
                        rs.summary[dd][kk + 1]++;
                        printf("   moved d%u k%u audit %d\n", dd, kk, (int)swarm_reserve_audit(&rs, &B, &E));
                        rs.summary[dd][kk + 1]--;
                        rs.summary[dd][kk]++;
                    }
        }
    }
    swarm_reserve_t rs0;
    printf("  rsv missing %d null %d %d\n", (int)swarm_reserve_build(&B, &E, 99999, &rs0),
           (int)swarm_reserve_build(NULL, &E, 1, &rs0), (int)swarm_reserve_audit(NULL, &B, &E));
    /* spend, earn */
    for (uint32_t i = 0; i < B.num_slots; i++) {
        uint64_t g = 0;
        uint64_t req = probe_below(3) ? probe_below(60) : UINT64_MAX;
        int cr = (int)swarm_budget_consume(&B, B.slots[i].model_id, req, &g);
        printf("  eat %u %d %llu", B.slots[i].model_id, cr, (unsigned long long)g);
        swarm_cap_t f = (swarm_cap_t)probe_below(SWARM_CAP_COUNT + 1);
        uint64_t amt = probe_below(10) ? probe_below(50) : UINT64_MAX;
        printf(" credit %d %d\n", (int)f, (int)swarm_market_credit(&M, B.slots[i].model_id, f, amt));
    }
    for (uint32_t q = 0; q < 2; q++) { /* a model nobody registered, and id 0 */
        uint32_t id = q ? 0 : 99999;
        uint64_t g = 7;
        const swarm_trader_t *tq = swarm_market_trader(&M, id);
        printf("  ask %u rem %llu eat %d %llu act %d trader %d bid %d credit %d\n", id,
               (unsigned long long)swarm_budget_remaining(&B, id), (int)swarm_budget_consume(&B, id, 3, &g),
               (unsigned long long)g, (int)swarm_budget_set_active(&B, id, true), tq ? (int)tq->model_id : -1,
               (int)swarm_market_bid(&M, id, 0), (int)swarm_market_credit(&M, id, SWARM_CAP_HUMAN, 1));
    }
    swarm_witness_reward(&M, w, nw);
    swarm_market_credit_frugality(&M, &B);
    st(" spent");
    printf("  ledger %d", (int)swarm_ledger_post_cycle(&L, &posted, &M, E.last_imag_pool, w, nw));
    if (c % 5 != 4) printf(" settle %d", (int)swarm_market_settle(&M)); /* sometimes a cycle opens unsettled */
    printf(" levy %d %d", (int)swarm_market_levy(&M, M.pot / 3), (int)swarm_market_levy(&M, M.pot + 1));
    printf(" release %d %d", (int)swarm_market_release(&M, M.levied / 2), (int)swarm_market_release(&M, M.levied + 1));
    printf(" conserved %d\n", (int)swarm_market_conserved(&M));
    for (uint32_t k = 0; k < M.n; k++) {
        printf("  T%u", M.t[k].model_id);
        for (uint32_t f = 0; f < SWARM_CAP_COUNT; f++) printf(" %llu", (unsigned long long)M.t[k].cap[f]);
        printf(" v%llu s%llu\n", (unsigned long long)M.t[k].value_cycle, (unsigned long long)M.t[k].social_cycle);
    }
    printf("  supply %llu pot %llu levied %llu\n", (unsigned long long)M.money_supply, (unsigned long long)M.pot,
           (unsigned long long)M.levied);
    if (c % 2) printf("  end %d unused %llu\n", (int)swarm_budget_end_cycle(&B), (unsigned long long)B.last_unused);
    printf("  ledger posted %llu head %u settled %llu held %llu\n", (unsigned long long)L.posted, L.head,
           (unsigned long long)L.settled_cycles, (unsigned long long)L.held_cycles);
    st(" done");
}

int main(void) {
    pure();
    printf("nulls %d %d %d %d %d %d\n", (int)swarm_budget_init(NULL, 1, 1), (int)swarm_budget_set_rate(NULL, 1),
           (int)swarm_budget_register(NULL, 1, 0), (int)swarm_budget_begin_cycle(NULL),
           (int)swarm_budget_end_cycle(NULL), (int)swarm_market_begin_cycle(NULL, &M, NULL));
    printf("nulls2 %d %d %d %d\n", (int)swarm_market_begin_cycle(&B, NULL, NULL), (int)swarm_market_settle(NULL),
           (int)swarm_market_conserved(NULL), (int)swarm_ledger_post_cycle(NULL, &B, &M, 0, NULL, 0));
    swarm_ledger_init(NULL);
    swarm_market_init(NULL);
    swarm_witness_reward(NULL, NULL, 0);
    static const uint64_t Ts[] = {0, 1, 2, 7, 100, 987, 100000, SWARM_MAX_TOKENS_PER_CYCLE,
                                  SWARM_MAX_TOKENS_PER_CYCLE + 1};
    for (uint32_t l = 0; l <= SWARM_MAX_LEVELS + 1; l++)
        for (unsigned t = 0; t < 9; t++) {
            memset(&B, 0x5A, sizeof B);
            int r = (int)swarm_budget_init(&B, l, Ts[t]);
            printf("init l%u T%u %d", l, t, r);
            printf(" %016llx", (unsigned long long)dig_b(&B));
            printf(" rate %d %d\n", (int)swarm_budget_set_rate(&B, Ts[t]), (int)swarm_budget_set_rate(&B, SWARM_MAX_TOKENS_PER_CYCLE + 1));
        }
    for (uint64_t seed = 1; seed <= 24; seed++) {
        uint32_t levels = 1 + (uint32_t)(seed % SWARM_MAX_LEVELS);
        int full = seed % 3 == 0;
        if (full) levels = SWARM_MAX_LEVELS;
        uint64_t T = seed % 5 == 0 ? (uint64_t)probe_below(5) : 1000 + 977 * seed;
        roster(seed, levels, T, full);
        uint64_t g = 0;
        printf("eat closed %d %llu rem %llu\n", (int)swarm_budget_consume(&B, 100, 5, &g), (unsigned long long)g,
               (unsigned long long)swarm_budget_remaining(&B, 100));
        printf("end closed %d\n", (int)swarm_budget_end_cycle(&B));
        printf("real over %d\n", (int)swarm_budget_begin_cycle_real(&B, B.tokens_per_cycle + 1));
        for (int c = 0; c < 6; c++) cycle(c, (int)((seed + c) % 3 != 0));
        printf("plain begin %d\n", (int)swarm_budget_begin_cycle(&B));
        printf("emo begin %d\n", (int)swarm_emotion_begin_cycle(&B, &E));
        st("after");
    }
    /* the ledger ring wraps */
    roster(99, SWARM_MAX_LEVELS, 5000, 1);
    for (int c = 0; c < 12; c++) cycle(c, 1);
    return 0;
}
