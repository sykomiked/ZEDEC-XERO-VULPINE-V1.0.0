/* predictive_model.h — Predictive Count Model
 *
 * Nonlinear predictive engine combining:
 *   - EDP M⁵ risk calculus (Curzi Manifold operators)
 *   - Interaction Surplus Framework (f(u) = ln(1+(N-1)u))
 *   - Fibonacci-signed paradox resolution
 *   - Wave-function collapse probability estimation
 *
 * Produces deterministic predictions given verified inputs,
 * with probabilistic envelope over ontological (Choice-axis) uncertainty.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef PREDICTIVE_MODEL_H
#define PREDICTIVE_MODEL_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"
#include "edp_risk.h"

/* Prediction horizon */
#define MAX_HORIZON 64

/* Prediction result — complex-valued like all M⁵ operators */
typedef struct {
    surplus_real_t predicted_count;      /* Central prediction (deterministic) */
    surplus_real_t lower_bound;           /* Coverage-floor lower bound */
    surplus_real_t upper_bound;           /* Ψ-collapse upper bound */
    surplus_real_t confidence;            /* [0,1] — ℓ-derived confidence */
    surplus_real_t surplus_contribution;  /* ISF f(u) contribution */
    surplus_real_t risk_adjustment;       /* EDP risk operator adjustment */
    fib_signed_t paradox_state;           /* Fibonacci-signed state */
    bool coverage_breached;
    bool zpd_invoked;
} prediction_t;

/* Model configuration */
typedef struct {
    uint32_t N;                  /* Block count for surplus computation */
    surplus_real_t delta;        /* Dissipation rate for dynamics */
    surplus_real_t eta;          /* Conversion efficiency */
    surplus_real_t kappa;        /* Heisenberg constant */
    surplus_real_t psi_amplitude_sq; /* Wave-function probability weight */
} predictive_config_t;

/* Initialize default configuration */
void predictive_config_init(predictive_config_t *cfg);

/* Core prediction: given M⁵ coordinates and historical interaction,
 * compute predicted count for next timestep */
prediction_t predictive_count(const m5_coords_t *coords,
                               surplus_real_t historical_u,
                               surplus_real_t exposure,
                               const predictive_config_t *cfg);

/* Multi-step prediction: forecast over horizon */
typedef struct {
    prediction_t steps[MAX_HORIZON];
    uint32_t num_steps;
    surplus_real_t cumulative_risk;
    surplus_real_t cumulative_surplus;
    surplus_real_t sustainability_index;  /* Q/Q_max ratio */
} multi_prediction_t;

void predictive_multi(multi_prediction_t *out,
                       const m5_coords_t *coords,
                       surplus_real_t initial_u,
                       surplus_real_t exposure,
                       const surplus_real_t *u_trajectory,
                       const surplus_real_t *cost_trajectory,
                       uint32_t horizon,
                       const predictive_config_t *cfg);

/* Risk calculus map: nonlinear risk surface over a 2D grid
 * (coverage_ratio × externality_phase) */
typedef struct {
    surplus_real_t risk_values[16][16];  /* 16×16 grid */
    surplus_real_t min_risk;
    surplus_real_t max_risk;
    uint32_t min_i, min_j;
    uint32_t max_i, max_j;
} risk_map_t;

void predictive_risk_map(risk_map_t *map,
                          surplus_real_t exposure,
                          const predictive_config_t *cfg);

/* Nonlinear risk calculus: combine EDP + ISF for composite risk */
surplus_real_t predictive_nonlinear_risk(const m5_coords_t *coords,
                                          surplus_real_t interaction_u,
                                          surplus_real_t exposure,
                                          const predictive_config_t *cfg);

/* Wave-function collapse probability for choice events */
surplus_real_t predictive_collapse_probability(surplus_real_t psi_amplitude_sq,
                                                surplus_real_t coverage_ratio,
                                                const predictive_config_t *cfg);

/* Sustainability forecast: will the system maintain Q > Q_min? */
typedef struct {
    bool sustainable;
    surplus_real_t time_to_breach;     /* Steps until Q < Q_min */
    surplus_real_t steady_state_Q;     /* Q_∞ if constant forcing */
    surplus_real_t max_increment;      /* Maximum achievable growth */
} sustainability_t;

sustainability_t predictive_sustainability(const surplus_dynamics_t *dyn,
                                            surplus_real_t u_star,
                                            surplus_real_t c_star,
                                            surplus_real_t Q_min);

#endif /* PREDICTIVE_MODEL_H */
