/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sutra_demo.c — a copybook and its Sutra twin declare the same record;
 * payment records built from exact text are posted to a libzxv ledger,
 * and every refusal is shown with its status.
 *
 *   sutra_demo examples/payment.cpy examples/payment.sutra */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zsutra.h"

static char *slurp(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb");
    char *b;
    long k;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    k = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = (char *) malloc((size_t) k + 1u);
    if (b && fread(b, 1, (size_t) k, f) != (size_t) k) {
        free(b);
        b = NULL;
    }
    if (b) b[k] = '\0';
    fclose(f);
    *n = (size_t) k;
    return b;
}

static zs_layout *load(const char *path, int is_sutra) {
    size_t n = 0;
    char *t = slurp(path, &n);
    zs_layout *l = NULL;
    zs_diag d;
    if (!t) {
        fprintf(stderr, "cannot read %s\n", path);
        exit(1);
    }
    if ((is_sutra ? zs_sutra_parse(t, n, &l, &d) : zs_copybook_parse(t, n, ZS_CB_FIXED, &l, &d)) !=
        ZXV_OK) {
        fprintf(stderr, "%s: %s\n", path, d.msg);
        exit(1);
    }
    free(t);
    return l;
}

static void must(int32_t st, const char *what) {
    if (st != ZXV_OK) {
        fprintf(stderr, "%s: %s\n", what, zs_status_name(st));
        exit(1);
    }
}

int main(int argc, char **argv) {
    zs_layout *cb, *su;
    uint8_t dg[32], rec[64];
    uint64_t size = 0;
    int32_t same = 0;
    zxv_ctx *ctx;
    zxv_cap *root, *poster;
    uint32_t bank, alice, bob;
    const zs_posting_map map = {"PAY-AMOUNT", "PAY-FROM", "PAY-TO", "PAY-CCY", NULL, "PAY-REF", -1};
    static const struct {
        const char *ref, *amount, *ccy;
        int from, to; /* 0 bank, 1 alice, 2 bob */
    } pays[] = {
        {"FUND-ALICE", "1000.00", "NGN", 0, 1},
        {"PAY-0001", "250.25", "NGN", 1, 2},
        {"PAY-0002", "5000.00", "NGN", 1, 2},  /* more than alice has */
        {"PAY-0001", "1.00", "NGN", 1, 2},     /* reference used before */
        {"PAY-0003", "0.00", "NGN", 1, 2},     /* nothing to move */
        {"PAY-0004", "12.34", "USD", 1, 2},    /* accounts are NGN */
    };
    if (argc != 3) {
        fprintf(stderr, "usage: sutra_demo PAYMENT.cpy PAYMENT.sutra\n");
        return 2;
    }
    cb = load(argv[1], 0);
    su = load(argv[2], 1);
    must(zs_layout_same(cb, su, &same), "same");
    zs_layout_size(cb, &size);
    zs_layout_digest(cb, dg);
    printf("%s and %s declare %s record: %llu bytes, sha3-256 %02x%02x%02x%02x...\n", argv[1],
           argv[2], same ? "the same" : "DIFFERENT", (unsigned long long) size, dg[0], dg[1], dg[2],
           dg[3]);
    if (!same || size > sizeof rec) return 1;

    /* exactness first: one value three ways */
    zs_record_init(su, rec, sizeof rec);
    must(zs_put_value(su, rec, sizeof rec, "PAY-AMOUNT", "-1234.56", 8), "put");
    {
        int64_t u, n, d;
        uint32_t sc;
        char hex[40];
        zs_get_decimal(cb, rec, sizeof rec, "PAY-AMOUNT", &u, &sc);
        zs_get_rat(cb, rec, sizeof rec, "PAY-AMOUNT", &n, &d);
        for (int i = 0; i < 8; i++) snprintf(hex + 2 * i, 3, "%02x", rec[16 + i]);
        printf("PAY-AMOUNT -1234.56 is COMP-3 bytes %s = %lld / 10^%u = %lld/%lld\n", hex,
               (long long) u, (unsigned) sc, (long long) n, (long long) d);
        printf("1/3 into PAY-AMOUNT: %s (never rounded)\n",
               zs_status_name(zs_put_rat(cb, rec, sizeof rec, "PAY-AMOUNT", 1, 3)));
    }

    /* a ledger, and a capability that may only post and read */
    must(zxv_ctx_create(&ctx), "ctx");
    must(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root), "cap");
    must(zxv_ccy_enable_cap(root, "NGN"), "NGN");
    must(zxv_ledger_open_account_cap(root, "bank", "NGN", ZXV_ACCT_ISSUER, &bank), "bank");
    must(zxv_ledger_open_account_cap(root, "alice", "NGN", ZXV_ACCT_HOLDER, &alice), "alice");
    must(zxv_ledger_open_account_cap(root, "bob", "NGN", ZXV_ACCT_HOLDER, &bob), "bob");
    must(zxv_cap_attenuate(root, ZXV_CAP_LEDGER_POST | ZXV_CAP_LEDGER_READ, &poster), "attenuate");
    for (size_t i = 0; i < sizeof pays / sizeof pays[0]; i++) {
        uint32_t ids[3] = {bank, alice, bob};
        zs_posting p;
        int32_t st;
        zs_record_init(cb, rec, sizeof rec);
        zs_put_value(cb, rec, sizeof rec, "PAY-REF", pays[i].ref, strlen(pays[i].ref));
        zs_put_value(cb, rec, sizeof rec, "PAY-AMOUNT", pays[i].amount, strlen(pays[i].amount));
        zs_put_value(cb, rec, sizeof rec, "PAY-CCY", pays[i].ccy, strlen(pays[i].ccy));
        zs_put_decimal(cb, rec, sizeof rec, "PAY-FROM", ids[pays[i].from], 0);
        zs_put_decimal(cb, rec, sizeof rec, "PAY-TO", ids[pays[i].to], 0);
        st = zs_post(poster, cb, &map, rec, sizeof rec, &p);
        if (st == ZXV_OK)
            printf("post %-10s %10s %s -> entry %llu (%lld minor units)\n", p.reference,
                   pays[i].amount, p.currency, (unsigned long long) p.entry_id,
                   (long long) p.amount_minor);
        else
            printf("post %-10s %10s %s -> %s\n", p.reference, pays[i].amount, p.currency,
                   zs_status_name(st));
    }
    {
        int64_t eb = 0, ea = 0, eo = 0;
        zxv_ledger_balance_cap(poster, bank, NULL, NULL, &eb);
        zxv_ledger_balance_cap(poster, alice, NULL, NULL, &ea);
        zxv_ledger_balance_cap(poster, bob, NULL, NULL, &eo);
        printf("equity in kobo: bank %lld, alice %lld, bob %lld (sum %lld)\n", (long long) eb,
               (long long) ea, (long long) eo, (long long) (eb + ea + eo));
        printf("ledger check: %s\n", zs_status_name(zxv_ledger_check_cap(poster)));
    }
    zxv_cap_destroy(poster);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
    zs_layout_destroy(cb);
    zs_layout_destroy(su);
    return 0;
}
