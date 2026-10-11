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
    c->refuse_dust = true;
    c->fee_num = RAFFLE_DEFAULT_FEE_NUM;
    c->fee_den = RAFFLE_DEFAULT_FEE_DEN;
    c->skim_num = RAFFLE_DEFAULT_FEE_NUM;
    c->skim_den = RAFFLE_DEFAULT_FEE_DEN;
    c->ticket_price = 1;
    c->fib_index = RAFFLE_DEFAULT_FIB_INDEX;
    c->min_reveals = 0;
}

raffle_status_t raffle_config_check(const raffle_config_t *c)
{
    if (!c) return RAFFLE_ERR_ARG;
    if (c->fee_den == 0 || c->fee_num >= c->fee_den) return RAFFLE_ERR_CONFIG;
    if (c->skim_den == 0 || c->skim_num >= c->skim_den) return RAFFLE_ERR_CONFIG;
    if (c->ticket_price == 0) return RAFFLE_ERR_CONFIG;
    if (c->fib_index < RAFFLE_FIB_MIN || c->fib_index > RAFFLE_FIB_MAX) return RAFFLE_ERR_CONFIG;
    if (c->min_reveals > RAFFLE_MAX_PARTIES) return RAFFLE_ERR_CONFIG;
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

raffle_status_t raffle_quote(const raffle_config_t *c, uint64_t amount, uint64_t *fee,
                             uint64_t *tickets)
{
    raffle_status_t st = raffle_config_check(c);
    if (st != RAFFLE_OK) return st;
    uint64_t f = ratio_floor(amount, c->fee_num, c->fee_den);
    uint64_t n = pay_udiv64(f, c->ticket_price, 0);
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
    r->threshold = raffle_fib(c->fib_index);
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
    pre_str(&p, "ZXV-RAFFLE-HDR1");
    pre_u64(&p, r->epoch);
    pre_u64(&p, r->cfg.fee_num);
    pre_u64(&p, r->cfg.fee_den);
    pre_u64(&p, r->cfg.skim_num);
    pre_u64(&p, r->cfg.skim_den);
    pre_u64(&p, r->cfg.ticket_price);
    pre_u64(&p, r->threshold);
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

/* Mint `count` tickets to `owner` for `fee` (kind 0 = paid, 1 = free). */
static raffle_status_t mint(raffle_t *r, const uint8_t owner[RAFFLE_ID_LEN], uint64_t amount,
                            uint64_t fee, uint64_t count, uint8_t kind, raffle_receipt_t *out)
{
    uint64_t tickets2 = r->tickets, pool2;
    if (!pay_add_ok(r->pool, fee, &pool2)) return RAFFLE_ERR_OVERFLOW;
    if (count && !pay_add_ok(r->tickets, count, &tickets2)) return RAFFLE_ERR_OVERFLOW;
    if (count && r->range_len >= r->range_cap) return RAFFLE_ERR_FULL;

    uint64_t first = r->tickets;
    if (count) {
        raffle_range_t *g = &r->ranges[r->range_len++];
        pay_memcpy(g->owner, owner, RAFFLE_ID_LEN);
        g->first = first;
        g->count = count;
    }
    /* E3: fold the entry into the chain. */
    pre_t p;
    p.len = 0;
    pre_str(&p, "ZXV-RAFFLE-ENTRY");
    pre_bytes(&p, r->chain, RAFFLE_HASH_LEN);
    pre_bytes(&p, owner, RAFFLE_ID_LEN);
    pre_u64(&p, amount);
    pre_u64(&p, fee);
    pre_u64(&p, first);
    pre_u64(&p, count);
    pre_bytes(&p, &kind, 1);
    sha3_256(p.buf, p.len, r->chain);

    r->pool = pool2;
    r->tickets = tickets2;
    if (kind)
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
                             raffle_receipt_t *out)
{
    uint64_t fee, n;
    if (!r || !owner) return RAFFLE_ERR_ARG;
    if (!r->cfg.jurisdiction_permits) return RAFFLE_ERR_DISABLED;
    if (r->phase != RAFFLE_OPEN) return RAFFLE_ERR_PHASE;
    raffle_status_t st = raffle_quote(&r->cfg, amount, &fee, &n);
    if (st != RAFFLE_OK) return st;
    if (n == 0 && r->cfg.refuse_dust) return RAFFLE_ERR_DUST;
    return mint(r, owner, amount, fee, n, 0, out);
}

raffle_status_t raffle_free_entry(raffle_t *r, const uint8_t owner[RAFFLE_ID_LEN],
                                  raffle_receipt_t *out)
{
    if (!r || !owner) return RAFFLE_ERR_ARG;
    if (!r->cfg.jurisdiction_permits || !r->cfg.free_entry) return RAFFLE_ERR_DISABLED;
    if (r->phase != RAFFLE_OPEN) return RAFFLE_ERR_PHASE;
    return mint(r, owner, 0, 0, 1, 1, out);
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

static void settlement_link(const raffle_settlement_t *s, uint8_t out[RAFFLE_HASH_LEN])
{
    pre_t p;
    p.len = 0;
    pre_str(&p, "ZXV-RAFFLE-SETTLE");
    pre_u64(&p, s->epoch);
    pre_bytes(&p, s->header_hash, RAFFLE_HASH_LEN);
    pre_bytes(&p, s->seed, RAFFLE_HASH_LEN);
    pre_u64(&p, s->winning_ticket);
    pre_bytes(&p, s->winner, RAFFLE_ID_LEN);
    pre_u64(&p, s->pool);
    pre_u64(&p, s->reserve_skim);
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
    s.crown_pot = r->pool - s.reserve_skim;
    settlement_link(&s, s.link);
    *out = s;
    return RAFFLE_OK;
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
    uint8_t link[RAFFLE_HASH_LEN];
    pay_memcpy(link, r->last.link, RAFFLE_HASH_LEN);
    return raffle_init(r, &c, r->epoch + 1, link, g, cap);
}
