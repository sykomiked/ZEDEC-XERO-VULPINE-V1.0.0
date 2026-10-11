/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* myra_demo — a global map and its MYRA twin, exact values from M value
 * strings, and payments posted to a libzxv ledger through a capability.
 * All data is synthetic.
 *
 *   myra_demo examples/payment.gmap examples/payment.myra */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zmyra.h"

static zm_schema *load(const char *path) {
    FILE *f = fopen(path, "rb");
    char *t;
    size_t n;
    zm_schema *s = NULL;
    zm_diag d;
    if (!f) return NULL;
    t = (char *) malloc(1u << 20);
    n = t ? fread(t, 1, (1u << 20) - 1u, f) : 0;
    fclose(f);
    if (t && zm_schema_parse(t, n, &s, &d) != ZXV_OK) fprintf(stderr, "%s: %s\n", path, d.msg);
    free(t);
    return s;
}

static const char *err(void) {
    static char b[600];
    size_t n = 0;
    if (zm_last_error(b, sizeof b, &n) != ZXV_OK) b[0] = '\0';
    return b;
}

int main(int argc, char **argv) {
    zm_schema *g, *m;
    uint8_t dg[32];
    int32_t same = 0, st;
    zxv_ctx *ctx = NULL;
    zxv_cap *root = NULL, *poster = NULL;
    uint32_t bank = 0, alice = 0, bob = 0;
    zm_record *r = NULL;
    char b[256];
    size_t n = 0;
    static const zm_posting_map map = {"amount", "from", "to", "ccy", NULL, "ref", -1};
    if (argc != 3) {
        fprintf(stderr, "usage: myra_demo MAP.gmap MAP.myra\n");
        return 2;
    }
    g = load(argv[1]);
    m = load(argv[2]);
    if (!g || !m) return 1;
    zm_schema_same(g, m, &same);
    zm_schema_digest(g, dg);
    printf("%s and %s declare %s: sha3-256 %02x%02x%02x%02x...\n", argv[1], argv[2],
           same ? "the same schema" : "DIFFERENT schemas", dg[0], dg[1], dg[2], dg[3]);

    /* exact values */
    st = zm_node_decode(m, "^ZXPAY(7,2)", 11, "PAY-0001^250.25^NGN^1^2^67854^N", 31, &r);
    if (st == ZXV_OK) {
        int64_t u = 0, num = 0, den = 0;
        uint32_t sc = 0;
        zm_get_decimal(r, "amount", &u, &sc);
        zm_get_rat(r, "amount", &num, &den);
        zm_get_value(r, "value-date", b, sizeof b, &n);
        printf("^ZXPAY(7,2) amount 250.25 = %lld / 10^%u = %lld/%lld, value-date $H 67854 = %s\n",
               (long long) u, (unsigned) sc, (long long) num, (long long) den, b);
        st = zm_put_value(r, "amount", "1/3", 3);
        printf("1/3 into amount: %s (never rounded)\n", zm_status_name(st));
        zm_put_value(r, "amount", "0250.50", 7);
        zm_record_encode(r, b, sizeof b, &n);
        printf("0250.50 into amount, M canonical value: %s\n", b);
    }
    zm_record_destroy(r);
    r = NULL;
    st = zm_node_decode(m, "^ZXPAY(7,3)", 11, "PAY-0002^12.50^NGN^1^2", 22, &r);
    printf("\"12.50\" in a value string: %s: %s\n", zm_status_name(st), err());

    /* the ledger */
    zxv_ctx_create(&ctx);
    zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &root);
    zxv_ccy_enable_cap(root, "NGN");
    zxv_ledger_open_account_cap(root, "bank", "NGN", ZXV_ACCT_ISSUER, &bank);
    zxv_ledger_open_account_cap(root, "alice", "NGN", ZXV_ACCT_HOLDER, &alice);
    zxv_ledger_open_account_cap(root, "bob", "NGN", ZXV_ACCT_HOLDER, &bob);
    zxv_cap_attenuate(root, ZXV_CAP_LEDGER_POST | ZXV_CAP_LEDGER_READ, &poster);
    {
        static const struct {
            const char *ref, *value;
        } pays[] = {
            {"^ZXPAY(7,1)", "FUND-ALICE^1000^NGN^%u^%u^67854^N"},
            {"^ZXPAY(7,2)", "PAY-0001^250.25^NGN^%u^%u^67854^N"},
            {"^ZXPAY(7,3)", "PAY-0002^5000^NGN^%u^%u^67854^N"},
            {"^ZXPAY(7,4)", "PAY-0001^1^NGN^%u^%u^67854^N"},
            {"^ZXPAY(7,5)", "PAY-0003^1.005^NGN^%u^%u^67854^N"},
            {"^ZXPAY(7,6)", "PAY-0004^12.34^USD^%u^%u^67854^N"},
        };
        for (size_t i = 0; i < sizeof pays / sizeof *pays; i++) {
            char v[128];
            zm_posting p;
            snprintf(v, sizeof v, pays[i].value, (unsigned) (i ? alice : bank),
                     (unsigned) (i ? bob : alice));
            zm_record_destroy(r);
            r = NULL;
            st = zm_node_decode(m, pays[i].ref, strlen(pays[i].ref), v, strlen(v), &r);
            if (st == ZXV_OK) st = zm_post(poster, r, &map, &p);
            if (st == ZXV_OK)
                printf("post %-12s %-28s -> entry %llu (%lld minor units)\n", pays[i].ref, v,
                       (unsigned long long) p.entry_id, (long long) p.amount_minor);
            else
                printf("post %-12s %-28s -> %s\n", pays[i].ref, v, zm_status_name(st));
        }
    }
    {
        int64_t eb = 0, ea = 0, eo = 0;
        zxv_ledger_balance_cap(poster, bank, NULL, NULL, &eb);
        zxv_ledger_balance_cap(poster, alice, NULL, NULL, &ea);
        zxv_ledger_balance_cap(poster, bob, NULL, NULL, &eo);
        printf("equity in kobo: bank %lld, alice %lld, bob %lld (sum %lld)\n", (long long) eb,
               (long long) ea, (long long) eo, (long long) (eb + ea + eo));
    }
    printf("ledger check: %s\n", zxv_status_name(zxv_ledger_check_cap(poster)));
    zm_record_destroy(r);
    zxv_cap_destroy(poster);
    zxv_cap_destroy(root);
    zxv_ctx_destroy(ctx);
    zm_schema_destroy(g);
    zm_schema_destroy(m);
    return same ? 0 : 1;
}
