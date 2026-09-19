/* smart_adapter_integration.c — Smart Adapter + Orbital Elevator + Yantra Fabric Integration
 *
 * Implementation of the three-layer fabric integration.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "smart_adapter_integration.h"
#include "smart_adapter.h"
#include "orbital_elevator.h"
#include "yantra_fabric.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"
#include "../sdk/selfaudit.h"

/* ===== Helper Functions ===== */
static void sai_mem_set(void *dst, int val, uint32_t len) {
    uint8_t *d = dst;
    for (uint32_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

static void sai_mem_copy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = dst; const uint8_t *s = src;
    for (uint32_t i = 0; i < len; i++) d[i] = s[i];
}

static int sai_str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static uint32_t sai_str_len(const char *s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

static void sai_str_copy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ===== Default Capability Guards ===== */

const sai_capability_guard_t SAI_GUARD_MODEL_TO_EMULATED = {
    .guard_id = 1,
    .name = "MODEL_ONLY -> EMULATED",
    .from_capability = YF_MODEL_ONLY,
    .to_capability = YF_EMULATED,
    .min_coverage_ratio = SR_FROM_FLOAT(1.0),
    .min_lpres_state = SMART_STATE_TRUE,
    .min_self_test_passes = 1,
    .max_self_test_failures = 0,
    .required_schema = "smart.adapter.basic",
    .required_schema_version = 1,
    .guard_fn = sai_guard_model_to_emulated,
    .guard_attestation = LPRES_NEITHER,
    .enabled = true
};

const sai_capability_guard_t SAI_GUARD_EMULATED_TO_RTL = {
    .guard_id = 2,
    .name = "EMULATED -> RTL_SIMULATED",
    .from_capability = YF_EMULATED,
    .to_capability = YF_RTL_SIMULATED,
    .min_coverage_ratio = SR_FROM_FLOAT(1.5),
    .min_lpres_state = SMART_STATE_TRUE,
    .min_self_test_passes = 3,
    .max_self_test_failures = 0,
    .required_schema = "smart.adapter.rtl",
    .required_schema_version = 1,
    .guard_fn = sai_guard_emulated_to_rtl,
    .guard_attestation = LPRES_NEITHER,
    .enabled = true
};

const sai_capability_guard_t SAI_GUARD_RTL_TO_SYNTHESIZED = {
    .guard_id = 3,
    .name = "RTL_SIMULATED -> SYNTHESIZED",
    .from_capability = YF_RTL_SIMULATED,
    .to_capability = YF_SYNTHESIZED,
    .min_coverage_ratio = SR_FROM_FLOAT(1.8),
    .min_lpres_state = SMART_STATE_TRUE,
    .min_self_test_passes = 5,
    .max_self_test_failures = 0,
    .required_schema = "smart.adapter.synthesis",
    .required_schema_version = 1,
    .guard_fn = sai_guard_rtl_to_synthesized,
    .guard_attestation = LPRES_NEITHER,
    .enabled = true
};

const sai_capability_guard_t SAI_GUARD_SYNTHESIZED_TO_FPGA = {
    .guard_id = 4,
    .name = "SYNTHESIZED -> FPGA_VERIFIED",
    .from_capability = YF_SYNTHESIZED,
    .to_capability = YF_FPGA_VERIFIED,
    .min_coverage_ratio = SR_FROM_FLOAT(2.0),
    .min_lpres_state = SMART_STATE_TRUE,
    .min_self_test_passes = 10,
    .max_self_test_failures = 0,
    .required_schema = "smart.adapter.fpga",
    .required_schema_version = 1,
    .guard_fn = sai_guard_synthesized_to_fpga,
    .guard_attestation = LPRES_NEITHER,
    .enabled = true
};

const sai_capability_guard_t SAI_GUARD_FPGA_TO_CONNECTED = {
    .guard_id = 5,
    .name = "FPGA_VERIFIED -> DEVICE_CONNECTED",
    .from_capability = YF_FPGA_VERIFIED,
    .to_capability = YF_DEVICE_CONNECTED,
    .min_coverage_ratio = SR_FROM_FLOAT(1.8),
    .min_lpres_state = SMART_STATE_TRUE,
    .min_self_test_passes = 5,
    .max_self_test_failures = 1,
    .required_schema = "smart.adapter.physical",
    .required_schema_version = 1,
    .guard_fn = sai_guard_fpga_to_connected,
    .guard_attestation = LPRES_NEITHER,
    .enabled = true
};

const sai_capability_guard_t SAI_GUARD_CONNECTED_TO_MEASURED = {
    .guard_id = 6,
    .name = "DEVICE_CONNECTED -> DEVICE_MEASURED",
    .from_capability = YF_DEVICE_CONNECTED,
    .to_capability = YF_DEVICE_MEASURED,
    .min_coverage_ratio = SR_FROM_FLOAT(1.8),
    .min_lpres_state = SMART_STATE_TRUE,
    .min_self_test_passes = 10,
    .max_self_test_failures = 1,
    .required_schema = "smart.adapter.measurement",
    .required_schema_version = 1,
    .guard_fn = sai_guard_connected_to_measured,
    .guard_attestation = LPRES_NEITHER,
    .enabled = true
};

const sai_capability_guard_t SAI_GUARD_MEASURED_TO_QUALIFIED = {
    .guard_id = 7,
    .name = "DEVICE_MEASURED -> QUALIFIED",
    .from_capability = YF_DEVICE_MEASURED,
    .to_capability = YF_QUALIFIED,
    .min_coverage_ratio = SR_FROM_FLOAT(2.0),
    .min_lpres_state = SMART_STATE_TRUE,
    .min_self_test_passes = 20,
    .max_self_test_failures = 0,
    .required_schema = "smart.adapter.qualification",
    .required_schema_version = 1,
    .guard_fn = sai_guard_measured_to_qualified,
    .guard_attestation = LPRES_NEITHER,
    .enabled = true
};

const sai_capability_guard_t SAI_GUARD_QUALIFIED_TO_CERTIFIED = {
    .guard_id = 8,
    .name = "QUALIFIED -> CERTIFIED",
    .from_capability = YF_QUALIFIED,
    .to_capability = YF_CERTIFIED,
    .min_coverage_ratio = SR_FROM_FLOAT(2.5),
    .min_lpres_state = SMART_STATE_TRUE,
    .min_self_test_passes = 50,
    .max_self_test_failures = 0,
    .required_schema = "smart.adapter.certification",
    .required_schema_version = 1,
    .guard_fn = sai_guard_qualified_to_certified,
    .guard_attestation = LPRES_NEITHER,
    .enabled = true
};

/* ===== Default Guard Implementations ===== */

bool sai_guard_model_to_emulated(const smart_device_t *sa_device,
                                  const yf_device_t *yf_device,
                                  const oe_elevator_t *elevator) {
    if (!sa_device || !yf_device) return false;
    
    /* Check basic self-test passed */
    if (sa_device->health.self_test_pass_count < 1) return false;
    if (sa_device->health.self_test_fail_count > 0) return false;
    
    /* Check coverage */
    if (SR_CMP(sa_device->health.coverage_ratio, SR_FROM_FLOAT(1.0)) < 0) return false;
    
    /* Check LPRES state */
    if (sa_device->lpres_state != SMART_STATE_TRUE) return false;
    
    return true;
}

bool sai_guard_emulated_to_rtl(const smart_device_t *sa_device,
                                const yf_device_t *yf_device,
                                const oe_elevator_t *elevator) {
    if (!sa_device || !yf_device) return false;
    
    if (sa_device->health.self_test_pass_count < 3) return false;
    if (sa_device->health.self_test_fail_count > 0) return false;
    if (SR_CMP(sa_device->health.coverage_ratio, SR_FROM_FLOAT(1.5)) < 0) return false;
    if (sa_device->lpres_state != SMART_STATE_TRUE) return false;
    
    /* Check RTL-specific self-test */
    if (sa_device->adapter && sa_device->adapter->self_test) {
        int32_t result = sa_device->adapter->self_test(sa_device->adapter, sa_device, SMART_TEST_REGISTERS, NULL);
        if (result < 0) return false;
    }
    
    return true;
}

bool sai_guard_rtl_to_synthesized(const smart_device_t *sa_device,
                                   const yf_device_t *yf_device,
                                   const oe_elevator_t *elevator) {
    if (!sa_device || !yf_device) return false;
    
    if (sa_device->health.self_test_pass_count < 5) return false;
    if (SR_CMP(sa_device->health.coverage_ratio, SR_FROM_FLOAT(1.8)) < 0) return false;
    if (sa_device->lpres_state != SMART_STATE_TRUE) return false;
    
    /* Check synthesis self-test */
    if (sa_device->adapter && sa_device->adapter->self_test) {
        int32_t result = sa_device->adapter->self_test(sa_device->adapter, sa_device, SMART_TEST_DMA, NULL);
        if (result < 0) return false;
    }
    
    return true;
}

bool sai_guard_synthesized_to_fpga(const smart_device_t *sa_device,
                                    const yf_device_t *yf_device,
                                    const oe_elevator_t *elevator) {
    if (!sa_device || !yf_device) return false;
    
    if (sa_device->health.self_test_pass_count < 10) return false;
    if (SR_CMP(sa_device->health.coverage_ratio, SR_FROM_FLOAT(2.0)) < 0) return false;
    if (sa_device->lpres_state != SMART_STATE_TRUE) return false;
    
    /* Check FPGA verification self-test */
    if (sa_device->adapter && sa_device->adapter->self_test) {
        int32_t result = sa_device->adapter->self_test(sa_device->adapter, sa_device, SMART_TEST_STATE_MACHINE, NULL);
        if (result < 0) return false;
    }
    
    return true;
}

bool sai_guard_fpga_to_connected(const smart_device_t *sa_device,
                                  const yf_device_t *yf_device,
                                  const oe_elevator_t *elevator) {
    if (!sa_device || !yf_device) return false;
    
    if (sa_device->health.self_test_pass_count < 5) return false;
    if (sa_device->health.self_test_fail_count > 1) return false;
    if (SR_CMP(sa_device->health.coverage_ratio, SR_FROM_FLOAT(1.8)) < 0) return false;
    if (sa_device->lpres_state != SMART_STATE_TRUE) return false;
    
    /* Check physical device detection */
    if (sa_device->adapter && sa_device->adapter->self_test) {
        int32_t result = sa_device->adapter->self_test(sa_device->adapter, sa_device, SMART_TEST_BASIC, NULL);
        if (result < 0) return false;
    }
    
    return true;
}

bool sai_guard_connected_to_measured(const smart_device_t *sa_device,
                                      const yf_device_t *yf_device,
                                      const oe_elevator_t *elevator) {
    if (!sa_device || !yf_device) return false;
    
    if (sa_device->health.self_test_pass_count < 10) return false;
    if (sa_device->health.self_test_fail_count > 1) return false;
    if (SR_CMP(sa_device->health.coverage_ratio, SR_FROM_FLOAT(1.8)) < 0) return false;
    if (sa_device->lpres_state != SMART_STATE_TRUE) return false;
    
    /* Check measurement self-test */
    if (sa_device->adapter && sa_device->adapter->self_test) {
        int32_t result = sa_device->adapter->self_test(sa_device->adapter, sa_device, SMART_TEST_COVERAGE, NULL);
        if (result < 0) return false;
    }
    
    return true;
}

bool sai_guard_measured_to_qualified(const smart_device_t *sa_device,
                                      const yf_device_t *yf_device,
                                      const oe_elevator_t *elevator) {
    if (!sa_device || !yf_device) return false;
    
    if (sa_device->health.self_test_pass_count < 20) return false;
    if (sa_device->health.self_test_fail_count > 0) return false;
    if (SR_CMP(sa_device->health.coverage_ratio, SR_FROM_FLOAT(2.0)) < 0) return false;
    if (sa_device->lpres_state != SMART_STATE_TRUE) return false;
    
    /* Full self-test */
    if (sa_device->adapter && sa_device->adapter->self_test) {
        int32_t result = sa_device->adapter->self_test(sa_device->adapter, sa_device, SMART_TEST_FULL, NULL);
        if (result < 0) return false;
    }
    
    return true;
}

bool sai_guard_qualified_to_certified(const smart_device_t *sa_device,
                                       const yf_device_t *yf_device,
                                       const oe_elevator_t *elevator) {
    if (!sa_device || !yf_device) return false;
    
    if (sa_device->health.self_test_pass_count < 50) return false;
    if (sa_device->health.self_test_fail_count > 0) return false;
    if (SR_CMP(sa_device->health.coverage_ratio, SR_FROM_FLOAT(2.5)) < 0) return false;
    if (sa_device->lpres_state != SMART_STATE_TRUE) return false;
    
    /* Stress test */
    if (sa_device->adapter && sa_device->adapter->self_test) {
        int32_t result = sa_device->adapter->self_test(sa_device->adapter, sa_device, SMART_TEST_STRESS, NULL);
        if (result < 0) return false;
    }
    
    return true;
}

/* ===== Integration Fabric Initialization ===== */

void sai_init(sai_integration_fabric_t *fabric,
              smart_adapter_registry_t *sa_registry,
              oe_elevator_t *elevator,
              yf_fabric_t *yf_fabric) {
    if (!fabric) return;
    
    sai_mem_set(fabric, 0, sizeof(*fabric));
    fabric->sa_registry = sa_registry;
    fabric->elevator = elevator;
    fabric->yf_fabric = yf_fabric;
    
    fabric->global_attestation = LPRES_NEITHER;
    fabric->global_safety_gate = false;
    
    /* Register default capability guards globally */
    sai_add_capability_guard(fabric, 0xFFFFFFFF, &SAI_GUARD_MODEL_TO_EMULATED);
    sai_add_capability_guard(fabric, 0xFFFFFFFF, &SAI_GUARD_EMULATED_TO_RTL);
    sai_add_capability_guard(fabric, 0xFFFFFFFF, &SAI_GUARD_RTL_TO_SYNTHESIZED);
    sai_add_capability_guard(fabric, 0xFFFFFFFF, &SAI_GUARD_SYNTHESIZED_TO_FPGA);
    sai_add_capability_guard(fabric, 0xFFFFFFFF, &SAI_GUARD_FPGA_TO_CONNECTED);
    sai_add_capability_guard(fabric, 0xFFFFFFFF, &SAI_GUARD_CONNECTED_TO_MEASURED);
    sai_add_capability_guard(fabric, 0xFFFFFFFF, &SAI_GUARD_MEASURED_TO_QUALIFIED);
    sai_add_capability_guard(fabric, 0xFFFFFFFF, &SAI_GUARD_QUALIFIED_TO_CERTIFIED);
}

/* ===== Device Registration ===== */

int32_t sai_register_device(sai_integration_fabric_t *fabric,
                            smart_device_t *sa_device,
                            const char *yf_device_class,
                            yf_capability_state_t initial_capability) {
    if (!fabric || !sa_device || !yf_device_class) return -1;
    if (fabric->num_devices >= SAI_MAX_DEVICES) return -1;
    
    /* Register in Yantra Fabric */
    int32_t yf_idx = yf_register_device(fabric->yf_fabric, sa_device->name, yf_device_class, initial_capability);
    if (yf_idx < 0) return -1;
    
    yf_device_t *yf_device = yf_get_device(fabric->yf_fabric, (uint32_t)yf_idx);
    if (!yf_device) return -1;
    
    /* Create integrated device */
    sai_integrated_device_t *int_dev = &fabric->devices[fabric->num_devices];
    sai_mem_set(int_dev, 0, sizeof(*int_dev));
    
    int_dev->sa_device = sa_device;
    int_dev->sa_device_idx = fabric->num_devices;  /* Use fabric index */
    int_dev->yf_device = yf_device;
    int_dev->yf_device_idx = (uint32_t)yf_idx;
    int_dev->current_capability = initial_capability;
    
    /* Initialize health */
    int_dev->health.coverage_ratio = sa_device->health.coverage_ratio;
    int_dev->health.lpres_state = sa_device->lpres_state;
    int_dev->health.yf_capability = initial_capability;
    int_dev->health.integration_attestation = LPRES_NEITHER;
    int_dev->global_attestation = LPRES_NEITHER;
    int_dev->safety_gate_open = false;
    
    /* Link back-references */
    sa_device->adapter = sa_device->adapter;  /* Already set */
    
    fabric->num_devices++;
    fabric->total_integrations++;
    
    /* Add default schema mappings for this device class */
    if (sai_str_cmp(yf_device_class, "radio") == 0) {
        sai_add_schema_mapping(fabric, "smart.adapter.radio.registers", 1,
                               "yf.radio.registers", 1,
                               "oe.radio.registers", 1, 0, true);
        sai_add_schema_mapping(fabric, "smart.adapter.radio.dma", 1,
                               "yf.radio.dma", 1,
                               "oe.radio.dma", 1, 0, true);
        sai_add_schema_mapping(fabric, "smart.adapter.radio.irq", 1,
                               "yf.radio.irq", 1,
                               "oe.radio.irq", 1, 0, true);
    }
    
    return (int32_t)(fabric->num_devices - 1);
}

int32_t sai_unregister_device(sai_integration_fabric_t *fabric, uint32_t device_idx) {
    if (!fabric || device_idx >= fabric->num_devices) return -1;
    
    sai_integrated_device_t *int_dev = &fabric->devices[device_idx];
    
    /* Remove from Yantra Fabric (would need yf_unregister_device) */
    
    /* Shift remaining devices */
    for (uint32_t i = device_idx; i < fabric->num_devices - 1; i++) {
        fabric->devices[i] = fabric->devices[i + 1];
    }
    fabric->num_devices--;
    sai_mem_set(&fabric->devices[fabric->num_devices], 0, sizeof(sai_integrated_device_t));
    
    return 0;
}

sai_integrated_device_t *sai_get_device(sai_integration_fabric_t *fabric, uint32_t idx) {
    if (!fabric || idx >= fabric->num_devices) return NULL;
    return &fabric->devices[idx];
}

sai_integrated_device_t *sai_get_device_by_sa(sai_integration_fabric_t *fabric, smart_device_t *sa_device) {
    if (!fabric || !sa_device) return NULL;
    for (uint32_t i = 0; i < fabric->num_devices; i++) {
        if (fabric->devices[i].sa_device == sa_device) return &fabric->devices[i];
    }
    return NULL;
}

sai_integrated_device_t *sai_get_device_by_yf_name(sai_integration_fabric_t *fabric, const char *yf_name) {
    if (!fabric || !yf_name) return NULL;
    for (uint32_t i = 0; i < fabric->num_devices; i++) {
        if (sai_str_cmp(fabric->devices[i].yf_device->name, yf_name) == 0) return &fabric->devices[i];
    }
    return NULL;
}

/* ===== Schema Translation ===== */

int32_t sai_add_schema_mapping(sai_integration_fabric_t *fabric,
                               const char *sa_schema, uint16_t sa_version,
                               const char *yf_schema, uint16_t yf_version,
                               const char *oe_schema, uint16_t oe_version,
                               uint32_t oe_adapter_id,
                               bool bidirectional) {
    if (!fabric) return -1;
    
    /* Add to global mappings */
    if (fabric->num_global_mappings < SAI_MAX_SCHEMA_TRANSLATIONS) {
        sai_schema_mapping_t *m = &fabric->global_mappings[fabric->num_global_mappings++];
        sai_mem_set(m, 0, sizeof(*m));
        m->mapping_id = fabric->num_global_mappings;
        sai_str_copy(m->sa_schema, sa_schema, 64);
        sai_str_copy(m->yf_schema, yf_schema, 64);
        sai_str_copy(m->oe_schema, oe_schema, 64);
        m->sa_version = sa_version;
        m->yf_version = yf_version;
        m->oe_version = oe_version;
        m->adapter_id = oe_adapter_id;
        m->bidirectional = bidirectional;
        m->translation_attestation = LPRES_NEITHER;
        m->active = true;
    }
    
    return 0;
}

int32_t sai_translate_sa_to_yf(sai_integration_fabric_t *fabric,
                                uint32_t device_idx,
                                const char *sa_schema, uint16_t sa_version,
                                ev_envelope_t *env) {
    if (!fabric || !env || device_idx >= fabric->num_devices) return -1;
    
    sai_integrated_device_t *int_dev = &fabric->devices[device_idx];
    
    /* Find matching schema mapping */
    for (uint32_t i = 0; i < int_dev->num_schema_mappings; i++) {
        sai_schema_mapping_t *m = &int_dev->schema_mappings[i];
        if (sai_str_cmp(m->sa_schema, sa_schema) == 0 && m->sa_version == sa_version && m->active) {
            /* Use Orbital Elevator to translate */
            oe_translate_result_t result = oe_translate(fabric->elevator, env, m->yf_schema, m->yf_version);
            
            if (result == OE_TRANSLATE_OK) {
                int_dev->health.successful_translations++;
                m->translation_attestation = LPRES_TRUE;
                return 0;
            } else {
                int_dev->health.failed_translations++;
                m->translation_attestation = LPRES_FALSE;
                return -1;
            }
        }
    }
    
    /* Check global mappings */
    for (uint32_t i = 0; i < fabric->num_global_mappings; i++) {
        sai_schema_mapping_t *m = &fabric->global_mappings[i];
        if (sai_str_cmp(m->sa_schema, sa_schema) == 0 && m->sa_version == sa_version && m->active) {
            oe_translate_result_t result = oe_translate(fabric->elevator, env, m->yf_schema, m->yf_version);
            
            if (result == OE_TRANSLATE_OK) {
                int_dev->health.successful_translations++;
                fabric->total_schema_translations++;
                m->translation_attestation = LPRES_TRUE;
                return 0;
            } else {
                int_dev->health.failed_translations++;
                m->translation_attestation = LPRES_FALSE;
                return -1;
            }
        }
    }
    
    return -1;
}

int32_t sai_translate_yf_to_sa(sai_integration_fabric_t *fabric,
                                uint32_t device_idx,
                                const char *yf_schema, uint16_t yf_version,
                                ev_envelope_t *env) {
    if (!fabric || !env || device_idx >= fabric->num_devices) return -1;
    
    sai_integrated_device_t *int_dev = &fabric->devices[device_idx];
    
    /* Find matching schema mapping (bidirectional) */
    for (uint32_t i = 0; i < int_dev->num_schema_mappings; i++) {
        sai_schema_mapping_t *m = &int_dev->schema_mappings[i];
        if (m->bidirectional && sai_str_cmp(m->yf_schema, yf_schema) == 0 && m->yf_version == yf_version && m->active) {
            oe_translate_result_t result = oe_translate(fabric->elevator, env, m->sa_schema, m->sa_version);
            
            if (result == OE_TRANSLATE_OK) {
                int_dev->health.successful_translations++;
                m->translation_attestation = LPRES_TRUE;
                return 0;
            } else {
                int_dev->health.failed_translations++;
                m->translation_attestation = LPRES_FALSE;
                return -1;
            }
        }
    }
    
    /* Check global mappings */
    for (uint32_t i = 0; i < fabric->num_global_mappings; i++) {
        sai_schema_mapping_t *m = &fabric->global_mappings[i];
        if (m->bidirectional && sai_str_cmp(m->yf_schema, yf_schema) == 0 && m->yf_version == yf_version && m->active) {
            oe_translate_result_t result = oe_translate(fabric->elevator, env, m->sa_schema, m->sa_version);
            
            if (result == OE_TRANSLATE_OK) {
                int_dev->health.successful_translations++;
                fabric->total_schema_translations++;
                m->translation_attestation = LPRES_TRUE;
                return 0;
            } else {
                int_dev->health.failed_translations++;
                m->translation_attestation = LPRES_FALSE;
                return -1;
            }
        }
    }
    
    return -1;
}

oe_translate_result_t sai_oe_translate(sai_integration_fabric_t *fabric,
                                        ev_envelope_t *env,
                                        const char *target_schema,
                                        uint16_t target_version) {
    if (!fabric || !fabric->elevator || !env) return OE_TRANSLATE_FAILED;
    return oe_translate(fabric->elevator, env, target_schema, target_version);
}

/* ===== Capability State Management ===== */

int32_t sai_upgrade_capability(sai_integration_fabric_t *fabric,
                               uint32_t device_idx,
                               yf_capability_state_t target_capability) {
    if (!fabric || device_idx >= fabric->num_devices) return -1;
    if (target_capability <= fabric->devices[device_idx].current_capability) return -1;
    
    sai_integrated_device_t *int_dev = &fabric->devices[device_idx];
    yf_capability_state_t from_cap = int_dev->current_capability;
    
    /* Check all capability guards */
    if (!sai_check_capability_guards(fabric, device_idx, from_cap, target_capability)) {
        fabric->total_safety_gate_closures++;
        int_dev->safety_gate_open = false;
        return -1;
    }
    
    /* Perform Yantra capability upgrade */
    if (!yf_set_capability(fabric->yf_fabric, int_dev->yf_device_idx, target_capability)) {
        return -1;
    }
    
    /* Update integrated device */
    int_dev->current_capability = target_capability;
    int_dev->health.yf_capability = target_capability;
    int_dev->safety_gate_open = true;
    fabric->total_capability_transitions++;
    
    /* Update LPRES attestation */
    lpres_state_t attest = sai_attest_integration(fabric, device_idx, 0x1000 | target_capability, NULL, 0);
    int_dev->health.integration_attestation = attest;
    int_dev->global_attestation = lpres_combine(int_dev->global_attestation, attest);
    
    return 0;
}

int32_t sai_downgrade_capability(sai_integration_fabric_t *fabric,
                                 uint32_t device_idx,
                                 yf_capability_state_t target_capability) {
    if (!fabric || device_idx >= fabric->num_devices) return -1;
    if (target_capability >= fabric->devices[device_idx].current_capability) return -1;
    
    sai_integrated_device_t *int_dev = &fabric->devices[device_idx];
    
    /* Downgrades don't require guards but do require attestation */
    if (!yf_set_capability(fabric->yf_fabric, int_dev->yf_device_idx, target_capability)) {
        return -1;
    }
    
    int_dev->current_capability = target_capability;
    int_dev->health.yf_capability = target_capability;
    fabric->total_capability_transitions++;
    
    /* Attest downgrade */
    lpres_state_t attest = sai_attest_integration(fabric, device_idx, 0x2000 | target_capability, NULL, 0);
    int_dev->health.integration_attestation = attest;
    
    return 0;
}

bool sai_check_capability_guards(sai_integration_fabric_t *fabric,
                                  uint32_t device_idx,
                                  yf_capability_state_t from_cap,
                                  yf_capability_state_t to_cap) {
    if (!fabric || device_idx >= fabric->num_devices) return false;
    
    sai_integrated_device_t *int_dev = &fabric->devices[device_idx];
    smart_device_t *sa_device = int_dev->sa_device;
    yf_device_t *yf_device = int_dev->yf_device;
    
    if (!sa_device || !yf_device) return false;
    
    /* Check device-specific guards */
    for (uint32_t i = 0; i < int_dev->num_capability_guards; i++) {
        sai_capability_guard_t *g = &int_dev->capability_guards[i];
        if (g->enabled && g->from_capability == from_cap && g->to_capability == to_cap) {
            if (g->guard_fn) {
                if (!g->guard_fn(sa_device, yf_device, fabric->elevator)) {
                    g->guard_attestation = LPRES_FALSE;
                    return false;
                }
                g->guard_attestation = LPRES_TRUE;
            }
            
            /* Check coverage */
            if (SR_CMP(sa_device->health.coverage_ratio, g->min_coverage_ratio) < 0) return false;
            
            /* Check LPRES state */
            if (sa_device->lpres_state < g->min_lpres_state) return false;
            
            /* Check self-test passes */
            if (sa_device->health.self_test_pass_count < g->min_self_test_passes) return false;
            if (sa_device->health.self_test_fail_count > g->max_self_test_failures) return false;
            
            /* Check schema translatability */
            if (g->required_schema && fabric->elevator) {
                oe_schema_t *schema = oe_get_schema(fabric->elevator, g->required_schema);
                if (!schema) return false;
                bool version_ok = false;
                for (uint32_t v = 0; v < schema->num_versions; v++) {
                    if (schema->versions[v].version >= g->required_schema_version && !schema->versions[v].deprecated) {
                        version_ok = true;
                        break;
                    }
                }
                if (!version_ok) return false;
            }
        }
    }
    
    /* Check global guards */
    for (uint32_t i = 0; i < fabric->num_global_guards; i++) {
        sai_capability_guard_t *g = &fabric->global_guards[i];
        if (g->enabled && g->from_capability == from_cap && g->to_capability == to_cap) {
            if (g->guard_fn) {
                if (!g->guard_fn(sa_device, yf_device, fabric->elevator)) {
                    g->guard_attestation = LPRES_FALSE;
                    return false;
                }
                g->guard_attestation = LPRES_TRUE;
            }
            
            if (SR_CMP(sa_device->health.coverage_ratio, g->min_coverage_ratio) < 0) return false;
            if (sa_device->lpres_state < g->min_lpres_state) return false;
            if (sa_device->health.self_test_pass_count < g->min_self_test_passes) return false;
            if (sa_device->health.self_test_fail_count > g->max_self_test_failures) return false;
        }
    }
    
    return true;
}

int32_t sai_add_capability_guard(sai_integration_fabric_t *fabric,
                                  uint32_t device_idx,
                                  const sai_capability_guard_t *guard) {
    if (!fabric || !guard) return -1;
    
    if (device_idx == 0xFFFFFFFF) {
        /* Global guard */
        if (fabric->num_global_guards >= SAI_MAX_CAPABILITY_GUARDS) return -1;
        fabric->global_guards[fabric->num_global_guards++] = *guard;
    } else if (device_idx < fabric->num_devices) {
        /* Device-specific guard */
        sai_integrated_device_t *int_dev = &fabric->devices[device_idx];
        if (int_dev->num_capability_guards >= SAI_MAX_CAPABILITY_GUARDS) return -1;
        int_dev->capability_guards[int_dev->num_capability_guards++] = *guard;
    } else {
        return -1;
    }
    
    return 0;
}

/* ===== Digital Twin Synchronization ===== */

int32_t sai_sync_twin(sai_integration_fabric_t *fabric, uint32_t device_idx) {
    if (!fabric || device_idx >= fabric->num_devices) return -1;
    
    sai_integrated_device_t *int_dev = &fabric->devices[device_idx];
    smart_device_t *sa_device = int_dev->sa_device;
    yf_device_t *yf_device = int_dev->yf_device;
    
    if (!sa_device || !yf_device) return -1;
    
    /* Run virtual simulation step */
    if (sa_device->adapter && sa_device->adapter->virtual_step) {
        sa_device->adapter->virtual_step(sa_device->adapter, sa_device, 1000);
    }
    
    /* Sync virtual state to digital twin */
    int32_t result = sai_run_twin_sync_hooks(fabric, device_idx);
    
    if (result >= 0) {
        int_dev->health.twin_sync_count++;
        fabric->total_twin_syncs++;
        
        /* Check for contradictions */
        uint32_t contradictions = yf_twin_check_contradictions(fabric->yf_fabric, int_dev->yf_device_idx);
        int_dev->health.twin_contradictions += contradictions;
        fabric->total_contradictions_detected += contradictions;
        
        /* Update health */
        int_dev->health.coverage_ratio = sa_device->health.coverage_ratio;
        int_dev->health.lpres_state = sa_device->lpres_state;
        int_dev->health.last_sync_tick = 0;  /* Current tick */
    }
    
    return result;
}

int32_t sai_add_twin_sync_hook(sai_integration_fabric_t *fabric,
                                uint32_t device_idx,
                                const sai_twin_sync_hook_t *hook) {
    if (!fabric || !hook || device_idx >= fabric->num_devices) return -1;
    
    sai_integrated_device_t *int_dev = &fabric->devices[device_idx];
    if (int_dev->num_twin_sync_hooks >= SAI_MAX_TWIN_SYNC_HOOKS) return -1;
    
    int_dev->twin_sync_hooks[int_dev->num_twin_sync_hooks++] = *hook;
    return 0;
}

int32_t sai_run_twin_sync_hooks(sai_integration_fabric_t *fabric, uint32_t device_idx) {
    if (!fabric || device_idx >= fabric->num_devices) return -1;
    
    sai_integrated_device_t *int_dev = &fabric->devices[device_idx];
    smart_device_t *sa_device = int_dev->sa_device;
    yf_device_t *yf_device = int_dev->yf_device;
    
    if (!sa_device || !yf_device) return -1;
    
    int32_t last_result = 0;
    for (uint32_t i = 0; i < int_dev->num_twin_sync_hooks; i++) {
        sai_twin_sync_hook_t *h = &int_dev->twin_sync_hooks[i];
        if (h->enabled && h->sync_fn) {
            last_result = h->sync_fn(sa_device, yf_device, fabric->elevator);
            h->last_sync_tick = 0;  /* Current tick */
            h->sync_attestation = (last_result >= 0) ? LPRES_TRUE : LPRES_FALSE;
        }
    }
    
    return last_result;
}

/* ===== Health & Attestation ===== */

int32_t sai_check_health(sai_integration_fabric_t *fabric,
                         uint32_t device_idx,
                         sai_integrated_device_t *health_out) {
    if (!fabric || device_idx >= fabric->num_devices || !health_out) return -1;
    
    sai_integrated_device_t *int_dev = &fabric->devices[device_idx];
    smart_device_t *sa_device = int_dev->sa_device;
    yf_device_t *yf_device = int_dev->yf_device;
    
    if (!sa_device || !yf_device) return -1;
    
    /* Update health from components */
    int_dev->health.coverage_ratio = sa_device->health.coverage_ratio;
    int_dev->health.lpres_state = sa_device->lpres_state;
    int_dev->health.yf_capability = yf_device->capability;
    
    /* Run self-test on Smart Adapter */
    if (sa_device->adapter && sa_device->adapter->self_test) {
        sa_device->adapter->self_test(sa_device->adapter, sa_device, SMART_TEST_BASIC, NULL);
    }
    
    /* Run health check on Smart Adapter */
    if (sa_device->adapter && sa_device->adapter->health_check) {
        sa_device->adapter->health_check(sa_device->adapter, sa_device, &sa_device->health);
    }
    
    /* Check Yantra device readiness */
    bool yf_ready = yf_device_ready_for(fabric->yf_fabric, int_dev->yf_device_idx, int_dev->current_capability);
    
    /* Compute integration attestation */
    lpres_state_t sa_attest = smart_lpres_from_state(sa_device->lpres_state);
    lpres_state_t yf_attest = (yf_ready) ? LPRES_TRUE : LPRES_FALSE;
    lpres_state_t coverage_attest = (SR_CMP(sa_device->health.coverage_ratio, SR_FROM_FLOAT(1.8)) >= 0) ? LPRES_TRUE : LPRES_FALSE;
    
    int_dev->health.integration_attestation = lpres_combine(lpres_combine(sa_attest, yf_attest), coverage_attest);
    int_dev->global_attestation = int_dev->health.integration_attestation;
    
    /* Safety gate */
    int_dev->safety_gate_open = (int_dev->health.integration_attestation == LPRES_TRUE);
    
    sai_mem_copy(health_out, int_dev, sizeof(sai_integrated_device_t));
    return 0;
}

int32_t sai_check_global_health(sai_integration_fabric_t *fabric) {
    if (!fabric) return -1;
    
    int32_t unhealthy = 0;
    for (uint32_t i = 0; i < fabric->num_devices; i++) {
        sai_integrated_device_t health;
        if (sai_check_health(fabric, i, &health) < 0 || health.safety_gate_open == false) {
            unhealthy++;
        }
    }
    
    fabric->global_safety_gate = (unhealthy == 0);
    fabric->global_attestation = fabric->global_safety_gate ? LPRES_TRUE : LPRES_BOTH;
    
    return unhealthy == 0 ? 0 : -1;
}

lpres_state_t sai_attest_integration(sai_integration_fabric_t *fabric,
                                      uint32_t device_idx,
                                      uint32_t op_id, void *args, int32_t result) {
    if (!fabric || device_idx >= fabric->num_devices) return LPRES_NEITHER;
    
    sai_integrated_device_t *int_dev = &fabric->devices[device_idx];
    smart_device_t *sa_device = int_dev->sa_device;
    
    if (!sa_device) return LPRES_NEITHER;
    
    lpres_state_t result_att = (result >= 0) ? LPRES_TRUE : LPRES_FALSE;
    lpres_state_t sa_att = smart_lpres_from_state(sa_device->lpres_state);
    lpres_state_t coverage_att = (SR_CMP(sa_device->health.coverage_ratio, SR_FROM_FLOAT(1.8)) >= 0) ? LPRES_TRUE : LPRES_FALSE;
    lpres_state_t yf_att = (int_dev->health.yf_capability >= int_dev->current_capability) ? LPRES_TRUE : LPRES_FALSE;
    
    lpres_state_t combined = lpres_combine(result_att, sa_att);
    combined = lpres_combine(combined, coverage_att);
    combined = lpres_combine(combined, yf_att);
    
    int_dev->health.integration_attestation = combined;
    int_dev->global_attestation = lpres_combine(int_dev->global_attestation, combined);
    fabric->global_attestation = lpres_combine(fabric->global_attestation, combined);
    
    return combined;
}

bool sai_global_safety_gate(sai_integration_fabric_t *fabric) {
    return fabric ? fabric->global_safety_gate : false;
}

bool sai_device_safety_gate(sai_integration_fabric_t *fabric, uint32_t device_idx) {
    if (!fabric || device_idx >= fabric->num_devices) return false;
    return fabric->devices[device_idx].safety_gate_open;
}

/* ===== Paraconsistent State ===== */

lpres_state_t sai_get_lpres_state(sai_integration_fabric_t *fabric, uint32_t device_idx) {
    if (!fabric || device_idx >= fabric->num_devices) return LPRES_NEITHER;
    return fabric->devices[device_idx].global_attestation;
}

void sai_set_lpres_state(sai_integration_fabric_t *fabric, uint32_t device_idx, lpres_state_t state) {
    if (!fabric || device_idx >= fabric->num_devices) return;
    fabric->devices[device_idx].global_attestation = state;
    fabric->devices[device_idx].health.integration_attestation = state;
}

/* ===== Event Emission ===== */

int32_t sai_emit_yf_event(sai_integration_fabric_t *fabric,
                          uint32_t device_idx,
                          uint32_t yf_event_idx,
                          const smart_register_t *sa_reg, uint32_t sa_value) {
    if (!fabric || device_idx >= fabric->num_devices) return -1;
    
    sai_integrated_device_t *int_dev = &fabric->devices[device_idx];
    yf_device_t *yf_device = int_dev->yf_device;
    
    if (!yf_device || yf_event_idx >= yf_device->num_events) return -1;
    
    /* Get Yantra event schema */
    char schema[EV_SCHEMA_LEN];
    if (!yf_device_emit_event(fabric->yf_fabric, int_dev->yf_device_idx, yf_event_idx, schema, EV_SCHEMA_LEN)) {
        return -1;
    }
    
    /* Create event envelope */
    ev_envelope_t env;
    sai_mem_set(&env, 0, sizeof(env));
    sai_str_copy(env.schema, schema, EV_SCHEMA_LEN);
    env.payload[0] = sa_value;
    env.payload_len = 4;
    
    /* Translate to Orbital Elevator canonical schema */
    sai_translate_sa_to_yf(fabric, device_idx, "smart.adapter.register", 1, &env);
    
    return 0;
}

int32_t sai_emit_sa_register(sai_integration_fabric_t *fabric,
                             uint32_t device_idx,
                             const char *sa_reg_name,
                             uint32_t value, uint32_t width) {
    if (!fabric || device_idx >= fabric->num_devices || !sa_reg_name) return -1;
    
    sai_integrated_device_t *int_dev = &fabric->devices[device_idx];
    smart_device_t *sa_device = int_dev->sa_device;
    
    if (!sa_device) return -1;
    
    /* Write to Smart Adapter register */
    return smart_reg_write(sa_device, 0, value, width);  /* Simplified - would find reg offset */
}

/* ===== Utility ===== */

const char *sai_capability_name(yf_capability_state_t cap) {
    return yf_capability_name(cap);
}

const char *sai_lpres_state_name(smart_device_state_t state) {
    return smart_state_name(state);
}

const char *sai_oe_result_name(oe_translate_result_t result) {
    static const char *names[] = {
        "OK", "NO_PATH", "FAILED", "TOO_MANY_HOPS",
        "SCHEMA_NOT_FOUND", "VERSION_NOT_FOUND"
    };
    if (result <= OE_TRANSLATE_VERSION_NOT_FOUND) return names[result];
    return "UNKNOWN";
}

smart_device_state_t sai_capability_to_min_lpres(yf_capability_state_t cap) {
    switch (cap) {
        case YF_MODEL_ONLY: return SMART_STATE_NEITHER;
        case YF_EMULATED: return SMART_STATE_TRUE;
        case YF_RTL_SIMULATED: return SMART_STATE_TRUE;
        case YF_SYNTHESIZED: return SMART_STATE_TRUE;
        case YF_FPGA_VERIFIED: return SMART_STATE_TRUE;
        case YF_DEVICE_CONNECTED: return SMART_STATE_TRUE;
        case YF_DEVICE_MEASURED: return SMART_STATE_TRUE;
        case YF_QUALIFIED: return SMART_STATE_TRUE;
        case YF_CERTIFIED: return SMART_STATE_TRUE;
        default: return SMART_STATE_NEITHER;
    }
}

surplus_real_t sai_capability_to_min_coverage(yf_capability_state_t cap) {
    switch (cap) {
        case YF_MODEL_ONLY: return SR_FROM_FLOAT(1.0);
        case YF_EMULATED: return SR_FROM_FLOAT(1.0);
        case YF_RTL_SIMULATED: return SR_FROM_FLOAT(1.5);
        case YF_SYNTHESIZED: return SR_FROM_FLOAT(1.8);
        case YF_FPGA_VERIFIED: return SR_FROM_FLOAT(2.0);
        case YF_DEVICE_CONNECTED: return SR_FROM_FLOAT(1.8);
        case YF_DEVICE_MEASURED: return SR_FROM_FLOAT(1.8);
        case YF_QUALIFIED: return SR_FROM_FLOAT(2.0);
        case YF_CERTIFIED: return SR_FROM_FLOAT(2.5);
        default: return SR_FROM_FLOAT(1.0);
    }
}

uint32_t sai_capability_to_min_self_tests(yf_capability_state_t cap) {
    switch (cap) {
        case YF_MODEL_ONLY: return 0;
        case YF_EMULATED: return 1;
        case YF_RTL_SIMULATED: return 3;
        case YF_SYNTHESIZED: return 5;
        case YF_FPGA_VERIFIED: return 10;
        case YF_DEVICE_CONNECTED: return 5;
        case YF_DEVICE_MEASURED: return 10;
        case YF_QUALIFIED: return 20;
        case YF_CERTIFIED: return 50;
        default: return 0;
    }
}
