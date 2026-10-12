/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* courier_demo.c — a shop pays 12.50 EUR for a delivery. The money waits in
 * escrow; the courier's app shows the recipient the handoff bytes, the
 * recipient's phone signs them with its own Ed25519 key (no network
 * needed), and the signature releases the escrow. A replay of the same
 * proof is refused, and a second parcel with a dispute is frozen until an
 * arbiter decides. */
#include <stdio.h>
#include <string.h>

#include "ztdeliver.h"

#define TRY(x)                                                                                 \
    do {                                                                                       \
        int32_t rc_ = (x);                                                                     \
        if (rc_ != ZXV_OK) {                                                                   \
            fprintf(stderr, "%s failed: %s\n", #x, ztd_status_name(rc_));                      \
            return 1;                                                                          \
        }                                                                                      \
    } while (0)

static int64_t eq(const zxv_cap *cap, uint32_t a) {
    int64_t v = 0;
    zxv_ledger_balance_cap(cap, a, NULL, NULL, &v);
    return v;
}

int main(void) {
    zxv_ctx *ctx = NULL;
    zxv_cap *cap = NULL;
    zind_signer *desk_key = NULL, *arbiter = NULL;
    ztd_desk *d = NULL;
    uint8_t seed[32] = {7}, aseed[32] = {8}, phone_sk[32] = {42}, phone_pk[32];
    uint8_t msg[128], sig[64], asig[ZIND_SIG_BYTES];
    size_t len = 0;
    uint32_t bank = 0, shop = 0, couriers = 0, p = 0, q = 0;
    const uint64_t t = 1760000000ULL;

    TRY(zxv_ctx_create(&ctx));
    TRY(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap));
    TRY(zind_signer_from_seed(cap, seed, &desk_key));
    TRY(zind_signer_from_seed(cap, aseed, &arbiter));
    TRY(ztd_desk_create(cap, "EUR", desk_key, NULL, &d));
    TRY(ztd_arbiter_add(d, zind_signer_pk(arbiter)));
    TRY(zxv_ledger_open_account_cap(cap, "bank", "EUR", ZXV_ACCT_ISSUER, &bank));
    TRY(zxv_ledger_open_account_cap(cap, "shop", "EUR", ZXV_ACCT_HOLDER, &shop));
    TRY(zxv_ledger_open_account_cap(cap, "couriers", "EUR", ZXV_ACCT_HOLDER, &couriers));
    TRY(zxv_ledger_post_cap(cap, bank, shop, 10000, "EUR", "deposit", NULL));
    TRY(zxv_ed25519_public(cap, phone_sk, phone_pk));

    printf("tri-deliver demo\n");
    TRY(ztd_parcel_create(d, "PARCEL-0001", shop, couriers, 1250, ZTD_ALG_ED25519, phone_pk, 32,
                          4 * 3600, &p));
    printf("  parcel created: shop %lld, escrow %lld, couriers %lld (cents)\n",
           (long long) eq(cap, shop), (long long) eq(cap, ztd_escrow_account(d)),
           (long long) eq(cap, couriers));
    TRY(ztd_dispatch(d, p, "rider-17", t));
    printf("  out for delivery with rider-17\n");

    /* on the recipient's phone, offline */
    TRY(ztd_handoff_message("PARCEL-0001", "rider-17", t + 1800, msg, sizeof msg, &len));
    TRY(zxv_ed25519_sign(cap, phone_sk, msg, len, sig));
    printf("  recipient signed the %zu-byte handoff message on their phone\n", len);

    TRY(ztd_handoff_submit(d, p, "rider-17", t + 1800, sig, 64));
    printf("  proof accepted: shop %lld, escrow %lld, couriers %lld\n", (long long) eq(cap, shop),
           (long long) eq(cap, ztd_escrow_account(d)), (long long) eq(cap, couriers));
    printf("  the same proof again: %s\n",
           ztd_status_name(ztd_handoff_submit(d, p, "rider-17", t + 1800, sig, 64)));
    TRY(ztd_close(d, p));

    TRY(ztd_parcel_create(d, "PARCEL-0002", shop, couriers, 800, ZTD_ALG_ED25519, phone_pk, 32,
                          4 * 3600, &q));
    TRY(ztd_dispatch(d, q, "rider-17", t + 100));
    TRY(ztd_dispute(d, q, 1));
    printf("  PARCEL-0002 disputed (recipient: never arrived): frozen as BOTH\n");
    TRY(ztd_handoff_message("PARCEL-0002", "rider-17", t + 200, msg, sizeof msg, &len));
    TRY(zxv_ed25519_sign(cap, phone_sk, msg, len, sig));
    printf("  a proof arriving now: %s\n",
           ztd_status_name(ztd_handoff_submit(d, q, "rider-17", t + 200, sig, 64)));
    TRY(ztd_arbiter_message(d, q, ZTD_REFUND_SENDER, msg, sizeof msg, &len));
    TRY(zind_sign(cap, arbiter, msg, len, ZTD_ARBITER_CTX, asig));
    TRY(ztd_arbiter_resolve(d, q, zind_signer_pk(arbiter), ZTD_REFUND_SENDER, asig));
    printf("  the arbiter (a person) signed: refund. shop %lld, escrow %lld\n",
           (long long) eq(cap, shop), (long long) eq(cap, ztd_escrow_account(d)));
    {
        const uint8_t *trust[1] = {zind_signer_pk(desk_key)};
        uint32_t n = 0;
        zind_chain_count(ztd_records(d), &n);
        printf("  %u signed records, chain: %s\n", n,
               ztd_status_name(zind_chain_verify(cap, ztd_records(d), trust, 1, NULL)));
    }
    ztd_desk_destroy(d);
    zind_signer_destroy(desk_key);
    zind_signer_destroy(arbiter);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
    return 0;
}
