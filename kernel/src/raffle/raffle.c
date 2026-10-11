/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* raffle.c — the Sicilian Crown Raffle. See raffle.h. */
#include "raffle.h"
#include "pay_util.h"
#include "keccak.h"

/* ===== Little helpers ===== */

static void put64(uint8_t *p, uint64_t v)
{
    for (uint32_t i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8u * i));
}

static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < 8; i++) v |= (uint64_t) p[i] << (8u * i);
    return v;
}

/* A bounded byte builder for hash preimages. */
typedef struct {
    uint8_t buf[1280];
    size_t len;
} pre_t;

static void pre_bytes(pre_t *p, const void *d, size_t n)
{
    if (n > sizeof p->buf - p->len) n = sizeof p->buf - p->len; /* unreachable: sized below */
    pay_memcpy(p->buf + p->len, d, n);
    p->len += n;
}

static void pre_str(pre_t *p, const char *s)
{
    pre_bytes(p, s, pay_strnlen(s, 64));
}

static void pre_u64(pre_t *p, uint64_t v)
{
    uint8_t b[8];
    put64(b, v);
    pre_bytes(p, b, 8);
}

/* floor(a * num / den) for num < den, den > 0: always fits 64 bits. */
static uint64_t ratio_floor(uint64_t a, uint64_t num, uint64_t den)
{
    uint64_t rem;
    pay_u128 q = pay_udiv128_64(pay_mul64(a, num), den, &rem);
    return q.lo; /* q <= a because num < den */
}

/* ===== Config ===== */

void raffle_config_default(raffle_config_t *c)
{
    if (!c) return;
    pay_memset(c, 0, sizeof *c);
    c->jurisdiction_permits = false;
    c->free_entry = false;
    c->min_rate.num = 1;
    c->min_rate.den = 100;
    c->max_rate.num = RAFFLE_RATE_CEIL_NUM;
    c->max_rate.den = RAFFLE_RATE_CEIL_DEN;
    c->skim_num = RAFFLE_SKIM_NUM;
    c->skim_den = RAFFLE_SKIM_DEN;
    c->grant_num = 5;
    c->grant_den = 55;
    c->fib_index = RAFFLE_DEFAULT_FIB_INDEX;
    c->min_reveals = 0;
    c->share_num = 8;
    c->share_den = 21;
    c->mode = RAFFLE_MODE_SAVINGS; /* M1 */
    c->rebate_num = 1;             /* S6 */
    c->rebate_den = 5;
    c->luck_cap = RAFFLE_LUCK_DEFAULT; /* W1 */
}

int raffle_rate_cmp(raffle_rate_t a, raffle_rate_t b)
{
    return pay_u128_cmp(pay_mul64(a.num, b.den), pay_mul64(b.num, a.den));
}

/* Small unsigned bignums for S3: up to five 64-bit limbs, little-endian. */
#define BIG_LIMBS 5
typedef struct {
    uint64_t l[BIG_LIMBS];
} big_t;

static void big_set(big_t *a, uint64_t v)
{
    pay_memset(a, 0, sizeof *a);
    a->l[0] = v;
}

static void big_mul64(big_t *a, uint64_t v) /* the callers keep it inside 320 bits */
{
    uint64_t carry = 0;
    for (int i = 0; i < BIG_LIMBS; i++) {
        pay_u128 p = pay_u128_add64(pay_mul64(a->l[i], v), carry);
        a->l[i] = p.lo;
        carry = p.hi;
    }
}

static void big_add(big_t *a, const big_t *b)
{
    uint64_t carry = 0;
    for (int i = 0; i < BIG_LIMBS; i++) {
        uint64_t x = a->l[i] + carry;
        uint64_t c1 = x < carry ? 1u : 0u;
        a->l[i] = x + b->l[i];
        carry = c1 + (a->l[i] < x ? 1u : 0u);
    }
}

static int big_cmp(const big_t *a, const big_t *b)
{
    for (int i = BIG_LIMBS - 1; i >= 0; i--)
        if (a->l[i] != b->l[i]) return a->l[i] < b->l[i] ? -1 : 1;
    return 0;
}

/* S3: skim + grant + nav + rebate < 1, exactly: with the ratios that are on
 * (den != 0), sum_i num_i * prod_{j != i} den_j < prod_j den_j. At most four
 * 64-bit factors per product (256 bits), so five limbs never overflow. */
static bool split_fits(const raffle_config_t *c)
{
    uint64_t num[4], den[4];
    int n = 0;
    num[n] = c->skim_num;
    den[n++] = c->skim_den;
    if (c->grant_den) num[n] = c->grant_num, den[n++] = c->grant_den;
    if (c->nav_den) num[n] = c->nav_num, den[n++] = c->nav_den;
    if (c->rebate_den) num[n] = c->rebate_num, den[n++] = c->rebate_den;
    big_t sum, prod, t;
    big_set(&sum, 0);
    big_set(&prod, 1);
    for (int i = 0; i < n; i++) {
        big_mul64(&prod, den[i]);
        big_set(&t, num[i]);
        for (int j = 0; j < n; j++)
            if (j != i) big_mul64(&t, den[j]);
        big_add(&sum, &t);
    }
    return big_cmp(&sum, &prod) < 0;
}

raffle_status_t raffle_config_check(const raffle_config_t *c)
{
    static const raffle_rate_t ceil = {RAFFLE_RATE_CEIL_NUM, RAFFLE_RATE_CEIL_DEN};
    if (!c) return RAFFLE_ERR_ARG;
    if (c->min_rate.den == 0 || c->max_rate.den == 0 || c->min_rate.num == 0)
        return RAFFLE_ERR_CONFIG;
    if (raffle_rate_cmp(c->min_rate, c->max_rate) > 0) return RAFFLE_ERR_CONFIG;
    if (raffle_rate_cmp(c->max_rate, ceil) > 0) return RAFFLE_ERR_CONFIG; /* F2 */
    if (c->skim_den == 0 || c->skim_num >= c->skim_den) return RAFFLE_ERR_CONFIG;
    if (c->grant_den && c->grant_num >= c->grant_den) return RAFFLE_ERR_CONFIG;
    if (c->nav_den && c->nav_num >= c->nav_den) return RAFFLE_ERR_CONFIG;
    if (c->rebate_den && c->rebate_num >= c->rebate_den) return RAFFLE_ERR_CONFIG;
    if (c->mode > RAFFLE_MODE_PAID) return RAFFLE_ERR_CONFIG;
    if (c->luck_cap < 1 || c->luck_cap > RAFFLE_LUCK_CEIL) return RAFFLE_ERR_CONFIG;
    if (!split_fits(c)) return RAFFLE_ERR_CONFIG; /* S3 */
    if (c->fib_index < RAFFLE_FIB_MIN || c->fib_index > RAFFLE_FIB_MAX) return RAFFLE_ERR_CONFIG;
    if (c->min_reveals > RAFFLE_MAX_PARTIES) return RAFFLE_ERR_CONFIG;
    if (c->share_den && (c->share_num == 0 || c->share_num > c->share_den))
        return RAFFLE_ERR_CONFIG;
    return RAFFLE_OK;
}

uint64_t raffle_fib(uint32_t n)
{
    if (n < 1 || n > 93) return 0;
    uint64_t a = 1, b = 1; /* F(1), F(2) */
    for (uint32_t i = 2; i < n; i++) {
        uint64_t t = a + b;
        a = b;
        b = t;
    }
    return n == 1 ? a : b;
}

raffle_status_t raffle_quote(const raffle_config_t *c, uint64_t amount, const raffle_rate_t *rate,
                             uint64_t *fee, uint64_t *tickets)
{
    raffle_status_t st = raffle_config_check(c);
    if (st != RAFFLE_OK) return st;
    raffle_rate_t r = rate ? *rate : c->min_rate;
    if (r.den == 0 || raffle_rate_cmp(r, c->min_rate) < 0 || raffle_rate_cmp(r, c->max_rate) > 0)
        return RAFFLE_ERR_RATE;
    uint64_t f = ratio_floor(amount, r.num, r.den); /* F2: r <= 1/2, fits */
    uint64_t n = f;                                 /* F3: one ticket per fee unit */
    if (fee) *fee = f;
    if (tickets) *tickets = n;
    return RAFFLE_OK;
}

/* ===== Epoch lifecycle ===== */

static void genesis_chain(uint64_t epoch, const uint8_t prev_link[RAFFLE_HASH_LEN],
                          uint8_t out[RAFFLE_HASH_LEN])
{
    pre_t p;
    p.len = 0;
    pre_str(&p, "ZXV-RAFFLE-GENESIS");
    pre_u64(&p, epoch);
    pre_bytes(&p, prev_link, RAFFLE_HASH_LEN);
    sha3_256(p.buf, p.len, out);
}

raffle_status_t raffle_init(raffle_t *r, const raffle_config_t *c, uint64_t epoch,
                            const uint8_t prev_link[RAFFLE_HASH_LEN], raffle_range_t *ranges,
                            uint32_t range_cap)
{
    static const uint8_t zero[RAFFLE_HASH_LEN] = {0};
    if (!r || !c || !ranges || range_cap == 0) return RAFFLE_ERR_ARG;
    raffle_status_t st = raffle_config_check(c);
    if (st != RAFFLE_OK) return st;
    pay_memset(r, 0, sizeof *r);
    r->cfg = *c;
    r->epoch = epoch;
    r->threshold = c->threshold ? c->threshold : raffle_fib(c->fib_index); /* E1 */
    r->phase = RAFFLE_OPEN;
    r->ranges = ranges;
    r->range_cap = range_cap;
    genesis_chain(epoch, prev_link ? prev_link : zero, r->chain);
    return RAFFLE_OK;
}

void raffle_commitment(uint64_t epoch, const uint8_t party[RAFFLE_ID_LEN],
                       const uint8_t secret[RAFFLE_HASH_LEN], uint8_t out[RAFFLE_HASH_LEN])
{
    pre_t p;
    p.len = 0;
    pre_str(&p, "ZXV-RAFFLE-COMMIT");
    pre_u64(&p, epoch);
    pre_bytes(&p, party, RAFFLE_ID_LEN);
    pre_bytes(&p, secret, RAFFLE_HASH_LEN);
    sha3_256(p.buf, p.len, out);
}

static int find_party(const raffle_t *r, const uint8_t party[RAFFLE_ID_LEN])
{
    for (uint32_t i = 0; i < r->n_parties; i++)
        if (pay_memeq(r->party[i], party, RAFFLE_ID_LEN)) return (int) i;
    return -1;
}

raffle_status_t raffle_commit(raffle_t *r, const uint8_t party[RAFFLE_ID_LEN],
                              const uint8_t commitment[RAFFLE_HASH_LEN])
{
    if (!r || !party || !commitment) return RAFFLE_ERR_ARG;
    if (r->phase != RAFFLE_OPEN || r->tickets != 0) return RAFFLE_ERR_PHASE;
    if (find_party(r, party) >= 0 || r->n_parties >= RAFFLE_MAX_PARTIES) return RAFFLE_ERR_PARTY;
    pay_memcpy(r->party[r->n_parties], party, RAFFLE_ID_LEN);
    pay_memcpy(r->commit[r->n_parties], commitment, RAFFLE_HASH_LEN);
    r->n_parties++;
    return RAFFLE_OK;
}

/* E3: the epoch header and its hash, fixed at close. */
static void header_hash(const raffle_t *r, uint8_t out[RAFFLE_HASH_LEN])
{
    pre_t p;
    p.len = 0;
    pre_str(&p, "ZXV-RAFFLE-HDR7");
    pre_u64(&p, r->epoch);
    pre_u64(&p, r->cfg.min_rate.num);
    pre_u64(&p, r->cfg.min_rate.den);
    pre_u64(&p, r->cfg.max_rate.num);
    pre_u64(&p, r->cfg.max_rate.den);
    pre_u64(&p, r->cfg.skim_num);
    pre_u64(&p, r->cfg.skim_den);
    pre_u64(&p, r->cfg.grant_num);
    pre_u64(&p, r->cfg.grant_den);
    pre_u64(&p, r->cfg.nav_num);
    pre_u64(&p, r->cfg.nav_den);
    pre_u64(&p, r->cfg.mode);
    pre_u64(&p, r->cfg.rebate_num);
    pre_u64(&p, r->cfg.rebate_den);
    pre_u64(&p, r->cfg.luck_cap);
    pre_u64(&p, r->threshold);
    pre_u64(&p, r->cfg.share_num);
    pre_u64(&p, r->cfg.share_den);
    pre_u64(&p, r->tickets);
    pre_u64(&p, r->pool);
    pre_bytes(&p, r->chain, RAFFLE_HASH_LEN);
    pre_u64(&p, r->n_parties);
    for (uint32_t i = 0; i < r->n_parties; i++) {
        pre_bytes(&p, r->party[i], RAFFLE_ID_LEN);
        pre_bytes(&p, r->commit[i], RAFFLE_HASH_LEN);
    }
    sha3_256(p.buf, p.len, out);
}

raffle_status_t raffle_set_tally(raffle_t *r, raffle_tally_t *tally, uint32_t cap)
{
    if (!r || !tally || cap < 4 || (cap & (cap - 1))) return RAFFLE_ERR_ARG;
    if (r->phase != RAFFLE_OPEN || r->tickets != 0) return RAFFLE_ERR_PHASE;
    pay_memset(tally, 0, (size_t) cap * sizeof *tally);
    r->tally = tally;
    r->tally_cap = cap;
    r->tally_len = 0;
    return RAFFLE_OK;
}

/* The tally slot of `owner`: its own slot, else the empty slot where it
 * would go. Never loops forever: the table is at most 3/4 full. */
static uint32_t tally_slot(const raffle_t *r, const uint8_t owner[RAFFLE_ID_LEN])
{
    uint32_t mask = r->tally_cap - 1, i = (uint32_t) get64(owner) & mask;
    while (r->tally[i].held && !pay_memeq(r->tally[i].owner, owner, RAFFLE_ID_LEN))
        i = (i + 1) & mask;
    return i;
}

static uint64_t held_by(const raffle_t *r, const uint8_t owner[RAFFLE_ID_LEN])
{
    if (r->tally) return r->tally[tally_slot(r, owner)].held;
    uint64_t held = 0;
    for (uint32_t i = 0; i < r->range_len; i++)
        if (pay_memeq(r->ranges[i].owner, owner, RAFFLE_ID_LEN)) held += r->ranges[i].count;
    return held;
}

uint64_t raffle_room(const raffle_t *r, const uint8_t owner[RAFFLE_ID_LEN])
{
    if (!r || !owner) return 0;
    if (!r->cfg.share_den) return UINT64_MAX;
    uint64_t cap = ratio_floor(r->threshold, r->cfg.share_num, r->cfg.share_den);
    if (cap == 0) cap = 1;
    uint64_t held = held_by(r, owner);
    return held >= cap ? 0 : cap - held;
}

raffle_status_t raffle_mint_check(const raffle_t *r, const uint8_t owner[RAFFLE_ID_LEN],
                                  uint64_t fee, uint64_t count)
{
    uint64_t t;
    if (!r || !owner) return RAFFLE_ERR_ARG;
    if (!r->cfg.jurisdiction_permits) return RAFFLE_ERR_DISABLED;
    if (r->phase != RAFFLE_OPEN) return RAFFLE_ERR_PHASE;
    if (count > raffle_room(r, owner)) return RAFFLE_ERR_CAP; /* F6 */
    if (!pay_add_ok(r->pool, fee, &t)) return RAFFLE_ERR_OVERFLOW;
    if (count && !pay_add_ok(r->tickets, count, &t)) return RAFFLE_ERR_OVERFLOW;
    if (count && r->range_len >= r->range_cap) return RAFFLE_ERR_FULL;
    if (count && r->tally && r->tally[tally_slot(r, owner)].held == 0 &&
        r->tally_len + 1 > r->tally_cap - r->tally_cap / 4)
        return RAFFLE_ERR_FULL;
    return RAFFLE_OK;
}

/* Mint `count` tickets to `owner` for `fee` (kind 0 = paid, 1 = free, 2 = a
 * ladder share, 3 = weighted or savings) at `rate` (0/1 for a free entry). */
static raffle_status_t mint(raffle_t *r, const uint8_t owner[RAFFLE_ID_LEN], uint64_t amount,
                            raffle_rate_t rate, uint64_t fee, uint64_t count, uint8_t kind,
                            raffle_receipt_t *out)
{
    raffle_status_t st = raffle_mint_check(r, owner, fee, count);
    if (st != RAFFLE_OK) return st;
    uint64_t pool2 = r->pool + fee, tickets2 = r->tickets + count;

    uint64_t first = r->tickets;
    if (count) {
        raffle_range_t *g = &r->ranges[r->range_len++];
        pay_memcpy(g->owner, owner, RAFFLE_ID_LEN);
        g->first = first;
        g->count = count;
        g->fee = fee;
        if (r->tally) {
            raffle_tally_t *t = &r->tally[tally_slot(r, owner)];
            if (t->held == 0) {
                pay_memcpy(t->owner, owner, RAFFLE_ID_LEN);
                r->tally_len++;
            }
            t->held += count; /* <= tickets, which did not overflow */
        }
    }
    /* E3: fold the entry into the chain. */
    pre_t p;
    p.len = 0;
    pre_str(&p, "ZXV-RAFFLE-ENTRY");
    pre_bytes(&p, r->chain, RAFFLE_HASH_LEN);
    pre_bytes(&p, owner, RAFFLE_ID_LEN);
    pre_u64(&p, amount);
    pre_u64(&p, rate.num);
    pre_u64(&p, rate.den);
    pre_u64(&p, fee);
    pre_u64(&p, first);
    pre_u64(&p, count);
    pre_bytes(&p, &kind, 1);
    sha3_256(p.buf, p.len, r->chain);

    r->pool = pool2;
    r->tickets = tickets2;
    if (kind == 1)
        r->free_entries++;
    else
        r->paid_entries++;

    bool closed = false;
    if (r->tickets >= r->threshold) { /* E2 */
        header_hash(r, r->header_hash);
        r->phase = RAFFLE_CLOSED;
        closed = true;
    }
    if (out) {
        out->fee = fee;
        out->first_ticket = first;
        out->count = count;
        out->closed_epoch = closed;
    }
    return RAFFLE_OK;
}

raffle_status_t raffle_enter(raffle_t *r, const uint8_t owner[RAFFLE_ID_LEN], uint64_t amount,
                             const raffle_rate_t *rate, raffle_receipt_t *out)
{
    uint64_t fee, n;
    if (!r || !owner) return RAFFLE_ERR_ARG;
    if (!r->cfg.jurisdiction_permits) return RAFFLE_ERR_DISABLED;
    if (r->cfg.mode != RAFFLE_MODE_PAID) return RAFFLE_ERR_MODE; /* M1 */
    if (r->phase != RAFFLE_OPEN) return RAFFLE_ERR_PHASE;
    raffle_status_t st = raffle_quote(&r->cfg, amount, rate, &fee, &n);
    if (st != RAFFLE_OK) return st;
    if (n == 0) return RAFFLE_ERR_DUST;
    return mint(r, owner, amount, rate ? *rate : r->cfg.min_rate, fee, n, 0, out);
}

raffle_status_t raffle_enter_share(raffle_t *r, const uint8_t owner[RAFFLE_ID_LEN], uint64_t amount,
                                   const raffle_rate_t *rate, uint64_t fee, raffle_receipt_t *out)
{
    if (!r || !owner) return RAFFLE_ERR_ARG;
    if (fee == 0) return RAFFLE_ERR_DUST;
    return mint(r, owner, amount, rate ? *rate : r->cfg.min_rate, fee, fee, 2, out);
}

raffle_rate_t raffle_luck_mult(const raffle_config_t *c, uint32_t misses)
{
    raffle_rate_t m = {1, 1};
    if (!c || c->luck_cap <= 1 || misses == 0) return m;
    /* 1 + n * F(20)/F(21) = (F(21) + n * F(20)) / F(21); n < 2^32, no wrap */
    uint64_t num = RAFFLE_LUCK_PHI_DEN + (uint64_t) misses * RAFFLE_LUCK_PHI_NUM;
    uint64_t cap = (uint64_t) c->luck_cap * RAFFLE_LUCK_PHI_DEN;
    if (num >= cap) {
        m.num = c->luck_cap; /* W1: capped */
        return m;
    }
    m.num = num;
    m.den = RAFFLE_LUCK_PHI_DEN;
    return m;
}

uint64_t raffle_luck_tickets(const raffle_config_t *c, uint64_t fee, raffle_rate_t m)
{
    uint64_t t;
    if (!c || m.den == 0 || m.num < m.den) return 0;
    raffle_rate_t cap = {c->luck_cap, 1};
    if (raffle_rate_cmp(m, cap) > 0) return 0;
    if (!pay_muldiv(fee, m.num, m.den, &t, 0)) return 0;
    return t;
}

raffle_status_t raffle_enter_weighted(raffle_t *r, const uint8_t owner[RAFFLE_ID_LEN],
                                      uint64_t amount, const raffle_rate_t *rate, uint64_t fee,
                                      uint64_t tickets, raffle_receipt_t *out)
{
    uint64_t most;
    if (!r || !owner) return RAFFLE_ERR_ARG;
    if (fee == 0) return RAFFLE_ERR_DUST;
    if (tickets < fee) return RAFFLE_ERR_ARG;
    if (pay_muldiv(fee, r->cfg.luck_cap, 1, &most, 0) && tickets > most) return RAFFLE_ERR_ARG;
    return mint(r, owner, amount, rate ? *rate : r->cfg.min_rate, fee, tickets, 3, out);
}

raffle_status_t raffle_enter_yield(raffle_t *r, const uint8_t owner[RAFFLE_ID_LEN], uint64_t profit,
                                   raffle_rate_t share, const raffle_rate_t *luck,
                                   raffle_receipt_t *out)
{
    uint64_t fee, tickets;
    if (!r || !owner) return RAFFLE_ERR_ARG;
    if (!r->cfg.jurisdiction_permits) return RAFFLE_ERR_DISABLED;
    if (share.den == 0 || share.num == 0 || share.num > share.den) return RAFFLE_ERR_RATE;
    (void) pay_muldiv(profit, share.num, share.den, &fee, 0); /* <= profit */
    if (fee == 0) return RAFFLE_ERR_DUST;
    tickets = luck ? raffle_luck_tickets(&r->cfg, fee, *luck) : fee;
    if (tickets == 0) return RAFFLE_ERR_RATE; /* a weight outside [1, luck_cap] */
    return mint(r, owner, profit, share, fee, tickets, 3, out);
}

raffle_status_t raffle_free_entry(raffle_t *r, const uint8_t owner[RAFFLE_ID_LEN],
                                  raffle_receipt_t *out)
{
    if (!r || !owner) return RAFFLE_ERR_ARG;
    if (!r->cfg.jurisdiction_permits || !r->cfg.free_entry) return RAFFLE_ERR_DISABLED;
    if (r->phase != RAFFLE_OPEN) return RAFFLE_ERR_PHASE;
    static const raffle_rate_t none = {0, 1};
    return mint(r, owner, 0, none, 0, 1, 1, out);
}

raffle_status_t raffle_reveal(raffle_t *r, const uint8_t party[RAFFLE_ID_LEN],
                              const uint8_t secret[RAFFLE_HASH_LEN])
{
    uint8_t c[RAFFLE_HASH_LEN];
    if (!r || !party || !secret) return RAFFLE_ERR_ARG;
    if (r->phase != RAFFLE_CLOSED) return RAFFLE_ERR_PHASE;
    int i = find_party(r, party);
    if (i < 0 || r->revealed[i]) return RAFFLE_ERR_PARTY;
    raffle_commitment(r->epoch, party, secret, c);
    if (!pay_memeq(c, r->commit[i], RAFFLE_HASH_LEN)) return RAFFLE_ERR_COMMIT;
    pay_memcpy(r->secret[i], secret, RAFFLE_HASH_LEN);
    r->revealed[i] = true;
    return RAFFLE_OK;
}

/* ===== The draw ===== */

bool raffle_owner_of(const raffle_t *r, uint64_t id, uint8_t owner[RAFFLE_ID_LEN])
{
    if (!r || id >= r->tickets) return false;
    uint32_t lo = 0, hi = r->range_len; /* ranges are sorted and contiguous */
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        const raffle_range_t *g = &r->ranges[mid];
        if (id < g->first)
            hi = mid;
        else if (id - g->first >= g->count)
            lo = mid + 1;
        else {
            if (owner) pay_memcpy(owner, g->owner, RAFFLE_ID_LEN);
            return true;
        }
    }
    return false;
}

bool raffle_pick(const uint8_t seed[RAFFLE_HASH_LEN], uint64_t t, uint64_t *out)
{
    uint64_t rem;
    if (!seed || !out || t == 0) return false;
    (void) pay_udiv64(UINT64_MAX, t, &rem);
    uint64_t r = rem + 1 == t ? 0 : rem + 1; /* 2^64 mod t */
    uint64_t limit = UINT64_MAX - r;         /* accept x <= limit: 2^64 - r values */
    for (uint64_t ctr = 0; ctr < RAFFLE_DRAW_TRIES; ctr++) {
        pre_t p;
        p.len = 0;
        uint8_t b[8];
        pre_str(&p, "ZXV-RAFFLE-DRAW");
        pre_bytes(&p, seed, RAFFLE_HASH_LEN);
        pre_u64(&p, ctr);
        shake256(p.buf, p.len, b, sizeof b);
        uint64_t x = get64(b);
        if (x <= limit) {
            (void) pay_udiv64(x, t, out);
            return true;
        }
    }
    return false;
}

/* S6: the rebate of each run not owned by `winner`, pro rata to its fee
 * over everyone else's fees. Returns their sum; with `credit`, also posts
 * each one (stopping, with *ok = false, at the first refusal). */
static uint64_t rebates(const raffle_t *r, const uint8_t winner[RAFFLE_ID_LEN], uint64_t pool,
                        raffle_credit_fn credit, void *ctx, bool *ok)
{
    uint64_t wfee = 0, sum = 0;
    if (pool == 0) return 0;
    for (uint32_t i = 0; i < r->range_len; i++)
        if (pay_memeq(r->ranges[i].owner, winner, RAFFLE_ID_LEN)) wfee += r->ranges[i].fee;
    uint64_t base = r->pool - wfee; /* fees of runs sum to the pool */
    if (base == 0) return 0;        /* nobody else paid: it stays in the crown pot */
    for (uint32_t i = 0; i < r->range_len; i++) {
        const raffle_range_t *g = &r->ranges[i];
        uint64_t x = 0;
        if (!g->fee || pay_memeq(g->owner, winner, RAFFLE_ID_LEN)) continue;
        (void) pay_muldiv(pool, g->fee, base, &x, 0); /* <= pool */
        sum += x;
        if (credit && x && !credit(ctx, RAFFLE_DEST_REBATE, g->owner, x)) {
            *ok = false;
            return sum;
        }
    }
    return sum;
}

static void settlement_link(const raffle_settlement_t *s, uint8_t out[RAFFLE_HASH_LEN])
{
    pre_t p;
    p.len = 0;
    pre_str(&p, "ZXV-RAFFLE-SETTLE3");
    pre_u64(&p, s->epoch);
    pre_bytes(&p, s->header_hash, RAFFLE_HASH_LEN);
    pre_bytes(&p, s->seed, RAFFLE_HASH_LEN);
    pre_u64(&p, s->winning_ticket);
    pre_bytes(&p, s->winner, RAFFLE_ID_LEN);
    pre_u64(&p, s->pool);
    pre_u64(&p, s->reserve_skim);
    pre_u64(&p, s->grant_pool);
    pre_u64(&p, s->nav_slice);
    pre_u64(&p, s->rebate_pool);
    pre_u64(&p, s->rebate_paid);
    pre_u64(&p, s->crown_pot);
    sha3_256(p.buf, p.len, out);
}

raffle_status_t raffle_draw(const raffle_t *r, raffle_settlement_t *out)
{
    if (!r || !out) return RAFFLE_ERR_ARG;
    if (r->phase != RAFFLE_CLOSED) return RAFFLE_ERR_PHASE;
    if (r->tickets == 0) return RAFFLE_ERR_NO_TICKETS;
    uint32_t got = 0;
    for (uint32_t i = 0; i < r->n_parties; i++) got += r->revealed[i] ? 1u : 0u;
    uint32_t need = r->cfg.min_reveals ? r->cfg.min_reveals : r->n_parties;
    if (need == 0 || got < need) return RAFFLE_ERR_REVEALS; /* R2: no parties, no draw */

    raffle_settlement_t s;
    pay_memset(&s, 0, sizeof s);
    s.epoch = r->epoch;
    s.tickets = r->tickets;
    s.pool = r->pool;
    pay_memcpy(s.header_hash, r->header_hash, RAFFLE_HASH_LEN);

    pre_t p;
    p.len = 0;
    pre_str(&p, "ZXV-RAFFLE-SEED");
    pre_bytes(&p, r->header_hash, RAFFLE_HASH_LEN);
    for (uint32_t i = 0; i < r->n_parties; i++) {
        static const uint8_t zero[RAFFLE_HASH_LEN] = {0};
        uint8_t flag = r->revealed[i] ? 1u : 0u;
        pre_bytes(&p, &flag, 1);
        pre_bytes(&p, r->revealed[i] ? r->secret[i] : zero, RAFFLE_HASH_LEN);
    }
    sha3_256(p.buf, p.len, s.seed);

    if (!raffle_pick(s.seed, r->tickets, &s.winning_ticket)) return RAFFLE_ERR_DRAW;
    if (!raffle_owner_of(r, s.winning_ticket, s.winner)) return RAFFLE_ERR_DRAW;

    s.reserve_skim = ratio_floor(r->pool, r->cfg.skim_num, r->cfg.skim_den); /* S1 */
    s.grant_pool =
        r->cfg.grant_den ? ratio_floor(r->pool, r->cfg.grant_num, r->cfg.grant_den) : 0; /* S2 */
    s.nav_slice =
        r->cfg.nav_den ? ratio_floor(r->pool, r->cfg.nav_num, r->cfg.nav_den) : 0; /* S5 */
    s.rebate_pool =
        r->cfg.rebate_den ? ratio_floor(r->pool, r->cfg.rebate_num, r->cfg.rebate_den) : 0;
    s.rebate_paid = rebates(r, s.winner, s.rebate_pool, 0, 0, 0); /* S6 */
    /* S3: config_check keeps the sum of the ratios below 1 */
    s.crown_pot = r->pool - s.reserve_skim - s.grant_pool - s.nav_slice - s.rebate_paid;
    settlement_link(&s, s.link);
    *out = s;
    return RAFFLE_OK;
}

bool raffle_post(const raffle_t *r, const raffle_settlement_t *s, raffle_credit_fn credit,
                 void *ctx)
{
    bool ok = true;
    if (!r || !s || !credit) return false;
    if (r->phase != RAFFLE_CLOSED || s->epoch != r->epoch || s->pool != r->pool ||
        !pay_memeq(s->header_hash, r->header_hash, RAFFLE_HASH_LEN) ||
        rebates(r, s->winner, s->rebate_pool, 0, 0, 0) != s->rebate_paid)
        return false;
    if (s->reserve_skim && !credit(ctx, RAFFLE_DEST_RESERVE_FLOOR, 0, s->reserve_skim))
        return false;
    if (s->grant_pool && !credit(ctx, RAFFLE_DEST_GRANT_POOL, 0, s->grant_pool)) return false;
    if (s->nav_slice && !credit(ctx, RAFFLE_DEST_BASKET_NAV, 0, s->nav_slice)) return false;
    (void) rebates(r, s->winner, s->rebate_pool, credit, ctx, &ok);
    if (!ok) return false;
    if (s->crown_pot && !credit(ctx, RAFFLE_DEST_WINNER, s->winner, s->crown_pot)) return false;
    return true;
}

raffle_status_t raffle_settle(raffle_t *r, const raffle_settlement_t *s)
{
    raffle_settlement_t want;
    if (!r || !s) return RAFFLE_ERR_ARG;
    raffle_status_t st = raffle_draw(r, &want);
    if (st != RAFFLE_OK) return st;
    if (!pay_memeq(&want, s, sizeof want)) return RAFFLE_ERR_SETTLEMENT;
    r->last = want;
    r->phase = RAFFLE_SETTLED;
    return RAFFLE_OK;
}

raffle_status_t raffle_next_epoch(raffle_t *r)
{
    if (!r) return RAFFLE_ERR_ARG;
    if (r->phase != RAFFLE_SETTLED) return RAFFLE_ERR_PHASE;
    if (r->epoch == UINT64_MAX) return RAFFLE_ERR_OVERFLOW;
    raffle_config_t c = r->cfg;
    raffle_range_t *g = r->ranges;
    uint32_t cap = r->range_cap;
    raffle_tally_t *t = r->tally;
    uint32_t tcap = r->tally_cap;
    uint8_t link[RAFFLE_HASH_LEN];
    pay_memcpy(link, r->last.link, RAFFLE_HASH_LEN);
    raffle_status_t st = raffle_init(r, &c, r->epoch + 1, link, g, cap);
    if (st == RAFFLE_OK && t) st = raffle_set_tally(r, t, tcap);
    return st;
}
