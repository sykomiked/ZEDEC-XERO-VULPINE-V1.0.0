/* smart_adapter_integration.h — Smart Adapter + Orbital Elevator + Yantra Fabric Integration
 *
 * Unifies three fabric layers:
 * 1. Smart Adapter — Generic hardware/firmware adapter with LPRES attestation, M5 coverage, virtual
 * simulation
 * 2. Orbital Elevator — Schema translation and compatibility for event envelopes
 * 3. Yantra Fabric — Software-defined hardware fabric with 9 capability states, digital twin, state
 * machines
 *
 * Integration patterns:
 * - Smart Adapter devices register as Yantra Fabric devices
 * - Orbital Elevator translates between Smart Adapter register schemas and Yantra event schemas
 * - Smart Adapter virtual simulation feeds Yantra digital twin predictions
 * - LPRES attestation from Smart Adapter informs Yantra capability state transitions
 * - M5 coverage enforcement gates Yantra capability upgrades
 * - Paraconsistent logic (LPRES) preserved across all three layers
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef SMART_ADAPTER_INTEGRATION_H
#define SMART_ADAPTER_INTEGRATION_H

#include <stdint.h>
#include <stdbool.h>
#include "smart_adapter.h"
#include "orbital_elevator.h"
#include "yantra_fabric.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"

/* ===== Integration Constants ===== */
#define SAI_MAX_DEVICES            32
#define SAI_MAX_SCHEMA_TRANSLATIONS 64
#define SAI_MAX_TWIN_SYNC_HOOKS    16
#define SAI_MAX_CAPABILITY_GUARDS  32

/* ===== Capability Guard ===== */
/* A guard that must pass before a Yantra capability transition */
typedef struct {
    uint32_t guard_id;
    char name[64];
    yf_capability_state_t from_capability;
    yf_capability_state_t to_capability;
    
    /* Smart Adapter requirements */
    surplus_real_t min_coverage_ratio;      /* M5 coverage required */
    smart_device_state_t min_lpres_state;   /* LPRES state required */
    uint32_t min_self_test_passes;          /* Self-test passes required */
    uint32_t max_self_test_failures;        /* Max failures allowed */
    
    /* Orbital Elevator requirements */
    const char *required_schema;            /* Schema that must be translatable */
    uint16_t required_schema_version;       /* Version required */
    
    /* Custom guard function */
    bool (*guard_fn)(const smart_device_t *sa_device,
                     const yf_device_t *yf_device,
                     const oe_elevator_t *elevator);
    
    lpres_state_t guard_attestation;
    bool enabled;
} sai_capability_guard_t;

/* ===== Schema Translation Mapping ===== */
typedef struct {
    uint32_t mapping_id;
    char sa_schema[64];              /* Smart Adapter register/event schema */
    char yf_schema[64];              /* Yantra Fabric event schema */
    char oe_schema[64];              /* Orbital Elevator canonical schema */
    uint16_t sa_version;
    uint16_t yf_version;
    uint16_t oe_version;
    uint32_t adapter_id;             /* Orbital Elevator adapter ID */
    bool bidirectional;
    lpres_state_t translation_attestation;
    bool active;
} sai_schema_mapping_t;

/* ===== Digital Twin Sync Hook ===== */
typedef struct {
    uint32_t hook_id;
    char name[64];
    uint32_t sa_device_idx;          /* Smart Adapter device index */
    uint32_t yf_device_idx;          /* Yantra Fabric device index */
    
    /* Sync function: called to synchronize virtual simulation with digital twin */
    int32_t (*sync_fn)(const smart_device_t *sa_device,
                       yf_device_t *yf_device,
                       const oe_elevator_t *elevator);
    
    uint32_t interval_ticks;         /* Sync interval */
    uint64_t last_sync_tick;
    lpres_state_t sync_attestation;
    bool enabled;
} sai_twin_sync_hook_t;

/* ===== Integrated Device Context ===== */
typedef struct {
    /* Smart Adapter device */
    smart_device_t *sa_device;
    uint32_t sa_device_idx;
    
    /* Yantra Fabric device */
    yf_device_t *yf_device;
    uint32_t yf_device_idx;
    
    /* Current capability state (synced) */
    yf_capability_state_t current_capability;
    
    /* Schema mappings for this device */
    sai_schema_mapping_t schema_mappings[SAI_MAX_SCHEMA_TRANSLATIONS];
    uint32_t num_schema_mappings;
    
    /* Capability guards for this device */
    sai_capability_guard_t capability_guards[SAI_MAX_CAPABILITY_GUARDS];
    uint32_t num_capability_guards;
    
    /* Twin sync hooks for this device */
    sai_twin_sync_hook_t twin_sync_hooks[SAI_MAX_TWIN_SYNC_HOOKS];
    uint32_t num_twin_sync_hooks;
    
    /* Integration health */
    struct {
        surplus_real_t coverage_ratio;
        smart_device_state_t lpres_state;
        yf_capability_state_t yf_capability;
        uint32_t successful_translations;
        uint32_t failed_translations;
        uint32_t twin_sync_count;
        uint32_t twin_contradictions;
        uint64_t last_sync_tick;
        lpres_state_t integration_attestation;
    } health;
    
    /* Paraconsistent integration state */
    lpres_state_t global_attestation;
    bool safety_gate_open;
} sai_integrated_device_t;

/* ===== Integration Fabric ===== */
typedef struct {
    /* Core components */
    smart_adapter_registry_t *sa_registry;
    oe_elevator_t *elevator;
    yf_fabric_t *yf_fabric;
    
    /* Integrated devices */
    sai_integrated_device_t devices[SAI_MAX_DEVICES];
    uint32_t num_devices;
    
    /* Global schema mappings */
    sai_schema_mapping_t global_mappings[SAI_MAX_SCHEMA_TRANSLATIONS];
    uint32_t num_global_mappings;
    
    /* Global capability guards */
    sai_capability_guard_t global_guards[SAI_MAX_CAPABILITY_GUARDS];
    uint32_t num_global_guards;
    
    /* Global twin sync hooks */
    sai_twin_sync_hook_t global_hooks[SAI_MAX_TWIN_SYNC_HOOKS];
    uint32_t num_global_hooks;
    
    /* Statistics */
    uint64_t total_integrations;
    uint64_t total_capability_transitions;
    uint64_t total_schema_translations;
    uint64_t total_twin_syncs;
    uint64_t total_contradictions_detected;
    uint64_t total_safety_gate_closures;
    
    /* Paraconsistent global state */
    lpres_state_t global_attestation;
    bool global_safety_gate;
} sai_integration_fabric_t;

/* ===== Default Capability Guards ===== */

/* Guard: MODEL_ONLY -> EMULATED requires basic self-test pass */
extern const sai_capability_guard_t SAI_GUARD_MODEL_TO_EMULATED;

/* Guard: EMULATED -> RTL_SIMULATED requires RTL simulation pass */
extern const sai_capability_guard_t SAI_GUARD_EMULATED_TO_RTL;

/* Guard: RTL_SIMULATED -> SYNTHESIZED requires synthesis pass */
extern const sai_capability_guard_t SAI_GUARD_RTL_TO_SYNTHESIZED;

/* Guard: SYNTHESIZED -> FPGA_VERIFIED requires FPGA verification */
extern const sai_capability_guard_t SAI_GUARD_SYNTHESIZED_TO_FPGA;

/* Guard: FPGA_VERIFIED -> DEVICE_CONNECTED requires physical device detection */
extern const sai_capability_guard_t SAI_GUARD_FPGA_TO_CONNECTED;

/* Guard: DEVICE_CONNECTED -> DEVICE_MEASURED requires measurements */
extern const sai_capability_guard_t SAI_GUARD_CONNECTED_TO_MEASURED;

/* Guard: DEVICE_MEASURED -> QUALIFIED requires qualification tests */
extern const sai_capability_guard_t SAI_GUARD_MEASURED_TO_QUALIFIED;

/* Guard: QUALIFIED -> CERTIFIED requires certification */
extern const sai_capability_guard_t SAI_GUARD_QUALIFIED_TO_CERTIFIED;

/* ===== API ===== */

/* Initialize integration fabric */
void sai_init(sai_integration_fabric_t *fabric,
              smart_adapter_registry_t *sa_registry,
              oe_elevator_t *elevator,
              yf_fabric_t *yf_fabric);

/* Register a Smart Adapter device with Yantra Fabric */
int32_t sai_register_device(sai_integration_fabric_t *fabric,
                            smart_device_t *sa_device,
                            const char *yf_device_class,
                            yf_capability_state_t initial_capability);

/* Unregister integrated device */
int32_t sai_unregister_device(sai_integration_fabric_t *fabric, uint32_t device_idx);

/* Get integrated device by index */
sai_integrated_device_t *sai_get_device(sai_integration_fabric_t *fabric, uint32_t idx);

/* Get integrated device by Smart Adapter device */
sai_integrated_device_t *sai_get_device_by_sa(sai_integration_fabric_t *fabric, smart_device_t *sa_device);

/* Get integrated device by Yantra device name */
sai_integrated_device_t *sai_get_device_by_yf_name(sai_integration_fabric_t *fabric, const char *yf_name);

/* ===== Schema Translation ===== */

/* Add a schema mapping between SA, YF, and OE schemas */
int32_t sai_add_schema_mapping(sai_integration_fabric_t *fabric,
                               const char *sa_schema, uint16_t sa_version,
                               const char *yf_schema, uint16_t yf_version,
                               const char *oe_schema, uint16_t oe_version,
                               uint32_t oe_adapter_id,
                               bool bidirectional);

/* Translate a Smart Adapter register read/write to Yantra event schema */
int32_t sai_translate_sa_to_yf(sai_integration_fabric_t *fabric,
                                uint32_t device_idx,
                                const char *sa_schema, uint16_t sa_version,
                                ev_envelope_t *env);

/* Translate a Yantra event to Smart Adapter register schema */
int32_t sai_translate_yf_to_sa(sai_integration_fabric_t *fabric,
                                uint32_t device_idx,
                                const char *yf_schema, uint16_t yf_version,
                                ev_envelope_t *env);

/* Translate using Orbital Elevator (full path) */
oe_translate_result_t sai_oe_translate(sai_integration_fabric_t *fabric,
                                        ev_envelope_t *env,
                                        const char *target_schema,
                                        uint16_t target_version);

/* ===== Capability State Management ===== */

/* Attempt to upgrade Yantra capability (with Smart Adapter guards) */
int32_t sai_upgrade_capability(sai_integration_fabric_t *fabric,
                               uint32_t device_idx,
                               yf_capability_state_t target_capability);

/* Attempt to downgrade Yantra capability */
int32_t sai_downgrade_capability(sai_integration_fabric_t *fabric,
                                 uint32_t device_idx,
                                 yf_capability_state_t target_capability);

/* Check all capability guards for a transition */
bool sai_check_capability_guards(sai_integration_fabric_t *fabric,
                                  uint32_t device_idx,
                                  yf_capability_state_t from_cap,
                                  yf_capability_state_t to_cap);

/* Add a capability guard */
int32_t sai_add_capability_guard(sai_integration_fabric_t *fabric,
                                  uint32_t device_idx,
                                  const sai_capability_guard_t *guard);

/* ===== Digital Twin Synchronization ===== */

/* Synchronize Smart Adapter virtual simulation with Yantra digital twin */
int32_t sai_sync_twin(sai_integration_fabric_t *fabric, uint32_t device_idx);

/* Add a twin sync hook */
int32_t sai_add_twin_sync_hook(sai_integration_fabric_t *fabric,
                                uint32_t device_idx,
                                const sai_twin_sync_hook_t *hook);

/* Run all twin sync hooks for a device */
int32_t sai_run_twin_sync_hooks(sai_integration_fabric_t *fabric, uint32_t device_idx);

/* ===== Health & Attestation ===== */

/* Check integration health for a device */
int32_t sai_check_health(sai_integration_fabric_t *fabric,
                         uint32_t device_idx,
                         sai_integrated_device_t *health_out);

/* Check global integration health */
int32_t sai_check_global_health(sai_integration_fabric_t *fabric);

/* Get LPRES attestation for integration operations */
lpres_state_t sai_attest_integration(sai_integration_fabric_t *fabric,
                                      uint32_t device_idx,
                                      uint32_t op_id, void *args, int32_t result);

/* Check global safety gate */
bool sai_global_safety_gate(sai_integration_fabric_t *fabric);

/* Check device safety gate */
bool sai_device_safety_gate(sai_integration_fabric_t *fabric, uint32_t device_idx);

/* ===== Paraconsistent State ===== */

/* Get integration LPRES state */
lpres_state_t sai_get_lpres_state(sai_integration_fabric_t *fabric, uint32_t device_idx);

/* Set integration LPRES state */
void sai_set_lpres_state(sai_integration_fabric_t *fabric, uint32_t device_idx, lpres_state_t state);

/* ===== Event Emission ===== */

/* Emit a Yantra event from Smart Adapter operation */
int32_t sai_emit_yf_event(sai_integration_fabric_t *fabric,
                          uint32_t device_idx,
                          uint32_t yf_event_idx,
                          const smart_register_t *sa_reg, uint32_t sa_value);

/* Emit a Smart Adapter register change from Yantra event */
int32_t sai_emit_sa_register(sai_integration_fabric_t *fabric,
                             uint32_t device_idx,
                             const char *sa_reg_name,
                             uint32_t value, uint32_t width);

/* ===== Default Guard Functions ===== */

bool sai_guard_model_to_emulated(const smart_device_t *sa_device,
                                  const yf_device_t *yf_device,
                                  const oe_elevator_t *elevator);

bool sai_guard_emulated_to_rtl(const smart_device_t *sa_device,
                                const yf_device_t *yf_device,
                                const oe_elevator_t *elevator);

bool sai_guard_rtl_to_synthesized(const smart_device_t *sa_device,
                                   const yf_device_t *yf_device,
                                   const oe_elevator_t *elevator);

bool sai_guard_synthesized_to_fpga(const smart_device_t *sa_device,
                                    const yf_device_t *yf_device,
                                    const oe_elevator_t *elevator);

bool sai_guard_fpga_to_connected(const smart_device_t *sa_device,
                                  const yf_device_t *yf_device,
                                  const oe_elevator_t *elevator);

bool sai_guard_connected_to_measured(const smart_device_t *sa_device,
                                      const yf_device_t *yf_device,
                                      const oe_elevator_t *elevator);

bool sai_guard_measured_to_qualified(const smart_device_t *sa_device,
                                      const yf_device_t *yf_device,
                                      const oe_elevator_t *elevator);

bool sai_guard_qualified_to_certified(const smart_device_t *sa_device,
                                       const yf_device_t *yf_device,
                                       const oe_elevator_t *elevator);

/* ===== Utility ===== */

const char *sai_capability_name(yf_capability_state_t cap);
const char *sai_lpres_state_name(smart_device_state_t state);
const char *sai_oe_result_name(oe_translate_result_t result);

/* Convert Yantra capability to minimum LPRES state */
smart_device_state_t sai_capability_to_min_lpres(yf_capability_state_t cap);

/* Convert Yantra capability to minimum M5 coverage */
surplus_real_t sai_capability_to_min_coverage(yf_capability_state_t cap);

/* Convert Yantra capability to minimum self-test passes */
uint32_t sai_capability_to_min_self_tests(yf_capability_state_t cap);

#endif /* SMART_ADAPTER_INTEGRATION_H */
