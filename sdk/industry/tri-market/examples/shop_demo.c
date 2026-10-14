/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* shop_demo.c — three orders in an online shop: one delivered and confirmed
 * by the buyer's phone (the seller is paid minus the assurance fee, the fee
 * split four ways by libzxv), one disputed and split 70/30 by an arbiter's
 * signed ruling, one never confirmed and refunded at its deadline; then a
 * cross-currency order priced in a hub test currency and quoted exactly
 * through the V-Bill. */
#include <stdio.h>
#include <string.h>

#include "ztmarket.h"

#define T0 1700000000ULL

static int fail(const char *what, int32_t st) {
    char err[300];
    size_t n = 0;
    if (zind_last_error(err, sizeof err, &n) != ZXV_OK) err[0] = 0;
    fprintf(stderr, "shop_demo: %s: %s (%s)\n", what, ztm_status_name(st), err);
    return 1;
}

#define TRY(what, expr)                                                                        \
    do {                                                                                       \
        int32_t st_ = (expr);                                                                  \
        if (st_ != ZXV_OK) return fail(what, st_);                                             \
    } while (0)

static void money(const char *label, int64_t minor) {
    printf("%s%lld.%02lld", label, (long long) (minor / 100), (long long) (minor % 100));
}

int main(void) {
    zxv_ctx *ctx = NULL;
    zxv_cap *cap = NULL;
    zxv_hexlog *qlog = NULL;
    zind_signer *desk_key = NULL, *arbiter = NULL;
    ztm_desk *d = NULL;
    zxv_vhub *hub = NULL;
    uint8_t s1[32], s2[32], phone_sk[32], phone_pk[32], m[160], sig[ZIND_SIG_BYTES];
    uint32_t issuer = 0, buyer = 0, seller = 0, fee[4], o1 = 0, o2 = 0, o3 = 0, o4 = 0;
    size_t len = 0;
    int64_t f = 0, ts = 0, tb = 0;
    int32_t st;

    for (int i = 0; i < 32; i++) {
        s1[i] = (uint8_t) (11 + i);
        s2[i] = (uint8_t) (12 + i);
        phone_sk[i] = (uint8_t) (13 + i);
    }
    TRY("context", zxv_ctx_create(&ctx));
    TRY("cap", zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap));
    TRY("currency", zxv_ccy_enable_cap(cap, "EUR"));
    TRY("log", zxv_hexlog_create(cap, 64, &qlog));
    TRY("desk key", zind_signer_from_seed(cap, s1, &desk_key));
    TRY("arbiter key", zind_signer_from_seed(cap, s2, &arbiter));
    TRY("phone key", zxv_ed25519_public(cap, phone_sk, phone_pk));
    TRY("desk", ztm_desk_create(cap, "EUR", desk_key, qlog, &d));
    TRY("arbiter", ztm_arbiter_add(d, zind_signer_pk(arbiter)));
    TRY("fee accounts", ztm_fee_accounts(d, fee));
    TRY("issuer", zxv_ledger_open_account_cap(cap, "bank", "EUR", ZXV_ACCT_ISSUER, &issuer));
    TRY("buyer", zxv_ledger_open_account_cap(cap, "buyer", "EUR", ZXV_ACCT_HOLDER, &buyer));
    TRY("seller", zxv_ledger_open_account_cap(cap, "seller", "EUR", ZXV_ACCT_HOLDER, &seller));
    TRY("fund", zxv_ledger_post_cap(cap, issuer, buyer, 500000, "EUR", "demo-funds", NULL));

    /* 1: delivered and confirmed on the buyer's phone */
    TRY("order 1", ztm_order_create(d, "SHOP-1001", buyer, seller, 189900, ZTM_ALG_ED25519,
                                    phone_pk, 32, T0 + 14 * 86400, T0, &o1));
    TRY("ship", ztm_ship(d, o1, "PARCEL-77", T0 + 3600));
    TRY("message", ztm_confirm_message(d, o1, T0 + 2 * 86400, m, sizeof m, &len));
    TRY("phone signs", zxv_ed25519_sign(cap, phone_sk, m, len, sig));
    TRY("confirm", ztm_confirm(d, o1, T0 + 2 * 86400, sig, 64));
    ztm_order_amounts(d, o1, NULL, &f, &ts, NULL);
    money("order SHOP-1001 confirmed: price 1899.00, fee ", f);
    money(", seller ", ts);
    printf("\n");
    st = ztm_confirm(d, o1, T0 + 2 * 86400, sig, 64);
    printf("order SHOP-1001 the same confirmation again: %s\n", ztm_status_name(st));
    {
        uint64_t parts[4];
        zxv_fee_split((uint64_t) f, parts);
        printf("fee split (libzxv): reserve floor %llu, V-Bill pool %llu, infrastructure %llu, "
               "regenerative %llu minor\n",
               (unsigned long long) parts[0], (unsigned long long) parts[1],
               (unsigned long long) parts[2], (unsigned long long) parts[3]);
    }

    /* 2: disputed, split by an arbiter */
    TRY("order 2", ztm_order_create(d, "SHOP-1002", buyer, seller, 50000, ZTM_ALG_ED25519,
                                    phone_pk, 32, T0 + 14 * 86400, T0, &o2));
    TRY("dispute", ztm_dispute(d, o2, ZTM_PARTY_BUYER, 3));
    st = ztm_expire(d, o2, T0 + 30 * 86400);
    printf("order SHOP-1002 disputed (BOTH); a timeout refund is refused: %s\n",
           ztm_status_name(st));
    st = ztm_ruling_message(d, o2, 35000, 14999, m, sizeof m, &len);
    printf("order SHOP-1002 a ruling of 350.00 + 149.99: %s\n", ztm_status_name(st));
    TRY("ruling", ztm_ruling_message(d, o2, 35000, 15000, m, sizeof m, &len));
    TRY("arbiter signs", zind_sign(cap, arbiter, m, len, ZTM_RULING_CTX, sig));
    TRY("rule", ztm_rule(d, o2, zind_signer_pk(arbiter), 35000, 15000, sig));
    ztm_order_amounts(d, o2, NULL, &f, &ts, &tb);
    money("order SHOP-1002 ruled 70/30: seller ", ts);
    money(" (fee ", f);
    money("), buyer refunded ", tb);
    printf("\n");

    /* 3: never confirmed, refunded at the deadline */
    TRY("order 3", ztm_order_create(d, "SHOP-1003", buyer, seller, 12345, ZTM_ALG_ED25519,
                                    phone_pk, 32, T0 + 86400, T0, &o3));
    st = ztm_expire(d, o3, T0 + 86400);
    printf("order SHOP-1003 refund at the deadline itself: %s\n", ztm_status_name(st));
    TRY("expire", ztm_expire(d, o3, T0 + 86401));
    printf("order SHOP-1003 refunded in full after the deadline\n");

    /* 4: priced in a hub test currency, quoted exactly through the V-Bill */
    {
        uint8_t ks[32], pk0[ZIND_PK_BYTES], sk0[4896], pk1[ZIND_PK_BYTES], sk1[4896], pm[40];
        uint64_t vb = 0;
        int64_t price = 0;
        for (int i = 0; i < 32; i++) ks[i] = (uint8_t) (21 + i);
        TRY("hub", zxv_vhub_create(cap, s1, &hub));
        TRY("issuer 0", zxv_mldsa87_keygen_seed(cap, ks, pk0, sk0));
        ks[0] ^= 1;
        TRY("issuer 1", zxv_mldsa87_keygen_seed(cap, ks, pk1, sk1));
        TRY("mirror", zxv_vhub_add_ccy(cap, hub, 0, "EURTEST", 2, pk0, 1000000, 1000000));
        TRY("list ccy", zxv_vhub_add_ccy(cap, hub, 1, "SHOPX", 0, pk1, 1000000, 1000000));
        TRY("peg 0", zxv_vhub_peg_msg(0, 100, 1, 1, pm));
        TRY("sign 0", zxv_mldsa87_sign(cap, sk0, pm, 40, NULL, 0, sig));
        TRY("set 0", zxv_vhub_set_peg(cap, hub, 0, 100, 1, 1, sig));
        TRY("peg 1", zxv_vhub_peg_msg(1, 4, 1, 1, pm));
        TRY("sign 1", zxv_mldsa87_sign(cap, sk1, pm, 40, NULL, 0, sig));
        TRY("set 1", zxv_vhub_set_peg(cap, hub, 1, 4, 1, 1, sig));
        TRY("attach", ztm_hub_attach(d, hub, 0, 2));
        st = ztm_order_create_quoted(d, "SHOP-1004", buyer, seller, 1, 1001, ZTM_ALG_ED25519,
                                     phone_pk, 32, T0 + 86400, T0, &o4);
        printf("order SHOP-1004 listed at 1001 SHOPX (4 per V-Bill): %s\n", ztm_status_name(st));
        TRY("quoted", ztm_order_create_quoted(d, "SHOP-1004", buyer, seller, 1, 1000,
                                              ZTM_ALG_ED25519, phone_pk, 32, T0 + 86400, T0, &o4));
        ztm_order_amounts(d, o4, &price, NULL, NULL, NULL);
        ztm_order_quote(d, o4, NULL, NULL, &vb, NULL, NULL);
        printf("order SHOP-1004 listed at 1000 SHOPX = %llu V-Bills = ", (unsigned long long) vb);
        money("", price);
        printf(" EUR held in escrow\n");
    }
    {
        const uint8_t *trust[1] = {zind_signer_pk(desk_key)};
        uint32_t n = 0;
        int64_t tot = 0;
        for (int i = 0; i < 4; i++) {
            int64_t eq = 0;
            zxv_ledger_balance_cap(cap, fee[i], NULL, NULL, &eq);
            tot += eq;
        }
        TRY("records", zind_chain_verify(cap, ztm_records(d), trust, 1, NULL));
        zind_chain_count(ztm_records(d), &n);
        money("fees collected ", tot);
        printf("; %u signed records verify\n", n);
    }
    ztm_desk_destroy(d);
    zxv_vhub_destroy(hub);
    zind_signer_destroy(arbiter);
    zind_signer_destroy(desk_key);
    zxv_hexlog_destroy(qlog);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
    return 0;
}
