/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* fuzz_econ_pay.c — state-machine property harness for the payment ledger
 * (pay/pay_ledger.c).
 *
 * A fresh ledger per input: USD (2 decimals) and VFV, an issuer, three
 * holders, a commons pool, a frozen holder and two Crown-form (Social
 * capital) accounts of different owners. The input is an op sequence:
 *   ISSUE / REDEEM / TRANSFER / FEE payment (pay_with_fee) / REVERSE an earlier posting /
 *   RAW post (1..8 arbitrary lines, edge-valued d_debit and d_credit) /
 *   SET_FLAGS / REPLAY (same idempotency key, same or a different request)
 * with edge-biased amounts (0, 1, 2^62, 2^63, -1, INT64_MIN, ...).
 * Invariants after every op:
 *   P1  pay_ledger_check: L2 + L3 hold (per asset and form sum DEBIT == sum
 *       CREDIT, EQUITY == DEBIT - CREDIT, no negative rails, CREDIT only on
 *       issuers, balances < 2^62)
 *   P2  a posting that returns PAY_OK changed each account by exactly the
 *       sum of its journal lines, and nothing else; so sum(DEBIT) moved by
 *       sum(d_debit) == sum(d_credit) == minted - burned
 *   P3  anything else (an error or PAY_DUPLICATE) changed no balance and
 *       did not advance the sequence (L4 all-or-nothing, R2 idempotency)
 *   P4  the provenance hash chain verifies */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pay_ledger.h"
#include "fuzz_in.h"

static pay_ledger_t g_L;
static unsigned long long g_ops;

enum { A_ISS, A_H1, A_H2, A_H3, A_COMMONS, A_FROZEN, A_CROWN1, A_CROWN2, A_VISS, A_VH1, NA };
static uint32_t g_acc[NA];

typedef struct {
    uint64_t d[PAY_MAX_ACCOUNTS], c[PAY_MAX_ACCOUNTS];
    uint64_t seq;
} snap_t;
static snap_t g_b, g_a;

static void snap(snap_t *s)
{
    for (uint32_t i = 0; i < g_L.n_accounts; i++) {
        s->d[i] = g_L.acct[i].debit;
        s->c[i] = g_L.acct[i].credit;
    }
    s->seq = g_L.seq;
}

static uint32_t pick(fz_in *in)
{
    uint8_t b = fz_u8(in);
    if (b >= 0xF8u) return b == 0xFFu ? 0xFFFFFFFFu : (uint32_t) PAY_MAX_ACCOUNTS + b; /* invalid */
    return g_acc[b % NA];
}

static int64_t edge_s(fz_in *in)
{
    return (int64_t) fz_edge64(in);
}

static uint32_t g_n;
static char g_uetrs[64][PAY_UETR_LEN + 1];
static uint32_t g_nuetr;

static void mkreq(pay_posting_req_t *r, fz_in *in)
{
    memset(r, 0, sizeof *r);
    uint8_t rnd[16];
    g_n++;
    for (int i = 0; i < 16; i++)
        rnd[i] = (uint8_t) (((uint64_t) g_n * 2654435761u) >> (i % 4 * 8)) ^ (uint8_t) i;
    memcpy(rnd, &g_n, sizeof g_n);
    pay_uetr_from_random(rnd, r->uetr);
    snprintf(r->e2e, sizeof r->e2e, "E2E-%u", g_n);
    memcpy(r->idem_key, &g_n, sizeof g_n);
    r->idem_key[31] = 0x5A;
    r->initiator = fz_u8(in) & 1u ? 1u : 100u;
    r->tick = g_n;
    r->attestor = 7;
    r->ext_cap = (uint8_t) (fz_u8(in) % (PAY_CAP_COUNT + 1));
    r->ext_value = (fz_u8(in) & 3u) == 0 ? edge_s(in) : 0;
}

static void check_after(pay_status_t st, const pay_posting_req_t *r)
{
    if (!pay_ledger_check(&g_L)) abort(); /* P1 */
    for (uint32_t a = 0; a < g_L.n_assets; a++)
        for (uint32_t c = 0; c < PAY_CAP_COUNT; c++) {
            uint64_t d, cr;
            int64_t e;
            pay_ledger_totals(&g_L, (uint16_t) a, (pay_cap_t) c, &d, &cr, &e);
            if (d != cr || e != 0) abort(); /* L2 */
        }
    snap(&g_a);
    if (st == PAY_OK) {
        const pay_journal_t *j = pay_ledger_find_uetr(&g_L, r->uetr);
        if (!j || g_a.seq != g_b.seq + 1) abort();
        __int128 want_d[PAY_MAX_ACCOUNTS], want_c[PAY_MAX_ACCOUNTS];
        __int128 sum_dd = 0, sum_dc = 0, tot_b = 0, tot_a = 0;
        for (uint32_t i = 0; i < g_L.n_accounts; i++) {
            want_d[i] = g_b.d[i];
            want_c[i] = g_b.c[i];
            tot_b += g_b.d[i];
            tot_a += g_a.d[i];
        }
        for (uint32_t k = 0; k < j->n_lines; k++) {
            uint32_t ac = j->lines[k].account;
            if (ac >= g_L.n_accounts) abort();
            want_d[ac] += j->lines[k].d_debit;
            want_c[ac] += j->lines[k].d_credit;
            sum_dd += j->lines[k].d_debit;
            sum_dc += j->lines[k].d_credit;
        }
        for (uint32_t i = 0; i < g_L.n_accounts; i++)
            if (want_d[i] != g_a.d[i] || want_c[i] != g_a.c[i]) abort(); /* P2 */
        if (sum_dd != sum_dc || tot_a != tot_b + sum_dd) abort(); /* supply: minted - burned */
        if (g_nuetr < 64) memcpy(g_uetrs[g_nuetr++], r->uetr, sizeof g_uetrs[0]);
    } else {
        if (g_a.seq != g_b.seq) abort(); /* P3 */
        for (uint32_t i = 0; i < g_L.n_accounts; i++)
            if (g_a.d[i] != g_b.d[i] || g_a.c[i] != g_b.c[i]) abort();
    }
    if (!pay_ledger_verify_chain(&g_L)) abort(); /* P4 */
}

static pay_posting_req_t g_req, g_last;

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 8192) return 0;
    fz_in in;
    fz_init(&in, data, size);
    pay_ledger_init(&g_L, NULL);
    uint16_t usd = 0;
    if (pay_ledger_add_fiat(&g_L, "USD", 840, 2, &usd) != PAY_OK) abort();
    uint16_t vfv = g_L.vfv_asset;
    if (pay_ledger_open(&g_L, 100, usd, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER, &g_acc[A_ISS]) ||
        pay_ledger_open(&g_L, 1, usd, PAY_CAP_FINANCIAL, 0, &g_acc[A_H1]) ||
        pay_ledger_open(&g_L, 2, usd, PAY_CAP_FINANCIAL, 0, &g_acc[A_H2]) ||
        pay_ledger_open(&g_L, 3, usd, PAY_CAP_FINANCIAL, 0, &g_acc[A_H3]) ||
        pay_ledger_open(&g_L, 999, usd, PAY_CAP_FINANCIAL, PAY_ACCT_COMMONS, &g_acc[A_COMMONS]) ||
        pay_ledger_open(&g_L, 4, usd, PAY_CAP_FINANCIAL, PAY_ACCT_FROZEN, &g_acc[A_FROZEN]) ||
        pay_ledger_open(&g_L, 1, usd, PAY_CAP_SOCIAL, PAY_ACCT_ISSUER, &g_acc[A_CROWN1]) ||
        pay_ledger_open(&g_L, 2, usd, PAY_CAP_SOCIAL, 0, &g_acc[A_CROWN2]) ||
        pay_ledger_open(&g_L, 100, vfv, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER, &g_acc[A_VISS]) ||
        pay_ledger_open(&g_L, 1, vfv, PAY_CAP_FINANCIAL, 0, &g_acc[A_VH1]))
        abort();
    g_n = 0;
    g_nuetr = 0;
    bool have_last = false;

    while (in.n) {
        uint8_t op = fz_u8(&in) % 8u;
        pay_receipt_t rc;
        pay_status_t st;
        g_ops++;
        snap(&g_b);
        if (op == 7 && have_last) {
            /* REPLAY: the same idempotency key, with the same request or with
             * one changed amount (R2: DUPLICATE or REPLAY, never a posting) */
            g_req = g_last;
            if (fz_u8(&in) & 1u) g_req.lines[0].d_debit ^= 1;
            st = pay_ledger_post(&g_L, &g_req, &rc);
            if (st == PAY_OK) abort();
            check_after(st, &g_req);
            continue;
        }
        mkreq(&g_req, &in);
        uint32_t x = pick(&in), y = pick(&in), z = pick(&in);
        uint64_t amt = fz_edge64(&in);
        switch (op) {
        case 0:
            st = pay_ledger_issue(&g_L, &g_req, x, y, amt, &rc);
            break;
        case 1:
            st = pay_ledger_redeem(&g_L, &g_req, x, y, amt, &rc);
            break;
        case 2:
            st = pay_ledger_transfer(&g_L, &g_req, x, y, amt, &rc);
            break;
        case 3: {
            /* a payment with the 0.08889% assurance fee split into four
             * bucket accounts (any of them may be invalid or repeated) */
            uint32_t bucket[PAY_ASSURE_BUCKETS];
            bucket[0] = z;
            for (uint32_t k = 1; k < PAY_ASSURE_BUCKETS; k++) bucket[k] = pick(&in);
            uint64_t excess = fz_edge64(&in), vfvc = fz_edge64(&in), fee = 0;
            st = pay_ledger_pay_with_fee(&g_L, &g_req, x, y, amt, bucket, excess, g_acc[A_VISS],
                                         g_acc[A_VH1], vfvc, &rc, &fee);
            /* the fee is 8889/10^7 of the amount, plus at most one unit of
             * carried remainder: never more than the amount */
            if (st == PAY_OK && amt > 0 && fee > amt) abort();
            break;
        }
        case 4: {
            const char *u = g_nuetr ? g_uetrs[fz_u8(&in) % g_nuetr] : "none";
            st = pay_ledger_reverse(&g_L, &g_req, u, &rc);
            break;
        }
        case 5: {
            uint32_t nl = (uint32_t) (fz_u8(&in) % (PAY_MAX_LINES + 2));
            g_req.n_lines = nl;
            for (uint32_t k = 0; k < nl && k < PAY_MAX_LINES; k++) {
                g_req.lines[k].account = pick(&in);
                g_req.lines[k].d_debit = edge_s(&in);
                g_req.lines[k].d_credit = edge_s(&in);
            }
            g_req.kind = (uint8_t) (fz_u8(&in) % 8u);
            st = pay_ledger_post(&g_L, &g_req, &rc);
            break;
        }
        case 6:
            st = pay_ledger_set_flags(&g_L, x, (uint8_t) amt);
            if (st == PAY_OK) {
                /* flags changed, balances must not have */
                st = PAY_ERR_STATE; /* checked as a non-posting below */
            }
            break;
        default:
            st = pay_ledger_transfer(&g_L, &g_req, x, y, amt, &rc);
            break;
        }
        check_after(st, &g_req);
        if (st == PAY_OK) {
            g_last = g_req;
            have_last = true;
        }
    }
    return 0;
}

__attribute__((destructor)) static void report(void)
{
    if (getenv("FUZZ_PROP_STATS")) fprintf(stderr, "fuzz_econ_pay: %llu ops checked\n", g_ops);
}
