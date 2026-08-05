/* financial.h — Full Financial Instruments Suite
 *
 * Hardware-as-code financial instrument processor with:
 *   - Equities, Bonds, Commodities, Futures, Options, FX, Derivatives
 *   - Nine-capital valuation (M⁵-native)
 *   - Dual compatibility with conventional accounting
 *   - Three execution modes: DC (direct), AC (alternating), PC (photonic)
 *   - Double quantization: each instrument is a quantum device
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef FINANCIAL_H
#define FINANCIAL_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"
#include "edp_risk.h"
#include "triple_ledger.h"
#include "predictive_model.h"

/* ===== Instrument Types ===== */

typedef enum {
    INST_EQUITY       = 0,
    INST_BOND         = 1,
    INST_COMMODITY    = 2,
    INST_FUTURE       = 3,
    INST_OPTION       = 4,
    INST_FX           = 5,
    INST_SWAP         = 6,
    INST_FORWARD      = 7,
    INST_CFD          = 8,
    INST_ETF          = 9,
    INST_INDEX        = 10,
    INST_CURRENCY     = 11,
    INST_PRECIOUS_METAL = 12,
    INST_ENERGY       = 13,
    INST_AGRICULTURAL = 14,
    INST_CRYPTO       = 15,
    INST_REAL_ESTATE  = 16,
    INST_ART          = 17,
    INST_COLLECTIBLE  = 18,
    INST_VOUCHER      = 19,  /* Floating voucher as instrument */
    INST_MAX          = 20
} instrument_type_t;

/* ===== Option Types ===== */

typedef enum {
    OPTION_CALL       = 0,
    OPTION_PUT        = 1,
    OPTION_STRADDLE   = 2,
    OPTION_STRANGLE   = 3,
    OPTION_SPREAD     = 4,
    OPTION_BINARY     = 5,
    OPTION_BARRIER    = 6,
    OPTION_ASIAN      = 7,
    OPTION_LOOKBACK   = 8,
} option_type_t;

/* ===== Execution Mode (hardware-as-code: DC/AC/PC) ===== */

typedef enum {
    FIN_EXEC_DC = 0,  /* Direct code — spot/settlement (like DC current) */
    FIN_EXEC_AC = 1,  /* Alternating code — rolling/periodic (like AC current) */
    FIN_EXEC_PC = 2,  /* Phase code — derivative/photonic (like photonic current) */
} fin_exec_mode_t;

/* ===== Instrument Register Map (hardware-as-code) ===== */

typedef struct {
    uint32_t instrument_id;        /* Device address */
    instrument_type_t type;        /* Device class */
    fin_exec_mode_t exec_mode;     /* Current mode */
    
    /* Register bank — like hardware registers */
    surplus_real_t reg_price;       /* Current price (spot) */
    surplus_real_t reg_strike;      /* Strike price (options) */
    surplus_real_t reg_notional;    /* Notional amount */
    surplus_real_t reg_quantity;    /* Position size */
    surplus_real_t reg_volatility;  /* Implied vol (σ) */
    surplus_real_t reg_rate;        /* Interest rate (r) */
    surplus_real_t reg_dividend;    /* Dividend yield (q) */
    surplus_real_t reg_time;        /* Time to expiry (T) */
    surplus_real_t reg_basis;       /* Basis (futures) */
    surplus_real_t reg_conv_yield;  /* Convenience yield */
    
    /* M⁵ coordinates */
    m5_coords_t m5;                /* Full manifold position */
    
    /* Coverage */
    surplus_real_t coverage_ratio;  /* r·ℓ/1.8 */
    bool coverage_breached;
    
    /* Risk registers */
    surplus_real_t reg_delta;       /* Δ — price sensitivity */
    surplus_real_t reg_gamma;       /* Γ — delta sensitivity */
    surplus_real_t reg_vega;        /* ν — vol sensitivity */
    surplus_real_t reg_theta;       /* Θ — time decay */
    surplus_real_t reg_rho;         /* ρ — rate sensitivity */
    
    /* DMA buffer — settlement queue */
    uint32_t dma_settlement[8];     /* Settlement queue positions */
    uint32_t dma_head;
    uint32_t dma_tail;
    
    /* IRQ flags */
    bool irq_margin_call;
    bool irq_settlement;
    bool irq_coverage_breach;
    bool irq_limit_hit;
    
    /* Option-specific */
    option_type_t option_type;
    bool option_exercised;
    
    /* Bond-specific */
    surplus_real_t reg_coupon;      /* Coupon rate */
    surplus_real_t reg_face_value;  /* Face value */
    surplus_real_t reg_yield;       /* Yield to maturity */
    uint32_t reg_maturity_years;
    
    /* Counterparty */
    uint32_t counterparty_id;
    uint32_t custodian_id;
    
    char ticker[16];
    char description[64];
} financial_instrument_t;

/* ===== Portfolio (collection of instruments) ===== */

typedef struct {
    financial_instrument_t instruments[256];
    uint32_t num_instruments;
    
    /* Portfolio-level M⁵ */
    m5_coords_t portfolio_m5;
    surplus_real_t total_exposure;
    surplus_real_t total_coverage;
    surplus_real_t portfolio_var;     /* Value at Risk (CBL-based) */
    surplus_real_t portfolio_surplus;  /* ISF surplus */
    
    /* Conventional compatibility */
    surplus_real_t total_market_value;
    surplus_real_t total_pnl;
} portfolio_t;

/* ===== API ===== */

void portfolio_init(portfolio_t *p);

/* Instrument creation — like powering on a device */
uint32_t financial_create_instrument(portfolio_t *p,
                                      instrument_type_t type,
                                      fin_exec_mode_t mode,
                                      const char *ticker,
                                      surplus_real_t price,
                                      surplus_real_t quantity);

/* Instrument operations — like writing to registers */
int32_t financial_set_price(financial_instrument_t *inst, surplus_real_t price);
int32_t financial_set_quantity(financial_instrument_t *inst, surplus_real_t qty);
int32_t financial_set_m5(financial_instrument_t *inst, const m5_coords_t *m5);

/* Pricing models */
surplus_real_t financial_price_option(const financial_instrument_t *inst);
surplus_real_t financial_price_future(const financial_instrument_t *inst);
surplus_real_t financial_price_bond(const financial_instrument_t *inst);
surplus_real_t financial_price_swap(const financial_instrument_t *inst);

/* Greeks computation (M⁵-enhanced) */
void financial_compute_greeks(financial_instrument_t *inst);

/* Coverage check */
bool financial_check_coverage(financial_instrument_t *inst);

/* Settlement (DMA-like operation) */
int32_t financial_settle(financial_instrument_t *inst, surplus_real_t settlement_price);

/* Portfolio risk assessment */
void financial_portfolio_risk(portfolio_t *p, const predictive_config_t *cfg);

/* Instrument type name */
const char *financial_instrument_name(instrument_type_t t);
const char *financial_exec_mode_name(fin_exec_mode_t m);

/* M⁵-enhanced valuation (replaces DCF, DDM, multiples) */
surplus_real_t financial_m5_valuation(const financial_instrument_t *inst,
                                       const predictive_config_t *cfg);

/* Conventional compatibility: export to standard accounting */
typedef struct {
    surplus_real_t market_value;
    surplus_real_t book_value;
    surplus_real_t unrealized_pnl;
    surplus_real_t realized_pnl;
    surplus_real_t delta_exposure;
    surplus_real_t var_95;
} conventional_position_t;

void financial_export_conventional(const financial_instrument_t *inst,
                                    conventional_position_t *out);

#endif /* FINANCIAL_H */
