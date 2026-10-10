/* financial_fabric.h — ZXV Financial Fabric Compound Module
 *
 * The Financial Fabric unifies all economic/financial modules into a single
 * coherent fabric for capital formation, derivatives, assurance, treaty
 * tokenization, mesh settlement, and market operations.
 *
 * Sub-modules integrated:
 *   1. Triple Ledger — Three-entry accounting (Source, Form, Destination)
 *   2. Derivatives — Nine-form derivatives with 100% backing enforcement
 *   3. Assurance — Pay-It-Forward proactive capital generation
 *   4. Treaty Tokenization — Conservation easements → Natural/Built capital tokens
 *   5. Mesh Token — Settlement along P2P trade routes
 *   6. Finance Markets — position/quote tracking and node-fee settlement
 *      (a tracker: no order book, no matching engine, no price discovery)
 *   7. Rails — Nine capital rails with Form→Rail mapping
 *   8. Crypto Bridge — a model of cross-chain bridge records (no chain
 *      connectivity, no on-chain verification)
 *   9. Vino Ledger — Nine-form capital ledger with vouchers
 *   10. Vena Runtime — Smart contracts for financial logic
 *
 * Design principles:
 * - All capital flows respect the Nine Forms taxonomy
 * - 100% backing enforced at kernel level (no fractional reserve)
 * - Temporal arbitrage only (phase-tick carry, no debt/leverage)
 * - Assurance inverts insurance: prevention → generation → forward
 * - Treaty tokens never fractionalized, backing ratio ≥ 1.0x
 * - Mesh settlement via Vino vouchers on JDR PirateNet transport
 * - Paraconsistent logic (LPRES) for all attestations
 * - M5 coverage hyperbola enforcement on all financial operations
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef FINANCIAL_FABRIC_H
#define FINANCIAL_FABRIC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "triple_ledger.h"
#include "derivatives.h"
#include "assurance.h"
#include "treaty_tokenization.h"
#include "mesh_token.h"
#include "finance_markets.h"
#include "rails.h"
#include "crypto_bridge.h"
#include "vino/vino.h"
#include "vena/vena.h"
#include "orbital_fabric.h"
#include "lpres.h"
#include "m5_types.h"
#include "m5_api.h"
#include "surplus.h"

/* fm_book_t, rail_system_t, bridge_registry_t are defined in their
 * respective submodule headers (included above). */

/* Use lpres_attestation_t for LPRES proofs */
typedef lpres_attestation_t m5_lpres_proof_t;

/* ===== Constants ===== */

#define FF_MAX_ACCOUNTS         256
#define FF_MAX_CONTRACTS        512
#define FF_MAX_ASSURANCES       256
#define FF_MAX_TREATY_ASSETS    128
#define FF_MAX_MESH_SETTLEMENTS 1024
#define FF_MAX_ORDER_BOOKS      64
#define FF_MAX_POSITIONS        1024
#define FF_MAX_NAME_LEN         64

/* Error codes beyond the generic -1 */
#define FF_EINALIENABLE (-2) /* state-reserved form (1..4): never transferred */
#define FF_ERAIL        (-3) /* rail does not carry this form */
#define FF_ENOTSUP      (-4) /* not implemented: nothing happened */

/* ===== Financial Fabric Account ===== */

typedef struct ff_account {
    uint32_t id;
    char name[FF_MAX_NAME_LEN];
    word168_t owner_id; /* 168-bit critical word identity */

    /* Nine-form capital balances (Vino) */
    uint64_t balances[9]; /* Forms 1-9: Social, Natural, Heritage, Governance, Financial, Material,
                             Living, Knowledge, Built */

    /* Active positions */
    uint32_t derivative_positions[FF_MAX_POSITIONS];
    uint32_t num_derivative_positions;
    uint32_t assurance_positions[FF_MAX_POSITIONS];
    uint32_t num_assurance_positions;
    uint32_t treaty_positions[FF_MAX_POSITIONS];
    uint32_t num_treaty_positions;

    /* Credit / reputation */
    surplus_real_t credit_score; /* 0.0 - 1.0 */
    surplus_real_t reputation;   /* 0.0 - 1.0 */

    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;

    /* Paraconsistent state */
    lpres_state_t attestation;
    bool active;
} ff_account_t;

/* ===== Derivative Position (extends m5_deriv_contract_t) ===== */

typedef struct ff_derivative_position {
    m5_deriv_contract_t contract; /* Base derivative contract */

    /* Position metadata */
    uint32_t account_id;           /* Owning account */
    int32_t quantity;              /* Positive = long, negative = short */
    uint64_t entry_price;          /* Entry price in base units */
    uint64_t current_price;        /* Current mark price */
    surplus_real_t unrealized_pnl; /* Unrealized P&L */
    surplus_real_t realized_pnl;   /* Realized P&L */

    /* Margin / backing */
    uint64_t margin_posted;   /* Capital posted as backing */
    uint64_t margin_required; /* Required margin (100% of notional) */
    bool margin_call;         /* True if margin insufficient */

    /* Temporal arbitrage */
    bool arb_active;          /* Currently in arb position */
    uint64_t arb_entry_tick;  /* Phase tick when arb entered */
    surplus_real_t arb_carry; /* Carry captured */

    /* LPRES attestation */
    lpres_state_t position_attestation;

    bool active;
} ff_derivative_position_t;

/* ===== Assurance Position (extends m5_assurance_contract_t) ===== */

typedef struct ff_assurance_position {
    m5_assurance_contract_t contract; /* Base assurance contract */

    /* Position metadata */
    uint32_t account_id;
    uint64_t contribution_posted;    /* Capital contributed */
    uint64_t capital_generated;      /* Capital generated from prevention */
    uint64_t capital_forwarded;      /* Capital forwarded to next assurance */
    surplus_real_t generation_ratio; /* Actual generation ratio achieved */

    /* Pay-it-forward chain */
    uint32_t forward_chain_id;       /* Chain identifier */
    uint32_t forward_depth;          /* Depth in forward chain */
    uint32_t forward_target_account; /* Account receiving forwarded capital */

    /* Verification status */
    bool generation_verified;
    uint64_t verification_tick;
    lpres_state_t verification_attestation;

    bool active;
} ff_assurance_position_t;

/* ===== Treaty Asset Position (extends m5_treaty_asset_t) ===== */

typedef struct ff_treaty_position {
    m5_treaty_asset_t asset; /* Base treaty asset */

    /* Position metadata */
    uint32_t account_id;
    uint64_t tokens_held; /* Number of treaty tokens held */
    uint64_t token_value; /* Current token value */

    /* Conservation metrics */
    surplus_real_t ecological_value;   /* Ecological value metric */
    surplus_real_t carbon_sequestered; /* Carbon sequestration (tons) */
    surplus_real_t biodiversity_index; /* Biodiversity index */

    /* Settlement */
    bool settled_on_rail_888; /* Settled on Externality rail */
    uint64_t settlement_tick;

    /* LPRES attestation */
    lpres_state_t asset_attestation;

    bool active;
} ff_treaty_position_t;

/* ===== Mesh Settlement ===== */

typedef struct ff_mesh_settlement {
    uint32_t id;
    uint32_t trade_route_id; /* Mesh Net trade route */
    uint32_t source_account;
    uint32_t dest_account;

    /* Settlement details */
    uint64_t amount;         /* Vino voucher amount */
    uint8_t capital_form;    /* Capital form being settled (1-9) */
    uint64_t price_per_unit; /* Settlement price */
    uint64_t quantity;       /* Quantity settled */

    /* Timing */
    uint64_t initiated_tick;
    uint64_t completed_tick;
    uint64_t deadline_tick;

    /* Status */
    bool completed;
    bool disputed;
    lpres_state_t settlement_attestation;

    /* Mesh Token reference */
    uint32_t mesh_token_id; /* Mesh-Token settlement ID */

    bool active;
} ff_mesh_settlement_t;

/* ===== Order Book (extends finance_markets) ===== */

typedef struct ff_order {
    uint64_t price;
    int64_t quantity; /* Positive = bid, negative = ask */
    uint32_t account_id;
    uint64_t timestamp;
    bool active;
} ff_order_t;

typedef struct ff_order_book {
    uint32_t id;
    char symbol[FF_MAX_NAME_LEN]; /* e.g., "FINANCIAL/MATERIAL" */
    uint8_t base_form;            /* Base capital form */
    uint8_t quote_form;           /* Quote capital form */

    /* Orders */
    ff_order_t orders[256];
    uint32_t num_orders;

    /* Market data */
    uint64_t best_bid;
    uint64_t best_ask;
    uint64_t last_price;
    uint64_t volume_24h;
    surplus_real_t spread_bps;

    /* M5 coverage for this market */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;

    bool active;
} ff_order_book_t;

/* ===== Financial Fabric ===== */

typedef struct financial_fabric {
    /* Core sub-modules */
    triple_ledger_t ledger; /* Triple Ledger */
    vino_ledger_t vino;     /* Vino Ledger */
    vena_runtime_t vena;    /* Vena Runtime */

    /* Financial modules */
    struct {
        m5_deriv_contract_t contracts[FF_MAX_CONTRACTS];
        uint32_t num_contracts;
    } derivatives;

    struct {
        m5_assurance_contract_t contracts[FF_MAX_ASSURANCES];
        uint32_t num_contracts;
    } assurances;

    struct {
        m5_treaty_asset_t assets[FF_MAX_TREATY_ASSETS];
        uint32_t num_assets;
    } treaty;

    struct {
        mesh_token_t tokens[FF_MAX_MESH_SETTLEMENTS];
        uint32_t num_tokens;
    } mesh_token;

    fm_book_t markets;        /* Finance Markets */
    rail_system_t rails;      /* Capital Rails */
    bridge_registry_t bridge; /* Crypto Bridge */

    /* Fabric-level state */
    ff_account_t accounts[FF_MAX_ACCOUNTS];
    uint32_t num_accounts;
    uint32_t next_account_id;

    ff_derivative_position_t deriv_positions[FF_MAX_POSITIONS];
    uint32_t num_deriv_positions;

    ff_assurance_position_t assur_positions[FF_MAX_POSITIONS];
    uint32_t num_assur_positions;

    ff_treaty_position_t treaty_positions[FF_MAX_POSITIONS];
    uint32_t num_treaty_positions;

    ff_mesh_settlement_t settlements[FF_MAX_MESH_SETTLEMENTS];
    uint32_t num_settlements;

    ff_order_book_t order_books[FF_MAX_ORDER_BOOKS];
    uint32_t num_order_books;

    /* Orbital Fabric integration */
    orbital_fabric_t *orbital; /* Reference to orbital fabric */

    /* Global statistics */
    struct {
        uint64_t total_accounts_created;
        uint64_t total_derivatives_traded;
        uint64_t total_assurances_created;
        uint64_t total_treaty_assets_tokenized;
        uint64_t total_mesh_settlements;
        uint64_t total_volume;
        uint64_t total_fees_collected;
        uint64_t total_margin_calls;
        uint64_t total_liquidations;
        uint64_t total_arb_profits;
        uint64_t total_forwarded_capital;
    } stats;

    /* Paraconsistent global state */
    lpres_state_t global_attestation;
    bool global_safety_gate;

    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    surplus_real_t min_coverage_ratio;

    /* Configuration */
    struct {
        bool enforce_100_percent_backing;
        bool allow_temporal_arb;
        bool require_treaty_rail_888;
        surplus_real_t min_margin_ratio; /* 1.0 = 100% */
        surplus_real_t max_leverage;     /* 1.0 = no leverage */
        uint32_t settlement_cycles;      /* Settlement period */
        bool auto_liquidate_margin_calls;
    } config;

    bool initialized;
} financial_fabric_t;

/* ===== API ===== */

/* Initialize the Financial Fabric */
void ff_init(financial_fabric_t *fabric, orbital_fabric_t *orbital);

/* Register built-in financial modules */
void ff_register_builtins(financial_fabric_t *fabric);

/* ===== Account Management ===== */

int32_t ff_create_account(financial_fabric_t *fabric, const char *name, const word168_t *owner_id,
                          const uint64_t initial_balances[9]);

ff_account_t *ff_get_account(financial_fabric_t *fabric, uint32_t account_id);
ff_account_t *ff_get_account_by_name(financial_fabric_t *fabric, const char *name);

/* Capital operations. Forms are 1-based (1..9). Transfers of the
 * state-reserved forms 1..4 return FF_EINALIENABLE. */
int32_t ff_transfer_capital(financial_fabric_t *fabric, uint32_t from_account, uint32_t to_account,
                            uint8_t form, uint64_t amount);

int32_t ff_mint_voucher(financial_fabric_t *fabric, uint32_t account_id, uint8_t form,
                        uint64_t amount);

int32_t ff_burn_voucher(financial_fabric_t *fabric, uint32_t account_id, uint8_t form,
                        uint64_t amount);

/* Triple Ledger entry */
int32_t ff_ledger_entry(financial_fabric_t *fabric, uint32_t account_id, uint8_t source_form,
                        uint8_t dest_form, uint64_t amount, const char *memo);

/* ===== Derivatives ===== */

int32_t ff_create_derivative(financial_fabric_t *fabric, uint32_t account_id,
                             m5_capital_form_t underlying_form, const surplus_real_t *notional,
                             const surplus_real_t *strike, uint64_t expiry_tick,
                             const uint8_t backing_cid[32], const m5_lpres_proof_t *backing_proof,
                             const uint8_t jurisdiction_cid[32], bool treaty_backed);

int32_t ff_settle_derivative(financial_fabric_t *fabric, uint32_t position_id);

int32_t ff_execute_temporal_arb(financial_fabric_t *fabric, uint32_t position_a_id,
                                uint32_t position_b_id, uint64_t settlement_tick);

int32_t ff_mark_to_market(financial_fabric_t *fabric, uint32_t position_id, uint64_t current_price);

int32_t ff_check_margin(financial_fabric_t *fabric, uint32_t position_id);

/* ===== Assurance ===== */

int32_t ff_create_assurance(financial_fabric_t *fabric, uint32_t account_id,
                            const surplus_real_t *contribution, m5_capital_form_t target_form,
                            const uint8_t prevention_cid[32],
                            const m5_lpres_proof_t *efficacy_proof,
                            m5_capital_form_t generated_form,
                            const surplus_real_t *generation_ratio, bool pay_it_forward,
                            const uint8_t forward_cid[32], const surplus_real_t *forward_phase);

int32_t ff_verify_generation(financial_fabric_t *fabric, uint32_t position_id);

int32_t ff_forward_capital(financial_fabric_t *fabric, uint32_t position_id);

/* ===== Treaty Tokenization ===== */

int32_t ff_tokenize_treaty(financial_fabric_t *fabric, uint32_t account_id,
                           const uint8_t treaty_cid[32], const uint8_t asset_cid[32],
                           m5_capital_form_t form, const surplus_real_t *quantified_value,
                           const m5_lpres_proof_t *sovereignty_proof,
                           const uint8_t corridor_cid[32]);

int32_t ff_verify_treaty_asset(financial_fabric_t *fabric, uint32_t position_id);

/* ===== Mesh Settlement ===== */

int32_t ff_initiate_settlement(financial_fabric_t *fabric, uint32_t trade_route_id,
                               uint32_t source_account, uint32_t dest_account, uint8_t capital_form,
                               uint64_t amount, uint64_t price_per_unit, uint64_t deadline_tick);

int32_t ff_complete_settlement(financial_fabric_t *fabric, uint32_t settlement_id);

int32_t ff_dispute_settlement(financial_fabric_t *fabric, uint32_t settlement_id);

/* ===== Order Books / Markets ===== */

int32_t ff_create_order_book(financial_fabric_t *fabric, const char *symbol, uint8_t base_form,
                             uint8_t quote_form);

int32_t ff_place_order(financial_fabric_t *fabric, uint32_t book_id, uint32_t account_id,
                       uint64_t price, int64_t quantity);

int32_t ff_cancel_order(financial_fabric_t *fabric, uint32_t book_id, uint32_t order_index);

int32_t ff_match_orders(financial_fabric_t *fabric, uint32_t book_id);

/* ===== Rails =====
 * Rail ids are the RAIL_FINANCIAL (DEBIT), RAIL_PROVENANCE (CREDIT) and
 * RAIL_EXTERNALITY (EQUITY) codes in finance/capital_forms.h, held in a
 * uint16_t so that 888 fits. ff_rail_supports_form() consults the
 * form -> rail table there; ff_route_through_rail() refuses state-reserved
 * forms (FF_EINALIENABLE) and forms the rail does not carry (FF_ERAIL). */

int32_t ff_route_through_rail(financial_fabric_t *fabric, uint32_t from_account,
                              uint32_t to_account, uint8_t form, uint64_t amount, uint16_t rail_id);

bool ff_rail_supports_form(rail_system_t *rails, uint8_t form, uint16_t rail_id);

/* ===== Crypto Bridge: not implemented, returns FF_ENOTSUP ===== */

int32_t ff_bridge_asset(financial_fabric_t *fabric, uint32_t account_id, uint8_t form,
                        uint64_t amount, const char *target_chain, const char *target_address);

/* ===== Health & Attestation ===== */

int32_t ff_check_account_health(financial_fabric_t *fabric, uint32_t account_id, void *health_out);

int32_t ff_check_global_health(financial_fabric_t *fabric);

bool ff_global_safety_gate(financial_fabric_t *fabric);

lpres_state_t ff_attest(financial_fabric_t *fabric, uint32_t account_id, uint32_t op_id, void *args,
                        int32_t result);

/* Coverage enforcement */
void ff_update_coverage(financial_fabric_t *fabric);
bool ff_enforce_coverage(financial_fabric_t *fabric, surplus_real_t min_ratio);

/* Statistics */
void ff_get_stats(financial_fabric_t *fabric, void *stats_out);

/* Paraconsistent state */
lpres_state_t ff_get_attestation(financial_fabric_t *fabric, uint32_t account_id);
void ff_set_attestation(financial_fabric_t *fabric, uint32_t account_id, lpres_state_t state);

/* Utility */
const char *ff_form_name(uint8_t form);
const char *ff_rail_name(uint16_t rail);
const char *ff_lpres_state_name(lpres_state_t state);

#endif /* FINANCIAL_FABRIC_H */
