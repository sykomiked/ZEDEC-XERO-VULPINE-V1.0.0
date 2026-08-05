/* rtl_device.h — Hardware-as-Code RTL/HDL Abstraction Layer
 *
 * Provides a unified interface for describing kernel modules as
 * hardware devices using RTL (Register Transfer Level) semantics.
 * Supports Chisel, SystemVerilog, and VHDL target descriptions.
 *
 * Each module is described as:
 *   - A set of registers (clock-domain synchronized)
 *   - Data paths (combinational logic)
 *   - DMA buffers (memory-mapped)
 *   - IRQ lines (interrupt flags)
 *   - Bus interfaces (AXI4, APB, AHB, Wishbone)
 *
 * This enables:
 *   1. Software execution (simulation mode)
 *   2. FPGA emulation (bitstream generation)
 *   3. ASIC synthesis (GDSII layout via open PDK)
 *
 * Second quantization: each device is a quantum field of logic gates,
 * where creation/annihilation operators map to gate enable/disable.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef RTL_DEVICE_H
#define RTL_DEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"
#include "edp_risk.h"

/* ===== HDL Target Languages ===== */

typedef enum {
    HDL_CHISEL         = 0,  /* Scala-based HDL */
    HDL_SYSTEMVERILOG  = 1,  /* Industry standard */
    HDL_VHDL           = 2,  /* IEEE 1076 */
    HDL_VERILOG        = 3,  /* IEEE 1364 */
    HDL_PYMTL          = 4,  /* Python-based HDL */
    HDL_AMARANTH       = 5,  /* Python-based nMigen successor */
    HDL_MAX            = 6
} hdl_lang_t;

/* ===== Bus Interface Types ===== */

typedef enum {
    BUS_AXI4      = 0,
    BUS_APB       = 1,
    BUS_AHB       = 2,
    BUS_WISHBONE  = 3,
    BUS_TILELINK  = 4,
    BUS_NOC       = 5,  /* Network-on-Chip */
    BUS_MAX       = 6
} bus_type_t;

/* ===== RTL Register ===== */

typedef struct {
    uint32_t address;           /* Memory-mapped address */
    uint32_t width;             /* Bit width (8, 16, 32, 64, 168) */
    bool readable;
    bool writable;
    uint64_t reset_value;       /* Power-on reset value */
    uint64_t current_value;     /* Current register state */
    char name[32];              /* Register name */
} rtl_register_t;

/* ===== RTL DMA Channel ===== */

typedef struct {
    uint32_t base_addr;         /* Base address */
    uint32_t depth;             /* Buffer depth (entries) */
    uint32_t width;             /* Entry width (bits) */
    uint32_t head;
    uint32_t tail;
    bool circular;
    char name[32];
} rtl_dma_t;

/* ===== RTL IRQ Line ===== */

typedef struct {
    uint32_t irq_number;
    bool level_triggered;
    bool active;
    bool pending;
    char name[32];
} rtl_irq_t;

/* ===== RTL Device ===== */

typedef struct {
    uint32_t device_id;
    char module_name[64];
    hdl_lang_t target_lang;
    bus_type_t bus;
    
    /* Clock domain */
    uint32_t clock_domain;      /* Clock domain ID */
    uint32_t clock_freq_hz;     /* Operating frequency */
    
    /* Registers */
    rtl_register_t registers[64];
    uint32_t num_registers;
    
    /* DMA channels */
    rtl_dma_t dma_channels[8];
    uint32_t num_dma;
    
    /* IRQ lines */
    rtl_irq_t irq_lines[16];
    uint32_t num_irqs;
    
    /* Data paths (combinational) */
    uint32_t datapath_width;    /* Main data path width */
    bool pipeline_stages;       /* Has pipeline registers */
    uint32_t num_pipeline_stages;
    
    /* M⁵ coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
    
    /* Second quantization: gate field */
    uint32_t total_gates;       /* Estimated gate count */
    uint32_t active_gates;      /* Currently active gates */
    
    /* Status */
    bool powered_on;
    bool clock_enabled;
    bool reset_asserted;
    
    /* Synthesis targets */
    bool fpga_target;
    bool asic_target;
    uint32_t target_process_nm; /* Process node (e.g., 7, 14, 28) */
} rtl_device_t;

/* ===== RTL Device Registry ===== */

typedef struct {
    rtl_device_t devices[128];
    uint32_t num_devices;
    
    /* System bus map */
    uint32_t next_addr;
    
    /* Synthesis status */
    bool fpga_bitstream_ready;
    bool asic_gdsii_ready;
} rtl_registry_t;

/* ===== API ===== */

void rtl_registry_init(rtl_registry_t *reg);

/* Create device — like instantiating a hardware module */
uint32_t rtl_device_create(rtl_registry_t *reg,
                            const char *module_name,
                            hdl_lang_t lang,
                            bus_type_t bus,
                            uint32_t clock_freq);

/* Add register to device */
int32_t rtl_device_add_register(rtl_device_t *dev,
                                 const char *name,
                                 uint32_t width,
                                 bool readable,
                                 bool writable,
                                 uint64_t reset_value);

/* Add DMA channel */
int32_t rtl_device_add_dma(rtl_device_t *dev,
                            const char *name,
                            uint32_t depth,
                            uint32_t width,
                            bool circular);

/* Add IRQ line */
int32_t rtl_device_add_irq(rtl_device_t *dev,
                            const char *name,
                            bool level_triggered);

/* Register read/write (bus transaction) */
uint64_t rtl_register_read(rtl_device_t *dev, uint32_t addr);
int32_t rtl_register_write(rtl_device_t *dev, uint32_t addr, uint64_t value);

/* DMA operations */
int32_t rtl_dma_push(rtl_device_t *dev, uint32_t channel, uint32_t data);
uint32_t rtl_dma_pop(rtl_device_t *dev, uint32_t channel);

/* IRQ operations */
void rtl_irq_raise(rtl_device_t *dev, uint32_t irq_num);
void rtl_irq_clear(rtl_device_t *dev, uint32_t irq_num);
bool rtl_irq_pending(rtl_device_t *dev, uint32_t irq_num);

/* Second quantization: gate field operations */
void rtl_gate_create(rtl_device_t *dev, uint32_t count);  /* a† — enable gates */
void rtl_gate_annihilate(rtl_device_t *dev, uint32_t count);  /* a — disable gates */

/* Power management */
void rtl_device_power_on(rtl_device_t *dev);
void rtl_device_power_off(rtl_device_t *dev);
void rtl_device_reset(rtl_device_t *dev);

/* Coverage verification */
bool rtl_device_verify_coverage(rtl_device_t *dev);

/* Generate HDL output */
int32_t rtl_device_generate_hdl(const rtl_device_t *dev,
                                 hdl_lang_t target,
                                 char *output,
                                 uint32_t max_len);

/* Names */
const char *hdl_lang_name(hdl_lang_t l);
const char *bus_type_name(bus_type_t b);

#endif /* RTL_DEVICE_H */
