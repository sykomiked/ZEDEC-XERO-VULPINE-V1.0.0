/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_econ_vswap.c — state-machine property harness for the Player Card
 * swap (vswap/vswap.c): the signed jurisdiction switch, swap in / swap out,
 * lock tiers with exit notice, and the weighted dividend, on a real
 * pay_ledger.
 *
 * A fresh ledger per input: Vino (VNO) and V-Bills (VBL); a Vino issuer, the
 * reserve, a dividend pool, a loss sink, four users (Vino + V-Bill account
 * each), the V-Bill issuer, a second V-Bill issuer, the vault and the two
 * lock accounts. The first input bytes pick the configuration (vault or
 * none, par, weights, Fibonacci indices). The signature hook accepts a
 * signature iff it is the 8-byte FNV-1a of (key, msg), so the harness makes
 * both valid and forged manifests. Ops: ENABLE, SWAP_IN, SWAP_OUT, LOCK,
 * NOTICE, UNLOCK, DIVIDEND, and outside postings (Vino and V-Bill issuance
 * to any account, reserve loss, a vault purchase), with edge-biased amounts
 * and any account id (including invalid ones).
 * Invariants after every op:
 *   V1  pay_ledger_check and vswap_epoch_check pass (T4: lock account ==
 *       sum of its positions)
 *   V2  a refused op changes no balance, no position and no switch state
 *   V3  conservation: total Vino moves only by outside issuance; N (issuer
 *       CREDIT) moves only by +v (swap in), -v (swap out), +out.vbills
 *       (reinvest) and outside V-Bill issuance
 *   V4  W1: while the switch is off, swap in / lock / dividend return
 *       DISABLED; swap out / notice / unlock are never DISABLED. Only a
 *       manifest signed by the gateway, for this jurisdiction, with a higher
 *       epoch changes the switch
 *   V5  W3: v = floor(a*rd/rn), a' = ceil(v*rn/rd) <= a, and the ratio never
 *       falls: a' * B >= v * R (header claim, with R, B before the swap);
 *       equivalently a buyer can never redeem straight back for more than
 *       a' (no free money)
 *   V6  W4: o = floor(v*R/B) and (R - o) * B >= R * (B - v)
 *   V7  T3: unlock only after notice and tick >= notice + period[tier];
 *       wrong owner refused; every locked V-Bill returns to the owner
 *   VA  owner binding: swap in / swap out / lock with the two accounts of
 *       different ledger owners is VSWAP_ERR_AUTH, and a success always had
 *       one owner on both sides
 *   V8  W5: on success the pool is emptied exactly: paid + reinvest == P;
 *       each holder's paid[i] equals the largest-remainder share computed
 *       independently; reinvesting positions get floor(share*rd/rn) V-Bills
 *       and are charged ceil(v*rn/rd), the rest paid to the payee
 *   V9  determinism: the same input run twice ends in the same ledger and
 *       swap state
 *   V10 a dividend with well-formed arguments (the pool, valid distinct
 *       holders) is never refused as ARG: no outsider's position can block
 *       it (a position paying into the pool itself gets weight 0) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vswap.h"
#include "fuzz_in.h"

static pay_ledger_t g_L;
static vswap_t g_S;
static vswap_cfg_t g_C;
static unsigned long long g_ops;
static unsigned long long g_hit[32]; /* ops that succeeded, per op code (FUZZ_PROP_STATS) */

#define NU    4
#define JURIS 0x4E5350u
#define GATE  77u

enum {
    A_VISS, A_RES, A_POOL, A_LOSS, A_UV0, A_UV1, A_UV2, A_UV3,
    A_BISS, A_BISS2, A_VAULT, A_LOCKM, A_LOCKD, A_UB0, A_UB1, A_UB2, A_UB3, NA
};
static uint32_t g_acc[NA];
static uint16_t g_vno, g_vbl;
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
    rnd[15] = 0xA5;
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
    if (b >= 0xF8u) return b == 0xFFu ? VSWAP_NONE : (uint32_t) PAY_MAX_ACCOUNTS + b;
    return g_acc[b % NA];
}
static uint32_t pick_uv(fz_in *in)
{
    uint8_t b = fz_u8(in);
    if ((b & 0xF0u) == 0xB0u) return g_acc[A_POOL]; /* the dividend pool as a party */
    return (b & 0xC0u) == 0xC0u ? pick(in) : g_acc[A_UV0 + b % NU];
}
static uint32_t pick_ub(fz_in *in)
{
    uint8_t b = fz_u8(in);
    return (b & 0xC0u) == 0xC0u ? pick(in) : g_acc[A_UB0 + b % NU];
}
/* the second account of a party: mostly the same user's (b & 0x20 clear),
 * else as pick_ub / pick_uv would choose */
static uint32_t user_of(uint32_t a, uint32_t base)
{
    for (uint32_t k = 0; k < NU; k++)
        if (a == g_acc[base + k]) return k;
    return NU;
}
static uint32_t pair_ub(fz_in *in, uint32_t uv)
{
    uint8_t b = fz_u8(in);
    uint32_t k = user_of(uv, A_UV0);
    if ((b & 0xC0u) == 0xC0u) return pick(in);
    return g_acc[A_UB0 + (!(b & 0x20u) && k < NU ? k : b % NU)];
}
static uint32_t pair_uv(fz_in *in, uint32_t ub)
{
    uint8_t b = fz_u8(in);
    uint32_t k = user_of(ub, A_UB0);
    if ((b & 0xF0u) == 0xB0u) return g_acc[A_POOL];
    if ((b & 0xC0u) == 0xC0u) return pick(in);
    return g_acc[A_UV0 + (!(b & 0x20u) && k < NU ? k : b % NU)];
}
/* VA: AUTH exactly when both are live accounts of different owners and the
 * call got that far; a success always had one owner */
static void owner_bound(uint32_t x, uint32_t y, vswap_status_t st)
{
    const pay_account_t *a = pay_ledger_account(&g_L, x), *b = pay_ledger_account(&g_L, y);
    bool differ = a && b && a->owner != b->owner;
    if ((st == VSWAP_ERR_AUTH && !differ) || (st == VSWAP_OK && differ)) abort();
}

/* amounts: mostly small (so the ops succeed), sometimes edge values */
static uint64_t amt(fz_in *in)
{
    uint8_t s = fz_u8(in);
    if (s < 0xA0u) return (uint64_t) fz_u16(in) % 5000u + (s & 1u);
    return fz_edge64(in);
}

static uint64_t debit(uint32_t a)
{
    const pay_account_t *x = pay_ledger_account(&g_L, a);
    return x ? x->debit : 0;
}
static uint64_t N(void)
{
    return pay_ledger_account(&g_L, g_acc[A_BISS])->credit;
}
static uint64_t vno_supply(void)
{
    return pay_ledger_account(&g_L, g_acc[A_VISS])->credit;
}

typedef struct {
    uint64_t d[NA], c[NA];
    uint64_t lseq;
    vswap_pos_t pos[VSWAP_MAX_POS];
    bool enabled;
    uint64_t sw_epoch;
} snap_t;
static snap_t g_b, g_a;

static void snap(snap_t *s)
{
    for (uint32_t i = 0; i < NA; i++) {
        s->d[i] = g_L.acct[g_acc[i]].debit;
        s->c[i] = g_L.acct[g_acc[i]].credit;
    }
    s->lseq = g_L.seq;
    memcpy(s->pos, g_S.pos, sizeof s->pos);
    s->enabled = g_S.enabled;
    s->sw_epoch = g_S.switch_epoch;
}
static bool same_bal(const snap_t *x, const snap_t *y)
{
    return !memcmp(x->d, y->d, sizeof x->d) && !memcmp(x->c, y->c, sizeof x->c) &&
           x->lseq == y->lseq;
}
static bool same_all(const snap_t *x, const snap_t *y)
{
    return same_bal(x, y) && !memcmp(x->pos, y->pos, sizeof x->pos) && x->enabled == y->enabled &&
           x->sw_epoch == y->sw_epoch;
}

/* R, B before an op (B = N - h); false when h > N */
static bool rb(uint64_t *R, uint64_t *B)
{
    uint64_t h = g_C.vault == VSWAP_NONE ? 0 : debit(g_C.vault);
    if (h > N()) return false;
    *R = debit(g_acc[A_RES]);
    *B = N() - h;
    return true;
}

static void common_checks(void)
{
    if (!pay_ledger_check(&g_L)) abort();                 /* V1 */
    if (vswap_epoch_check(&g_S) != VSWAP_OK) abort();     /* V1 */
}

static void run(const uint8_t *data, size_t size)
{
    fz_in in;
    fz_init(&in, data, size);
    pay_ledger_init(&g_L, NULL);
    if (pay_ledger_add_asset(&g_L, "VNO", 2, PAY_ASSET_UNIT, &g_vno) != PAY_OK ||
        pay_ledger_add_asset(&g_L, "VBL", 0, PAY_ASSET_UNIT, &g_vbl) != PAY_OK)
        abort();
    for (uint32_t i = 0; i < NA; i++) {
        uint16_t as = i < A_BISS ? g_vno : g_vbl;
        uint8_t fl = (i == A_VISS || i == A_BISS || i == A_BISS2) ? PAY_ACCT_ISSUER : 0;
        /* each user's two accounts share one owner (100 + k) */
        uint32_t owner = i >= A_UB0 ? 100u + (i - A_UB0) : (i >= A_UV0 && i < A_BISS) ? 100u + (i - A_UV0) : 10u + i;
        if (pay_ledger_open(&g_L, owner, as, PAY_CAP_FINANCIAL, fl, &g_acc[i]) != PAY_OK) abort();
    }
    g_nf = 0;
    for (uint32_t i = 0; i < NU; i++)
        if (raw(g_acc[A_UV0 + i], 1000000, 0, g_acc[A_VISS], 0, 1000000) != PAY_OK) abort();

    vswap_cfg_default(&g_C);
    g_C.vino = g_vno;
    g_C.vbill = g_vbl;
    g_C.reserve = g_acc[A_RES];
    g_C.issuer = g_acc[A_BISS];
    uint8_t cf = fz_u8(&in);
    g_C.vault = (cf & 1u) ? g_acc[A_VAULT] : VSWAP_NONE;
    g_C.lock[VSWAP_TIER_MEDIUM] = g_acc[A_LOCKM];
    g_C.lock[VSWAP_TIER_DEEP] = g_acc[A_LOCKD];
    g_C.gateway = GATE;
    g_C.juris = JURIS;
    g_C.verify = verify;
    if (cf & 2u) {
        g_C.par_num = fz_u8(&in) % 7u + 1u;
        g_C.par_den = fz_u8(&in) % 7u + 1u;
    }
    if (cf & 4u)
        for (int t = 0; t < VSWAP_TIERS; t++) g_C.weight[t] = fz_u8(&in) % 5u + 1u;
    if (cf & 8u) {
        g_C.k_medium = fz_u8(&in) % 10u + 1u;
        g_C.k_deep = g_C.k_medium + fz_u8(&in) % 10u + 2u; /* F strictly larger */
    }
    for (int i = 0; i < 32; i++) g_C.seed[i] = (uint8_t) (i * 13 + 5);
    if (vswap_init(&g_S, &g_L, &g_C) != VSWAP_OK) abort();
    if (g_S.enabled) abort(); /* W1: OFF by default */
    if (cf & 0x10u) { /* start switched on, with V-Bills already outstanding */
        uint8_t m[VSWAP_SWITCH_MSG];
        vswap_switch_msg(JURIS, true, 1, m);
        uint64_t sig = fnv(GATE, m, sizeof m);
        if (vswap_enable(&g_S, JURIS, true, 1, (const uint8_t *) &sig, 8) != VSWAP_OK) abort();
        for (uint32_t i = 0; i < NU; i++)
            if (raw(g_acc[A_UB0 + i], 1000, 0, g_acc[A_BISS], 0, 1000) != PAY_OK) abort();
        if (raw(g_acc[A_RES], 5000, 0, g_acc[A_VISS], 0, 5000) != PAY_OK) abort();
    }

    uint64_t tick = 1;
    while (in.n) {
        /* bias toward the lock / notice / unlock / dividend life cycle */
        static const uint8_t cycle[8] = {3, 4, 5, 5, 1, 2, 6, 11};
        uint8_t ob = fz_u8(&in);
        uint8_t op = ob < 0x40u ? cycle[ob % 8u] : (uint8_t) (ob % 14u);
        g_ops++;
        snap(&g_b);
        uint64_t vsup0 = vno_supply(), n0 = N(), R0 = 0, B0 = 0;
        bool rb_ok = rb(&R0, &B0);
        uint64_t minted_vno = 0;
        uint64_t dn_expect = 0; /* expected change of N */
        bool refused = false;
        tick += fz_u8(&in) % 4u;
        switch (op) {
        case 0: { /* ENABLE */
            uint8_t f = fz_u8(&in);
            uint32_t j = (f & 0x10u) ? JURIS + 1u : JURIS;
            bool en = f & 1u;
            uint64_t ep = (f & 0x20u) ? fz_edge64(&in) : g_S.switch_epoch + (f >> 6);
            uint8_t m[VSWAP_SWITCH_MSG];
            vswap_switch_msg(j, en, ep, m);
            uint64_t sig = fnv((f & 0x08u) ? GATE + 1u : GATE, m, sizeof m);
            if (f & 0x04u) sig ^= 1u;
            vswap_status_t st = vswap_enable(&g_S, j, en, ep, (const uint8_t *) &sig, 8);
            bool good = !(f & 0x18u) && !(f & 0x04u) && ep > g_b.sw_epoch;
            if ((st == VSWAP_OK) != good) abort(); /* V4 */
            if (st == VSWAP_OK) {
                if (g_S.enabled != en || g_S.switch_epoch != ep) abort();
                snap(&g_a);
                if (!same_bal(&g_a, &g_b)) abort();
            } else
                refused = true;
            break;
        }
        case 1: { /* SWAP IN */
            uint32_t uv = pick_uv(&in), ub = pair_ub(&in, uv);
            uint64_t a = amt(&in);
            vswap_quote_t q;
            uint64_t uv0 = debit(uv), ub0 = debit(ub);
            vswap_status_t st = vswap_swap_in(&g_S, uv, ub, a, tick, &q);
            owner_bound(uv, ub, st);
            if (!g_b.enabled && st != VSWAP_ERR_DISABLED) abort(); /* V4 */
            if (st != VSWAP_OK) {
                refused = true;
                break;
            }
            uint64_t rn = q.num, rd = q.den, v = q.vbills, ap = q.vino;
            if (!rb_ok) abort();
            if ((__uint128_t) a * rd / rn != v || v == 0) abort();           /* V5 */
            if (ap > a || (__uint128_t) ap * rd < (__uint128_t) v * rn) abort(); /* ceil */
            if (ap && (__uint128_t) (ap - 1) * rd >= (__uint128_t) v * rn) abort();
            if (debit(uv) != uv0 - ap || debit(ub) != ub0 + v) abort();
            if (debit(g_acc[A_RES]) != R0 + ap) abort();
            /* V5: the ratio never falls (header W3 claim) */
            if ((__uint128_t) ap * B0 < (__uint128_t) v * R0) {
                fprintf(stderr, "W3 ratio fell: R=%llu B=%llu paid a'=%llu for v=%llu (par=%d)\n",
                        (unsigned long long) R0, (unsigned long long) B0, (unsigned long long) ap,
                        (unsigned long long) v, (int) q.par);
                abort();
            }
            dn_expect = v;
            break;
        }
        case 2: { /* SWAP OUT */
            uint32_t ub = pick_ub(&in), uv = pair_uv(&in, ub);
            uint64_t v = amt(&in);
            vswap_quote_t q;
            uint64_t uv0 = debit(uv), ub0 = debit(ub);
            vswap_status_t st = vswap_swap_out(&g_S, ub, uv, v, tick, &q);
            owner_bound(uv, ub, st);
            if (st == VSWAP_ERR_DISABLED) abort(); /* V4: leaving never blocked */
            if (st != VSWAP_OK) {
                refused = true;
                break;
            }
            if (!rb_ok || B0 == 0) abort();
            uint64_t o = (uint64_t) ((__uint128_t) v * R0 / B0);
            if (q.vino != o || o == 0 || q.vbills != v) abort(); /* V6 */
            if ((__uint128_t) (R0 - o) * B0 < (__uint128_t) R0 * (B0 - v)) abort();
            if (debit(ub) != ub0 - v || debit(uv) != uv0 + o) abort();
            if (debit(g_acc[A_RES]) != R0 - o) abort();
            dn_expect = 0 - v;
            break;
        }
        case 3: { /* LOCK */
            uint32_t owner = pick_ub(&in), payee = pair_uv(&in, owner);
            uint8_t tb = fz_u8(&in);
            uint8_t tier = (tb & 0x80u) ? tb % 4u : (uint8_t) (1u + (tb & 1u));
            uint64_t v = (tb & 0x40u) ? amt(&in) : fz_u8(&in) % 300u + 1u;
            bool re = fz_u8(&in) & 1u;
            uint32_t id = 0xFFFFu;
            uint64_t o0 = debit(owner);
            vswap_status_t st = vswap_lock(&g_S, owner, payee, tier, v, re, tick, &id);
            owner_bound(owner, payee, st);
            if (!g_b.enabled && st != VSWAP_ERR_DISABLED) abort(); /* V4 */
            if (st != VSWAP_OK) {
                refused = true;
                break;
            }
            if (tier != VSWAP_TIER_MEDIUM && tier != VSWAP_TIER_DEEP) abort();
            if (id >= VSWAP_MAX_POS || g_b.pos[id].used || !g_S.pos[id].used) abort();
            const vswap_pos_t *p = &g_S.pos[id];
            if (p->owner != owner || p->payee != payee || p->vbills != v || p->tier != tier ||
                p->noticed)
                abort();
            if (debit(owner) != o0 - v || debit(g_C.lock[tier]) != g_b.d[tier == 1 ? A_LOCKM : A_LOCKD] + v)
                abort();
            break;
        }
        case 4: { /* NOTICE */
            uint8_t ib = fz_u8(&in); /* mostly the low slots, where positions are */
            uint32_t id = (ib & 0x80u) ? ib % (VSWAP_MAX_POS + 4u) : ib % 4u;
            uint8_t w = fz_u8(&in);
            uint32_t owner = (id < VSWAP_MAX_POS && (w & 3u)) ? g_S.pos[id].owner : pick(&in);
            vswap_status_t st = vswap_notice(&g_S, id, owner, tick);
            if (st == VSWAP_ERR_DISABLED) abort();
            if (st != VSWAP_OK) {
                refused = true;
                break;
            }
            if (!g_b.pos[id].used || g_b.pos[id].owner != owner || g_b.pos[id].noticed) abort();
            snap(&g_a);
            if (!same_bal(&g_a, &g_b)) abort();
            break;
        }
        case 5: { /* UNLOCK */
            uint8_t ib = fz_u8(&in); /* mostly the low slots, where positions are */
            uint32_t id = (ib & 0x80u) ? ib % (VSWAP_MAX_POS + 4u) : ib % 4u;
            uint8_t w = fz_u8(&in);
            uint32_t owner = (id < VSWAP_MAX_POS && (w & 3u)) ? g_S.pos[id].owner : pick(&in);
            uint64_t t = (w & 0x80u) ? fz_edge64(&in) : tick + (w & 0x40u ? 7000u : 0u);
            /* w & 0x0C: exactly at the end of the notice period, or one tick short */
            if (id < VSWAP_MAX_POS && g_S.pos[id].noticed && (w & 0x0Cu) &&
                g_S.pos[id].notice_tick < UINT64_MAX / 2)
                t = g_S.pos[id].notice_tick + g_S.period[g_S.pos[id].tier] - ((w & 0x08u) ? 1u : 0u);
            uint64_t o0 = debit(owner);
            vswap_pos_t p0;
            memset(&p0, 0, sizeof p0);
            if (id < VSWAP_MAX_POS) p0 = g_S.pos[id];
            vswap_status_t st = vswap_unlock(&g_S, id, owner, t);
            if (st == VSWAP_ERR_DISABLED) abort();
            if (st != VSWAP_OK) {
                refused = true;
                break;
            }
            /* V7 */
            if (!p0.used || p0.owner != owner || !p0.noticed) abort();
            if ((__uint128_t) p0.notice_tick + g_S.period[p0.tier] > t) abort();
            if (g_S.pos[id].used || debit(owner) != o0 + p0.vbills) abort();
            break;
        }
        case 6: { /* DIVIDEND */
            uint32_t pool = (fz_u8(&in) & 7u) ? g_acc[A_POOL] : pick(&in);
            uint32_t n = fz_u8(&in) % 7u;
            vswap_holder_t h[8];
            for (uint32_t i = 0; i < n; i++) {
                uint8_t s = fz_u8(&in);
                h[i].vbill = (s & 0xE0u) == 0xE0u ? pick(&in)
                             : (s & 0x10u)         ? g_acc[A_VAULT]
                                                   : g_acc[A_UB0 + s % NU];
                h[i].payee = (s & 0x08u) ? pick(&in) : g_acc[A_UV0 + (s >> 1) % NU];
            }
            bool ex = fz_u8(&in) & 1u;
            uint64_t paid[8];
            vswap_div_t out;
            vswap_quote_t rate;
            bool have_rate = vswap_rate(&g_S, &rate) == VSWAP_OK;
            uint64_t P0 = debit(pool);
            /* payee balances before (each payee may appear more than once) */
            vswap_status_t st = vswap_dividend(&g_S, pool, h, n, ex, tick, paid, &out);
            if (!g_b.enabled && st != VSWAP_ERR_DISABLED) abort(); /* V4 */
            /* V10: well-formed arguments are never refused as ARG (one
             * outsider's position must not be able to block the dividend) */
            bool args_ok = pool == g_acc[A_POOL];
            for (uint32_t i = 0; i < n; i++) {
                const pay_account_t *hv = pay_ledger_account(&g_L, h[i].vbill),
                                    *hp = pay_ledger_account(&g_L, h[i].payee);
                if (!hv || hv->asset != g_vbl || h[i].vbill == g_acc[A_BISS] ||
                    h[i].vbill == g_acc[A_LOCKM] || h[i].vbill == g_acc[A_LOCKD] || !hp ||
                    hp->asset != g_vno || h[i].payee == pool)
                    args_ok = false;
                for (uint32_t j = 0; j < i; j++)
                    if (h[j].vbill == h[i].vbill) args_ok = false;
            }
            if (args_ok && st == VSWAP_ERR_ARG) {
                fprintf(stderr, "V10: dividend refused (ARG) with well-formed arguments\n");
                abort();
            }
            if (st == VSWAP_ERR_PARTIAL) {
                dn_expect = out.vbills;
                break;
            }
            if (st != VSWAP_OK) {
                refused = true;
                break;
            }
            /* V8 */
            if (!have_rate || out.pool != P0 || debit(pool) != 0) abort();
            if (out.paid + out.reinvest != P0) abort();
            uint64_t to_res = 0; /* a listed holder may name the reserve as its payee */
            for (uint32_t i = 0; i < n; i++)
                if (h[i].payee == g_acc[A_RES]) to_res += paid[i];
            if (debit(g_acc[A_RES]) != R0 + out.reinvest + to_res) abort();
            /* independent largest-remainder reference */
            uint32_t cnt = n + VSWAP_MAX_POS;
            static uint64_t u[VSWAP_MAX_HOLD + VSWAP_MAX_POS], sh[VSWAP_MAX_HOLD + VSWAP_MAX_POS],
                rm[VSWAP_MAX_HOLD + VSWAP_MAX_POS];
            __uint128_t U = 0;
            for (uint32_t i = 0; i < cnt; i++) {
                if (i < n) {
                    uint64_t w = (ex && h[i].vbill == g_C.vault) ? 0 : g_C.weight[0];
                    u[i] = debit(h[i].vbill) * w; /* a dividend never moves holder V-Bills */
                } else {
                    const vswap_pos_t *p = &g_b.pos[i - n];
                    /* a position paying into the pool itself has weight 0 */
                    u[i] = p->used && p->payee != pool ? p->vbills * g_C.weight[p->tier] : 0;
                }
                U += u[i];
            }
            if (U == 0 || U != out.units) abort();
            uint64_t sum = 0;
            for (uint32_t i = 0; i < cnt; i++) {
                sh[i] = (uint64_t) ((__uint128_t) P0 * u[i] / U);
                rm[i] = (uint64_t) ((__uint128_t) P0 * u[i] % U);
                sum += sh[i];
            }
            bool got[VSWAP_MAX_HOLD + VSWAP_MAX_POS] = {false};
            for (uint64_t left = P0 - sum; left; left--) {
                int best = -1;
                for (uint32_t i = 0; i < cnt; i++)
                    if (!got[i] && (best < 0 || rm[i] > rm[best])) best = (int) i;
                got[best] = true;
                sh[best]++;
            }
            uint64_t tot_v = 0;
            for (uint32_t i = 0; i < cnt; i++) {
                if (i < n) {
                    if (paid[i] != sh[i]) abort();
                    continue;
                }
                const vswap_pos_t *p0 = &g_b.pos[i - n], *p1 = &g_S.pos[i - n];
                if (!p0->used) continue;
                uint64_t addv = 0, cost = 0;
                if (p0->reinvest && sh[i]) {
                    addv = (uint64_t) ((__uint128_t) sh[i] * rate.den / rate.num);
                    if (addv) {
                        __uint128_t c = (__uint128_t) addv * rate.num;
                        cost = (uint64_t) ((c + rate.den - 1) / rate.den);
                    }
                }
                if (p1->last_vbills != addv || p1->vbills != p0->vbills + addv) abort();
                if (p1->last_vino != sh[i] - cost) abort();
                tot_v += addv;
            }
            if (tot_v != out.vbills) abort();
            dn_expect = out.vbills;
            break;
        }
        case 7: { /* outside: Vino issued to any account */
            uint32_t to = (fz_u8(&in) & 3u) == 0 ? pick(&in) : g_acc[A_RES + fz_u8(&in) % 2u];
            uint64_t a = amt(&in);
            if (raw(to, (int64_t) a, 0, g_acc[A_VISS], 0, (int64_t) a) == PAY_OK) minted_vno = a;
            break;
        }
        case 8: { /* outside: reserve loss */
            uint64_t a = amt(&in);
            raw(g_acc[A_RES], (int64_t) (0 - a), 0, g_acc[A_LOSS], (int64_t) a, 0);
            break;
        }
        case 9: { /* outside: V-Bills issued */
            uint32_t to = (fz_u8(&in) & 3u) ? g_acc[A_UB0 + fz_u8(&in) % NU] : g_acc[A_VAULT];
            uint64_t a = amt(&in);
            if (raw(to, (int64_t) a, 0, g_acc[A_BISS], 0, (int64_t) a) == PAY_OK)
                dn_expect = a;
            break;
        }
        case 10: { /* outside: vault buys V-Bills from a user */
            uint32_t from = g_acc[A_UB0 + fz_u8(&in) % NU];
            uint64_t a = amt(&in);
            raw(from, (int64_t) (0 - a), 0, g_acc[A_VAULT], (int64_t) a, 0);
            break;
        }
        case 11: { /* outside: the dividend pool is funded from a user */
            uint32_t from = g_acc[A_UV0 + fz_u8(&in) % NU];
            uint64_t a = amt(&in);
            raw(from, (int64_t) (0 - a), 0, g_acc[A_POOL], (int64_t) a, 0);
            break;
        }
        case 12: { /* quotes: pure */
            vswap_quote_t q;
            uint64_t a = amt(&in);
            vswap_quote_in(&g_S, a, &q);
            vswap_quote_out(&g_S, a, &q);
            snap(&g_a);
            if (!same_all(&g_a, &g_b)) abort();
            break;
        }
        default: { /* other issuer's V-Bills into the vault (h may exceed N) */
            uint64_t a = amt(&in);
            raw(g_acc[A_VAULT], (int64_t) a, 0, g_acc[A_BISS2], 0, (int64_t) a);
            break;
        }
        }
        if (!refused) g_hit[op]++;
        snap(&g_a);
        if (refused && !same_all(&g_a, &g_b)) abort(); /* V2 */
        if (vno_supply() != vsup0 + minted_vno) abort(); /* V3 */
        if (N() - n0 != dn_expect) abort();  /* V3 */
        common_checks();
    }
    if (!pay_ledger_verify_chain(&g_L)) abort(); /* once per input: it is O(journal) */
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 8192) return 0;
    static pay_account_t acct1[PAY_MAX_ACCOUNTS];
    static vswap_pos_t pos1[VSWAP_MAX_POS];
    static uint8_t head1[PAY_HASH_LEN];
    unsigned long long ops = g_ops;
    run(data, size);
    memcpy(acct1, g_L.acct, sizeof acct1);
    memcpy(pos1, g_S.pos, sizeof pos1);
    memcpy(head1, g_L.chain_head, sizeof head1);
    bool en = g_S.enabled;
    uint64_t seq = g_S.seq;
    run(data, size);
    g_ops = ops + (g_ops - ops) / 2;
    /* V9 */
    if (memcmp(acct1, g_L.acct, sizeof acct1) || memcmp(pos1, g_S.pos, sizeof pos1) ||
        memcmp(head1, g_L.chain_head, sizeof head1) || en != g_S.enabled || seq != g_S.seq)
        abort();
    return 0;
}

__attribute__((destructor)) static void report(void)
{
    if (getenv("FUZZ_PROP_STATS")) fprintf(stderr, "fuzz_econ_vswap: %llu ops checked\n", g_ops);
    if (getenv("FUZZ_PROP_STATS")) {
        fprintf(stderr, "  succeeded per op:");
        for (int i = 0; i < 32; i++)
            if (g_hit[i]) fprintf(stderr, " %d:%llu", i, g_hit[i]);
        fprintf(stderr, "\n");
    }
}
