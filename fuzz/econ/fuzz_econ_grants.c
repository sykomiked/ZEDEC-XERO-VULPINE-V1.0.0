/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_econ_grants.c — state-machine property harness for the Olive Oil
 * Barrel grants (grants/grants.c) on a real pay_ledger.
 *
 * Eight participants (owner 200+i) each hold a BRL-V and a Vino account. The
 * verify hook accepts sig == first 8 bytes of SHA-256(signer || msg). Ops:
 * EPOCH_BEGIN, POOL_CREDIT (from the raffle account, or from any account
 * including the pool and the reserve), POOL_RECORD (with or without a real
 * pool deposit first), APPEAL, DISTRIBUTE (eligible or not, edge prices),
 * LOT_OPEN, UNDERWRITER_ADD, ATTEST, DISPUTE, REDEEM (own or another
 * owner's Vino account), SET_CAP / SET_THRESHOLDS, FUND (explicit mint).
 * Invariants after every op:
 *   G-CONS  the sum of every Vino account's DEBIT changes only by FUND; the
 *           barrel issuer's CREDIT changes only by a distribute (+B) or a
 *           redeem (-n)
 *   G-REF   a refused op changes nothing (module state modulo seq /
 *           last_ledger; every ledger account), except that a distribute
 *           refused AFTER every recipient verified (EMPTY / FULL / OVERFLOW /
 *           LEDGER) records exactly their weight W as the epoch's active
 *           verified weight, as G3 and test_grants specify (define
 *           GRANTS_STRICT_WEIGHT to forbid even that)
 *   G-SEEN  pool_seen (inflow already counted) never exceeds the pool's
 *           balance, so inflow is never recorded for money that did not land
 *           — define GRANTS_ALLOW_SELF_CREDIT to skip
 *   G-AUTH  a redemption pays only a Vino account of the barrels' owner —
 *           define GRANTS_ALLOW_ANY_PAYEE to skip
 *   G1      grant_check (holdings == lot counts == issuer CREDIT, holder
 *           DEBIT == holdings) and pay_ledger_check pass
 *   G2/G3   a distribute succeeds only when every recipient is eligible by
 *           an independent model of the thresholds and the signed appeals;
 *           B == floor(floor(P*num/den)/price), shares are the exact
 *           largest-remainder split, cost B*price leaves the pool >= 0,
 *           one distribution per epoch, never into an attested lot
 *   G5      redeem only for a backed lot (attested, not disputed, now <
 *           expiry) and only up to the holding; attest only by a listed
 *           underwriter over the exact outstanding count with a later expiry
 *   NO USURY: no balance grows with the tick; G_RATE is inflow / weight. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "grants.h"
#include "../robin_debanks/sha256.h"
#include "fuzz_in.h"

#define NP     8u
#define FUNNEL 66u
#define OWNER  9u

static pay_ledger_t L;
static grant_t G;
static uint16_t vno;
static uint32_t viss, raffle, brl[NP], vin[NP];
static uint64_t nfund;
static unsigned long long g_ops, g_okc[16];

typedef struct {
    uint32_t subject;
    uint64_t epoch;
    uint32_t mask;
} appeal_m;
static appeal_m g_ap[64];
static uint32_t g_nap;

static void mksig(uint32_t who, const uint8_t *m, uint32_t n, uint8_t sig[8])
{
    uint8_t b[128], h[32];
    memcpy(b, &who, 4);
    memcpy(b + 4, m, n);
    sha256(b, 4 + n, h);
    memcpy(sig, h, 8);
}

static bool verify(void *ctx, uint32_t who, const uint8_t *m, uint32_t n, const uint8_t *sig,
                   uint32_t sl)
{
    (void) ctx;
    uint8_t want[8];
    if (sl != 8 || n > 120) return false;
    mksig(who, m, n, want);
    return !memcmp(want, sig, 8);
}

static pay_status_t raw(uint32_t a, int64_t da, uint32_t b, int64_t db, int64_t cb)
{
    pay_posting_req_t r;
    pay_receipt_t rc;
    memset(&r, 0, sizeof r);
    nfund++;
    snprintf(r.uetr, sizeof r.uetr, "00000000-0000-4000-8000-%012llx", (unsigned long long) nfund);
    snprintf(r.e2e, sizeof r.e2e, "FUND-%llu", (unsigned long long) nfund);
    memcpy(r.idem_key, &nfund, sizeof nfund);
    r.idem_key[31] = 0xEE;
    r.lines[0] = (pay_line_t){a, da, 0};
    r.lines[1] = (pay_line_t){b, db, cb};
    r.n_lines = 2;
    return pay_ledger_post(&L, &r, &rc);
}

static uint32_t opn(uint16_t as, uint32_t owner, uint8_t flags)
{
    uint32_t a = 0;
    if (pay_ledger_open(&L, owner, as, PAY_CAP_FINANCIAL, flags, &a) != PAY_OK) abort();
    return a;
}

static const pay_account_t *A(uint32_t a)
{
    return pay_ledger_account(&L, a);
}

static __uint128_t vsum(void)
{
    __uint128_t s = 0;
    for (uint32_t i = 0; i < L.n_accounts; i++)
        if (L.acct[i].asset == vno) s += L.acct[i].debit;
    return s;
}

static void setup(void)
{
    pay_ledger_init(&L, NULL);
    nfund = 0;
    g_nap = 0;
    if (pay_ledger_add_asset(&L, "VNO", 2, PAY_ASSET_UNIT, &vno) != PAY_OK) abort();
    viss = opn(vno, 1, PAY_ACCT_ISSUER);
    raffle = opn(vno, 2, 0);
    uint8_t seed[32];
    for (int i = 0; i < 32; i++) seed[i] = (uint8_t) (i * 3 + 1);
    if (grant_init(&G, &L, vno, OWNER, FUNNEL, verify, NULL, seed) != GRANT_OK) abort();
    for (uint32_t i = 0; i < NP; i++) {
        brl[i] = opn(G.barrel, 200 + i, 0);
        vin[i] = opn(vno, 200 + i, 0);
    }
    if (raw(raffle, 1000000000000, viss, 0, 1000000000000) != PAY_OK) abort();
    if (grant_lot_open(&G, 1) != GRANT_OK) abort();
    if (grant_underwriter_add(&G, 60) != GRANT_OK) abort();
    if (grant_epoch_begin(&G, 1) != GRANT_OK) abort();
}

static bool same_state(const grant_t *a, const pay_account_t *acct0, bool weight_ok)
{
    grant_t x = *a, y = G;
    x.seq = y.seq = 0;
    x.last_ledger = y.last_ledger = PAY_OK;
    if (weight_ok) x.hist[x.epoch & (GRANT_HIST - 1u)].weight = y.hist[y.epoch & (GRANT_HIST - 1u)].weight;
    return !memcmp(&x, &y, sizeof x) && !memcmp(acct0, L.acct, L.n_accounts * sizeof L.acct[0]);
}

/* G2 model */
static bool eligible(const grant_metrics_t *m, uint64_t epoch)
{
    uint32_t r = 0;
    if (m->uptime_bps > 10000u) r |= GRANT_FAIL_INVALID;
    if (m->uptime_bps < G.th.min_uptime_bps) r |= GRANT_FAIL_UPTIME;
    if (m->dropped_reports) r |= GRANT_FAIL_DROPPED;
    if (m->routing_reports) r |= GRANT_FAIL_ROUTING;
    if (m->failed_challenges) r |= GRANT_FAIL_AUDIT;
    if (m->fraud_findings) r |= GRANT_FAIL_FRAUD;
    if (m->volume < G.th.min_volume) r |= GRANT_FAIL_VOLUME;
    if (m->stake < G.th.min_stake) r |= GRANT_FAIL_STAKE;
    for (uint32_t i = 0; i < g_nap; i++)
        if (g_ap[i].subject == m->subject && g_ap[i].epoch == epoch) r &= ~g_ap[i].mask;
    return r == 0;
}

static void metrics(fz_in *in, grant_metrics_t *m, uint32_t subject)
{
    uint8_t b = fz_u8(in);
    memset(m, 0, sizeof *m);
    m->subject = subject;
    m->uptime_bps = 9500 + fz_u8(in) * 2u;
    m->volume = 10000 + fz_u8(in);
    m->stake = 100000 + fz_u8(in);
    if (b >= 0xE0) {
        switch (b & 7) {
        case 0: m->uptime_bps = 9499; break;
        case 1: m->dropped_reports = 1; break;
        case 2: m->routing_reports = 1; break;
        case 3: m->failed_challenges = 1; break;
        case 4: m->fraud_findings = 1; break;
        case 5: m->volume = 9999; break;
        case 6: m->stake = 99999; break;
        default: m->uptime_bps = 10001; break;
        }
    }
}

static uint64_t small_or(uint8_t b)
{
    return b < 250 ? (uint64_t) (b % 8) : UINT64_MAX - (b & 1);
}

static uint32_t pick_lot(fz_in *in)
{
    uint8_t b = fz_u8(in);
    if (b < 200) {
        for (uint32_t k = 0; k < GRANT_MAX_LOTS; k++) {
            uint32_t i = (b + k) % GRANT_MAX_LOTS;
            if (G.lot[i].used) return G.lot[i].id;
        }
    }
    return b % 20;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 4096) return 0;
    fz_in in;
    fz_init(&in, data, size);
    setup();
    static grant_t snap;
    static pay_account_t asnap[PAY_MAX_ACCOUNTS];
    uint64_t now = 0;

    while (in.n) {
        uint8_t op = fz_u8(&in) % 12u;
        g_ops++;
        now += fz_u8(&in);
        snap = G;
        memcpy(asnap, L.acct, L.n_accounts * sizeof L.acct[0]);
        __uint128_t s0 = vsum(), minted = 0;
        uint64_t icred0 = A(G.issuer)->credit, dcred = 0;
        int st = 0;
        bool weight_ok = false;

        if (op == 0) { /* EPOCH_BEGIN */
            uint8_t f = fz_u8(&in);
            uint64_t e = (f & 3) ? G.epoch + 1 + (f >> 5) : fz_edge64(&in);
            st = grant_epoch_begin(&G, e);
            if (st == GRANT_OK && e <= snap.epoch) abort();
        } else if (op == 1) { /* POOL_CREDIT */
            uint8_t f = fz_u8(&in);
            uint32_t from = (f & 3) ? raffle
                            : (f & 4) ? G.pool
                            : (f & 8) ? G.reserve
                                      : vin[f % NP];
            uint64_t amt = (fz_u8(&in) & 1) ? fz_u32(&in) : fz_edge64(&in);
            uint64_t pb = A(G.pool)->debit;
            st = grant_pool_credit(&G, from, amt, now);
            if (st == GRANT_OK) {
                if (G.hist[G.epoch & 7].inflow != snap.hist[snap.epoch & 7].inflow + amt) abort();
                if (from != G.pool && A(G.pool)->debit != pb + amt) abort();
            }
        } else if (op == 2) { /* POOL_RECORD */
            uint8_t f = fz_u8(&in);
            uint64_t amt = fz_u32(&in);
            if (f & 1) { /* the raffle's own settlement deposits first */
                if (raw(raffle, -(int64_t) (amt & 0xFFFFFFF), G.pool, (int64_t) (amt & 0xFFFFFFF),
                        0) == PAY_OK) {
                    snap = G;
                    memcpy(asnap, L.acct, L.n_accounts * sizeof L.acct[0]);
                }
            }
            uint64_t seen = G.pool_seen, bal = A(G.pool)->debit;
            st = grant_pool_record(&G, amt);
            if (st == GRANT_OK && (bal < seen || bal - seen < amt)) abort();
        } else if (op == 3) { /* APPEAL */
            uint8_t f = fz_u8(&in);
            uint32_t subj = 200 + f % NP;
            uint64_t ep = (f & 0x10) ? fz_edge64(&in) : G.epoch + ((f >> 5) & 1);
            uint32_t mask = fz_u8(&in);
            uint8_t m[GRANT_APPEAL_MSG], sig[8];
            grant_appeal_msg(subj, ep, mask, m);
            mksig((f & 0xC0) == 0xC0 ? FUNNEL + 1 : FUNNEL, m, sizeof m, sig);
            st = grant_appeal(&G, subj, ep, mask, sig, 8);
            if (st == GRANT_OK) {
                if ((f & 0xC0) == 0xC0 || mask == 0 || (mask & ~GRANT_FAIL_ALL) || ep < snap.epoch)
                    abort();
                if (g_nap < 64) g_ap[g_nap++] = (appeal_m){subj, ep, mask};
            }
        } else if (op == 4) { /* DISTRIBUTE */
            grant_recipient_t r[GRANT_MAX_RECIP];
            uint32_t n = 1 + fz_u8(&in) % NP;
            uint8_t f = fz_u8(&in);
            uint32_t first = fz_u8(&in) % NP;
            for (uint32_t i = 0; i < n; i++) {
                uint32_t p = (first + i) % NP;
                metrics(&in, &r[i].m, 200 + p);
                r[i].account = brl[p];
                r[i].weight = (f & 1) ? fz_edge64(&in) : (uint64_t) 1 + fz_u8(&in) % 16;
            }
            if ((f & 0x0E) == 0x0E) r[n - 1].account = vin[0]; /* a non-barrel account */
            uint32_t lot = pick_lot(&in);
            uint64_t price = (f & 0x10) ? fz_edge64(&in) : (uint64_t) 1 + fz_u16(&in);
            uint64_t P = A(G.pool)->debit, rb = A(G.reserve)->debit;
            uint64_t bb[NP];
            for (uint32_t i = 0; i < NP; i++) bb[i] = A(brl[i])->debit;
            grant_dist_t d;
            st = grant_epoch_distribute(&G, lot, r, n, price, now, &d);
#ifndef GRANTS_STRICT_WEIGHT
            if (st == GRANT_ERR_EMPTY || st == GRANT_ERR_FULL || st == GRANT_ERR_OVERFLOW ||
                st == GRANT_ERR_LEDGER) {
                __uint128_t W = 0;
                for (uint32_t i = 0; i < n; i++) W += r[i].weight;
                if (st != GRANT_ERR_OVERFLOW || !(W >> 64)) {
                    if (G.hist[G.epoch & 7].weight != (uint64_t) W) abort(); /* exactly W */
                    for (uint32_t i = 0; i < n; i++)
                        if (!eligible(&r[i].m, snap.epoch)) abort();
                }
                weight_ok = true;
            }
#endif
            if (st == GRANT_OK || st == GRANT_ERR_LEDGER) {
                const grant_lot_t *lt = grant_lot(&G, lot);
                if (snap.hist[snap.epoch & 7].distributed || !lt || lt->attested) abort();
                for (uint32_t i = 0; i < n; i++)
                    if (!eligible(&r[i].m, snap.epoch)) abort(); /* G2 */
                __uint128_t W = 0;
                for (uint32_t i = 0; i < n; i++) W += r[i].weight;
                uint64_t budget = (uint64_t) ((__uint128_t) P * G.cap_num / G.cap_den);
                uint64_t B = budget / price;
                if (d.barrels != B || (__uint128_t) B * price > P) abort();
                uint64_t sh[GRANT_MAX_RECIP], rm[GRANT_MAX_RECIP], sum = 0;
                for (uint32_t i = 0; i < n; i++) {
                    __uint128_t x = (__uint128_t) B * r[i].weight;
                    sh[i] = (uint64_t) (x / W);
                    rm[i] = (uint64_t) (x % W);
                    sum += sh[i];
                }
                bool got[GRANT_MAX_RECIP] = {false};
                for (uint64_t left = B - sum; left; left--) {
                    int best = -1;
                    for (uint32_t i = 0; i < n; i++)
                        if (!got[i] && (best < 0 || rm[i] > rm[best])) best = (int) i;
                    got[best] = true;
                    sh[best]++;
                }
                uint64_t paid = 0;
                for (uint32_t i = 0; i < n; i++) {
                    if (d.share[i] != sh[i]) abort();
                    uint32_t p = (first + i) % NP;
                    uint64_t delta = A(brl[p])->debit - bb[p];
                    if (st == GRANT_OK && delta != sh[i]) abort();
                    paid += delta;
                }
                if (A(G.pool)->debit != P - paid * price) abort();
                if (A(G.reserve)->debit != rb + paid * price) abort();
                dcred = paid;
            }
        } else if (op == 5) { /* LOT_OPEN */
            st = grant_lot_open(&G, fz_u8(&in) % 24);
        } else if (op == 6) { /* UNDERWRITER_ADD */
            st = grant_underwriter_add(&G, 60 + fz_u8(&in) % 6);
        } else if (op == 7) { /* ATTEST */
            uint32_t lot = pick_lot(&in);
            const grant_lot_t *lt = grant_lot(&G, lot);
            uint8_t f = fz_u8(&in);
            uint64_t count = (f & 3) && lt ? lt->count : fz_edge64(&in);
            uint32_t uw = 60 + (((f >> 2) & 1) ? (f >> 3) % 8 : 0);
            uint64_t exp = (f & 0x20) ? fz_edge64(&in) : now + 1 + fz_u16(&in);
            uint8_t m[GRANT_ATTEST_MSG], sig[8];
            grant_attest_msg(lot, count, uw, exp, m);
            mksig((f & 0xC0) == 0xC0 ? uw + 1 : uw, m, sizeof m, sig);
            uint64_t old_exp = lt ? lt->expiry : 0;
            bool listed = false;
            for (uint32_t i = 0; i < G.n_underw; i++) listed |= G.underwriter[i] == uw;
            st = grant_attest(&G, lot, count, uw, exp, now, sig, 8);
            if (st == GRANT_OK) {
                if ((f & 0xC0) == 0xC0 || !listed || !lt || count != lt->count || !count ||
                    exp <= now || exp <= old_exp)
                    abort();
                if (!grant_backed(&G, lot, now)) abort();
            }
        } else if (op == 8) { /* DISPUTE */
            uint32_t lot = pick_lot(&in);
            const grant_lot_t *lt = grant_lot(&G, lot);
            uint8_t f = fz_u8(&in);
            uint8_t m[GRANT_DISPUTE_MSG], sig[8];
            grant_dispute_msg(lot, lt ? lt->expiry + ((f & 0x10) ? 1 : 0) : 0, m);
            mksig((f & 3) == 3 ? FUNNEL + 1 : FUNNEL, m, sizeof m, sig);
            st = grant_dispute(&G, lot, sig, 8);
            if (st == GRANT_OK) {
                if ((f & 3) == 3 || (f & 0x10) || !lt || !lt->attested) abort();
                if (grant_backed(&G, lot, now)) abort();
            }
        } else if (op == 9) { /* REDEEM */
            uint8_t f = fz_u8(&in);
            uint32_t lot = pick_lot(&in);
            uint32_t h = f % NP;
            for (uint32_t k = 0; k < NP; k++) /* prefer a holder of this lot */
                if (grant_holding(&G, brl[(f + k) % NP], lot)) {
                    h = (f + k) % NP;
                    break;
                }
            uint32_t hv = (f & 0x30) == 0x30 ? vin[(h + 1) % NP] : vin[h];
            if ((f & 0xC0) == 0xC0) hv = G.pool;
            uint64_t have = grant_holding(&G, brl[h], lot);
            uint64_t n = (fz_u8(&in) & 1) && have ? 1 + fz_u8(&in) % have : fz_edge64(&in);
            uint64_t price = (fz_u8(&in) & 1) ? (uint64_t) 1 + fz_u8(&in) : fz_edge64(&in);
            uint64_t t = (fz_u8(&in) & 1) ? now : fz_edge64(&in);
            const grant_lot_t *lt = grant_lot(&G, lot);
            bool backed = lt && lt->attested && !lt->disputed && t < lt->expiry;
            uint64_t vb = A(hv)->debit, rb = A(G.reserve)->debit;
            st = grant_redeem(&G, brl[h], hv, lot, n, price, t);
            if (st == GRANT_OK) {
                if (!backed || n > have || n == 0) abort();
#ifndef GRANTS_ALLOW_ANY_PAYEE
                if (A(hv)->owner != A(brl[h])->owner) abort(); /* G-AUTH */
#endif
                if (A(hv)->debit != vb + n * price || A(G.reserve)->debit != rb - n * price)
                    abort();
                if (grant_holding(&G, brl[h], lot) != have - n) abort();
                dcred = (uint64_t) -n; /* issuer credit falls by n */
            }
        } else if (op == 10) { /* SET_CAP / SET_THRESHOLDS */
            uint8_t f = fz_u8(&in);
            if (f & 1) {
                uint64_t a = small_or(fz_u8(&in)), b = small_or(fz_u8(&in));
                st = grant_set_cap(&G, a, b);
                if (st == GRANT_OK && (a == 0 || a > b)) abort();
            } else {
                grant_thresholds_t th = {9000 + fz_u16(&in) % 1200, fz_u16(&in), fz_u32(&in)};
                st = grant_set_thresholds(&G, &th);
                if (st == GRANT_OK && th.min_uptime_bps > 10000) abort();
            }
        } else { /* FUND the raffle account (explicit mint) */
            uint64_t amt = fz_edge64(&in) & (((uint64_t) 1 << 59) - 1);
            if (raw(raffle, (int64_t) amt, viss, 0, (int64_t) amt) == PAY_OK) minted = amt;
            st = GRANT_OK;
        }

        if (st == GRANT_OK) g_okc[op]++;
        if (getenv("FZ_TRACE")) fprintf(stderr, "op %u st %d\n", op, st);
        /* G-REF (a later-batch ledger refusal of a distribute is partial by design) */
        if (st != GRANT_OK && !(op == 4 && st == GRANT_ERR_LEDGER && dcred))
            if (!same_state(&snap, asnap, weight_ok)) abort();
        if (vsum() != s0 + minted) abort();                     /* G-CONS */
        if (A(G.issuer)->credit != icred0 + dcred) abort();     /* G-CONS */
#ifndef GRANTS_ALLOW_SELF_CREDIT
        if (G.pool_seen > A(G.pool)->debit) abort();            /* G-SEEN */
#endif
        if (!grant_check(&G)) abort();                          /* G1 */
        uint64_t rn, rd;
        bool def;
        if (grant_rate(&G, G.epoch, &rn, &rd, &def) != GRANT_OK) abort();
        if (def != (rd != 0)) abort();
    }
    return 0;
}

__attribute__((destructor)) static void report(void)
{
    if (getenv("FUZZ_PROP_STATS")) {
        fprintf(stderr, "fuzz_econ_grants: %llu ops checked\n", g_ops);
        for (int i = 0; i < 16; i++)
            if (g_okc[i]) fprintf(stderr, "  op %d ok %llu\n", i, g_okc[i]);
    }
}
