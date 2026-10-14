/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_econ_vauction.c — state-machine property harness for V-Bill issuance
 * by descending uniform-price auction (vauction/vauction.c) on a real
 * pay_ledger: "backed only", per-epoch cap, fiat to the reserve, never
 * burned.
 *
 * A fresh ledger per input: fiat (USD) and V-Bills (VBL); the fiat issuer,
 * the reserve, the escrow, a sink, four bidders (fiat + V-Bill account each)
 * and the V-Bill issuer. The first bytes pick the configuration (floor,
 * backing, cap, price ladder, staleness, quorum). The verify hook accepts a
 * signature iff it is the 8-byte FNV-1a of (signer, msg), so both genuine
 * and forged quotes and receipts are made. Ops: CUSTODIAN, ORACLE, FEED,
 * QUOTE, RECEIPT, DISPUTE, OPEN, BID, CLEAR, SETTLE, CANCEL, SET_CONFIG,
 * outside reserve top-up / drain and V-Bill issue / redeem, clock steps, and
 * pure probes of A1-A4, with edge-biased amounts and any account id.
 * Invariants after every op:
 *   F1  fiat is never burned or created by the module: the fiat supply moves
 *       only by outside issuance; escrow + reserve + bidders change only by
 *       transfers among themselves
 *   F2  escrow == sum of the locks of the current epoch's bids not yet
 *       settled or refunded (0 between epochs)
 *   F3  clearing: issued qty <= cap and <= S(price) at the clearing R, N;
 *       fills sum to qty, each fill <= its bid qty, every winner's limit >=
 *       the one uniform price, the price is a ladder rung >= p_min, and no
 *       higher rung had S > 0 and D >= S (descending clock); bids with a
 *       limit at or above the previous rung fill in full before a marginal
 *       bid gets anything
 *   F4  "backed only": the clearing quantity keeps (R + q p) den >= k (N + q)
 *       at the clearing R and N; after a settlement that issued, the floor
 *       holds at that `now` (vauc_floor_ok, and the audit record says so)
 *   F5  settlement: the reserve gains exactly price * qty, N grows by exactly
 *       qty, each winner gets fill V-Bills and the refund locked - fill*p,
 *       each loser its whole lock; cancel issues nothing and refunds all
 *   F6  per-epoch cap: N grows by at most cap over an epoch
 *   F7  authorisation: a forged or unknown-signer quote or receipt is
 *       refused and changes nothing; a quarantined receipt counts 0 forever
 *   F8  A1: vauc_reserve == reserve DEBIT + sum of counted receipts, with
 *       the lower median of fresh quotes recomputed independently
 *   F9  A4: Q(p) is exact (Q passes, Q + 1 fails) and S never falls as p
 *       rises
 *   F10 A7: vauc_cancel is refused (STATE) only out of order, or after a
 *       settlement posting already issued V-Bills (an escrow compliance hold
 *       op makes settlement postings fail)
 *   R   a refused op (other than a resumable settle / cancel) changes no
 *       balance and no auction state
 *   D   determinism */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vauction.h"
#include "fuzz_in.h"

static pay_ledger_t g_L;
static vauc_t g_A;
static unsigned long long g_ops;
static unsigned long long g_hit[32]; /* ops that succeeded, per op code (FUZZ_PROP_STATS) */

enum {
    A_FISS, A_RES, A_ESC, A_SINK, A_BF0, A_BF1, A_BF2, A_BF3,
    A_VISS, A_BV0, A_BV1, A_BV2, A_BV3, NA
};
static uint32_t g_acc[NA];
static uint16_t g_usd, g_vbl;
static uint64_t g_nf;

static uint64_t fnv(uint32_t key, const uint8_t *m, uint32_t n)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (int i = 0; i < 4; i++) h = (h ^ (uint8_t) (key >> (8 * i))) * 0x100000001b3ull;
    for (uint32_t i = 0; i < n; i++) h = (h ^ m[i]) * 0x100000001b3ull;
    return h;
}
static bool verify(void *ctx, uint32_t key, const uint8_t *m, uint32_t n, const uint8_t *sig,
                   uint32_t sl)
{
    (void) ctx;
    uint64_t h = fnv(key, m, n);
    return sl == 8 && memcmp(sig, &h, 8) == 0;
}

static pay_status_t raw(uint32_t a, int64_t da, int64_t ca, uint32_t b, int64_t db, int64_t cb)
{
    pay_posting_req_t r;
    pay_receipt_t rc;
    memset(&r, 0, sizeof r);
    g_nf++;
    uint8_t rnd[16] = {0};
    memcpy(rnd, &g_nf, sizeof g_nf);
    rnd[15] = 0x5B;
    pay_uetr_from_random(rnd, r.uetr);
    snprintf(r.e2e, sizeof r.e2e, "FZ-%llu", (unsigned long long) g_nf);
    memcpy(r.idem_key, &g_nf, sizeof g_nf);
    r.idem_key[31] = 0xEE;
    r.lines[0] = (pay_line_t){a, da, ca};
    r.lines[1] = (pay_line_t){b, db, cb};
    r.n_lines = 2;
    return pay_ledger_post(&g_L, &r, &rc);
}

static uint32_t pick(fz_in *in)
{
    uint8_t b = fz_u8(in);
    if (b >= 0xF8u) return b == 0xFFu ? 0xFFFFFFFFu : (uint32_t) PAY_MAX_ACCOUNTS + b;
    return g_acc[b % NA];
}
static uint64_t amt(fz_in *in)
{
    uint8_t s = fz_u8(in);
    if (s < 0xA0u) return (uint64_t) fz_u16(in) % 3000u + (s & 1u);
    return fz_edge64(in);
}
static uint64_t D(uint32_t i)
{
    return g_L.acct[g_acc[i]].debit;
}
static uint64_t Nout(void)
{
    return g_L.acct[g_acc[A_VISS]].credit;
}

typedef struct {
    uint64_t d[NA], c[NA], lseq;
    vauc_t a;
} snap_t;
static snap_t g_b, g_a;

static void snap(snap_t *s)
{
    for (uint32_t i = 0; i < NA; i++) {
        s->d[i] = g_L.acct[g_acc[i]].debit;
        s->c[i] = g_L.acct[g_acc[i]].credit;
    }
    s->lseq = g_L.seq;
    s->a = g_A;
    s->a.seq = 0; /* identifiers advance even on a refused posting */
    s->a.last_ledger = PAY_OK;
}
static bool same(const snap_t *x, const snap_t *y)
{
    return !memcmp(x->d, y->d, sizeof x->d) && !memcmp(x->c, y->c, sizeof x->c) &&
           x->lseq == y->lseq && !memcmp(&x->a, &y->a, sizeof x->a);
}

/* ---- references ---- */
static bool ref_median(uint32_t commodity, uint64_t now, uint64_t *out)
{
    const vauc_feed_t *f = 0;
    for (uint32_t i = 0; i < VAUC_MAX_FEEDS; i++)
        if (g_A.feed[i].used && g_A.feed[i].commodity == commodity) f = &g_A.feed[i];
    if (!f) return false;
    uint64_t v[VAUC_MAX_QUOTES];
    uint32_t n = 0;
    for (uint32_t i = 0; i < VAUC_MAX_QUOTES; i++)
        if (f->has[i] && f->tick[i] <= now && now - f->tick[i] <= g_A.cfg.max_age)
            v[n++] = f->price[i];
    if (n < g_A.cfg.min_quotes || n == 0) return false;
    for (uint32_t i = 0; i < n; i++)
        for (uint32_t j = i + 1; j < n; j++)
            if (v[j] < v[i]) {
                uint64_t t = v[i];
                v[i] = v[j];
                v[j] = t;
            }
    *out = v[(n - 1) / 2];
    return true;
}
static uint64_t ref_counted(const vauc_receipt_t *r, uint64_t now)
{
    if (!r->used || r->quarantined || now >= r->expiry) return 0;
    const vauc_feed_t *f = 0;
    const vauc_custodian_t *c = 0;
    for (uint32_t i = 0; i < VAUC_MAX_FEEDS; i++)
        if (g_A.feed[i].used && g_A.feed[i].commodity == r->commodity) f = &g_A.feed[i];
    for (uint32_t i = 0; i < VAUC_MAX_CUSTODIANS; i++)
        if (g_A.cust[i].used && g_A.cust[i].id == r->custodian) c = &g_A.cust[i];
    uint64_t med;
    if (!f || !c || !ref_median(r->commodity, now, &med)) return 0;
    __uint128_t v = (__uint128_t) r->qty * med / f->scale;
    if (v >> 64) return 0;
    return (uint64_t) (v * (VAUC_BPS - c->haircut_bps) / VAUC_BPS);
}
/* false when the sum does not fit */
static bool ref_reserve(uint64_t now, uint64_t *R)
{
    __uint128_t s = D(A_RES);
    for (uint32_t i = 0; i < VAUC_MAX_RECEIPTS; i++) {
        s += ref_counted(&g_A.rcpt[i], now);
        if (s >> 64) return false;
    }
    *R = (uint64_t) s;
    return true;
}
static uint64_t kk(void)
{
    return g_A.cfg.floor_num * g_A.cfg.backing;
}
static bool ref_floor(__uint128_t R, __uint128_t N)
{
    /* R * den >= k * N, R and N may be above 2^64 here (fits 192 bits:
     * split as needed; R, N < 2^65 and den, k < 2^64) */
    unsigned __int128 l_hi, l_lo, r_hi, r_lo;
    const unsigned __int128 M = ~(uint64_t) 0;
    l_lo = (R & M) * g_A.cfg.floor_den;
    l_hi = (R >> 64) * g_A.cfg.floor_den + (l_lo >> 64);
    l_lo &= M;
    r_lo = (N & M) * kk();
    r_hi = (N >> 64) * kk() + (r_lo >> 64);
    r_lo &= M;
    return l_hi != r_hi ? l_hi > r_hi : l_lo >= r_lo;
}
static uint64_t ref_supply(uint64_t R, uint64_t N, uint64_t p)
{
    uint64_t q = vauc_dilution_q(&g_A, R, N, p);
    return q < g_A.cfg.cap ? q : g_A.cfg.cap;
}
static uint64_t ref_demand(uint64_t p)
{
    uint64_t d = 0;
    for (uint32_t i = 0; i < g_A.n_bids; i++)
        if (g_A.bid[i].limit >= p) d += g_A.bid[i].qty;
    return d;
}
static uint64_t esc_expect(void)
{
    uint64_t s = 0;
    if (g_A.state == VAUC_OPEN || g_A.state == VAUC_CLEARED)
        for (uint32_t i = 0; i < g_A.n_bids; i++)
            if (!g_A.bid[i].settled) s += g_A.bid[i].locked;
    return s;
}

static void checks(void)
{
    if (!pay_ledger_check(&g_L)) abort();
    if (D(A_ESC) != esc_expect()) abort(); /* F2 */
}

static uint64_t g_now, g_epoch_n0, g_epoch_cap;
static uint64_t g_clear_R, g_clear_N;

static void sign8(uint32_t who, const uint8_t *m, uint32_t n, bool forge, uint8_t sig[8])
{
    uint64_t h = fnv(who, m, n);
    if (forge) h ^= 0x100u;
    memcpy(sig, &h, 8);
}

static void run(const uint8_t *data, size_t size)
{
    fz_in in;
    fz_init(&in, data, size);
    pay_ledger_init(&g_L, NULL);
    if (pay_ledger_add_fiat(&g_L, "USD", 840, 2, &g_usd) != PAY_OK ||
        pay_ledger_add_asset(&g_L, "VBL", 0, PAY_ASSET_UNIT, &g_vbl) != PAY_OK)
        abort();
    for (uint32_t i = 0; i < NA; i++) {
        uint16_t as = i < A_VISS ? g_usd : g_vbl;
        uint8_t fl = (i == A_FISS || i == A_VISS) ? PAY_ACCT_ISSUER : 0;
        /* each user's two accounts share one owner (100 + k) */
        uint32_t owner = i >= A_BV0 ? 100u + (i - A_BV0) : (i >= A_BF0 && i < A_VISS) ? 100u + (i - A_BF0) : 10u + i;
        if (pay_ledger_open(&g_L, owner, as, PAY_CAP_FINANCIAL, fl, &g_acc[i]) != PAY_OK) abort();
    }
    g_nf = 0;
    uint64_t r0 = (uint64_t) fz_u16(&in) * 10u, n0 = fz_u16(&in) % 2000u;
    if (raw(g_acc[A_RES], (int64_t) r0, 0, g_acc[A_FISS], 0, (int64_t) r0) != PAY_OK) abort();
    if (n0 && raw(g_acc[A_BV0], (int64_t) n0, 0, g_acc[A_VISS], 0, (int64_t) n0) != PAY_OK)
        abort();
    for (uint32_t i = 0; i < 4; i++)
        if (raw(g_acc[A_BF0 + i], 10000000, 0, g_acc[A_FISS], 0, 10000000) != PAY_OK) abort();

    vauc_config_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.floor_num = fz_u8(&in) % 4u + 1u;
    cfg.floor_den = fz_u8(&in) % 4u + 1u;
    cfg.backing = fz_u8(&in) % 20u + 1u;
    cfg.cap = fz_u16(&in) % 500u + 1u;
    cfg.p_min = fz_u8(&in) % 30u + 1u;
    cfg.p_start = cfg.p_min + fz_u8(&in) % 60u;
    cfg.p_step = fz_u8(&in) % 7u + 1u;
    cfg.max_age = fz_u8(&in) % 20u;
    cfg.min_quotes = fz_u8(&in) % 3u + 1u;
    uint8_t seed[32];
    for (int i = 0; i < 32; i++) seed[i] = (uint8_t) (i * 3 + 1);
    if (vauc_init(&g_A, &g_L, &cfg, g_acc[A_RES], g_acc[A_ESC], g_acc[A_VISS], verify, 0, seed) !=
        VAUC_OK)
        abort();
    /* a starting collateral setup (ops may add more): oracles 1-3, feeds
     * for commodities 0 and 1, custodians 20 and 21 */
    if (vauc_oracle_add(&g_A, 1) || vauc_oracle_add(&g_A, 2) || vauc_oracle_add(&g_A, 3) ||
        vauc_feed_add(&g_A, 0, 1) || vauc_feed_add(&g_A, 1, 1000) ||
        vauc_custodian_set(&g_A, 20, fz_u16(&in) % (VAUC_BPS + 1u)) ||
        vauc_custodian_set(&g_A, 21, 0))
        abort();
    g_now = 100;
    g_epoch_n0 = Nout();
    g_epoch_cap = cfg.cap;
    checks();

    while (in.n) {
        /* bias toward the epoch cycle so that auctions really clear and settle */
        static const uint8_t cycle[8] = {6, 7, 7, 7, 7, 8, 9, 3};
        uint8_t ob = fz_u8(&in);
        uint8_t op = ob < 0x50u ? cycle[ob % 8u] : (uint8_t) (ob % 18u);
        g_ops++;
        snap(&g_b);
        uint64_t fsup = g_L.acct[g_acc[A_FISS]].credit, N0 = Nout(), res0 = D(A_RES);
        uint64_t minted = 0, dN = 0, dRes = 0;
        bool strict = true, ok = false;
        vauc_status_t st = VAUC_OK;
        uint64_t now = g_now + fz_u8(&in) % 3u;
        switch (op) {
        case 0: { /* CUSTODIAN */
            uint32_t id = 20u + fz_u8(&in) % 5u;
            uint32_t hc = (uint32_t) (amt(&in) % (VAUC_BPS + 3u));
            st = vauc_custodian_set(&g_A, id, hc);
            if ((hc > VAUC_BPS) && st == VAUC_OK) abort();
            break;
        }
        case 1: /* ORACLE */
            st = vauc_oracle_add(&g_A, 1u + fz_u8(&in) % 5u);
            break;
        case 2: { /* FEED */
            uint8_t w = fz_u8(&in);
            uint64_t scale = (w & 0x80u) ? fz_edge64(&in) : (uint64_t) (w % 4u) * 10u;
            st = vauc_feed_add(&g_A, fz_u8(&in) % 4u, scale);
            break;
        }
        case 3: { /* QUOTE */
            uint8_t w = fz_u8(&in);
            uint32_t orc = 1u + fz_u8(&in) % 5u, com = fz_u8(&in) % 4u;
            uint64_t price = (w & 0x40u) ? fz_edge64(&in) : fz_u8(&in) % 200u;
            uint64_t tick = (w & 0x20u) ? fz_edge64(&in) : now - (w & 7u);
            uint8_t m[VAUC_QUOTE_MSG], sig[8];
            vauc_quote_msg(com, price, tick, m);
            bool forge = (w & 0x80u) && (w & 0x10u);
            sign8((w & 0x08u) && (w & 0x80u) ? orc + 1u : orc, m, sizeof m, forge, sig);
            st = vauc_quote(&g_A, orc, com, price, tick, sig, 8);
            if (((w & 0x80u) && (w & 0x18u)) && st == VAUC_OK) abort(); /* F7 */
            break;
        }
        case 4: { /* RECEIPT */
            uint8_t w = fz_u8(&in);
            uint32_t cus = 20u + fz_u8(&in) % 5u, com = fz_u8(&in) % 4u;
            uint8_t id[32] = {0};
            id[0] = fz_u8(&in) % 12u;
            uint64_t qty = amt(&in);
            uint64_t exp = (w & 0x40u) ? fz_edge64(&in) : now + fz_u8(&in) % 50u;
            uint8_t m[VAUC_RCPT_MSG], sig[8];
            vauc_receipt_msg(cus, id, com, qty, exp, m);
            bool forge = (w & 0x80u) && (w & 0x10u);
            sign8((w & 0x08u) && (w & 0x80u) ? cus + 1u : cus, m, sizeof m, forge, sig);
            st = vauc_receipt_add(&g_A, cus, id, com, qty, exp, now, sig, 8);
            if (((w & 0x80u) && (w & 0x18u)) && st == VAUC_OK) abort(); /* F7 */
            break;
        }
        case 5: { /* DISPUTE */
            uint8_t id[32] = {0};
            id[0] = fz_u8(&in) % 12u;
            st = vauc_receipt_dispute(&g_A, id);
            if (st == VAUC_OK) {
                if (vauc_receipt_counted(&g_A, id, now) != 0) abort(); /* F7 */
                uint8_t list[VAUC_MAX_RECEIPTS][32];
                uint32_t nq = vauc_quarantine_list(&g_A, list, VAUC_MAX_RECEIPTS);
                bool found = false;
                for (uint32_t i = 0; i < nq && i < VAUC_MAX_RECEIPTS; i++)
                    if (!memcmp(list[i], id, 32)) found = true;
                if (!found) abort();
            }
            break;
        }
        case 6: /* OPEN */
            st = vauc_open(&g_A);
            if (st == VAUC_OK) {
                g_epoch_n0 = Nout();
                g_epoch_cap = g_A.cfg.cap;
            }
            break;
        case 7: { /* BID */
            uint8_t w = fz_u8(&in);
            uint32_t f = (w & 0xC0u) == 0xC0u ? pick(&in) : g_acc[A_BF0 + (w & 3u)];
            uint32_t v = (w & 0x30u) == 0x30u ? pick(&in) : g_acc[A_BV0 + ((w >> 2) & 3u)];
            uint64_t qty = (w & 0x08u) ? amt(&in) : fz_u8(&in) % 60u + 1u;
            uint64_t lim = (w & 0x08u) ? amt(&in) : g_A.cfg.p_min + fz_u8(&in) % 70u;
            uint32_t idx = 0xFFFFu;
            uint64_t f0 = f < g_L.n_accounts ? g_L.acct[f].debit : 0;
            st = vauc_bid(&g_A, f, v, qty, lim, now, &idx);
            if (st == VAUC_OK) {
                if (g_b.a.state != VAUC_OPEN || idx != g_b.a.n_bids) abort();
                const vauc_bid_t *b = &g_A.bid[idx];
                if ((__uint128_t) qty * lim != b->locked || lim < g_A.cfg.p_min) abort();
                if (g_L.acct[f].debit != f0 - b->locked || D(A_ESC) != g_b.d[A_ESC] + b->locked)
                    abort();
                if (f == g_acc[A_ESC] || f == g_acc[A_RES] || v == g_acc[A_VISS]) abort();
                if (g_A.locked >= PAY_BAL_MAX) abort();
            }
            break;
        }
        case 8: { /* CLEAR */
            uint64_t Rr = 0;
            bool rok = ref_reserve(now, &Rr);
            st = vauc_clear(&g_A, now);
            if (st != VAUC_OK) break;
            if (!rok || g_A.cur.r_before != Rr || g_A.cur.n_before != N0) abort(); /* F8 */
            g_clear_R = Rr;
            g_clear_N = N0;
            const vauc_config_t *c = &g_A.cfg;
            uint64_t p = g_A.cur.price, q = g_A.cur.qty, sumf = 0;
            /* F3: the price is a rung; walk the ladder */
            uint64_t prev = 0, r = c->p_start;
            bool on = false;
            for (;;) {
                if (r == p) {
                    on = true;
                    break;
                }
                uint64_t S = ref_supply(Rr, N0, r), Dd = ref_demand(r);
                if (S > 0 && Dd >= S) abort(); /* an earlier rung should have cleared */
                if (r == c->p_min) break;
                prev = r;
                r = r - c->p_min > c->p_step ? r - c->p_step : c->p_min;
            }
            if (!on) abort();
            if (q > c->cap || q > ref_supply(Rr, N0, p)) abort();
            /* F4: backed only, at clearing R and N */
            if (q && !ref_floor((__uint128_t) Rr + (__uint128_t) q * p, (__uint128_t) N0 + q))
                abort();
            bool marginal_filled = false;
            for (uint32_t i = 0; i < g_A.n_bids; i++) {
                const vauc_bid_t *b = &g_A.bid[i];
                if (b->fill > b->qty) abort();
                if (b->fill && b->limit < p) abort();
                if (b->fill && prev && b->limit < prev) marginal_filled = true;
                sumf += b->fill;
            }
            if (sumf != q) abort();
            if (marginal_filled)
                for (uint32_t i = 0; i < g_A.n_bids; i++)
                    if (g_A.bid[i].limit >= prev && g_A.bid[i].fill != g_A.bid[i].qty) abort();
            break;
        }
        case 9: { /* SETTLE */
            uint64_t bf[4], bv[4];
            for (uint32_t i = 0; i < 4; i++) {
                bf[i] = D(A_BF0 + i);
                bv[i] = D(A_BV0 + i);
            }
            st = vauc_settle(&g_A, now);
            if (st == VAUC_OK) {
                uint64_t p = g_A.cur.price, q = g_A.cur.qty, issued = 0;
                for (uint32_t i = 0; i < g_b.a.n_bids; i++)
                    if (!g_b.a.bid[i].settled) issued += g_b.a.bid[i].fill;
                dN = issued;
                dRes = issued * p;
                if (g_A.state != VAUC_SETTLED || D(A_ESC) != 0) abort();
                if (issued && (!vauc_floor_ok(&g_A, now) || !g_A.cur.floor_ok)) abort(); /* F4 */
                const vauc_record_t *rec = vauc_record(&g_A, g_A.epoch);
                if (!rec || rec->qty != q || rec->price != p) abort();
                /* F5: per-bidder (only for the four standard pairs, when the
                 * bids used them uniquely, checked in aggregate) */
                uint64_t want_f[4] = {0}, want_v[4] = {0};
                bool simple = true;
                for (uint32_t i = 0; i < g_b.a.n_bids; i++) {
                    const vauc_bid_t *b = &g_b.a.bid[i];
                    if (b->settled) {
                        simple = false;
                        continue;
                    }
                    int fi = -1, vi = -1;
                    for (int k = 0; k < 4; k++) {
                        if (b->fiat == g_acc[A_BF0 + k]) fi = k;
                        if (b->vbill == g_acc[A_BV0 + k]) vi = k;
                    }
                    if (fi < 0 || vi < 0) {
                        simple = false;
                        continue;
                    }
                    want_f[fi] += b->locked - b->fill * p;
                    want_v[vi] += b->fill;
                }
                if (simple)
                    for (int k = 0; k < 4; k++)
                        if (D(A_BF0 + k) != bf[k] + want_f[k] || D(A_BV0 + k) != bv[k] + want_v[k])
                            abort();
            } else if (g_b.a.mode == 1 || st == VAUC_ERR_LEDGER) {
                strict = false; /* resumable: earlier postings stand */
                for (uint32_t i = 0; i < g_b.a.n_bids; i++)
                    if (!g_b.a.bid[i].settled && g_A.bid[i].settled) {
                        dN += g_A.bid[i].fill;
                        dRes += g_A.bid[i].fill * g_A.cur.price;
                    }
            }
            break;
        }
        case 10: { /* CANCEL */
            bool issued_any = false;
            for (uint32_t i = 0; i < g_b.a.n_bids; i++)
                if (g_b.a.bid[i].settled && g_b.a.bid[i].fill) issued_any = true;
            st = vauc_cancel(&g_A, now);
            /* A7: cancel is allowed while OPEN, or CLEARED before any
             * settlement posting issued anything */
            if (st == VAUC_ERR_STATE &&
                (g_b.a.state == VAUC_OPEN || g_b.a.state == VAUC_CLEARED) && !issued_any) {
                fprintf(stderr, "A7: cancel refused although nothing was issued (mode %u)\n",
                        (unsigned) g_b.a.mode);
                abort();
            }
            if (st == VAUC_OK) {
                if (g_A.state != VAUC_SETTLED || D(A_ESC) != 0 || !g_A.cur.cancelled) abort();
            } else if (st == VAUC_ERR_LEDGER)
                strict = false;
            break;
        }
        case 11: { /* SET_CONFIG */
            vauc_config_t c = g_A.cfg;
            uint8_t w = fz_u8(&in);
            if (w & 1u) c.cap = amt(&in);
            if (w & 2u) c.p_step = amt(&in);
            if (w & 4u) c.p_min = fz_u8(&in) % 40u;
            if (w & 8u) c.p_start = c.p_min + amt(&in) % 100u;
            if (w & 16u) c.floor_num = fz_edge64(&in);
            if (w & 32u) c.backing = fz_edge64(&in);
            if (w & 64u) c.min_quotes = fz_u8(&in) % 10u;
            st = vauc_set_config(&g_A, &c);
            if (st == VAUC_OK) {
                if (!(g_b.a.state == VAUC_IDLE || g_b.a.state == VAUC_SETTLED)) abort();
                if (c.cap == 0 || c.cap >= PAY_BAL_MAX || c.p_min == 0 || c.p_start < c.p_min ||
                    !c.floor_num || !c.floor_den || !c.backing || !c.p_step || !c.min_quotes ||
                    c.min_quotes > VAUC_MAX_QUOTES ||
                    (__uint128_t) c.floor_num * c.backing >> 64)
                    abort();
            }
            break;
        }
        case 12: { /* outside: reserve top-up */
            uint64_t a = amt(&in);
            if (raw(g_acc[A_RES], (int64_t) a, 0, g_acc[A_FISS], 0, (int64_t) a) == PAY_OK) {
                minted = a;
                dRes = a;
            }
            st = VAUC_OK;
            ok = true;
            break;
        }
        case 13: { /* outside: reserve drained */
            uint64_t a = amt(&in);
            if (raw(g_acc[A_RES], (int64_t) (0 - a), 0, g_acc[A_SINK], (int64_t) a, 0) == PAY_OK)
                dRes = 0 - a;
            ok = true;
            break;
        }
        case 14: { /* outside: V-Bills issued or redeemed */
            uint64_t a = amt(&in);
            uint8_t w = fz_u8(&in);
            int64_t s = (w & 1u) ? (int64_t) (0 - a) : (int64_t) a;
            if (raw(g_acc[A_BV0 + (w >> 1) % 4u], s, 0, g_acc[A_VISS], 0, s) == PAY_OK)
                dN = (uint64_t) s;
            /* outside issuance is not the auction's: move the epoch base up
             * (a redemption only lowers N, which keeps F6 conservative) */
            if ((int64_t) dN > 0) g_epoch_n0 += dN;
            ok = true;
            break;
        }
        case 17: { /* compliance hold on the escrow, on or off */
            pay_ledger_set_flags(&g_L, g_acc[A_ESC], (fz_u8(&in) & 3u) == 0 ? PAY_ACCT_FROZEN : 0);
            ok = true;
            break;
        }
        case 15: /* clock */
            g_now += fz_u8(&in) % 16u;
            ok = true;
            break;
        default: { /* pure probes: A1-A4 */
            uint64_t R = 0, Rr = 0;
            vauc_status_t rs = vauc_reserve(&g_A, now, &R);
            bool rok = ref_reserve(now, &Rr);
            if ((rs == VAUC_OK) != rok || (rok && R != Rr)) abort(); /* F8 */
            for (uint32_t i = 0; i < VAUC_MAX_RECEIPTS; i++)
                if (g_A.rcpt[i].used &&
                    vauc_receipt_counted(&g_A, g_A.rcpt[i].id, now) != ref_counted(&g_A.rcpt[i], now))
                    abort();
            uint64_t Rp = (fz_u8(&in) & 1u) ? amt(&in) : R, Np = (fz_u8(&in) & 1u) ? amt(&in) : Nout();
            uint64_t p1 = amt(&in), p2 = amt(&in);
            if (p1 > p2) {
                uint64_t t = p1;
                p1 = p2;
                p2 = t;
            }
            uint64_t q1 = vauc_dilution_q(&g_A, Rp, Np, p1), q2 = vauc_dilution_q(&g_A, Rp, Np, p2);
            if (q1 > q2) abort(); /* F9 */
            if (vauc_supply(&g_A, Rp, Np, p1) > vauc_supply(&g_A, Rp, Np, p2)) abort();
            if (!ref_floor(Rp, Np)) {
                if (q1 || q2) abort();
            } else if (q1 != VAUC_UNBOUNDED) {
                /* Q passes, Q + 1 fails */
                if (!ref_floor((__uint128_t) Rp + (__uint128_t) q1 * p1, (__uint128_t) Np + q1))
                    abort();
                if (ref_floor((__uint128_t) Rp + (__uint128_t) (q1 + 1) * p1,
                              (__uint128_t) Np + q1 + 1))
                    abort();
            }
            if (vauc_floor_ok(&g_A, now) != (rok && ref_floor(Rr, Nout()))) abort();
            ok = true;
            break;
        }
        }
        (void) ok;
        if (st == VAUC_OK) g_hit[op]++;
        if (op == 9 && st == VAUC_OK && g_A.cur.qty) g_hit[20]++; /* settled with issuance */
        if (op == 8 && st == VAUC_OK && g_A.cur.qty) g_hit[21]++; /* cleared with issuance */
        snap(&g_a);
        if (st != VAUC_OK && strict && !same(&g_a, &g_b)) abort(); /* R */
        /* F1: fiat supply only by outside issuance; reserve only by settlement */
        if (g_L.acct[g_acc[A_FISS]].credit != fsup + minted) abort();
        if (D(A_RES) - res0 != dRes) abort();
        if (Nout() - N0 != dN) abort();
        /* F6: per-epoch cap (outside issuance is added to the epoch base) */
        if (Nout() > g_epoch_n0 && Nout() - g_epoch_n0 > g_epoch_cap) {
            fprintf(stderr, "F6: op %u epoch %llu N %llu base %llu cap %llu state %d qty %llu\n", op,
                    (unsigned long long) g_A.epoch, (unsigned long long) Nout(),
                    (unsigned long long) g_epoch_n0, (unsigned long long) g_epoch_cap,
                    (int) g_A.state, (unsigned long long) g_A.cur.qty);
            abort();
        }
        checks();
    }
    if (!pay_ledger_verify_chain(&g_L)) abort(); /* once per input: it is O(journal) */
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 8192) return 0;
    static pay_account_t acct1[PAY_MAX_ACCOUNTS];
    static vauc_t a1;
    static uint8_t head1[PAY_HASH_LEN];
    unsigned long long ops = g_ops;
    run(data, size);
    memcpy(acct1, g_L.acct, sizeof acct1);
    a1 = g_A;
    memcpy(head1, g_L.chain_head, sizeof head1);
    run(data, size);
    g_ops = ops + (g_ops - ops) / 2;
    /* D */
    if (memcmp(acct1, g_L.acct, sizeof acct1) || memcmp(head1, g_L.chain_head, sizeof head1) ||
        memcmp(&a1, &g_A, sizeof a1))
        abort();
    return 0;
}

__attribute__((destructor)) static void report(void)
{
    if (getenv("FUZZ_PROP_STATS")) fprintf(stderr, "fuzz_econ_vauction: %llu ops checked\n", g_ops);
    if (getenv("FUZZ_PROP_STATS")) {
        fprintf(stderr, "  succeeded per op:");
        for (int i = 0; i < 32; i++)
            if (g_hit[i]) fprintf(stderr, " %d:%llu", i, g_hit[i]);
        fprintf(stderr, "\n");
    }
}
