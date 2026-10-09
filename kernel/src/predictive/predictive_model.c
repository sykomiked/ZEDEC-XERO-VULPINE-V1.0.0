/* predictive_model.c — Predictive Count Model implementation
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "predictive_model.h"
#include <stddef.h>

void predictive_config_init(predictive_config_t *cfg) {
    cfg->N = 12;  /* Vovina Ontological OmniTautology uses N=M=12 */
    cfg->delta = SR_FROM_FLOAT(0.05);  /* 5% dissipation per step */
    cfg->eta = SR_FROM_FLOAT(0.8);     /* 80% conversion efficiency */
    cfg->kappa = SR_FROM_FLOAT(0.01);  /* Heisenberg conjugate constant */
    cfg->psi_amplitude_sq = SR_FROM_FLOAT(0.5);
}

prediction_t predictive_count(const m5_coords_t *coords,
                               surplus_real_t historical_u,
                               surplus_real_t exposure,
                               const predictive_config_t *cfg) {
    prediction_t p;
    
    /* Coverage check */
    p.coverage_breached = !edp_coverage_satisfied(coords);
    p.zpd_invoked = (coords->ell == SR_ZERO);
    
    /* Surplus contribution: f(u) = ln(1 + (N-1)u) */
    p.surplus_contribution = surplus_f(historical_u, cfg->N);
    
    /* Risk adjustment: EDP risk operator magnitude */
    risk_result_t risk = edp_compute_risk(coords, exposure, cfg->psi_amplitude_sq);
    p.risk_adjustment = risk.magnitude;
    p.paradox_state = risk.paradox_state;
    
    /* Central prediction: surplus-driven count minus risk adjustment */
    surplus_real_t base_count = SR_MUL(p.surplus_contribution, exposure);
    p.predicted_count = SR_SUB(base_count, p.risk_adjustment);
    if (p.predicted_count < 0) p.predicted_count = SR_ZERO;
    
    /* Lower bound: coverage-floor constrained */
    surplus_real_t coverage_floor = SR_DIV(SR_FROM_INT(COVERAGE_FLOOR_NUM), SR_FROM_INT(COVERAGE_FLOOR_DEN));
    surplus_real_t coverage_ratio = SR_DIV(edp_coverage_product(coords), coverage_floor);
    if (coverage_ratio > SR_ONE) coverage_ratio = SR_ONE;
    p.lower_bound = SR_MUL(p.predicted_count, coverage_ratio);
    
    /* Upper bound: add Ψ-collapse expected loss */
    surplus_real_t psi_loss = SR_MUL(cfg->psi_amplitude_sq, exposure);
    p.upper_bound = SR_ADD(p.predicted_count, psi_loss);
    
    /* Confidence: ℓ-derived, scaled by coverage */
    p.confidence = SR_MUL(coords->ell, coverage_ratio);
    
    return p;
}

void predictive_multi(multi_prediction_t *out,
                       const m5_coords_t *coords,
                       surplus_real_t initial_u,
                       surplus_real_t exposure,
                       const surplus_real_t *u_trajectory,
                       const surplus_real_t *cost_trajectory,
                       uint32_t horizon,
                       const predictive_config_t *cfg) {
    (void)cost_trajectory;
    if (horizon > MAX_HORIZON) horizon = MAX_HORIZON;
    out->num_steps = horizon;
    
    surplus_dynamics_t dyn;
    surplus_dynamics_init(&dyn, SR_ZERO, cfg->delta, cfg->eta, cfg->N);
    
    out->cumulative_risk = SR_ZERO;
    out->cumulative_surplus = SR_ZERO;
    
    for (uint32_t t = 0; t < horizon; t++) {
        surplus_real_t u_t = (u_trajectory != NULL) ? u_trajectory[t] : initial_u;
        (void)u_t;
        m5_coords_t step_coords = *coords;
        step_coords.omega = coords->omega + t;
        out->steps[t] = predictive_count(&step_coords, u_t, exposure, cfg);
        
        out->cumulative_risk = SR_ADD(out->cumulative_risk, out->steps[t].risk_adjustment);
        out->cumulative_surplus = SR_ADD(out->cumulative_surplus, out->steps[t].surplus_contribution);
    }
    
    /* Sustainability index: Q / Q_max */
    surplus_real_t ceiling = surplus_sustainability_ceiling(&dyn);
    if (ceiling > SR_ZERO) {
        out->sustainability_index = SR_DIV(dyn.Q, ceiling);
    } else {
        out->sustainability_index = SR_ZERO;
    }
}

void predictive_risk_map(risk_map_t *map,
                          surplus_real_t exposure,
                          const predictive_config_t *cfg) {
    map->min_risk = surplus_real_t_max;
    map->max_risk = SR_ZERO;
    map->min_i = 0; map->min_j = 0;
    map->max_i = 0; map->max_j = 0;
    
    for (uint32_t i = 0; i < 16; i++) {
        for (uint32_t j = 0; j < 16; j++) {
            /* coverage_ratio ranges 0..1.5, externality ranges 0..π */
            surplus_real_t coverage = SR_DIV(SR_FROM_INT(i), SR_FROM_INT(10));
            surplus_real_t externality = SR_DIV(SR_MUL(SR_FROM_INT(j), SR_FROM_INT(314)), SR_FROM_INT(160));
            
            m5_coords_t coords;
            coords.omega = 0;
            coords.r = coverage;
            coords.ell = (coverage > SR_ZERO) ? SR_FROM_FLOAT(1.8 / 1.5) : SR_ZERO; /* Approximate */
            coords.phi = externality;
            coords.chi = 0;
            
            surplus_real_t risk = predictive_nonlinear_risk(&coords, SR_FROM_FLOAT(0.5), exposure, cfg);
            map->risk_values[i][j] = risk;
            
            if (SR_CMP(risk, map->min_risk) < 0) {
                map->min_risk = risk;
                map->min_i = i; map->min_j = j;
            }
            if (SR_CMP(risk, map->max_risk) > 0) {
                map->max_risk = risk;
                map->max_i = i; map->max_j = j;
            }
        }
    }
}

surplus_real_t predictive_nonlinear_risk(const m5_coords_t *coords,
                                          surplus_real_t interaction_u,
                                          surplus_real_t exposure,
                                          const predictive_config_t *cfg) {
    /* Combine EDP risk with ISF surplus for composite nonlinear risk */
    risk_result_t edp = edp_compute_risk(coords, exposure, cfg->psi_amplitude_sq);
    surplus_real_t surplus_val = surplus_f(interaction_u, cfg->N);
    
    /* Nonlinear combination: risk is amplified by surplus interaction
     * when coverage is breached, dampened when coverage holds */
    surplus_real_t risk_mag = edp.magnitude;
    if (edp.coverage_breached) {
        /* Amplify: risk × (1 + surplus) */
        risk_mag = SR_MUL(risk_mag, SR_ADD(SR_ONE, surplus_val));
    } else {
        /* Dampen: risk / (1 + surplus) */
        surplus_real_t dampener = SR_ADD(SR_ONE, surplus_val);
        if (dampener > SR_ZERO) {
            risk_mag = SR_DIV(risk_mag, dampener);
        }
    }
    
    return risk_mag;
}

surplus_real_t predictive_collapse_probability(surplus_real_t psi_amplitude_sq,
                                                surplus_real_t coverage_ratio,
                                                const predictive_config_t *cfg) {
    /* P(collapse) = |Ψ|² × (1 - coverage_ratio) × effective_count_factor */
    surplus_real_t coverage_deficit = SR_SUB(SR_ONE, coverage_ratio);
    if (coverage_deficit < 0) coverage_deficit = SR_ZERO;
    
    surplus_real_t g = surplus_effective_count(coverage_deficit, cfg->N);
    surplus_real_t g_max = SR_FROM_INT(cfg->N);
    surplus_real_t normalized_g = SR_DIV(g, g_max);
    
    return SR_MUL(SR_MUL(psi_amplitude_sq, coverage_deficit), normalized_g);
}

sustainability_t predictive_sustainability(const surplus_dynamics_t *dyn,
                                            surplus_real_t u_star,
                                            surplus_real_t c_star,
                                            surplus_real_t Q_min) {
    sustainability_t s;
    
    /* Steady state under constant forcing */
    s.steady_state_Q = surplus_steady_state(u_star, c_star, dyn->delta, dyn->eta, dyn->N);
    
    /* Check sustainability */
    s.sustainable = SR_CMP(s.steady_state_Q, Q_min) >= 0;
    
    /* Max increment */
    s.max_increment = surplus_max_increment((surplus_dynamics_t *)dyn, c_star);
    
    /* Time to breach: iterate until Q < Q_min */
    surplus_dynamics_t sim = *dyn;
    s.time_to_breach = SR_ZERO;
    if (s.sustainable) {
        s.time_to_breach = surplus_real_t_max; /* Never breaches under constant forcing */
    } else {
        for (uint32_t t = 0; t < 1000; t++) {
            surplus_dynamics_step(&sim, u_star, c_star);
            if (SR_CMP(sim.Q, Q_min) < 0) {
                s.time_to_breach = SR_FROM_INT(t);
                break;
            }
        }
    }
    
    return s;
}

/* surplus_real_t_max is now defined in surplus.h as a macro */

/* ---- DECLARATION -----------------------------------------------------------

 * situation_model.o names predictive_count and predictive_nonlinear_risk.
 * Both requirements below are measured: predictive_model.o's `nm -u` holds
 * three edp_* symbols and seven surplus_* symbols.
 */
#include "zxv_decl.h"
ZXV_DECLARE(predictive,
    ZXV_PROVIDES(predictive_ready),
    ZXV_REQUIRES(edp_risk_ready, surplus_ready),
    ZXV_NO_BRINGUP);
