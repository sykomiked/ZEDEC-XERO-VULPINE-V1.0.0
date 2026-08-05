/* financial.c — Full Financial Instruments Suite implementation
 *
 * Hardware-as-code: each instrument is a virtual device with registers,
 * DMA buffers, and IRQ flags. Three execution modes (DC/AC/PC) map
 * spot/rolling/derivative operations.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "financial.h"

static const char *inst_names[] = {
    "Equity", "Bond", "Commodity", "Future", "Option",
    "FX", "Swap", "Forward", "CFD", "ETF",
    "Index", "Currency", "Precious Metal", "Energy", "Agricultural",
    "Crypto", "Real Estate", "Art", "Collectible", "Voucher"
};

static const char *exec_mode_names[] = {
    "DC (Spot)", "AC (Rolling)", "PC (Derivative)"
};

const char *financial_instrument_name(instrument_type_t t) {
    if (t < INST_MAX) return inst_names[t];
    return "Unknown";
}

const char *financial_exec_mode_name(fin_exec_mode_t m) {
    if (m <= FIN_EXEC_PC) return exec_mode_names[m];
    return "Unknown";
}

void portfolio_init(portfolio_t *p) {
    p->num_instruments = 0;
    p->total_exposure = SR_ZERO;
    p->total_coverage = SR_ZERO;
    p->portfolio_var = SR_ZERO;
    p->portfolio_surplus = SR_ZERO;
    p->total_market_value = SR_ZERO;
    p->total_pnl = SR_ZERO;
    p->portfolio_m5.omega = 0;
    p->portfolio_m5.r = SR_ONE;
    p->portfolio_m5.ell = SR_ONE;
    p->portfolio_m5.phi = SR_ZERO;
    p->portfolio_m5.chi = 0;
}

uint32_t financial_create_instrument(portfolio_t *p,
                                      instrument_type_t type,
                                      fin_exec_mode_t mode,
                                      const char *ticker,
                                      surplus_real_t price,
                                      surplus_real_t quantity) {
    if (p->num_instruments >= 256) return 0xFFFFFFFF;
    financial_instrument_t *inst = &p->instruments[p->num_instruments];
    
    /* Power-on: initialize registers */
    inst->instrument_id = p->num_instruments;
    inst->type = type;
    inst->exec_mode = mode;
    inst->reg_price = price;
    inst->reg_quantity = quantity;
    inst->reg_strike = SR_ZERO;
    inst->reg_notional = price;
    inst->reg_volatility = SR_FROM_FLOAT(0.2);
    inst->reg_rate = SR_FROM_FLOAT(0.05);
    inst->reg_dividend = SR_ZERO;
    inst->reg_time = SR_FROM_FLOAT(1.0);
    inst->reg_basis = SR_ZERO;
    inst->reg_conv_yield = SR_ZERO;
    
    /* M⁵ coordinates */
    inst->m5.omega = p->num_instruments;
    inst->m5.r = price;
    inst->m5.ell = SR_ONE;
    inst->m5.phi = SR_ZERO;
    inst->m5.chi = 0;
    
    inst->coverage_ratio = SR_DIV(SR_MUL(price, SR_ONE),
                                   SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10)));
    inst->coverage_breached = false;
    
    /* Clear risk registers */
    inst->reg_delta = SR_ZERO;
    inst->reg_gamma = SR_ZERO;
    inst->reg_vega = SR_ZERO;
    inst->reg_theta = SR_ZERO;
    inst->reg_rho = SR_ZERO;
    
    /* Clear DMA */
    inst->dma_head = 0;
    inst->dma_tail = 0;
    uint32_t i;
    for (i = 0; i < 8; i++) inst->dma_settlement[i] = 0;
    
    /* Clear IRQs */
    inst->irq_margin_call = false;
    inst->irq_settlement = false;
    inst->irq_coverage_breach = false;
    inst->irq_limit_hit = false;
    
    inst->option_type = OPTION_CALL;
    inst->option_exercised = false;
    inst->reg_coupon = SR_ZERO;
    inst->reg_face_value = SR_ZERO;
    inst->reg_yield = SR_ZERO;
    inst->reg_maturity_years = 0;
    inst->counterparty_id = 0;
    inst->custodian_id = 0;
    
    /* Copy ticker */
    int j;
    for (j = 0; j < 15 && ticker && ticker[j]; j++)
        inst->ticker[j] = ticker[j];
    inst->ticker[j] = 0;
    inst->description[0] = 0;
    
    return p->num_instruments++;
}

int32_t financial_set_price(financial_instrument_t *inst, surplus_real_t price) {
    inst->reg_price = price;
    inst->m5.r = price;
    financial_check_coverage(inst);
    return 0;
}

int32_t financial_set_quantity(financial_instrument_t *inst, surplus_real_t qty) {
    inst->reg_quantity = qty;
    return 0;
}

int32_t financial_set_m5(financial_instrument_t *inst, const m5_coords_t *m5) {
    inst->m5 = *m5;
    financial_check_coverage(inst);
    return 0;
}

bool financial_check_coverage(financial_instrument_t *inst) {
    surplus_real_t product = SR_MUL(inst->m5.r, inst->m5.ell);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10));
    inst->coverage_ratio = SR_DIV(product, floor);
    inst->coverage_breached = SR_CMP(product, floor) < 0;
    if (inst->coverage_breached) {
        inst->irq_coverage_breach = true;
    }
    return !inst->coverage_breached;
}

/* ===== Pricing Models ===== */

surplus_real_t financial_price_option(const financial_instrument_t *inst) {
    /* Simplified Black-Scholes with M⁵ coverage adjustment
     * For call: C = S·N(d1) - K·e^(-rT)·N(d2)
     * For put:  P = K·e^(-rT)·N(-d2) - S·N(-d1)
     * M⁵ adjustment: multiply by coverage_ratio */
    surplus_real_t S = inst->reg_price;
    surplus_real_t K = inst->reg_strike;
    surplus_real_t sigma = inst->reg_volatility;
    surplus_real_t r = inst->reg_rate;
    surplus_real_t T = inst->reg_time;
    
    if (T == SR_ZERO || sigma == SR_ZERO) {
        /* Intrinsic value */
        if (inst->option_type == OPTION_CALL) {
            surplus_real_t iv = SR_SUB(S, K);
            return iv > 0 ? iv : SR_ZERO;
        } else {
            surplus_real_t iv = SR_SUB(K, S);
            return iv > 0 ? iv : SR_ZERO;
        }
    }
    
    /* d1 = [ln(S/K) + (r + σ²/2)T] / (σ√T) */
    surplus_real_t ln_SK = SR_LN(SR_DIV(S, K));
    surplus_real_t half_sigma_sq = SR_DIV(SR_MUL(sigma, sigma), SR_FROM_INT(2));
    surplus_real_t drift = SR_MUL(SR_ADD(r, half_sigma_sq), T);
    surplus_real_t d1_num = SR_ADD(ln_SK, drift);
    surplus_real_t sqrt_T = SR_SQRT(T);
    surplus_real_t d1_den = SR_MUL(sigma, sqrt_T);
    surplus_real_t d1 = (d1_den != SR_ZERO) ? SR_DIV(d1_num, d1_den) : SR_ZERO;
    
    /* Simplified N(d1) approximation: linear in [0,1] range */
    surplus_real_t N_d1 = SR_ADD(SR_FROM_FLOAT(0.5), SR_DIV(d1, SR_FROM_INT(4)));
    if (N_d1 < 0) N_d1 = SR_ZERO;
    if (N_d1 > SR_ONE) N_d1 = SR_ONE;
    
    surplus_real_t N_d2 = SR_ADD(SR_FROM_FLOAT(0.5), SR_DIV(SR_SUB(d1, SR_MUL(sigma, sqrt_T)), SR_FROM_INT(4)));
    if (N_d2 < 0) N_d2 = SR_ZERO;
    if (N_d2 > SR_ONE) N_d2 = SR_ONE;
    
    surplus_real_t disc = SR_ONE; /* e^(-rT) approximation */
    surplus_real_t price;
    
    if (inst->option_type == OPTION_CALL) {
        price = SR_SUB(SR_MUL(S, N_d1), SR_MUL(SR_MUL(K, disc), N_d2));
    } else {
        price = SR_SUB(SR_MUL(SR_MUL(K, disc), SR_SUB(SR_ONE, N_d2)),
                       SR_MUL(S, SR_SUB(SR_ONE, N_d1)));
    }
    
    if (price < 0) price = SR_ZERO;
    
    /* M⁵ coverage adjustment */
    price = SR_MUL(price, inst->coverage_ratio);
    
    return price;
}

surplus_real_t financial_price_future(const financial_instrument_t *inst) {
    /* F = S·e^((r-q)T) ≈ S·(1 + (r-q)·T) for small T */
    surplus_real_t S = inst->reg_price;
    surplus_real_t r = inst->reg_rate;
    surplus_real_t q = inst->reg_dividend;
    surplus_real_t T = inst->reg_time;
    surplus_real_t carry = SR_MUL(SR_SUB(r, q), T);
    surplus_real_t F = SR_MUL(S, SR_ADD(SR_ONE, carry));
    
    /* Add convenience yield */
    surplus_real_t conv = SR_MUL(inst->reg_conv_yield, T);
    F = SR_SUB(F, conv);
    
    /* M⁵ coverage adjustment */
    F = SR_MUL(F, inst->coverage_ratio);
    
    return F;
}

surplus_real_t financial_price_bond(const financial_instrument_t *inst) {
    /* Bond price = Σ (C/(1+y)^t) + F/(1+y)^T
     * Simplified: use coupon × annuity + face × discount */
    surplus_real_t C = inst->reg_coupon;
    surplus_real_t F = inst->reg_face_value;
    surplus_real_t y = inst->reg_yield;
    uint32_t n = inst->reg_maturity_years;
    
    if (n == 0 || y == SR_ZERO) return F;
    
    /* Annuity factor: (1 - (1+y)^(-n)) / y */
    surplus_real_t disc_factor = SR_ONE;
    uint32_t i;
    for (i = 0; i < n; i++) {
        disc_factor = SR_DIV(disc_factor, SR_ADD(SR_ONE, y));
    }
    
    surplus_real_t annuity = SR_DIV(SR_SUB(SR_ONE, disc_factor), y);
    surplus_real_t price = SR_ADD(SR_MUL(C, annuity), SR_MUL(F, disc_factor));
    
    /* M⁵ coverage adjustment */
    price = SR_MUL(price, inst->coverage_ratio);
    
    return price;
}

surplus_real_t financial_price_swap(const financial_instrument_t *inst) {
    /* Swap value = (fixed_rate - floating_rate) × notional × T */
    surplus_real_t fixed = inst->reg_rate;
    surplus_real_t floating = inst->reg_dividend; /* Reuse for floating rate */
    surplus_real_t notional = inst->reg_notional;
    surplus_real_t T = inst->reg_time;
    
    surplus_real_t value = SR_MUL(SR_MUL(SR_SUB(fixed, floating), notional), T);
    
    /* M⁵ coverage adjustment */
    value = SR_MUL(value, inst->coverage_ratio);
    
    return value;
}

void financial_compute_greeks(financial_instrument_t *inst) {
    if (inst->type != INST_OPTION) return;
    
    surplus_real_t S = inst->reg_price;
    surplus_real_t K = inst->reg_strike;
    surplus_real_t sigma = inst->reg_volatility;
    surplus_real_t r = inst->reg_rate;
    surplus_real_t T = inst->reg_time;
    
    if (T == SR_ZERO || sigma == SR_ZERO) return;
    
    /* Delta: ∂C/∂S = N(d1) */
    surplus_real_t ln_SK = SR_LN(SR_DIV(S, K));
    surplus_real_t half_sigma_sq = SR_DIV(SR_MUL(sigma, sigma), SR_FROM_INT(2));
    surplus_real_t d1_num = SR_ADD(ln_SK, SR_MUL(SR_ADD(r, half_sigma_sq), T));
    surplus_real_t d1_den = SR_MUL(sigma, SR_SQRT(T));
    surplus_real_t d1 = (d1_den != SR_ZERO) ? SR_DIV(d1_num, d1_den) : SR_ZERO;
    
    inst->reg_delta = SR_ADD(SR_FROM_FLOAT(0.5), SR_DIV(d1, SR_FROM_INT(4)));
    if (inst->reg_delta < 0) inst->reg_delta = SR_ZERO;
    if (inst->reg_delta > SR_ONE) inst->reg_delta = SR_ONE;
    
    if (inst->option_type == OPTION_PUT) {
        inst->reg_delta = SR_SUB(inst->reg_delta, SR_ONE);
    }
    
    /* Gamma: ∂²C/∂S² ≈ simplified */
    inst->reg_gamma = SR_DIV(SR_ONE, SR_MUL(S, SR_MUL(sigma, SR_SQRT(T))));
    
    /* Vega: ∂C/∂σ ≈ S·√T·n(d1) */
    inst->reg_vega = SR_MUL(S, SR_SQRT(T));
    
    /* Theta: ∂C/∂T ≈ -(S·σ·n(d1))/(2√T) - r·K·e^(-rT)·N(d2) */
    inst->reg_theta = SR_DIV(SR_MUL(S, sigma), SR_FROM_INT(2));
    inst->reg_theta = -SR_DIV(inst->reg_theta, SR_SQRT(T));
    
    /* Rho: ∂C/∂r ≈ K·T·e^(-rT)·N(d2) */
    inst->reg_rho = SR_MUL(K, T);
    
    /* Apply coverage adjustment */
    inst->reg_delta = SR_MUL(inst->reg_delta, inst->coverage_ratio);
    inst->reg_gamma = SR_MUL(inst->reg_gamma, inst->coverage_ratio);
    inst->reg_vega = SR_MUL(inst->reg_vega, inst->coverage_ratio);
}

int32_t financial_settle(financial_instrument_t *inst, surplus_real_t settlement_price) {
    /* DMA-like settlement: push to settlement queue */
    inst->dma_settlement[inst->dma_tail] = 1; /* Mark as settled */
    inst->dma_tail = (inst->dma_tail + 1) % 8;
    inst->irq_settlement = true;
    
    /* Update price */
    financial_set_price(inst, settlement_price);
    
    /* For options, check exercise */
    if (inst->type == INST_OPTION && !inst->option_exercised) {
        if (inst->option_type == OPTION_CALL) {
            if (SR_CMP(settlement_price, inst->reg_strike) > 0) {
                inst->option_exercised = true;
            }
        } else {
            if (SR_CMP(settlement_price, inst->reg_strike) < 0) {
                inst->option_exercised = true;
            }
        }
    }
    
    return 0;
}

void financial_portfolio_risk(portfolio_t *p, const predictive_config_t *cfg) {
    p->total_exposure = SR_ZERO;
    p->total_coverage = SR_ZERO;
    p->portfolio_var = SR_ZERO;
    p->portfolio_surplus = SR_ZERO;
    p->total_market_value = SR_ZERO;
    
    uint32_t i;
    for (i = 0; i < p->num_instruments; i++) {
        financial_instrument_t *inst = &p->instruments[i];
        
        /* Compute market value */
        surplus_real_t mv = SR_MUL(inst->reg_price, inst->reg_quantity);
        p->total_market_value = SR_ADD(p->total_market_value, mv);
        p->total_exposure = SR_ADD(p->total_exposure, mv);
        
        /* Coverage */
        p->total_coverage = SR_ADD(p->total_coverage, inst->coverage_ratio);
        
        /* CBL-based VaR */
        cbl_result_t cbl = edp_cbl(mv, inst->coverage_ratio,
                                    cfg->psi_amplitude_sq, cfg->N);
        p->portfolio_var = SR_ADD(p->portfolio_var, cbl.deterministic_worst);
        
        /* ISF surplus */
        surplus_real_t u = SR_DIV(inst->m5.r, SR_ADD(inst->m5.r, SR_ONE));
        p->portfolio_surplus = SR_ADD(p->portfolio_surplus, surplus_f(u, cfg->N));
    }
    
    if (p->num_instruments > 0) {
        p->total_coverage = SR_DIV(p->total_coverage, SR_FROM_INT(p->num_instruments));
        p->portfolio_surplus = SR_DIV(p->portfolio_surplus, SR_FROM_INT(p->num_instruments));
    }
}

surplus_real_t financial_m5_valuation(const financial_instrument_t *inst,
                                       const predictive_config_t *cfg) {
    /* M⁵ valuation: combine traditional pricing with surplus and risk */
    surplus_real_t traditional_price;
    
    switch (inst->type) {
        case INST_OPTION:
            traditional_price = financial_price_option(inst);
            break;
        case INST_FUTURE:
        case INST_FORWARD:
            traditional_price = financial_price_future(inst);
            break;
        case INST_BOND:
            traditional_price = financial_price_bond(inst);
            break;
        case INST_SWAP:
            traditional_price = financial_price_swap(inst);
            break;
        default:
            traditional_price = inst->reg_price;
    }
    
    /* ISF surplus contribution */
    surplus_real_t u = SR_DIV(inst->m5.r, SR_ADD(inst->m5.r, SR_ONE));
    surplus_real_t surplus_val = surplus_f(u, cfg->N);
    
    /* EDP risk adjustment */
    risk_result_t risk = edp_compute_risk(&inst->m5, traditional_price,
                                           cfg->psi_amplitude_sq);
    
    /* M⁵ valuation = traditional × (1 + surplus) - risk */
    surplus_real_t adjusted = SR_MUL(traditional_price,
                                      SR_ADD(SR_ONE, surplus_val));
    surplus_real_t m5_value = SR_SUB(adjusted, risk.magnitude);
    
    if (m5_value < 0) m5_value = SR_ZERO;
    return m5_value;
}

void financial_export_conventional(const financial_instrument_t *inst,
                                    conventional_position_t *out) {
    out->market_value = SR_MUL(inst->reg_price, inst->reg_quantity);
    out->book_value = inst->reg_notional;
    out->unrealized_pnl = SR_SUB(out->market_value, out->book_value);
    out->realized_pnl = SR_ZERO;
    out->delta_exposure = SR_MUL(inst->reg_delta, out->market_value);
    out->var_95 = SR_MUL(out->market_value, SR_FROM_FLOAT(0.05));
}
