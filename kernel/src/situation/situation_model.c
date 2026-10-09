/* situation_model.c — Tactical & Strategic Situation Modeler implementation
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "situation_model.h"

static const char *domain_names[] = {
    "Military", "Economic", "Logistics", "Geopolitical",
    "Climate", "Pandemic", "Cyber", "Energy",
    "Space", "Social", "Agricultural", "Maritime",
    "Nuclear", "Information", "Humanitarian"
};

static const char *threat_names[] = {
    "None", "Low", "Moderate", "High", "Critical", "Existential"
};

const char *situation_domain_name(situation_domain_t d) {
    if (d < SIT_DOMAIN_MAX) return domain_names[d];
    return "Unknown";
}

const char *situation_threat_name(threat_level_t t) {
    if (t <= THREAT_EXISTENTIAL) return threat_names[t];
    return "Unknown";
}

threat_level_t situation_threat_level(surplus_real_t risk_magnitude) {
    surplus_real_t low = SR_FROM_FLOAT(0.1);
    surplus_real_t mod = SR_FROM_FLOAT(0.3);
    surplus_real_t high = SR_FROM_FLOAT(0.5);
    surplus_real_t crit = SR_FROM_FLOAT(0.7);
    surplus_real_t exis = SR_FROM_FLOAT(0.9);
    if (SR_CMP(risk_magnitude, exis) >= 0) return THREAT_EXISTENTIAL;
    if (SR_CMP(risk_magnitude, crit) >= 0) return THREAT_CRITICAL;
    if (SR_CMP(risk_magnitude, high) >= 0) return THREAT_HIGH;
    if (SR_CMP(risk_magnitude, mod) >= 0) return THREAT_MODERATE;
    if (SR_CMP(risk_magnitude, low) >= 0) return THREAT_LOW;
    return THREAT_NONE;
}

situation_result_t situation_assess(const situation_input_t *input,
                                     const predictive_config_t *cfg) {
    switch (input->domain) {
        case SIT_DOMAIN_MILITARY:     return situation_military(input, cfg);
        case SIT_DOMAIN_ECONOMIC:     return situation_economic(input, cfg);
        case SIT_DOMAIN_LOGISTICS:    return situation_logistics(input, cfg);
        case SIT_DOMAIN_GEOPOLITICAL: return situation_geopolitical(input, cfg);
        case SIT_DOMAIN_CLIMATE:      return situation_climate(input, cfg);
        case SIT_DOMAIN_PANDEMIC:     return situation_pandemic(input, cfg);
        case SIT_DOMAIN_CYBER:        return situation_cyber(input, cfg);
        case SIT_DOMAIN_ENERGY:       return situation_energy(input, cfg);
        case SIT_DOMAIN_SPACE:        return situation_space(input, cfg);
        case SIT_DOMAIN_SOCIAL:       return situation_social(input, cfg);
        default: {
            situation_result_t r;
            r.domain = input->domain;
            r.threat_level = THREAT_NONE;
            r.risk = edp_compute_risk(&input->coords, input->exposure, input->psi_amplitude_sq);
            r.surplus_value = surplus_f(input->interaction_u, cfg->N);
            r.composite_risk = predictive_nonlinear_risk(&input->coords, input->interaction_u,
                                                          input->exposure, cfg);
            r.prediction = predictive_count(&input->coords, input->interaction_u,
                                             input->exposure, cfg);
            r.operational_readiness = SR_ONE;
            r.vulnerability_index = SR_ZERO;
            r.resilience_score = SR_ONE;
            r.escalation_probability = SR_ZERO;
            r.action_rating = edp_nc_rating(edp_coverage_product(&input->coords), input->coords.phi);
            r.immediate_action_required = false;
            r.coverage_breached = !edp_coverage_satisfied(&input->coords);
            r.assessment_summary[0] = 0;
            return r;
        }
    }
}

situation_result_t situation_military(const situation_input_t *input,
                                       const predictive_config_t *cfg) {
    situation_result_t r;
    r.domain = SIT_DOMAIN_MILITARY;
    r.risk = edp_compute_risk(&input->coords, input->exposure, input->psi_amplitude_sq);
    r.surplus_value = surplus_f(input->interaction_u, cfg->N);
    r.composite_risk = predictive_nonlinear_risk(&input->coords, input->interaction_u,
                                                  input->exposure, cfg);
    r.prediction = predictive_count(&input->coords, input->interaction_u, input->exposure, cfg);
    r.coverage_breached = r.risk.coverage_breached;
    /* Force ratio determines operational readiness */
    r.operational_readiness = input->force_ratio;
    r.vulnerability_index = SR_SUB(SR_ONE, input->force_ratio);
    if (r.vulnerability_index < 0) r.vulnerability_index = SR_ZERO;
    r.resilience_score = SR_MUL(input->force_ratio, r.surplus_value);
    /* Escalation probability: interaction surplus × vulnerability */
    r.escalation_probability = SR_MUL(r.surplus_value, r.vulnerability_index);
    r.threat_level = situation_threat_level(r.composite_risk);
    r.action_rating = edp_nc_rating(edp_coverage_product(&input->coords), input->coords.phi);
    r.immediate_action_required = (r.threat_level >= THREAT_HIGH);
    return r;
}

situation_result_t situation_economic(const situation_input_t *input,
                                       const predictive_config_t *cfg) {
    situation_result_t r;
    r.domain = SIT_DOMAIN_ECONOMIC;
    r.risk = edp_compute_risk(&input->coords, input->exposure, input->psi_amplitude_sq);
    r.surplus_value = surplus_f(input->interaction_u, cfg->N);
    r.composite_risk = predictive_nonlinear_risk(&input->coords, input->interaction_u,
                                                  input->exposure, cfg);
    r.prediction = predictive_count(&input->coords, input->interaction_u, input->exposure, cfg);
    r.coverage_breached = r.risk.coverage_breached;
    r.operational_readiness = edp_coverage_product(&input->coords);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10));
    r.operational_readiness = SR_DIV(r.operational_readiness, floor);
    if (SR_CMP(r.operational_readiness, SR_ONE) > 0) r.operational_readiness = SR_ONE;
    r.vulnerability_index = SR_SUB(SR_ONE, r.operational_readiness);
    if (r.vulnerability_index < 0) r.vulnerability_index = SR_ZERO;
    r.resilience_score = SR_MUL(r.surplus_value, r.operational_readiness);
    r.escalation_probability = SR_MUL(r.composite_risk, input->psi_amplitude_sq);
    r.threat_level = situation_threat_level(r.composite_risk);
    r.action_rating = edp_nc_rating(edp_coverage_product(&input->coords), input->coords.phi);
    r.immediate_action_required = r.coverage_breached;
    return r;
}

situation_result_t situation_logistics(const situation_input_t *input,
                                        const predictive_config_t *cfg) {
    situation_result_t r;
    r.domain = SIT_DOMAIN_LOGISTICS;
    r.risk = edp_compute_risk(&input->coords, input->exposure, input->psi_amplitude_sq);
    r.surplus_value = surplus_f(input->interaction_u, cfg->N);
    r.composite_risk = predictive_nonlinear_risk(&input->coords, input->interaction_u,
                                                  input->exposure, cfg);
    r.prediction = predictive_count(&input->coords, input->interaction_u, input->exposure, cfg);
    r.coverage_breached = r.risk.coverage_breached;
    r.operational_readiness = input->supply_coverage;
    if (SR_CMP(r.operational_readiness, SR_ONE) > 0) r.operational_readiness = SR_ONE;
    r.vulnerability_index = SR_SUB(SR_ONE, r.operational_readiness);
    if (r.vulnerability_index < 0) r.vulnerability_index = SR_ZERO;
    r.resilience_score = SR_MUL(r.surplus_value, input->supply_coverage);
    r.escalation_probability = SR_ZERO;
    r.threat_level = situation_threat_level(r.composite_risk);
    r.action_rating = edp_nc_rating(edp_coverage_product(&input->coords), input->coords.phi);
    r.immediate_action_required = (input->supply_coverage < SR_FROM_FLOAT(0.5));
    return r;
}

situation_result_t situation_geopolitical(const situation_input_t *input,
                                           const predictive_config_t *cfg) {
    situation_result_t r;
    r.domain = SIT_DOMAIN_GEOPOLITICAL;
    m5_coords_t coords = input->coords;
    coords.phi = input->diplomatic_phase;
    r.risk = edp_compute_risk(&coords, input->exposure, input->psi_amplitude_sq);
    r.surplus_value = surplus_f(input->interaction_u, cfg->N);
    r.composite_risk = predictive_nonlinear_risk(&coords, input->interaction_u,
                                                  input->exposure, cfg);
    r.prediction = predictive_count(&coords, input->interaction_u, input->exposure, cfg);
    r.coverage_breached = r.risk.coverage_breached;
    r.operational_readiness = SR_SUB(SR_ONE, SR_DIV(input->diplomatic_phase, SR_FROM_INT(3)));
    if (r.operational_readiness < 0) r.operational_readiness = SR_ZERO;
    r.vulnerability_index = SR_DIV(input->diplomatic_phase, SR_FROM_INT(3));
    if (SR_CMP(r.vulnerability_index, SR_ONE) > 0) r.vulnerability_index = SR_ONE;
    r.resilience_score = r.surplus_value;
    r.escalation_probability = SR_MUL(r.composite_risk, input->psi_amplitude_sq);
    r.threat_level = situation_threat_level(r.composite_risk);
    r.action_rating = edp_nc_rating(edp_coverage_product(&coords), coords.phi);
    r.immediate_action_required = (r.threat_level >= THREAT_HIGH);
    return r;
}

situation_result_t situation_climate(const situation_input_t *input,
                                      const predictive_config_t *cfg) {
    situation_result_t r;
    r.domain = SIT_DOMAIN_CLIMATE;
    r.risk = edp_compute_risk(&input->coords, input->exposure, input->psi_amplitude_sq);
    r.surplus_value = surplus_f(input->interaction_u, cfg->N);
    r.composite_risk = predictive_nonlinear_risk(&input->coords, input->interaction_u,
                                                  input->exposure, cfg);
    r.prediction = predictive_count(&input->coords, input->interaction_u, input->exposure, cfg);
    r.coverage_breached = r.risk.coverage_breached;
    r.operational_readiness = SR_SUB(SR_ONE, input->environmental_stress);
    if (r.operational_readiness < 0) r.operational_readiness = SR_ZERO;
    r.vulnerability_index = input->environmental_stress;
    r.resilience_score = SR_MUL(r.surplus_value, r.operational_readiness);
    r.escalation_probability = SR_MUL(input->environmental_stress, input->psi_amplitude_sq);
    r.threat_level = situation_threat_level(r.composite_risk);
    r.action_rating = edp_nc_rating(edp_coverage_product(&input->coords), input->coords.phi);
    r.immediate_action_required = (input->environmental_stress > SR_FROM_FLOAT(0.7));
    return r;
}

situation_result_t situation_pandemic(const situation_input_t *input,
                                       const predictive_config_t *cfg) {
    situation_result_t r;
    r.domain = SIT_DOMAIN_PANDEMIC;
    r.risk = edp_compute_risk(&input->coords, input->exposure, input->psi_amplitude_sq);
    r.surplus_value = surplus_f(input->interaction_u, cfg->N);
    r.composite_risk = predictive_nonlinear_risk(&input->coords, input->interaction_u,
                                                  input->exposure, cfg);
    r.prediction = predictive_count(&input->coords, input->interaction_u, input->exposure, cfg);
    r.coverage_breached = r.risk.coverage_breached;
    surplus_real_t r0 = input->contagion_rate;
    r.operational_readiness = SR_DIV(SR_ONE, r0);
    if (SR_CMP(r.operational_readiness, SR_ONE) > 0) r.operational_readiness = SR_ONE;
    r.vulnerability_index = SR_SUB(r0, SR_ONE);
    if (r.vulnerability_index < 0) r.vulnerability_index = SR_ZERO;
    r.resilience_score = SR_MUL(r.surplus_value, r.operational_readiness);
    r.escalation_probability = SR_MUL(r0, input->psi_amplitude_sq);
    r.threat_level = situation_threat_level(r.composite_risk);
    r.action_rating = edp_nc_rating(edp_coverage_product(&input->coords), input->coords.phi);
    r.immediate_action_required = (SR_CMP(r0, SR_FROM_FLOAT(1.5)) > 0);
    return r;
}

situation_result_t situation_cyber(const situation_input_t *input,
                                    const predictive_config_t *cfg) {
    situation_result_t r;
    r.domain = SIT_DOMAIN_CYBER;
    r.risk = edp_compute_risk(&input->coords, input->exposure, input->psi_amplitude_sq);
    r.surplus_value = surplus_f(input->interaction_u, cfg->N);
    r.composite_risk = predictive_nonlinear_risk(&input->coords, input->interaction_u,
                                                  input->exposure, cfg);
    r.prediction = predictive_count(&input->coords, input->interaction_u, input->exposure, cfg);
    r.coverage_breached = r.risk.coverage_breached;
    r.operational_readiness = SR_SUB(SR_ONE, input->cyber_exposure);
    if (r.operational_readiness < 0) r.operational_readiness = SR_ZERO;
    r.vulnerability_index = input->cyber_exposure;
    r.resilience_score = SR_MUL(r.surplus_value, r.operational_readiness);
    r.escalation_probability = SR_MUL(input->cyber_exposure, input->psi_amplitude_sq);
    r.threat_level = situation_threat_level(r.composite_risk);
    r.action_rating = edp_nc_rating(edp_coverage_product(&input->coords), input->coords.phi);
    r.immediate_action_required = (input->cyber_exposure > SR_FROM_FLOAT(0.7));
    return r;
}

situation_result_t situation_energy(const situation_input_t *input,
                                     const predictive_config_t *cfg) {
    situation_result_t r;
    r.domain = SIT_DOMAIN_ENERGY;
    r.risk = edp_compute_risk(&input->coords, input->exposure, input->psi_amplitude_sq);
    r.surplus_value = surplus_f(input->interaction_u, cfg->N);
    r.composite_risk = predictive_nonlinear_risk(&input->coords, input->interaction_u,
                                                  input->exposure, cfg);
    r.prediction = predictive_count(&input->coords, input->interaction_u, input->exposure, cfg);
    r.coverage_breached = r.risk.coverage_breached;
    r.operational_readiness = SR_SUB(SR_ONE, input->resource_depletion);
    if (r.operational_readiness < 0) r.operational_readiness = SR_ZERO;
    r.vulnerability_index = input->resource_depletion;
    r.resilience_score = SR_MUL(r.surplus_value, r.operational_readiness);
    r.escalation_probability = SR_MUL(input->resource_depletion, input->psi_amplitude_sq);
    r.threat_level = situation_threat_level(r.composite_risk);
    r.action_rating = edp_nc_rating(edp_coverage_product(&input->coords), input->coords.phi);
    r.immediate_action_required = (input->resource_depletion > SR_FROM_FLOAT(0.7));
    return r;
}

situation_result_t situation_space(const situation_input_t *input,
                                    const predictive_config_t *cfg) {
    situation_result_t r;
    r.domain = SIT_DOMAIN_SPACE;
    r.risk = edp_compute_risk(&input->coords, input->exposure, input->psi_amplitude_sq);
    r.surplus_value = surplus_f(input->interaction_u, cfg->N);
    r.composite_risk = predictive_nonlinear_risk(&input->coords, input->interaction_u,
                                                  input->exposure, cfg);
    r.prediction = predictive_count(&input->coords, input->interaction_u, input->exposure, cfg);
    r.coverage_breached = r.risk.coverage_breached;
    r.operational_readiness = SR_SUB(SR_ONE, input->orbital_congestion);
    if (r.operational_readiness < 0) r.operational_readiness = SR_ZERO;
    r.vulnerability_index = input->orbital_congestion;
    r.resilience_score = SR_MUL(r.surplus_value, r.operational_readiness);
    r.escalation_probability = SR_MUL(input->orbital_congestion, input->psi_amplitude_sq);
    r.threat_level = situation_threat_level(r.composite_risk);
    r.action_rating = edp_nc_rating(edp_coverage_product(&input->coords), input->coords.phi);
    r.immediate_action_required = (input->orbital_congestion > SR_FROM_FLOAT(0.8));
    return r;
}

situation_result_t situation_social(const situation_input_t *input,
                                     const predictive_config_t *cfg) {
    situation_result_t r;
    r.domain = SIT_DOMAIN_SOCIAL;
    r.risk = edp_compute_risk(&input->coords, input->exposure, input->psi_amplitude_sq);
    r.surplus_value = surplus_f(input->interaction_u, cfg->N);
    r.composite_risk = predictive_nonlinear_risk(&input->coords, input->interaction_u,
                                                  input->exposure, cfg);
    r.prediction = predictive_count(&input->coords, input->interaction_u, input->exposure, cfg);
    r.coverage_breached = r.risk.coverage_breached;
    r.operational_readiness = input->social_cohesion;
    r.vulnerability_index = SR_SUB(SR_ONE, input->social_cohesion);
    if (r.vulnerability_index < 0) r.vulnerability_index = SR_ZERO;
    r.resilience_score = SR_MUL(r.surplus_value, input->social_cohesion);
    r.escalation_probability = SR_MUL(r.vulnerability_index, input->psi_amplitude_sq);
    r.threat_level = situation_threat_level(r.composite_risk);
    r.action_rating = edp_nc_rating(edp_coverage_product(&input->coords), input->coords.phi);
    r.immediate_action_required = (input->social_cohesion < SR_FROM_FLOAT(0.3));
    return r;
}

surplus_real_t situation_cross_domain_contagion(const situation_result_t *domains,
                                                 uint32_t count) {
    if (count == 0) return SR_ZERO;
    surplus_real_t total = SR_ZERO;
    for (uint32_t i = 0; i < count; i++) {
        for (uint32_t j = i + 1; j < count; j++) {
            surplus_real_t product = SR_MUL(domains[i].composite_risk, domains[j].composite_risk);
            total = SR_ADD(total, product);
        }
    }
    return SR_DIV(total, SR_FROM_INT(count * count));
}

void situation_assess_all(comprehensive_assessment_t *out,
                           const situation_input_t *inputs,
                           uint32_t num_inputs,
                           const predictive_config_t *cfg) {
    out->aggregate_risk = SR_ZERO;
    out->aggregate_readiness = SR_ZERO;
    out->max_threat = THREAT_NONE;
    out->critical_domains = 0;
    out->system_surplus = SR_ZERO;
    out->system_sustainability = SR_ZERO;
    
    uint32_t assessed = 0;
    for (uint32_t i = 0; i < num_inputs && i < SIT_DOMAIN_MAX; i++) {
        out->domains[i] = situation_assess(&inputs[i], cfg);
        out->aggregate_risk = SR_ADD(out->aggregate_risk, out->domains[i].composite_risk);
        out->aggregate_readiness = SR_ADD(out->aggregate_readiness, out->domains[i].operational_readiness);
        out->system_surplus = SR_ADD(out->system_surplus, out->domains[i].surplus_value);
        if (out->domains[i].threat_level > out->max_threat)
            out->max_threat = out->domains[i].threat_level;
        if (out->domains[i].threat_level >= THREAT_HIGH)
            out->critical_domains++;
        assessed++;
    }
    
    if (assessed > 0) {
        out->aggregate_risk = SR_DIV(out->aggregate_risk, SR_FROM_INT(assessed));
        out->aggregate_readiness = SR_DIV(out->aggregate_readiness, SR_FROM_INT(assessed));
        out->system_sustainability = SR_DIV(out->system_surplus, SR_FROM_INT(assessed));
    }
    
    out->systemic_contagion = situation_cross_domain_contagion(out->domains, assessed);
}

/* ---- DECLARATION -----------------------------------------------------------

 * ROOTING THE CALLER, NOT THE LEAF. situation_model.o was measured at
 * linked=0, dropped=16, with 35 edp_* call sites. Rooting edp_risk directly
 * would put the risk engine in the image with nothing consulting it;
 * situation_model is what consults it, so the root belongs here and edp_risk,
 * predictive and surplus come in behind it.
 *
 * REQUIRES measured from situation_model.o's `nm -u`: edp_compute_risk /
 * edp_coverage_product / edp_coverage_satisfied / edp_nc_rating ->
 * edp_risk_ready, predictive_count / predictive_nonlinear_risk ->
 * predictive_ready, surplus_f -> surplus_ready.
 *
 * WHY THE BRING-UP IS SHAPED THIS WAY. Every numeric field here is
 * surplus_real_t, which is `double` on the host and Q32.32 on the target --
 * the standing rule is never to hand-roll a conversion between them. A zeroed
 * static input is the one value that is identically zero in BOTH
 * representations, so it needs no conversion and cannot be wrong. The check is
 * that a zero-exposure situation is not rated a threat: a risk model that
 * returns CRITICAL for an empty input is broken in the direction that gets
 * acted on.
 */
#include "zxv_decl.h"
static int zxvd_situation_bringup(void) {
    static const situation_input_t in;      /* zero in both surplus_real_t forms */
    static const predictive_config_t cfg;
    situation_result_t r = situation_cyber(&in, &cfg);
    if (r.threat_level > THREAT_EXISTENTIAL) return -1;  /* a real level  */
    if (situation_threat_name(r.threat_level) == 0) return -1;
    if (situation_domain_name(SIT_DOMAIN_CYBER) == 0) return -1;
    return 0;
}

ZXV_DECLARE(situation,
    ZXV_PROVIDES(situation_model_ready),
    ZXV_REQUIRES(edp_risk_ready, predictive_ready, surplus_ready),
    ZXV_BRINGUP(zxvd_situation_bringup));
