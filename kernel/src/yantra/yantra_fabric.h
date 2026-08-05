/* yantra_fabric.h — ZXV Software-Defined Hardware Fabric (Yantra Fabric)
 *
 * Six maturity layers with clear boundaries:
 *   1. Hardware model — software representation of registers, state, commands
 *   2. Hardware emulator — executable service behaving like the device
 *   3. Synthesizable RTL — SystemVerilog/VHDL/Chisel capable of FPGA/ASIC
 *   4. Physical device driver — kernel driver via MMIO/PCIe/USB/SPI/I2C
 *   5. User-facing capability — logical event interface for applications
 *   6. Measured physical product — production hardware with calibration
 *
 * Capability states:
 *   MODEL_ONLY → EMULATED → RTL_SIMULATED → SYNTHESIZED →
 *   FPGA_VERIFIED → DEVICE_CONNECTED → DEVICE_MEASURED →
 *   QUALIFIED → CERTIFIED
 *
 * Digital twin: model predicts behavior, sensors report actual, contradictions
 * are preserved and acted upon (paraconsistent logic).
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef YANTRA_FABRIC_H
#define YANTRA_FABRIC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../event_space/event_space.h"

/* ===== Constants ===== */

#define YF_MAX_DEVICES         32    /* max registered devices */
#define YF_MAX_REGISTERS       64    /* max registers per device */
#define YF_MAX_STATES          16    /* max state machine states */
#define YF_MAX_EVENTS          32    /* max device events */
#define YF_MAX_NAME_LEN        32    /* device/register name length */
#define YF_MAX_TWIN_ASSERTIONS 16    /* max digital twin assertions */

/* ===== Capability States ===== */

typedef enum {
    YF_MODEL_ONLY       = 0,  /* software model exists, no hardware */
    YF_EMULATED         = 1,  /* emulator runs, no real hardware */
    YF_RTL_SIMULATED    = 2,  /* RTL exists and passes simulation */
    YF_SYNTHESIZED      = 3,  /* RTL synthesizes successfully */
    YF_FPGA_VERIFIED    = 4,  /* FPGA prototype validated */
    YF_DEVICE_CONNECTED = 5,  /* physical device detected and communicating */
    YF_DEVICE_MEASURED  = 6,  /* physical measurements taken */
    YF_QUALIFIED        = 7,  /* passed qualification tests */
    YF_CERTIFIED        = 8,  /* passed certification */
} yf_capability_state_t;

/* ===== Device Register ===== */

typedef struct yf_register {
    char name[YF_MAX_NAME_LEN];
    uint32_t offset;                /* register offset in device address space */
    uint32_t width;                 /* width in bits (8/16/32/64) */
    bool readable;
    bool writable;
    uint32_t reset_value;
} yf_register_t;

/* ===== Device State Machine ===== */

typedef struct yf_state {
    char name[YF_MAX_NAME_LEN];
    uint32_t id;
    bool is_safe_state;             /* safe state for fault recovery */
} yf_state_t;

/* ===== Device Event ===== */

typedef struct yf_device_event {
    char name[YF_MAX_NAME_LEN];
    char schema[EV_SCHEMA_LEN];     /* event schema for this device event */
    uint32_t from_state;            /* source state (or UINT32_MAX for any) */
    uint32_t to_state;              /* target state */
} yf_device_event_t;

/* ===== Digital Twin Assertion ===== */

typedef enum {
    YF_TWIN_CONSISTENT    = 0,  /* predicted matches measured */
    YF_TWIN_CONTRADICTORY = 1,  /* predicted differs from measured */
    YF_TWIN_UNKNOWN       = 2,  /* no measurement available */
} yf_twin_status_t;

typedef struct yf_twin_assertion {
    char predicted_event[YF_MAX_NAME_LEN];
    char measured_event[YF_MAX_NAME_LEN];
    yf_twin_status_t status;
    uint64_t sequence;              /* when the assertion was made */
} yf_twin_assertion_t;

/* ===== Device ===== */

typedef struct yf_device {
    uint32_t id;
    char name[YF_MAX_NAME_LEN];     /* e.g., "dlp-projector" */
    char device_class[YF_MAX_NAME_LEN]; /* e.g., "display", "input", "sensor" */

    /* Current capability state */
    yf_capability_state_t capability;

    /* Registers */
    yf_register_t registers[YF_MAX_REGISTERS];
    uint32_t num_registers;

    /* State machine */
    yf_state_t states[YF_MAX_STATES];
    uint32_t num_states;
    uint32_t current_state;

    /* Device events */
    yf_device_event_t events[YF_MAX_EVENTS];
    uint32_t num_events;

    /* Digital twin assertions */
    yf_twin_assertion_t twin_assertions[YF_MAX_TWIN_ASSERTIONS];
    uint32_t num_twin_assertions;

    /* Statistics */
    uint64_t total_events_emitted;
    uint64_t total_contradictions;
    uint64_t total_safe_state_transitions;

    bool registered;
} yf_device_t;

/* ===== Yantra Fabric ===== */

typedef struct yf_fabric {
    yf_device_t devices[YF_MAX_DEVICES];
    uint32_t num_devices;
    uint32_t next_device_id;

    /* Global statistics */
    uint64_t total_devices_registered;
    uint64_t total_capability_upgrades;  /* device moved to higher capability */
    uint64_t total_capability_downgrades;
    uint64_t total_contradictions;
    uint64_t total_safe_state_events;
} yf_fabric_t;

/* ===== API ===== */

/* Initialize the Yantra Fabric */
void yf_init(yf_fabric_t *yf);

/* Device management */
int32_t yf_register_device(yf_fabric_t *yf, const char *name,
                            const char *device_class,
                            yf_capability_state_t initial_capability);
yf_device_t *yf_get_device(yf_fabric_t *yf, uint32_t idx);
yf_device_t *yf_get_device_by_name(yf_fabric_t *yf, const char *name);

/* Capability state transitions */
bool yf_set_capability(yf_fabric_t *yf, uint32_t device_idx,
                        yf_capability_state_t new_state);
const char *yf_capability_name(yf_capability_state_t state);

/* Register management */
bool yf_device_add_register(yf_fabric_t *yf, uint32_t device_idx,
                             const char *name, uint32_t offset,
                             uint32_t width, bool readable, bool writable,
                             uint32_t reset_value);

/* State machine management */
bool yf_device_add_state(yf_fabric_t *yf, uint32_t device_idx,
                          const char *name, bool is_safe_state);
bool yf_device_set_state(yf_fabric_t *yf, uint32_t device_idx,
                          uint32_t state_id);

/* Event management */
bool yf_device_add_event(yf_fabric_t *yf, uint32_t device_idx,
                          const char *name, const char *schema,
                          uint32_t from_state, uint32_t to_state);

/* Emit a device event (transitions state + returns schema) */
bool yf_device_emit_event(yf_fabric_t *yf, uint32_t device_idx,
                           uint32_t event_idx, char *out_schema,
                           uint32_t schema_max);

/* Digital twin */
bool yf_twin_assert(yf_fabric_t *yf, uint32_t device_idx,
                     const char *predicted, const char *measured,
                     uint64_t sequence);
uint32_t yf_twin_check_contradictions(yf_fabric_t *yf, uint32_t device_idx);
bool yf_twin_enter_safe_state(yf_fabric_t *yf, uint32_t device_idx);

/* Get all devices at a given capability level */
uint32_t yf_get_devices_at_capability(yf_fabric_t *yf,
                                       yf_capability_state_t cap,
                                       uint32_t *out_indices,
                                       uint32_t max_out);

/* Check if a device is ready for a given use (minimum capability) */
bool yf_device_ready_for(yf_fabric_t *yf, uint32_t device_idx,
                          yf_capability_state_t min_required);

#endif /* YANTRA_FABRIC_H */
