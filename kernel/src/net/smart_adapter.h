/* smart_adapter.h — Generic Smart Hardware/Firmware Adapter Framework
 *
 * A device-agnostic adapter layer that uses the "hardware-in-software" simulation
 * approach to create smart adapters for ANY device type. Built on paraconsistent
 * logic (LPRES) with M5 coverage enforcement for military-grade reliability.
 *
 * Key insight: The JDR PirateNet virtual SDR (jdr_transceiver_t) IS a hardware
 * simulator. We generalize this pattern: any device can be modeled as a virtual
 * device with register maps, DMA, IRQs, and state machines. The adapter bridges
 * virtual → physical with LPRES-attested operations.
 *
 * Author: 36N9 Genetics, LLC
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef SMART_ADAPTER_H
#define SMART_ADAPTER_H

#include <stdint.h>
#include <stdbool.h>
#include "jdr_piratenet.h"
#include "firmware_adapters.h"
#include "lpres.h"
#include "m5_types.h"
#include "surplus.h"
#include "../sdk/selfaudit.h"

/* LPRES state aliases for compatibility */
#define LPRES_NEITHER LPRES_STATE_NEITHER
#define LPRES_TRUE    LPRES_STATE_TRUE
#define LPRES_FALSE   LPRES_STATE_FALSE
#define LPRES_BOTH    LPRES_STATE_BOTH
#define lpres_combine lpres_conjoin

/* ===== Universal Device Model ===== */
/* Any device can be modeled as: registers + DMA + IRQs + state machine */

#define SMART_ADAPTER_MAX_REGISTERS    256
#define SMART_ADAPTER_MAX_DMA_CHANNELS 8
#define SMART_ADAPTER_MAX_IRQS         32
#define SMART_ADAPTER_MAX_STATES       64
#define SMART_ADAPTER_MAX_TRANSITIONS  128
#define SMART_ADAPTER_MAX_HOOKS        16

/* ===== Device Capability Flags (extensible) ===== */
#define SMART_CAP_REGISTER_MAP    0x00000001
#define SMART_CAP_DMA             0x00000002
#define SMART_CAP_IRQ             0x00000004
#define SMART_CAP_STATE_MACHINE   0x00000008
#define SMART_CAP_SELF_TEST       0x00000010
#define SMART_CAP_HEALTH_MONITOR  0x00000020
#define SMART_CAP_FIRMWARE_UPDATE 0x00000040
#define SMART_CAP_POWER_MGMT      0x00000080
#define SMART_CAP_CRYPTO          0x00000100
#define SMART_CAP_TIMESYNC        0x00000200
#define SMART_CAP_VIRTUALIZATION  0x00000400
#define SMART_CAP_HOT_SWAP        0x00000800
#define SMART_CAP_REDUNDANCY      0x00001000
#define SMART_CAP_LPRES_ATTEST    0x00002000
#define SMART_CAP_M5_COVERAGE     0x00004000
#define SMART_CAP_PARACONSISTENT  0x00008000

/* ===== Paraconsistent Device State (LPRES) ===== */
typedef enum {
    SMART_STATE_NEITHER = 0,  /* Unknown/Uninitialized - LPRES: NEITHER */
    SMART_STATE_TRUE    = 1,  /* Operational - LPRES: TRUE */
    SMART_STATE_FALSE   = 2,  /* Failed - LPRES: FALSE */
    SMART_STATE_BOTH    = 3,  /* Degraded/Conflict - LPRES: BOTH */
    SMART_STATE_MAX     = 4
} smart_device_state_t;

/* ===== Device Health Metrics ===== */
typedef struct {
    surplus_real_t uptime_ratio;        /* Operational time / total time */
    surplus_real_t error_rate;          /* Errors per operation */
    surplus_real_t coverage_ratio;      /* M5 coverage (must be >= 1.8) */
    surplus_real_t thermal_margin;      /* Thermal headroom */
    surplus_real_t power_margin;        /* Power headroom */
    surplus_real_t signal_quality;      /* Signal integrity */
    uint32_t total_operations;
    uint32_t failed_operations;
    uint32_t self_test_pass_count;
    uint32_t self_test_fail_count;
    uint64_t last_self_test_tick;
    uint64_t last_health_check_tick;
} smart_health_t;

/* ===== Register Descriptor ===== */
typedef struct {
    uint32_t offset;
    uint32_t width;          /* 8, 16, 32, 64 bits */
    uint32_t access;         /* R, W, RW, WO, RC, WC */
    uint32_t reset_value;
    const char *name;
    const char *description;
    /* LPRES attestation for this register */
    lpres_state_t read_attestation;
    lpres_state_t write_attestation;
} smart_register_t;

/* ===== DMA Channel Descriptor ===== */
typedef struct {
    uint32_t channel_id;
    uint32_t direction;      /* TX, RX, BIDIR */
    uint32_t max_burst_size;
    uint32_t buffer_phys;
    uint32_t buffer_size;
    uint32_t descriptor_phys;
    uint32_t num_descriptors;
    bool active;
    lpres_state_t dma_attestation;
} smart_dma_channel_t;

/* ===== IRQ Descriptor ===== */
typedef struct {
    uint32_t irq_number;
    uint32_t priority;
    uint32_t trigger;        /* LEVEL, EDGE_RISING, EDGE_FALLING, EDGE_BOTH */
    bool shared;
    bool enabled;
    lpres_state_t irq_attestation;
    void (*handler)(void *context);
    void *handler_context;
} smart_irq_t;

/* ===== State Machine ===== */
typedef struct {
    uint32_t state_id;
    const char *name;
    uint32_t entry_action;
    uint32_t exit_action;
    uint32_t timeout_ticks;
    lpres_state_t state_attestation;
} smart_state_t;

typedef struct {
    uint32_t from_state;
    uint32_t to_state;
    uint32_t event_id;
    uint32_t guard_condition;
    uint32_t action;
    lpres_state_t transition_attestation;
} smart_transition_t;

/* ===== Operation Hook (for extensibility) ===== */
typedef struct {
    uint32_t hook_id;
    const char *name;
    int32_t (*pre_hook)(void *device, void *args);
    int32_t (*post_hook)(void *device, void *args, int32_t result);
    lpres_state_t hook_attestation;
    bool enabled;
} smart_hook_t;

/* ===== Universal Device Descriptor ===== */
typedef struct {
    /* Identification */
    uint32_t device_class;      /* e.g., RADIO, STORAGE, NETWORK, SENSOR, CRYPTO, etc. */
    uint32_t device_subclass;
    uint32_t vendor_id;
    uint32_t product_id;
    uint32_t revision;
    char name[64];
    char firmware_version[32];
    
    /* Capabilities */
    uint32_t capabilities;
    
    /* Register map */
    smart_register_t registers[SMART_ADAPTER_MAX_REGISTERS];
    uint32_t num_registers;
    
    /* DMA channels */
    smart_dma_channel_t dma_channels[SMART_ADAPTER_MAX_DMA_CHANNELS];
    uint32_t num_dma_channels;
    
    /* IRQs */
    smart_irq_t irqs[SMART_ADAPTER_MAX_IRQS];
    uint32_t num_irqs;
    
    /* State machine */
    smart_state_t states[SMART_ADAPTER_MAX_STATES];
    uint32_t num_states;
    smart_transition_t transitions[SMART_ADAPTER_MAX_TRANSITIONS];
    uint32_t num_transitions;
    uint32_t current_state;
    uint32_t previous_state;
    uint64_t state_enter_tick;
    
    /* Hooks */
    smart_hook_t hooks[SMART_ADAPTER_MAX_HOOKS];
    uint32_t num_hooks;
    
    /* Health */
    smart_health_t health;
    
    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t min_coverage_ratio;  /* Default 1.8 */
    
    /* Paraconsistent state */
    smart_device_state_t lpres_state;
    lpres_state_t global_attestation;
    
    /* Virtual device context (the "hardware-in-software" simulation) */
    void *virtual_context;
    size_t virtual_context_size;
    
    /* Physical device context */
    void *physical_context;
    
    /* Adapter that owns this device */
    struct smart_adapter *adapter;
} smart_device_t;

/* ===== Smart Adapter ===== */
typedef struct smart_adapter {
    /* Identification */
    uint32_t adapter_id;
    char name[64];
    char version[32];
    uint32_t capabilities;
    
    /* Supported device classes */
    uint32_t supported_classes[32];
    uint32_t num_supported_classes;
    
    /* Device instances managed by this adapter */
    smart_device_t *devices[32];
    uint32_t num_devices;
    
    /* Adapter operations (virtual → physical bridge) */
    int32_t (*probe)(struct smart_adapter *adapter, void *bus_info, smart_device_t **out_device);
    int32_t (*remove)(struct smart_adapter *adapter, smart_device_t *device);
    int32_t (*init)(struct smart_adapter *adapter, smart_device_t *device);
    int32_t (*deinit)(struct smart_adapter *adapter, smart_device_t *device);
    int32_t (*reset)(struct smart_adapter *adapter, smart_device_t *device);
    int32_t (*suspend)(struct smart_adapter *adapter, smart_device_t *device);
    int32_t (*resume)(struct smart_adapter *adapter, smart_device_t *device);
    
    /* Register operations */
    int32_t (*reg_read)(struct smart_adapter *adapter, smart_device_t *device,
                        uint32_t reg_offset, uint32_t *value, uint32_t width);
    int32_t (*reg_write)(struct smart_adapter *adapter, smart_device_t *device,
                         uint32_t reg_offset, uint32_t value, uint32_t width);
    int32_t (*reg_read_bulk)(struct smart_adapter *adapter, smart_device_t *device,
                             uint32_t reg_offset, uint32_t *values, uint32_t count);
    int32_t (*reg_write_bulk)(struct smart_adapter *adapter, smart_device_t *device,
                              uint32_t reg_offset, const uint32_t *values, uint32_t count);
    
    /* DMA operations */
    int32_t (*dma_setup)(struct smart_adapter *adapter, smart_device_t *device,
                         uint32_t channel, uint32_t direction,
                         uint32_t buffer_phys, uint32_t buffer_size);
    int32_t (*dma_start)(struct smart_adapter *adapter, smart_device_t *device,
                         uint32_t channel);
    int32_t (*dma_stop)(struct smart_adapter *adapter, smart_device_t *device,
                        uint32_t channel);
    int32_t (*dma_status)(struct smart_adapter *adapter, smart_device_t *device,
                          uint32_t channel, uint32_t *status);
    
    /* IRQ operations */
    int32_t (*irq_enable)(struct smart_adapter *adapter, smart_device_t *device,
                          uint32_t irq_index);
    int32_t (*irq_disable)(struct smart_adapter *adapter, smart_device_t *device,
                           uint32_t irq_index);
    int32_t (*irq_ack)(struct smart_adapter *adapter, smart_device_t *device,
                       uint32_t irq_index);
    int32_t (*irq_set_handler)(struct smart_adapter *adapter, smart_device_t *device,
                               uint32_t irq_index, void (*handler)(void *), void *context);
    
    /* State machine operations */
    int32_t (*state_transition)(struct smart_adapter *adapter, smart_device_t *device,
                                uint32_t event_id);
    int32_t (*state_get)(struct smart_adapter *adapter, smart_device_t *device,
                         uint32_t *current_state);
    int32_t (*state_set)(struct smart_adapter *adapter, smart_device_t *device,
                         uint32_t new_state);
    
    /* Health & Self-test */
    int32_t (*self_test)(struct smart_adapter *adapter, smart_device_t *device,
                         uint32_t test_id, void *results);
    int32_t (*health_check)(struct smart_adapter *adapter, smart_device_t *device,
                            smart_health_t *health);
    int32_t (*coverage_verify)(struct smart_adapter *adapter, smart_device_t *device,
                               surplus_real_t *coverage);
    
    /* Firmware */
    int32_t (*firmware_update)(struct smart_adapter *adapter, smart_device_t *device,
                               const uint8_t *firmware, uint32_t len);
    int32_t (*firmware_verify)(struct smart_adapter *adapter, smart_device_t *device,
                               uint32_t *version);
    
    /* Power management */
    int32_t (*power_set_state)(struct smart_adapter *adapter, smart_device_t *device,
                               uint32_t power_state);
    int32_t (*power_get_state)(struct smart_adapter *adapter, smart_device_t *device,
                               uint32_t *power_state);
    
    /* Crypto operations (if supported) */
    int32_t (*crypto_op)(struct smart_adapter *adapter, smart_device_t *device,
                         uint32_t op_id, void *in, uint32_t in_len,
                         void *out, uint32_t *out_len);
    
    /* Time sync */
    int32_t (*time_sync)(struct smart_adapter *adapter, smart_device_t *device,
                         uint64_t *timestamp);
    
    /* Hook operations */
    int32_t (*hook_register)(struct smart_adapter *adapter, smart_device_t *device,
                             const smart_hook_t *hook);
    int32_t (*hook_unregister)(struct smart_adapter *adapter, smart_device_t *device,
                               uint32_t hook_id);
    
    /* Virtual device simulation (the "hardware-in-software" part) */
    int32_t (*virtual_init)(struct smart_adapter *adapter, smart_device_t *device);
    int32_t (*virtual_step)(struct smart_adapter *adapter, smart_device_t *device,
                            uint64_t delta_ticks);
    int32_t (*virtual_sync)(struct smart_adapter *adapter, smart_device_t *device);
    
    /* LPRES attestation */
    lpres_state_t (*attest_operation)(struct smart_adapter *adapter, smart_device_t *device,
                                       uint32_t op_id, void *args, int32_t result);
    
    /* Paraconsistent state management */
    smart_device_state_t (*get_lpres_state)(struct smart_adapter *adapter, smart_device_t *device);
    void (*set_lpres_state)(struct smart_adapter *adapter, smart_device_t *device,
                            smart_device_state_t state);
    bool (*safety_gate)(struct smart_adapter *adapter, smart_device_t *device);
    
    /* Adapter context */
    void *adapter_context;
    size_t adapter_context_size;
    
    /* M5 coverage for adapter itself */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
} smart_adapter_t;

/* ===== Adapter Registry ===== */
#define SMART_MAX_ADAPTERS 32

typedef struct {
    smart_adapter_t *adapters[SMART_MAX_ADAPTERS];
    uint32_t num_adapters;
    smart_adapter_t *default_adapter;
    
    /* Global health */
    smart_health_t global_health;
    uint64_t last_global_check;
    
    /* Paraconsistent global state */
    smart_device_state_t global_lpres_state;
    lpres_state_t global_attestation;
} smart_adapter_registry_t;

/* ===== Device Classes ===== */
#define SMART_CLASS_RADIO          0x0001
#define SMART_CLASS_STORAGE        0x0002
#define SMART_CLASS_NETWORK        0x0003
#define SMART_CLASS_SENSOR         0x0004
#define SMART_CLASS_CRYPTO         0x0005
#define SMART_CLASS_DISPLAY        0x0006
#define SMART_CLASS_INPUT          0x0007
#define SMART_CLASS_POWER          0x0008
#define SMART_CLASS_TIME           0x0009
#define SMART_CLASS_PROCESSOR      0x000A
#define SMART_CLASS_MEMORY         0x000B
#define SMART_CLASS_BUS            0x000C
#define SMART_CLASS_ACTUATOR       0x000D
#define SMART_CLASS_CAMERA         0x000E
#define SMART_CLASS_AUDIO          0x000F
#define SMART_CLASS_HAPTIC         0x0010
#define SMART_CLASS_BIOSENSOR      0x0011
#define SMART_CLASS_QUANTUM        0x0012
#define SMART_CLASS_NEUTRINO       0x0013
#define SMART_CLASS_HARMONIC       0x0014
#define SMART_CLASS_CUSTOM         0xFFFF

/* ===== Power States ===== */
#define SMART_POWER_ON       0
#define SMART_POWER_STANDBY  1
#define SMART_POWER_SLEEP    2
#define SMART_POWER_DEEP_SLEEP 3
#define SMART_POWER_OFF      4

/* ===== Self-Test IDs ===== */
#define SMART_TEST_BASIC        0
#define SMART_TEST_REGISTERS    1
#define SMART_TEST_DMA          2
#define SMART_TEST_IRQ          3
#define SMART_TEST_STATE_MACHINE 4
#define SMART_TEST_CRYPTO       5
#define SMART_TEST_COVERAGE     6
#define SMART_TEST_STRESS       7
#define SMART_TEST_FULL         0xFF

/* ===== API ===== */

/* Registry management */
void smart_adapter_registry_init(smart_adapter_registry_t *reg);
int32_t smart_adapter_register(smart_adapter_registry_t *reg, smart_adapter_t *adapter);
int32_t smart_adapter_unregister(smart_adapter_registry_t *reg, smart_adapter_t *adapter);
smart_adapter_t *smart_adapter_find_by_class(smart_adapter_registry_t *reg, uint32_t device_class);
smart_adapter_t *smart_adapter_find_by_name(smart_adapter_registry_t *reg, const char *name);

/* Device management */
int32_t smart_device_probe(smart_adapter_registry_t *reg, void *bus_info, smart_device_t **out_device);
int32_t smart_device_init(smart_adapter_registry_t *reg, smart_device_t *device);
int32_t smart_device_deinit(smart_adapter_registry_t *reg, smart_device_t *device);
int32_t smart_device_remove(smart_adapter_registry_t *reg, smart_device_t *device);

/* Register operations (with LPRES attestation) */
int32_t smart_reg_read(smart_device_t *device, uint32_t reg_offset, uint32_t *value, uint32_t width);
int32_t smart_reg_write(smart_device_t *device, uint32_t reg_offset, uint32_t value, uint32_t width);
int32_t smart_reg_read_bulk(smart_device_t *device, uint32_t reg_offset, uint32_t *values, uint32_t count);
int32_t smart_reg_write_bulk(smart_device_t *device, uint32_t reg_offset, const uint32_t *values, uint32_t count);

/* DMA operations */
int32_t smart_dma_setup(smart_device_t *device, uint32_t channel, uint32_t direction,
                        uint32_t buffer_phys, uint32_t buffer_size);
int32_t smart_dma_start(smart_device_t *device, uint32_t channel);
int32_t smart_dma_stop(smart_device_t *device, uint32_t channel);
int32_t smart_dma_status(smart_device_t *device, uint32_t channel, uint32_t *status);

/* IRQ operations */
int32_t smart_irq_enable(smart_device_t *device, uint32_t irq_index);
int32_t smart_irq_disable(smart_device_t *device, uint32_t irq_index);
int32_t smart_irq_ack(smart_device_t *device, uint32_t irq_index);
int32_t smart_irq_set_handler(smart_device_t *device, uint32_t irq_index,
                              void (*handler)(void *), void *context);

/* State machine */
int32_t smart_state_transition(smart_device_t *device, uint32_t event_id);
int32_t smart_state_get(smart_device_t *device, uint32_t *current_state);
int32_t smart_state_set(smart_device_t *device, uint32_t new_state);

/* Health & Self-test */
int32_t smart_self_test(smart_device_t *device, uint32_t test_id, void *results);
int32_t smart_health_check(smart_device_t *device, smart_health_t *health);
int32_t smart_coverage_verify(smart_device_t *device, surplus_real_t *coverage);

/* Firmware */
int32_t smart_firmware_update(smart_device_t *device, const uint8_t *firmware, uint32_t len);
int32_t smart_firmware_verify(smart_device_t *device, uint32_t *version);

/* Power management */
int32_t smart_power_set_state(smart_device_t *device, uint32_t power_state);
int32_t smart_power_get_state(smart_device_t *device, uint32_t *power_state);

/* Crypto */
int32_t smart_crypto_op(smart_device_t *device, uint32_t op_id, void *in, uint32_t in_len,
                        void *out, uint32_t *out_len);

/* Time sync */
int32_t smart_time_sync(smart_device_t *device, uint64_t *timestamp);

/* Hooks */
int32_t smart_hook_register(smart_device_t *device, const smart_hook_t *hook);
int32_t smart_hook_unregister(smart_device_t *device, uint32_t hook_id);

/* Virtual device simulation */
int32_t smart_virtual_init(smart_device_t *device);
int32_t smart_virtual_step(smart_device_t *device, uint64_t delta_ticks);
int32_t smart_virtual_sync(smart_device_t *device);

/* LPRES attestation */
lpres_state_t smart_attest_operation(smart_device_t *device, uint32_t op_id, void *args, int32_t result);

/* Paraconsistent state */
smart_device_state_t smart_get_lpres_state(smart_device_t *device);
void smart_set_lpres_state(smart_device_t *device, smart_device_state_t state);
bool smart_safety_gate(smart_device_t *device);

/* Global operations */
int32_t smart_registry_self_test_all(smart_adapter_registry_t *reg);
int32_t smart_registry_health_check_all(smart_adapter_registry_t *reg);
int32_t smart_registry_coverage_verify_all(smart_adapter_registry_t *reg);
bool smart_registry_safety_gate_all(smart_adapter_registry_t *reg);

/* JDR PirateNet Integration */
/* Create a JDR smart adapter */
smart_adapter_t *smart_adapter_create_jdr(jdr_adapter_registry_t *jdr_reg, jdr_network_t *jdr_net);

/* Create a smart device from a JDR transceiver */
int32_t smart_device_from_jdr_transceiver(smart_adapter_registry_t *reg,
                                           jdr_transceiver_t *tc,
                                           smart_device_t **out_device);

/* Create a JDR transceiver from a smart device */
int32_t smart_device_to_jdr_transceiver(smart_device_t *device,
                                         jdr_transceiver_t *tc);

/* Virtual SDR simulation for any device */
int32_t smart_virtual_sdr_simulate(smart_device_t *device,
                                    uint64_t frequency,
                                    uint64_t bandwidth,
                                    jdr_modulation_t mod,
                                    jdr_exec_mode_t exec);

/* Military-grade reliability: Continuous health monitoring */
int32_t smart_start_health_monitor(smart_device_t *device, uint32_t interval_ticks);
int32_t smart_stop_health_monitor(smart_device_t *device);

/* Graceful degradation */
int32_t smart_degrade_gracefully(smart_device_t *device, uint32_t failure_mode);
int32_t smart_recover(smart_device_t *device);

/* Device descriptor helpers */
int32_t smart_device_descriptor_init(smart_device_t *device, uint32_t device_class,
                                      uint32_t vendor_id, uint32_t product_id,
                                      const char *name);
int32_t smart_device_add_register(smart_device_t *device, const smart_register_t *reg);
int32_t smart_device_add_dma_channel(smart_device_t *device, const smart_dma_channel_t *dma);
int32_t smart_device_add_irq(smart_device_t *device, const smart_irq_t *irq);
int32_t smart_device_add_state(smart_device_t *device, const smart_state_t *state);
int32_t smart_device_add_transition(smart_device_t *device, const smart_transition_t *trans);
int32_t smart_device_add_hook(smart_device_t *device, const smart_hook_t *hook);

/* Coverage enforcement */
bool smart_enforce_coverage(smart_device_t *device, surplus_real_t required_ratio);
surplus_real_t smart_compute_coverage(smart_device_t *device);

/* Paraconsistent logic helpers */
const char *smart_state_name(smart_device_state_t state);
const char *smart_class_name(uint32_t device_class);
lpres_state_t smart_lpres_from_state(smart_device_state_t state);
smart_device_state_t smart_state_from_lpres(lpres_state_t lpres);

#endif /* SMART_ADAPTER_H */
