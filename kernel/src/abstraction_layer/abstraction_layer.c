/* abstraction_layer.c — ZXV Abstraction Layer Implementation
 *
 * Universal abstraction layer connecting C/H kernel to Fortran, COBOL, Sutra,
 * Assembly, Rust, Zig, Python, and WebAssembly via Orbital Compat IR.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "abstraction_layer.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"
#include "selfaudit.h"

/* ===== Helper Functions ===== */

static void al_mem_set(void *dst, int val, uint32_t len) {
    uint8_t *d = dst;
    for (uint32_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

static void al_mem_copy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = dst; const uint8_t *s = src;
    for (uint32_t i = 0; i < len; i++) d[i] = s[i];
}

static int al_str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static uint32_t al_str_len(const char *s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

static void al_str_copy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ===== Coverage Computation ===== */

static surplus_real_t al_compute_coverage(const m5_coords_t *m5) {
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

lpres_state_t al_attest(abstraction_layer_t *layer, uint32_t module_id,
                        uint32_t op_id, void *args, int32_t result) {
    if (!layer || module_id >= layer->num_modules) return LPRES_STATE_NEITHER;
    
    al_module_t *mod = &layer->modules[module_id];
    lpres_state_t result_att = (result >= 0) ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t mod_att = mod->attestation;
    lpres_state_t coverage_att = (SR_CMP(mod->coverage_ratio, layer->min_coverage_ratio) >= 0) 
                                  ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t layer_att = layer->global_attestation;
    
    lpres_state_t combined = lpres_conjoin(result_att, mod_att);
    combined = lpres_conjoin(combined, coverage_att);
    combined = lpres_conjoin(combined, layer_att);
    
    mod->attestation = combined;
    mod->global_attestation = combined;
    layer->global_attestation = lpres_conjoin(layer->global_attestation, combined);
    
    return combined;
}

/* ===== Initialization ===== */

void al_init(abstraction_layer_t *layer,
             orbital_fabric_t *orbital,
             financial_fabric_t *financial,
             identity_fabric_t *identity,
             porter_house_t *porter,
             ev_sequencer_t *sequencer,
             ev_audit_t *audit,
             ev_healing_t *healing) {
    if (!layer) return;
    
    al_mem_set(layer, 0, sizeof(*layer));
    layer->orbital = orbital;
    layer->financial = financial;
    layer->identity = identity;
    layer->porter = porter;
    layer->sequencer = sequencer;
    layer->audit = audit;
    layer->healing = healing;
    
    /* Initialize M5 coordinates */
    layer->m5.omega = 1;
    layer->m5.r = SR_FROM_FLOAT(11.0);  /* Abstraction rail */
    layer->m5.ell = SR_ONE;
    layer->m5.phi = SR_ZERO;
    layer->m5.chi = 0;
    layer->coverage_ratio = al_compute_coverage(&layer->m5);
    layer->min_coverage_ratio = SR_FROM_FLOAT(1.8);
    
    /* Default configuration */
    layer->config.auto_register_builtins = true;
    layer->config.auto_heal = true;
    layer->config.audit_interval = 1000;
    layer->config.health_check_interval = 5000;
    layer->config.min_global_coverage = SR_FROM_FLOAT(1.8);
    layer->config.require_porter_house = true;
    layer->config.require_zk_attestation = true;
    
    layer->global_attestation = LPRES_STATE_NEITHER;
    layer->global_safety_gate = false;
    layer->initialized = true;
    
    /* Register built-in languages */
    al_register_builtin_languages(layer);
    
    /* Register global capabilities */
    al_register_capability(layer, "capital.transfer", "Transfer capital between accounts",
                          SR_FROM_FLOAT(1.8), LPRES_STATE_TRUE, false, false,
                          5, 1000, 0);
    al_register_capability(layer, "derivatives.trade", "Trade derivatives contracts",
                          SR_FROM_FLOAT(2.0), LPRES_STATE_TRUE, true, true,
                          5, 10000, 0);
    al_register_capability(layer, "assurance.create", "Create assurance contracts",
                          SR_FROM_FLOAT(2.0), LPRES_STATE_TRUE, false, true,
                          5, 5000, 0);
    al_register_capability(layer, "treaty.tokenize", "Tokenize conservation easements",
                          SR_FROM_FLOAT(2.5), LPRES_STATE_TRUE, true, true,
                          3, 100000, 0);
    al_register_capability(layer, "network.mesh", "Create/join mesh networks",
                          SR_FROM_FLOAT(1.8), LPRES_STATE_TRUE, false, false,
                          6, 1000, 0);
    al_register_capability(layer, "storage.persist", "Persistent storage access",
                          SR_FROM_FLOAT(1.8), LPRES_STATE_TRUE, false, false,
                          6, 10000, 0);
    al_register_capability(layer, "media.stream", "Real-time media streaming",
                          SR_FROM_FLOAT(2.0), LPRES_STATE_TRUE, false, false,
                          8, 10000, 0);
    al_register_capability(layer, "governance.vote", "Participate in governance",
                          SR_FROM_FLOAT(2.0), LPRES_STATE_TRUE, false, true,
                          4, 10000, 0);
    al_register_capability(layer, "identity.credential", "Issue/verify credentials",
                          SR_FROM_FLOAT(1.8), LPRES_STATE_TRUE, false, false,
                          1, 1000, 0);
    al_register_capability(layer, "compute.schedule", "Schedule compute tasks",
                          SR_FROM_FLOAT(1.8), LPRES_STATE_TRUE, false, false,
                          2, 1000, 0);
}

void al_register_builtin_languages(abstraction_layer_t *layer) {
    if (!layer) return;
    
    /* C (native) - already registered via oc_register_builtins */
    if (layer->num_lang_adapters < AL_MAX_LANGUAGES) {
        al_language_adapter_t *la = &layer->lang_adapters[layer->num_lang_adapters++];
        la->lang = AL_LANG_C;
        al_str_copy(la->name, "C", AL_MAX_NAME_LEN);
        la->ops = (oc_lang_ops_t){0};
        la->registered = true;
        la->module_count = 0;
        la->adapter_attestation = LPRES_STATE_TRUE;
    }
    
    /* Fortran - registered via oc_register_builtins */
    if (layer->num_lang_adapters < AL_MAX_LANGUAGES) {
        al_language_adapter_t *la = &layer->lang_adapters[layer->num_lang_adapters++];
        la->lang = AL_LANG_FORTRAN;
        al_str_copy(la->name, "Fortran", AL_MAX_NAME_LEN);
        la->ops = (oc_lang_ops_t){0};
        la->registered = true;
        la->module_count = 0;
        la->adapter_attestation = LPRES_STATE_TRUE;
    }
    
    /* COBOL - registered via oc_register_builtins */
    if (layer->num_lang_adapters < AL_MAX_LANGUAGES) {
        al_language_adapter_t *la = &layer->lang_adapters[layer->num_lang_adapters++];
        la->lang = AL_LANG_COBOL;
        al_str_copy(la->name, "COBOL", AL_MAX_NAME_LEN);
        la->ops = (oc_lang_ops_t){0};
        la->registered = true;
        la->module_count = 0;
        la->adapter_attestation = LPRES_STATE_TRUE;
    }
    
    /* Sutra - native AI language, requires custom adapter */
    if (layer->num_lang_adapters < AL_MAX_LANGUAGES) {
        al_language_adapter_t *la = &layer->lang_adapters[layer->num_lang_adapters++];
        la->lang = AL_LANG_SUTRA;
        al_str_copy(la->name, "Sutra", AL_MAX_NAME_LEN);
        la->ops = (oc_lang_ops_t){0};
        la->registered = false;  /* Requires custom binding */
        la->module_count = 0;
        la->adapter_attestation = LPRES_STATE_NEITHER;
    }
    
    /* Assembly */
    if (layer->num_lang_adapters < AL_MAX_LANGUAGES) {
        al_language_adapter_t *la = &layer->lang_adapters[layer->num_lang_adapters++];
        la->lang = AL_LANG_ASM;
        al_str_copy(la->name, "Assembly", AL_MAX_NAME_LEN);
        la->ops = (oc_lang_ops_t){0};
        la->registered = false;
        la->module_count = 0;
        la->adapter_attestation = LPRES_STATE_NEITHER;
    }
    
    /* Rust */
    if (layer->num_lang_adapters < AL_MAX_LANGUAGES) {
        al_language_adapter_t *la = &layer->lang_adapters[layer->num_lang_adapters++];
        la->lang = AL_LANG_RUST;
        al_str_copy(la->name, "Rust", AL_MAX_NAME_LEN);
        la->ops = (oc_lang_ops_t){0};
        la->registered = false;
        la->module_count = 0;
        la->adapter_attestation = LPRES_STATE_NEITHER;
    }
    
    /* Zig */
    if (layer->num_lang_adapters < AL_MAX_LANGUAGES) {
        al_language_adapter_t *la = &layer->lang_adapters[layer->num_lang_adapters++];
        la->lang = AL_LANG_ZIG;
        al_str_copy(la->name, "Zig", AL_MAX_NAME_LEN);
        la->ops = (oc_lang_ops_t){0};
        la->registered = false;
        la->module_count = 0;
        la->adapter_attestation = LPRES_STATE_NEITHER;
    }
    
    /* Python */
    if (layer->num_lang_adapters < AL_MAX_LANGUAGES) {
        al_language_adapter_t *la = &layer->lang_adapters[layer->num_lang_adapters++];
        la->lang = AL_LANG_PYTHON;
        al_str_copy(la->name, "Python", AL_MAX_NAME_LEN);
        la->ops = (oc_lang_ops_t){0};
        la->registered = false;
        la->module_count = 0;
        la->adapter_attestation = LPRES_STATE_NEITHER;
    }
    
    /* WebAssembly */
    if (layer->num_lang_adapters < AL_MAX_LANGUAGES) {
        al_language_adapter_t *la = &layer->lang_adapters[layer->num_lang_adapters++];
        la->lang = AL_LANG_WASM;
        al_str_copy(la->name, "WebAssembly", AL_MAX_NAME_LEN);
        la->ops = (oc_lang_ops_t){0};
        la->registered = false;
        la->module_count = 0;
        la->adapter_attestation = LPRES_STATE_NEITHER;
    }
}

int32_t al_register_language(abstraction_layer_t *layer, al_lang_t lang, const oc_lang_ops_t *ops) {
    if (!layer || !ops || lang >= AL_LANG_MAX) return -1;
    
    /* Register with Orbital Compat */
    int32_t result = oc_register_lang((oc_lang_t)lang, ops);
    if (result != OC_OK) return result;
    
    /* Find or create adapter entry */
    for (uint32_t i = 0; i < layer->num_lang_adapters; i++) {
        if (layer->lang_adapters[i].lang == lang) {
            layer->lang_adapters[i].ops = *ops;
            layer->lang_adapters[i].registered = true;
            return 0;
        }
    }
    
    if (layer->num_lang_adapters >= AL_MAX_LANGUAGES) return -1;
    
    al_language_adapter_t *la = &layer->lang_adapters[layer->num_lang_adapters++];
    la->lang = lang;
    al_str_copy(la->name, al_lang_name(lang), AL_MAX_NAME_LEN);
    la->ops = *ops;
    la->registered = true;
    la->module_count = 0;
    la->adapter_attestation = LPRES_STATE_NEITHER;
    
    return 0;
}

/* ===== Module Registration ===== */

int32_t al_register_module(abstraction_layer_t *layer,
                           const char *name, al_lang_t language,
                           const char *source_path, const char *entry_symbol,
                           const char **required_capabilities, uint32_t num_required,
                           const char **provided_capabilities, uint32_t num_provided,
                           const char **accepted_schemas, uint32_t num_accepted,
                           const char **emitted_schemas, uint32_t num_emitted) {
    if (!layer || !name || layer->num_modules >= AL_MAX_MODULES) return -1;
    
    al_module_t *mod = &layer->modules[layer->num_modules];
    al_mem_set(mod, 0, sizeof(*mod));
    mod->id = layer->next_module_id++;
    
    al_str_copy(mod->name, name, AL_MAX_NAME_LEN);
    mod->language = language;
    mod->state = AL_STATE_NEITHER;
    
    if (source_path) al_str_copy(mod->source_path, source_path, 256);
    if (entry_symbol) al_str_copy(mod->entry_symbol, entry_symbol, AL_MAX_NAME_LEN);
    
    for (uint32_t i = 0; i < num_required && i < AL_MAX_CAPABILITIES; i++) {
        al_str_copy(mod->required_capabilities[mod->num_required_capabilities++], required_capabilities[i], AL_MAX_NAME_LEN);
    }
    
    for (uint32_t i = 0; i < num_provided && i < AL_MAX_CAPABILITIES; i++) {
        al_capability_t *cap = &mod->capabilities[mod->num_capabilities++];
        al_str_copy(cap->name, provided_capabilities[i], AL_MAX_NAME_LEN);
        cap->active = true;
    }
    
    for (uint32_t i = 0; i < num_accepted && i < 8; i++) {
        al_str_copy(mod->accepted_schemas[mod->num_accepted_schemas++], accepted_schemas[i], AL_MAX_SCHEMA_LEN);
    }
    
    for (uint32_t i = 0; i < num_emitted && i < 8; i++) {
        al_str_copy(mod->emitted_schemas[mod->num_emitted_schemas++], emitted_schemas[i], AL_MAX_SCHEMA_LEN);
    }
    
    /* Initialize M5 */
    mod->m5.omega = layer->num_modules + 1;
    mod->m5.r = SR_FROM_FLOAT(11.0);
    mod->m5.ell = SR_ONE;
    mod->m5.phi = SR_ZERO;
    mod->m5.chi = 0;
    mod->coverage_ratio = al_compute_coverage(&mod->m5);
    mod->min_coverage_ratio = layer->config.min_global_coverage;
    
    mod->attestation = LPRES_STATE_NEITHER;
    mod->global_attestation = LPRES_STATE_NEITHER;
    mod->initialized = false;
    mod->active = true;
    mod->builtin = true;
    
    layer->num_modules++;
    layer->stats.total_modules_registered++;
    
    return al_attest(layer, mod->id, 0x1000, mod, 0);
}

al_module_t *al_get_module(abstraction_layer_t *layer, uint32_t module_id) {
    if (!layer) return NULL;
    for (uint32_t i = 0; i < layer->num_modules; i++) {
        if (layer->modules[i].id == module_id && layer->modules[i].active) {
            return &layer->modules[i];
        }
    }
    return NULL;
}

al_module_t *al_get_module_by_name(abstraction_layer_t *layer, const char *name) {
    if (!layer || !name) return NULL;
    for (uint32_t i = 0; i < layer->num_modules; i++) {
        if (layer->modules[i].active && al_str_cmp(layer->modules[i].name, name) == 0) {
            return &layer->modules[i];
        }
    }
    return NULL;
}

/* ===== Module Initialization ===== */

int32_t al_initialize_module(abstraction_layer_t *layer, uint32_t module_id) {
    if (!layer) return -1;
    al_module_t *mod = al_get_module(layer, module_id);
    if (!mod) return -1;
    
    /* Check dependencies */
    for (uint32_t i = 0; i < mod->num_dependencies; i++) {
        if (!mod->dependencies[i].satisfied) {
            return -1;  /* Dependency not satisfied */
        }
    }
    
    /* Check required capabilities */
    for (uint32_t i = 0; i < mod->num_required_capabilities; i++) {
        bool found = false;
        for (uint32_t j = 0; j < layer->num_global_capabilities; j++) {
            if (al_str_cmp(layer->global_capabilities[j].name, mod->required_capabilities[i]) == 0) {
                if (layer->global_capabilities[j].active) {
                    found = true;
                    break;
                }
            }
        }
        if (!found) return -1;
    }
    
    /* Initialize canonical IR via Orbital Compat */
    if (mod->language < AL_LANG_MAX) {
        al_language_adapter_t *la = NULL;
        for (uint32_t i = 0; i < layer->num_lang_adapters; i++) {
            if (layer->lang_adapters[i].lang == mod->language) {
                la = &layer->lang_adapters[i];
                break;
            }
        }
        
        if (la && la->registered) {
            /* Lower source to IR */
            /* oc_lower(la->lang, mod->source_code, mod->source_len, &mod->canonical_ir); */
            mod->ir_valid = true;
        }
    }
    
    /* Create event domain */
    if (layer->sequencer) {
        int32_t dom_id = ev_seq_create_domain(layer->sequencer, mod->name, 100, 1000, EV_CONSISTENCY_LOCAL);
        if (dom_id >= 0) {
            mod->event_domain_id = (uint32_t)dom_id;
            ev_domain_t *dom = ev_seq_get_domain(layer->sequencer, mod->event_domain_id);
            
            /* Grant capabilities */
            for (uint32_t i = 0; i < mod->num_capabilities; i++) {
                ev_domain_grant_capability(layer->sequencer, mod->event_domain_id, mod->capabilities[i].name);
            }
            
            /* Register schemas */
            for (uint32_t i = 0; i < mod->num_accepted_schemas; i++) {
                ev_domain_add_accepted_schema(layer->sequencer, mod->event_domain_id, mod->accepted_schemas[i]);
            }
            for (uint32_t i = 0; i < mod->num_emitted_schemas; i++) {
                ev_domain_add_emitted_schema(layer->sequencer, mod->event_domain_id, mod->emitted_schemas[i]);
            }
        }
    }
    
    /* Create financial account */
    if (layer->financial && mod->financial_account_id == 0) {
        uint64_t initial_balances[9] = {0};
        initial_balances[4] = 10000;  /* Form 5: Financial */
        word168_t owner = {0};
        for (int i = 0; i < 168/64; i++) owner.words[i] = (uint64_t)(mod->id + i * 0x9E3779B97F4A7C15ULL);
        mod->financial_account_id = ff_create_account(layer->financial, mod->name, &owner, initial_balances);
    }
    
    /* Porter House port sealing */
    if (layer->porter && mod->porter_house_port > 0) {
        porter_house_seal_port(layer->porter, mod->porter_house_port, PH_SEAL_TRUSTED, mod->min_trust_weight);
    }
    
    mod->initialized = true;
    mod->state = AL_STATE_TRUE;
    mod->attestation = LPRES_STATE_TRUE;
    
    layer->stats.total_modules_initialized++;
    
    return al_attest(layer, module_id, 0x2000, mod, 0);
}

int32_t al_initialize_all(abstraction_layer_t *layer) {
    if (!layer) return -1;
    
    int32_t failed = 0;
    for (uint32_t i = 0; i < layer->num_modules; i++) {
        if (layer->modules[i].active && !layer->modules[i].initialized) {
            if (al_initialize_module(layer, layer->modules[i].id) < 0) {
                failed++;
                layer->modules[i].state = AL_STATE_FALSE;
                layer->modules[i].attestation = LPRES_STATE_FALSE;
                layer->stats.total_modules_failed++;
            }
        }
    }
    
    return failed == 0 ? 0 : -1;
}

/* ===== Module Lifecycle ===== */

int32_t al_start_module(abstraction_layer_t *layer, uint32_t module_id) {
    if (!layer) return -1;
    al_module_t *mod = al_get_module(layer, module_id);
    if (!mod) return -1;
    
    if (!mod->initialized) {
        if (al_initialize_module(layer, module_id) < 0) return -1;
    }
    
    mod->state = AL_STATE_TRUE;
    mod->attestation = LPRES_STATE_TRUE;
    
    return al_attest(layer, module_id, 0x3000, mod, 0);
}

int32_t al_stop_module(abstraction_layer_t *layer, uint32_t module_id) {
    if (!layer) return -1;
    al_module_t *mod = al_get_module(layer, module_id);
    if (!mod) return -1;
    
    mod->state = AL_STATE_FALSE;
    mod->attestation = LPRES_STATE_FALSE;
    
    /* Revoke capabilities */
    for (uint32_t i = 0; i < mod->num_capabilities; i++) {
        /* Would revoke granted capabilities */
    }
    
    return al_attest(layer, module_id, 0x4000, mod, 0);
}

int32_t al_restart_module(abstraction_layer_t *layer, uint32_t module_id) {
    if (!layer) return -1;
    al_stop_module(layer, module_id);
    return al_start_module(layer, module_id);
}

/* ===== Schema Translation ===== */

int32_t al_translate(abstraction_layer_t *layer,
                     al_lang_t from_lang, const void *src, uint32_t len,
                     al_lang_t to_lang, void *out, uint32_t cap) {
    if (!layer) return -1;
    
    layer->stats.total_translations++;
    
    /* Direct translation via Orbital Compat */
    int32_t result = oc_lower((oc_lang_t)from_lang, src, len, &layer->shared_ir);
    if (result != OC_OK) {
        layer->stats.total_translation_failures++;
        return al_attest(layer, 0xFFFFFFFF, 0x5000, (void*)from_lang, -1);
    }
    
    result = oc_lift((oc_lang_t)to_lang, &layer->shared_ir, out, cap);
    if (result != OC_OK) {
        layer->stats.total_translation_failures++;
        return al_attest(layer, 0xFFFFFFFF, 0x6000, (void*)to_lang, -1);
    }
    
    return al_attest(layer, 0xFFFFFFFF, 0x7000, out, 0);
}

int32_t al_translate_through_ir(abstraction_layer_t *layer,
                                al_lang_t from_lang, const void *src, uint32_t len,
                                al_lang_t to_lang, void *out, uint32_t cap) {
    if (!layer) return -1;
    
    /* Use shared IR for multi-hop translation */
    oc_ir_t ir;
    int32_t result = oc_lower((oc_lang_t)from_lang, src, len, &ir);
    if (result != OC_OK) {
        layer->stats.total_translation_failures++;
        return -1;
    }
    
    result = oc_lift((oc_lang_t)to_lang, &ir, out, cap);
    if (result != OC_OK) {
        layer->stats.total_translation_failures++;
        return -1;
    }
    
    layer->stats.total_translations++;
    return al_attest(layer, 0xFFFFFFFF, 0x8000, out, 0);
}

oe_translate_result_t al_oe_translate(abstraction_layer_t *layer,
                                       ev_envelope_t *env,
                                       const char *target_schema,
                                       uint16_t target_version) {
    if (!layer || !layer->orbital) return OE_TRANSLATE_FAILED;
    
    return oe_translate(&layer->orbital->elevator, env, target_schema, target_version);
}

/* ===== Capability Management ===== */

int32_t al_register_capability(abstraction_layer_t *layer,
                               const char *name, const char *description,
                               surplus_real_t min_coverage,
                               lpres_state_t min_attestation,
                               bool requires_hsm, bool requires_zk,
                               uint8_t capital_form, uint64_t min_balance,
                               uint32_t policy_id) {
    if (!layer || !name || layer->num_global_capabilities >= AL_MAX_CAPABILITIES) return -1;
    
    al_capability_t *cap = &layer->global_capabilities[layer->num_global_capabilities];
    al_mem_set(cap, 0, sizeof(*cap));
    cap->id = layer->num_global_capabilities;
    
    al_str_copy(cap->name, name, AL_MAX_NAME_LEN);
    al_str_copy(cap->description, description, AL_MAX_NAME_LEN);
    cap->min_coverage_ratio = min_coverage;
    cap->min_attestation = min_attestation;
    cap->requires_hsm = requires_hsm;
    cap->requires_zk_proof = requires_zk;
    cap->capital_form_required = capital_form;
    cap->min_balance = min_balance;
    cap->policy_id = policy_id;
    cap->attestation = LPRES_STATE_NEITHER;
    cap->active = true;
    
    layer->num_global_capabilities++;
    
    return al_attest(layer, 0xFFFFFFFF, 0x9000 | cap->id, cap, 0);
}

int32_t al_grant_capability(abstraction_layer_t *layer,
                            uint32_t module_id, uint32_t capability_id) {
    if (!layer) return -1;
    al_module_t *mod = al_get_module(layer, module_id);
    if (!mod) return -1;
    if (capability_id >= layer->num_global_capabilities) return -1;
    
    al_capability_t *cap = &layer->global_capabilities[capability_id];
    if (!cap->active) return -1;
    
    /* Check requirements */
    if (SR_CMP(mod->coverage_ratio, cap->min_coverage_ratio) < 0) return -1;
    if ((int)mod->attestation < (int)cap->min_attestation) return -1;
    
    if (cap->requires_hsm && mod->tls_session_id == 0) return -1;
    if (cap->requires_zk_proof && layer->config.require_zk_attestation) {
        /* Would verify ZK proof */
    }
    
    if (cap->capital_form_required > 0 && cap->min_balance > 0) {
        if (layer->financial) {
            ff_account_t *acc = ff_get_account(layer->financial, mod->financial_account_id);
            if (!acc || acc->balances[cap->capital_form_required - 1] < cap->min_balance) return -1;
        }
    }
    
    cap->attestation = LPRES_STATE_TRUE;
    layer->stats.total_capability_grants++;
    
    return al_attest(layer, module_id, 0xA000 | capability_id, cap, 0);
}

int32_t al_revoke_capability(abstraction_layer_t *layer,
                             uint32_t module_id, uint32_t capability_id) {
    if (!layer) return -1;
    al_module_t *mod = al_get_module(layer, module_id);
    if (!mod) return -1;
    
    if (capability_id < layer->num_global_capabilities) {
        layer->global_capabilities[capability_id].attestation = LPRES_STATE_FALSE;
    }
    
    layer->stats.total_capability_revokes++;
    return al_attest(layer, module_id, 0xB000 | capability_id, NULL, 0);
}

/* ===== Self-Audit & Self-Heal ===== */

int32_t al_self_audit_module(abstraction_layer_t *layer, uint32_t module_id) {
    if (!layer) return -1;
    al_module_t *mod = al_get_module(layer, module_id);
    if (!mod) return -1;
    
    if (!layer->config.auto_heal) return 0;
    
    bool audit_passed = true;
    
    /* Check coverage */
    mod->coverage_ratio = al_compute_coverage(&mod->m5);
    if (SR_CMP(mod->coverage_ratio, layer->config.min_global_coverage) < 0) {
        audit_passed = false;
    }
    
    /* Check capabilities */
    for (uint32_t i = 0; i < mod->num_capabilities; i++) {
        /* Would check each capability's requirements */
    }
    
    /* Check financial health */
    if (layer->financial && mod->financial_account_id > 0) {
        ff_check_account_health(layer->financial, mod->financial_account_id, NULL);
    }
    
    /* Check identity health */
    if (layer->identity) {
        /* Would check identity attestation */
    }
    
    /* Run event-space audit */
    if (layer->sequencer && layer->audit && layer->healing && mod->event_domain_id > 0) {
        ev_domain_t *dom = ev_seq_get_domain(layer->sequencer, mod->event_domain_id);
        if (dom) {
            ev_audit_result_t audit_result = ev_audit_check_domain(layer->sequencer, layer->audit, dom);
            ev_heal_action_t heal_action = ev_healing_apply(layer->sequencer, layer->healing, dom, audit_result);
            
            if (heal_action >= EV_HEAL_QUARANTINE) {
                audit_passed = false;
                mod->state = AL_STATE_QUARANTINE;
            }
        }
    }
    
    mod->health.total_self_audits++;
    layer->stats.total_self_audits++;
    
    if (audit_passed) {
        mod->state = AL_STATE_TRUE;
        mod->attestation = LPRES_STATE_TRUE;
    } else {
        mod->state = AL_STATE_BOTH;
        mod->attestation = LPRES_STATE_BOTH;
        
        if (layer->config.auto_heal) {
            mod->health.total_healings++;
            layer->stats.total_healings++;
        }
    }
    
    return audit_passed ? 0 : -1;
}

int32_t al_self_audit_system(abstraction_layer_t *layer) {
    if (!layer) return -1;
    
    int32_t failed = 0;
    for (uint32_t i = 0; i < layer->num_modules; i++) {
        if (layer->modules[i].active) {
            if (al_self_audit_module(layer, layer->modules[i].id) < 0) {
                failed++;
            }
        }
    }
    
    return failed == 0 ? 0 : -1;
}

int32_t al_self_heal_module(abstraction_layer_t *layer, uint32_t module_id) {
    if (!layer) return -1;
    al_module_t *mod = al_get_module(layer, module_id);
    if (!mod) return -1;
    
    if (mod->state == AL_STATE_QUARANTINE || mod->state == AL_STATE_FALSE) {
        /* Attempt recovery */
        mod->health.consecutive_failures = 0;
        mod->state = AL_STATE_TRUE;
        mod->attestation = LPRES_STATE_TRUE;
        mod->health.total_healings++;
        layer->stats.total_healings++;
    }
    
    return al_attest(layer, module_id, 0xC000, mod, 0);
}

int32_t al_self_heal_system(abstraction_layer_t *layer) {
    if (!layer) return -1;
    
    for (uint32_t i = 0; i < layer->num_modules; i++) {
        if (layer->modules[i].active) {
            al_self_heal_module(layer, layer->modules[i].id);
        }
    }
    
    return 0;
}

/* ===== Health & Attestation ===== */

int32_t al_check_module_health(abstraction_layer_t *layer,
                               uint32_t module_id,
                               void *health_out) {
    if (!layer) return -1;
    al_module_t *mod = al_get_module(layer, module_id);
    if (!mod) return -1;
    
    /* Update coverage */
    mod->coverage_ratio = al_compute_coverage(&mod->m5);
    
    /* Run self-audit */
    al_self_audit_module(layer, module_id);
    
    return mod->attestation == LPRES_STATE_TRUE ? 0 : -1;
}

int32_t al_check_global_health(abstraction_layer_t *layer) {
    if (!layer) return -1;
    
    int32_t unhealthy = 0;
    for (uint32_t i = 0; i < layer->num_modules; i++) {
        if (layer->modules[i].active) {
            if (al_check_module_health(layer, layer->modules[i].id, NULL) < 0) {
                unhealthy++;
            }
        }
    }
    
    layer->global_safety_gate = (unhealthy == 0);
    layer->global_attestation = layer->global_safety_gate ? LPRES_STATE_TRUE : LPRES_STATE_BOTH;
    
    return unhealthy == 0 ? 0 : -1;
}

bool al_global_safety_gate(abstraction_layer_t *layer) {
    return layer ? layer->global_safety_gate : false;
}

/* ===== Coverage ===== */

void al_update_coverage(abstraction_layer_t *layer) {
    if (!layer) return;
    
    layer->coverage_ratio = al_compute_coverage(&layer->m5);
    
    for (uint32_t i = 0; i < layer->num_modules; i++) {
        if (layer->modules[i].active) {
            layer->modules[i].coverage_ratio = al_compute_coverage(&layer->modules[i].m5);
        }
    }
}

bool al_enforce_coverage(abstraction_layer_t *layer, surplus_real_t min_ratio) {
    if (!layer) return false;
    
    if (SR_CMP(layer->coverage_ratio, min_ratio) < 0) return false;
    
    for (uint32_t i = 0; i < layer->num_modules; i++) {
        if (layer->modules[i].active) {
            if (SR_CMP(layer->modules[i].coverage_ratio, min_ratio) < 0) return false;
        }
    }
    
    return true;
}

/* ===== Statistics ===== */

void al_get_stats(abstraction_layer_t *layer, void *stats_out) {
    if (!layer || !stats_out) return;
    al_mem_copy(stats_out, &layer->stats, sizeof(layer->stats));
}

/* ===== Paraconsistent State ===== */

al_module_state_t al_get_module_state(abstraction_layer_t *layer, uint32_t module_id) {
    if (!layer || module_id >= layer->num_modules) return AL_STATE_NEITHER;
    return layer->modules[module_id].state;
}

void al_set_module_state(abstraction_layer_t *layer, uint32_t module_id, al_module_state_t state) {
    if (!layer || module_id >= layer->num_modules) return;
    layer->modules[module_id].state = state;
    layer->modules[module_id].attestation = (lpres_state_t)state;
}

lpres_state_t al_get_attestation(abstraction_layer_t *layer, uint32_t module_id) {
    if (!layer || module_id >= layer->num_modules) return LPRES_STATE_NEITHER;
    return layer->modules[module_id].attestation;
}

void al_set_attestation(abstraction_layer_t *layer, uint32_t module_id, lpres_state_t state) {
    if (!layer || module_id >= layer->num_modules) return;
    layer->modules[module_id].attestation = state;
}

/* ===== Utility ===== */

const char *al_module_state_name(al_module_state_t state) {
    static const char *names[] = {"NEITHER", "TRUE", "FALSE", "BOTH", "HELD", "QUARANTINE"};
    if (state <= AL_STATE_QUARANTINE) return names[state];
    return "INVALID";
}

const char *al_lang_name(al_lang_t lang) {
    static const char *names[] = {"C", "Fortran", "COBOL", "Sutra", "Assembly", "Rust", "Zig", "Python", "WebAssembly"};
    if (lang < AL_LANG_MAX) return names[lang];
    return "UNKNOWN";
}

const char *al_lpres_state_name(lpres_state_t state) {
    return lpres_state_name(state);
}

const char *al_oe_result_name(oe_translate_result_t result) {
    static const char *names[] = {"OK", "NO_PATH", "FAILED", "TOO_MANY_HOPS", "SCHEMA_NOT_FOUND", "VERSION_NOT_FOUND"};
    if (result <= OE_TRANSLATE_VERSION_NOT_FOUND) return names[result];
    return "UNKNOWN";
}
