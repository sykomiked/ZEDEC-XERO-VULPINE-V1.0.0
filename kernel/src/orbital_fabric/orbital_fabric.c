/* orbital_fabric.c — ZXV Orbital Fabric Compound Module Implementation
 *
 * Unifies the orbital stack: Elevator, Compat, Constellation, Yantra,
 * Smart Adapter, Event Space, and Mesh Net into a single coherent fabric.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "orbital_fabric.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"
#include "../sdk/selfaudit.h"

/* ===== Helper Functions ===== */

static void of_mem_set(void *dst, int val, uint32_t len) {
    uint8_t *d = dst;
    for (uint32_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

static void of_mem_copy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = dst; const uint8_t *s = src;
    for (uint32_t i = 0; i < len; i++) d[i] = s[i];
}

static int of_str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static uint32_t of_str_len(const char *s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

static void of_str_copy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ===== Paraconsistent State Helpers ===== */

const char *of_node_state_name(of_node_state_t state) {
    static const char *names[] = {"NEITHER", "TRUE", "FALSE", "BOTH", "QUARANTINE"};
    if (state <= OF_NODE_QUARANTINE) return names[state];
    return "INVALID";
}

const char *of_lpres_state_name(lpres_state_t state) {
    return lpres_state_name(state);
}

const char *of_oe_result_name(oe_translate_result_t result) {
    static const char *names[] = {"OK", "NO_PATH", "FAILED", "TOO_MANY_HOPS", "SCHEMA_NOT_FOUND", "VERSION_NOT_FOUND"};
    if (result <= OE_TRANSLATE_VERSION_NOT_FOUND) return names[result];
    return "UNKNOWN";
}

const char *of_oc_status_name(oc_status_t status) {
    static const char *names[] = {"OK", "NO_LANG", "ARG", "CONV", "CAP"};
    if (status >= OC_ERR_CAP && status <= OC_OK) return names[-status];
    return "UNKNOWN";
}

/* ===== Coverage Computation ===== */

static surplus_real_t of_compute_coverage(const m5_coords_t *m5) {
    if (!m5) return SR_ZERO;
    surplus_real_t omega = SR_FROM_INT(m5->omega);
    surplus_real_t r = m5->r;
    surplus_real_t ell = m5->ell;
    surplus_real_t phi = m5->phi;
    surplus_real_t chi = SR_FROM_INT(m5->chi);
    surplus_real_t numerator = SR_MUL(SR_MUL(omega, r), ell);
    surplus_real_t denominator = SR_MUL(phi, chi);
    if (SR_CMP(denominator, SR_ZERO) == 0) return SR_FROM_FLOAT(100.0);
    return SR_DIV(numerator, denominator);
}

/* ===== LPRES Attestation ===== */

lpres_state_t of_attest(orbital_fabric_t *fabric, uint32_t domain_id,
                        uint32_t op_id, void *args, int32_t result) {
    if (!fabric || domain_id >= fabric->num_domains) return LPRES_STATE_NEITHER;
    
    of_domain_t *dom = &fabric->domains[domain_id];
    lpres_state_t result_att = (result >= 0) ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t domain_att = (lpres_state_t)dom->lpres_state;
    lpres_state_t coverage_att = (SR_CMP(dom->coverage_ratio, dom->min_coverage_ratio) >= 0) 
                                  ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t fabric_att = (lpres_state_t)fabric->global_lpres_state;
    
    lpres_state_t combined = lpres_conjoin(result_att, domain_att);
    combined = lpres_conjoin(combined, coverage_att);
    combined = lpres_conjoin(combined, fabric_att);
    
    dom->global_attestation = combined;
    fabric->global_attestation = lpres_conjoin(fabric->global_attestation, combined);
    
    return combined;
}

/* ===== Initialization ===== */

void of_init(orbital_fabric_t *fabric, const char *local_node_id, uint32_t device_id) {
    if (!fabric) return;
    
    of_mem_set(fabric, 0, sizeof(*fabric));
    
    /* Initialize sub-modules */
    oe_init(&fabric->elevator);
    oc_register_builtins();  /* Register COBOL, Fortran, C adapters */
    cc_coordinator_init(&fabric->constellation, local_node_id);
    yf_init(&fabric->yantra);
    smart_adapter_registry_init(&fabric->sa_registry);
    sai_init(&fabric->sai_fabric, &fabric->sa_registry, &fabric->elevator, &fabric->yantra);
    ev_seq_init(&fabric->sequencer, local_node_id);
    ev_audit_init(&fabric->audit);
    ev_healing_init(&fabric->healing);
    mn_init(&fabric->mesh, device_id, "orbital-mesh", NULL);  /* Porter House would be passed */
    
    /* Initialize JDR PirateNet */
    jdr_network_init(&fabric->jdr_network);
    jdr_adapter_registry_init(&fabric->jdr_registry);
    
    /* Register JDR firmware adapters */
    jdr_adapter_register(&fabric->jdr_registry, jdr_adapter_create_ad9361());
    jdr_adapter_register(&fabric->jdr_registry, jdr_adapter_create_hackrf_one());
    jdr_adapter_register(&fabric->jdr_registry, jdr_adapter_create_rtl_sdr());
    
    /* Create JDR smart adapter and register with SA registry */
    smart_adapter_t *jdr_smart = smart_adapter_create_jdr(&fabric->jdr_registry, &fabric->jdr_network);
    smart_adapter_register(&fabric->sa_registry, jdr_smart);
    
    /* Initialize fabric M5 coordinates */
    fabric->m5.omega = 1;
    fabric->m5.r = SR_FROM_FLOAT(2.4);
    fabric->m5.ell = SR_ONE;
    fabric->m5.phi = SR_ZERO;
    fabric->m5.chi = 0;
    fabric->coverage_ratio = of_compute_coverage(&fabric->m5);
    
    /* Default configuration */
    fabric->config.auto_register_builtins = true;
    fabric->config.auto_heal = true;
    fabric->config.audit_interval = 1000;
    fabric->config.health_check_interval = 5000;
    fabric->config.route_expiry_cycles = 10000;
    fabric->config.min_global_coverage = SR_FROM_FLOAT(1.8);
    
    /* Register default capability guards */
    sai_add_capability_guard(&fabric->sai_fabric, 0xFFFFFFFF, &SAI_GUARD_MODEL_TO_EMULATED);
    sai_add_capability_guard(&fabric->sai_fabric, 0xFFFFFFFF, &SAI_GUARD_EMULATED_TO_RTL);
    sai_add_capability_guard(&fabric->sai_fabric, 0xFFFFFFFF, &SAI_GUARD_RTL_TO_SYNTHESIZED);
    sai_add_capability_guard(&fabric->sai_fabric, 0xFFFFFFFF, &SAI_GUARD_SYNTHESIZED_TO_FPGA);
    sai_add_capability_guard(&fabric->sai_fabric, 0xFFFFFFFF, &SAI_GUARD_FPGA_TO_CONNECTED);
    sai_add_capability_guard(&fabric->sai_fabric, 0xFFFFFFFF, &SAI_GUARD_CONNECTED_TO_MEASURED);
    sai_add_capability_guard(&fabric->sai_fabric, 0xFFFFFFFF, &SAI_GUARD_MEASURED_TO_QUALIFIED);
    sai_add_capability_guard(&fabric->sai_fabric, 0xFFFFFFFF, &SAI_GUARD_QUALIFIED_TO_CERTIFIED);
    
    fabric->initialized = true;
    fabric->global_lpres_state = OF_NODE_NEITHER;
    fabric->global_attestation = LPRES_STATE_NEITHER;
    fabric->global_safety_gate = false;
}

void of_register_builtins(orbital_fabric_t *fabric) {
    if (!fabric) return;
    /* Built-ins already registered in of_init via oc_register_builtins() */
    /* This function exists for explicit control */
}

/* ===== Language Adapter Registration ===== */

int32_t of_register_lang(orbital_fabric_t *fabric, oc_lang_t lang, const oc_lang_ops_t *ops) {
    if (!fabric || !ops || lang >= OC_LANG_MAX) return -1;
    
    /* Register with Orbital Compat */
    int32_t result = oc_register_lang(lang, ops);
    if (result != OC_OK) return result;
    
    /* Register in fabric's language registry */
    if (fabric->num_lang_adapters >= OF_MAX_LANG_ADAPTERS) return -1;
    
    of_lang_registry_t *reg = &fabric->lang_registry[fabric->num_lang_adapters++];
    reg->lang = lang;
    reg->ops = *ops;
    reg->registered = true;
    reg->domain_count = 0;
    reg->adapter_attestation = LPRES_STATE_NEITHER;
    
    return 0;
}

/* ===== Domain Creation ===== */

int32_t of_create_domain(orbital_fabric_t *fabric, const char *name,
                         uint32_t event_budget, uint32_t max_cost,
                         ev_consistency_t consistency,
                         const char *yf_device_class,
                         yf_capability_state_t initial_capability,
                         const char *mesh_name, mn_net_access_t mesh_access) {
    if (!fabric || !name || fabric->num_domains >= OF_MAX_INTEGRATED_NODES) return -1;
    
    of_domain_t *dom = &fabric->domains[fabric->num_domains];
    of_mem_set(dom, 0, sizeof(*dom));
    dom->id = fabric->next_domain_id++;
    
    of_str_copy(dom->name, name, OF_MAX_NAME_LEN);
    dom->min_coverage_ratio = fabric->config.min_global_coverage;
    dom->lpres_state = OF_NODE_NEITHER;
    dom->global_attestation = LPRES_STATE_NEITHER;
    dom->registered = true;
    
    /* Create Event Space domain */
    int32_t ev_dom_id = ev_seq_create_domain(&fabric->sequencer, name, event_budget, max_cost, consistency);
    if (ev_dom_id < 0) return -1;
    dom->ev_domain = ev_seq_get_domain(&fabric->sequencer, (uint32_t)ev_dom_id);
    
    /* Register Constellation node */
    int32_t cc_result = cc_register_node(&fabric->constellation, name, "orbital", 1);
    if (cc_result < 0) return -1;
    dom->cc_node = cc_get_node_by_idx(&fabric->constellation, (uint32_t)cc_result);
    
    /* Register Yantra device */
    int32_t yf_idx = yf_register_device(&fabric->yantra, name, yf_device_class, initial_capability);
    if (yf_idx < 0) return -1;
    dom->yf_device = yf_get_device(&fabric->yantra, (uint32_t)yf_idx);
    
    /* Register Smart Adapter device */
    /* This would probe for actual hardware; for now we create a virtual device */
    smart_device_t *sa_dev = NULL;
    /* In real implementation: smart_device_probe(&fabric->sa_registry, ..., &sa_dev); */
    dom->sa_device = sa_dev;
    
    /* Create Mesh Network */
    word168_t creator_id = {0};  /* Would be actual identity */
    int32_t mn_result = mn_create_network(&fabric->mesh, mesh_name, mesh_access, &creator_id, 100);
    if (mn_result < 0) return -1;
    dom->mn_network = mn_get_network(&fabric->mesh, (uint32_t)mn_result);
    
    /* Initialize M5 for domain */
    dom->m5.omega = fabric->num_domains + 1;
    dom->m5.r = SR_FROM_FLOAT(2.4 + fabric->num_domains * 0.1);
    dom->m5.ell = SR_ONE;
    dom->m5.phi = SR_ZERO;
    dom->m5.chi = 0;
    dom->coverage_ratio = of_compute_coverage(&dom->m5);
    
    /* Register domain with SAI integration fabric */
    if (dom->sa_device && dom->yf_device) {
        sai_register_device(&fabric->sai_fabric, dom->sa_device, yf_device_class, initial_capability);
    }
    
    fabric->num_domains++;
    fabric->stats.total_domains_created++;
    
    return (int32_t)(dom->id);
}

of_domain_t *of_get_domain(orbital_fabric_t *fabric, uint32_t domain_id) {
    if (!fabric) return NULL;
    for (uint32_t i = 0; i < fabric->num_domains; i++) {
        if (fabric->domains[i].id == domain_id && fabric->domains[i].registered) {
            return &fabric->domains[i];
        }
    }
    return NULL;
}

of_domain_t *of_get_domain_by_name(orbital_fabric_t *fabric, const char *name) {
    if (!fabric || !name) return NULL;
    for (uint32_t i = 0; i < fabric->num_domains; i++) {
        if (fabric->domains[i].registered && of_str_cmp(fabric->domains[i].name, name) == 0) {
            return &fabric->domains[i];
        }
    }
    return NULL;
}

/* ===== Schema Mappings ===== */

int32_t of_add_schema_mapping(orbital_fabric_t *fabric, uint32_t domain_id,
                              const char *sa_schema, uint16_t sa_version,
                              const char *yf_schema, uint16_t yf_version,
                              const char *oe_schema, uint16_t oe_version,
                              const char *oc_schema, uint16_t oc_version,
                              uint32_t oe_adapter_id, uint32_t oc_adapter_id) {
    if (!fabric) return -1;
    of_domain_t *dom = of_get_domain(fabric, domain_id);
    if (!dom || dom->num_schema_mappings >= OF_MAX_SCHEMA_MAPPINGS) return -1;
    
    uint32_t idx = dom->num_schema_mappings++;
    of_str_copy(dom->schema_mappings[idx].sa_schema, sa_schema, EV_SCHEMA_LEN);
    of_str_copy(dom->schema_mappings[idx].yf_schema, yf_schema, EV_SCHEMA_LEN);
    of_str_copy(dom->schema_mappings[idx].oe_schema, oe_schema, EV_SCHEMA_LEN);
    of_str_copy(dom->schema_mappings[idx].oc_schema, oc_schema, EV_SCHEMA_LEN);
    dom->schema_mappings[idx].sa_version = sa_version;
    dom->schema_mappings[idx].yf_version = yf_version;
    dom->schema_mappings[idx].oe_version = oe_version;
    dom->schema_mappings[idx].oc_version = oc_version;
    dom->schema_mappings[idx].oe_adapter_id = oe_adapter_id;
    dom->schema_mappings[idx].oc_adapter_id = oc_adapter_id;
    dom->schema_mappings[idx].active = true;
    
    return 0;
}

int32_t of_add_global_mapping(orbital_fabric_t *fabric,
                              const char *sa_schema, uint16_t sa_version,
                              const char *yf_schema, uint16_t yf_version,
                              const char *oe_schema, uint16_t oe_version,
                              const char *oc_schema, uint16_t oc_version,
                              uint32_t oe_adapter_id, uint32_t oc_adapter_id) {
    if (!fabric || fabric->num_global_mappings >= OF_MAX_SCHEMA_MAPPINGS) return -1;
    
    uint32_t idx = fabric->num_global_mappings++;
    of_str_copy(fabric->global_mappings[idx].sa_schema, sa_schema, EV_SCHEMA_LEN);
    of_str_copy(fabric->global_mappings[idx].yf_schema, yf_schema, EV_SCHEMA_LEN);
    of_str_copy(fabric->global_mappings[idx].oe_schema, oe_schema, EV_SCHEMA_LEN);
    of_str_copy(fabric->global_mappings[idx].oc_schema, oc_schema, EV_SCHEMA_LEN);
    fabric->global_mappings[idx].sa_version = sa_version;
    fabric->global_mappings[idx].yf_version = yf_version;
    fabric->global_mappings[idx].oe_version = oe_version;
    fabric->global_mappings[idx].oc_version = oc_version;
    fabric->global_mappings[idx].oe_adapter_id = oe_adapter_id;
    fabric->global_mappings[idx].oc_adapter_id = oc_adapter_id;
    fabric->global_mappings[idx].active = true;
    
    return 0;
}

/* ===== Event Translation ===== */

int32_t of_translate_event(orbital_fabric_t *fabric, uint32_t domain_id,
                           ev_envelope_t *env,
                           const char *target_sa_schema, uint16_t target_sa_version,
                           const char *target_yf_schema, uint16_t target_yf_version,
                           const char *target_oe_schema, uint16_t target_oe_version,
                           const char *target_oc_schema, uint16_t target_oc_version) {
    if (!fabric || !env) return -1;
    
    of_domain_t *dom = of_get_domain(fabric, domain_id);
    if (!dom) return -1;
    
    fabric->stats.total_translations++;
    
    /* 1. Orbital Elevator: SA/YF schema -> OE canonical schema */
    if (target_oe_schema && target_oe_version > 0) {
        oe_translate_result_t oe_result = oe_translate(&fabric->elevator, env, target_oe_schema, target_oe_version);
        if (oe_result != OE_TRANSLATE_OK) {
            fabric->stats.total_translation_failures++;
            of_attest(fabric, domain_id, 0x1000, (void*)target_oe_schema, -1);
            return -1;
        }
    }
    
    /* 2. Orbital Compat: Language translation if needed */
    if (target_oc_schema && target_oc_version > 0) {
        /* Find language adapter for this domain */
        for (uint32_t i = 0; i < dom->num_lang_adapters; i++) {
            of_lang_registry_t *lr = &fabric->lang_registry[i];
            if (lr->registered) {
                /* Would translate through OC IR */
                oc_ir_t ir;
                int32_t lower_result = lr->ops.lower(env->payload, env->payload_len, &ir);
                if (lower_result == OC_OK) {
                    int32_t lift_result = lr->ops.lift(&ir, env->payload, EV_PAYLOAD_MAX);
                    if (lift_result != OC_OK) {
                        fabric->stats.total_translation_failures++;
                        of_attest(fabric, domain_id, 0x2000, (void*)target_oc_schema, -1);
                        return -1;
                    }
                    env->payload_len = (uint16_t)lift_result;
                }
            }
        }
    }
    
    /* 3. SAI Integration: Translate to Yantra schema */
    if (target_yf_schema && target_yf_version > 0) {
        int32_t sai_result = sai_translate_sa_to_yf(&fabric->sai_fabric, 0, target_yf_schema, target_yf_version, env);
        if (sai_result < 0) {
            fabric->stats.total_translation_failures++;
            of_attest(fabric, domain_id, 0x3000, (void*)target_yf_schema, -1);
            return -1;
        }
    }
    
    of_attest(fabric, domain_id, 0x4000, env, 0);
    return 0;
}

oe_translate_result_t of_oe_translate(orbital_fabric_t *fabric,
                                       ev_envelope_t *env,
                                       const char *target_schema,
                                       uint16_t target_version) {
    if (!fabric || !env) return OE_TRANSLATE_FAILED;
    return oe_translate(&fabric->elevator, env, target_schema, target_version);
}

int32_t of_oc_translate(orbital_fabric_t *fabric,
                         oc_lang_t from_lang, const void *src, uint32_t len,
                         oc_lang_t to_lang, void *out, uint32_t cap) {
    if (!fabric) return -1;
    
    /* Lower from source language to IR */
    oc_ir_t ir;
    int32_t lower_result = oc_lower(from_lang, src, len, &ir);
    if (lower_result != OC_OK) return lower_result;
    
    /* Lift from IR to target language */
    return oc_lift(to_lang, &ir, out, cap);
}

/* ===== Constellation Routing ===== */

cc_node_t *of_route_event(orbital_fabric_t *fabric, const char *schema,
                          uint16_t schema_version) {
    if (!fabric || !schema) return NULL;
    return cc_route_event(&fabric->constellation, schema, schema_version);
}

/* ===== Trade Routes ===== */

int32_t of_create_trade_route(orbital_fabric_t *fabric,
                              uint32_t source_domain_id,
                              uint32_t dest_domain_id,
                              mn_route_type_t type,
                              uint64_t price_per_unit, uint64_t capacity,
                              uint64_t current_cycle,
                              const char *source_schema, uint16_t source_version,
                              const char *dest_schema, uint16_t dest_version) {
    if (!fabric) return -1;
    
    of_domain_t *src = of_get_domain(fabric, source_domain_id);
    of_domain_t *dst = of_get_domain(fabric, dest_domain_id);
    if (!src || !dst) return -1;
    
    /* Create base mesh route */
    word168_t source_id = {0}, dest_id = {0};  /* Would be actual identities */
    int32_t route_id = mn_create_route(&fabric->mesh, src->mn_network->id, type,
                                        &source_id, &dest_id, price_per_unit, capacity, current_cycle);
    if (route_id < 0) return -1;
    
    /* Extend with orbital metadata */
    if (fabric->num_trade_routes >= OF_MAX_TRADE_ROUTES) return -1;
    
    of_trade_route_t *of_route = &fabric->trade_routes[fabric->num_trade_routes++];
    of_route->base = *mn_get_route(&fabric->mesh, (uint32_t)route_id);
    of_str_copy(of_route->source_schema, source_schema, EV_SCHEMA_LEN);
    of_str_copy(of_route->dest_schema, dest_schema, EV_SCHEMA_LEN);
    of_route->source_version = source_version;
    of_route->dest_version = dest_version;
    of_route->settlement_price = price_per_unit;
    of_route->settlement_interval = 1000;  /* Default settlement interval */
    of_route->last_settlement_cycle = current_cycle;
    of_route->latency_slo = SR_FROM_FLOAT(100.0);  /* 100ms */
    of_route->availability_slo = SR_FROM_FLOAT(0.999);
    of_route->throughput_slo = SR_FROM_FLOAT(1000000.0);  /* 1 MB/s */
    of_route->lpres_state = OF_NODE_NEITHER;
    of_route->route_attestation = LPRES_STATE_NEITHER;
    of_route->active = true;
    
    /* Add to domain trade routes */
    if (src->num_trade_routes < OF_MAX_TRADE_ROUTES) {
        src->trade_route_ids[src->num_trade_routes++] = route_id;
    }
    if (dst->num_trade_routes < OF_MAX_TRADE_ROUTES) {
        dst->trade_route_ids[dst->num_trade_routes++] = route_id;
    }
    
    fabric->stats.total_trade_routes++;
    
    return route_id;
}

int32_t of_settle_route(orbital_fabric_t *fabric, uint32_t route_id, uint64_t current_cycle) {
    if (!fabric) return -1;
    
    mn_route_t *base_route = mn_get_route(&fabric->mesh, route_id);
    if (!base_route) return -1;
    
    /* Find orbital route */
    of_trade_route_t *of_route = NULL;
    for (uint32_t i = 0; i < fabric->num_trade_routes; i++) {
        if (fabric->trade_routes[i].base.id == route_id) {
            of_route = &fabric->trade_routes[i];
            break;
        }
    }
    if (!of_route) return -1;
    
    /* Check settlement interval */
    if (current_cycle - of_route->last_settlement_cycle < of_route->settlement_interval) {
        return 0;  /* Not yet time to settle */
    }
    
    /* Perform settlement via Mesh-Token (would integrate with mesh_token module) */
    /* For now, just update stats */
    of_route->last_settlement_cycle = current_cycle;
    fabric->stats.total_settlements++;
    
    /* Attest settlement */
    of_attest(fabric, 0xFFFFFFFF, 0x5000 | route_id, NULL, 0);
    
    return 0;
}

/* ===== Capability Upgrade ===== */

int32_t of_upgrade_capability(orbital_fabric_t *fabric, uint32_t domain_id,
                              yf_capability_state_t target_capability) {
    if (!fabric) return -1;
    of_domain_t *dom = of_get_domain(fabric, domain_id);
    if (!dom) return -1;
    
    /* Use SAI integration for capability upgrade with full guards */
    int32_t result = sai_upgrade_capability(&fabric->sai_fabric, 0, target_capability);
    
    if (result == 0) {
        dom->yf_device->capability = target_capability;
        dom->health.events_processed++;
    } else {
        dom->health.events_failed++;
        fabric->stats.total_domains_failed++;
    }
    
    return result;
}

/* ===== Self-Audit + Self-Healing ===== */

ev_heal_action_t of_self_audit_heal_domain(orbital_fabric_t *fabric, uint32_t domain_id) {
    if (!fabric) return EV_HEAL_TERMINATE;
    of_domain_t *dom = of_get_domain(fabric, domain_id);
    if (!dom || !dom->ev_domain) return EV_HEAL_TERMINATE;
    
    fabric->stats.total_self_audits++;
    
    /* Run Event Space audit + healing */
    ev_heal_action_t action = ev_self_audit_heal_domain(&fabric->sequencer, &fabric->audit, &fabric->healing, dom->ev_domain);
    
    /* Run SAI health check */
    sai_integrated_device_t health;
    if (sai_check_health(&fabric->sai_fabric, 0, &health) < 0) {
        action = (action > EV_HEAL_WARN) ? action : EV_HEAL_WARN;
    }
    
    /* Run Constellation health check */
    cc_check_health(&fabric->constellation, fabric->sequencer.next_sequence, 1000);
    
    /* Run Mesh health check */
    mn_check_expired(&fabric->mesh, fabric->sequencer.next_sequence);
    
    /* Update coverage */
    dom->coverage_ratio = of_compute_coverage(&dom->m5);
    if (SR_CMP(dom->coverage_ratio, dom->min_coverage_ratio) < 0) {
        action = (action > EV_HEAL_QUARANTINE) ? action : EV_HEAL_QUARANTINE;
        dom->lpres_state = OF_NODE_BOTH;
    }
    
    /* Update fabric global state */
    fabric->stats.total_healings++;
    
    if (action >= EV_HEAL_QUARANTINE) {
        dom->lpres_state = OF_NODE_QUARANTINE;
    } else if (action == EV_HEAL_NONE) {
        dom->lpres_state = OF_NODE_TRUE;
    }
    
    of_attest(fabric, domain_id, 0x6000, &action, (action == EV_HEAL_NONE) ? 0 : -1);
    
    return action;
}

ev_heal_action_t of_self_audit_heal_system(orbital_fabric_t *fabric) {
    if (!fabric) return EV_HEAL_TERMINATE;
    
    ev_heal_action_t worst_action = EV_HEAL_NONE;
    
    for (uint32_t i = 0; i < fabric->num_domains; i++) {
        if (fabric->domains[i].registered) {
            ev_heal_action_t action = of_self_audit_heal_domain(fabric, fabric->domains[i].id);
            if (action > worst_action) worst_action = action;
        }
    }
    
    /* Update global LPRES state */
    if (worst_action >= EV_HEAL_QUARANTINE) {
        fabric->global_lpres_state = OF_NODE_BOTH;
    } else if (worst_action == EV_HEAL_NONE) {
        fabric->global_lpres_state = OF_NODE_TRUE;
    } else {
        fabric->global_lpres_state = OF_NODE_BOTH;
    }
    
    fabric->global_attestation = (worst_action == EV_HEAL_NONE) ? LPRES_STATE_TRUE : LPRES_STATE_BOTH;
    fabric->global_safety_gate = (worst_action == EV_HEAL_NONE);
    
    return worst_action;
}

/* ===== Health & Safety ===== */

int32_t of_check_global_health(orbital_fabric_t *fabric) {
    if (!fabric) return -1;
    
    int32_t unhealthy = 0;
    for (uint32_t i = 0; i < fabric->num_domains; i++) {
        if (fabric->domains[i].registered) {
            sai_integrated_device_t health;
            if (sai_check_health(&fabric->sai_fabric, i, &health) < 0 || !health.safety_gate_open) {
                unhealthy++;
            }
        }
    }
    
    /* Check constellation */
    cc_check_health(&fabric->constellation, fabric->sequencer.next_sequence, 1000);
    
    /* Check mesh */
    mn_check_expired(&fabric->mesh, fabric->sequencer.next_sequence);
    
    return unhealthy == 0 ? 0 : -1;
}

bool of_global_safety_gate(orbital_fabric_t *fabric) {
    return fabric ? fabric->global_safety_gate : false;
}

/* ===== Coverage ===== */

void of_update_coverage(orbital_fabric_t *fabric) {
    if (!fabric) return;
    
    fabric->coverage_ratio = of_compute_coverage(&fabric->m5);
    
    for (uint32_t i = 0; i < fabric->num_domains; i++) {
        if (fabric->domains[i].registered) {
            fabric->domains[i].coverage_ratio = of_compute_coverage(&fabric->domains[i].m5);
        }
    }
}

bool of_enforce_coverage(orbital_fabric_t *fabric, surplus_real_t min_ratio) {
    if (!fabric) return false;
    
    if (SR_CMP(fabric->coverage_ratio, min_ratio) < 0) return false;
    
    for (uint32_t i = 0; i < fabric->num_domains; i++) {
        if (fabric->domains[i].registered) {
            if (SR_CMP(fabric->domains[i].coverage_ratio, min_ratio) < 0) return false;
        }
    }
    
    return true;
}

/* ===== Statistics ===== */

void of_get_stats(orbital_fabric_t *fabric, void *stats_out) {
    if (!fabric || !stats_out) return;
    of_mem_copy(stats_out, &fabric->stats, sizeof(fabric->stats));
}

/* ===== Paraconsistent State ===== */

of_node_state_t of_get_lpres_state(orbital_fabric_t *fabric, uint32_t domain_id) {
    if (!fabric) return OF_NODE_NEITHER;
    if (domain_id == 0xFFFFFFFF) return fabric->global_lpres_state;
    
    of_domain_t *dom = of_get_domain(fabric, domain_id);
    return dom ? dom->lpres_state : OF_NODE_NEITHER;
}

void of_set_lpres_state(orbital_fabric_t *fabric, uint32_t domain_id, of_node_state_t state) {
    if (!fabric) return;
    if (domain_id == 0xFFFFFFFF) {
        fabric->global_lpres_state = state;
        fabric->global_attestation = (lpres_state_t)state;
    } else {
        of_domain_t *dom = of_get_domain(fabric, domain_id);
        if (dom) {
            dom->lpres_state = state;
            dom->global_attestation = (lpres_state_t)state;
        }
    }
}
