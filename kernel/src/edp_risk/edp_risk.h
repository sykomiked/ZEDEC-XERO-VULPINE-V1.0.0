/* edp_risk.h — EDP Risk Calculus Engine
 *
 * Implements the M⁵ Curzi Manifold risk operators from the
 * Economic Determinism Papers (EDP-000 through EDP-029).
 *
 * Core concepts:
 *   M⁵ = ℕ × ℝ⁺ × [0,1] × iℝ × Χ (five-axis manifold)
 *   Coverage Hyperbola: r · ℓ ≥ 1.8
 *   ZPD: x/(0·y) = y (zero-preserving division)
 *   Fibonacci-Signed Paradox Algebra
 *   Risk operator R: M⁵ → ℂ (complex-valued, deterministic)
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef EDP_RISK_H
#define EDP_RISK_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"

/* ===== M⁵ Coordinates ===== */

typedef struct {
    uint32_t omega;       /* Ordinal axis: position, sequence, priority */
    surplus_real_t r;     /* Rational axis: magnitude, ratio, price */
    surplus_real_t ell;   /* Logical axis: existential-import truth ∈ [0,1] */
    surplus_real_t phi;   /* Imaginary axis: phase, externality loading */
    uint32_t chi;         /* Choice axis: observer vantage / agent state */
} m5_coords_t;

/* Coverage floor constant */
#define COVERAGE_FLOOR_NUM  18   /* 1.8 = 18/10 */
#define COVERAGE_FLOOR_DEN  10

/* ===== Ten Governing Constraints ===== */

/* III.1: Coverage Hyperbola — r · ℓ ≥ 1.8 */
bool edp_coverage_satisfied(const m5_coords_t *c);
surplus_real_t edp_coverage_product(const m5_coords_t *c);
surplus_real_t edp_coverage_deficit(const m5_coords_t *c);

/* III.4: Zero-Preserving Division (ZPD)
 * x / (0 · y) = y  — preserves class identity of divisor */
surplus_real_t edp_zpd(surplus_real_t x, surplus_real_t y);

/* III.7: Financial Heisenberg — σ(r) · σ(ℓ) ≥ κ */
typedef struct {
    surplus_real_t sigma_r;   /* Precision of rational measurement */
    surplus_real_t sigma_ell; /* Precision of logical measurement */
    surplus_real_t kappa;     /* Conjugate-precision constant */
} heisenberg_t;

bool edp_heisenberg_satisfied(const heisenberg_t *h);

/* ===== Fibonacci-Signed Paradox Algebra (EDP-000 §IV) ===== */

typedef struct {
    int32_t magnitude;    /* Fibonacci magnitude: 0,1,1,2,3,5,8,13,21,... */
    int32_t sign;         /* +1 or -1 (annihilator pair) */
    uint32_t level;       /* Fibonacci level (0=base, 1,2,3,...) */
    surplus_real_t phase; /* Phase φ for this assertion */
} fib_signed_t;

/* Get Fibonacci number */
uint32_t edp_fibonacci(uint32_t n);

/* Resolve annihilator pair: A + ¬A = signed-zero at level n */
fib_signed_t edp_annihilate(const fib_signed_t *a, const fib_signed_t *b);

/* Check if two assertions form an annihilator pair */
bool edp_are_annihilators(const fib_signed_t *a, const fib_signed_t *b);

/* ===== Credit Risk (EDP-001): PVLD ===== */

/* Presence-Verified Default: PVD = 1 - ℓ · 𝟙[r·ℓ ≥ 1.8] */
surplus_real_t edp_pvd(const m5_coords_t *c);

/* Coverage-Floor Recovery: CFR = max(0, 1 - r·ℓ/1.8) */
surplus_real_t edp_cfr(const m5_coords_t *c);

/* Externality-Adjusted Exposure: EAE = EAD · e^{iφ} */
typedef struct {
    surplus_real_t magnitude;  /* |EAE| = traditional EAD */
    surplus_real_t phase;      /* φ = externality loading */
} complex_exposure_t;

complex_exposure_t edp_eae(surplus_real_t ead, surplus_real_t phi);

/* Expected Loss under PVLD: EL = PVD · CFR · |EAE| */
surplus_real_t edp_expected_loss(const m5_coords_t *c, surplus_real_t ead);

/* Ordinal-Priority Discount: K_ω = K · π(ω, ω_max) */
surplus_real_t edp_ordinal_discount(uint32_t omega, uint32_t omega_max);

/* ZPD invocation: when ℓ → 0, R returns class identity */
surplus_real_t edp_credit_zpd(const m5_coords_t *c);

/* ===== Market Risk (EDP-002): CBL ===== */

/* Coverage-Bound Deterministic Loss Envelope
 * CBL = max_det{-ΔV} + E_Ψ[-ΔV(χ)] */
typedef struct {
    surplus_real_t deterministic_worst;  /* Max loss over verified M⁴ */
    surplus_real_t psi_expected_loss;    /* Wave-function expected loss on Χ */
} cbl_result_t;

cbl_result_t edp_cbl(surplus_real_t portfolio_value,
                      surplus_real_t coverage_ratio,
                      surplus_real_t psi_amplitude_sq,
                      uint32_t num_eigenstates);

/* Phase-Dependent Correlation: ρ_ab = |ρ| · e^{i(φ_a - φ_b)} */
typedef struct {
    surplus_real_t magnitude;
    surplus_real_t phase;
} complex_corr_t;

complex_corr_t edp_phase_correlation(surplus_real_t corr_mag,
                                      surplus_real_t phi_a,
                                      surplus_real_t phi_b);

/* Ordinal-Position Multiplier: CBL(ω) = CBL_base · (1 + ω/ω_max · κ_queue) */
surplus_real_t edp_ordinal_multiplier(uint32_t omega, uint32_t omega_max,
                                       surplus_real_t kappa_queue,
                                       surplus_real_t cbl_base);

/* Ψ-Collapse Reserve: K_choice = Σ |⟨ψ_k|Ψ⟩|² · ΔV_k */
surplus_real_t edp_psi_collapse_reserve(const surplus_real_t *amplitudes_sq,
                                         const surplus_real_t *delta_v,
                                         uint32_t num_states);

/* ===== Liquidity Risk (EDP-003): PDAR ===== */

/* Physical Delivery Availability Ratio: PDAR = HQLA_verified / Net Outflows */
surplus_real_t edp_pdar(surplus_real_t hqla_verified,
                         surplus_real_t net_outflows_30d);

/* Physical Delivery Stable Funding Ratio */
surplus_real_t edp_pdsfr(surplus_real_t available_stable,
                          surplus_real_t required_stable);

/* ===== Sovereign Risk (EDP-011): NC-SS ===== */

typedef struct {
    surplus_real_t coverage_ratio;    /* r · ℓ / 1.8 */
    surplus_real_t externality_phase; /* iφ loading */
    uint32_t reserve_quantum_n;       /* Principal quantum number */
    uint32_t reserve_quantum_l;       /* Azimuthal (composition type) */
    int32_t  reserve_quantum_s;       /* Spin (creditor/debtor) */
    surplus_real_t political_stability; /* [0,1] */
} sovereign_risk_input_t;

typedef struct {
    surplus_real_t risk_score;        /* |R| — real projection */
    surplus_real_t risk_phase;        /* arg(R) — externality */
    surplus_real_t coverage_deficit;
    bool selection_rule_violated;
    fib_signed_t rating;              /* NC-Rating (Fibonacci-signed) */
} sovereign_risk_result_t;

sovereign_risk_result_t edp_sovereign_risk(const sovereign_risk_input_t *input);

/* Reserve transition selection rules (EDP-000 §VI.2):
 * Δl = ±1, Δm = 0±1, Δs = 0 */
bool edp_selection_rule_valid(uint32_t l_old, uint32_t l_new,
                               int32_t m_old, int32_t m_new,
                               int32_t s_old, int32_t s_new);

/* ===== Systemic Risk (EDP-016): PCCI ===== */

/* Presence-Verified Contagion Index */
typedef struct {
    uint32_t node_id;
    surplus_real_t coverage_ratio;
    surplus_real_t externality_phase;
    uint32_t num_counterparties;
    surplus_real_t counterparty_exposure_total;
} systemic_node_t;

surplus_real_t edp_pcci(const systemic_node_t *nodes, uint32_t count);

/* ===== General Risk Operator R: M⁵ → ℂ ===== */

typedef struct {
    surplus_real_t real_part;      /* Re(R) — real projection */
    surplus_real_t imag_part;      /* Im(R) — externality loading */
    surplus_real_t magnitude;      /* |R| */
    surplus_real_t phase;          /* arg(R) */
    bool coverage_breached;
    bool zpd_invoked;
    fib_signed_t paradox_state;    /* Fibonacci-signed state */
} risk_result_t;

/* Compute general risk operator */
risk_result_t edp_compute_risk(const m5_coords_t *coords,
                                surplus_real_t exposure,
                                surplus_real_t psi_amplitude_sq);

/* ===== NC-Rating (Fibonacci-Signed Rating Scale) ===== */

typedef enum {
    NC_RATING_LEVEL_0 = 0,  /* Base: {0,1} — Boolean */
    NC_RATING_LEVEL_1 = 1,  /* ±1 — signed binary */
    NC_RATING_LEVEL_2 = 2,  /* ±2 — first magnitude */
    NC_RATING_LEVEL_3 = 3,  /* ±3, ±5 — Fibonacci jump */
    NC_RATING_LEVEL_4 = 4,  /* ±8, ±13 */
    NC_RATING_LEVEL_5 = 5,  /* ±21, ±34 */
} nc_rating_level_t;

fib_signed_t edp_nc_rating(surplus_real_t coverage_ratio, surplus_real_t externality);

/* Convert NC-Rating to legacy alphabetic scale */
const char *edp_rating_to_alpha(const fib_signed_t *rating);

#endif /* EDP_RISK_H */
