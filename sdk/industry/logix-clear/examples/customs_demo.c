/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* customs_demo.c — two containers from Shenzhen to Long Beach through a
 * Logix-Clear desk. One clears with an exact 5% duty; the other carries a
 * line one jurisdiction refuses and the other releases, so it is held as
 * BOTH until a (simulated) human broker signs a decision.
 *
 * The rule table and jurisdictions below are invented for the example: the
 * kit is not connected to any customs authority and knows no real tariff. */
#include <stdio.h>
#include <string.h>

#include "zlogix.h"

#define TRY(x)                                                                                 \
    do {                                                                                       \
        int32_t rc_ = (x);                                                                     \
        if (rc_ != ZXV_OK) {                                                                   \
            fprintf(stderr, "%s failed: %s\n", #x, zlc_status_name(rc_));                      \
            return 1;                                                                          \
        }                                                                                      \
    } while (0)

static void show(const zlc_desk *d, uint32_t id, const char *what) {
    static const char *names[] = {"?", "booked", "in transit", "HELD", "refused", "released",
                                  "closed"};
    int32_t st = 0;
    uint8_t tag = 0;
    zlc_shipment_state(d, id, &st, &tag);
    printf("  shipment %u %-22s state %-10s tag %s\n", id, what, names[st], zxv_hex_name(tag));
}

int main(void) {
    zxv_ctx *ctx = NULL;
    zxv_cap *cap = NULL;
    zxv_hexlog *qlog = NULL;
    zind_signer *desk_key = NULL, *broker = NULL;
    zlc_desk *d = NULL;
    uint32_t duty_cn = 0, duty_us = 0, issuer = 0, importer = 0, a = 0, b = 0, n = 0;
    uint8_t seed[32] = {1}, bseed[32] = {2}, msg[128], sig[ZIND_SIG_BYTES];
    size_t len = 0;
    const char *route[2] = {"CN", "US"};
    zlc_release_info info;
    zind_qty v, ex, res;
    int64_t posted = 0;
    char txt[64];
    int32_t rc;

    TRY(zxv_ctx_create(&ctx));
    TRY(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap));
    TRY(zxv_hexlog_create(cap, 64, &qlog));
    TRY(zind_signer_from_seed(cap, seed, &desk_key));
    TRY(zind_signer_from_seed(cap, bseed, &broker));
    TRY(zlc_desk_create(cap, "USD", desk_key, qlog, &d));
    TRY(zlc_jurisdiction_add(d, "CN", &duty_cn));
    TRY(zlc_jurisdiction_add(d, "US", &duty_us));
    TRY(zlc_broker_add(d, zind_signer_pk(broker)));
    {
        zlc_rule rules[] = {
            {"CN", "8471", "*", ZLC_RULE_RELEASE, {0, 1}},
            {"CN", "8542", "*", ZLC_RULE_REFUSE, {0, 1}}, /* an export control */
            {"US", "8471", "*", ZLC_RULE_RELEASE, {1, 20}},
            {"US", "8542", "*", ZLC_RULE_RELEASE, {0, 1}},
        };
        for (size_t i = 0; i < 4; i++) TRY(zlc_rule_add(d, &rules[i]));
    }
    TRY(zxv_ledger_open_account_cap(cap, "bank", "USD", ZXV_ACCT_ISSUER, &issuer));
    TRY(zxv_ledger_open_account_cap(cap, "importer", "USD", ZXV_ACCT_HOLDER, &importer));
    TRY(zxv_ledger_post_cap(cap, issuer, importer, 2000000, "USD", "deposit", NULL));

    printf("Logix-Clear demo (not connected to any customs authority)\n\n");
    printf("Container CSQU3054383: laptops, declared 1234.50 USD\n");
    TRY(zlc_book(d, "CSQU3054383", route, 2, importer, &a));
    TRY(zind_qty_parse("1234.50", &v));
    TRY(zlc_line_add(d, a, "84713000", "CN", v));
    show(d, a, "booked");
    TRY(zlc_depart(d, a));
    show(d, a, "departed");
    rc = zlc_release(d, a, &info);
    printf("  release under the exact rule: %s (5%% is 61.725, not a whole cent)\n",
           zlc_status_name(rc));
    TRY(zlc_set_rounding(d, ZIND_ROUND_HALF_EVEN));
    TRY(zlc_release(d, a, &info));
    TRY(zlc_duty(d, a, "US", &ex, &posted, &res));
    zind_qty_format(ex, txt, sizeof txt, &len);
    printf("  US duty exact %s, posted %lld cents half-even, residual ", txt, (long long) posted);
    zind_qty_format(res, txt, sizeof txt, &len);
    printf("%s recorded\n", txt);
    show(d, a, "released");
    TRY(zlc_close(d, a));
    show(d, a, "closed");

    printf("\nSecond container, ISO 6346 check digit computed: ");
    {
        char c[12] = "MSKU000000";
        c[10] = (char) ('0' + zlc_container_digit(c));
        c[11] = 0;
        printf("%s\n", c);
        TRY(zlc_book(d, c, route, 2, importer, &b));
    }
    TRY(zind_qty_parse("90000", &v));
    TRY(zlc_line_add(d, b, "85423100", "CN", v));
    TRY(zlc_depart(d, b));
    rc = zlc_release(d, b, &info);
    printf("  release: %s (CN refuses export, US would release: BOTH)\n", zlc_status_name(rc));
    show(d, b, "held");
    zxv_hexlog_count(qlog, &n);
    printf("  quarantine log records: %u\n", n);
    printf("  automatic release again: %s\n", zlc_status_name(zlc_release(d, b, &info)));
    TRY(zlc_broker_message(d, b, ZXV_LP_FALSE, msg, sizeof msg, &len));
    TRY(zind_sign(cap, broker, msg, len, "logix-clear/broker", sig));
    TRY(zlc_broker_resolve(d, b, zind_signer_pk(broker), ZXV_LP_FALSE, sig, &info));
    printf("  a licensed broker reviewed it and signed: refuse\n");
    show(d, b, "after the broker");

    {
        const uint8_t *trust[1] = {zind_signer_pk(desk_key)};
        uint32_t cnt = 0;
        zind_chain_count(zlc_records(d), &cnt);
        printf("\nSigned records: %u, chain verifies: %s\n", cnt,
               zlc_status_name(zind_chain_verify(cap, zlc_records(d), trust, 1, NULL)));
    }
    TRY(zxv_ledger_check_cap(cap));
    printf("Ledger check: ok\n");

    zlc_desk_destroy(d);
    zind_signer_destroy(desk_key);
    zind_signer_destroy(broker);
    zxv_hexlog_destroy(qlog);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
    return 0;
}
