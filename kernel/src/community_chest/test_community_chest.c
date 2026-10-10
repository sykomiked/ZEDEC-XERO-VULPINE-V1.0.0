/* test_community_chest.c — Community Chest P2P App Store tests
 *
 * Tests app listing, signature verification, download/install flow,
 * paid app purchases, revenue split calculations, deprecation,
 * and M5 coverage computation.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "community_chest.h"
#include "vino.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../robin_debanks/crypto_verify.h"

/* TEST VERIFIER — see the note in test_count_house.c. The production path is
 * real Ed25519 against an embedded public key whose private key is held
 * offline, so a host test cannot mint a valid signature. The API exposes an
 * injectable verify_sig hook for exactly this case; we install one checking
 * the same HMAC construction the test signs with, which exercises the module's
 * logic without weakening the production path. (These tests signed HMAC and
 * called the default verifier; when the implementation was upgraded
 * HMAC -> Ed25519 they were left behind and failed silently, because they were
 * never wired into verify-all.) */
static bool test_verify_hmac(const cc_app_t *e)
{
    if (!e) return false;
    uint8_t msg[128];
    uint32_t pos = 0;
    for (uint32_t i = 0; i < 64 && e->name[i]; i++) msg[pos++] = (uint8_t) e->name[i];
    for (uint32_t i = 0; i < 32 && pos < sizeof(msg); i++) msg[pos++] = e->dev_pubkey[i];
    uint8_t expect[32];
    crypto_hmac_sha256(msg, pos, CRYPTO_AUTHORITY_KEY_COMMUNITY_CHEST, expect);
    for (uint32_t i = 0; i < 32; i++)
        if (expect[i] != e->signature[i]) return false;
    return true;
}

static int feq(double a, double b, double eps)
{
    double diff = fabs(a - b);
    double scale = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    if (scale < 1.0) scale = 1.0;
    return diff <= eps * scale;
}

static word168_t make_dev(uint8_t seed)
{
    word168_t w;
    for (int i = 0; i < WORD168_OCTETS; i++) w.bytes[i] = (uint8_t) (seed + i);
    return w;
}

static uint8_t zero_sig[CC_SIG_LEN];
static uint8_t pubkey[CC_PUBKEY_LEN];
static uint8_t hash[CC_CONTENT_HASH_LEN];

/* Compute a valid HMAC-SHA256 community_chest signature for test apps */
static void compute_cc_sig(const char *name, const uint8_t *pubkey, uint8_t sig[CC_SIG_LEN])
{
    uint8_t msg[128];
    uint32_t pos = 0;
    for (uint32_t i = 0; name[i] && i < 64 && pos < sizeof(msg); i++)
        msg[pos++] = (uint8_t) name[i];
    for (uint32_t i = 0; i < 32 && pos < sizeof(msg); i++) msg[pos++] = pubkey[i];
    uint8_t hmac_out[32];
    crypto_hmac_sha256(msg, pos, CRYPTO_AUTHORITY_KEY_COMMUNITY_CHEST, hmac_out);
    for (uint32_t i = 0; i < 32; i++) sig[i] = hmac_out[i];
    for (uint32_t i = 32; i < CC_SIG_LEN; i++) sig[i] = 0;
}

int main(void)
{
    memset(zero_sig, 0, CC_SIG_LEN);
    memset(pubkey, 0x42, CC_PUBKEY_LEN);
    memset(hash, 0x77, CC_CONTENT_HASH_LEN);

    /* ===== init ===== */
    {
        community_chest_t cc;
        cc_init(&cc, 1, "ZEDEC:community-chest");
        cc.verify_sig = test_verify_hmac;
        assert(cc.device_id == 1);
        assert(strcmp(cc.name, "ZEDEC:community-chest") == 0);
        assert(cc.num_apps == 0);
        assert(cc.next_id == 1);
    }

    /* ===== list a free app ===== */
    {
        community_chest_t cc;
        cc_init(&cc, 1, "chest");
        cc.verify_sig = test_verify_hmac;

        word168_t dev = make_dev(1);
        uint8_t sig1[CC_SIG_LEN];
        compute_cc_sig("MeshChat", pubkey, sig1);
        int32_t id = cc_list_app(&cc, "MeshChat", "P2P chat app", "DevAlpha", &dev, pubkey, hash,
                                 sig1, CC_APP_FREE, CC_APP_NATIVE, 0, 7500);
        assert(id > 0);
        assert(cc.num_apps == 1);

        cc_app_t *app = cc_get_app(&cc, (uint32_t) id);
        assert(app != NULL);
        assert(strcmp(app->name, "MeshChat") == 0);
        assert(app->type == CC_APP_FREE);
        assert(app->state == CC_APP_LISTED);
        assert(app->sig_verified == true); /* HMAC verified */
        assert(app->dev_share_bp == 7500);
        assert(app->platform_share_bp == 500); /* 10000 - 7500 - 2000 */
    }

    /* ===== list with zero signature -> rejected ===== */
    {
        community_chest_t cc;
        cc_init(&cc, 1, "chest");
        cc.verify_sig = test_verify_hmac;
        word168_t dev = make_dev(2);
        int32_t id = cc_list_app(&cc, "BadApp", "Malicious", "EvilDev", &dev, pubkey, hash,
                                 zero_sig, CC_APP_FREE, CC_APP_NATIVE, 0, 7000);
        assert(id == -2);
        cc_app_t *app = cc_get_app(&cc, (uint32_t) 1);
        assert(app->state == CC_APP_REJECTED);
        assert(app->sig_verified == false);
    }

    /* ===== download + install flow ===== */
    {
        community_chest_t cc;
        cc_init(&cc, 1, "chest");
        cc.verify_sig = test_verify_hmac;
        word168_t dev = make_dev(3);
        uint8_t sig3[CC_SIG_LEN];
        compute_cc_sig("Tool", pubkey, sig3);
        int32_t id = cc_list_app(&cc, "Tool", "Utility", "DevB", &dev, pubkey, hash, sig3,
                                 CC_APP_FREE, CC_APP_TOOL, 0, 7000);
        assert(id > 0);

        assert(cc_download_app(&cc, (uint32_t) id) == 0);
        cc_app_t *app = cc_get_app(&cc, (uint32_t) id);
        assert(app->download_count == 1);
        assert(app->state == CC_APP_VERIFIED);

        assert(cc_install_app(&cc, (uint32_t) id) == 0);
        assert(app->state == CC_APP_INSTALLED);
        assert(app->install_count == 1);
        assert(cc.total_downloads == 1);
    }

    /* ===== paid app purchase + revenue split ===== */
    {
        community_chest_t cc;
        cc_init(&cc, 1, "chest");
        cc.verify_sig = test_verify_hmac;
        word168_t dev = make_dev(4);
        uint8_t sig4[CC_SIG_LEN];
        compute_cc_sig("ProTool", pubkey, sig4);
        int32_t id = cc_list_app(&cc, "ProTool", "Pro utility", "DevC", &dev, pubkey, hash, sig4,
                                 CC_APP_PAID, CC_APP_PLUGIN, 10000, 8000);
        assert(id > 0);

        /* Purchase: 10000 tokens, dev 80%, platform 0%, royalty 20% */
        /* platform = 10000 - 8000 - 2000 = 0 */
        assert(cc_purchase_app(&cc, (uint32_t) id) == 0);

        cc_app_t *app = cc_get_app(&cc, (uint32_t) id);
        assert(app->total_revenue == 10000);
        assert(cc.total_revenue == 10000);

        /* dev payout = 10000 * 8000 / 10000 = 8000 */
        assert(cc.total_dev_payouts == 8000);
        /* royalty = 10000 * 2000 / 10000 = 2000 */
        assert(cc.total_royalty_revenue == 2000);
        /* platform = 10000 * 0 / 10000 = 0 */
        assert(cc.total_platform_revenue == 0);
    }

    /* ===== revenue split with 70% dev (minimum) ===== */
    {
        uint64_t platform_rev, royalty_rev;
        uint64_t dev_payout = cc_calc_revenue_split(1000, 7000, &platform_rev, &royalty_rev);
        assert(dev_payout == 700);   /* 70% */
        assert(royalty_rev == 200);  /* 20% */
        assert(platform_rev == 100); /* 10% */
    }

    /* ===== revenue split with 85% dev (maximum) ===== */
    {
        uint64_t platform_rev, royalty_rev;
        uint64_t dev_payout = cc_calc_revenue_split(1000, 8500, &platform_rev, &royalty_rev);
        assert(dev_payout == 850);  /* 85% */
        assert(royalty_rev == 150); /* royalty limited to what is left */
        assert(platform_rev == 0);
        assert(dev_payout + royalty_rev + platform_rev == 1000); /* conserved */
    }

    /* ===== revenue split conserves odd and huge amounts ===== */
    {
        uint64_t platform_rev, royalty_rev;
        uint64_t d = cc_calc_revenue_split(9999, 7333, &platform_rev, &royalty_rev);
        assert(d + platform_rev + royalty_rev == 9999);
        uint64_t big = UINT64_MAX - 7;
        d = cc_calc_revenue_split(big, 8000, &platform_rev, &royalty_rev);
        assert(d + platform_rev + royalty_rev == big);
        assert(d == (big / 10000) * 8000 + ((big % 10000) * 8000) / 10000);
    }

    /* ===== revenue split clamps out-of-range dev share ===== */
    {
        uint64_t platform_rev, royalty_rev;
        /* Below minimum: clamps to 70% */
        uint64_t dev1 = cc_calc_revenue_split(1000, 5000, &platform_rev, &royalty_rev);
        assert(dev1 == 700);
        /* Above maximum: clamps to 85% */
        uint64_t dev2 = cc_calc_revenue_split(1000, 9500, &platform_rev, &royalty_rev);
        assert(dev2 == 850);
    }

    /* ===== purchase free app fails ===== */
    {
        community_chest_t cc;
        cc_init(&cc, 1, "chest");
        cc.verify_sig = test_verify_hmac;
        word168_t dev = make_dev(5);
        uint8_t sig5[CC_SIG_LEN];
        compute_cc_sig("Free", pubkey, sig5);
        int32_t id = cc_list_app(&cc, "Free", "Free app", "DevD", &dev, pubkey, hash, sig5,
                                 CC_APP_FREE, CC_APP_VENA, 0, 7000);
        assert(cc_purchase_app(&cc, (uint32_t) id) == -2);
    }

    /* ===== deprecate app ===== */
    {
        community_chest_t cc;
        cc_init(&cc, 1, "chest");
        cc.verify_sig = test_verify_hmac;
        word168_t dev = make_dev(6);
        uint8_t sig6[CC_SIG_LEN];
        compute_cc_sig("OldApp", pubkey, sig6);
        int32_t id = cc_list_app(&cc, "OldApp", "Deprecated", "DevE", &dev, pubkey, hash, sig6,
                                 CC_APP_FREE, CC_APP_NATIVE, 0, 7000);
        assert(cc_deprecate_app(&cc, (uint32_t) id) == 0);
        cc_app_t *app = cc_get_app(&cc, (uint32_t) id);
        assert(app->state == CC_APP_DEPRECATED);
    }

    /* ===== AI model app type ===== */
    {
        community_chest_t cc;
        cc_init(&cc, 1, "chest");
        cc.verify_sig = test_verify_hmac;
        word168_t dev = make_dev(7);
        uint8_t sig7[CC_SIG_LEN];
        compute_cc_sig("QuantumAI", pubkey, sig7);
        int32_t id = cc_list_app(&cc, "QuantumAI", "Post-quantum AI model", "AI_Lab", &dev, pubkey,
                                 hash, sig7, CC_APP_PAID, CC_APP_AI_MODEL, 50000, 7500);
        assert(id > 0);
        cc_app_t *app = cc_get_app(&cc, (uint32_t) id);
        assert(app->category == CC_APP_AI_MODEL);
        assert(app->price == 50000);
    }

    /* ===== capacity limit ===== */
    {
        community_chest_t cc;
        cc_init(&cc, 1, "chest");
        cc.verify_sig = test_verify_hmac;
        word168_t dev = make_dev(8);

        for (uint32_t i = 0; i < CC_MAX_APPS; i++) {
            char name[32];
            /* simple integer to string */
            uint32_t n = i;
            int pos = 0;
            if (n == 0) {
                name[pos++] = '0';
            } else {
                char tmp[16];
                int tp = 0;
                while (n > 0) {
                    tmp[tp++] = '0' + (n % 10);
                    n /= 10;
                }
                while (tp > 0) name[pos++] = tmp[--tp];
            }
            name[pos] = '\0';
            uint8_t sigc[CC_SIG_LEN];
            compute_cc_sig(name, pubkey, sigc);
            int32_t id = cc_list_app(&cc, name, "test", "dev", &dev, pubkey, hash, sigc,
                                     CC_APP_FREE, CC_APP_NATIVE, 0, 7000);
            assert(id > 0);
        }

        /* Next should fail */
        uint8_t sigov[CC_SIG_LEN];
        compute_cc_sig("overflow", pubkey, sigov);
        int32_t id = cc_list_app(&cc, "overflow", "test", "dev", &dev, pubkey, hash, sigov,
                                 CC_APP_FREE, CC_APP_NATIVE, 0, 7000);
        assert(id == -1);
    }

    /* ===== coverage computation ===== */
    {
        community_chest_t cc;
        cc_init(&cc, 1, "chest");
        cc.verify_sig = test_verify_hmac;
        word168_t dev = make_dev(9);

        /* List two apps */
        uint8_t siga1[CC_SIG_LEN];
        compute_cc_sig("App1", pubkey, siga1);
        uint8_t siga2[CC_SIG_LEN];
        compute_cc_sig("App2", pubkey, siga2);
        int32_t id1 = cc_list_app(&cc, "App1", "desc", "dev", &dev, pubkey, hash, siga1,
                                  CC_APP_PAID, CC_APP_NATIVE, 1000, 7000);
        int32_t id2 = cc_list_app(&cc, "App2", "desc", "dev", &dev, pubkey, hash, siga2,
                                  CC_APP_FREE, CC_APP_VENA, 0, 7000);

        /* Purchase and install app1 */
        cc_purchase_app(&cc, (uint32_t) id1);
        cc_download_app(&cc, (uint32_t) id1);
        cc_install_app(&cc, (uint32_t) id1);

        /* Download app2 (free) */
        cc_download_app(&cc, (uint32_t) id2);

        cc_update_coverage(&cc);
        /* r = 1 app with revenue / 2 listed = 0.5
         * ell = 1 installed / 2 listed = 0.5 */
        assert(feq(cc.m5.r, 0.5, 1e-9));
        assert(feq(cc.m5.ell, 0.5, 1e-9));
    }

    /* ===== Vino Floating Voucher: cash-in ===== */
    {
        static community_chest_t cc;
        static vino_ledger_t vino;
        vino_init(&vino, 1);
        cc_init(&cc, 1, "chest");
        cc.verify_sig = test_verify_hmac;
        cc_link_vino(&cc, &vino);

        uint64_t credited = cc_voucher_cash_in(&cc, "alice", 5000, CAP_FINANCIAL);
        assert(credited == 5000);
        assert(cc.voucher_float == 5000);
        assert(cc.voucher_cashin_total == 5000);

        vino_account_t *acct = vino_get_account(&vino, "alice");
        assert(acct != NULL);
        assert(acct->balance[CAP_FINANCIAL] == 5000);
    }

    /* ===== Vino Floating Voucher: cash-out ===== */
    {
        static community_chest_t cc;
        static vino_ledger_t vino;
        vino_init(&vino, 1);
        cc_init(&cc, 1, "chest");
        cc.verify_sig = test_verify_hmac;
        cc_link_vino(&cc, &vino);

        cc_voucher_cash_in(&cc, "bob", 3000, CAP_FINANCIAL);
        uint64_t redeemed = cc_voucher_cash_out(&cc, "bob", 1000, CAP_FINANCIAL);
        assert(redeemed == 1000);
        assert(cc.voucher_float == 2000);
        assert(cc.voucher_cashout_total == 1000);

        vino_account_t *acct = vino_get_account(&vino, "bob");
        assert(acct->balance[CAP_FINANCIAL] == 2000);
    }

    /* ===== Vino Floating Voucher: purchase app with vouchers ===== */
    {
        static community_chest_t cc;
        static vino_ledger_t vino;
        vino_init(&vino, 1);
        cc_init(&cc, 1, "chest");
        cc.verify_sig = test_verify_hmac;
        cc_link_vino(&cc, &vino);

        /* User cashes in 10000 vouchers */
        cc_voucher_cash_in(&cc, "carol", 10000, CAP_FINANCIAL);

        /* Developer lists a paid app */
        word168_t dev = make_dev(10);
        uint8_t sig10[CC_SIG_LEN];
        compute_cc_sig("ProApp", pubkey, sig10);
        int32_t id = cc_list_app(&cc, "ProApp", "Pro tool", "DevF", &dev, pubkey, hash, sig10,
                                 CC_APP_PAID, CC_APP_PLUGIN, 3000, 7500);
        assert(id > 0);

        /* User purchases with vouchers */
        int32_t result = cc_purchase_with_vouchers(&cc, (uint32_t) id, "carol");
        assert(result == 0);

        /* Check voucher balance debited */
        vino_account_t *acct = vino_get_account(&vino, "carol");
        assert(acct->balance[CAP_FINANCIAL] == 7000); /* 10000 - 3000 */
        /* the price went somewhere: escrow holds it, nothing was destroyed */
        vino_account_t *esc = vino_get_account(&vino, CC_ESCROW_ADDR);
        assert(esc && esc->balance[CAP_FINANCIAL] == 3000);

        /* Check revenue recorded */
        cc_app_t *app = cc_get_app(&cc, (uint32_t) id);
        assert(app->total_revenue == 3000);
        assert(app->download_count == 1);
        assert(cc.total_revenue == 3000);
    }

    /* ===== Vino Floating Voucher: insufficient balance ===== */
    {
        static community_chest_t cc;
        static vino_ledger_t vino;
        vino_init(&vino, 1);
        cc_init(&cc, 1, "chest");
        cc.verify_sig = test_verify_hmac;
        cc_link_vino(&cc, &vino);

        cc_voucher_cash_in(&cc, "dave", 500, CAP_FINANCIAL);

        word168_t dev = make_dev(11);
        uint8_t sig11[CC_SIG_LEN];
        compute_cc_sig("ExpensiveApp", pubkey, sig11);
        int32_t id = cc_list_app(&cc, "ExpensiveApp", "Costly", "DevG", &dev, pubkey, hash, sig11,
                                 CC_APP_PAID, CC_APP_NATIVE, 1000, 7000);
        assert(id > 0);

        int32_t result = cc_purchase_with_vouchers(&cc, (uint32_t) id, "dave");
        assert(result == -2); /* insufficient balance */
    }

    /* ===== Vino Floating Voucher: no ledger linked ===== */
    {
        community_chest_t cc;
        cc_init(&cc, 1, "chest");
        cc.verify_sig = test_verify_hmac;
        word168_t dev = make_dev(12);
        uint8_t sig12[CC_SIG_LEN];
        compute_cc_sig("App", pubkey, sig12);
        int32_t id = cc_list_app(&cc, "App", "desc", "dev", &dev, pubkey, hash, sig12, CC_APP_PAID,
                                 CC_APP_NATIVE, 100, 7000);
        assert(id > 0);
        int32_t result = cc_purchase_with_vouchers(&cc, (uint32_t) id, "user");
        assert(result == -3); /* no vino linked */
    }

    /* ===== Vino Floating Voucher: free app via voucher API ===== */
    {
        static community_chest_t cc;
        static vino_ledger_t vino;
        vino_init(&vino, 1);
        cc_init(&cc, 1, "chest");
        cc.verify_sig = test_verify_hmac;
        cc_link_vino(&cc, &vino);

        word168_t dev = make_dev(13);
        uint8_t sig13[CC_SIG_LEN];
        compute_cc_sig("FreeApp", pubkey, sig13);
        int32_t id = cc_list_app(&cc, "FreeApp", "free", "dev", &dev, pubkey, hash, sig13,
                                 CC_APP_FREE, CC_APP_VENA, 0, 7000);
        assert(id > 0);

        int32_t result = cc_purchase_with_vouchers(&cc, (uint32_t) id, "eve");
        assert(result == 0); /* free app, no debit */

        cc_app_t *app = cc_get_app(&cc, (uint32_t) id);
        assert(app->download_count == 1);
    }

    /* ===== revenue split: shares sum exactly to the gross, none exceeds it ===== */
    {
        static const uint64_t prices[] = {1,
                                          2,
                                          3,
                                          7,
                                          9999,
                                          10000,
                                          10001,
                                          123457,
                                          99999999,
                                          UINT64_MAX / 3,
                                          UINT64_MAX - 1,
                                          UINT64_MAX};
        static const uint32_t bps[] = {0, 6999, 7000, 7777, 8500, 8501, 10000, 50000};
        for (uint32_t i = 0; i < sizeof prices / sizeof prices[0]; i++)
            for (uint32_t j = 0; j < sizeof bps / sizeof bps[0]; j++) {
                uint64_t plat = 0, roy = 0;
                uint64_t dev = cc_calc_revenue_split(prices[i], bps[j], &plat, &roy);
                assert(dev <= prices[i] && plat <= prices[i] && roy <= prices[i]);
                /* exact: no dust lost, nothing minted (checked without wrapping) */
                assert(dev <= prices[i] - roy && plat == prices[i] - dev - roy);
            }
        /* the remainder goes deterministically to the platform */
        uint64_t plat = 0, roy = 0;
        uint64_t dev = cc_calc_revenue_split(10001, 7000, &plat, &roy);
        assert(dev == 7000 && roy == 2000 && plat == 1001);
        printf("[PASS] revenue split sums exactly to the gross for %u cases\n",
               (unsigned) ((sizeof prices / sizeof prices[0]) * (sizeof bps / sizeof bps[0])));
    }

    /* ===== conservation across cash-in, purchase and cash-out ===== */
    {
        static community_chest_t cc;
        static vino_ledger_t vino;
        vino_init(&vino, 1);
        cc_init(&cc, 1, "chest");
        cc.verify_sig = test_verify_hmac;
        cc_link_vino(&cc, &vino);
        word168_t dev = make_dev(20);
        uint8_t sig20[CC_SIG_LEN];
        compute_cc_sig("Cons", pubkey, sig20);
        int32_t id = cc_list_app(&cc, "Cons", "c", "dev", &dev, pubkey, hash, sig20, CC_APP_PAID,
                                 CC_APP_NATIVE, 250, 7000);
        assert(id > 0);
        uint64_t minted = 0, burned = 0;
#define CC_SUM(out)                                                                                \
    do {                                                                                           \
        out = 0;                                                                                   \
        for (uint32_t i_ = 0; i_ < vino.num_accounts; i_++)                                        \
            out += vino.balances[i_].balance[CAP_FINANCIAL];                                       \
    } while (0)
        uint64_t s0, s;
        CC_SUM(s0);
        minted += cc_voucher_cash_in(&cc, "f1", 1000, CAP_FINANCIAL);
        CC_SUM(s);
        assert(s == s0 + minted - burned);
        assert(cc_purchase_with_vouchers(&cc, (uint32_t) id, "f1") == 0);
        CC_SUM(s);
        assert(s == s0 + minted - burned); /* buyer -> escrow: moved, not minted */
        assert(cc_purchase_with_vouchers(&cc, (uint32_t) id, "nobody") == -2);
        CC_SUM(s);
        assert(s == s0 + minted - burned);
        burned += cc_voucher_cash_out(&cc, "f1", 300, CAP_FINANCIAL);
        CC_SUM(s);
        assert(s == s0 + minted - burned && burned == 300);
        assert(cc_voucher_cash_out(&cc, "f1", 100000, CAP_FINANCIAL) == 0); /* uncovered */
        CC_SUM(s);
        assert(s == s0 + minted - burned);
        assert(cc.voucher_float == minted - burned);

        /* overflow: a cash-in that would wrap any counter changes nothing */
        vino_account_t *f1 = vino_get_account(&vino, "f1");
        uint64_t b0 = f1->balance[CAP_FINANCIAL], fl0 = cc.voucher_float;
        assert(cc_voucher_cash_in(&cc, "f1", UINT64_MAX, CAP_FINANCIAL) == 0);
        assert(f1->balance[CAP_FINANCIAL] == b0 && cc.voucher_float == fl0);
        /* a sale whose revenue counter would wrap is refused before value moves */
        cc_app_t *app = cc_get_app(&cc, (uint32_t) id);
        uint64_t tr = app->total_revenue;
        cc.total_revenue = UINT64_MAX - 10;
        uint64_t esc0 = vino_get_account(&vino, CC_ESCROW_ADDR)->balance[CAP_FINANCIAL];
        assert(cc_purchase_with_vouchers(&cc, (uint32_t) id, "f1") == -4);
        assert(f1->balance[CAP_FINANCIAL] == b0 &&
               vino_get_account(&vino, CC_ESCROW_ADDR)->balance[CAP_FINANCIAL] == esc0 &&
               app->total_revenue == tr);
        assert(cc_purchase_app(&cc, (uint32_t) id) == -4 && app->total_revenue == tr);
#undef CC_SUM
        printf("[PASS] community chest conserves value: sum(after) == sum(before) + minted - "
               "burned\n");
    }

    printf("All Community Chest tests passed\n");
    return 0;
}
