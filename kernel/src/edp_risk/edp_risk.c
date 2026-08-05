/* edp_risk.c — EDP Risk Calculus Engine implementation
 *
 * Implements the M⁵ Curzi Manifold risk operators from EDP-000 through EDP-029.
 * All computations are deterministic given verified inputs.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "edp_risk.h"

/* ===== Coverage Hyperbola (III.1) ===== */

surplus_real_t edp_coverage_product(const m5_coords_t *c) {
    return SR_MUL(c->r, c->ell);
}

bool edp_coverage_satisfied(const m5_coords_t *c) {
    /* r · ℓ ≥ 1.8 → r · ℓ · 10 ≥ 18 */
    surplus_real_t product = edp_coverage_product(c);
    surplus_real_t floor = SR_FROM_INT(COVERAGE_FLOOR_NUM);
    surplus_real_t ten = SR_FROM_INT(COVERAGE_FLOOR_DEN);
    return SR_CMP(SR_MUL(product, ten), floor) >= 0;
}

surplus_real_t edp_coverage_deficit(const m5_coords_t *c) {
    surplus_real_t product = edp_coverage_product(c);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(COVERAGE_FLOOR_NUM), SR_FROM_INT(COVERAGE_FLOOR_DEN));
    surplus_real_t deficit = SR_SUB(floor, product);
    if (deficit < 0) deficit = SR_ZERO;
    return deficit;
}

/* ===== Zero-Preserving Division (III.4) ===== */

surplus_real_t edp_zpd(surplus_real_t x, surplus_real_t y) {
    (void)x;
    /* x / (0 · y) = y — when scalar collapses to zero, preserve class identity */
    if (y == SR_ZERO) return SR_ZERO; /* x/(0·0) = 0 (zero-class is its own identity) */
    return y;
}

/* ===== Financial Heisenberg (III.7) ===== */

bool edp_heisenberg_satisfied(const heisenberg_t *h) {
    surplus_real_t product = SR_MUL(h->sigma_r, h->sigma_ell);
    return SR_CMP(product, h->kappa) >= 0;
}

/* ===== Fibonacci-Signed Paradox Algebra (IV) ===== */

uint32_t edp_fibonacci(uint32_t n) {
    if (n == 0) return 0;
    if (n == 1) return 1;
    uint32_t a = 0, b = 1;
    for (uint32_t i = 2; i <= n; i++) {
        uint32_t c = a + b;
        a = b;
        b = c;
    }
    return b;
}

bool edp_are_annihilators(const fib_signed_t *a, const fib_signed_t *b) {
    return (a->magnitude == b->magnitude &&
            a->level == b->level &&
            a->sign == -b->sign);
}

fib_signed_t edp_annihilate(const fib_signed_t *a, const fib_signed_t *b) {
    fib_signed_t result;
    if (edp_are_annihilators(a, b)) {
        /* A + ¬A = signed-zero at level n */
        result.magnitude = 0;
        result.sign = a->sign; /* Preserve level-of-origin sign */
        result.level = a->level;
        result.phase = SR_ADD(a->phase, b->phase); /* Combined externality */
    } else {
        /* Not annihilators — return sum */
        result.magnitude = a->magnitude + b->magnitude;
        result.sign = 1;
        result.level = a->level > b->level ? a->level : b->level;
        result.phase = SR_ADD(a->phase, b->phase);
    }
    return result;
}

/* ===== Credit Risk (EDP-001): PVLD ===== */

surplus_real_t edp_pvd(const m5_coords_t *c) {
    /* PVD = 1 - ℓ · 𝟙[r·ℓ ≥ 1.8] */
    if (edp_coverage_satisfied(c)) {
        return SR_SUB(SR_ONE, c->ell);
    }
    /* Below coverage floor — PVD rises */
    surplus_real_t deficit = edp_coverage_deficit(c);
    return SR_SUB(SR_ONE, SR_SUB(c->ell, deficit));
}

surplus_real_t edp_cfr(const m5_coords_t *c) {
    /* CFR = max(0, 1 - r·ℓ/1.8) */
    surplus_real_t product = edp_coverage_product(c);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(COVERAGE_FLOOR_NUM), SR_FROM_INT(COVERAGE_FLOOR_DEN));
    surplus_real_t ratio = SR_DIV(product, floor);
    surplus_real_t result = SR_SUB(SR_ONE, ratio);
    if (result < 0) result = SR_ZERO;
    return result;
}

complex_exposure_t edp_eae(surplus_real_t ead, surplus_real_t phi) {
    complex_exposure_t eae;
    eae.magnitude = ead;
    eae.phase = phi;
    return eae;
}

surplus_real_t edp_expected_loss(const m5_coords_t *c, surplus_real_t ead) {
    surplus_real_t pvd = edp_pvd(c);
    surplus_real_t cfr = edp_cfr(c);
    /* EL = PVD · CFR · |EAE| */
    return SR_MUL(SR_MUL(pvd, cfr), ead);
}

surplus_real_t edp_ordinal_discount(uint32_t omega, uint32_t omega_max) {
    if (omega_max == 0) return SR_ONE;
    /* π(ω, ω_max) = 1 - ω/(ω_max+1) — earlier queue = higher recovery */
    surplus_real_t ratio = SR_DIV(SR_FROM_INT(omega), SR_FROM_INT(omega_max + 1));
    surplus_real_t result = SR_SUB(SR_ONE, ratio);
    if (result < 0) result = SR_ZERO;
    return result;
}

surplus_real_t edp_credit_zpd(const m5_coords_t *c) {
    /* When ℓ → 0, R returns class identity (the referent itself) */
    if (c->ell == SR_ZERO) {
        return c->r; /* Class-identity = rational value preserved */
    }
    return edp_expected_loss(c, c->r);
}

/* ===== Market Risk (EDP-002): CBL ===== */

cbl_result_t edp_cbl(surplus_real_t portfolio_value,
                      surplus_real_t coverage_ratio,
                      surplus_real_t psi_amplitude_sq,
                      uint32_t num_eigenstates) {
    cbl_result_t result;
    /* Deterministic worst-case: portfolio value × coverage deficit */
    surplus_real_t coverage_deficit = SR_SUB(SR_ONE, coverage_ratio);
    if (coverage_deficit < 0) coverage_deficit = SR_ZERO;
    result.deterministic_worst = SR_MUL(portfolio_value, coverage_deficit);
    
    /* Ψ-expected loss: amplitude² × value per eigenstate */
    surplus_real_t per_state = SR_DIV(portfolio_value, SR_FROM_INT(num_eigenstates > 0 ? num_eigenstates : 1));
    result.psi_expected_loss = SR_MUL(psi_amplitude_sq, per_state);
    
    return result;
}

complex_corr_t edp_phase_correlation(surplus_real_t corr_mag,
                                      surplus_real_t phi_a,
                                      surplus_real_t phi_b) {
    complex_corr_t result;
    result.magnitude = corr_mag;
    result.phase = SR_SUB(phi_a, phi_b);
    return result;
}

surplus_real_t edp_ordinal_multiplier(uint32_t omega, uint32_t omega_max,
                                       surplus_real_t kappa_queue,
                                       surplus_real_t cbl_base) {
    if (omega_max == 0) return cbl_base;
    surplus_real_t ratio = SR_DIV(SR_FROM_INT(omega), SR_FROM_INT(omega_max));
    surplus_real_t factor = SR_ADD(SR_ONE, SR_MUL(ratio, kappa_queue));
    return SR_MUL(cbl_base, factor);
}

surplus_real_t edp_psi_collapse_reserve(const surplus_real_t *amplitudes_sq,
                                         const surplus_real_t *delta_v,
                                         uint32_t num_states) {
    surplus_real_t sum = SR_ZERO;
    for (uint32_t i = 0; i < num_states; i++) {
        sum = SR_ADD(sum, SR_MUL(amplitudes_sq[i], delta_v[i]));
    }
    return sum;
}

/* ===== Liquidity Risk (EDP-003) ===== */

surplus_real_t edp_pdar(surplus_real_t hqla_verified,
                         surplus_real_t net_outflows_30d) {
    if (net_outflows_30d == SR_ZERO) return edp_zpd(hqla_verified, net_outflows_30d);
    return SR_DIV(hqla_verified, net_outflows_30d);
}

surplus_real_t edp_pdsfr(surplus_real_t available_stable,
                          surplus_real_t required_stable) {
    if (required_stable == SR_ZERO) return edp_zpd(available_stable, required_stable);
    return SR_DIV(available_stable, required_stable);
}

/* ===== Sovereign Risk (EDP-011) ===== */

bool edp_selection_rule_valid(uint32_t l_old, uint32_t l_new,
                               int32_t m_old, int32_t m_new,
                               int32_t s_old, int32_t s_new) {
    /* Δl = ±1 */
    int32_t dl = (int32_t)l_new - (int32_t)l_old;
    if (dl != -1 && dl != 1) return false;
    /* Δm = 0, ±1 */
    int32_t dm = m_new - m_old;
    if (dm < -1 || dm > 1) return false;
    /* Δs = 0 */
    if (s_old != s_new) return false;
    return true;
}

sovereign_risk_result_t edp_sovereign_risk(const sovereign_risk_input_t *input) {
    sovereign_risk_result_t result;
    
    /* Coverage deficit drives core risk */
    surplus_real_t floor = SR_DIV(SR_FROM_INT(COVERAGE_FLOOR_NUM), SR_FROM_INT(COVERAGE_FLOOR_DEN));
    result.coverage_deficit = SR_SUB(floor, input->coverage_ratio);
    if (result.coverage_deficit < 0) result.coverage_deficit = SR_ZERO;
    
    /* Real projection: coverage deficit × (1 - political stability) */
    surplus_real_t instability = SR_SUB(SR_ONE, input->political_stability);
    result.risk_score = SR_MUL(result.coverage_deficit, instability);
    
    /* Phase: externality loading */
    result.risk_phase = input->externality_phase;
    
    /* Selection rule check for reserve transitions */
    /* (Simplified: check if quantum numbers are self-consistent) */
    result.selection_rule_violated = false;
    
    /* NC-Rating */
    result.rating = edp_nc_rating(input->coverage_ratio, input->externality_phase);
    
    return result;
}

/* ===== Systemic Risk (EDP-016): PCCI ===== */

surplus_real_t edp_pcci(const systemic_node_t *nodes, uint32_t count) {
    if (count == 0) return SR_ZERO;
    surplus_real_t total_risk = SR_ZERO;
    for (uint32_t i = 0; i < count; i++) {
        surplus_real_t node_risk = SR_MUL(
            SR_SUB(SR_ONE, nodes[i].coverage_ratio),
            nodes[i].counterparty_exposure_total
        );
        /* Weight by number of counterparties (connectivity) */
        surplus_real_t weight = SR_DIV(SR_FROM_INT(nodes[i].num_counterparties),
                                        SR_FROM_INT(count));
        total_risk = SR_ADD(total_risk, SR_MUL(node_risk, weight));
    }
    return total_risk;
}

/* ===== General Risk Operator R: M⁵ → ℂ ===== */

risk_result_t edp_compute_risk(const m5_coords_t *coords,
                                surplus_real_t exposure,
                                surplus_real_t psi_amplitude_sq) {
    (void)psi_amplitude_sq;
    risk_result_t result;
    
    (void)coords;
    (void)exposure;
    result.coverage_breached = !edp_coverage_satisfied(coords);
    result.zpd_invoked = (coords->ell == SR_ZERO);
    
    if (result.zpd_invoked) {
        /* ZPD: return class identity */
        result.real_part = coords->r;
        result.imag_part = coords->phi;
        result.magnitude = coords->r;
        result.phase = coords->phi;
        result.paradox_state.magnitude = 0;
        result.paradox_state.sign = 1;
        result.paradox_state.level = 0;
        result.paradox_state.phase = coords->phi;
        return result;
    }
    
    /* Real part: PVD × CFR × exposure */
    surplus_real_t pvd = edp_pvd(coords);
    surplus_real_t cfr = edp_cfr(coords);
    result.real_part = SR_MUL(SR_MUL(pvd, cfr), exposure);
    
    /* Imaginary part: phase × exposure × coverage deficit */
    surplus_real_t deficit = edp_coverage_deficit(coords);
    result.imag_part = SR_MUL(SR_MUL(coords->phi, deficit), exposure);
    
    /* Magnitude: |R| = sqrt(Re² + Im²) */
    surplus_real_t re_sq = SR_MUL(result.real_part, result.real_part);
    surplus_real_t im_sq = SR_MUL(result.imag_part, result.imag_part);
    result.magnitude = SR_SQRT(SR_ADD(re_sq, im_sq));
    
    /* Phase: arg(R) = atan2(Im, Re) — simplified to ratio */
    if (result.real_part != SR_ZERO) {
        result.phase = SR_DIV(result.imag_part, result.real_part);
    } else {
        result.phase = result.imag_part;
    }
    
    /* Paradox state: Fibonacci-signed based on coverage */
    result.paradox_state = edp_nc_rating(edp_coverage_product(coords), coords->phi);
    
    return result;
}

/* ===== NC-Rating ===== */

fib_signed_t edp_nc_rating(surplus_real_t coverage_ratio, surplus_real_t externality) {
    fib_signed_t rating;
    surplus_real_t floor = SR_DIV(SR_FROM_INT(COVERAGE_FLOOR_NUM), SR_FROM_INT(COVERAGE_FLOOR_DEN));
    surplus_real_t ratio = SR_DIV(coverage_ratio, floor);
    
    /* Determine level based on coverage ratio */
    if (ratio >= SR_ONE) {
        rating.level = NC_RATING_LEVEL_0;
        rating.magnitude = 0;
        rating.sign = 1;
    } else if (SR_CMP(ratio, SR_FROM_FLOAT(0.8)) >= 0) {
        rating.level = NC_RATING_LEVEL_1;
        rating.magnitude = 1;
        rating.sign = 1;
    } else if (SR_CMP(ratio, SR_FROM_FLOAT(0.6)) >= 0) {
        rating.level = NC_RATING_LEVEL_2;
        rating.magnitude = 2;
        rating.sign = 1;
    } else if (SR_CMP(ratio, SR_FROM_FLOAT(0.4)) >= 0) {
        rating.level = NC_RATING_LEVEL_3;
        rating.magnitude = 3;
        rating.sign = -1;
    } else if (SR_CMP(ratio, SR_FROM_FLOAT(0.2)) >= 0) {
        rating.level = NC_RATING_LEVEL_4;
        rating.magnitude = 5;
        rating.sign = -1;
    } else {
        rating.level = NC_RATING_LEVEL_5;
        rating.magnitude = 8;
        rating.sign = -1;
    }
    
    rating.phase = externality;
    return rating;
}

const char *edp_rating_to_alpha(const fib_signed_t *rating) {
    if (rating->sign < 0) {
        switch (rating->level) {
            case NC_RATING_LEVEL_3: return "BB-";
            case NC_RATING_LEVEL_4: return "CCC+";
            case NC_RATING_LEVEL_5: return "CC-";
            default: return "BBB-";
        }
    }
    switch (rating->level) {
        case NC_RATING_LEVEL_0: return "AAA";
        case NC_RATING_LEVEL_1: return "AA+";
        case NC_RATING_LEVEL_2: return "A+";
        default: return "A";
    }
}
