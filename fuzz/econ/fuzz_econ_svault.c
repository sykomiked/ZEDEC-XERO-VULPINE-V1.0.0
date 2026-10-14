/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_econ_svault.c — state-machine property harness for the protocol
 * solvency vault (svault/svault.c) on a real pay_ledger.
 *
 * A fresh ledger per input: a fiat asset (USD) and V-Bills (VBL); the fiat
 * issuer, the reserve-floor account, the vault cash account, two sellers
 * (fiat + V-Bill account each), a fiat sink, the V-Bill issuer, a second
 * V-Bill issuer and the vault V-Bill account. Ops: SWEEP (a reserve share),
 * SWEEP_FEE (a fee through pay_assure_split), BUY (any seller accounts,
 * including the vault's own and invalid ids), FIRST_LOSS, INIT probes over
 * random account quadruples, pure S1/S2/S4 checks, and outside postings
 * (V-Bill issue / redeem / transfer, reserve top-up and drain, and hostile
 * deposits straight into the vault's accounts, which svault_check must
 * detect), with edge-biased amounts.
 * Invariants after every op:
 *   S1' an independent model of the carry: share_total, carry < 3,
 *       swept_total + overflow_total == floor(share_total / 3), and the
 *       status (OK / OVERFLOW at room 0 / ZERO for part 0) agree with svault
 *   S2' a buy never takes the vault above floor(N / 3): after a buy of
 *       qty > 0, h <= floor(N / 3); qty == min(max_qty, room, cash / price)
 *   S3' buy postings move exactly cost fiat cash -> seller and qty V-Bills
 *       seller -> vault; cash only ever comes from sweeps
 *   S4' zxv_verify_vault_solvency agrees with an independent 192-bit
 *       reference; svault_redemption_floor is reserve / (N - h)
 *   S5' first loss burns min(loss, h) vault V-Bills at par (N and h fall by
 *       it), never touches the reserve or the cash, reports the rest
 *   S6  svault_check passes after every op (and fails after a hostile
 *       deposit into the vault's accounts); pay_ledger_check passes
 *   C   conservation: fiat supply moves only by outside issuance; N moves
 *       only by outside issue / redeem and first loss
 *   R   a refused op (status < 0) changes no balance and no counter
 *   I   svault_init accepts only what svault.h says it accepts
 *   D   determinism: the same input twice gives the same ledger and vault */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "svault.h"
#include "fuzz_in.h"

static pay_ledger_t g_L;
static svault_t g_V;
static unsigned long long g_ops;
static unsigned long long g_hit[32]; /* ops that succeeded, per op code (FUZZ_PROP_STATS) */

enum { A_FISS, A_RES, A_CASH, A_SF0, A_SF1, A_SINK, A_VISS, A_VISS2, A_VAULT, A_SB0, A_SB1, NA };
static uint32_t g_acc[NA];
static uint16_t g_usd, g_vbl;
static uint64_t g_nf;

static pay_status_t raw(uint32_t a, int64_t da, int64_t ca, uint32_t b, int64_t db, int64_t cb)
{
    pay_posting_req_t r;
    pay_receipt_t rc;
    memset(&r, 0, sizeof r);
    g_nf++;
    uint8_t rnd[16] = {0};
    memcpy(rnd, &g_nf, sizeof g_nf);
    rnd[15] = 0x3C;
    pay_uetr_from_random(rnd, r.uetr);
    snprintf(r.e2e, sizeof r.e2e, "FZ-%llu", (unsigned long long) g_nf);
    memcpy(r.idem_key, &g_nf, sizeof g_nf);
    r.idem_key[31] = 0xEE;
    r.lines[0] = (pay_line_t){a, da, ca};
    r.lines[1] = (pay_line_t){b, db, cb};
    r.n_lines = 2;
    return pay_ledger_post(&g_L, &r, &rc);
}

static void mkreq(pay_posting_req_t *r)
{
    memset(r, 0, sizeof *r);
    g_nf++;
    uint8_t rnd[16] = {0};
    memcpy(rnd, &g_nf, sizeof g_nf);
    rnd[15] = 0x77;
    pay_uetr_from_random(rnd, r->uetr);
    snprintf(r->e2e, sizeof r->e2e, "SV-%llu", (unsigned long long) g_nf);
    memcpy(r->idem_key, &g_nf, sizeof g_nf);
    r->idem_key[31] = 0x11;
    r->tick = g_nf;
    r->ext_cap = PAY_CAP_FINANCIAL;
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
    if (s < 0xA0u) return (uint64_t) fz_u16(in) % 20000u + (s & 1u);
    return fz_edge64(in);
}
static uint64_t D(uint32_t i)
{
    return g_L.acct[g_acc[i]].debit;
}
static uint64_t C(uint32_t i)
{
    return g_L.acct[g_acc[i]].credit;
}

typedef struct {
    uint64_t d[NA], c[NA], lseq;
    uint64_t carry, share, swept, over, spent, bought, burned;
} snap_t;
static snap_t g_b, g_a;

static void snap(snap_t *s)
{
    for (uint32_t i = 0; i < NA; i++) {
        s->d[i] = D(i);
        s->c[i] = C(i);
    }
    s->lseq = g_L.seq;
    s->carry = g_V.carry;
    s->share = g_V.share_total;
    s->swept = g_V.swept_total;
    s->over = g_V.overflow_total;
    s->spent = g_V.spent_total;
    s->bought = g_V.vbills_bought;
    s->burned = g_V.vbills_burned;
}
static bool same(const snap_t *x, const snap_t *y)
{
    return !memcmp(x, y, sizeof *x);
}

/* independent model of S1 */
static uint64_t m_share, m_carry, m_swept, m_over;

/* S4 reference: reserve * fd >= fn * (n - h) * backing, in 192 bits */
static bool ref_solvency(uint64_t reserve, uint64_t n, uint64_t h, uint64_t fn, uint64_t fd,
                         uint64_t backing)
{
    if (fd == 0 || h > n) return false;
    const __uint128_t M = ~(uint64_t) 0;
    __uint128_t lhs = (__uint128_t) reserve * fd;
    __uint128_t t = (__uint128_t) fn * (n - h);
    __uint128_t lo = (t & M) * backing, hi = (t >> 64) * backing;
    __uint128_t mid = (lo >> 64) + (hi & M);
    __uint128_t top = (hi >> 64) + (mid >> 64);
    if (top) return false;
    __uint128_t rhs = ((mid & M) << 64) | (lo & M);
    return lhs >= rhs;
}

static void checks(void)
{
    if (!svault_check(&g_V, &g_L)) abort(); /* S6 */
    if (!pay_ledger_check(&g_L)) abort();
    /* S1' */
    if (g_V.share_total != m_share || g_V.carry != m_carry || g_V.swept_total != m_swept ||
        g_V.overflow_total != m_over)
        abort();
    if (g_V.carry >= 3 || g_V.swept_total + g_V.overflow_total != g_V.share_total / 3) abort();
    if (g_V.vbills_burned > g_V.vbills_bought || g_V.spent_total > g_V.swept_total) abort();
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
        uint8_t fl = (i == A_FISS || i == A_VISS || i == A_VISS2) ? PAY_ACCT_ISSUER : 0;
        /* each user's two accounts share one owner (100 + k) */
        uint32_t owner = i >= A_SB0 ? 100u + (i - A_SB0) : (i == A_SF0 || i == A_SF1) ? 100u + (i - A_SF0) : 10u + i;
        if (pay_ledger_open(&g_L, owner, as, PAY_CAP_FINANCIAL, fl, &g_acc[i]) != PAY_OK) abort();
    }
    g_nf = 0;
    uint64_t r0 = (uint64_t) fz_u16(&in) * 1000u, n0 = fz_u16(&in);
    if (raw(g_acc[A_RES], (int64_t) r0, 0, g_acc[A_FISS], 0, (int64_t) r0) != PAY_OK ||
        raw(g_acc[A_SF0], 1000000, 0, g_acc[A_FISS], 0, 1000000) != PAY_OK ||
        raw(g_acc[A_SB0], (int64_t) n0, 0, g_acc[A_VISS], 0, (int64_t) n0) != PAY_OK ||
        raw(g_acc[A_SB1], (int64_t) n0, 0, g_acc[A_VISS], 0, (int64_t) n0) != PAY_OK)
        abort();
    if (svault_init(&g_V, &g_L, g_acc[A_RES], g_acc[A_CASH], g_acc[A_VISS], g_acc[A_VAULT]) !=
        SVAULT_OK)
        abort();
    m_share = m_carry = m_swept = m_over = 0;
    checks();

    while (in.n) {
        uint8_t op = fz_u8(&in) % 13u;
        g_ops++;
        snap(&g_b);
        uint64_t fsup = C(A_FISS), N0 = C(A_VISS), h0 = D(A_VAULT), cash0 = D(A_CASH),
                 res0 = D(A_RES);
        uint64_t minted = 0, dn = 0; /* expected change of fiat supply / N (mod 2^64) */
        svault_status_t st = SVAULT_OK;
        pay_posting_req_t req;
        mkreq(&req);
        switch (op) {
        case 0:
        case 1: { /* SWEEP / SWEEP_FEE */
            uint64_t x = amt(&in), share = x;
            if (op == 1) {
                uint64_t parts[PAY_ASSURE_BUCKETS];
                pay_assure_split(x, parts);
                share = parts[PAY_ASSURE_RESERVE_FLOOR];
                st = svault_sweep_fee(&g_V, &g_L, &req, x);
            } else
                st = svault_sweep(&g_V, &g_L, &req, x);
            uint64_t tot = m_share + share;
            if (tot < m_share) { /* would overflow */
                if (st != SVAULT_ERR_OVERFLOW) abort();
                break;
            }
            uint64_t xx = share + m_carry; /* share_total >= carry: no wrap here */
            uint64_t part = xx / 3, rem = xx % 3;
            uint64_t cap = N0 / 3, room = h0 < cap ? cap - h0 : 0;
            if (st < 0) {
                /* only the ledger may refuse, and only for a real transfer */
                if (st != SVAULT_ERR_LEDGER || !part || !room) abort();
                break;
            }
            if (part == 0) {
                if (st != SVAULT_ZERO) abort();
            } else if (room == 0) {
                if (st != SVAULT_OVERFLOW) abort();
                m_over += part;
            } else {
                if (st != SVAULT_OK || D(A_RES) != res0 - part || D(A_CASH) != cash0 + part)
                    abort();
                m_swept += part;
            }
            if (st != SVAULT_OK && (D(A_RES) != res0 || D(A_CASH) != cash0)) abort();
            m_carry = rem;
            m_share = tot;
            break;
        }
        case 2: { /* BUY */
            uint8_t w = fz_u8(&in);
            uint32_t sf = (w & 0xE0u) == 0xE0u ? pick(&in) : g_acc[A_SF0 + (w & 1u)];
            uint32_t sb = (w & 0xC0u) == 0xC0u ? pick(&in) : g_acc[A_SB0 + ((w >> 1) & 1u)];
            uint64_t price = (w & 0x10u) ? amt(&in) : fz_u8(&in) % 50u + 1u;
            uint64_t maxq = amt(&in);
            uint64_t sf0 = sf < g_L.n_accounts ? g_L.acct[sf].debit : 0;
            uint64_t sb0 = sb < g_L.n_accounts ? g_L.acct[sb].debit : 0;
            st = svault_buy(&g_V, &g_L, &req, sf, sb, price, maxq);
            if (st < 0) break;
            uint64_t cap = N0 / 3, room = h0 < cap ? cap - h0 : 0;
            uint64_t q = cash0 / price;
            if (q > room) q = room;
            if (q > maxq) q = maxq;
            if (q == 0) {
                if (st != SVAULT_ZERO) abort();
                break;
            }
            uint64_t cost = q * price;
            if (st != SVAULT_OK || g_V.last_qty != q || g_V.last_cost != cost) abort();
            if (D(A_CASH) != cash0 - cost || D(A_VAULT) != h0 + q) abort(); /* S3' */
            if (sf == sb || g_L.acct[sf].debit != sf0 + cost || g_L.acct[sb].debit != sb0 - q)
                abort();
            if (D(A_VAULT) > C(A_VISS) / 3) abort(); /* S2' */
            break;
        }
        case 3: { /* FIRST LOSS */
            uint64_t loss = amt(&in), unc = 0xDEADu;
            st = svault_first_loss(&g_V, &g_L, &req, loss, &unc);
            uint64_t b = loss < h0 ? loss : h0;
            if (st < 0) {
                if (unc != loss) abort();
                break;
            }
            if (b == 0) {
                if (st != SVAULT_ZERO || unc != loss) abort();
                break;
            }
            if (st != SVAULT_OK || unc != loss - b) abort();
            if (D(A_VAULT) != h0 - b || C(A_VISS) != N0 - b) abort(); /* S5' */
            if (D(A_RES) != res0 || D(A_CASH) != cash0) abort();
            dn = 0 - b;
            break;
        }
        case 4: { /* outside: V-Bills issued to a seller */
            uint64_t a = amt(&in);
            uint8_t w = fz_u8(&in);
            /* w & 0x80: a second issuer of the same V-Bill asset (not in N) */
            uint32_t iss = (w & 0x80u) ? g_acc[A_VISS2] : g_acc[A_VISS];
            if (raw(g_acc[A_SB0 + (w & 1u)], (int64_t) a, 0, iss, 0, (int64_t) a) == PAY_OK &&
                iss == g_acc[A_VISS])
                dn = a;
            break;
        }
        case 5: { /* outside: a seller redeems V-Bills (N falls; h may pass N/3) */
            uint64_t a = amt(&in);
            if (raw(g_acc[A_SB0 + fz_u8(&in) % 2u], (int64_t) (0 - a), 0, g_acc[A_VISS], 0,
                    (int64_t) (0 - a)) == PAY_OK)
                dn = 0 - a;
            break;
        }
        case 6: { /* outside: reserve top-up */
            uint64_t a = amt(&in);
            if (raw(g_acc[A_RES], (int64_t) a, 0, g_acc[A_FISS], 0, (int64_t) a) == PAY_OK)
                minted = a;
            break;
        }
        case 7: { /* outside: reserve drained to a sink */
            uint64_t a = amt(&in);
            raw(g_acc[A_RES], (int64_t) (0 - a), 0, g_acc[A_SINK], (int64_t) a, 0);
            break;
        }
        case 8: { /* pure S1 / S2 / S4 */
            uint64_t a = fz_edge64(&in), b = fz_edge64(&in), c = fz_edge64(&in),
                     d = fz_edge64(&in), e = fz_edge64(&in), f = fz_edge64(&in);
            uint64_t part = 7, rem = 7;
            bool ok = svault_carry(a % 4u, b, &part, &rem);
            uint64_t ri = a % 4u;
            if (ok != (ri < 3 && b + ri >= b)) abort();
            if (ok && (part != (b + ri) / 3 || rem != (b + ri) % 3)) abort();
            if (!ok && (part != 7 || rem != 7)) abort();
            if (svault_cap(a) != a / 3) abort();
            if (zxv_verify_vault_solvency(a, b, c, d, e, f) != ref_solvency(a, b, c, d, e, f))
                abort();
            uint64_t nu = 1, de = 1;
            svault_status_t fs = svault_redemption_floor(a, b, c, &nu, &de);
            if (c > b ? fs != SVAULT_ERR_ARG
                      : (b == c ? (fs != SVAULT_ZERO || nu || de) : (fs != SVAULT_OK || nu != a ||
                                                                     de != b - c)))
                abort();
            /* from the ledger */
            uint64_t fn = fz_u8(&in), fd = fz_u8(&in), bk = fz_u8(&in);
            if (svault_verify_solvency(&g_V, &g_L, fn, fd, bk) !=
                ref_solvency(D(A_RES), C(A_VISS), D(A_VAULT), fn, fd, bk))
                abort();
            uint64_t room = svault_room(&g_V, &g_L), cap = C(A_VISS) / 3;
            if (room != (D(A_VAULT) < cap ? cap - D(A_VAULT) : 0)) abort();
            st = SVAULT_ZERO;
            break;
        }
        case 9: { /* outside: V-Bills move between sellers */
            uint64_t a = amt(&in);
            raw(g_acc[A_SB0], (int64_t) (0 - a), 0, g_acc[A_SB1], (int64_t) a, 0);
            break;
        }
        case 10: { /* INIT probe over random accounts */
            uint32_t r = pick(&in), c = pick(&in), i = pick(&in), b = pick(&in);
            svault_t t;
            svault_status_t s2 = svault_init(&t, &g_L, r, c, i, b);
            if (s2 == SVAULT_OK) {
                const pay_account_t *R = pay_ledger_account(&g_L, r), *Cc = pay_ledger_account(&g_L, c),
                                    *I = pay_ledger_account(&g_L, i), *B = pay_ledger_account(&g_L, b);
                if (!R || !Cc || !I || !B || r == c || i == b || R->asset != Cc->asset ||
                    I->asset != B->asset || R->asset == I->asset || !(I->flags & PAY_ACCT_ISSUER) ||
                    ((R->flags | Cc->flags | B->flags) & PAY_ACCT_ISSUER) || Cc->debit || B->debit)
                    abort(); /* I */
                if (!svault_check(&t, &g_L)) abort();
            } else if (s2 > 0 || t.ready)
                abort();
            st = SVAULT_ZERO;
            break;
        }
        case 11: { /* hostile: fiat straight into vault cash; S6 must notice */
            uint64_t a = fz_u8(&in) % 100u + 1u;
            if (raw(g_acc[A_SF0], -(int64_t) a, 0, g_acc[A_CASH], (int64_t) a, 0) == PAY_OK) {
                if (svault_check(&g_V, &g_L)) abort();
                if (raw(g_acc[A_CASH], -(int64_t) a, 0, g_acc[A_SF0], (int64_t) a, 0) != PAY_OK)
                    abort();
            }
            break;
        }
        default: { /* hostile: V-Bills straight into the vault; S6 must notice */
            uint64_t a = fz_u8(&in) % 100u + 1u;
            if (raw(g_acc[A_SB0], -(int64_t) a, 0, g_acc[A_VAULT], (int64_t) a, 0) == PAY_OK) {
                if (svault_check(&g_V, &g_L)) abort();
                if (raw(g_acc[A_VAULT], -(int64_t) a, 0, g_acc[A_SB0], (int64_t) a, 0) != PAY_OK)
                    abort();
            }
            break;
        }
        }
        if (st == SVAULT_OK) g_hit[op]++;
        snap(&g_a);
        if (st < 0) {
            /* R: last_ledger / last_qty / last_cost are diagnostics, not state */
            if (!same(&g_a, &g_b)) abort();
        }
        if (C(A_FISS) != fsup + minted) abort();      /* C */
        if (C(A_VISS) - N0 != dn) abort();             /* C */
        checks();
    }
    if (!pay_ledger_verify_chain(&g_L)) abort(); /* once per input: it is O(journal) */
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 8192) return 0;
    static pay_account_t acct1[PAY_MAX_ACCOUNTS];
    static svault_t v1;
    static uint8_t head1[PAY_HASH_LEN];
    unsigned long long ops = g_ops;
    run(data, size);
    memcpy(acct1, g_L.acct, sizeof acct1);
    v1 = g_V;
    memcpy(head1, g_L.chain_head, sizeof head1);
    run(data, size);
    g_ops = ops + (g_ops - ops) / 2;
    /* D */
    if (memcmp(acct1, g_L.acct, sizeof acct1) || memcmp(head1, g_L.chain_head, sizeof head1) ||
        v1.share_total != g_V.share_total || v1.carry != g_V.carry ||
        v1.spent_total != g_V.spent_total || v1.vbills_bought != g_V.vbills_bought ||
        v1.vbills_burned != g_V.vbills_burned || v1.overflow_total != g_V.overflow_total)
        abort();
    return 0;
}

__attribute__((destructor)) static void report(void)
{
    if (getenv("FUZZ_PROP_STATS")) fprintf(stderr, "fuzz_econ_svault: %llu ops checked\n", g_ops);
    if (getenv("FUZZ_PROP_STATS")) {
        fprintf(stderr, "  succeeded per op:");
        for (int i = 0; i < 32; i++)
            if (g_hit[i]) fprintf(stderr, " %d:%llu", i, g_hit[i]);
        fprintf(stderr, "\n");
    }
}
