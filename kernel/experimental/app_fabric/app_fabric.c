/* app_fabric.c — ZXV Application Fabric Compound Module Implementation
 *
 * Unifies all user-space application modules: SDK, App Templates, Wallet,
 * Derivatives, Assurance, Treaty, App Launcher, Self-Audit, with all
 * kernel fabric integration.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "app_fabric.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"
#include "selfaudit.h"

/* ===== Helper Functions ===== */

static void af_mem_set(void *dst, int val, uint32_t len) {
    uint8_t *d = dst;
    for (uint32_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

static void af_mem_copy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = dst; const uint8_t *s = src;
    for (uint32_t i = 0; i < len; i++) d[i] = s[i];
}

static int af_str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static uint32_t af_str_len(const char *s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

static void af_str_copy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ===== Coverage Computation ===== */

static surplus_real_t af_compute_coverage(const m5_coords_t *m5) {
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

lpres_state_t af_attest(app_fabric_t *fabric, uint32_t instance_id,
                        uint32_t op_id, void *args, int32_t result) {
    if (!fabric || instance_id >= fabric->num_instances) return LPRES_STATE_NEITHER;
    
    af_app_instance_t *inst = &fabric->instances[instance_id];
    lpres_state_t result_att = (result >= 0) ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t inst_att = inst->attestation;
    lpres_state_t coverage_att = (SR_CMP(inst->coverage_ratio, fabric->min_coverage_ratio) >= 0) 
                                  ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t fabric_att = fabric->global_attestation;
    
    lpres_state_t combined = lpres_conjoin(result_att, inst_att);
    combined = lpres_conjoin(combined, coverage_att);
    combined = lpres_conjoin(combined, fabric_att);
    
    inst->attestation = combined;
    fabric->global_attestation = lpres_conjoin(fabric->global_attestation, combined);
    
    return combined;
}

/* ===== Initialization ===== */

void af_init(app_fabric_t *fabric,
             financial_fabric_t *financial,
             identity_fabric_t *identity,
             orbital_fabric_t *orbital,
             compute_fabric_t *compute,
             security_fabric_t *security,
             storage_fabric_t *storage,
             network_fabric_t *network,
             media_fabric_t *media,
             governance_fabric_t *governance) {
    if (!fabric) return;
    
    af_mem_set(fabric, 0, sizeof(*fabric));
    fabric->financial = financial;
    fabric->identity = identity;
    fabric->orbital = orbital;
    fabric->compute = compute;
    fabric->security = security;
    fabric->storage = storage;
    fabric->network = network;
    fabric->media = media;
    fabric->governance = governance;
    
    /* Initialize SDK */
    /* m5_sdk_init(&fabric->sdk); */
    
    /* Initialize App Launcher */
    /* app_launcher_init(&fabric->launcher); */
    
    /* Initialize built-in apps */
    /* wallet_app_init(&fabric->wallet_app); */
    /* derivatives_app_init(&fabric->derivatives_app); */
    /* assurance_app_init(&fabric->assurance_app); */
    /* treaty_app_init(&fabric->treaty_app); */
    
    /* Initialize M5 coordinates */
    fabric->m5.omega = 1;
    fabric->m5.r = SR_FROM_FLOAT(9.0);  /* Application rail */
    fabric->m5.ell = SR_ONE;
    fabric->m5.phi = SR_ZERO;
    fabric->m5.chi = 0;
    fabric->coverage_ratio = af_compute_coverage(&fabric->m5);
    fabric->min_coverage_ratio = SR_FROM_FLOAT(1.8);
    
    /* Default configuration */
    fabric->config.auto_grant_builtin_capabilities = true;
    fabric->config.require_self_audit = true;
    fabric->config.audit_interval = 1000;
    fabric->config.auto_heal = true;
    fabric->config.min_instance_coverage = SR_FROM_FLOAT(1.8);
    
    fabric->global_attestation = LPRES_STATE_NEITHER;
    fabric->global_safety_gate = false;
    fabric->initialized = true;
    
    /* Register built-in capabilities */
    af_register_capability(fabric, "capital.transfer", "Transfer capital between accounts",
                          SR_FROM_FLOAT(1.8), LPRES_STATE_TRUE, false, false,
                          5, 1000, 0, 0);  /* Form 5: Financial */
    af_register_capability(fabric, "derivatives.trade", "Trade derivatives contracts",
                          SR_FROM_FLOAT(2.0), LPRES_STATE_TRUE, true, true,
                          5, 10000, 0, 0);
    af_register_capability(fabric, "assurance.create", "Create assurance contracts",
                          SR_FROM_FLOAT(2.0), LPRES_STATE_TRUE, false, true,
                          5, 5000, 0, 0);
    af_register_capability(fabric, "treaty.tokenize", "Tokenize conservation easements",
                          SR_FROM_FLOAT(2.5), LPRES_STATE_TRUE, true, true,
                          3, 100000, 0, 0);  /* Form 3: Heritage */
    af_register_capability(fabric, "network.mesh", "Create/join mesh networks",
                          SR_FROM_FLOAT(1.8), LPRES_STATE_TRUE, false, false,
                          6, 1000, 0, 0);  /* Form 6: Material */
    af_register_capability(fabric, "storage.persist", "Persistent storage access",
                          SR_FROM_FLOAT(1.8), LPRES_STATE_TRUE, false, false,
                          6, 10000, 0, 0);
    af_register_capability(fabric, "media.stream", "Real-time media streaming",
                          SR_FROM_FLOAT(2.0), LPRES_STATE_TRUE, false, false,
                          8, 10000, 0, 0);  /* Form 8: Knowledge */
    af_register_capability(fabric, "governance.vote", "Participate in governance",
                          SR_FROM_FLOAT(2.0), LPRES_STATE_TRUE, false, true,
                          4, 10000, 0, 0);  /* Form 4: Governance */
}

void af_register_builtins(app_fabric_t *fabric) {
    if (!fabric) return;
    
    /* Register built-in app descriptors */
    af_register_app(fabric, "wallet", AF_APP_WALLET, (void*)wallet_app_main,
                    65536, 1048576,
                    (const char*[]){"capital.transfer", "storage.persist", "identity.credential"}, 3,
                    (const char*[]){"wallet.balance.query", "wallet.transfer.request"}, 2,
                    (const char*[]){"wallet.balance.updated", "wallet.transfer.completed"}, 2);
    
    af_register_app(fabric, "derivatives", AF_APP_DERIVATIVES, (void*)derivatives_app_main,
                    131072, 2097152,
                    (const char*[]){"derivatives.trade", "capital.transfer", "network.mesh"}, 3,
                    (const char*[]){"derivatives.order.place", "derivatives.arb.execute"}, 2,
                    (const char*[]){"derivatives.order.filled", "derivatives.arb.completed"}, 2);
    
    af_register_app(fabric, "assurance", AF_APP_ASSURANCE, (void*)assurance_app_main,
                    65536, 1048576,
                    (const char*[]){"assurance.create", "capital.transfer", "governance.vote"}, 3,
                    (const char*[]){"assurance.contract.create", "assurance.generation.verify"}, 2,
                    (const char*[]){"assurance.capital.generated", "assurance.capital.forwarded"}, 2);
    
    af_register_app(fabric, "treaty", AF_APP_TREATY, (void*)treaty_app_main,
                    131072, 2097152,
                    (const char*[]){"treaty.tokenize", "storage.persist", "identity.credential"}, 3,
                    (const char*[]){"treaty.asset.create", "treaty.asset.verify"}, 2,
                    (const char*[]){"treaty.asset.tokenized", "treaty.asset.verified"}, 2);
}

/* ===== App Descriptor Management ===== */

int32_t af_register_app(app_fabric_t *fabric,
                        const char *name, af_app_type_t type,
                        void *entry_point,
                        uint32_t stack_size, uint32_t heap_size,
                        const char **capabilities, uint32_t num_capabilities,
                        const char **accepted_schemas, uint32_t num_accepted,
                        const char **emitted_schemas, uint32_t num_emitted) {
    if (!fabric || !name || fabric->num_descriptors >= AF_MAX_APPS) return -1;
    
    af_app_descriptor_t *desc = &fabric->descriptors[fabric->num_descriptors];
    af_mem_set(desc, 0, sizeof(*desc));
    desc->id = fabric->next_descriptor_id++;
    
    af_str_copy(desc->name, name, AF_MAX_NAME_LEN);
    desc->type = type;
    desc->entry_point = entry_point;
    desc->stack_size = stack_size;
    desc->heap_size = heap_size;
    
    for (uint32_t i = 0; i < num_capabilities && i < AF_MAX_CAPABILITIES; i++) {
        af_str_copy(desc->capabilities[desc->num_capabilities++], capabilities[i], AF_MAX_NAME_LEN);
    }
    
    for (uint32_t i = 0; i < num_accepted && i < 8; i++) {
        af_str_copy(desc->accepted_schemas[desc->num_accepted_schemas++], accepted_schemas[i], AF_MAX_SCHEMA_LEN);
    }
    
    for (uint32_t i = 0; i < num_emitted && i < 8; i++) {
        af_str_copy(desc->emitted_schemas[desc->num_emitted_schemas++], emitted_schemas[i], AF_MAX_SCHEMA_LEN);
    }
    
    desc->max_cpu_percent = SR_FROM_FLOAT(10.0);
    desc->max_memory_mb = SR_FROM_FLOAT(100.0);
    desc->max_network_mbps = SR_FROM_FLOAT(10.0);
    desc->max_storage_gb = SR_FROM_FLOAT(1.0);
    
    /* Initialize M5 */
    desc->m5.omega = fabric->num_descriptors + 1;
    desc->m5.r = SR_FROM_FLOAT(9.0);
    desc->m5.ell = SR_ONE;
    desc->m5.phi = SR_ZERO;
    desc->m5.chi = 0;
    desc->coverage_ratio = af_compute_coverage(&desc->m5);
    desc->min_coverage_ratio = fabric->min_coverage_ratio;
    
    desc->attestation = LPRES_STATE_NEITHER;
    desc->active = true;
    desc->builtin = (type != AF_APP_CUSTOM);
    
    fabric->num_descriptors++;
    fabric->stats.total_apps_registered++;
    
    return af_attest(fabric, 0xFFFFFFFF, 0x1000 | desc->id, desc, 0);
}

af_app_descriptor_t *af_get_descriptor(app_fabric_t *fabric, uint32_t descriptor_id) {
    if (!fabric) return NULL;
    for (uint32_t i = 0; i < fabric->num_descriptors; i++) {
        if (fabric->descriptors[i].id == descriptor_id && fabric->descriptors[i].active) {
            return &fabric->descriptors[i];
        }
    }
    return NULL;
}

af_app_descriptor_t *af_get_descriptor_by_name(app_fabric_t *fabric, const char *name) {
    if (!fabric || !name) return NULL;
    for (uint32_t i = 0; i < fabric->num_descriptors; i++) {
        if (fabric->descriptors[i].active && af_str_cmp(fabric->descriptors[i].name, name) == 0) {
            return &fabric->descriptors[i];
        }
    }
    return NULL;
}

/* ===== App Instance Management ===== */

int32_t af_create_instance(app_fabric_t *fabric,
                           uint32_t descriptor_id, uint32_t owner_id) {
    if (!fabric || fabric->num_instances >= AF_MAX_APP_INSTANCES) return -1;
    
    af_app_descriptor_t *desc = af_get_descriptor(fabric, descriptor_id);
    if (!desc) return -1;
    
    if_identity_t *owner = if_get_identity(fabric->identity, owner_id);
    if (!owner) return -1;
    
    af_app_instance_t *inst = &fabric->instances[fabric->num_instances];
    af_mem_set(inst, 0, sizeof(*inst));
    inst->id = fabric->next_instance_id++;
    inst->descriptor_id = descriptor_id;
    inst->owner_id = owner_id;
    inst->state = AF_INSTANCE_CREATED;
    
    /* Create compute domain for this app */
    if (fabric->compute) {
        int32_t dom_id = cf_create_domain(fabric->compute, desc->name, 0, 100, 1000, EV_PRIORITY_NORMAL, EV_CONSISTENCY_LOCAL);
        if (dom_id >= 0) inst->compute_domain_id = (uint32_t)dom_id;
    }
    
    /* Create financial account */
    if (fabric->financial) {
        uint64_t initial_balances[9] = {0};
        initial_balances[4] = 10000;  /* Form 5: Financial */
        inst->financial_account_id = ff_create_account(fabric->financial, desc->name, &owner->critical_word, initial_balances);
    }
    
    /* Create storage volume */
    if (fabric->storage) {
        /* Would create a volume for this app */
        inst->storage_volume_id = 1;  /* Placeholder */
    }
    
    /* Initialize M5 */
    inst->m5.omega = fabric->num_instances + 1;
    inst->m5.r = SR_FROM_FLOAT(9.0);
    inst->m5.ell = SR_ONE;
    inst->m5.phi = SR_ZERO;
    inst->m5.chi = 0;
    inst->coverage_ratio = af_compute_coverage(&inst->m5);
    
    inst->attestation = LPRES_STATE_NEITHER;
    inst->active = true;
    
    fabric->num_instances++;
    fabric->stats.total_instances_created++;
    
    return af_attest(fabric, inst->id, 0x2000, inst, 0);
}

af_app_instance_t *af_get_instance(app_fabric_t *fabric, uint32_t instance_id) {
    if (!fabric) return NULL;
    for (uint32_t i = 0; i < fabric->num_instances; i++) {
        if (fabric->instances[i].id == instance_id && fabric->instances[i].active) {
            return &fabric->instances[i];
        }
    }
    return NULL;
}

int32_t af_start_instance(app_fabric_t *fabric, uint32_t instance_id) {
    if (!fabric) return -1;
    af_app_instance_t *inst = af_get_instance(fabric, instance_id);
    if (!inst) return -1;
    
    af_app_descriptor_t *desc = af_get_descriptor(fabric, inst->descriptor_id);
    if (!desc) return -1;
    
    /* Grant capabilities */
    if (fabric->config.auto_grant_builtin_capabilities && desc->builtin) {
        for (uint32_t i = 0; i < desc->num_capabilities; i++) {
            for (uint32_t j = 0; j < fabric->num_capabilities; j++) {
                if (af_str_cmp(fabric->capabilities[j].name, desc->capabilities[i]) == 0) {
                    af_grant_capability(fabric, instance_id, fabric->capabilities[j].id);
                    break;
                }
            }
        }
    }
    
    /* Create TLS session for sandboxing */
    if (fabric->security) {
        /* secf_tls_create_session(...); */
        inst->tls_session_id = 1;
    }
    
    /* Start compute domain */
    if (fabric->compute && inst->compute_domain_id > 0) {
        /* cf_start_domain(...); */
    }
    
    inst->state = AF_INSTANCE_RUNNING;
    fabric->stats.total_instances_started++;
    
    return af_attest(fabric, instance_id, 0x3000, inst, 0);
}

int32_t af_stop_instance(app_fabric_t *fabric, uint32_t instance_id) {
    if (!fabric) return -1;
    af_app_instance_t *inst = af_get_instance(fabric, instance_id);
    if (!inst) return -1;
    
    inst->state = AF_INSTANCE_STOPPING;
    
    /* Stop compute domain */
    if (fabric->compute && inst->compute_domain_id > 0) {
        /* cf_stop_domain(...); */
    }
    
    /* Revoke capabilities */
    for (uint32_t i = 0; i < inst->num_capabilities; i++) {
        af_revoke_capability(fabric, instance_id, inst->capability_ids[i]);
    }
    
    inst->state = AF_INSTANCE_STOPPED;
    fabric->stats.total_instances_stopped++;
    
    return af_attest(fabric, instance_id, 0x4000, inst, 0);
}

int32_t af_suspend_instance(app_fabric_t *fabric, uint32_t instance_id) {
    if (!fabric) return -1;
    af_app_instance_t *inst = af_get_instance(fabric, instance_id);
    if (!inst) return -1;
    
    inst->state = AF_INSTANCE_SUSPENDED;
    return af_attest(fabric, instance_id, 0x5000, inst, 0);
}

int32_t af_restart_instance(app_fabric_t *fabric, uint32_t instance_id) {
    if (!fabric) return -1;
    af_app_instance_t *inst = af_get_instance(fabric, instance_id);
    if (!inst) return -1;
    
    af_stop_instance(fabric, instance_id);
    return af_start_instance(fabric, instance_id);
}

/* ===== Capability Management ===== */

int32_t af_register_capability(app_fabric_t *fabric,
                               const char *name, const char *description,
                               surplus_real_t min_coverage,
                               lpres_state_t min_attestation,
                               bool requires_hsm, bool requires_zk,
                               uint8_t capital_form, uint64_t min_balance,
                               uint32_t credential_schema_id,
                               uint32_t policy_id) {
    if (!fabric || !name || fabric->num_capabilities >= AF_MAX_CAPABILITIES) return -1;
    
    af_capability_t *cap = &fabric->capabilities[fabric->num_capabilities];
    af_mem_set(cap, 0, sizeof(*cap));
    cap->id = fabric->num_capabilities;
    
    af_str_copy(cap->name, name, AF_MAX_NAME_LEN);
    af_str_copy(cap->description, description, AF_MAX_NAME_LEN);
    cap->min_coverage_ratio = min_coverage;
    cap->min_attestation = min_attestation;
    cap->requires_hsm = requires_hsm;
    cap->requires_zk_proof = requires_zk;
    cap->capital_form_required = capital_form;
    cap->min_balance = min_balance;
    cap->credential_schema_id = credential_schema_id;
    cap->policy_id = policy_id;
    cap->attestation = LPRES_STATE_NEITHER;
    cap->active = true;
    
    fabric->num_capabilities++;
    return af_attest(fabric, 0xFFFFFFFF, 0x6000 | cap->id, cap, 0);
}

int32_t af_grant_capability(app_fabric_t *fabric,
                            uint32_t instance_id, uint32_t capability_id) {
    if (!fabric) return -1;
    af_app_instance_t *inst = af_get_instance(fabric, instance_id);
    if (!inst) return -1;
    if (capability_id >= fabric->num_capabilities) return -1;
    
    af_capability_t *cap = &fabric->capabilities[capability_id];
    if (!cap->active) return -1;
    
    /* Check requirements */
    if (SR_CMP(inst->coverage_ratio, cap->min_coverage_ratio) < 0) return -1;
    if ((int)inst->attestation < (int)cap->min_attestation) return -1;
    
    if (cap->requires_hsm && !inst->sandboxed) return -1;
    if (cap->requires_zk_proof) {
        /* Would verify ZK proof */
    }
    
    if (cap->capital_form_required > 0 && cap->min_balance > 0) {
        if (fabric->financial) {
            ff_account_t *acc = ff_get_account(fabric->financial, inst->financial_account_id);
            if (!acc || acc->balances[cap->capital_form_required - 1] < cap->min_balance) return -1;
        }
    }
    
    if (inst->num_capabilities < AF_MAX_CAPABILITIES) {
        inst->capability_ids[inst->num_capabilities++] = capability_id;
    }
    
    cap->attestation = LPRES_STATE_TRUE;
    fabric->stats.total_capability_grants++;
    
    return af_attest(fabric, instance_id, 0x7000 | capability_id, cap, 0);
}

int32_t af_revoke_capability(app_fabric_t *fabric,
                             uint32_t instance_id, uint32_t capability_id) {
    if (!fabric) return -1;
    af_app_instance_t *inst = af_get_instance(fabric, instance_id);
    if (!inst) return -1;
    
    for (uint32_t i = 0; i < inst->num_capabilities; i++) {
        if (inst->capability_ids[i] == capability_id) {
            for (uint32_t j = i; j < inst->num_capabilities - 1; j++) {
                inst->capability_ids[j] = inst->capability_ids[j + 1];
            }
            inst->num_capabilities--;
            break;
        }
    }
    
    if (capability_id < fabric->num_capabilities) {
        fabric->capabilities[capability_id].attestation = LPRES_STATE_FALSE;
    }
    
    fabric->stats.total_capability_revokes++;
    return af_attest(fabric, instance_id, 0x8000 | capability_id, NULL, 0);
}

/* ===== Event Dispatch ===== */

int32_t af_dispatch_event(app_fabric_t *fabric,
                          uint32_t instance_id,
                          const char *schema, uint16_t schema_version,
                          const uint8_t *payload, uint16_t payload_len) {
    if (!fabric) return -1;
    af_app_instance_t *inst = af_get_instance(fabric, instance_id);
    if (!inst || inst->state != AF_INSTANCE_RUNNING) return -1;
    
    af_app_descriptor_t *desc = af_get_descriptor(fabric, inst->descriptor_id);
    if (!desc) return -1;
    
    /* Check if instance accepts this schema */
    bool accepts = false;
    for (uint32_t i = 0; i < desc->num_accepted_schemas; i++) {
        if (af_str_cmp(desc->accepted_schemas[i], schema) == 0) {
            accepts = true; break;
        }
    }
    if (!accepts) return -1;
    
    /* Dispatch via Compute Fabric */
    if (fabric->compute && inst->compute_domain_id > 0) {
        /* cf_send_event(...); */
    }
    
    inst->events_processed++;
    fabric->stats.total_events_dispatched++;
    
    return af_attest(fabric, instance_id, 0x9000, (void*)schema, 0);
}

/* ===== Self-Audit ===== */

int32_t af_run_self_audit(app_fabric_t *fabric, uint32_t instance_id) {
    if (!fabric) return -1;
    af_app_instance_t *inst = af_get_instance(fabric, instance_id);
    if (!inst) return -1;
    
    if (!fabric->config.require_self_audit) return 0;
    
    /* Run self-audit checks */
    bool audit_passed = true;
    
    /* Check coverage */
    inst->coverage_ratio = af_compute_coverage(&inst->m5);
    if (SR_CMP(inst->coverage_ratio, fabric->config.min_instance_coverage) < 0) {
        audit_passed = false;
    }
    
    /* Check capabilities */
    for (uint32_t i = 0; i < inst->num_capabilities; i++) {
        uint32_t cap_id = inst->capability_ids[i];
        if (cap_id < fabric->num_capabilities) {
            af_capability_t *cap = &fabric->capabilities[cap_id];
            if (SR_CMP(inst->coverage_ratio, cap->min_coverage_ratio) < 0) {
                audit_passed = false;
            }
        }
    }
    
    /* Check financial health */
    if (fabric->financial && inst->financial_account_id > 0) {
        ff_check_account_health(fabric->financial, inst->financial_account_id, NULL);
    }
    
    /* Check identity health */
    if (fabric->identity && inst->owner_id > 0) {
        if_check_identity_health(fabric->identity, inst->owner_id, NULL);
    }
    
    if (audit_passed) {
        inst->self_audit_passes++;
        inst->attestation = LPRES_STATE_TRUE;
    } else {
        inst->self_audit_failures++;
        inst->attestation = LPRES_STATE_BOTH;
        
        if (fabric->config.auto_heal) {
            inst->state = AF_INSTANCE_QUARANTINED;
            fabric->stats.total_healings++;
        }
    }
    
    fabric->stats.total_self_audits++;
    return audit_passed ? 0 : -1;
}

int32_t af_run_system_audit(app_fabric_t *fabric) {
    if (!fabric) return -1;
    
    int32_t failed = 0;
    for (uint32_t i = 0; i < fabric->num_instances; i++) {
        if (fabric->instances[i].active) {
            if (af_run_self_audit(fabric, fabric->instances[i].id) < 0) {
                failed++;
            }
        }
    }
    
    return failed == 0 ? 0 : -1;
}

/* ===== Health & Attestation ===== */

int32_t af_check_instance_health(app_fabric_t *fabric,
                                 uint32_t instance_id,
                                 void *health_out) {
    if (!fabric) return -1;
    af_app_instance_t *inst = af_get_instance(fabric, instance_id);
    if (!inst) return -1;
    
    /* Update coverage */
    inst->coverage_ratio = af_compute_coverage(&inst->m5);
    
    /* Run self-audit */
    af_run_self_audit(fabric, instance_id);
    
    /* Check compute domain health */
    if (fabric->compute && inst->compute_domain_id > 0) {
        cf_check_domain_health(fabric->compute, inst->compute_domain_id, NULL);
    }
    
    return inst->attestation == LPRES_STATE_TRUE ? 0 : -1;
}

int32_t af_check_global_health(app_fabric_t *fabric) {
    if (!fabric) return -1;
    
    int32_t unhealthy = 0;
    for (uint32_t i = 0; i < fabric->num_instances; i++) {
        if (fabric->instances[i].active) {
            if (af_check_instance_health(fabric, fabric->instances[i].id, NULL) < 0) {
                unhealthy++;
            }
        }
    }
    
    fabric->global_safety_gate = (unhealthy == 0);
    fabric->global_attestation = fabric->global_safety_gate ? LPRES_STATE_TRUE : LPRES_STATE_BOTH;
    
    return unhealthy == 0 ? 0 : -1;
}

bool af_global_safety_gate(app_fabric_t *fabric) {
    return fabric ? fabric->global_safety_gate : false;
}

/* ===== Coverage ===== */

void af_update_coverage(app_fabric_t *fabric) {
    if (!fabric) return;
    
    fabric->coverage_ratio = af_compute_coverage(&fabric->m5);
    
    for (uint32_t i = 0; i < fabric->num_descriptors; i++) {
        if (fabric->descriptors[i].active) {
            fabric->descriptors[i].coverage_ratio = af_compute_coverage(&fabric->descriptors[i].m5);
        }
    }
    
    for (uint32_t i = 0; i < fabric->num_instances; i++) {
        if (fabric->instances[i].active) {
            fabric->instances[i].coverage_ratio = af_compute_coverage(&fabric->instances[i].m5);
        }
    }
}

bool af_enforce_coverage(app_fabric_t *fabric, surplus_real_t min_ratio) {
    if (!fabric) return false;
    
    if (SR_CMP(fabric->coverage_ratio, min_ratio) < 0) return false;
    
    for (uint32_t i = 0; i < fabric->num_instances; i++) {
        if (fabric->instances[i].active) {
            if (SR_CMP(fabric->instances[i].coverage_ratio, min_ratio) < 0) return false;
        }
    }
    
    return true;
}

/* ===== Statistics ===== */

void af_get_stats(app_fabric_t *fabric, void *stats_out) {
    if (!fabric || !stats_out) return;
    af_mem_copy(stats_out, &fabric->stats, sizeof(fabric->stats));
}

/* ===== Paraconsistent State ===== */

lpres_state_t af_get_attestation(app_fabric_t *fabric, uint32_t instance_id) {
    if (!fabric || instance_id >= fabric->num_instances) return LPRES_STATE_NEITHER;
    return fabric->instances[instance_id].attestation;
}

void af_set_attestation(app_fabric_t *fabric, uint32_t instance_id, lpres_state_t state) {
    if (!fabric || instance_id >= fabric->num_instances) return;
    fabric->instances[instance_id].attestation = state;
}

/* ===== Utility ===== */

const char *af_lpres_state_name(lpres_state_t state) {
    return lpres_state_name(state);
}

const char *af_app_type_name(af_app_type_t type) {
    static const char *names[] = {"UNUSED", "WALLET", "DERIVATIVES", "ASSURANCE", "TREATY", "CUSTOM", "SYSTEM"};
    if (type <= AF_APP_SYSTEM) return names[type];
    return "UNKNOWN";
}

const char *af_instance_state_name(int state) {
    static const char *names[] = {"CREATED", "STARTING", "RUNNING", "SUSPENDED", "STOPPING", "STOPPED", "CRASHED", "QUARANTINED"};
    if (state >= 0 && state <= 7) return names[state];
    return "UNKNOWN";
}
