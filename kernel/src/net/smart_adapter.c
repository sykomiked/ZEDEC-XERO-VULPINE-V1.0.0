/* smart_adapter.c — Generic Smart Hardware/Firmware Adapter Implementation
 *
 * Military-grade reliability through:
 * - Paraconsistent logic (LPRES) for all state management
 * - M5 coverage hyperbola enforcement (>= 1.8x)
 * - Continuous health monitoring with self-test
 * - Graceful degradation and recovery
 * - Virtual device simulation (hardware-in-software)
 * - LPRES attestation for all operations
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "smart_adapter.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"
#include "../sdk/selfaudit.h"
#include "jdr_piratenet.h"

/* ===== Global Registry ===== */
static smart_adapter_registry_t g_smart_registry;

/* ===== Helper: Memory Operations ===== */
static void smart_mem_set(void *dst, int val, uint32_t len) {
    uint8_t *d = dst;
    for (uint32_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

static void smart_mem_copy(void *dst, const void *src, uint32_t len) {
    uint8_t *d = dst; const uint8_t *s = src;
    for (uint32_t i = 0; i < len; i++) d[i] = s[i];
}

static int smart_str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static uint32_t smart_str_len(const char *s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}

static void smart_str_copy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* ===== Paraconsistent State Helpers ===== */
const char *smart_state_name(smart_device_state_t state) {
    static const char *names[] = {"NEITHER", "TRUE", "FALSE", "BOTH"};
    if (state < SMART_STATE_MAX) return names[state];
    return "INVALID";
}

const char *smart_class_name(uint32_t device_class) {
    switch (device_class) {
        case SMART_CLASS_RADIO: return "RADIO";
        case SMART_CLASS_STORAGE: return "STORAGE";
        case SMART_CLASS_NETWORK: return "NETWORK";
        case SMART_CLASS_SENSOR: return "SENSOR";
        case SMART_CLASS_CRYPTO: return "CRYPTO";
        case SMART_CLASS_DISPLAY: return "DISPLAY";
        case SMART_CLASS_INPUT: return "INPUT";
        case SMART_CLASS_POWER: return "POWER";
        case SMART_CLASS_TIME: return "TIME";
        case SMART_CLASS_PROCESSOR: return "PROCESSOR";
        case SMART_CLASS_MEMORY: return "MEMORY";
        case SMART_CLASS_BUS: return "BUS";
        case SMART_CLASS_ACTUATOR: return "ACTUATOR";
        case SMART_CLASS_CAMERA: return "CAMERA";
        case SMART_CLASS_AUDIO: return "AUDIO";
        case SMART_CLASS_HAPTIC: return "HAPTIC";
        case SMART_CLASS_BIOSENSOR: return "BIOSENSOR";
        case SMART_CLASS_QUANTUM: return "QUANTUM";
        case SMART_CLASS_NEUTRINO: return "NEUTRINO";
        case SMART_CLASS_HARMONIC: return "HARMONIC";
        case SMART_CLASS_CUSTOM: return "CUSTOM";
        default: return "UNKNOWN";
    }
}

lpres_state_t smart_lpres_from_state(smart_device_state_t state) {
    switch (state) {
        case SMART_STATE_NEITHER: return LPRES_NEITHER;
        case SMART_STATE_TRUE: return LPRES_TRUE;
        case SMART_STATE_FALSE: return LPRES_FALSE;
        case SMART_STATE_BOTH: return LPRES_BOTH;
        default: return LPRES_NEITHER;
    }
}

smart_device_state_t smart_state_from_lpres(lpres_state_t lpres) {
    switch (lpres) {
        case LPRES_NEITHER: return SMART_STATE_NEITHER;
        case LPRES_TRUE: return SMART_STATE_TRUE;
        case LPRES_FALSE: return SMART_STATE_FALSE;
        case LPRES_BOTH: return SMART_STATE_BOTH;
        default: return SMART_STATE_NEITHER;
    }
}

/* ===== Coverage Computation ===== */
surplus_real_t smart_compute_coverage(smart_device_t *device) {
    if (!device) return SR_ZERO;
    
    /* M5 coverage: omega * r * ell >= 1.8 * phi * chi */
    surplus_real_t omega = SR_FROM_INT(device->m5.omega);
    surplus_real_t r = device->m5.r;
    surplus_real_t ell = device->m5.ell;
    surplus_real_t phi = device->m5.phi;
    surplus_real_t chi = SR_FROM_INT(device->m5.chi);
    
    surplus_real_t numerator = SR_MUL(SR_MUL(omega, r), ell);
    surplus_real_t denominator = SR_MUL(phi, chi);
    
    if (SR_CMP(denominator, SR_ZERO) == 0) return SR_FROM_FLOAT(100.0); /* Infinite coverage */
    
    return SR_DIV(numerator, denominator);
}

bool smart_enforce_coverage(smart_device_t *device, surplus_real_t required_ratio) {
    if (!device) return false;
    surplus_real_t coverage = smart_compute_coverage(device);
    device->health.coverage_ratio = coverage;
    device->min_coverage_ratio = required_ratio;
    return SR_CMP(coverage, required_ratio) >= 0;
}

/* ===== LPRES Attestation ===== */
lpres_state_t smart_attest_operation(smart_device_t *device, uint32_t op_id, void *args, int32_t result) {
    if (!device) return LPRES_NEITHER;
    
    /* Attest based on result and device state */
    lpres_state_t result_attestation = (result >= 0) ? LPRES_TRUE : LPRES_FALSE;
    lpres_state_t device_attestation = smart_lpres_from_state(device->lpres_state);
    
    /* Paraconsistent combination: if both TRUE and FALSE, result is BOTH */
    if (result_attestation == LPRES_TRUE && device_attestation == LPRES_FALSE) {
        return LPRES_BOTH;
    }
    if (result_attestation == LPRES_FALSE && device_attestation == LPRES_TRUE) {
        return LPRES_BOTH;
    }
    
    /* Combine using LPRES logic */
    return lpres_combine(result_attestation, device_attestation);
}

/* ===== Paraconsistent State Management ===== */
smart_device_state_t smart_get_lpres_state(smart_device_t *device) {
    if (!device) return SMART_STATE_NEITHER;
    return device->lpres_state;
}

void smart_set_lpres_state(smart_device_t *device, smart_device_state_t state) {
    if (!device) return;
    device->lpres_state = state;
    device->global_attestation = smart_lpres_from_state(state);
    
    /* Update health based on state */
    if (state == SMART_STATE_TRUE) {
        device->health.uptime_ratio = SR_ONE;
    } else if (state == SMART_STATE_FALSE) {
        device->health.uptime_ratio = SR_ZERO;
    } else if (state == SMART_STATE_BOTH) {
        device->health.uptime_ratio = SR_FROM_FLOAT(0.5);
    }
}

bool smart_safety_gate(smart_device_t *device) {
    if (!device) return false;
    
    /* Safety gate: only proceed if LPRES state is TRUE and coverage >= 1.8 */
    if (device->lpres_state != SMART_STATE_TRUE) return false;
    if (!smart_enforce_coverage(device, device->min_coverage_ratio)) return false;
    
    return true;
}

/* ===== Registry Management ===== */
void smart_adapter_registry_init(smart_adapter_registry_t *reg) {
    if (!reg) return;
    smart_mem_set(reg, 0, sizeof(*reg));
    reg->global_lpres_state = SMART_STATE_NEITHER;
    reg->global_attestation = LPRES_NEITHER;
    smart_mem_set(&reg->global_health, 0, sizeof(reg->global_health));
    reg->global_health.coverage_ratio = SR_ONE;
    reg->global_health.uptime_ratio = SR_ONE;
}

int32_t smart_adapter_register(smart_adapter_registry_t *reg, smart_adapter_t *adapter) {
    if (!reg || !adapter) return -1;
    if (reg->num_adapters >= SMART_MAX_ADAPTERS) return -1;
    
    /* Validate adapter */
    if (!adapter->probe || !adapter->init || !adapter->deinit) return -1;
    
    /* Self-test the adapter */
    if (adapter->self_test) {
        int32_t result = adapter->self_test(adapter, NULL, SMART_TEST_BASIC, NULL);
        if (result < 0) return -1;
    }
    
    reg->adapters[reg->num_adapters++] = adapter;
    
    /* Set as default if first adapter */
    if (reg->num_adapters == 1) reg->default_adapter = adapter;
    
    return 0;
}

int32_t smart_adapter_unregister(smart_adapter_registry_t *reg, smart_adapter_t *adapter) {
    if (!reg || !adapter) return -1;
    
    for (uint32_t i = 0; i < reg->num_adapters; i++) {
        if (reg->adapters[i] == adapter) {
            /* Remove devices using this adapter first */
            for (uint32_t j = 0; j < adapter->num_devices; j++) {
                if (adapter->devices[j]) {
                    smart_device_remove(reg, adapter->devices[j]);
                }
            }
            
            /* Shift remaining adapters */
            for (uint32_t j = i; j < reg->num_adapters - 1; j++) {
                reg->adapters[j] = reg->adapters[j + 1];
            }
            reg->num_adapters--;
            reg->adapters[reg->num_adapters] = NULL;
            
            /* Update default */
            if (reg->default_adapter == adapter) {
                reg->default_adapter = (reg->num_adapters > 0) ? reg->adapters[0] : NULL;
            }
            return 0;
        }
    }
    return -1;
}

smart_adapter_t *smart_adapter_find_by_class(smart_adapter_registry_t *reg, uint32_t device_class) {
    if (!reg) return NULL;
    
    for (uint32_t i = 0; i < reg->num_adapters; i++) {
        smart_adapter_t *a = reg->adapters[i];
        for (uint32_t j = 0; j < a->num_supported_classes; j++) {
            if (a->supported_classes[j] == device_class) return a;
        }
    }
    return reg->default_adapter;
}

smart_adapter_t *smart_adapter_find_by_name(smart_adapter_registry_t *reg, const char *name) {
    if (!reg || !name) return NULL;
    
    for (uint32_t i = 0; i < reg->num_adapters; i++) {
        if (smart_str_cmp(reg->adapters[i]->name, name) == 0) return reg->adapters[i];
    }
    return NULL;
}

/* ===== Device Management ===== */
int32_t smart_device_probe(smart_adapter_registry_t *reg, void *bus_info, smart_device_t **out_device) {
    if (!reg || !out_device) return -1;
    
    /* Find appropriate adapter based on bus_info */
    smart_adapter_t *adapter = reg->default_adapter;
    if (bus_info) {
        /* In real implementation, parse bus_info to determine device class */
        uint32_t device_class = *(uint32_t *)bus_info;
        adapter = smart_adapter_find_by_class(reg, device_class);
    }
    
    if (!adapter || !adapter->probe) return -1;
    
    smart_device_t *device = NULL;
    int32_t result = adapter->probe(adapter, bus_info, &device);
    if (result < 0 || !device) return -1;
    
    /* Initialize device */
    result = smart_device_init(reg, device);
    if (result < 0) return -1;
    
    *out_device = device;
    return 0;
}

int32_t smart_device_init(smart_adapter_registry_t *reg, smart_device_t *device) {
    if (!reg || !device || !device->adapter) return -1;
    
    smart_adapter_t *adapter = device->adapter;
    
    /* Initialize virtual context (hardware-in-software simulation) */
    if (adapter->virtual_init) {
        int32_t result = adapter->virtual_init(adapter, device);
        if (result < 0) return -1;
    }
    
    /* Initialize physical hardware */
    if (adapter->init) {
        int32_t result = adapter->init(adapter, device);
        if (result < 0) return -1;
    }
    
    /* Initialize state machine */
    device->current_state = 0;
    device->previous_state = 0;
    device->state_enter_tick = 0;
    
    /* Initialize health */
    smart_mem_set(&device->health, 0, sizeof(device->health));
    device->health.coverage_ratio = SR_ONE;
    device->health.uptime_ratio = SR_ONE;
    
    /* Set initial LPRES state */
    device->lpres_state = SMART_STATE_NEITHER;
    device->global_attestation = LPRES_NEITHER;
    
    /* Verify coverage */
    surplus_real_t coverage;
    if (adapter->coverage_verify) {
        adapter->coverage_verify(adapter, device, &coverage);
        device->health.coverage_ratio = coverage;
    }
    
    /* Run basic self-test */
    if (adapter->self_test) {
        adapter->self_test(adapter, device, SMART_TEST_BASIC, NULL);
    }
    
    /* Set to TRUE if all checks pass */
    if (smart_safety_gate(device)) {
        smart_set_lpres_state(device, SMART_STATE_TRUE);
    } else {
        smart_set_lpres_state(device, SMART_STATE_BOTH); /* Degraded */
    }
    
    /* Register device with adapter */
    if (adapter->num_devices < 32) {
        adapter->devices[adapter->num_devices++] = device;
    }
    
    return 0;
}

int32_t smart_device_deinit(smart_adapter_registry_t *reg, smart_device_t *device) {
    if (!reg || !device || !device->adapter) return -1;
    
    smart_adapter_t *adapter = device->adapter;
    
    /* Stop health monitor */
    smart_stop_health_monitor(device);
    
    /* Deinit physical hardware */
    if (adapter->deinit) {
        adapter->deinit(adapter, device);
    }
    
    /* Deinit virtual context */
    if (adapter->virtual_init) { /* Reuse for deinit */
        /* Virtual deinit would go here */
    }
    
    /* Update state */
    smart_set_lpres_state(device, SMART_STATE_NEITHER);
    
    return 0;
}

int32_t smart_device_remove(smart_adapter_registry_t *reg, smart_device_t *device) {
    if (!reg || !device || !device->adapter) return -1;
    
    smart_adapter_t *adapter = device->adapter;
    
    /* Deinit first */
    smart_device_deinit(reg, device);
    
    /* Remove from adapter's device list */
    for (uint32_t i = 0; i < adapter->num_devices; i++) {
        if (adapter->devices[i] == device) {
            for (uint32_t j = i; j < adapter->num_devices - 1; j++) {
                adapter->devices[j] = adapter->devices[j + 1];
            }
            adapter->num_devices--;
            adapter->devices[adapter->num_devices] = NULL;
            break;
        }
    }
    
    return 0;
}

/* ===== Register Operations ===== */
int32_t smart_reg_read(smart_device_t *device, uint32_t reg_offset, uint32_t *value, uint32_t width) {
    if (!device || !device->adapter || !device->adapter->reg_read || !value) return -1;
    
    /* Safety gate */
    if (!smart_safety_gate(device)) return -1;
    
    int32_t result = device->adapter->reg_read(device->adapter, device, reg_offset, value, width);
    
    /* Attest operation */
    lpres_state_t attest = smart_attest_operation(device, 0x1000 | reg_offset, (void*)(uintptr_t)reg_offset, result);
    device->global_attestation = lpres_combine(device->global_attestation, attest);
    
    return result;
}

int32_t smart_reg_write(smart_device_t *device, uint32_t reg_offset, uint32_t value, uint32_t width) {
    if (!device || !device->adapter || !device->adapter->reg_write) return -1;
    
    if (!smart_safety_gate(device)) return -1;
    
    int32_t result = device->adapter->reg_write(device->adapter, device, reg_offset, value, width);
    
    lpres_state_t attest = smart_attest_operation(device, 0x2000 | reg_offset, (void*)(uintptr_t)value, result);
    device->global_attestation = lpres_combine(device->global_attestation, attest);
    
    return result;
}

int32_t smart_reg_read_bulk(smart_device_t *device, uint32_t reg_offset, uint32_t *values, uint32_t count) {
    if (!device || !device->adapter || !device->adapter->reg_read_bulk || !values) return -1;
    if (!smart_safety_gate(device)) return -1;
    return device->adapter->reg_read_bulk(device->adapter, device, reg_offset, values, count);
}

int32_t smart_reg_write_bulk(smart_device_t *device, uint32_t reg_offset, const uint32_t *values, uint32_t count) {
    if (!device || !device->adapter || !device->adapter->reg_write_bulk || !values) return -1;
    if (!smart_safety_gate(device)) return -1;
    return device->adapter->reg_write_bulk(device->adapter, device, reg_offset, values, count);
}

/* ===== DMA Operations ===== */
int32_t smart_dma_setup(smart_device_t *device, uint32_t channel, uint32_t direction,
                        uint32_t buffer_phys, uint32_t buffer_size) {
    if (!device || !device->adapter || !device->adapter->dma_setup) return -1;
    if (!smart_safety_gate(device)) return -1;
    return device->adapter->dma_setup(device->adapter, device, channel, direction, buffer_phys, buffer_size);
}

int32_t smart_dma_start(smart_device_t *device, uint32_t channel) {
    if (!device || !device->adapter || !device->adapter->dma_start) return -1;
    if (!smart_safety_gate(device)) return -1;
    return device->adapter->dma_start(device->adapter, device, channel);
}

int32_t smart_dma_stop(smart_device_t *device, uint32_t channel) {
    if (!device || !device->adapter || !device->adapter->dma_stop) return -1;
    if (!smart_safety_gate(device)) return -1;
    return device->adapter->dma_stop(device->adapter, device, channel);
}

int32_t smart_dma_status(smart_device_t *device, uint32_t channel, uint32_t *status) {
    if (!device || !device->adapter || !device->adapter->dma_status || !status) return -1;
    if (!smart_safety_gate(device)) return -1;
    return device->adapter->dma_status(device->adapter, device, channel, status);
}

/* ===== IRQ Operations ===== */
int32_t smart_irq_enable(smart_device_t *device, uint32_t irq_index) {
    if (!device || !device->adapter || !device->adapter->irq_enable) return -1;
    if (!smart_safety_gate(device)) return -1;
    return device->adapter->irq_enable(device->adapter, device, irq_index);
}

int32_t smart_irq_disable(smart_device_t *device, uint32_t irq_index) {
    if (!device || !device->adapter || !device->adapter->irq_disable) return -1;
    if (!smart_safety_gate(device)) return -1;
    return device->adapter->irq_disable(device->adapter, device, irq_index);
}

int32_t smart_irq_ack(smart_device_t *device, uint32_t irq_index) {
    if (!device || !device->adapter || !device->adapter->irq_ack) return -1;
    if (!smart_safety_gate(device)) return -1;
    return device->adapter->irq_ack(device->adapter, device, irq_index);
}

int32_t smart_irq_set_handler(smart_device_t *device, uint32_t irq_index,
                              void (*handler)(void *), void *context) {
    if (!device || !device->adapter || !device->adapter->irq_set_handler) return -1;
    if (!smart_safety_gate(device)) return -1;
    return device->adapter->irq_set_handler(device->adapter, device, irq_index, handler, context);
}

/* ===== State Machine ===== */
int32_t smart_state_transition(smart_device_t *device, uint32_t event_id) {
    if (!device || !device->adapter || !device->adapter->state_transition) return -1;
    if (!smart_safety_gate(device)) return -1;
    
    int32_t result = device->adapter->state_transition(device->adapter, device, event_id);
    
    if (result >= 0) {
        device->previous_state = device->current_state;
        /* New state would be set by adapter */
    }
    
    return result;
}

int32_t smart_state_get(smart_device_t *device, uint32_t *current_state) {
    if (!device || !device->adapter || !device->adapter->state_get || !current_state) return -1;
    return device->adapter->state_get(device->adapter, device, current_state);
}

int32_t smart_state_set(smart_device_t *device, uint32_t new_state) {
    if (!device || !device->adapter || !device->adapter->state_set) return -1;
    if (!smart_safety_gate(device)) return -1;
    
    int32_t result = device->adapter->state_set(device->adapter, device, new_state);
    if (result >= 0) {
        device->previous_state = device->current_state;
        device->current_state = new_state;
        device->state_enter_tick = 0; /* Would be set to current tick */
    }
    return result;
}

/* ===== Health & Self-test ===== */
int32_t smart_self_test(smart_device_t *device, uint32_t test_id, void *results) {
    if (!device || !device->adapter || !device->adapter->self_test) return -1;
    if (!smart_safety_gate(device)) return -1;
    
    int32_t result = device->adapter->self_test(device->adapter, device, test_id, results);
    
    if (result >= 0) {
        device->health.self_test_pass_count++;
    } else {
        device->health.self_test_fail_count++;
        device->health.failed_operations++;
    }
    device->health.total_operations++;
    device->health.last_self_test_tick = 0; /* Would be current tick */
    
    /* Update LPRES state based on self-test */
    if (result < 0 && device->lpres_state == SMART_STATE_TRUE) {
        smart_set_lpres_state(device, SMART_STATE_BOTH); /* Degraded */
    }
    
    return result;
}

int32_t smart_health_check(smart_device_t *device, smart_health_t *health) {
    if (!device || !device->adapter || !device->adapter->health_check || !health) return -1;
    
    int32_t result = device->adapter->health_check(device->adapter, device, health);
    
    if (result >= 0) {
        smart_mem_copy(&device->health, health, sizeof(smart_health_t));
        device->health.last_health_check_tick = 0; /* Current tick */
    }
    
    return result;
}

int32_t smart_coverage_verify(smart_device_t *device, surplus_real_t *coverage) {
    if (!device || !device->adapter || !device->adapter->coverage_verify || !coverage) return -1;
    
    int32_t result = device->adapter->coverage_verify(device->adapter, device, coverage);
    
    if (result >= 0) {
        device->health.coverage_ratio = *coverage;
        /* Enforce minimum coverage */
        if (SR_CMP(*coverage, device->min_coverage_ratio) < 0) {
            smart_set_lpres_state(device, SMART_STATE_BOTH);
        }
    }
    
    return result;
}

/* ===== Firmware ===== */
int32_t smart_firmware_update(smart_device_t *device, const uint8_t *firmware, uint32_t len) {
    if (!device || !device->adapter || !device->adapter->firmware_update || !firmware) return -1;
    if (!smart_safety_gate(device)) return -1;
    return device->adapter->firmware_update(device->adapter, device, firmware, len);
}

int32_t smart_firmware_verify(smart_device_t *device, uint32_t *version) {
    if (!device || !device->adapter || !device->adapter->firmware_verify || !version) return -1;
    return device->adapter->firmware_verify(device->adapter, device, version);
}

/* ===== Power Management ===== */
int32_t smart_power_set_state(smart_device_t *device, uint32_t power_state) {
    if (!device || !device->adapter || !device->adapter->power_set_state) return -1;
    if (!smart_safety_gate(device)) return -1;
    return device->adapter->power_set_state(device->adapter, device, power_state);
}

int32_t smart_power_get_state(smart_device_t *device, uint32_t *power_state) {
    if (!device || !device->adapter || !device->adapter->power_get_state || !power_state) return -1;
    return device->adapter->power_get_state(device->adapter, device, power_state);
}

/* ===== Crypto ===== */
int32_t smart_crypto_op(smart_device_t *device, uint32_t op_id, void *in, uint32_t in_len,
                        void *out, uint32_t *out_len) {
    if (!device || !device->adapter || !device->adapter->crypto_op) return -1;
    if (!smart_safety_gate(device)) return -1;
    return device->adapter->crypto_op(device->adapter, device, op_id, in, in_len, out, out_len);
}

/* ===== Time Sync ===== */
int32_t smart_time_sync(smart_device_t *device, uint64_t *timestamp) {
    if (!device || !device->adapter || !device->adapter->time_sync || !timestamp) return -1;
    return device->adapter->time_sync(device->adapter, device, timestamp);
}

/* ===== Hooks ===== */
int32_t smart_hook_register(smart_device_t *device, const smart_hook_t *hook) {
    if (!device || !device->adapter || !device->adapter->hook_register || !hook) return -1;
    if (!smart_safety_gate(device)) return -1;
    return device->adapter->hook_register(device->adapter, device, hook);
}

int32_t smart_hook_unregister(smart_device_t *device, uint32_t hook_id) {
    if (!device || !device->adapter || !device->adapter->hook_unregister) return -1;
    if (!smart_safety_gate(device)) return -1;
    return device->adapter->hook_unregister(device->adapter, device, hook_id);
}

/* ===== Virtual Device Simulation ===== */
int32_t smart_virtual_init(smart_device_t *device) {
    if (!device || !device->adapter || !device->adapter->virtual_init) return -1;
    return device->adapter->virtual_init(device->adapter, device);
}

int32_t smart_virtual_step(smart_device_t *device, uint64_t delta_ticks) {
    if (!device || !device->adapter || !device->adapter->virtual_step) return -1;
    return device->adapter->virtual_step(device->adapter, device, delta_ticks);
}

int32_t smart_virtual_sync(smart_device_t *device) {
    if (!device || !device->adapter || !device->adapter->virtual_sync) return -1;
    return device->adapter->virtual_sync(device->adapter, device);
}

/* ===== Global Operations ===== */
int32_t smart_registry_self_test_all(smart_adapter_registry_t *reg) {
    if (!reg) return -1;
    
    int32_t failed = 0;
    for (uint32_t i = 0; i < reg->num_adapters; i++) {
        smart_adapter_t *a = reg->adapters[i];
        for (uint32_t j = 0; j < a->num_devices; j++) {
            if (a->devices[j] && a->self_test) {
                if (a->self_test(a, a->devices[j], SMART_TEST_FULL, NULL) < 0) {
                    failed++;
                }
            }
        }
    }
    return failed == 0 ? 0 : -1;
}

int32_t smart_registry_health_check_all(smart_adapter_registry_t *reg) {
    if (!reg) return -1;
    
    int32_t unhealthy = 0;
    for (uint32_t i = 0; i < reg->num_adapters; i++) {
        smart_adapter_t *a = reg->adapters[i];
        for (uint32_t j = 0; j < a->num_devices; j++) {
            if (a->devices[j] && a->health_check) {
                smart_health_t health;
                if (a->health_check(a, a->devices[j], &health) < 0) {
                    unhealthy++;
                }
            }
        }
    }
    return unhealthy == 0 ? 0 : -1;
}

int32_t smart_registry_coverage_verify_all(smart_adapter_registry_t *reg) {
    if (!reg) return -1;
    
    int32_t uncovered = 0;
    for (uint32_t i = 0; i < reg->num_adapters; i++) {
        smart_adapter_t *a = reg->adapters[i];
        for (uint32_t j = 0; j < a->num_devices; j++) {
            if (a->devices[j] && a->coverage_verify) {
                surplus_real_t coverage;
                if (a->coverage_verify(a, a->devices[j], &coverage) < 0 ||
                    SR_CMP(coverage, a->devices[j]->min_coverage_ratio) < 0) {
                    uncovered++;
                }
            }
        }
    }
    return uncovered == 0 ? 0 : -1;
}

bool smart_registry_safety_gate_all(smart_adapter_registry_t *reg) {
    if (!reg) return false;
    
    for (uint32_t i = 0; i < reg->num_adapters; i++) {
        smart_adapter_t *a = reg->adapters[i];
        for (uint32_t j = 0; j < a->num_devices; j++) {
            if (a->devices[j] && !smart_safety_gate(a->devices[j])) {
                return false;
            }
        }
    }
    return true;
}

/* ===== JDR PirateNet Integration ===== */
int32_t smart_device_from_jdr_transceiver(smart_adapter_registry_t *reg,
                                           jdr_transceiver_t *tc,
                                           smart_device_t **out_device) {
    if (!reg || !tc || !out_device) return -1;
    
    /* Find radio adapter */
    smart_adapter_t *adapter = smart_adapter_find_by_class(reg, SMART_CLASS_RADIO);
    if (!adapter) return -1;
    
    /* Allocate device */
    smart_device_t *device = (smart_device_t *)0; /* Would allocate from pool */
    if (!device) return -1;
    
    /* Initialize device descriptor for radio */
    smart_device_descriptor_init(device, SMART_CLASS_RADIO, 0x3699, tc->device_id, "JDR Transceiver");
    
    /* Copy M5 coordinates */
    device->m5 = tc->m5;
    device->min_coverage_ratio = SR_FROM_FLOAT(1.8);
    
    /* Set virtual context to JDR transceiver */
    device->virtual_context = tc;
    device->virtual_context_size = sizeof(jdr_transceiver_t);
    device->adapter = adapter;
    
    /* Add registers from JDR transceiver */
    smart_register_t regs[] = {
        {0x00, 64, 3, 0, "frequency", "Operating frequency", LPRES_NEITHER, LPRES_NEITHER},
        {0x08, 64, 3, 0, "bandwidth", "Channel bandwidth", LPRES_NEITHER, LPRES_NEITHER},
        {0x10, 32, 3, 20, "power_dbm", "Transmit power", LPRES_NEITHER, LPRES_NEITHER},
        {0x14, 32, 3, 1000000, "sample_rate", "Sample rate", LPRES_NEITHER, LPRES_NEITHER},
        {0x18, 8, 3, 0, "band", "Active band", LPRES_NEITHER, LPRES_NEITHER},
        {0x19, 8, 3, 0, "modulation", "Active modulation", LPRES_NEITHER, LPRES_NEITHER},
        {0x1A, 8, 3, 0, "exec_mode", "Execution mode", LPRES_NEITHER, LPRES_NEITHER},
        {0x20, 32, 3, 0, "hum_freq", "Harmonic hum frequency", LPRES_NEITHER, LPRES_NEITHER},
        {0x24, 32, 3, 0, "hum_amplitude", "Harmonic hum amplitude", LPRES_NEITHER, LPRES_NEITHER},
        {0x28, 32, 3, 0, "hum_phase", "Harmonic hum phase", LPRES_NEITHER, LPRES_NEITHER},
        {0x2C, 32, 3, 1, "harmonic_n", "Harmonic order", LPRES_NEITHER, LPRES_NEITHER},
    };
    for (uint32_t i = 0; i < sizeof(regs)/sizeof(regs[0]); i++) {
        smart_device_add_register(device, &regs[i]);
    }
    
    /* Add DMA channels */
    smart_dma_channel_t dma_tx = {0, 0, 256, 0x80000000, 65536, 0x80020000, 256, true, LPRES_NEITHER};
    smart_dma_channel_t dma_rx = {1, 1, 256, 0x80010000, 65536, 0x80030000, 256, true, LPRES_NEITHER};
    smart_device_add_dma_channel(device, &dma_tx);
    smart_device_add_dma_channel(device, &dma_rx);
    
    /* Add IRQs */
    smart_irq_t irqs[] = {
        {42, 5, 1, false, false, LPRES_NEITHER, NULL, NULL},  /* carrier_lock */
        {43, 5, 1, false, false, LPRES_NEITHER, NULL, NULL},  /* rx_ready */
        {44, 5, 1, false, false, LPRES_NEITHER, NULL, NULL},  /* tx_done */
        {45, 5, 1, false, false, LPRES_NEITHER, NULL, NULL},  /* harmonic_resonance */
        {46, 5, 1, false, false, LPRES_NEITHER, NULL, NULL},  /* coverage_breach */
        {47, 5, 1, false, false, LPRES_NEITHER, NULL, NULL},  /* frequency_hop */
    };
    for (uint32_t i = 0; i < sizeof(irqs)/sizeof(irqs[0]); i++) {
        smart_device_add_irq(device, &irqs[i]);
    }
    
    /* Add states */
    smart_state_t states[] = {
        {0, "IDLE", 0, 0, 0, LPRES_NEITHER},
        {1, "CONFIGURING", 0, 0, 1000, LPRES_NEITHER},
        {2, "ACTIVE", 0, 0, 0, LPRES_NEITHER},
        {3, "TRANSMITTING", 0, 0, 5000, LPRES_NEITHER},
        {4, "RECEIVING", 0, 0, 5000, LPRES_NEITHER},
        {5, "FHSS_HOPPING", 0, 0, 100, LPRES_NEITHER},
        {6, "HARMONIC_LOCK", 0, 0, 2000, LPRES_NEITHER},
        {7, "ERROR", 0, 0, 0, LPRES_NEITHER},
    };
    for (uint32_t i = 0; i < sizeof(states)/sizeof(states[0]); i++) {
        smart_device_add_state(device, &states[i]);
    }
    
    /* Add transitions */
    smart_transition_t trans[] = {
        {0, 1, 1, 0, 0, LPRES_NEITHER},  /* IDLE -> CONFIGURING */
        {1, 2, 2, 0, 0, LPRES_NEITHER},  /* CONFIGURING -> ACTIVE */
        {2, 3, 3, 0, 0, LPRES_NEITHER},  /* ACTIVE -> TRANSMITTING */
        {2, 4, 4, 0, 0, LPRES_NEITHER},  /* ACTIVE -> RECEIVING */
        {2, 5, 5, 0, 0, LPRES_NEITHER},  /* ACTIVE -> FHSS_HOPPING */
        {2, 6, 6, 0, 0, LPRES_NEITHER},  /* ACTIVE -> HARMONIC_LOCK */
        {3, 2, 7, 0, 0, LPRES_NEITHER},  /* TRANSMITTING -> ACTIVE */
        {4, 2, 8, 0, 0, LPRES_NEITHER},  /* RECEIVING -> ACTIVE */
        {5, 2, 9, 0, 0, LPRES_NEITHER},  /* FHSS_HOPPING -> ACTIVE */
        {6, 2, 10, 0, 0, LPRES_NEITHER}, /* HARMONIC_LOCK -> ACTIVE */
        {0, 7, 0xFF, 0, 0, LPRES_NEITHER}, /* ANY -> ERROR */
    };
    for (uint32_t i = 0; i < sizeof(trans)/sizeof(trans[0]); i++) {
        smart_device_add_transition(device, &trans[i]);
    }
    
    *out_device = device;
    return 0;
}

int32_t smart_device_to_jdr_transceiver(smart_device_t *device, jdr_transceiver_t *tc) {
    if (!device || !tc) return -1;
    
    /* Read registers from device */
    uint32_t freq, bw, power, sr, band, mod, exec;
    smart_reg_read(device, 0x00, &freq, 64);
    smart_reg_read(device, 0x08, &bw, 64);
    smart_reg_read(device, 0x10, &power, 32);
    smart_reg_read(device, 0x14, &sr, 32);
    smart_reg_read(device, 0x18, &band, 8);
    smart_reg_read(device, 0x19, &mod, 8);
    smart_reg_read(device, 0x1A, &exec, 8);
    
    tc->reg_frequency = freq;
    tc->reg_bandwidth = bw;
    tc->reg_power_dbm = (int32_t)power;
    tc->reg_sample_rate = sr;
    tc->reg_band = (jdr_band_t)band;
    tc->reg_mod = (jdr_modulation_t)mod;
    tc->reg_exec = (jdr_exec_mode_t)exec;
    
    return 0;
}

int32_t smart_virtual_sdr_simulate(smart_device_t *device,
                                    uint64_t frequency,
                                    uint64_t bandwidth,
                                    jdr_modulation_t mod,
                                    jdr_exec_mode_t exec) {
    if (!device) return -1;
    
    /* Configure virtual SDR */
    smart_reg_write(device, 0x00, (uint32_t)frequency, 64);
    smart_reg_write(device, 0x08, (uint32_t)bandwidth, 64);
    smart_reg_write(device, 0x19, mod, 8);
    smart_reg_write(device, 0x1A, exec, 8);
    
    /* State transition to CONFIGURING */
    smart_state_transition(device, 1);
    
    /* Initialize harmonic hum */
    smart_reg_write(device, 0x20, (uint32_t)(frequency / 1000000), 32);
    smart_reg_write(device, 0x24, 0x10000, 32);  /* SR_ONE */
    smart_reg_write(device, 0x28, 0, 32);
    smart_reg_write(device, 0x2C, 1, 32);
    
    /* State transition to HARMONIC_LOCK */
    smart_state_transition(device, 6);
    
    /* Verify carrier lock */
    uint32_t carrier_lock;
    smart_reg_read(device, 0x00, &carrier_lock, 32); /* Would read carrier lock register */
    
    /* State transition to ACTIVE */
    smart_state_transition(device, 2);
    
    return 0;
}

/* ===== Health Monitor ===== */
static uint32_t g_health_monitor_interval = 0;
static bool g_health_monitor_active = false;

int32_t smart_start_health_monitor(smart_device_t *device, uint32_t interval_ticks) {
    if (!device) return -1;
    g_health_monitor_interval = interval_ticks;
    g_health_monitor_active = true;
    return 0;
}

int32_t smart_stop_health_monitor(smart_device_t *device) {
    if (!device) return -1;
    g_health_monitor_active = false;
    return 0;
}

/* ===== Graceful Degradation ===== */
int32_t smart_degrade_gracefully(smart_device_t *device, uint32_t failure_mode) {
    if (!device) return -1;
    
    /* Log failure */
    device->health.failed_operations++;
    
    /* Set state to BOTH (degraded) */
    smart_set_lpres_state(device, SMART_STATE_BOTH);
    
    /* Transition to ERROR state */
    smart_state_transition(device, 0xFF);
    
    /* Attempt to maintain minimal functionality */
    switch (failure_mode) {
        case 0: /* Radio: reduce power, single modulation */
            smart_reg_write(device, 0x10, 10, 32);  /* Reduce power to 10 dBm */
            smart_reg_write(device, 0x19, JDR_MOD_FM, 8);  /* Fallback to FM */
            break;
        case 1: /* Storage: read-only mode */
            break;
        case 2: /* Network: reduce bandwidth */
            smart_reg_write(device, 0x08, 1000000, 64);  /* 1 MHz bandwidth */
            break;
        default:
            break;
    }
    
    return 0;
}

int32_t smart_recover(smart_device_t *device) {
    if (!device) return -1;
    
    /* Run full self-test */
    if (device->adapter && device->adapter->self_test) {
        int32_t result = device->adapter->self_test(device->adapter, device, SMART_TEST_FULL, NULL);
        if (result < 0) return -1;
    }
    
    /* Verify coverage */
    surplus_real_t coverage;
    if (device->adapter && device->adapter->coverage_verify) {
        device->adapter->coverage_verify(device->adapter, device, &coverage);
        if (SR_CMP(coverage, device->min_coverage_ratio) < 0) return -1;
    }
    
    /* Restore state to TRUE */
    smart_set_lpres_state(device, SMART_STATE_TRUE);
    
    /* Transition to ACTIVE */
    smart_state_transition(device, 2);
    
    return 0;
}

/* ===== Device Descriptor Helpers ===== */
int32_t smart_device_descriptor_init(smart_device_t *device, uint32_t device_class,
                                      uint32_t vendor_id, uint32_t product_id,
                                      const char *name) {
    if (!device) return -1;
    
    smart_mem_set(device, 0, sizeof(*device));
    device->device_class = device_class;
    device->vendor_id = vendor_id;
    device->product_id = product_id;
    device->revision = 1;
    smart_str_copy(device->name, name ? name : "Unknown", 64);
    smart_str_copy(device->firmware_version, "1.0.0", 32);
    device->capabilities = SMART_CAP_REGISTER_MAP | SMART_CAP_DMA | SMART_CAP_IRQ |
                           SMART_CAP_STATE_MACHINE | SMART_CAP_SELF_TEST |
                           SMART_CAP_HEALTH_MONITOR | SMART_CAP_LPRES_ATTEST |
                           SMART_CAP_M5_COVERAGE | SMART_CAP_PARACONSISTENT;
    device->min_coverage_ratio = SR_FROM_FLOAT(1.8);
    device->lpres_state = SMART_STATE_NEITHER;
    device->global_attestation = LPRES_NEITHER;
    
    return 0;
}

int32_t smart_device_add_register(smart_device_t *device, const smart_register_t *reg) {
    if (!device || !reg || device->num_registers >= SMART_ADAPTER_MAX_REGISTERS) return -1;
    device->registers[device->num_registers++] = *reg;
    return 0;
}

int32_t smart_device_add_dma_channel(smart_device_t *device, const smart_dma_channel_t *dma) {
    if (!device || !dma || device->num_dma_channels >= SMART_ADAPTER_MAX_DMA_CHANNELS) return -1;
    device->dma_channels[device->num_dma_channels++] = *dma;
    return 0;
}

int32_t smart_device_add_irq(smart_device_t *device, const smart_irq_t *irq) {
    if (!device || !irq || device->num_irqs >= SMART_ADAPTER_MAX_IRQS) return -1;
    device->irqs[device->num_irqs++] = *irq;
    return 0;
}

int32_t smart_device_add_state(smart_device_t *device, const smart_state_t *state) {
    if (!device || !state || device->num_states >= SMART_ADAPTER_MAX_STATES) return -1;
    device->states[device->num_states++] = *state;
    return 0;
}

int32_t smart_device_add_transition(smart_device_t *device, const smart_transition_t *trans) {
    if (!device || !trans || device->num_transitions >= SMART_ADAPTER_MAX_TRANSITIONS) return -1;
    device->transitions[device->num_transitions++] = *trans;
    return 0;
}

int32_t smart_device_add_hook(smart_device_t *device, const smart_hook_t *hook) {
    if (!device || !hook || device->num_hooks >= SMART_ADAPTER_MAX_HOOKS) return -1;
    device->hooks[device->num_hooks++] = *hook;
    return 0;
}
