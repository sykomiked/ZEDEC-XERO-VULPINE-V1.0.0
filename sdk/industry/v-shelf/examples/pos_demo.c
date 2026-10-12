/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pos_demo.c — a grocery store day on v-shelf: a delivery of cola and
 * apples, a transfer to the floor, sales by the unit and by weight (3/4 kg
 * stays 3/4 kg), a till that re-sends a sale, a sale that would go below
 * zero, and an evening cycle count that does not match: the variance is
 * logged and held until the manager signs a decision. */
#include <stdio.h>

#include "zvshelf.h"

#define TRY(x)                                                                                 \
    do {                                                                                       \
        int32_t rc_ = (x);                                                                     \
        if (rc_ != ZXV_OK) {                                                                   \
            fprintf(stderr, "%s failed: %s\n", #x, zvs_status_name(rc_));                      \
            return 1;                                                                          \
        }                                                                                      \
    } while (0)

static void show(const zvs_store *s, const char *sku, uint32_t loc, const char *unit) {
    zind_qty q = ZIND_QTY_ZERO;
    char b[48];
    size_t n = 0;
    zvs_on_hand(s, sku, loc, &q);
    zind_qty_format(q, b, sizeof b, &n);
    printf("    %-10s %s %s\n", sku, b, unit);
}

int main(void) {
    zxv_ctx *ctx = NULL;
    zxv_cap *cap = NULL;
    zxv_hexlog *qlog = NULL;
    zind_signer *till = NULL, *manager = NULL;
    zvs_store *s = NULL;
    uint8_t seed[32] = {3}, mseed[32] = {4}, msg[128], sig[ZIND_SIG_BYTES];
    uint32_t floor = 0, back = 0, n = 0;
    size_t len = 0;
    zind_qty q, var;
    char b[48];

    TRY(zxv_ctx_create(&ctx));
    TRY(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap));
    TRY(zxv_hexlog_create(cap, 64, &qlog));
    TRY(zind_signer_from_seed(cap, seed, &till));
    TRY(zind_signer_from_seed(cap, mseed, &manager));
    TRY(zvs_store_create(cap, till, qlog, &s));
    TRY(zvs_location_add(s, "floor", &floor));
    TRY(zvs_location_add(s, "backroom", &back));
    TRY(zvs_item_add(s, "COLA-330", ZVS_UNIT_EACH, "ea"));
    TRY(zvs_item_add(s, "APPLES", ZVS_UNIT_MEASURE, "kg"));
    TRY(zvs_approver_add(s, zind_signer_pk(manager)));

    printf("v-shelf demo\n  delivery: 24 cola, 12.5 kg apples into the backroom\n");
    TRY(zvs_receive(s, "PO-1001", "COLA-330", back, (zind_qty){24, 1}));
    TRY(zind_qty_parse("12.5", &q));
    TRY(zvs_receive(s, "PO-1002", "APPLES", back, q));
    TRY(zvs_transfer(s, "TR-1", "COLA-330", back, floor, (zind_qty){12, 1}));
    TRY(zind_qty_parse("5", &q));
    TRY(zvs_transfer(s, "TR-2", "APPLES", back, floor, q));
    TRY(zvs_sell(s, "POS-1", "COLA-330", floor, (zind_qty){3, 1}));
    TRY(zvs_sell(s, "POS-2", "APPLES", floor, (zind_qty){3, 4}));
    TRY(zind_qty_parse("1/3", &q));
    TRY(zvs_sell(s, "POS-3", "APPLES", floor, q));
    printf("  sold 3 cola, 3/4 kg and 1/3 kg of apples. On the floor:\n");
    show(s, "COLA-330", floor, "ea");
    show(s, "APPLES", floor, "kg");
    printf("  the till re-sends POS-1: %s\n",
           zvs_status_name(zvs_sell(s, "POS-1", "COLA-330", floor, (zind_qty){3, 1})));
    printf("  a sale of 10 cola from the floor: %s\n",
           zvs_status_name(zvs_sell(s, "POS-4", "COLA-330", floor, (zind_qty){10, 1})));
    printf("  half a can: %s\n",
           zvs_status_name(zvs_sell(s, "POS-5", "COLA-330", floor, (zind_qty){1, 2})));

    printf("  evening count of the floor: 8 cola\n");
    printf("  -> %s", zvs_status_name(zvs_count(s, "CC-1", "COLA-330", floor, (zind_qty){8, 1}, &var)));
    zind_qty_format(var, b, sizeof b, &len);
    zxv_hexlog_count(qlog, &n);
    printf(", variance %s, quarantine log records %u\n", b, n);
    printf("  a sale while held: %s\n",
           zvs_status_name(zvs_sell(s, "POS-6", "COLA-330", floor, (zind_qty){1, 1})));
    TRY(zvs_variance_message(s, "COLA-330", floor, ZVS_ACCEPT_COUNT, msg, sizeof msg, &len));
    TRY(zind_sign(cap, manager, msg, len, ZVS_VARIANCE_CTX, sig));
    TRY(zvs_variance_resolve(s, "COLA-330", floor, zind_signer_pk(manager), ZVS_ACCEPT_COUNT, sig));
    printf("  the manager signed: accept the count. On the floor:\n");
    show(s, "COLA-330", floor, "ea");
    printf("  journal replay: %s\n", zvs_status_name(zvs_reconcile(s, NULL)));

    zvs_store_destroy(s);
    zind_signer_destroy(till);
    zind_signer_destroy(manager);
    zxv_hexlog_destroy(qlog);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
    return 0;
}
