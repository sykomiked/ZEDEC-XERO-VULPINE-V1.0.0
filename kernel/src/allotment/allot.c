/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* allot.c — the Sicilian Crown Allotment. See allot.h. */
#include "allot.h"
#include "pay_util.h"
#include "keccak.h"
#include "vrf.h"

_Static_assert(ALLOT_VRF_PK == VRF_PK_LEN && ALLOT_VRF_PROOF == VRF_PROOF_LEN, "R5 sizes");

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

void allot_config_default(allot_config_t *c)
{
    if (!c) return;
    pay_memset(c, 0, sizeof *c);
    c->jurisdiction_permits = false;
    c->free_entry = true; /* L2 */
    c->min_rate.num = 1;
    c->min_rate.den = 100;
    c->max_rate.num = ALLOT_RATE_CEIL_NUM;
    c->max_rate.den = ALLOT_RATE_CEIL_DEN;
    c->skim_num = ALLOT_SKIM_NUM;
    c->skim_den = ALLOT_SKIM_DEN;
    c->grant_num = 5;
    c->grant_den = 55;
    c->fib_index = ALLOT_DEFAULT_FIB_INDEX;
    c->min_reveals = 0;
    c->share_num = 8;
    c->share_den = 21;
    c->mode = ALLOT_MODE_FREE; /* M0 */
    c->rebate_num = 1;         /* S6 */
    c->rebate_den = 5;
    c->luck_cap = ALLOT_LUCK_DEFAULT; /* W1 */
}

int allot_rate_cmp(allot_rate_t a, allot_rate_t b)
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
static bool split_fits(const allot_config_t *c)
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

allot_status_t allot_config_check(const allot_config_t *c)
{
    static const allot_rate_t ceil = {ALLOT_RATE_CEIL_NUM, ALLOT_RATE_CEIL_DEN};
    if (!c) return ALLOT_ERR_ARG;
    if (c->min_rate.den == 0 || c->max_rate.den == 0 || c->min_rate.num == 0)
        return ALLOT_ERR_CONFIG;
    if (allot_rate_cmp(c->min_rate, c->max_rate) > 0) return ALLOT_ERR_CONFIG;
    if (allot_rate_cmp(c->max_rate, ceil) > 0) return ALLOT_ERR_CONFIG; /* F2 */
    if (c->skim_den == 0 || c->skim_num >= c->skim_den) return ALLOT_ERR_CONFIG;
    if (c->grant_den && c->grant_num >= c->grant_den) return ALLOT_ERR_CONFIG;
    if (c->nav_den && c->nav_num >= c->nav_den) return ALLOT_ERR_CONFIG;
    if (c->rebate_den && c->rebate_num >= c->rebate_den) return ALLOT_ERR_CONFIG;
    if (c->mode > ALLOT_MODE_FREE) return ALLOT_ERR_CONFIG;
    if (!c->free_entry) return ALLOT_ERR_CONFIG; /* L2: free entry is always on */
    if (c->luck_cap < 1 || c->luck_cap > ALLOT_LUCK_CEIL) return ALLOT_ERR_CONFIG;
    if (!split_fits(c)) return ALLOT_ERR_CONFIG; /* S3 */
    if (c->fib_index < ALLOT_FIB_MIN || c->fib_index > ALLOT_FIB_MAX) return ALLOT_ERR_CONFIG;
    if (c->min_reveals > ALLOT_MAX_PARTIES) return ALLOT_ERR_CONFIG;
    if (c->share_den && (c->share_num == 0 || c->share_num > c->share_den)) return ALLOT_ERR_CONFIG;
    return ALLOT_OK;
}

uint64_t allot_fib(uint32_t n)
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

allot_status_t allot_quote(const allot_config_t *c, uint64_t amount, const allot_rate_t *rate,
                           uint64_t *fee, uint64_t *ballots)
{
    allot_status_t st = allot_config_check(c);
    if (st != ALLOT_OK) return st;
    allot_rate_t r = rate ? *rate : c->min_rate;
    if (r.den == 0 || allot_rate_cmp(r, c->min_rate) < 0 || allot_rate_cmp(r, c->max_rate) > 0)
        return ALLOT_ERR_RATE;
    uint64_t f = ratio_floor(amount, r.num, r.den); /* F2: r <= 1/2, fits */
    uint64_t n = f;                                 /* F3: one ballot per fee unit */
    if (fee) *fee = f;
    if (ballots) *ballots = n;
    return ALLOT_OK;
}

/* ===== Epoch lifecycle ===== */

static void genesis_chain(uint64_t epoch, const uint8_t prev_link[ALLOT_HASH_LEN],
                          uint8_t out[ALLOT_HASH_LEN])
{
    pre_t p;
    p.len = 0;
    pre_str(&p, "ZXV-ALLOT-GENESIS");
    pre_u64(&p, epoch);
    pre_bytes(&p, prev_link, ALLOT_HASH_LEN);
    sha3_256(p.buf, p.len, out);
}

allot_status_t allot_init(allot_t *r, const allot_config_t *c, uint64_t epoch,
                          const uint8_t prev_link[ALLOT_HASH_LEN], allot_range_t *ranges,
                          uint32_t range_cap)
{
    static const uint8_t zero[ALLOT_HASH_LEN] = {0};
    if (!r || !c || !ranges || range_cap == 0) return ALLOT_ERR_ARG;
    allot_status_t st = allot_config_check(c);
    if (st != ALLOT_OK) return st;
    pay_memset(r, 0, sizeof *r);
    r->cfg = *c;
    r->epoch = epoch;
    r->threshold = c->threshold ? c->threshold : allot_fib(c->fib_index); /* E1 */
    r->phase = ALLOT_OPEN;
    r->ranges = ranges;
    r->range_cap = range_cap;
    genesis_chain(epoch, prev_link ? prev_link : zero, r->chain);
    return ALLOT_OK;
}

void allot_commitment(uint64_t epoch, const uint8_t party[ALLOT_ID_LEN],
                      const uint8_t secret[ALLOT_HASH_LEN], uint8_t out[ALLOT_HASH_LEN])
{
    pre_t p;
    p.len = 0;
    pre_str(&p, "ZXV-ALLOT-COMMIT");
    pre_u64(&p, epoch);
    pre_bytes(&p, party, ALLOT_ID_LEN);
    pre_bytes(&p, secret, ALLOT_HASH_LEN);
    sha3_256(p.buf, p.len, out);
}

static int find_party(const allot_t *r, const uint8_t party[ALLOT_ID_LEN])
{
    for (uint32_t i = 0; i < r->n_parties; i++)
        if (pay_memeq(r->party[i], party, ALLOT_ID_LEN)) return (int) i;
    return -1;
}

allot_status_t allot_commit(allot_t *r, const uint8_t party[ALLOT_ID_LEN],
                            const uint8_t commitment[ALLOT_HASH_LEN])
{
    if (!r || !party || !commitment) return ALLOT_ERR_ARG;
    if (r->phase != ALLOT_OPEN || r->ballots != 0) return ALLOT_ERR_PHASE;
    if (find_party(r, party) >= 0 || r->n_parties >= ALLOT_MAX_PARTIES) return ALLOT_ERR_PARTY;
    pay_memcpy(r->party[r->n_parties], party, ALLOT_ID_LEN);
    pay_memcpy(r->commit[r->n_parties], commitment, ALLOT_HASH_LEN);
    r->n_parties++;
    return ALLOT_OK;
}

/* E3: the epoch header and its hash, fixed at close. */
static void header_hash(const allot_t *r, uint8_t out[ALLOT_HASH_LEN])
{
    pre_t p;
    p.len = 0;
    pre_str(&p, "ZXV-ALLOT-HDR7");
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
    pre_u64(&p, r->ballots);
    pre_u64(&p, r->pool);
    pre_bytes(&p, r->chain, ALLOT_HASH_LEN);
    pre_u64(&p, r->n_parties);
    for (uint32_t i = 0; i < r->n_parties; i++) {
        pre_bytes(&p, r->party[i], ALLOT_ID_LEN);
        pre_bytes(&p, r->commit[i], ALLOT_HASH_LEN);
    }
    sha3_256(p.buf, p.len, out);
}

allot_status_t allot_set_tally(allot_t *r, allot_tally_t *tally, uint32_t cap)
{
    if (!r || !tally || cap < 4 || (cap & (cap - 1))) return ALLOT_ERR_ARG;
    if (r->phase != ALLOT_OPEN || r->ballots != 0) return ALLOT_ERR_PHASE;
    pay_memset(tally, 0, (size_t) cap * sizeof *tally);
    r->tally = tally;
    r->tally_cap = cap;
    r->tally_len = 0;
    return ALLOT_OK;
}

/* The tally slot of `owner`: its own slot, else the empty slot where it
 * would go. Never loops forever: the table is at most 3/4 full. */
static uint32_t tally_slot(const allot_t *r, const uint8_t owner[ALLOT_ID_LEN])
{
    uint32_t mask = r->tally_cap - 1, i = (uint32_t) get64(owner) & mask;
    while (r->tally[i].held && !pay_memeq(r->tally[i].owner, owner, ALLOT_ID_LEN))
        i = (i + 1) & mask;
    return i;
}

static uint64_t held_by(const allot_t *r, const uint8_t owner[ALLOT_ID_LEN])
{
    if (r->tally) return r->tally[tally_slot(r, owner)].held;
    uint64_t held = 0;
    for (uint32_t i = 0; i < r->range_len; i++)
        if (pay_memeq(r->ranges[i].owner, owner, ALLOT_ID_LEN)) held += r->ranges[i].count;
    return held;
}

uint64_t allot_room(const allot_t *r, const uint8_t owner[ALLOT_ID_LEN])
{
    if (!r || !owner) return 0;
    uint64_t cap;
    if (r->cfg.mode == ALLOT_MODE_FREE)
        cap = 1; /* A1: one ballot per owner per epoch */
    else if (!r->cfg.share_den)
        return UINT64_MAX;
    else
        cap = ratio_floor(r->threshold, r->cfg.share_num, r->cfg.share_den);
    if (cap == 0) cap = 1;
    uint64_t held = held_by(r, owner);
    return held >= cap ? 0 : cap - held;
}

allot_status_t allot_mint_check(const allot_t *r, const uint8_t owner[ALLOT_ID_LEN], uint64_t fee,
                                uint64_t count)
{
    uint64_t t;
    if (!r || !owner) return ALLOT_ERR_ARG;
    if (!r->cfg.jurisdiction_permits) return ALLOT_ERR_DISABLED;
    if (r->phase != ALLOT_OPEN) return ALLOT_ERR_PHASE;
    if (r->cfg.mode == ALLOT_MODE_FREE && fee) return ALLOT_ERR_MODE; /* A3 */
    if (count > allot_room(r, owner)) return ALLOT_ERR_CAP;           /* F6, A1 */
    if (!pay_add_ok(r->pool, fee, &t)) return ALLOT_ERR_OVERFLOW;
    if (count && !pay_add_ok(r->ballots, count, &t)) return ALLOT_ERR_OVERFLOW;
    if (count && r->range_len >= r->range_cap) return ALLOT_ERR_FULL;
    if (count && r->tally && r->tally[tally_slot(r, owner)].held == 0 &&
        r->tally_len + 1 > r->tally_cap - r->tally_cap / 4)
        return ALLOT_ERR_FULL;
    return ALLOT_OK;
}

/* Mint `count` ballots to `owner` for `fee` (kind 0 = paid, 1 = free, 2 = a
 * ladder share, 3 = weighted or savings) at `rate` (0/1 for a free entry). */
static allot_status_t mint(allot_t *r, const uint8_t owner[ALLOT_ID_LEN], uint64_t amount,
                           allot_rate_t rate, uint64_t fee, uint64_t count, uint8_t kind,
                           allot_receipt_t *out)
{
    if (r->cfg.mode == ALLOT_MODE_FREE && kind != 1) return ALLOT_ERR_MODE; /* A3 */
    allot_status_t st = allot_mint_check(r, owner, fee, count);
    if (st != ALLOT_OK) return st;
    uint64_t pool2 = r->pool + fee, ballots2 = r->ballots + count;

    uint64_t first = r->ballots;
    if (count) {
        allot_range_t *g = &r->ranges[r->range_len++];
        pay_memcpy(g->owner, owner, ALLOT_ID_LEN);
        g->first = first;
        g->count = count;
        g->fee = fee;
        g->gratuitous = kind == 1; /* L2 */
        if (r->tally) {
            allot_tally_t *t = &r->tally[tally_slot(r, owner)];
            if (t->held == 0) {
                pay_memcpy(t->owner, owner, ALLOT_ID_LEN);
                r->tally_len++;
            }
            t->held += count; /* <= ballots, which did not overflow */
        }
    }
    /* E3: fold the entry into the chain. */
    pre_t p;
    p.len = 0;
    pre_str(&p, "ZXV-ALLOT-ENTRY");
    pre_bytes(&p, r->chain, ALLOT_HASH_LEN);
    pre_bytes(&p, owner, ALLOT_ID_LEN);
    pre_u64(&p, amount);
    pre_u64(&p, rate.num);
    pre_u64(&p, rate.den);
    pre_u64(&p, fee);
    pre_u64(&p, first);
    pre_u64(&p, count);
    pre_bytes(&p, &kind, 1);
    sha3_256(p.buf, p.len, r->chain);

    r->pool = pool2;
    r->ballots = ballots2;
    if (kind == 1)
        r->free_entries++;
    else
        r->paid_entries++;

    bool closed = false;
    if (r->ballots >= r->threshold) { /* E2 */
        header_hash(r, r->header_hash);
        r->phase = ALLOT_CLOSED;
        closed = true;
    }
    if (out) {
        out->fee = fee;
        out->first_ballot = first;
        out->count = count;
        out->closed_epoch = closed;
    }
    return ALLOT_OK;
}

allot_status_t allot_enter(allot_t *r, const uint8_t owner[ALLOT_ID_LEN], uint64_t amount,
                           const allot_rate_t *rate, allot_receipt_t *out)
{
    uint64_t fee, n;
    if (!r || !owner) return ALLOT_ERR_ARG;
    if (!r->cfg.jurisdiction_permits) return ALLOT_ERR_DISABLED;
    if (r->cfg.mode != ALLOT_MODE_PAID) return ALLOT_ERR_MODE; /* M1 */
    if (r->phase != ALLOT_OPEN) return ALLOT_ERR_PHASE;
    allot_status_t st = allot_quote(&r->cfg, amount, rate, &fee, &n);
    if (st != ALLOT_OK) return st;
    if (n == 0) return ALLOT_ERR_DUST;
    return mint(r, owner, amount, rate ? *rate : r->cfg.min_rate, fee, n, 0, out);
}

allot_status_t allot_enter_share(allot_t *r, const uint8_t owner[ALLOT_ID_LEN], uint64_t amount,
                                 const allot_rate_t *rate, uint64_t fee, allot_receipt_t *out)
{
    if (!r || !owner) return ALLOT_ERR_ARG;
    if (fee == 0) return ALLOT_ERR_DUST;
    return mint(r, owner, amount, rate ? *rate : r->cfg.min_rate, fee, fee, 2, out);
}

allot_rate_t allot_luck_mult(const allot_config_t *c, uint32_t misses)
{
    allot_rate_t m = {1, 1};
    if (!c || c->luck_cap <= 1 || misses == 0) return m;
    /* 1 + n * F(20)/F(21) = (F(21) + n * F(20)) / F(21); n < 2^32, no wrap */
    uint64_t num = ALLOT_LUCK_PHI_DEN + (uint64_t) misses * ALLOT_LUCK_PHI_NUM;
    uint64_t cap = (uint64_t) c->luck_cap * ALLOT_LUCK_PHI_DEN;
    if (num >= cap) {
        m.num = c->luck_cap; /* W1: capped */
        return m;
    }
    m.num = num;
    m.den = ALLOT_LUCK_PHI_DEN;
    return m;
}

uint64_t allot_luck_ballots(const allot_config_t *c, uint64_t fee, allot_rate_t m)
{
    uint64_t t;
    if (!c || m.den == 0 || m.num < m.den) return 0;
    allot_rate_t cap = {c->luck_cap, 1};
    if (allot_rate_cmp(m, cap) > 0) return 0;
    if (!pay_muldiv(fee, m.num, m.den, &t, 0)) return 0;
    return t;
}

allot_status_t allot_enter_weighted(allot_t *r, const uint8_t owner[ALLOT_ID_LEN], uint64_t amount,
                                    const allot_rate_t *rate, uint64_t fee, uint64_t ballots,
                                    allot_receipt_t *out)
{
    uint64_t most;
    if (!r || !owner) return ALLOT_ERR_ARG;
    if (fee == 0) return ALLOT_ERR_DUST;
    if (ballots < fee) return ALLOT_ERR_ARG;
    if (pay_muldiv(fee, r->cfg.luck_cap, 1, &most, 0) && ballots > most) return ALLOT_ERR_ARG;
    return mint(r, owner, amount, rate ? *rate : r->cfg.min_rate, fee, ballots, 3, out);
}

allot_status_t allot_enter_yield(allot_t *r, const uint8_t owner[ALLOT_ID_LEN], uint64_t profit,
                                 allot_rate_t share, const allot_rate_t *luck, allot_receipt_t *out)
{
    uint64_t fee, ballots;
    if (!r || !owner) return ALLOT_ERR_ARG;
    if (!r->cfg.jurisdiction_permits) return ALLOT_ERR_DISABLED;
    if (share.den == 0 || share.num == 0 || share.num > share.den) return ALLOT_ERR_RATE;
    (void) pay_muldiv(profit, share.num, share.den, &fee, 0); /* <= profit */
    if (fee == 0) return ALLOT_ERR_DUST;
    ballots = luck ? allot_luck_ballots(&r->cfg, fee, *luck) : fee;
    if (ballots == 0) return ALLOT_ERR_RATE; /* a weight outside [1, luck_cap] */
    return mint(r, owner, profit, share, fee, ballots, 3, out);
}

allot_status_t allot_free_entry(allot_t *r, const uint8_t owner[ALLOT_ID_LEN], allot_receipt_t *out)
{
    if (!r || !owner) return ALLOT_ERR_ARG;
    if (!r->cfg.jurisdiction_permits || !r->cfg.free_entry) return ALLOT_ERR_DISABLED;
    if (r->phase != ALLOT_OPEN) return ALLOT_ERR_PHASE;
    static const allot_rate_t none = {0, 1};
    return mint(r, owner, 0, none, 0, 1, 1, out);
}

allot_status_t allot_endow(allot_t *r, const uint8_t source[ALLOT_ID_LEN], uint64_t amount)
{
    uint64_t pool2;
    if (!r || !source) return ALLOT_ERR_ARG;
    if (!r->cfg.jurisdiction_permits) return ALLOT_ERR_DISABLED;
    if (r->cfg.mode != ALLOT_MODE_FREE) return ALLOT_ERR_MODE;
    if (r->phase != ALLOT_OPEN) return ALLOT_ERR_PHASE;
    if (amount == 0) return ALLOT_ERR_DUST;
    if (!pay_add_ok(r->pool, amount, &pool2)) return ALLOT_ERR_OVERFLOW;
    /* E3: kind 4 = endowment; no ballot, so first = count = 0 */
    pre_t p;
    p.len = 0;
    pre_str(&p, "ZXV-ALLOT-ENDOW");
    pre_bytes(&p, r->chain, ALLOT_HASH_LEN);
    pre_bytes(&p, source, ALLOT_ID_LEN);
    pre_u64(&p, amount);
    sha3_256(p.buf, p.len, r->chain);
    r->pool = pool2;
    return ALLOT_OK;
}

allot_status_t allot_reveal(allot_t *r, const uint8_t party[ALLOT_ID_LEN],
                            const uint8_t secret[ALLOT_HASH_LEN])
{
    uint8_t c[ALLOT_HASH_LEN];
    if (!r || !party || !secret) return ALLOT_ERR_ARG;
    if (r->phase != ALLOT_CLOSED) return ALLOT_ERR_PHASE;
    int i = find_party(r, party);
    if (i < 0 || r->revealed[i] || r->is_vrf[i]) return ALLOT_ERR_PARTY;
    allot_commitment(r->epoch, party, secret, c);
    if (!pay_memeq(c, r->commit[i], ALLOT_HASH_LEN)) return ALLOT_ERR_COMMIT;
    pay_memcpy(r->secret[i], secret, ALLOT_HASH_LEN);
    r->revealed[i] = true;
    return ALLOT_OK;
}

/* R5: the commitment slot of a VRF party binds its key. */
static void vrf_pk_commitment(uint64_t epoch, const uint8_t party[ALLOT_ID_LEN],
                              const uint8_t pk[ALLOT_VRF_PK], uint8_t out[ALLOT_HASH_LEN])
{
    pre_t p;
    p.len = 0;
    pre_str(&p, "ZXV-ALLOT-VRF-PK");
    pre_u64(&p, epoch);
    pre_bytes(&p, party, ALLOT_ID_LEN);
    pre_bytes(&p, pk, ALLOT_VRF_PK);
    sha3_256(p.buf, p.len, out);
}

allot_status_t allot_commit_vrf(allot_t *r, const uint8_t party[ALLOT_ID_LEN],
                                const uint8_t pk[ALLOT_VRF_PK])
{
    uint8_t c[ALLOT_HASH_LEN];
    if (!r || !party || !pk) return ALLOT_ERR_ARG;
    vrf_pk_commitment(r->epoch, party, pk, c);
    allot_status_t st = allot_commit(r, party, c);
    if (st != ALLOT_OK) return st;
    r->is_vrf[r->n_parties - 1] = true;
    pay_memcpy(r->vrf_pk[r->n_parties - 1], pk, ALLOT_VRF_PK);
    return ALLOT_OK;
}

allot_status_t allot_reveal_vrf(allot_t *r, const uint8_t party[ALLOT_ID_LEN],
                                const uint8_t pi[ALLOT_VRF_PROOF])
{
    uint8_t beta[VRF_OUT_LEN];
    if (!r || !party || !pi) return ALLOT_ERR_ARG;
    if (r->phase != ALLOT_CLOSED) return ALLOT_ERR_PHASE;
    int i = find_party(r, party);
    if (i < 0 || r->revealed[i] || !r->is_vrf[i]) return ALLOT_ERR_PARTY;
    if (!vrf_verify(r->vrf_pk[i], r->header_hash, ALLOT_HASH_LEN, pi, beta))
        return ALLOT_ERR_COMMIT;
    pre_t p;
    p.len = 0;
    pre_str(&p, "ZXV-ALLOT-VRF-BETA");
    pre_bytes(&p, beta, VRF_OUT_LEN);
    sha3_256(p.buf, p.len, r->secret[i]);
    r->revealed[i] = true;
    return ALLOT_OK;
}

/* ===== The selection ===== */

bool allot_owner_of(const allot_t *r, uint64_t id, uint8_t owner[ALLOT_ID_LEN])
{
    if (!r || id >= r->ballots) return false;
    uint32_t lo = 0, hi = r->range_len; /* ranges are sorted and contiguous */
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        const allot_range_t *g = &r->ranges[mid];
        if (id < g->first)
            hi = mid;
        else if (id - g->first >= g->count)
            lo = mid + 1;
        else {
            if (owner) pay_memcpy(owner, g->owner, ALLOT_ID_LEN);
            return true;
        }
    }
    return false;
}

bool allot_pick(const uint8_t seed[ALLOT_HASH_LEN], uint64_t t, uint64_t *out)
{
    uint64_t rem;
    if (!seed || !out || t == 0) return false;
    (void) pay_udiv64(UINT64_MAX, t, &rem);
    uint64_t r = rem + 1 == t ? 0 : rem + 1; /* 2^64 mod t */
    uint64_t limit = UINT64_MAX - r;         /* accept x <= limit: 2^64 - r values */
    for (uint64_t ctr = 0; ctr < ALLOT_SELECT_TRIES; ctr++) {
        pre_t p;
        p.len = 0;
        uint8_t b[8];
        pre_str(&p, "ZXV-ALLOT-SELECT");
        pre_bytes(&p, seed, ALLOT_HASH_LEN);
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

/* S6: the rebate of each run not owned by `steward`, pro rata to its fee
 * over everyone else's fees. Returns their sum; with `credit`, also posts
 * each one (stopping, with *ok = false, at the first refusal). */
static uint64_t rebates(const allot_t *r, const uint8_t steward[ALLOT_ID_LEN], uint64_t pool,
                        allot_credit_fn credit, void *ctx, bool *ok)
{
    uint64_t wfee = 0, sum = 0;
    if (pool == 0) return 0;
    for (uint32_t i = 0; i < r->range_len; i++)
        if (pay_memeq(r->ranges[i].owner, steward, ALLOT_ID_LEN)) wfee += r->ranges[i].fee;
    uint64_t base = r->pool - wfee; /* fees of runs sum to the pool */
    if (base == 0) return 0;        /* nobody else paid: it stays in the Crown endowment */
    for (uint32_t i = 0; i < r->range_len; i++) {
        const allot_range_t *g = &r->ranges[i];
        uint64_t x = 0;
        if (!g->fee || pay_memeq(g->owner, steward, ALLOT_ID_LEN)) continue;
        (void) pay_muldiv(pool, g->fee, base, &x, 0); /* <= pool */
        sum += x;
        if (credit && x && !credit(ctx, ALLOT_DEST_REBATE, g->owner, x)) {
            *ok = false;
            return sum;
        }
    }
    return sum;
}

static void settlement_link(const allot_settlement_t *s, uint8_t out[ALLOT_HASH_LEN])
{
    pre_t p;
    p.len = 0;
    pre_str(&p, "ZXV-ALLOT-SETTLE3");
    pre_u64(&p, s->epoch);
    pre_bytes(&p, s->header_hash, ALLOT_HASH_LEN);
    pre_bytes(&p, s->seed, ALLOT_HASH_LEN);
    pre_u64(&p, s->selected_ballot);
    pre_bytes(&p, s->steward, ALLOT_ID_LEN);
    pre_u64(&p, s->pool);
    pre_u64(&p, s->reserve_skim);
    pre_u64(&p, s->grant_pool);
    pre_u64(&p, s->nav_slice);
    pre_u64(&p, s->rebate_pool);
    pre_u64(&p, s->rebate_paid);
    pre_u64(&p, s->crown_endowment);
    sha3_256(p.buf, p.len, out);
}

allot_status_t allot_select(const allot_t *r, allot_settlement_t *out)
{
    if (!r || !out) return ALLOT_ERR_ARG;
    if (r->phase != ALLOT_CLOSED) return ALLOT_ERR_PHASE;
    if (r->ballots == 0) return ALLOT_ERR_NO_BALLOTS;
    uint32_t got = 0;
    for (uint32_t i = 0; i < r->n_parties; i++) got += r->revealed[i] ? 1u : 0u;
    uint32_t need = r->cfg.min_reveals ? r->cfg.min_reveals : r->n_parties;
    if (need == 0 || got < need) return ALLOT_ERR_REVEALS; /* R2: no parties, no draw */

    allot_settlement_t s;
    pay_memset(&s, 0, sizeof s);
    s.epoch = r->epoch;
    s.ballots = r->ballots;
    s.pool = r->pool;
    pay_memcpy(s.header_hash, r->header_hash, ALLOT_HASH_LEN);

    pre_t p;
    p.len = 0;
    pre_str(&p, "ZXV-ALLOT-SEED");
    pre_bytes(&p, r->header_hash, ALLOT_HASH_LEN);
    for (uint32_t i = 0; i < r->n_parties; i++) {
        static const uint8_t zero[ALLOT_HASH_LEN] = {0};
        uint8_t flag = r->revealed[i] ? 1u : 0u;
        pre_bytes(&p, &flag, 1);
        pre_bytes(&p, r->revealed[i] ? r->secret[i] : zero, ALLOT_HASH_LEN);
    }
    sha3_256(p.buf, p.len, s.seed);

    if (!allot_pick(s.seed, r->ballots, &s.selected_ballot)) return ALLOT_ERR_SELECT;
    if (!allot_owner_of(r, s.selected_ballot, s.steward)) return ALLOT_ERR_SELECT;

    if (r->cfg.mode == ALLOT_MODE_FREE) { /* S7: the whole endowment */
        s.crown_endowment = r->pool;
        settlement_link(&s, s.link);
        *out = s;
        return ALLOT_OK;
    }
    s.reserve_skim = ratio_floor(r->pool, r->cfg.skim_num, r->cfg.skim_den); /* S1 */
    s.grant_pool =
        r->cfg.grant_den ? ratio_floor(r->pool, r->cfg.grant_num, r->cfg.grant_den) : 0; /* S2 */
    s.nav_slice =
        r->cfg.nav_den ? ratio_floor(r->pool, r->cfg.nav_num, r->cfg.nav_den) : 0; /* S5 */
    s.rebate_pool =
        r->cfg.rebate_den ? ratio_floor(r->pool, r->cfg.rebate_num, r->cfg.rebate_den) : 0;
    s.rebate_paid = rebates(r, s.steward, s.rebate_pool, 0, 0, 0); /* S6 */
    /* S3: config_check keeps the sum of the ratios below 1 */
    s.crown_endowment = r->pool - s.reserve_skim - s.grant_pool - s.nav_slice - s.rebate_paid;
    settlement_link(&s, s.link);
    *out = s;
    return ALLOT_OK;
}

bool allot_post(const allot_t *r, const allot_settlement_t *s, allot_credit_fn credit, void *ctx)
{
    bool ok = true;
    if (!r || !s || !credit) return false;
    if (r->phase != ALLOT_CLOSED || s->epoch != r->epoch || s->pool != r->pool ||
        !pay_memeq(s->header_hash, r->header_hash, ALLOT_HASH_LEN) ||
        rebates(r, s->steward, s->rebate_pool, 0, 0, 0) != s->rebate_paid)
        return false;
    if (s->reserve_skim && !credit(ctx, ALLOT_DEST_RESERVE_FLOOR, 0, s->reserve_skim)) return false;
    if (s->grant_pool && !credit(ctx, ALLOT_DEST_GRANT_POOL, 0, s->grant_pool)) return false;
    if (s->nav_slice && !credit(ctx, ALLOT_DEST_BASKET_NAV, 0, s->nav_slice)) return false;
    (void) rebates(r, s->steward, s->rebate_pool, credit, ctx, &ok);
    if (!ok) return false;
    if (s->crown_endowment && !credit(ctx, ALLOT_DEST_STEWARD, s->steward, s->crown_endowment))
        return false;
    return true;
}

allot_status_t allot_settle(allot_t *r, const allot_settlement_t *s)
{
    allot_settlement_t want;
    if (!r || !s) return ALLOT_ERR_ARG;
    allot_status_t st = allot_select(r, &want);
    if (st != ALLOT_OK) return st;
    if (!pay_memeq(&want, s, sizeof want)) return ALLOT_ERR_SETTLEMENT;
    r->last = want;
    r->phase = ALLOT_SETTLED;
    return ALLOT_OK;
}

allot_status_t allot_next_epoch(allot_t *r)
{
    if (!r) return ALLOT_ERR_ARG;
    if (r->phase != ALLOT_SETTLED) return ALLOT_ERR_PHASE;
    if (r->epoch == UINT64_MAX) return ALLOT_ERR_OVERFLOW;
    allot_config_t c = r->cfg;
    allot_range_t *g = r->ranges;
    uint32_t cap = r->range_cap;
    allot_tally_t *t = r->tally;
    uint32_t tcap = r->tally_cap;
    uint8_t link[ALLOT_HASH_LEN];
    pay_memcpy(link, r->last.link, ALLOT_HASH_LEN);
    allot_status_t st = allot_init(r, &c, r->epoch + 1, link, g, cap);
    if (st == ALLOT_OK && t) st = allot_set_tally(r, t, tcap);
    return st;
}
