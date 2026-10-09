/* situation_model.h — Tactical & Strategic Situation Modeler
 *
 * All-domain risk assessment engine covering:
 *   - Military / conflict
 *   - Economic / financial
 *   - Logistics / supply chain
 *   - Geopolitical / diplomatic
 *   - Climate / environmental
 *   - Pandemic / biological
 *   - Cyber / information warfare
 *   - Energy / resource
 *   - Space / orbital
 *   - Social / civil unrest
 *
 * Each domain uses the M⁵ Curzi Manifold coordinates and produces
 * a deterministic risk assessment with surplus-driven predictions.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef SITUATION_MODEL_H
#define SITUATION_MODEL_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"
#include "edp_risk.h"
#include "predictive_model.h"

/* ===== Situation Domains ===== */

typedef enum {
    SIT_DOMAIN_MILITARY      = 0,
    SIT_DOMAIN_ECONOMIC      = 1,
    SIT_DOMAIN_LOGISTICS     = 2,
    SIT_DOMAIN_GEOPOLITICAL  = 3,
    SIT_DOMAIN_CLIMATE       = 4,
    SIT_DOMAIN_PANDEMIC      = 5,
    SIT_DOMAIN_CYBER         = 6,
    SIT_DOMAIN_ENERGY        = 7,
    SIT_DOMAIN_SPACE         = 8,
    SIT_DOMAIN_SOCIAL        = 9,
    SIT_DOMAIN_AGRICULTURAL  = 10,
    SIT_DOMAIN_MARITIME      = 11,
    SIT_DOMAIN_NUCLEAR       = 12,
    SIT_DOMAIN_INFORMATION   = 13,
    SIT_DOMAIN_HUMANITARIAN  = 14,
    SIT_DOMAIN_MAX           = 15
} situation_domain_t;

/* ===== Threat Levels ===== */

typedef enum {
    THREAT_NONE     = 0,
    THREAT_LOW      = 1,
    THREAT_MODERATE = 2,
    THREAT_HIGH     = 3,
    THREAT_CRITICAL = 4,
    THREAT_EXISTENTIAL = 5
} threat_level_t;

/* ===== Situation Input ===== */

typedef struct {
    situation_domain_t domain;
    m5_coords_t coords;           /* M⁵ coordinates for this situation */
    surplus_real_t exposure;      /* Magnitude of exposure/stakes */
    surplus_real_t interaction_u; /* Interaction parameter with adversary/environment */
    surplus_real_t psi_amplitude_sq; /* Probability weight for choice-collapse */
    
    /* Domain-specific factors */
    surplus_real_t force_ratio;       /* Military: friendly vs adversary strength */
    surplus_real_t supply_coverage;   /* Logistics: supply vs demand ratio */
    surplus_real_t diplomatic_phase;  /* Geopolitical: relationship phase loading */
    surplus_real_t environmental_stress; /* Climate: stress level [0,1] */
    surplus_real_t contagion_rate;    /* Pandemic: R0 or equivalent */
    surplus_real_t cyber_exposure;    /* Cyber: attack surface [0,1] */
    surplus_real_t resource_depletion; /* Energy: depletion rate */
    surplus_real_t orbital_congestion; /* Space: debris/satellite density */
    surplus_real_t social_cohesion;   /* Social: cohesion index [0,1] */
    
    /* Temporal */
    uint32_t time_horizon_steps;  /* How far ahead to predict */
    surplus_real_t cost_per_step; /* Operational cost per time step */
} situation_input_t;

/* ===== Situation Assessment Result ===== */

typedef struct {
    situation_domain_t domain;
    threat_level_t threat_level;
    
    /* Risk assessment */
    risk_result_t risk;               /* EDP risk operator result */
    surplus_real_t surplus_value;     /* ISF interaction surplus */
    surplus_real_t composite_risk;    /* Nonlinear combined risk */
    
    /* Prediction */
    prediction_t prediction;          /* Predictive count for next step */
    multi_prediction_t forecast;      /* Multi-step forecast */
    
    /* Domain-specific outputs */
    surplus_real_t operational_readiness; /* [0,1] — ability to respond */
    surplus_real_t vulnerability_index;   /* [0,1] — weakness assessment */
    surplus_real_t resilience_score;      /* [0,1] — recovery capacity */
    surplus_real_t escalation_probability; /* [0,1] — conflict escalation likelihood */
    
    /* Recommendations */
    fib_signed_t action_rating;        /* Fibonacci-signed action priority */
    bool immediate_action_required;
    bool coverage_breached;
    
    /* Narrative */
    char assessment_summary[256];
} situation_result_t;

/* ===== Multi-Domain Assessment ===== */

typedef struct {
    situation_result_t domains[SIT_DOMAIN_MAX];
    surplus_real_t aggregate_risk;       /* Total risk across all domains */
    surplus_real_t aggregate_readiness;  /* Overall operational readiness */
    threat_level_t max_threat;           /* Highest threat across domains */
    uint32_t critical_domains;           /* Count of domains at HIGH+ */
    surplus_real_t systemic_contagion;   /* Cross-domain contagion index */
    
    /* Surplus-driven system health */
    surplus_real_t system_surplus;       /* Total interaction surplus */
    surplus_real_t system_sustainability; /* Long-term sustainability index */
} comprehensive_assessment_t;

/* ===== API ===== */

/* Assess a single domain situation */
situation_result_t situation_assess(const situation_input_t *input,
                                     const predictive_config_t *cfg);

/* Comprehensive multi-domain assessment */
void situation_assess_all(comprehensive_assessment_t *out,
                           const situation_input_t *inputs,
                           uint32_t num_inputs,
                           const predictive_config_t *cfg);

/* Domain-specific assessment functions */
situation_result_t situation_military(const situation_input_t *input,
                                       const predictive_config_t *cfg);
situation_result_t situation_economic(const situation_input_t *input,
                                       const predictive_config_t *cfg);
situation_result_t situation_logistics(const situation_input_t *input,
                                        const predictive_config_t *cfg);
situation_result_t situation_geopolitical(const situation_input_t *input,
                                           const predictive_config_t *cfg);
situation_result_t situation_climate(const situation_input_t *input,
                                      const predictive_config_t *cfg);
situation_result_t situation_pandemic(const situation_input_t *input,
                                       const predictive_config_t *cfg);
situation_result_t situation_cyber(const situation_input_t *input,
                                    const predictive_config_t *cfg);
situation_result_t situation_energy(const situation_input_t *input,
                                     const predictive_config_t *cfg);
situation_result_t situation_space(const situation_input_t *input,
                                    const predictive_config_t *cfg);
situation_result_t situation_social(const situation_input_t *input,
                                     const predictive_config_t *cfg);

/* Cross-domain contagion: risk propagation between domains */
surplus_real_t situation_cross_domain_contagion(const situation_result_t *domains,
                                                 uint32_t count);

/* Threat level from risk magnitude */
threat_level_t situation_threat_level(surplus_real_t risk_magnitude);

/* Domain name strings */
const char *situation_domain_name(situation_domain_t d);
const char *situation_threat_name(threat_level_t t);

#endif /* SITUATION_MODEL_H */
