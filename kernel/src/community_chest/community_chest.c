/* community_chest.c — Community Chest P2P App Store implementation
 *
 * See community_chest.h for design rationale. Follows the same
 * conventions as count_house.c / porter_house.c: SR_* fixed-point,
 * M5 coordinates, inline coverage computation, pluggable signature
 * verification (HMAC-SHA256 verification, same as Count House).
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "community_chest.h"
#include <string.h>
#include "../robin_debanks/ed25519_verify.h"

static bool cc_default_verify_sig(const cc_app_t *app) {
    if (!app) return false;
    /* Ed25519 asymmetric verification against embedded PUBLIC key.
     * Private key held offline/HSM-backed. */
    uint8_t msg[128]; uint32_t pos = 0;
    for (uint32_t i = 0; i < 64 && app->name[i]; i++) msg[pos++] = (uint8_t)app->name[i];
    for (uint32_t i = 0; i < 32 && pos < sizeof(msg); i++) msg[pos++] = app->dev_pubkey[i];
    return ed25519_verify(msg, pos, app->signature, ED25519_PUBKEY_COMMUNITY_CHEST);
}

void cc_init(community_chest_t *cc, uint32_t device_id, const char *name) {
    if (!cc) return;
    memset(cc, 0, sizeof(*cc));
    cc->device_id = device_id;

    uint32_t i;
    for (i = 0; i + 1 < CC_MAX_DEVELOPER_LEN && name && name[i]; i++) {
        cc->name[i] = name[i];
    }
    cc->name[i] = '\0';

    cc->num_apps = 0;
    cc->next_id = 1;

    cc->m5.omega = device_id;
    cc->m5.chi = device_id;
    cc->m5.phi = SR_ZERO;
    cc->vino = NULL;
    cc->voucher_float = 0;
    cc->voucher_cashin_total = 0;
    cc->voucher_cashout_total = 0;

    cc_update_coverage(cc);
}

int32_t cc_list_app(community_chest_t *cc, const char *name,
                     const char *description, const char *developer,
                     const word168_t *developer_id,
                     const uint8_t dev_pubkey[CC_PUBKEY_LEN],
                     const uint8_t content_hash[CC_CONTENT_HASH_LEN],
                     const uint8_t signature[CC_SIG_LEN],
                     cc_app_type_t type, cc_app_category_t category,
                     uint64_t price, uint32_t dev_share_bp) {
    if (!cc || !name || !developer_id) return -1;

    /* Find unused slot */
    uint32_t slot = CC_MAX_APPS;
    for (uint32_t i = 0; i < CC_MAX_APPS; i++) {
        if (!cc->apps[i].active) {
            slot = i;
            break;
        }
    }
    if (slot >= CC_MAX_APPS) return -1;

    /* Validate dev_share_bp */
    if (dev_share_bp < CC_DEV_SHARE_MIN) dev_share_bp = CC_DEV_SHARE_MIN;
    if (dev_share_bp > CC_DEV_SHARE_MAX) dev_share_bp = CC_DEV_SHARE_MAX;

    cc_app_t *app = &cc->apps[slot];
    memset(app, 0, sizeof(*app));
    app->id = cc->next_id++;
    app->active = true;

    /* Copy strings */
    uint32_t j;
    for (j = 0; j + 1 < CC_MAX_NAME_LEN && name[j]; j++) app->name[j] = name[j];
    app->name[j] = '\0';
    for (j = 0; j + 1 < CC_MAX_DESC_LEN && description && description[j]; j++)
        app->description[j] = description[j];
    app->description[j] = '\0';
    for (j = 0; j + 1 < CC_MAX_DEVELOPER_LEN && developer && developer[j]; j++)
        app->developer[j] = developer[j];
    app->developer[j] = '\0';

    /* Copy identity and crypto fields */
    app->developer_id = *developer_id;
    if (dev_pubkey) memcpy(app->dev_pubkey, dev_pubkey, CC_PUBKEY_LEN);
    if (content_hash) memcpy(app->content_hash, content_hash, CC_CONTENT_HASH_LEN);
    if (signature) memcpy(app->signature, signature, CC_SIG_LEN);

    app->type = type;
    app->category = category;
    app->price = price;
    app->dev_share_bp = dev_share_bp;
    {
        int32_t pshare = (int32_t)(10000 - dev_share_bp - CC_ROYALTY_SHARE);
        app->platform_share_bp = (pshare < 0) ? 0 : (uint32_t)pshare;
    }
    app->seeder_count = 1; /* listing node is first seeder */

    /* Verify signature */
    app->sig_verified = cc->verify_sig ? cc->verify_sig(app) : cc_default_verify_sig(app);
    if (!app->sig_verified) {
        app->state = CC_APP_REJECTED;
        cc->num_apps++;
        cc_update_coverage(cc);
        return -2;
    }

    app->state = CC_APP_LISTED;
    app->m5.omega = app->id;
    app->m5.chi = (uint32_t)category;
    app->m5.phi = SR_FROM_INT((int64_t)price);

    cc->num_apps++;
    cc_update_coverage(cc);
    return (int32_t)app->id;
}

bool cc_verify_app(community_chest_t *cc, uint32_t app_id) {
    if (!cc) return false;
    cc_app_t *app = cc_get_app(cc, app_id);
    if (!app) return false;
    app->sig_verified = cc->verify_sig ? cc->verify_sig(app) : cc_default_verify_sig(app);
    if (app->sig_verified && app->state == CC_APP_REJECTED) {
        app->state = CC_APP_LISTED;
    }
    return app->sig_verified;
}

int32_t cc_download_app(community_chest_t *cc, uint32_t app_id) {
    if (!cc) return -1;
    cc_app_t *app = cc_get_app(cc, app_id);
    if (!app) return -1;
    if (app->state != CC_APP_LISTED && app->state != CC_APP_VERIFIED) return -2;

    app->download_count++;
    app->state = CC_APP_DOWNLOADING;
    cc->total_downloads++;

    /* Simulate mesh transfer completion + signature re-verification */
    if (app->sig_verified) {
        app->state = CC_APP_VERIFIED;
    }

    cc_update_coverage(cc);
    return 0;
}

int32_t cc_install_app(community_chest_t *cc, uint32_t app_id) {
    if (!cc) return -1;
    cc_app_t *app = cc_get_app(cc, app_id);
    if (!app) return -1;
    if (app->state != CC_APP_VERIFIED) return -2;

    app->state = CC_APP_INSTALLED;
    app->install_count++;

    cc_update_coverage(cc);
    return 0;
}

int32_t cc_purchase_app(community_chest_t *cc, uint32_t app_id) {
    if (!cc) return -1;
    cc_app_t *app = cc_get_app(cc, app_id);
    if (!app) return -1;
    if (app->type == CC_APP_FREE) return -2;
    if (app->price == 0) return -2;

    /* Calculate revenue split */
    uint64_t platform_rev, royalty_rev;
    uint64_t dev_payout = cc_calc_revenue_split(app->price, app->dev_share_bp,
                                                  &platform_rev, &royalty_rev);

    app->total_revenue += app->price;
    cc->total_revenue += app->price;
    cc->total_dev_payouts += dev_payout;
    cc->total_platform_revenue += platform_rev;
    cc->total_royalty_revenue += royalty_rev;

    cc_update_coverage(cc);
    return 0;
}

int32_t cc_deprecate_app(community_chest_t *cc, uint32_t app_id) {
    if (!cc) return -1;
    cc_app_t *app = cc_get_app(cc, app_id);
    if (!app) return -1;
    app->state = CC_APP_DEPRECATED;
    cc_update_coverage(cc);
    return 0;
}

cc_app_t *cc_get_app(community_chest_t *cc, uint32_t app_id) {
    if (!cc) return NULL;
    for (uint32_t i = 0; i < CC_MAX_APPS; i++) {
        if (cc->apps[i].active && cc->apps[i].id == app_id) {
            return &cc->apps[i];
        }
    }
    return NULL;
}

uint64_t cc_calc_revenue_split(uint64_t amount, uint32_t dev_share_bp,
                                 uint64_t *platform_revenue,
                                 uint64_t *royalty_revenue) {
    if (dev_share_bp < CC_DEV_SHARE_MIN) dev_share_bp = CC_DEV_SHARE_MIN;
    if (dev_share_bp > CC_DEV_SHARE_MAX) dev_share_bp = CC_DEV_SHARE_MAX;

    int32_t platform_bp = (int32_t)(10000 - dev_share_bp - CC_ROYALTY_SHARE);
    if (platform_bp < 0) platform_bp = 0;

    uint64_t dev_payout = (amount * dev_share_bp) / 10000;
    *platform_revenue = (amount * (uint32_t)platform_bp) / 10000;
    *royalty_revenue = (amount * CC_ROYALTY_SHARE) / 10000;

    return dev_payout;
}

void cc_link_vino(community_chest_t *cc, vino_ledger_t *vino) {
    if (!cc || !vino) return;
    cc->vino = vino;
}

uint64_t cc_voucher_cash_in(community_chest_t *cc, const char *account_addr,
                              uint64_t external_amount, capital_type_t capital) {
    if (!cc || !cc->vino || !account_addr || external_amount == 0) return 0;

    vino_account_t *acct = vino_get_account(cc->vino, account_addr);
    if (!acct) {
        /* Auto-create account if it doesn't exist */
        if (vino_create_account(cc->vino, account_addr, "voucher-user") < 0) return 0;
        acct = vino_get_account(cc->vino, account_addr);
        if (!acct) return 0;
    }

    /* Credit voucher balance (stored in CAP_FINANCIAL slot as vouchers) */
    acct->balance[capital] += external_amount;
    cc->voucher_float += external_amount;
    cc->voucher_cashin_total += external_amount;

    /* Record deposit in Vino ledger */
    /* Use a self-transfer to record the deposit on-chain */
    vino_transfer(cc->vino, account_addr, account_addr, 0, capital,
                  RAIL_VINO_NATIVE, "voucher-cashin");

    cc_update_coverage(cc);
    return external_amount;
}

uint64_t cc_voucher_cash_out(community_chest_t *cc, const char *account_addr,
                               uint64_t voucher_amount, capital_type_t capital) {
    if (!cc || !cc->vino || !account_addr || voucher_amount == 0) return 0;

    vino_account_t *acct = vino_get_account(cc->vino, account_addr);
    if (!acct) return 0;
    if (acct->balance[capital] < voucher_amount) return 0;

    /* Debit voucher balance */
    acct->balance[capital] -= voucher_amount;
    if (cc->voucher_float >= voucher_amount) {
        cc->voucher_float -= voucher_amount;
    }
    cc->voucher_cashout_total += voucher_amount;

    /* Record withdrawal in Vino ledger */
    vino_transfer(cc->vino, account_addr, account_addr, 0, capital,
                  RAIL_VINO_NATIVE, "voucher-cashout");

    cc_update_coverage(cc);
    return voucher_amount;
}

int32_t cc_purchase_with_vouchers(community_chest_t *cc, uint32_t app_id,
                                    const char *buyer_addr) {
    if (!cc) return -1;
    if (!cc->vino) return -3;

    cc_app_t *app = cc_get_app(cc, app_id);
    if (!app) return -1;
    if (app->type == CC_APP_FREE || app->price == 0) {
        /* Free app: just record download */
        cc_download_app(cc, app_id);
        return 0;
    }

    vino_account_t *buyer = vino_get_account(cc->vino, buyer_addr);
    if (!buyer) return -2;
    if (buyer->balance[CAP_FINANCIAL] < app->price) return -2;

    /* Debit buyer's voucher balance */
    buyer->balance[CAP_FINANCIAL] -= app->price;

    /* Calculate revenue split */
    uint64_t platform_rev, royalty_rev;
    cc_calc_revenue_split(app->price, app->dev_share_bp,
                           &platform_rev, &royalty_rev);

    app->total_revenue += app->price;
    cc->total_revenue += app->price;
    cc->total_dev_payouts += (app->price * app->dev_share_bp) / 10000;
    cc->total_platform_revenue += platform_rev;
    cc->total_royalty_revenue += royalty_rev;

    /* Record clearing transaction in Vino ledger */
    vino_transfer(cc->vino, buyer_addr, buyer_addr, 0, CAP_FINANCIAL,
                  RAIL_VINO_NATIVE, "cc-voucher-purchase");

    /* Record download */
    app->download_count++;
    cc->total_downloads++;

    cc_update_coverage(cc);
    return 0;
}

surplus_real_t cc_update_coverage(community_chest_t *cc) {
    if (!cc) return SR_ZERO;

    /* r: revenue ratio = apps with revenue / total listed apps */
    uint32_t revenue_apps = 0;
    uint32_t total_listed = 0;
    for (uint32_t i = 0; i < CC_MAX_APPS; i++) {
        if (!cc->apps[i].active) continue;
        if (cc->apps[i].state == CC_APP_LISTED ||
            cc->apps[i].state == CC_APP_INSTALLED ||
            cc->apps[i].state == CC_APP_VERIFIED) {
            total_listed++;
            if (cc->apps[i].total_revenue > 0) revenue_apps++;
        }
    }
    cc->m5.r = (total_listed == 0) ? SR_ZERO
        : SR_DIV(SR_FROM_INT((int64_t)revenue_apps), SR_FROM_INT((int64_t)total_listed));

    /* ell: installation rate = installed / listed */
    uint32_t installed = 0;
    for (uint32_t i = 0; i < CC_MAX_APPS; i++) {
        if (!cc->apps[i].active) continue;
        if (cc->apps[i].state == CC_APP_INSTALLED) installed++;
    }
    cc->m5.ell = (total_listed == 0) ? SR_ZERO
        : SR_DIV(SR_FROM_INT((int64_t)installed), SR_FROM_INT((int64_t)total_listed));

    /* Coverage hyperbola */
    surplus_real_t product = SR_MUL(cc->m5.r, cc->m5.ell);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(COVERAGE_FLOOR_NUM), SR_FROM_INT(COVERAGE_FLOOR_DEN));
    cc->coverage_ratio = SR_DIV(product, floor);

    return cc->coverage_ratio;
}
