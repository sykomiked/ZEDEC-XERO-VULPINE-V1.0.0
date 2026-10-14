/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_econ_vderiv.c — state-machine property harness for V-Calls and
 * V-Perps (vderiv/vderiv.c) on a real pay_ledger.
 *
 * Every user u has ledger owner id 100+u and signing key 100+u (the key that
 * owns an account is its owner id, as in vbond). The verify hook accepts
 * sig == first 8 bytes of SHA-256(key || msg). Ops: ENABLE, PUSH (in-order or
 * random epoch, edge qty), CALL_OPEN, CALL_SETTLE, PERP_OPEN, PERP_CLOSE,
 * FUND. Opens draw keys either from the accounts' owners or from some other
 * user (an attacker who names a victim's account and signs with keys it
 * holds itself), honest or forged signatures, current or stale nonces.
 * Invariants after every op:
 *   D-CONS  the sum of every Vino account's DEBIT changes only by FUND
 *   D-REF   a refused op changes nothing (module modulo seq / last_ledger;
 *           every ledger account)
 *   D-AUTH  a successful open moves value out of an account only when that
 *           account's owner signed (key == account owner) — define
 *           VDERIV_ALLOW_UNBOUND_KEYS to skip this check
 *   V1      open: escrow +n*(C-K), buyer -premium, writer +premium-fee-lock,
 *           reserve +fee; settle: buyer +payoff-fee, writer +lock-payoff,
 *           payoff == n*(min(I,C)-K)^+ <= lock, escrow back to its other locks
 *   V2      push: each open perp moves exactly n*min(|dI|,band) loser ->
 *           winner less fee, stops iff a side < n*band; close pays both
 *           balances; only an open perp's own side key may close it
 *   V3      every fee == pay_assure_fee(amount) <= amount
 *   V4      vderiv_check and pay_ledger_check pass
 *   V5      opens only while enabled; push / settle / close always work
 *   NO USURY: balances never change between pushes (no funding, no accrual):
 *           a close at any tick pays exactly the recorded balances. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vderiv.h"
#include "../pay/pay_assure.h"
#include "../robin_debanks/sha256.h"
#include "fuzz_in.h"

#define JUR  0x00525444u
#define GATE 55u
#define NU   6u

static pay_ledger_t L;
static vderiv_t S;
static uint16_t vno;
static uint32_t viss, esc, escL, escS, rsv, u[NU], acc[32], n_acc;
static uint64_t nfund;
static unsigned long long g_ops, g_okc[16];

static void mksig(uint32_t key, const uint8_t *m, uint32_t n, uint8_t sig[8])
{
    uint8_t b[160], h[32];
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
    if (sl != 8 || n > 150) return false;
    mksig(key, m, n, want);
    return !memcmp(want, sig, 8);
}

static pay_status_t raw(uint32_t a, int64_t da, uint32_t b, int64_t cb)
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
    r.lines[1] = (pay_line_t){b, 0, cb};
    r.n_lines = 2;
    return pay_ledger_post(&L, &r, &rc);
}

static uint32_t opn(uint32_t owner, uint8_t flags)
{
    uint32_t a = 0;
    if (pay_ledger_open(&L, owner, vno, PAY_CAP_FINANCIAL, flags, &a) != PAY_OK) abort();
    acc[n_acc++] = a;
    return a;
}

static const pay_account_t *A(uint32_t a)
{
    return pay_ledger_account(&L, a);
}

static __uint128_t vsum(void)
{
    __uint128_t s = 0;
    for (uint32_t i = 0; i < L.n_accounts; i++) s += L.acct[i].debit;
    return s;
}

static void setup(void)
{
    pay_ledger_init(&L, NULL);
    n_acc = 0;
    nfund = 0;
    if (pay_ledger_add_asset(&L, "VNO", 2, PAY_ASSET_UNIT, &vno) != PAY_OK) abort();
    viss = opn(1, PAY_ACCT_ISSUER);
    esc = opn(2, 0);
    escL = opn(3, 0);
    escS = opn(4, 0);
    rsv = opn(5, 0);
    for (uint32_t i = 0; i < NU; i++) {
        u[i] = opn(100 + i, 0);
        if (raw(u[i], 1000000000, viss, 1000000000) != PAY_OK) abort();
    }
    vderiv_cfg_t c;
    vderiv_cfg_default(&c);
    c.vino = vno;
    c.escrow = esc;
    c.esc_long = escL;
    c.esc_short = escS;
    c.reserve = rsv;
    c.gateway = GATE;
    c.juris = JUR;
    c.first_epoch = 10;
    c.verify = verify;
    for (int i = 0; i < 32; i++) c.seed[i] = (uint8_t) (i * 5 + 3);
    if (vderiv_init(&S, &L, &c) != VDERIV_OK) abort();
    uint8_t m[VDERIV_SWITCH_MSG], sig[8];
    vderiv_switch_msg(JUR, true, 1, m);
    mksig(GATE, m, sizeof m, sig);
    if (vderiv_enable(&S, JUR, true, 1, sig, 8) != VDERIV_OK) abort();
}

static bool same_state(const vderiv_t *a, const pay_account_t *acct0)
{
    vderiv_t x = *a, y = S;
    x.seq = y.seq = 0;
    x.last_ledger = y.last_ledger = PAY_OK;
    return !memcmp(&x, &y, sizeof x) && !memcmp(acct0, L.acct, L.n_accounts * sizeof L.acct[0]);
}

/* a key: the account's owner, or (attack) another user's key */
static uint32_t pick_key(fz_in *in, uint32_t acct, bool *bound)
{
    uint8_t b = fz_u8(in);
    if (b < 192) {
        *bound = true;
        return A(acct)->owner;
    }
    *bound = (100u + b % NU) == A(acct)->owner;
    return 100u + b % NU;
}

static uint64_t small_or_edge(fz_in *in)
{
    uint8_t b = fz_u8(in);
    if (b < 160) return (uint64_t) (b % 40);
    return fz_edge64(in);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 4096) return 0;
    fz_in in;
    fz_init(&in, data, size);
    setup();
    static vderiv_t snap;
    static pay_account_t asnap[PAY_MAX_ACCOUNTS];
    uint64_t tick = 0;

    while (in.n) {
        uint8_t ob = fz_u8(&in), op = ob % 8u;
        if (op == 7 && ob < 224) op = 1; /* a freeze is rare */
        if (op == 6 && ob < 128) op = 3; /* so is a mint */
        if (op == 0 && ob < 200) op = 5; /* and a switch change */
        g_ops++;
        tick += fz_u8(&in);
        snap = S;
        memcpy(asnap, L.acct, L.n_accounts * sizeof L.acct[0]);
        __uint128_t s0 = vsum(), minted = 0;
        int st = 0;

        if (op == 0) { /* ENABLE */
            uint8_t f = fz_u8(&in);
            uint32_t j = (f & 1) ? JUR : JUR + 1;
            bool en = (f & 2) || (f & 0x80);
            uint64_t ep = (f & 4) ? S.switch_epoch + 1 : fz_edge64(&in);
            uint8_t m[VDERIV_SWITCH_MSG], sig[8];
            vderiv_switch_msg(j, en, ep, m);
            mksig((f & 8) ? GATE + 1 : GATE, m, sizeof m, sig);
            if (f & 16) sig[0] ^= 1;
            st = vderiv_enable(&S, j, en, ep, sig, 8);
            if (st == VDERIV_OK && ((f & (8 | 16)) || j != JUR || ep <= snap.switch_epoch)) abort();
        } else if (op == 1) { /* PUSH */
            uint8_t f = fz_u8(&in);
            uint64_t want = S.n_pushed ? S.last_epoch + 1 : S.cfg.first_epoch;
            uint64_t ep = (f & 7) ? want : fz_edge64(&in);
            uint64_t qty = small_or_edge(&in);
            /* model of every open perp's move */
            uint64_t nb[VDERIV_MAX_PERPS][2], fees = 0;
            bool stop[VDERIV_MAX_PERPS];
            for (uint32_t i = 0; i < VDERIV_MAX_PERPS; i++) {
                const vderiv_perp_t *p = &S.perp[i];
                if (p->state != VDERIV_OPEN) continue;
                uint64_t d = qty > p->ref ? qty - p->ref : p->ref - qty;
                if (d > p->t.band) d = p->t.band;
                __uint128_t v = (__uint128_t) p->t.size * d;
                if (v > p->bal[0] || v > p->bal[1]) abort(); /* collateral covers a move */
                uint64_t fee = pay_assure_fee((uint64_t) v);
                if (fee > v) abort();
                int win = qty > p->ref ? 0 : 1;
                nb[i][1 - win] = p->bal[1 - win] - (uint64_t) v;
                nb[i][win] = p->bal[win] + (uint64_t) v - fee;
                fees += fee;
                uint64_t fl = p->t.size * p->t.band;
                stop[i] = nb[i][0] < fl || nb[i][1] < fl;
            }
            uint64_t rb = A(rsv)->debit;
            vderiv_push_t out;
            st = vderiv_index_push(&S, ep, fz_u64(&in), qty, tick, &out);
            if (st == VDERIV_OK) {
                if (ep != want) abort();
                if (A(rsv)->debit != rb + fees || out.fees != fees) abort();
                for (uint32_t i = 0; i < VDERIV_MAX_PERPS; i++) {
                    if (snap.perp[i].state != VDERIV_OPEN) continue;
                    const vderiv_perp_t *p = &S.perp[i];
                    if (p->bal[0] != nb[i][0] || p->bal[1] != nb[i][1] || p->ref != qty) abort();
                    if ((p->state == VDERIV_STOPPED) != stop[i]) abort();
                }
                for (uint32_t i = 0; i < VDERIV_MAX_CALLS; i++)
                    if (snap.call[i].state == VDERIV_OPEN && snap.call[i].t.expiry == ep &&
                        (S.call[i].state != VDERIV_FIXED || S.call[i].fixing != qty))
                        abort();
                uint64_t ix;
                if (!vderiv_index_at(&S, ep, &ix) || ix != qty) abort();
            }
        } else if (op == 2) { /* CALL_OPEN */
            vderiv_call_terms_t t;
            memset(&t, 0, sizeof t);
            uint32_t w = fz_u8(&in) % NU, b = fz_u8(&in) % NU;
            t.writer = (fz_u8(&in) & 0x1F) ? u[w] : acc[fz_u8(&in) % n_acc];
            t.buyer = u[b];
            bool wb, bb;
            t.writer_key = pick_key(&in, t.writer, &wb);
            t.buyer_key = pick_key(&in, t.buyer, &bb);
            uint8_t f = fz_u8(&in);
            t.nonce = (f & 3) ? S.nonce : fz_edge64(&in);
            t.strike = small_or_edge(&in);
            t.cap = (f & 4) ? t.strike + 1 + fz_u8(&in) % 30 : small_or_edge(&in);
            t.size = small_or_edge(&in);
            uint64_t base = S.n_pushed ? S.last_epoch + 1 : S.cfg.first_epoch;
            t.expiry = (f & 8) ? base + fz_u8(&in) % 4 : fz_edge64(&in);
            t.premium = small_or_edge(&in);
            uint8_t m[VDERIV_CALL_MSG], ws[8], bs[8];
            vderiv_call_msg(&t, m);
            mksig(t.writer_key, m, sizeof m, ws);
            mksig(t.buyer_key, m, sizeof m, bs);
            uint8_t g = fz_u8(&in);
            if (g >= 240) ws[1] ^= 2;
            if (g < 16) bs[1] ^= 2;
            uint64_t wb0 = A(t.writer)->debit, bb0 = A(t.buyer)->debit, eb = A(esc)->debit,
                     rb = A(rsv)->debit;
            uint32_t id = 99;
            st = vderiv_call_open(&S, &t, ws, 8, bs, 8, tick, &id);
            if (st == VDERIV_OK) {
                if (!snap.enabled || g >= 240 || g < 16 || t.nonce != snap.nonce) abort();
                if (t.writer == t.buyer || t.writer_key == t.buyer_key) abort();
#ifndef VDERIV_ALLOW_UNBOUND_KEYS
                if (!wb || !bb) abort(); /* D-AUTH: a key moved another owner's Vino */
#endif
                uint64_t lock = t.size * (t.cap - t.strike), fee = pay_assure_fee(t.premium);
                if (fee > t.premium || S.call[id].lock != lock || S.call[id].state != VDERIV_OPEN)
                    abort();
                if (A(t.buyer)->debit != bb0 - t.premium) abort();
                if (A(t.writer)->debit != wb0 + (t.premium - fee) - lock) abort();
                if (A(esc)->debit != eb + lock || A(rsv)->debit != rb + fee) abort();
                if (S.nonce != snap.nonce + 1) abort();
            }
        } else if (op == 3) { /* CALL_SETTLE */
            uint32_t id = fz_u8(&in) % (VDERIV_MAX_CALLS + 2);
            vderiv_call_t c = id < VDERIV_MAX_CALLS ? S.call[id] : (vderiv_call_t){0};
            uint64_t bb0 = 0, wb0 = 0, eb = A(esc)->debit, rb = A(rsv)->debit;
            if (c.state) {
                bb0 = A(c.t.buyer)->debit;
                wb0 = A(c.t.writer)->debit;
            }
            vderiv_settle_t o;
            st = vderiv_call_settle(&S, id, tick, &o);
            if (st == VDERIV_OK) {
                if (c.state != VDERIV_FIXED) abort();
                uint64_t top = c.fixing < c.t.cap ? c.fixing : c.t.cap;
                uint64_t p = top > c.t.strike ? c.t.size * (top - c.t.strike) : 0;
                uint64_t fee = pay_assure_fee(p);
                if (p > c.lock || o.payoff != p || o.fee != fee) abort();
                if (A(c.t.buyer)->debit != bb0 + p - fee) abort();
                if (A(c.t.writer)->debit != wb0 + (c.lock - p)) abort();
                if (A(esc)->debit != eb - c.lock || A(rsv)->debit != rb + fee) abort();
                if (S.call[id].state != VDERIV_FREE) abort();
            }
        } else if (op == 4) { /* PERP_OPEN */
            vderiv_perp_terms_t t;
            memset(&t, 0, sizeof t);
            uint32_t l = fz_u8(&in) % NU, s = fz_u8(&in) % NU;
            t.long_acct = u[l];
            t.short_acct = (fz_u8(&in) & 0x1F) ? u[s] : acc[fz_u8(&in) % n_acc];
            bool lb, sbd;
            t.long_key = pick_key(&in, t.long_acct, &lb);
            t.short_key = pick_key(&in, t.short_acct, &sbd);
            uint8_t f = fz_u8(&in);
            t.nonce = (f & 3) ? S.nonce : fz_edge64(&in);
            t.size = small_or_edge(&in);
            t.band = small_or_edge(&in);
            uint64_t nbnd = t.size * t.band;
            t.lock_long = (f & 4) ? nbnd + fz_u8(&in) : small_or_edge(&in);
            t.lock_short = (f & 8) ? nbnd + fz_u8(&in) : small_or_edge(&in);
            uint8_t m[VDERIV_PERP_MSG], ls[8], ss[8];
            vderiv_perp_msg(&t, m);
            mksig(t.long_key, m, sizeof m, ls);
            mksig(t.short_key, m, sizeof m, ss);
            uint8_t g = fz_u8(&in);
            if (g >= 240) ls[2] ^= 4;
            if (g < 16) ss[2] ^= 4;
            uint64_t a0 = A(t.long_acct)->debit, b0 = A(t.short_acct)->debit;
            uint32_t id = 99;
            st = vderiv_perp_open(&S, &t, ls, 8, ss, 8, tick, &id);
            if (st == VDERIV_OK) {
                if (!snap.enabled || g >= 240 || g < 16 || t.nonce != snap.nonce || !snap.n_pushed)
                    abort();
#ifndef VDERIV_ALLOW_UNBOUND_KEYS
                if (!lb || !sbd) abort(); /* D-AUTH */
#endif
                __uint128_t nb128 = (__uint128_t) t.size * t.band;
                if (t.size == 0 || t.band == 0 || nb128 > t.lock_long || nb128 > t.lock_short)
                    abort();
                if (A(t.long_acct)->debit != a0 - t.lock_long) abort();
                if (A(t.short_acct)->debit != b0 - t.lock_short) abort();
                const vderiv_perp_t *p = &S.perp[id];
                if (p->state != VDERIV_OPEN || p->bal[0] != t.lock_long ||
                    p->bal[1] != t.lock_short)
                    abort();
            }
        } else if (op == 5) { /* PERP_CLOSE */
            uint32_t id = fz_u8(&in) % (VDERIV_MAX_PERPS + 2);
            uint8_t f = fz_u8(&in);
            uint8_t side = (uint8_t) (f % 3);
            vderiv_perp_t p = id < VDERIV_MAX_PERPS ? S.perp[id] : (vderiv_perp_t){0};
            uint32_t key = side == 0 ? p.t.long_key : p.t.short_key;
            if (f & 8) key = 100 + (f >> 4) % NU;
            uint8_t m[VDERIV_CLOSE_MSG], sig[8];
            vderiv_close_msg((f & 4) ? p.t.nonce + 1 : p.t.nonce, side, m);
            mksig(key, m, sizeof m, sig);
            uint64_t a0 = p.state ? A(p.t.long_acct)->debit : 0;
            uint64_t b0 = p.state ? A(p.t.short_acct)->debit : 0;
            uint64_t out[2] = {0, 0};
            st = vderiv_perp_close(&S, id, side, sig, 8, tick + fz_edge64(&in), out);
            if (st == VDERIV_OK) {
                if (p.state == VDERIV_FREE || side > 1) abort();
                bool honest = !(f & 4) && (!(f & 8) || key == (side == 0 ? p.t.long_key
                                                                          : p.t.short_key));
                if (p.state == VDERIV_OPEN && !honest) abort(); /* V2: own side's key only */
                if (out[0] != p.bal[0] || out[1] != p.bal[1]) abort(); /* no accrual */
                if (A(p.t.long_acct)->debit != a0 + p.bal[0]) abort();
                if (A(p.t.short_acct)->debit != b0 + p.bal[1]) abort();
                if (S.perp[id].state != VDERIV_FREE) abort();
            }
        } else if (op == 6) { /* FUND (explicit mint) */
            uint32_t a = u[fz_u8(&in) % NU];
            uint64_t amt = fz_edge64(&in) & (((uint64_t) 1 << 59) - 1);
            if (raw(a, (int64_t) amt, viss, (int64_t) amt) == PAY_OK) minted = amt;
            st = VDERIV_OK;
        } else { /* freeze / unfreeze a user account */
            uint32_t a = u[fz_u8(&in) % NU];
            (void) pay_ledger_set_flags(&L, a, A(a)->flags ^ PAY_ACCT_FROZEN);
            st = VDERIV_OK;
        }

        if (st == 0) g_okc[op]++;
        if (getenv("FZ_TRACE")) fprintf(stderr, "op %u st %d\n", op, st);
        if (st != VDERIV_OK && !same_state(&snap, asnap)) abort(); /* D-REF */
        if (vsum() != s0 + minted) abort();                         /* D-CONS */
        if (vderiv_check(&S) != VDERIV_OK) abort();                 /* V4 */
        if (!pay_ledger_check(&L)) abort();
    }
    return 0;
}

__attribute__((destructor)) static void report(void)
{
    if (getenv("FUZZ_PROP_STATS")) fprintf(stderr, "fuzz_econ_vderiv: %llu ops checked\n", g_ops);
    if (getenv("FUZZ_PROP_STATS"))
        for (int i = 0; i < 16; i++)
            if (g_okc[i]) fprintf(stderr, "  op %d ok %llu\n", i, g_okc[i]);
}
