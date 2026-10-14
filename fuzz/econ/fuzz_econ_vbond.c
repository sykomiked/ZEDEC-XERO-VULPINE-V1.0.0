/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_econ_vbond.c — state-machine property harness for V-Bonds
 * (vbond/vbond.c) on a real pay_ledger.
 *
 * The verify hook accepts sig == first 8 bytes of SHA-256(key || msg), so the
 * harness can sign honestly, forge (flip a bit) or sign with the wrong key,
 * and every signed message is checked to be bound to its signer and fields.
 * Ops: ENABLE (right/wrong jurisdiction, any epoch), ISSUE (any owner, any
 * account, edge qty, allowed or random k, edge tick), TRANSFER (owner or
 * not, honest / forged / stale-nonce signature), REDEEM, PREMIUM_CREDIT,
 * POOL_CREDIT, DISTRIBUTE, FUND (an explicit mint from the issuers).
 * Invariants after every op:
 *   B-CONS  per asset, the sum of every account's DEBIT changes
 *           only by FUND (bond ops move value, never create or destroy it)
 *   B-REF   a refused op changes nothing (module state modulo seq /
 *           last_ledger, and the ledger's accounts); a refused distribute
 *           may only zero last_paid
 *   B1      issue: lock +qty, payer -qty, maturity = tick + F(k), switch on,
 *           accounts are the owner's, serials strictly increase
 *   B2      transfer: only the current owner with an honest signature over
 *           the current nonce; nonce +1, ledger untouched
 *   B3      redeem: only the owner, only at tick >= maturity, lock -qty
 *   B4      premium == floor((price-floor)*qty*num/den) (exact 192-bit
 *           model); distribute pays the whole pool, each payee exactly the
 *           largest-remainder share of qty*weight, sum == P
 *   B5      vbond_check and pay_ledger_check pass
 *   NO USURY: no op ever pays more than the realised pool; nothing grows
 *           with the tick (a distribute at any tick pays exactly P). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vbond.h"
#include "../robin_debanks/sha256.h"
#include "fuzz_in.h"

#define JUR  0x0050534Eu
#define GATE 77u
#define NU   6u

static pay_ledger_t L;
static vbond_t S;
static uint16_t vno, vbl;
static uint32_t viss, biss, lock_a, pool_a, pool2_a, src_a;
static uint32_t acc[64], n_acc, uv[NU + 1], ub[NU + 1];
static uint64_t nfund;
static unsigned long long g_ops, g_okc[16];

static void mksig(uint32_t key, const uint8_t *m, uint32_t n, uint8_t sig[8])
{
    uint8_t b[128], h[32];
    memcpy(b, &key, 4);
    memcpy(b + 4, m, n);
    sha256(b, 4 + n, h);
    memcpy(sig, h, 8);
}

static bool verify(void *ctx, uint32_t key, const uint8_t *m, uint32_t n, const uint8_t *sig,
                   uint32_t sl)
{
    (void) ctx;
    uint8_t want[8];
    if (sl != 8 || n > 120) return false;
    mksig(key, m, n, want);
    return !memcmp(want, sig, 8);
}

static pay_status_t raw(uint32_t a, int64_t da, int64_t ca, uint32_t b, int64_t db, int64_t cb)
{
    pay_posting_req_t r;
    pay_receipt_t rc;
    memset(&r, 0, sizeof r);
    nfund++;
    snprintf(r.uetr, sizeof r.uetr, "00000000-0000-4000-8000-%012llx", (unsigned long long) nfund);
    snprintf(r.e2e, sizeof r.e2e, "FUND-%llu", (unsigned long long) nfund);
    memcpy(r.idem_key, &nfund, sizeof nfund);
    r.idem_key[31] = 0xEE;
    r.lines[0] = (pay_line_t){a, da, ca};
    r.lines[1] = (pay_line_t){b, db, cb};
    r.n_lines = 2;
    return pay_ledger_post(&L, &r, &rc);
}

static uint32_t opn(uint16_t as, uint32_t owner, uint8_t flags)
{
    uint32_t a = 0;
    if (pay_ledger_open(&L, owner, as, PAY_CAP_FINANCIAL, flags, &a) != PAY_OK) abort();
    acc[n_acc++] = a;
    return a;
}

static const pay_account_t *A(uint32_t a)
{
    return pay_ledger_account(&L, a);
}

static __uint128_t asset_sum(uint16_t as)
{
    __uint128_t s = 0;
    for (uint32_t i = 0; i < L.n_accounts; i++)
        if (L.acct[i].active && L.acct[i].asset == as) s += L.acct[i].debit;
    return s;
}

static void setup(void)
{
    pay_ledger_init(&L, NULL);
    n_acc = 0;
    nfund = 0;
    if (pay_ledger_add_asset(&L, "VNO", 2, PAY_ASSET_UNIT, &vno) != PAY_OK) abort();
    if (pay_ledger_add_asset(&L, "VBL", 0, PAY_ASSET_UNIT, &vbl) != PAY_OK) abort();
    viss = opn(vno, 1, PAY_ACCT_ISSUER);
    biss = opn(vbl, 5, PAY_ACCT_ISSUER);
    lock_a = opn(vbl, 7, 0);
    pool_a = opn(vno, 3, 0);
    pool2_a = opn(vno, 3, 0);
    src_a = opn(vno, 4, 0);
    for (uint32_t i = 0; i < NU; i++) {
        uv[i] = opn(vno, 100 + i, 0);
        ub[i] = opn(vbl, 100 + i, 0);
        if (raw(ub[i], 1000000, 0, biss, 0, 1000000) != PAY_OK) abort();
    }
    ub[NU] = opn(vbl, 100, 0); /* a second V-Bill account of user 0 */
    uv[NU] = opn(vno, 101, 0); /* a second Vino account of user 1 */
    if (raw(src_a, 1000000000, 0, viss, 0, 1000000000) != PAY_OK) abort();

    vbond_cfg_t c;
    vbond_cfg_default(&c);
    c.vino = vno;
    c.vbill = vbl;
    c.lock = lock_a;
    c.gateway = GATE;
    c.juris = JUR;
    c.verify = verify;
    for (int i = 0; i < 32; i++) c.seed[i] = (uint8_t) (i * 7 + 1);
    if (vbond_init(&S, &L, &c) != VBOND_OK) abort();
    uint8_t m[VBOND_SWITCH_MSG], sig[8];
    vbond_switch_msg(JUR, true, 1, m);
    mksig(GATE, m, sizeof m, sig);
    if (vbond_enable(&S, JUR, true, 1, sig, 8) != VBOND_OK) abort();
}

/* mostly the owner's own account of the asset, sometimes any account */
static uint32_t own_acct(fz_in *in, uint32_t owner, bool vino)
{
    uint8_t b = fz_u8(in);
    if (b >= 224) return acc[b % n_acc];
    uint32_t i = owner - 100;
    if (b & 1) i = NU; /* the second account (another owner's for one asset) */
    return vino ? uv[i] : ub[i];
}

/* module state with the per-call counters normalised */
static void norm(vbond_t *x)
{
    x->seq = 0;
    x->last_ledger = PAY_OK;
}

static bool same_state(const vbond_t *a, const pay_account_t *acct0, bool allow_lastpaid_zero)
{
    vbond_t x = *a, y = S;
    norm(&x);
    norm(&y);
    if (allow_lastpaid_zero)
        for (uint32_t i = 0; i < VBOND_MAX_BONDS; i++) {
            if (y.bond[i].last_paid != 0 && y.bond[i].last_paid != x.bond[i].last_paid)
                return false;
            x.bond[i].last_paid = y.bond[i].last_paid;
        }
    if (memcmp(&x, &y, sizeof x)) return false;
    return !memcmp(acct0, L.acct, L.n_accounts * sizeof L.acct[0]);
}

static uint32_t pick_acct(fz_in *in)
{
    return acc[fz_u8(in) % n_acc];
}

/* a live bond's serial (or a random one) */
static uint64_t pick_serial(fz_in *in)
{
    uint8_t b = fz_u8(in);
    if (b & 0x80u) return fz_edge64(in);
    for (uint32_t k = 0; k < VBOND_MAX_BONDS; k++) {
        uint32_t i = (b + k) % VBOND_MAX_BONDS;
        if (S.bond[i].used) return S.bond[i].serial;
    }
    return b;
}

/* B4 model: floor(diff * qty * num / den), OVERFLOW past 2^64 or LINE_MAX */
static int premium_model(uint64_t price, uint64_t fl, uint64_t qty, uint64_t *amt)
{
    *amt = 0;
    if (price < fl) return VBOND_OK;
    __uint128_t q = (__uint128_t) (price - fl) * qty;
    __uint128_t hi = q / S.cfg.share_den, lo = q % S.cfg.share_den;
    if (hi >> 64) return VBOND_ERR_OVERFLOW;
    __uint128_t a = hi * S.cfg.share_num + lo * S.cfg.share_num / S.cfg.share_den;
    if (a > VBOND_LINE_MAX) return VBOND_ERR_OVERFLOW;
    *amt = (uint64_t) a;
    return VBOND_OK;
}

static uint64_t last_serial;
static uint64_t now;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 4096) return 0;
    fz_in in;
    fz_init(&in, data, size);
    setup();
    last_serial = 0;
    now = 0;
    static vbond_t snap;
    static pay_account_t asnap[PAY_MAX_ACCOUNTS];

    while (in.n) {
        uint8_t ob = fz_u8(&in), op = ob % 9u;
        if (op == 8 && ob < 224) op = 1; /* a freeze is rare */
        g_ops++;
        now += fz_u16(&in);
        snap = S;
        memcpy(asnap, L.acct, L.n_accounts * sizeof L.acct[0]);
        __uint128_t sv = asset_sum(vno), sb = asset_sum(vbl);
        __uint128_t minted_v = 0, minted_b = 0;
        int st = 0;
        bool lastpaid_ok = false;

        if (op == 0) { /* ENABLE */
            uint8_t f = fz_u8(&in);
            uint32_t j = (f & 1) ? JUR : JUR ^ 1u;
            bool en = (f & 2) != 0;
            uint64_t ep = (f & 4) ? S.switch_epoch + 1 : fz_edge64(&in);
            uint8_t m[VBOND_SWITCH_MSG], sig[8];
            vbond_switch_msg(j, en, ep, m);
            mksig((f & 8) ? GATE + 1 : GATE, m, sizeof m, sig);
            if (f & 16) sig[f % 8] ^= 1;
            st = vbond_enable(&S, j, en, ep, sig, 8);
            bool honest = !(f & 8) && !(f & 16);
            if (st == VBOND_OK) {
                if (!honest || j != JUR || ep <= snap.switch_epoch) abort();
                if (S.enabled != en || S.switch_epoch != ep) abort();
            }
        } else if (op == 1) { /* ISSUE */
            uint32_t owner = 100 + fz_u8(&in) % (NU + 1);
            if (owner > 100 + NU - 1) owner = 100;
            uint32_t va = own_acct(&in, owner, false), pa = own_acct(&in, owner, true);
            uint64_t qty = (fz_u8(&in) & 1) ? (uint64_t) 1 + fz_u16(&in) : fz_edge64(&in);
            uint8_t kb = fz_u8(&in);
            uint32_t k = kb < 200 ? (uint32_t[]){13, 17, 21}[kb % 3] : kb % 100;
            uint64_t tick = (fz_u8(&in) & 1) ? now : fz_edge64(&in);
            uint64_t serial = 0;
            uint64_t vbefore = A(va)->debit, lbefore = A(lock_a)->debit;
            st = vbond_issue(&S, owner, va, pa, qty, k, tick, &serial);
            if (st == VBOND_OK) {
                const vbond_bond_t *b = vbond_get(&S, serial);
                if (!snap.enabled || !b) abort();
                if (A(va)->owner != owner || A(va)->asset != vbl || va == lock_a) abort();
                if (A(pa)->owner != owner || A(pa)->asset != vno) abort();
                if (qty == 0 || qty > VBOND_LINE_MAX) abort();
                if (A(va)->debit != vbefore - qty || A(lock_a)->debit != lbefore + qty) abort();
                if (b->maturity != tick + vbond_fib(k) || b->maturity < tick) abort();
                if (b->qty != qty || b->owner != owner || b->payee != pa || b->nonce) abort();
                if (serial <= last_serial) abort(); /* serials never reused */
                last_serial = serial;
            }
        } else if (op == 2) { /* TRANSFER */
            uint64_t serial = pick_serial(&in);
            const vbond_bond_t *b0 = vbond_get(&S, serial);
            vbond_bond_t before = {0};
            if (b0) before = *b0;
            uint8_t f = fz_u8(&in);
            uint32_t from = (f & 1) && b0 ? b0->owner : 100 + fz_u8(&in) % (NU + 1);
            uint32_t to = 100 + fz_u8(&in) % NU;
            uint32_t tp = own_acct(&in, to, true);
            uint64_t nonce = b0 ? b0->nonce : 0;
            if (f & 2) nonce += 1 + (f >> 6); /* stale / future nonce */
            uint32_t signer = (f & 4) ? from + 1 : from;
            uint8_t m[VBOND_TX_MSG], sig[8];
            vbond_transfer_msg((f & 8) ? JUR ^ 2u : JUR, serial, to, tp, nonce, m);
            mksig(signer, m, sizeof m, sig);
            if (f & 16) sig[3] ^= 0x40;
            st = vbond_transfer(&S, serial, from, to, tp, sig, 8);
            if (st == VBOND_OK) {
                const vbond_bond_t *b = vbond_get(&S, serial);
                if (!b0 || !b || !snap.enabled) abort();
                if (before.owner != from) abort();               /* B2: only the owner */
                if (f & (2 | 4 | 8 | 16)) abort();               /* B2: honest sig only */
                if (A(tp)->owner != to || A(tp)->asset != vno) abort();
                if (b->owner != to || b->payee != tp || b->nonce != before.nonce + 1) abort();
                if (b->qty != before.qty || b->maturity != before.maturity) abort();
                if (memcmp(asnap, L.acct, L.n_accounts * sizeof L.acct[0])) abort(); /* no ledger move */
            }
        } else if (op == 3) { /* REDEEM */
            uint64_t serial = pick_serial(&in);
            const vbond_bond_t *b0 = vbond_get(&S, serial);
            vbond_bond_t before = {0};
            if (b0) before = *b0;
            uint8_t f = fz_u8(&in);
            uint32_t owner = (f & 1) && b0 ? b0->owner : 100 + fz_u8(&in) % NU;
            uint32_t va = own_acct(&in, owner >= 100 && owner < 100 + NU ? owner : 100, false);
            uint64_t tick = (f & 2) && b0 ? b0->maturity - ((f >> 2) & 1)
                            : (f & 4) ? now + 20000 : now + fz_edge64(&in);
            uint64_t vb = A(va)->debit, lb = A(lock_a)->debit;
            st = vbond_redeem(&S, serial, owner, va, tick);
            if (st == VBOND_OK) {
                if (!b0 || before.owner != owner || tick < before.maturity) abort();
                if (A(va)->owner != owner || A(va)->asset != vbl) abort();
                if (A(va)->debit != vb + before.qty || A(lock_a)->debit != lb - before.qty) abort();
                if (vbond_get(&S, serial)) abort();
            }
        } else if (op == 4 || op == 5) { /* PREMIUM_CREDIT / POOL_CREDIT */
            uint8_t pf = fz_u8(&in);
            uint32_t sa = (pf & 2) ? src_a : pick_acct(&in);
            uint32_t pa = (pf & 1) ? pool_a : (pf & 4) ? pool2_a : pick_acct(&in);
            uint64_t pb = A(pa)->debit;
            uint64_t amt = 0, want = 0;
            if (op == 4) {
                uint64_t price = fz_edge64(&in), fl = fz_edge64(&in), qty = fz_edge64(&in);
                int ms = premium_model(price, fl, qty, &want);
                uint64_t pm = 0;
                int ps = vbond_premium(&S, price, fl, qty, &pm);
                if (ps != ms || pm != want) abort(); /* B4 exact */
                st = vbond_premium_credit(&S, sa, pa, price, fl, qty, now, &amt);
                if (st == VBOND_OK && amt != (snap.enabled ? want : 0)) abort();
            } else {
                want = fz_edge64(&in);
                st = vbond_pool_credit(&S, sa, pa, want, now);
                amt = st == VBOND_OK ? want : 0;
            }
            if (st == VBOND_OK && amt) {
                if (!snap.enabled || sa == pa) abort();
                if (A(pa)->debit != pb + amt) abort();
            }
        } else if (op == 6) { /* DISTRIBUTE */
            uint8_t pf = fz_u8(&in);
            uint32_t pa = (pf & 1) ? pool_a : (pf & 2) ? pool2_a : pick_acct(&in);
            uint64_t tick = (pf & 4) ? fz_edge64(&in) : now;
            uint64_t P = A(pa)->debit;
            /* model: largest remainder over qty * weight of live bonds */
            uint64_t u[VBOND_MAX_BONDS], sh[VBOND_MAX_BONDS], rm[VBOND_MAX_BONDS];
            __uint128_t U = 0;
            for (uint32_t i = 0; i < VBOND_MAX_BONDS; i++) {
                const vbond_bond_t *b = &S.bond[i];
                __uint128_t x = (b->used && b->start <= tick)
                                    ? (__uint128_t) b->qty * S.cfg.weight[b->term]
                                    : 0;
                u[i] = (uint64_t) x;
                U += x;
            }
            uint64_t before[PAY_MAX_ACCOUNTS];
            for (uint32_t i = 0; i < L.n_accounts; i++) before[i] = L.acct[i].debit;
            vbond_dist_t d;
            st = vbond_distribute(&S, pa, tick, &d);
            lastpaid_ok = true;
            if (st == VBOND_OK) {
                if (U == 0 || (U >> 64) || P == 0) abort();
                uint64_t sum = 0;
                for (uint32_t i = 0; i < VBOND_MAX_BONDS; i++) {
                    __uint128_t x = (__uint128_t) P * u[i];
                    sh[i] = (uint64_t) (x / U);
                    rm[i] = (uint64_t) (x % U);
                    sum += sh[i];
                }
                bool got[VBOND_MAX_BONDS] = {false};
                for (uint64_t left = P - sum; left; left--) {
                    int best = -1;
                    for (uint32_t i = 0; i < VBOND_MAX_BONDS; i++)
                        if (!got[i] && (best < 0 || rm[i] > rm[best])) best = (int) i;
                    if (best < 0 || u[best] == 0) abort(); /* a zero-unit bond won a unit */
                    got[best] = true;
                    sh[best]++;
                }
                uint64_t exp[PAY_MAX_ACCOUNTS];
                memcpy(exp, before, sizeof exp);
                exp[pa] -= P;
                for (uint32_t i = 0; i < VBOND_MAX_BONDS; i++) {
                    if (S.bond[i].last_paid != sh[i]) abort();
                    if (sh[i]) exp[S.bond[i].payee] += sh[i];
                }
                for (uint32_t i = 0; i < L.n_accounts; i++)
                    if (L.acct[i].debit != exp[i]) abort(); /* B4: exactly the model */
                if (d.paid != P || A(pa)->debit != 0 || d.pool != P) abort();
            } else if (st == VBOND_ERR_PARTIAL) {
                /* earlier batches stand: the pool paid exactly what last_paid says */
                uint64_t lp = 0;
                for (uint32_t i = 0; i < VBOND_MAX_BONDS; i++) lp += S.bond[i].last_paid;
                if (lp != d.paid || A(pa)->debit != P - d.paid) abort();
            }
        } else if (op == 7) { /* FUND (explicit mint; never the lock account) */
            uint32_t a = pick_acct(&in);
            if (a == lock_a) a = src_a;
            uint64_t amt = fz_edge64(&in) & (((uint64_t) 1 << 59) - 1);
            const pay_account_t *x = A(a);
            uint32_t iss = x->asset == vno ? viss : biss;
            if (raw(a, (int64_t) amt, 0, iss, 0, (int64_t) amt) == PAY_OK) {
                if (x->asset == vno)
                    minted_v = amt;
                else
                    minted_b = amt;
            }
            st = VBOND_OK;
        } else { /* freeze/unfreeze a random account (compliance hold) */
            uint32_t a = pick_acct(&in);
            uint8_t fl = A(a)->flags ^ PAY_ACCT_FROZEN;
            (void) pay_ledger_set_flags(&L, a, fl);
            st = VBOND_OK;
            snap = S;
            memcpy(asnap, L.acct, L.n_accounts * sizeof L.acct[0]);
        }

        if (st == 0) g_okc[op]++;
        if (getenv("FZ_TRACE")) fprintf(stderr, "op %u st %d\n", op, st);
        /* B-REF */
        if (st != VBOND_OK && op != 7 && !(op == 6 && st == VBOND_ERR_PARTIAL))
            if (!same_state(&snap, asnap, lastpaid_ok)) abort();
        /* B-CONS */
        if (asset_sum(vno) != sv + minted_v) abort();
        if (asset_sum(vbl) != sb + minted_b) abort();
        /* B5 */
        if (vbond_check(&S) != VBOND_OK) abort();
        if (!pay_ledger_check(&L)) abort();
    }
    return 0;
}

__attribute__((destructor)) static void report(void)
{
    if (getenv("FUZZ_PROP_STATS")) fprintf(stderr, "fuzz_econ_vbond: %llu ops checked\n", g_ops);
    if (getenv("FUZZ_PROP_STATS"))
        for (int i = 0; i < 16; i++)
            if (g_okc[i]) fprintf(stderr, "  op %d ok %llu\n", i, g_okc[i]);
}
