/* community_chest.h — Community Chest: P2P Decentralized App Store
 *
 * A native ZXV application marketplace that operates as a decentralized
 * peer-to-peer distribution platform. No centralized server hosting —
 * apps are seeded and distributed across the device mesh network via
 * trust-weighted stash buckets and JDR PirateNet transport.
 *
 * Key design principles (post-quantum, native ZXV):
 *   - All app packages are identified by 168-bit content hashes
 *   - All developer identities are 168-bit peer node IDs
 *   - App packages must be signed; verification uses the same
 *     pluggable signature scheme as Count House (Ed25519/ML-DSA-44 ready)
 *   - Porter House gates which peers can seed/download from this node
 *   - Revenue split: developer 70-85%, platform 15-24%, 36N9 royalty 20%
 *   - Free and paid apps supported; payments via Mesh-Token settlement
 *   - Apps can be AI models, native ZXV binaries, or Vena runtime scripts
 *
 * This is NOT an App Store or Play Store clone. It is a post-quantum
 * P2P marketplace native to the M5 Axiomatic architecture.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef COMMUNITY_CHEST_H
#define COMMUNITY_CHEST_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"
#include "surplus.h"
#include "edp_risk.h"
#include "vino.h"

/* ===== Constants ===== */

#define CC_MAX_APPS            128
#ifndef CC_MAX_NAME_LEN
#define CC_MAX_NAME_LEN        64
#endif
#define CC_MAX_DESC_LEN        128
#define CC_MAX_DEVELOPER_LEN   32
#define CC_MAX_SEEDERS         32
#define CC_SIG_LEN             64    /* post-quantum signature (pluggable) */
#define CC_PUBKEY_LEN          32    /* developer public key */
#define CC_CONTENT_HASH_LEN    21    /* 168-bit content hash */

/* Revenue split (in basis points, 10000 = 100%) */
#define CC_DEV_SHARE_MIN       7000   /* 70% minimum developer share */
#define CC_DEV_SHARE_MAX       8500   /* 85% maximum developer share */
#define CC_PLATFORM_SHARE_MIN  1500   /* 15% minimum platform share */
#define CC_PLATFORM_SHARE_MAX  2400   /* 24% maximum platform share */
#define CC_ROYALTY_SHARE       2000   /* 20% royalty to 36N9 Genetics */

/* ===== App Types ===== */

typedef enum {
    CC_APP_FREE       = 0,  /* free distribution */
    CC_APP_PAID       = 1,  /* one-time payment */
    CC_APP_SUBSCRIPTION = 2, /* recurring payment */
    CC_APP_DONATION   = 3   /* voluntary payment */
} cc_app_type_t;

typedef enum {
    CC_APP_NATIVE     = 0,  /* native ZXV binary (M5 Axiomatic) */
    CC_APP_VENA       = 1,  /* Vena runtime script */
    CC_APP_AI_MODEL   = 2,  /* AI model package */
    CC_APP_TOOL       = 3,  /* developer tool / utility */
    CC_APP_PLUGIN     = 4   /* enterprise plugin */
} cc_app_category_t;

typedef enum {
    CC_APP_UNUSED     = 0,
    CC_APP_LISTED     = 1,  /* published and available */
    CC_APP_INSTALLED  = 2,  /* installed on this device */
    CC_APP_DOWNLOADING = 3,  /* currently downloading via mesh */
    CC_APP_VERIFIED   = 4,  /* signature verified, ready to install */
    CC_APP_REJECTED   = 5,  /* signature verification failed */
    CC_APP_DEPRECATED = 6   /* developer marked as deprecated */
} cc_app_state_t;

/* ===== App Record ===== */

typedef struct cc_app {
    uint32_t id;
    char name[CC_MAX_NAME_LEN];
    char description[CC_MAX_DESC_LEN];
    char developer[CC_MAX_DEVELOPER_LEN];
    word168_t developer_id;           /* 168-bit peer node ID */
    uint8_t content_hash[CC_CONTENT_HASH_LEN]; /* 168-bit content hash */
    uint8_t dev_pubkey[CC_PUBKEY_LEN]; /* developer public key */
    uint8_t signature[CC_SIG_LEN];     /* post-quantum signature */
    bool sig_verified;

    cc_app_type_t type;               /* free, paid, subscription, donation */
    cc_app_category_t category;       /* native, vena, AI model, tool, plugin */
    cc_app_state_t state;

    uint64_t price;                   /* in token units (0 if free) */
    uint64_t download_count;          /* total downloads */
    uint64_t install_count;           /* successful installs */
    uint32_t seeder_count;            /* active mesh seeders */
    uint64_t total_revenue;           /* lifetime revenue */

    /* Revenue split for this app (basis points) */
    uint32_t dev_share_bp;            /* developer share */
    uint32_t platform_share_bp;       /* platform share */

    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;

    bool active;
} cc_app_t;

/* ===== Community Chest Store ===== */

typedef struct community_chest {
    uint32_t device_id;
    char name[CC_MAX_DEVELOPER_LEN];

    cc_app_t apps[CC_MAX_APPS];
    uint32_t num_apps;
    uint32_t next_id;

    /* Stats */
    uint64_t total_downloads;
    uint64_t total_revenue;
    uint64_t total_dev_payouts;
    uint64_t total_platform_revenue;
    uint64_t total_royalty_revenue;

    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;

    /* Vino Floating Voucher integration */
    vino_ledger_t *vino;              /* linked Vino ledger for voucher clearing */
    uint64_t voucher_float;           /* total voucher float in circulation */
    uint64_t voucher_cashin_total;    /* total external currency converted in */
    uint64_t voucher_cashout_total;   /* total vouchers redeemed out */

    /* Pluggable signature verification (mirrors count_house_t.verify_sig).
     * NULL = the built-in Ed25519 ASYMMETRIC verification against the embedded
     * public key, whose private key is held offline. Override to supply a
     * different trust root, or in tests that cannot mint a real signature. */
    bool (*verify_sig)(const cc_app_t *entry);
} community_chest_t;

/* ===== API ===== */

void cc_init(community_chest_t *cc, uint32_t device_id, const char *name);

/* List a new app in the store. Returns app ID on success, -1 if full,
 * -2 if signature verification fails. */
int32_t cc_list_app(community_chest_t *cc, const char *name,
                     const char *description, const char *developer,
                     const word168_t *developer_id,
                     const uint8_t dev_pubkey[CC_PUBKEY_LEN],
                     const uint8_t content_hash[CC_CONTENT_HASH_LEN],
                     const uint8_t signature[CC_SIG_LEN],
                     cc_app_type_t type, cc_app_category_t category,
                     uint64_t price, uint32_t dev_share_bp);

/* Verify an app's signature. Returns true if valid. */
bool cc_verify_app(community_chest_t *cc, uint32_t app_id);

/* Download an app (increments download count, initiates mesh transfer).
 * Returns 0 on success, -1 if app not found, -2 if app not listed. */
int32_t cc_download_app(community_chest_t *cc, uint32_t app_id);

/* Install a downloaded and verified app. Returns 0 on success. */
int32_t cc_install_app(community_chest_t *cc, uint32_t app_id);

/* Purchase a paid app (processes payment via revenue split).
 * Returns 0 on success, -1 if app not found, -2 if app is free. */
int32_t cc_purchase_app(community_chest_t *cc, uint32_t app_id);

/* Mark an app as deprecated. */
int32_t cc_deprecate_app(community_chest_t *cc, uint32_t app_id);

/* Get app by ID. Returns NULL if not found. */
cc_app_t *cc_get_app(community_chest_t *cc, uint32_t app_id);

/* Calculate revenue split for a purchase amount.
 * Returns developer payout, sets platform_revenue and royalty_revenue. */
uint64_t cc_calc_revenue_split(uint64_t amount, uint32_t dev_share_bp,
                                 uint64_t *platform_revenue,
                                 uint64_t *royalty_revenue);

/* Update M5 coverage for the store. */
surplus_real_t cc_update_coverage(community_chest_t *cc);

/* ===== Vino Floating Voucher API ===== */

/* Link a Vino ledger to the Community Chest for voucher clearing. */
void cc_link_vino(community_chest_t *cc, vino_ledger_t *vino);

/* Cash in: convert external currency to Vino Floating Vouchers.
 * Creates a TXN_DEPOSIT in the Vino ledger and credits the user's
 * voucher balance. Returns vouchers credited, or 0 on failure. */
uint64_t cc_voucher_cash_in(community_chest_t *cc, const char *account_addr,
                              uint64_t external_amount, capital_type_t capital);

/* Cash out: redeem Vino Floating Vouchers back to external currency.
 * Creates a TXN_WITHDRAW in the Vino ledger. Returns vouchers redeemed. */
uint64_t cc_voucher_cash_out(community_chest_t *cc, const char *account_addr,
                               uint64_t voucher_amount, capital_type_t capital);

/* Purchase an app using Vino Floating Vouchers instead of raw tokens.
 * Settles the purchase through the Vino ledger with a TXN_CLEAR
 * transaction, applies the revenue split, and records the sale.
 * Returns 0 on success, -1 if app not found, -2 if insufficient
 * voucher balance, -3 if no Vino ledger linked. */
int32_t cc_purchase_with_vouchers(community_chest_t *cc, uint32_t app_id,
                                    const char *buyer_addr);

#endif /* COMMUNITY_CHEST_H */
