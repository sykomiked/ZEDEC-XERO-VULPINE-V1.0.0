/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_econ_zenny.c — state-machine property harness for Zenny chips
 * (zenny/zenny.c), the exact basket manifest core (no ledger).
 *
 * Mints are built to be exact (value_i = base * weight_i, qty_i = value_i *
 * den_i / num_i, base a multiple of kappa * 840) and then optionally
 * mutated, so both the accepting and every refusing path run. The verify
 * hook accepts sig == first 8 bytes of SHA-256(signer || msg). Ops: MINT,
 * REBALANCE (new prices, or weight moved between legs), DECOMPRESS (quoted or
 * raw c, stored or other batch prices), TOPUP (quoted or raw amount),
 * ATTEST, RECEIPT_ACK.
 * Invariants after every op (128-bit exact models, independent of the
 * module's arithmetic):
 *   Z-CLAIM  for every chip: sum qty_i*num_i/den_i (at the stored prices)
 *            == chips * kappa as an exact rational: compression never
 *            multiplies value; zenny_check passes (define
 *            ZENNY_ALLOW_INEXACT_REMAINDER to skip only the zenny_check call)
 *   Z-WORM   zenny_verify_chain passes; kappa, rules, minter, co-minter and
 *            each leg's kind / asset / custodian never change; versions only
 *            grow; a version's recipient is 0 unless it is a top-up (define
 *            ZENNY_ALLOW_STALE_RECIPIENT to skip)
 *   Z-REF    a refused op changes nothing (the chip, the receipt ring and
 *            every other chip's version count)
 *   Z2       a mint succeeds only with the minter's (and a BILATERAL
 *            co-minter's) honest signature and an honest custodian
 *            attestation for every COMMODITY leg; MARGIN / CREDIT always
 *            refused; a clean exact request is accepted
 *   Z4       a rebalance needs ALLOW_REBALANCE and signatures over the
 *            current version hash; the claim (chips) never changes; a
 *            custodian attestation replayed from an earlier version (even
 *            for the same quantity, e.g. after X -> Y -> X) is refused
 *   Z5       decompress: out_i == qty_i*c/N exact, the remainder is
 *            qty_i - out_i, value_i exact at the batch price, fee ==
 *            pay_assure_fee(total) <= total, c <= N, the paid-out part is
 *            exact at the stored prices too (so the remainder stays exact);
 *            zenny_quote(want) is exactly the largest accepted c <= want
 *            (brute force when small)
 *   Z7       top-up: chips grow by amount/kappa exactly, each weighted leg by
 *            amount*w_i/W*den_i/num_i; never into an unbacked weighted leg;
 *            topup_quote(want) > 0 implies the top-up accepts it
 *   NO USURY: nothing depends on the tick except attestation expiry; no
 *            claim ever exceeds the deposit. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zenny.h"
#include "../robin_debanks/sha256.h"
#include "fuzz_in.h"

typedef unsigned __int128 u128;

static zenny_t Z;
static unsigned long long g_ops, g_okc[8];
static uint8_t g_att[ZENNY_MAX_LEGS][8], g_sm[8], g_sc[8];
/* the last honest commodity attestation seen per leg (for the replay probe) */
static uint8_t g_old_att[ZENNY_MAX_CHIPS][ZENNY_MAX_LEGS][8];
static uint64_t g_old_qty[ZENNY_MAX_CHIPS][ZENNY_MAX_LEGS];
static bool g_old_ok[ZENNY_MAX_CHIPS][ZENNY_MAX_LEGS];
static const uint8_t g_zero[32];
unsigned long long g_stale_att_refused; /* reported */

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

static void sign_digest(uint32_t who, zenny_purpose_t p, const uint8_t d[32], uint8_t sig[8])
{
    uint8_t m[ZENNY_SIG_MSG];
    zenny_sign_msg(p, d, m);
    mksig(who, m, sizeof m, sig);
}

/* exact value of a basket at its prices: false when inexact or den 0 */
static bool basket_value(uint32_t n, const zenny_leg_t *leg, const zenny_price_t *pr, u128 *T)
{
    *T = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (pr[i].den == 0) return false;
        u128 x = (u128) leg[i].qty * pr[i].num;
        if (x % pr[i].den) return false;
        *T += x / pr[i].den;
    }
    return true;
}

/* Z-CLAIM as an exact rational: sum qty*num/den == chips * kappa */
static bool claim_rational(const zenny_chip_t *ch)
{
    const zenny_version_t *v = &ch->ver[ch->n_ver - 1];
    /* sum over a common denominator would overflow; compare leg by leg via
     * the integer part and the fractional numerators over D = prod(den) is
     * too big, so use the fact that each den <= 2^64 and accumulate as a
     * fraction a/b reduced at each step (b stays a divisor of lcm(dens)). */
    u128 a = 0, b = 1;
    for (uint32_t i = 0; i < v->n_legs; i++) {
        uint64_t d = v->price[i].den;
        if (d == 0) return false;
        u128 x = (u128) v->leg[i].qty * v->price[i].num; /* x / d */
        u128 q = x / d, r = x % d;
        a += q * b; /* integer part (may overflow only on absurd values) */
        if (r) {
            /* a/b + r/d = (a*d' + r*b')/lcm with small dens only */
            u128 g0 = b, g1 = d;
            while (g1) {
                u128 t = g0 % g1;
                g0 = g1;
                g1 = t;
            }
            u128 l = b / g0 * d;
            if (l >> 64) return true; /* too large to judge exactly here */
            a = a * (l / b) + r * (l / d);
            b = l;
        }
    }
    u128 want = (u128) v->chips * ch->kappa;
    return a == want * b;
}

static uint64_t small_or_edge(fz_in *in, uint64_t mod)
{
    uint8_t b = fz_u8(in);
    if (b < 200) return 1 + (uint64_t) (b % mod);
    return fz_edge64(in);
}

static zenny_chip_t snap;
static void take(uint32_t s)
{
    if (s < ZENNY_MAX_CHIPS) snap = Z.chip[s];
}

static bool chip_snapshot_eq(const zenny_chip_t *a, const zenny_chip_t *b)
{
    return !memcmp(a, b, sizeof *a);
}

/* ---- the brute-force acceptance model for decompression ---- */
static bool decompress_ok_model(const zenny_version_t *v, uint64_t c, const zenny_price_t *batch)
{
    if (c == 0 || c > v->chips) return false;
    u128 tot = 0;
    for (uint32_t i = 0; i < v->n_legs; i++) {
        u128 x = (u128) v->leg[i].qty * c;
        if (x % v->chips) return false;
        u128 o = x / v->chips;
#ifndef ZENNY_ALLOW_INEXACT_REMAINDER
        /* the remainder keeps the stored prices: the part paid out must be
         * exact at them too, or the remainder no longer values exactly */
        if (v->price[i].den == 0 || (o * v->price[i].num) % v->price[i].den) return false;
#endif
        if (!batch) continue;
        if (batch[i].den == 0) return false;
        u128 y = o * batch[i].num;
        if (y % batch[i].den) return false;
        u128 val = y / batch[i].den;
        if (val >> 64) return false;
        tot += val;
        if (tot >> 64) return false;
    }
    return true;
}

static uint32_t pick_slot(fz_in *in)
{
    uint8_t b = fz_u8(in);
    for (uint32_t k = 0; k < ZENNY_MAX_CHIPS; k++) {
        uint32_t s = (b + k) % ZENNY_MAX_CHIPS;
        if (Z.chip[s].used) return s;
    }
    return b % (ZENNY_MAX_CHIPS + 1);
}

static void post_checks(void)
{
    for (uint32_t s = 0; s < ZENNY_MAX_CHIPS; s++) {
        const zenny_chip_t *ch = &Z.chip[s];
        if (!ch->used) continue;
        if (zenny_verify_chain(&Z, s) != ZENNY_OK) abort(); /* Z-WORM */
        if (!claim_rational(ch)) abort();                  /* Z-CLAIM */
#ifndef ZENNY_ALLOW_INEXACT_REMAINDER
        if (zenny_check(&Z, s) != ZENNY_OK) abort();
#endif
        for (uint32_t k = 0; k < ch->n_ver; k++) {
            const zenny_version_t *v = &ch->ver[k];
            if (v->n_legs != ch->ver[0].n_legs) abort();
            for (uint32_t i = 0; i < v->n_legs; i++)
                if (v->leg[i].kind != ch->ver[0].leg[i].kind ||
                    v->leg[i].asset != ch->ver[0].leg[i].asset ||
                    v->leg[i].custodian != ch->ver[0].leg[i].custodian)
                    abort();
#ifndef ZENNY_ALLOW_STALE_RECIPIENT
            if (v->op != ZENNY_OP_TOPUP && v->recipient != 0) abort();
#endif
        }
    }
    if (Z.r_next - Z.r_acked > ZENNY_MAX_RECEIPTS || Z.r_acked > Z.r_next) abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 4096) return 0;
    fz_in in;
    fz_init(&in, data, size);
    zenny_init(&Z, verify, 0);
    memset(g_old_ok, 0, sizeof g_old_ok);
    static zenny_receipt_t rsnap[ZENNY_MAX_RECEIPTS];

    while (in.n) {
        uint8_t op = fz_u8(&in) % 6u;
        g_ops++;
        uint32_t nver[ZENNY_MAX_CHIPS];
        for (uint32_t s = 0; s < ZENNY_MAX_CHIPS; s++)
            nver[s] = Z.chip[s].used ? Z.chip[s].n_ver : 0;
        memcpy(rsnap, Z.rcpt, sizeof rsnap);
        uint64_t rn0 = Z.r_next, ra0 = Z.r_acked;
        uint32_t slot = ZENNY_MAX_CHIPS;
        int st = 0;

        if (op == 0) { /* MINT */
            zenny_req_t r;
            memset(&r, 0, sizeof r);
            uint8_t f = fz_u8(&in), g = fz_u8(&in);
            bool clean = true;
            r.n_legs = 1 + fz_u8(&in) % ZENNY_MAX_LEGS;
            r.kappa = small_or_edge(&in, 16);
            r.rules = (f & 1 ? ZENNY_RULE_ALLOW_REBALANCE : 0) |
                      (f & 2 ? ZENNY_RULE_BILATERAL : 0) | (f & 4 ? ZENNY_RULE_ALLOW_TOPUP : 0);
            r.minter = 10 + (g & 3);
            r.co_minter = 10 + ((g >> 2) & 3);
            r.nonce = fz_u8(&in);
            r.tick = fz_u16(&in);
            uint64_t m = small_or_edge(&in, 4);
            u128 base = (u128) r.kappa * 840u * m;
            if ((base >> 64) || base == 0) clean = false;
            for (uint32_t i = 0; i < r.n_legs; i++) {
                uint8_t lb = fz_u8(&in), pb = fz_u8(&in);
                zenny_leg_t *l = &r.leg[i];
                l->kind = i == 0 ? ZENNY_VBILL : (uint8_t) (1 + lb % 4);
                l->custodian = 50 + (lb >> 6);
                l->asset = 1000 + i;
                l->weight = i == 0 ? 1 + (lb >> 2) % 5 : (lb >> 2) % 5;
                r.price[i].num = 1 + pb % 8;
                r.price[i].den = 1 + (pb >> 3) % 8;
                u128 v = base * l->weight, q = v * r.price[i].den / r.price[i].num;
                if (v >> 64 || q >> 64) clean = false;
                l->qty = (uint64_t) q;
            }
            /* mutations (each makes the request unclean) */
            uint8_t mu = fz_u8(&in);
            if (mu >= 0xF0) {
                uint32_t i = mu % r.n_legs;
                switch ((mu >> 1) & 7) {
                case 0: r.leg[i].qty++; break;
                case 1: r.leg[i].kind = (uint8_t) (mu % 7); break;
                case 2: r.rules |= ZENNY_RULE_MARGIN; break;
                case 3: r.rules |= ZENNY_RULE_CREDIT; break;
                case 4: r.price[i].den = 0; break;
                case 5: r.leg[i].weight += 1; break;
                case 6: r.kappa = 0; break;
                default: r.n_legs = (mu & 1) ? 0 : ZENNY_MAX_LEGS + 1; break;
                }
                clean = false;
            }
            if ((r.rules & ZENNY_RULE_BILATERAL) && r.co_minter == r.minter) clean = false;
            uint8_t id[32];
            zenny_mint_digest(&r, id);
            uint8_t sf = fz_u8(&in);
            sign_digest((sf & 0xE0) == 0xE0 ? r.minter + 1 : r.minter, ZENNY_SIG_MINT, id, g_sm);
            sign_digest((sf & 0x1C) == 0x1C ? r.co_minter + 1 : r.co_minter, ZENNY_SIG_MINT, id,
                        g_sc);
            bool sig_ok = (sf & 0xE0) != 0xE0 &&
                          (!(r.rules & ZENNY_RULE_BILATERAL) || (sf & 0x1C) != 0x1C);
            bool att_ok = true;
            for (uint32_t i = 0; i < r.n_legs && i < ZENNY_MAX_LEGS; i++) {
                uint8_t d[32];
                zenny_att_digest(id, g_zero, &r.leg[i], d);
                sign_digest(r.leg[i].custodian, ZENNY_SIG_ATTEST, d, g_att[i]);
                if ((sf & 3) == 3 && i == r.n_legs - 1) {
                    g_att[i][5] ^= 0x10;
                    if (r.leg[i].kind == ZENNY_COMMODITY) att_ok = false;
                }
                r.att[i] = g_att[i];
                r.att_len[i] = 8;
            }
            r.sig_minter = g_sm;
            r.sig_minter_len = 8;
            r.sig_co = g_sc;
            r.sig_co_len = 8;
            bool dup = zenny_find(&Z, id, 0), full = true;
            for (uint32_t s = 0; s < ZENNY_MAX_CHIPS; s++)
                if (!Z.chip[s].used) full = false;
            st = zenny_mint(&Z, &r, &slot);
            if (st == ZENNY_OK) {
                if (!sig_ok || !att_ok || dup) abort(); /* Z2 authorisation */
                if (r.rules & (ZENNY_RULE_MARGIN | ZENNY_RULE_CREDIT)) abort();
                u128 T;
                if (!basket_value(r.n_legs, r.leg, r.price, &T) || T == 0 || T % r.kappa) abort();
                const zenny_version_t *v = zenny_current(&Z, slot);
                if (!v || v->chips != (uint64_t) (T / r.kappa) || v->version != 0) abort();
                for (uint32_t i = 0; i < r.n_legs; i++)
                    if (r.leg[i].kind == ZENNY_COMMODITY) {
                        memcpy(g_old_att[slot][i], g_att[i], 8);
                        g_old_qty[slot][i] = r.leg[i].qty;
                        g_old_ok[slot][i] = true;
                    } else {
                        g_old_ok[slot][i] = false;
                    }
            } else if (clean && sig_ok && att_ok && !dup && !full) {
                /* a clean exact request must be accepted unless a sum overflows */
                u128 T = 0, W = 0;
                for (uint32_t i = 0; i < r.n_legs; i++) {
                    T += (u128) r.leg[i].qty * r.price[i].num / r.price[i].den;
                    W += r.leg[i].weight;
                }
                if (getenv("FZ_TRACE")) fprintf(stderr, "clean mint refused st %d\n", st);
                if (!(T >> 64) && !(W >> 64) && st != ZENNY_ERR_OVERFLOW) abort();
            }
        } else if (op == 1) { /* REBALANCE */
            slot = pick_slot(&in);
            const zenny_chip_t *ch = slot < ZENNY_MAX_CHIPS && Z.chip[slot].used ? &Z.chip[slot] : 0;
            zenny_req_t r;
            memset(&r, 0, sizeof r);
            uint8_t f = fz_u8(&in);
            if (ch) {
                const zenny_version_t *cur = &ch->ver[ch->n_ver - 1];
                r.minter = ch->minter;
                r.co_minter = ch->co_minter;
                r.kappa = ch->kappa;
                r.rules = ch->rules;
                r.n_legs = cur->n_legs;
                for (uint32_t i = 0; i < r.n_legs; i++) {
                    r.leg[i] = cur->leg[i];
                    r.price[i] = cur->price[i];
                }
                if (f & 1) { /* new prices, same values where exact */
                    for (uint32_t i = 0; i < r.n_legs; i++) {
                        uint8_t pb = fz_u8(&in);
                        u128 v = (u128) cur->leg[i].qty * cur->price[i].num / cur->price[i].den;
                        zenny_price_t np = {1 + pb % 8, 1 + (pb >> 3) % 8};
                        u128 q = v * np.den / np.num;
                        if (!(q >> 64)) {
                            r.leg[i].qty = (uint64_t) q;
                            r.price[i] = np;
                        }
                    }
                }
                if (f & 2) { /* move weight between two legs at fixed value per unit */
                    uint32_t a = fz_u8(&in) % r.n_legs, b = fz_u8(&in) % r.n_legs;
                    u128 T;
                    uint64_t W = 0;
                    for (uint32_t i = 0; i < r.n_legs; i++) W += r.leg[i].weight;
                    if (basket_value(r.n_legs, r.leg, r.price, &T) && W && T % W == 0 &&
                        r.leg[a].weight && r.price[a].num && r.price[b].num) {
                        u128 u = T / W;
                        r.leg[a].weight--;
                        r.leg[b].weight++;
                        r.leg[a].qty = (uint64_t) (u * r.leg[a].weight * r.price[a].den /
                                                   r.price[a].num);
                        r.leg[b].qty = (uint64_t) (u * r.leg[b].weight * r.price[b].den /
                                                   r.price[b].num);
                    }
                }
                if ((f & 0xC0) == 0xC0) r.kappa++;          /* immutable change */
                if ((f & 0x30) == 0x30) r.leg[0].asset ^= 1; /* immutable change */
            }
            uint8_t d[32] = {0};
            if (ch) zenny_rebalance_digest(&Z, slot, &r, d);
            uint8_t sf = fz_u8(&in);
            sign_digest(r.minter, ZENNY_SIG_REBALANCE, d, g_sm);
            sign_digest(r.co_minter, ZENNY_SIG_REBALANCE, d, g_sc);
            if ((sf & 7) == 7) g_sm[0] ^= 1;
            if ((sf & 0x38) == 0x38) g_sc[0] ^= 1;
            bool stale_used = false, att_ok = true;
            for (uint32_t i = 0; i < r.n_legs && i < ZENNY_MAX_LEGS; i++) {
                uint8_t ad[32];
                if (ch) zenny_att_digest(ch->id, ch->ver[ch->n_ver - 1].hash, &r.leg[i], ad);
                sign_digest(r.leg[i].custodian, ZENNY_SIG_ATTEST, ad, g_att[i]);
                if (ch && (sf & 0x40) && g_old_ok[slot][i]) { /* replay an older attestation */
                    memcpy(g_att[i], g_old_att[slot][i], 8);
                    stale_used = true;
                }
                if ((sf & 0xC0) == 0xC0 && r.leg[i].kind == ZENNY_COMMODITY) {
                    g_att[i][2] ^= 8;
                    att_ok = false;
                }
                r.att[i] = g_att[i];
                r.att_len[i] = 8;
            }
            r.sig_minter = g_sm;
            r.sig_minter_len = 8;
            r.sig_co = g_sc;
            r.sig_co_len = 8;
            uint64_t chips0 = ch ? ch->ver[ch->n_ver - 1].chips : 0;
            take(slot);
            st = zenny_rebalance(&Z, slot, &r);
            if (stale_used && st == ZENNY_ERR_ATTEST) g_stale_att_refused++;
            if (st == ZENNY_OK) {
                if (!(ch->rules & ZENNY_RULE_ALLOW_REBALANCE)) abort();
                if ((sf & 7) == 7) abort();
                if ((ch->rules & ZENNY_RULE_BILATERAL) && (sf & 0x38) == 0x38) abort();
                if (!att_ok) abort();
                if (zenny_current(&Z, slot)->chips != chips0) abort(); /* Z4 claim */
                if (stale_used) abort(); /* Z4: an attestation from an earlier version */
                for (uint32_t i = 0; i < r.n_legs; i++)
                    if (r.leg[i].kind == ZENNY_COMMODITY) {
                        memcpy(g_old_att[slot][i], g_att[i], 8);
                        g_old_qty[slot][i] = r.leg[i].qty;
                    }
            }
        } else if (op == 2) { /* DECOMPRESS */
            slot = pick_slot(&in);
            const zenny_chip_t *ch = slot < ZENNY_MAX_CHIPS && Z.chip[slot].used ? &Z.chip[slot] : 0;
            zenny_price_t batch[ZENNY_MAX_LEGS];
            uint8_t f = fz_u8(&in);
            for (uint32_t i = 0; i < ZENNY_MAX_LEGS; i++) {
                uint8_t pb = (f & 1) ? fz_u8(&in) : 0;
                batch[i] = (f & 1) ? (zenny_price_t){pb % 9, 1 + (pb >> 4)}
                                   : ch ? ch->ver[ch->n_ver - 1].price[i] : (zenny_price_t){1, 1};
                if (batch[i].den == 0) batch[i].den = 1;
            }
            uint64_t want = (f & 2) ? fz_edge64(&in) : fz_u8(&in);
            uint64_t c = want;
            zenny_version_t cur;
            if (ch) {
                cur = ch->ver[ch->n_ver - 1];
                uint64_t q = zenny_quote(&Z, slot, want, batch);
                /* Z5: the quote is exactly the largest accepted c <= want */
                if (q && !decompress_ok_model(&cur, q, batch)) abort();
                if (q > want || q > cur.chips) abort();
                uint64_t top = want < cur.chips ? want : cur.chips;
                if (top <= 256) {
                    uint64_t best = 0;
                    for (uint64_t k = top; k >= 1; k--)
                        if (decompress_ok_model(&cur, k, batch)) {
                            best = k;
                            break;
                        }
                    if (best != q) abort();
                }
                if (!(f & 4)) c = q;
            }
            zenny_receipt_t rc;
            take(slot);
            st = zenny_decompress(&Z, slot, c, batch, fz_u16(&in), &rc);
            if (st == ZENNY_OK) {
                if (!decompress_ok_model(&cur, c, batch)) abort();
                const zenny_version_t *v = zenny_current(&Z, slot);
                if (v->chips != cur.chips - c || v->op != ZENNY_OP_DECOMPRESS) abort();
                u128 tot = 0;
                for (uint32_t i = 0; i < cur.n_legs; i++) {
                    uint64_t o = (uint64_t) ((u128) cur.leg[i].qty * c / cur.chips);
                    if (rc.out[i] != o || v->leg[i].qty != cur.leg[i].qty - o) abort();
                    uint64_t val = (uint64_t) ((u128) o * batch[i].num / batch[i].den);
                    if (rc.value[i] != val) abort();
                    tot += val;
                    if (v->price[i].num != cur.price[i].num || v->price[i].den != cur.price[i].den)
                        abort();
                }
                if (rc.total != (uint64_t) tot || rc.fee != pay_assure_fee(rc.total) ||
                    rc.fee > rc.total)
                    abort();
                if (rc.seq != rn0 || Z.r_next != rn0 + 1 || rc.chips != c) abort();
            }
        } else if (op == 3) { /* TOPUP */
            slot = pick_slot(&in);
            const zenny_chip_t *ch = slot < ZENNY_MAX_CHIPS && Z.chip[slot].used ? &Z.chip[slot] : 0;
            zenny_price_t pr[ZENNY_MAX_LEGS];
            uint8_t f = fz_u8(&in);
            for (uint32_t i = 0; i < ZENNY_MAX_LEGS; i++) {
                uint8_t pb = (f & 1) ? fz_u8(&in) : 0;
                pr[i] = (f & 1) ? (zenny_price_t){pb % 9, 1 + (pb >> 4)}
                                : ch ? ch->ver[ch->n_ver - 1].price[i] : (zenny_price_t){1, 1};
            }
            uint64_t tick = fz_u16(&in);
            uint64_t want = (f & 2) ? fz_edge64(&in) : (uint64_t) fz_u16(&in) * fz_u16(&in);
            uint64_t amount = want;
            if (ch && !(f & 4)) {
                uint64_t q = zenny_topup_quote(&Z, slot, want, pr, tick);
                uint64_t unit = zenny_topup_unit(&Z, slot, pr);
                if (q && (!unit || q % unit || q > want)) abort();
                amount = q ? q : want;
            }
            zenny_version_t cur;
            if (ch) cur = ch->ver[ch->n_ver - 1];
            uint32_t who = 70 + (f >> 4);
            uint64_t added = 0;
            take(slot);
            st = zenny_topup(&Z, slot, amount, pr, who, tick, &added);
            if (st == ZENNY_OK) {
                if (!(ch->rules & ZENNY_RULE_ALLOW_TOPUP) || amount % ch->kappa) abort();
                const zenny_version_t *v = zenny_current(&Z, slot);
                if (added != amount / ch->kappa || v->chips != cur.chips + added) abort();
                if (v->recipient != who || v->op != ZENNY_OP_TOPUP) abort();
                u128 W = 0;
                for (uint32_t i = 0; i < cur.n_legs; i++) W += cur.leg[i].weight;
                for (uint32_t i = 0; i < cur.n_legs; i++) {
                    const zenny_leg_t *l = &cur.leg[i];
                    u128 dq = 0;
                    if (l->weight) {
                        if (l->kind == ZENNY_COMPUTE) abort(); /* unbacked */
                        if (l->kind == ZENNY_COMMODITY &&
                            (tick >= ch->att_exp[i] || v->leg[i].qty > ch->att_cap[i]))
                            abort();
                        dq = (u128) amount * l->weight / W * pr[i].den / pr[i].num;
                    }
                    if (v->leg[i].qty != l->qty + dq) abort();
                }
            }
        } else if (op == 4) { /* ATTEST */
            slot = pick_slot(&in);
            const zenny_chip_t *ch = slot < ZENNY_MAX_CHIPS && Z.chip[slot].used ? &Z.chip[slot] : 0;
            uint32_t i = fz_u8(&in) % (ZENNY_MAX_LEGS + 1);
            uint64_t cap = small_or_edge(&in, 1u << 20) * 1000u;
            uint8_t f = fz_u8(&in);
            uint64_t exp = (ch && i < ZENNY_MAX_LEGS ? ch->att_exp[i] : 0) + 1 + fz_u16(&in);
            if (f & 1) exp = fz_edge64(&in);
            uint8_t d[32] = {0}, sig[8];
            zenny_leg_t l = {0};
            if (ch && i < ch->ver[ch->n_ver - 1].n_legs) l = ch->ver[ch->n_ver - 1].leg[i];
            if (ch) zenny_cap_digest(ch->id, i, &l, cap, exp, d);
            sign_digest(l.custodian, ZENNY_SIG_ATTEST, d, sig);
            if ((f & 6) == 6) sig[7] ^= 1;
            uint64_t old_exp = ch && i < ZENNY_MAX_LEGS ? ch->att_exp[i] : 0;
            take(slot);
            st = zenny_attest(&Z, slot, i, cap, exp, sig, 8);
            if (st == ZENNY_OK) {
                if ((f & 6) == 6 || l.kind != ZENNY_COMMODITY || exp <= old_exp) abort();
                if (ch->att_cap[i] != cap || ch->att_exp[i] != exp) abort();
            }
        } else { /* RECEIPT_ACK / receipt reads */
            uint64_t upto = Z.r_acked + fz_u8(&in) % 20;
            zenny_receipt_t g;
            for (uint64_t k = Z.r_acked; k < Z.r_next; k++)
                if (zenny_receipt_get(&Z, k, &g) != ZENNY_OK || g.seq != k) abort();
            st = zenny_receipt_ack(&Z, upto);
            if (st == ZENNY_OK && (upto > rn0 || upto < ra0)) abort();
            if (st == ZENNY_OK) {
                for (uint32_t s = 0; s < ZENNY_MAX_CHIPS; s++)
                    if ((Z.chip[s].used ? Z.chip[s].n_ver : 0) != nver[s]) abort();
                post_checks();
                g_okc[op]++;
                continue;
            }
        }
        if (st == ZENNY_OK) g_okc[op]++;
        if (getenv("FZ_TRACE")) fprintf(stderr, "op %u slot %u st %d\n", op, slot, st);

        /* Z-REF: a refused op changes nothing */
        if (st != ZENNY_OK) {
            for (uint32_t s = 0; s < ZENNY_MAX_CHIPS; s++)
                if ((Z.chip[s].used ? Z.chip[s].n_ver : 0) != nver[s]) abort();
            if (Z.r_next != rn0 || Z.r_acked != ra0 || memcmp(rsnap, Z.rcpt, sizeof rsnap)) abort();
            if (op != 0 && slot < ZENNY_MAX_CHIPS && Z.chip[slot].used &&
                !chip_snapshot_eq(&snap, &Z.chip[slot]))
                abort();
        }
        post_checks();
    }
    return 0;
}

__attribute__((destructor)) static void report(void)
{
    if (getenv("FUZZ_PROP_STATS")) {
        fprintf(stderr, "fuzz_econ_zenny: %llu ops checked\n", g_ops);
        for (int i = 0; i < 8; i++)
            if (g_okc[i]) fprintf(stderr, "  op %d ok %llu\n", i, g_okc[i]);
        fprintf(stderr, "  stale commodity attestations refused in rebalance: %llu\n",
                g_stale_att_refused);
    }
}
