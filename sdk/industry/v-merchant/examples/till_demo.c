/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* till_demo.c — a market stall's day: the till is offline, three customers
 * pay by signing on their own phones, one tries the same proof twice, one
 * proof is forged; back online the batch settles, one payment bounces and
 * its invoice reopens; the day closes with an exact report whose digest is
 * signed. Merchant side only: the customers' wallets are not part of it. */
#include <stdio.h>
#include <string.h>

#include "zvmerch.h"

#define T0 1760000000ULL

static int fail(const char *what, int32_t st) {
    char err[300];
    size_t n = 0;
    if (zind_last_error(err, sizeof err, &n) != ZXV_OK) err[0] = 0;
    fprintf(stderr, "till_demo: %s: %s (%s)\n", what, zvm_status_name(st), err);
    return 1;
}

#define TRY(what, expr)                                                                        \
    do {                                                                                       \
        int32_t st_ = (expr);                                                                  \
        if (st_ != ZXV_OK) return fail(what, st_);                                             \
    } while (0)

/* On the customer's phone, offline. */
static int32_t pay(const zxv_cap *cap, const uint8_t sk[32], const char *inv, int64_t amount,
                   uint32_t payer, uint64_t nonce, zvm_proof *p) {
    uint8_t m[160];
    size_t len = 0;
    int32_t st;
    memset(p, 0, sizeof *p);
    snprintf(p->merchant, sizeof p->merchant, "STALL-42");
    snprintf(p->invoice, sizeof p->invoice, "%s", inv);
    snprintf(p->ccy, sizeof p->ccy, "KES");
    p->amount = amount;
    p->payer = payer;
    p->nonce = nonce;
    p->time = T0 + 60;
    p->alg = ZVM_ALG_ED25519;
    p->sig_len = 64;
    st = zvm_pay_bytes(p, m, sizeof m, &len);
    if (st == ZXV_OK) st = zxv_ed25519_sign(cap, sk, m, len, p->sig);
    return st;
}

int main(void) {
    zxv_ctx *ctx = NULL;
    zxv_cap *cap = NULL;
    zxv_hexlog *qlog = NULL;
    zind_signer *till_key = NULL;
    zvm_terminal *t = NULL;
    uint8_t seed[32], sk[3][32], pk[3][32], wire[ZVM_PROOF_MAX], dig[32];
    uint32_t bank = 0, stall = 0, cust[3], inv = 0, pr = 0;
    const char *names[3] = {"amina", "brian", "chao"};
    zvm_proof p, q;
    zvm_batch b;
    size_t len = 0;
    char report[1024];
    int32_t st;

    for (int i = 0; i < 32; i++) {
        seed[i] = (uint8_t) (40 + i);
        for (int c = 0; c < 3; c++) sk[c][i] = (uint8_t) (50 + 10 * c + i);
    }
    TRY("context", zxv_ctx_create(&ctx));
    TRY("cap", zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap));
    TRY("currency", zxv_ccy_enable_cap(cap, "KES"));
    TRY("log", zxv_hexlog_create(cap, 64, &qlog));
    TRY("till key", zind_signer_from_seed(cap, seed, &till_key));
    TRY("bank", zxv_ledger_open_account_cap(cap, "bank", "KES", ZXV_ACCT_ISSUER, &bank));
    TRY("stall", zxv_ledger_open_account_cap(cap, "stall-42", "KES", ZXV_ACCT_HOLDER, &stall));
    TRY("till", zvm_terminal_create(cap, "STALL-42", till_key, qlog, &t));
    TRY("currency", zvm_ccy_add(t, "KES", stall));
    for (int c = 0; c < 3; c++) {
        TRY("customer key", zxv_ed25519_public(cap, sk[c], pk[c]));
        TRY("customer", zxv_ledger_open_account_cap(cap, names[c], "KES", ZXV_ACCT_HOLDER, &cust[c]));
        TRY("payer list", zvm_payer_add(t, cust[c], ZVM_ALG_ED25519, pk[c], 32, 0, NULL));
    }
    TRY("fund", zxv_ledger_post_cap(cap, bank, cust[0], 100000, "KES", "f-a", NULL));
    TRY("fund", zxv_ledger_post_cap(cap, bank, cust[1], 200000, "KES", "f-b", NULL));
    TRY("fund", zxv_ledger_post_cap(cap, bank, cust[2], 500, "KES", "f-c", NULL)); /* too little */

    printf("-- offline --\n");
    TRY("invoice", zvm_invoice_create(t, "T-001", "KES", 45050, T0, T0 + 900, &inv));
    TRY("pay", pay(cap, sk[0], "T-001", 45050, cust[0], 1, &p));
    TRY("encode", zvm_proof_encode(&p, wire, sizeof wire, &len)); /* as a QR code */
    TRY("decode", zvm_proof_decode(wire, len, &q));
    st = zvm_accept(t, &q, &pr);
    printf("T-001 450.50 KES from amina (%zu-byte proof): %s\n", len, zvm_status_name(st));
    st = zvm_accept(t, &q, &pr);
    printf("T-001 the same proof again: %s\n", zvm_status_name(st));

    TRY("invoice", zvm_invoice_create(t, "T-002", "KES", 120000, T0, T0 + 900, &inv));
    TRY("pay", pay(cap, sk[0], "T-002", 120000, cust[1], 1, &p)); /* signed with amina's key */
    st = zvm_accept(t, &p, &pr);
    printf("T-002 1200.00 KES claiming to be brian, signed by another phone: %s\n",
           zvm_status_name(st));
    TRY("pay", pay(cap, sk[1], "T-002", 120000, cust[1], 1, &p));
    st = zvm_accept(t, &p, &pr);
    printf("T-002 1200.00 KES from brian: %s\n", zvm_status_name(st));

    TRY("invoice", zvm_invoice_create(t, "T-003", "KES", 9900, T0, T0 + 900, &inv));
    TRY("pay", pay(cap, sk[2], "T-003", 9900, cust[2], 1, &p));
    st = zvm_accept(t, &p, &pr);
    printf("T-003 99.00 KES from chao: %s (verified offline; funds unknown until online)\n",
           zvm_status_name(st));

    st = zvm_eod_close(t, 20261012, dig);
    printf("close the day before settling: %s\n", zvm_status_name(st));

    printf("-- online --\n");
    TRY("settle", zvm_settle(t, &b));
    printf("batch: %u settled, %u bounced, %u duplicates\n", b.settled, b.bounced, b.duplicates);
    TRY("state", zvm_invoice_state(t, inv, &st, NULL));
    printf("T-003 is open again: %s\n", st == ZVM_INV_OPEN ? "yes" : "no");
    TRY("report", zvm_eod_report(t, 20261012, report, sizeof report, &len));
    printf("%s", report);
    TRY("close", zvm_eod_close(t, 20261012, dig));
    {
        const uint8_t *trust[1] = {zind_signer_pk(till_key)};
        uint32_t n = 0, nq = 0;
        TRY("records", zind_chain_verify(cap, zvm_records(t), trust, 1, NULL));
        zind_chain_count(zvm_records(t), &n);
        zxv_hexlog_count(qlog, &nq);
        printf("day closed; report digest %02x%02x%02x%02x..., %u signed records verify, %u "
               "quarantine records\n",
               dig[0], dig[1], dig[2], dig[3], n, nq);
    }
    zvm_terminal_destroy(t);
    zind_signer_destroy(till_key);
    zxv_hexlog_destroy(qlog);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
    return 0;
}
