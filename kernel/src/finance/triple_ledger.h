/* triple_ledger.h — Nine-Capital Triple-Ledger System
 *
 * Implements the VINO triple-ledger architecture:
 *   1. Financial ledger (rational axis — conventional accounting)
 *   2. Provenance ledger (logical axis — attestation/coverage)
 *   3. Externality ledger (imaginary axis — relationship/phase loading)
 *
 * Nine forms of capital:
 *   1. Financial (currency, deposits)
 *   2. Physical (gold, silver, commodities)
 *   3. Land/Natural (real estate, resources)
 *   4. Human (skills, knowledge)
 *   5. Social (relationships, networks)
 *   6. Intellectual (patents, IP)
 *   7. Cultural (traditions, brand)
 *   8. Spiritual (trust, reputation)
 *   9. Ecological (environmental, climate)
 *
 * Dual compatibility: all entries project to conventional
 * double-entry accounting (Debit/Credit) while preserving
 * M⁵ coordinates (ordinal, rational, logical, imaginary, choice).
 *
 * No-debt architecture with floating vouchers (merit-based credit).
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef TRIPLE_LEDGER_H
#define TRIPLE_LEDGER_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"
#include "edp_risk.h"
#include "vino.h"  /* capital_type_t, CAP_MAX defined here */

/* ===== Ledger Types ===== */

typedef enum {
    LEDGER_FINANCIAL   = 0,  /* Rational axis — conventional debits/credits */
    LEDGER_PROVENANCE  = 1,  /* Logical axis — attestation, coverage */
    LEDGER_EXTERNALITY = 2,  /* Imaginary axis — relationship phase */
    LEDGER_MAX         = 3
} ledger_type_t;

/* ===== Triple-Ledger Entry ===== */

typedef struct {
    uint64_t id;                    /* Unique entry ID (ordinal) */
    uint32_t timestamp;             /* Tick count */
    capital_type_t capital_type;    /* Which of the nine capitals */
    ledger_type_t ledger;           /* Which of the three ledgers */
    
    /* M⁵ coordinates */
    uint32_t omega;                 /* Ordinal: sequence position */
    surplus_real_t r;               /* Rational: magnitude (amount) */
    surplus_real_t ell;             /* Logical: attestation level [0,1] */
    surplus_real_t phi;             /* Imaginary: externality phase */
    uint32_t chi;                   /* Choice: agent/vantage */
    
    /* Conventional accounting compatibility */
    surplus_real_t debit;           /* Conventional debit */
    surplus_real_t credit;          /* Conventional credit */
    
    /* Provenance */
    uint32_t counterparty_id;       /* Counterparty UCI */
    uint32_t custodian_id;          /* Custodian UCI */
    char description[64];           /* Entry description */
    
    /* Coverage */
    bool coverage_verified;         /* r·ℓ ≥ 1.8 */
} ledger_entry_t;

/* ===== Floating Voucher (No-Debt Merit System) ===== */

typedef struct {
    uint64_t voucher_id;
    uint32_t issuer_id;             /* Issuing entity UCI */
    uint32_t holder_id;             /* Current holder UCI */
    capital_type_t capital_type;    /* Backed by which capital */
    surplus_real_t merit_value;     /* Merit-based value (not debt) */
    surplus_real_t coverage_ratio;  /* r·ℓ/1.8 backing ratio */
    surplus_real_t phase;           /* Externality phase */
    uint32_t issued_tick;           /* When issued */
    uint32_t expires_tick;          /* Floating expiry (0 = no expiry) */
    bool transferable;              /* Can be transferred */
    bool redeemed;
    char purpose[64];
} floating_voucher_t;

/* ===== Account ===== */

typedef struct {
    uint32_t account_id;
    uint32_t entity_id;             /* Owner UCI */
    capital_type_t capital_type;
    char name[64];
    
    /* Triple-ledger balances */
    surplus_real_t balance[LEDGER_MAX];  /* One per ledger */
    
    /* Conventional accounting balance (dual compatibility) */
    surplus_real_t conventional_balance;
    
    /* M⁵ aggregate */
    surplus_real_t coverage_ratio;  /* r·ℓ/1.8 */
    surplus_real_t externality_phase;
    
    /* Entry history */
    ledger_entry_t entries[256];
    uint32_t num_entries;
    
    /* Floating vouchers held */
    floating_voucher_t vouchers[64];
    uint32_t num_vouchers;
} account_t;

/* ===== Ledger System ===== */

typedef struct {
    account_t accounts[512];
    uint32_t num_accounts;
    
    /* Global counters */
    uint64_t next_entry_id;
    uint64_t next_voucher_id;
    
    /* System-wide metrics */
    surplus_real_t total_coverage;      /* Σ r·ℓ across all accounts */
    surplus_real_t total_externality;   /* Σ |iφ| across all accounts */
    surplus_real_t system_health;       /* Coverage-weighted health index */
    
    /* Conventional accounting compatibility */
    surplus_real_t total_assets;        /* Σ debits (conventional) */
    surplus_real_t total_liabilities;   /* Σ credits (conventional) */
    surplus_real_t total_equity;        /* Assets - Liabilities */
} triple_ledger_t;

/* ===== API ===== */

void triple_ledger_init(triple_ledger_t *tl);

/* Account management */
uint32_t triple_ledger_create_account(triple_ledger_t *tl,
                                       uint32_t entity_id,
                                       capital_type_t cap,
                                       const char *name);

/* Post entry to triple ledger */
int32_t triple_ledger_post(triple_ledger_t *tl,
                            uint32_t account_id,
                            ledger_type_t ledger,
                            surplus_real_t r,
                            surplus_real_t ell,
                            surplus_real_t phi,
                            surplus_real_t debit,
                            surplus_real_t credit,
                            uint32_t counterparty_id,
                            const char *description);

/* Transfer between accounts (triple-ledger + conventional) */
int32_t triple_ledger_transfer(triple_ledger_t *tl,
                                uint32_t from_id,
                                uint32_t to_id,
                                capital_type_t cap,
                                surplus_real_t amount,
                                surplus_real_t ell,
                                surplus_real_t phi,
                                const char *description);

/* Floating voucher operations */
uint64_t triple_ledger_issue_voucher(triple_ledger_t *tl,
                                      uint32_t issuer_id,
                                      uint32_t holder_id,
                                      capital_type_t cap,
                                      surplus_real_t merit_value,
                                      surplus_real_t coverage_ratio,
                                      const char *purpose);

int32_t triple_ledger_transfer_voucher(triple_ledger_t *tl,
                                        uint64_t voucher_id,
                                        uint32_t new_holder_id);

int32_t triple_ledger_redeem_voucher(triple_ledger_t *tl,
                                      uint64_t voucher_id,
                                      uint32_t account_id);

/* Coverage verification */
bool triple_ledger_verify_coverage(const triple_ledger_t *tl,
                                    uint32_t account_id);

surplus_real_t triple_ledger_account_coverage(const triple_ledger_t *tl,
                                                uint32_t account_id);

/* System health */
void triple_ledger_update_health(triple_ledger_t *tl);

/* Conventional accounting export (dual compatibility) */
typedef struct {
    surplus_real_t total_debits;
    surplus_real_t total_credits;
    surplus_real_t trial_balance;    /* Should be zero if balanced */
    surplus_real_t balance_sheet_assets;
    surplus_real_t balance_sheet_liabilities;
    surplus_real_t balance_sheet_equity;
    surplus_real_t income_statement_revenue;
    surplus_real_t income_statement_expenses;
    surplus_real_t net_income;
} conventional_report_t;

void triple_ledger_export_conventional(const triple_ledger_t *tl,
                                        conventional_report_t *report);

/* Capital type names */
const char *capital_type_name(capital_type_t c);
const char *ledger_type_name(ledger_type_t l);

#endif /* TRIPLE_LEDGER_H */
