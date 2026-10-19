/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* swarm_denom.c — token denominations and octane (rules in swarm_denom.h). */
#include "swarm_denom.h"

/* D1: F(k+2) for k = 0 .. 63, checked against swarm_fib in the tests. */
static const uint64_t FACE[SWARM_DENOM_TIERS] = {
    1ULL,
    2ULL,
    3ULL,
    5ULL,
    8ULL,
    13ULL,
    21ULL,
    34ULL,
    55ULL,
    89ULL,
    144ULL,
    233ULL,
    377ULL,
    610ULL,
    987ULL,
    1597ULL,
    2584ULL,
    4181ULL,
    6765ULL,
    10946ULL,
    17711ULL,
    28657ULL,
    46368ULL,
    75025ULL,
    121393ULL,
    196418ULL,
    317811ULL,
    514229ULL,
    832040ULL,
    1346269ULL,
    2178309ULL,
    3524578ULL,
    5702887ULL,
    9227465ULL,
    14930352ULL,
    24157817ULL,
    39088169ULL,
    63245986ULL,
    102334155ULL,
    165580141ULL,
    267914296ULL,
    433494437ULL,
    701408733ULL,
    1134903170ULL,
    1836311903ULL,
    2971215073ULL,
    4807526976ULL,
    7778742049ULL,
    12586269025ULL,
    20365011074ULL,
    32951280099ULL,
    53316291173ULL,
    86267571272ULL,
    139583862445ULL,
    225851433717ULL,
    365435296162ULL,
    591286729879ULL,
    956722026041ULL,
    1548008755920ULL,
    2504730781961ULL,
    4052739537881ULL,
    6557470319842ULL,
    10610209857723ULL,
    17167680177565ULL,
};

uint64_t swarm_denom_face(uint32_t tier)
{
    if (tier >= SWARM_DENOM_TIERS) return 0;
    return FACE[tier];
}

bool swarm_denom_is_super(uint32_t tier)
{
    return tier >= SWARM_DENOM_SUPER_TIER && tier < SWARM_DENOM_TIERS;
}

static void clear_counts(swarm_wallet_t *w)
{
    for (uint32_t k = 0; k < SWARM_DENOM_TIERS; k++) w->count[k] = 0;
}

swarm_status_t swarm_denom_init(swarm_wallet_t *w, uint32_t model_id, uint32_t octane)
{
    if (!w || octane >= SWARM_DENOM_TIERS) return SWARM_ERR_ARG;
    w->model_id = model_id;
    w->octane = octane;
    clear_counts(w);
    return SWARM_OK;
}

bool swarm_denom_value(const swarm_wallet_t *w, uint64_t *out)
{
    if (out) *out = 0;
    if (!w || !out) return false;
    uint64_t v = 0;
    for (uint32_t k = 0; k < SWARM_DENOM_TIERS; k++) {
        uint64_t c = w->count[k];
        uint64_t f = FACE[k];
        if (c > UINT64_MAX / f) return false;
        uint64_t p = c * f;
        if (p > UINT64_MAX - v) return false;
        v += p;
    }
    *out = v;
    return true;
}

uint64_t swarm_denom_pieces(const swarm_wallet_t *w)
{
    if (!w) return 0;
    uint64_t n = 0;
    for (uint32_t k = 0; k < SWARM_DENOM_TIERS; k++) {
        if (w->count[k] > UINT64_MAX - n) return UINT64_MAX;
        n += w->count[k];
    }
    return n;
}

/* D3: the canonical counts of `value` at octane o, into c[]. */
static void canonical(uint64_t value, uint32_t o, uint64_t *c)
{
    for (uint32_t k = 0; k < SWARM_DENOM_TIERS; k++) c[k] = 0;
    uint64_t top = FACE[o];
    c[o] = value / top;
    value %= top;
    for (uint32_t k = o; k-- > 0;) {
        uint64_t f = FACE[k];
        if (value >= f) {
            c[k] = 1;
            value -= f;
        }
    }
}

bool swarm_denom_is_canonical(const swarm_wallet_t *w)
{
    uint64_t v;
    if (!w || w->octane >= SWARM_DENOM_TIERS || !swarm_denom_value(w, &v)) return false;
    uint64_t c[SWARM_DENOM_TIERS];
    canonical(v, w->octane, c);
    for (uint32_t k = 0; k < SWARM_DENOM_TIERS; k++)
        if (c[k] != w->count[k]) return false;
    return true;
}

swarm_status_t swarm_denom_set(swarm_wallet_t *w, uint64_t value)
{
    if (!w || w->octane >= SWARM_DENOM_TIERS) return SWARM_ERR_ARG;
    canonical(value, w->octane, w->count);
    return SWARM_OK;
}

swarm_status_t swarm_denom_compress(swarm_wallet_t *w)
{
    uint64_t v;
    if (!w || !swarm_denom_value(w, &v)) return SWARM_ERR_ARG;
    return swarm_denom_set(w, v);
}

swarm_status_t swarm_denom_fuse(swarm_wallet_t *w, uint32_t tier)
{
    /* tier <= octane - 2, written so that no huge tier can wrap into range. */
    if (!w || w->octane >= SWARM_DENOM_TIERS || w->octane < 2 || tier > w->octane - 2)
        return SWARM_ERR_ARG;
    if (w->count[tier] == 0 || w->count[tier + 1] == 0) return SWARM_ERR_ARG;
    if (w->count[tier + 2] == UINT64_MAX) return SWARM_ERR_ARG;
    w->count[tier]--;
    w->count[tier + 1]--;
    w->count[tier + 2]++;
    return SWARM_OK;
}

swarm_status_t swarm_denom_split(swarm_wallet_t *w, uint32_t tier)
{
    if (!w || tier == 0 || tier >= SWARM_DENOM_TIERS || w->count[tier] == 0) return SWARM_ERR_ARG;
    if (tier == 1) {
        if (w->count[0] > UINT64_MAX - 2) return SWARM_ERR_ARG;
        w->count[1]--;
        w->count[0] += 2;
        return SWARM_OK;
    }
    if (w->count[tier - 1] == UINT64_MAX || w->count[tier - 2] == UINT64_MAX) return SWARM_ERR_ARG;
    w->count[tier]--;
    w->count[tier - 1]++;
    w->count[tier - 2]++;
    return SWARM_OK;
}

swarm_status_t swarm_denom_set_octane(swarm_wallet_t *w, uint32_t octane)
{
    uint64_t v;
    if (!w || octane >= SWARM_DENOM_TIERS || !swarm_denom_value(w, &v)) return SWARM_ERR_ARG;
    w->octane = octane;
    return swarm_denom_set(w, v);
}

swarm_status_t swarm_denom_spend(swarm_wallet_t *w, uint64_t amount)
{
    uint64_t v;
    if (!w || !swarm_denom_value(w, &v) || amount > v) return SWARM_ERR_ARG;
    return swarm_denom_set(w, v - amount);
}

swarm_status_t swarm_denom_transfer(swarm_wallet_t *from, swarm_wallet_t *to, uint64_t amount)
{
    uint64_t vf, vt;
    if (!from || !to || !swarm_denom_value(from, &vf) || !swarm_denom_value(to, &vt))
        return SWARM_ERR_ARG;
    if (amount > vf) return SWARM_ERR_ARG;
    if (from == to) return SWARM_OK;
    if (amount > UINT64_MAX - vt) return SWARM_ERR_ARG;
    swarm_denom_set(from, vf - amount);
    return swarm_denom_set(to, vt + amount);
}

/* ------------------------------------------------------------ D8 wire code */

static bool bit_get(const uint8_t *buf, uint32_t i)
{
    return (buf[i >> 3] >> (i & 7u)) & 1u;
}

static void bit_put(uint8_t *buf, uint32_t i, bool b)
{
    if (b)
        buf[i >> 3] |= (uint8_t) (1u << (i & 7u));
    else
        buf[i >> 3] &= (uint8_t) ~(1u << (i & 7u));
}

swarm_status_t swarm_denom_code_put(uint8_t *buf, uint32_t cap, uint32_t *pos, uint64_t v)
{
    if (!buf || !pos || v == UINT64_MAX) return SWARM_ERR_ARG;
    uint64_t n = v + 1;
    /* Highest Fibonacci index used: the largest j with F(j) <= n, j >= 2. */
    uint32_t j = 2;
    while (j < 93 && swarm_fib(j + 1) <= n) j++;
    uint32_t len = j; /* bits for F(2)..F(j), then the closing 1 */
    if (*pos > cap || len > cap - *pos) return SWARM_ERR_ARG;
    for (uint32_t i = j + 1; i-- > 2;) {
        uint64_t f = swarm_fib(i);
        bool b = (n >= f);
        if (b) n -= f;
        bit_put(buf, *pos + i - 2, b);
    }
    bit_put(buf, *pos + j - 1, true);
    *pos += len;
    return SWARM_OK;
}

swarm_status_t swarm_denom_code_get(const uint8_t *buf, uint32_t cap, uint32_t *pos, uint64_t *v)
{
    if (!buf || !pos || !v) return SWARM_ERR_ARG;
    uint64_t n = 0;
    bool prev = false;
    for (uint32_t i = *pos; i < cap; i++) {
        bool b = bit_get(buf, i);
        if (b && prev) {
            *v = n - 1;
            *pos = i + 1;
            return SWARM_OK;
        }
        if (b) {
            uint32_t idx = i - *pos + 2;
            uint64_t f = swarm_fib(idx);
            if (f == 0 || f > UINT64_MAX - n) return SWARM_ERR_ARG;
            n += f;
        }
        prev = b;
    }
    return SWARM_ERR_ARG; /* truncated: no closing 11 */
}

/* --------------------------------------------------------- D9 the budget */

static swarm_wallet_t *wallet_of(swarm_wallet_t *wallets, uint32_t n, uint32_t model_id)
{
    for (uint32_t i = 0; i < n; i++)
        if (wallets[i].model_id == model_id) return &wallets[i];
    return 0;
}

swarm_status_t swarm_denom_payout(const swarm_budget_t *b, swarm_wallet_t *wallets, uint32_t n)
{
    if (!b || !wallets || n != b->num_slots) return SWARM_ERR_ARG;
    if (!b->cycle_open) return SWARM_ERR_NO_CYCLE;
    for (uint32_t i = 0; i < n; i++)
        if (wallets[i].octane >= SWARM_DENOM_TIERS) return SWARM_ERR_ARG;
    for (uint32_t i = 0; i < n; i++) {
        const swarm_slot_t *s = &b->slots[i];
        wallets[i].model_id = s->model_id;
        swarm_denom_set(&wallets[i], s->allotted - s->used);
    }
    return SWARM_OK;
}

swarm_status_t swarm_denom_consume(swarm_budget_t *b, swarm_wallet_t *wallets, uint32_t n,
                                   uint32_t model_id, uint64_t requested, uint64_t *granted)
{
    if (!b || !wallets || !granted || n != b->num_slots) return SWARM_ERR_ARG;
    *granted = 0;
    swarm_wallet_t *w = wallet_of(wallets, n, model_id);
    if (!w) return SWARM_ERR_NO_MODEL;
    uint64_t v;
    if (!swarm_denom_value(w, &v)) return SWARM_ERR_ARG;
    /* Never let the budget move without the wallet: check before spending. */
    uint64_t left = swarm_budget_remaining(b, model_id);
    if (v != left) return SWARM_ERR_ARG;
    swarm_status_t st = swarm_budget_consume(b, model_id, requested, granted);
    if (st != SWARM_OK) return st;
    return swarm_denom_spend(w, *granted);
}

bool swarm_denom_reconciles(const swarm_budget_t *b, const swarm_wallet_t *wallets, uint32_t n)
{
    if (!b || !wallets || n != b->num_slots) return false;
    for (uint32_t i = 0; i < n; i++) {
        const swarm_slot_t *s = &b->slots[i];
        uint64_t v;
        if (wallets[i].model_id != s->model_id) return false;
        if (!swarm_denom_value(&wallets[i], &v)) return false;
        if (v != s->allotted - s->used) return false;
    }
    return true;
}

swarm_status_t swarm_denom_end_cycle(swarm_budget_t *b, swarm_wallet_t *wallets, uint32_t n)
{
    if (!b || !wallets || n != b->num_slots) return SWARM_ERR_ARG;
    swarm_status_t st = swarm_budget_end_cycle(b);
    if (st != SWARM_OK) return st;
    for (uint32_t i = 0; i < n; i++) clear_counts(&wallets[i]);
    return SWARM_OK;
}
