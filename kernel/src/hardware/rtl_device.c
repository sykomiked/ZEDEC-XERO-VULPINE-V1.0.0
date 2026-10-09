/* rtl_device.c — Hardware-as-Code RTL/HDL Abstraction Layer
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "rtl_device.h"

#ifdef TEST_HOST
#include <stdio.h>
#else
#include "freestanding.h"
#endif

static const char *hdl_names[] = {
    "Chisel", "SystemVerilog", "VHDL", "Verilog", "PyMTL", "Amaranth"
};

static const char *bus_names[] = {
    "AXI4", "APB", "AHB", "Wishbone", "TileLink", "NoC"
};

const char *hdl_lang_name(hdl_lang_t l) {
    if (l < HDL_MAX) return hdl_names[l];
    return "Unknown";
}

const char *bus_type_name(bus_type_t b) {
    if (b < BUS_MAX) return bus_names[b];
    return "Unknown";
}

void rtl_registry_init(rtl_registry_t *reg) {
    reg->num_devices = 0;
    reg->next_addr = 0x10000000; /* Standard peripheral base */
    reg->fpga_bitstream_ready = false;
    reg->asic_gdsii_ready = false;
}

uint32_t rtl_device_create(rtl_registry_t *reg,
                            const char *module_name,
                            hdl_lang_t lang,
                            bus_type_t bus,
                            uint32_t clock_freq) {
    if (reg->num_devices >= 128) return 0xFFFFFFFF;
    rtl_device_t *dev = &reg->devices[reg->num_devices];
    
    dev->device_id = reg->num_devices;
    int i;
    for (i = 0; i < 63 && module_name && module_name[i]; i++)
        dev->module_name[i] = module_name[i];
    dev->module_name[i] = 0;
    
    dev->target_lang = lang;
    dev->bus = bus;
    dev->clock_domain = reg->num_devices;
    dev->clock_freq_hz = clock_freq;
    dev->num_registers = 0;
    dev->num_dma = 0;
    dev->num_irqs = 0;
    dev->datapath_width = 32;
    dev->pipeline_stages = false;
    dev->num_pipeline_stages = 0;
    
    /* M⁵ */
    dev->m5.omega = reg->num_devices;
    dev->m5.r = SR_ONE;
    dev->m5.ell = SR_ONE;
    dev->m5.phi = SR_ZERO;
    dev->m5.chi = 0;
    dev->coverage_ratio = SR_ONE;
    
    /* Second quantization */
    dev->total_gates = 0;
    dev->active_gates = 0;
    
    dev->powered_on = false;
    dev->clock_enabled = false;
    dev->reset_asserted = true;
    dev->fpga_target = true;
    dev->asic_target = false;
    dev->target_process_nm = 28;
    
    return reg->num_devices++;
}

int32_t rtl_device_add_register(rtl_device_t *dev,
                                 const char *name,
                                 uint32_t width,
                                 bool readable,
                                 bool writable,
                                 uint64_t reset_value) {
    if (dev->num_registers >= 64) return -1;
    rtl_register_t *reg = &dev->registers[dev->num_registers];
    reg->address = dev->num_registers * 4; /* 4-byte aligned */
    reg->width = width;
    reg->readable = readable;
    reg->writable = writable;
    reg->reset_value = reset_value;
    reg->current_value = reset_value;
    
    int i;
    for (i = 0; i < 31 && name && name[i]; i++)
        reg->name[i] = name[i];
    reg->name[i] = 0;
    
    /* Estimate gates: each register bit ≈ 4 FFs + logic */
    dev->total_gates += width * 4;
    dev->active_gates = dev->total_gates;
    
    return dev->num_registers++;
}

int32_t rtl_device_add_dma(rtl_device_t *dev,
                            const char *name,
                            uint32_t depth,
                            uint32_t width,
                            bool circular) {
    if (dev->num_dma >= 8) return -1;
    rtl_dma_t *dma = &dev->dma_channels[dev->num_dma];
    dma->base_addr = dev->num_dma * 0x1000;
    dma->depth = depth;
    dma->width = width;
    dma->head = 0;
    dma->tail = 0;
    dma->circular = circular;
    
    int i;
    for (i = 0; i < 31 && name && name[i]; i++)
        dma->name[i] = name[i];
    dma->name[i] = 0;
    
    /* Estimate gates: DMA buffer ≈ depth × width FFs */
    dev->total_gates += depth * width;
    dev->active_gates = dev->total_gates;
    
    return dev->num_dma++;
}

int32_t rtl_device_add_irq(rtl_device_t *dev,
                            const char *name,
                            bool level_triggered) {
    if (dev->num_irqs >= 16) return -1;
    rtl_irq_t *irq = &dev->irq_lines[dev->num_irqs];
    irq->irq_number = dev->num_irqs;
    irq->level_triggered = level_triggered;
    irq->active = false;
    irq->pending = false;
    
    int i;
    for (i = 0; i < 31 && name && name[i]; i++)
        irq->name[i] = name[i];
    irq->name[i] = 0;
    
    return dev->num_irqs++;
}

uint64_t rtl_register_read(rtl_device_t *dev, uint32_t addr) {
    uint32_t i;
    for (i = 0; i < dev->num_registers; i++) {
        if (dev->registers[i].address == addr && dev->registers[i].readable) {
            return dev->registers[i].current_value;
        }
    }
    return 0;
}

int32_t rtl_register_write(rtl_device_t *dev, uint32_t addr, uint64_t value) {
    uint32_t i;
    for (i = 0; i < dev->num_registers; i++) {
        if (dev->registers[i].address == addr && dev->registers[i].writable) {
            dev->registers[i].current_value = value;
            return 0;
        }
    }
    return -1;
}

int32_t rtl_dma_push(rtl_device_t *dev, uint32_t channel, uint32_t data) {
    (void)data;
    if (channel >= dev->num_dma) return -1;
    rtl_dma_t *dma = &dev->dma_channels[channel];
    uint32_t next = (dma->tail + 1) % dma->depth;
    if (!dma->circular && next == dma->head) return -1; /* Full */
    /* Store data (simplified — would be memory-mapped) */
    dma->tail = next;
    return 0;
}

uint32_t rtl_dma_pop(rtl_device_t *dev, uint32_t channel) {
    if (channel >= dev->num_dma) return 0;
    rtl_dma_t *dma = &dev->dma_channels[channel];
    if (dma->head == dma->tail) return 0; /* Empty */
    uint32_t data = 0; /* Would read from buffer */
    dma->head = (dma->head + 1) % dma->depth;
    return data;
}

void rtl_irq_raise(rtl_device_t *dev, uint32_t irq_num) {
    if (irq_num >= dev->num_irqs) return;
    dev->irq_lines[irq_num].active = true;
    dev->irq_lines[irq_num].pending = true;
}

void rtl_irq_clear(rtl_device_t *dev, uint32_t irq_num) {
    if (irq_num >= dev->num_irqs) return;
    dev->irq_lines[irq_num].pending = false;
    if (!dev->irq_lines[irq_num].level_triggered) {
        dev->irq_lines[irq_num].active = false;
    }
}

bool rtl_irq_pending(rtl_device_t *dev, uint32_t irq_num) {
    if (irq_num >= dev->num_irqs) return false;
    return dev->irq_lines[irq_num].pending;
}

/* Second quantization: gate field operations */
void rtl_gate_create(rtl_device_t *dev, uint32_t count) {
    /* a†: enable gates — power up logic blocks */
    dev->active_gates += count;
    if (dev->active_gates > dev->total_gates)
        dev->active_gates = dev->total_gates;
}

void rtl_gate_annihilate(rtl_device_t *dev, uint32_t count) {
    /* a: disable gates — power down logic blocks */
    if (count >= dev->active_gates) {
        dev->active_gates = 0;
    } else {
        dev->active_gates -= count;
    }
}

void rtl_device_power_on(rtl_device_t *dev) {
    dev->powered_on = true;
    dev->clock_enabled = true;
    dev->reset_asserted = false;
    /* All gates active */
    dev->active_gates = dev->total_gates;
}

void rtl_device_power_off(rtl_device_t *dev) {
    dev->powered_on = false;
    dev->clock_enabled = false;
    dev->active_gates = 0;
}

void rtl_device_reset(rtl_device_t *dev) {
    dev->reset_asserted = true;
    uint32_t i;
    for (i = 0; i < dev->num_registers; i++) {
        dev->registers[i].current_value = dev->registers[i].reset_value;
    }
    for (i = 0; i < dev->num_dma; i++) {
        dev->dma_channels[i].head = 0;
        dev->dma_channels[i].tail = 0;
    }
    for (i = 0; i < dev->num_irqs; i++) {
        dev->irq_lines[i].active = false;
        dev->irq_lines[i].pending = false;
    }
    dev->reset_asserted = false;
}

bool rtl_device_verify_coverage(rtl_device_t *dev) {
    surplus_real_t product = SR_MUL(dev->m5.r, dev->m5.ell);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10));
    dev->coverage_ratio = SR_DIV(product, floor);
    return SR_CMP(product, floor) >= 0;
}

int32_t rtl_device_generate_hdl(const rtl_device_t *dev,
                                 hdl_lang_t target,
                                 char *output,
                                 uint32_t max_len) {
    if (!output || max_len == 0) return -1;
    
    uint32_t pos = 0;
    
    switch (target) {
        case HDL_SYSTEMVERILOG:
        case HDL_VERILOG: {
            pos +=
                snprintf(output + pos, max_len - pos,
                         "// Auto-generated RTL: %s\n// License: Apache-2.0\n", dev->module_name);
            pos += snprintf(output + pos, max_len - pos,
                "module %s (\n", dev->module_name);
            pos += snprintf(output + pos, max_len - pos,
                "  input  logic clk,\n  input  logic rst_n,\n");
            pos += snprintf(output + pos, max_len - pos,
                "  // Bus: %s\n", bus_type_name(dev->bus));
            pos += snprintf(output + pos, max_len - pos,
                "  // Clock: %u Hz\n", dev->clock_freq_hz);
            pos += snprintf(output + pos, max_len - pos,
                ");\n\n");
            
            /* Registers */
            uint32_t i;
            for (i = 0; i < dev->num_registers; i++) {
                pos += snprintf(output + pos, max_len - pos,
                    "  logic [%u:0] %s;\n",
                    dev->registers[i].width - 1,
                    dev->registers[i].name);
            }
            
            /* DMA */
            for (i = 0; i < dev->num_dma; i++) {
                pos += snprintf(output + pos, max_len - pos,
                    "  logic [%u:0] %s_mem [0:%u];\n",
                    dev->dma_channels[i].width - 1,
                    dev->dma_channels[i].name,
                    dev->dma_channels[i].depth - 1);
            }
            
            pos += snprintf(output + pos, max_len - pos,
                "\n  // Gate count: %u\n", dev->total_gates);
            pos += snprintf(output + pos, max_len - pos,
                "endmodule\n");
            break;
        }
        case HDL_VHDL: {
            pos +=
                snprintf(output + pos, max_len - pos,
                         "-- Auto-generated RTL: %s\n-- License: Apache-2.0\n", dev->module_name);
            pos += snprintf(output + pos, max_len - pos,
                "library ieee;\nuse ieee.std_logic_1164.all;\n\n");
            pos += snprintf(output + pos, max_len - pos,
                "entity %s is\n", dev->module_name);
            pos += snprintf(output + pos, max_len - pos,
                "  port (\n    clk : in std_logic;\n    rst_n : in std_logic\n");
            pos += snprintf(output + pos, max_len - pos,
                "  );\nend entity;\n\n");
            pos += snprintf(output + pos, max_len - pos,
                "architecture rtl of %s is\n", dev->module_name);
            pos += snprintf(output + pos, max_len - pos,
                "begin\nend architecture;\n");
            break;
        }
        case HDL_CHISEL: {
            pos +=
                snprintf(output + pos, max_len - pos,
                         "// Auto-generated RTL: %s\n// License: Apache-2.0\n", dev->module_name);
            pos += snprintf(output + pos, max_len - pos,
                "import chisel3._\nimport chisel3.util._\n\n");
            pos += snprintf(output + pos, max_len - pos,
                "class %s extends Module {\n", dev->module_name);
            pos += snprintf(output + pos, max_len - pos,
                "  val io = IO(new Bundle {\n");
            pos += snprintf(output + pos, max_len - pos,
                "    // Bus: %s, Clock: %u Hz\n",
                bus_type_name(dev->bus), dev->clock_freq_hz);
            pos += snprintf(output + pos, max_len - pos,
                "  })\n\n");
            pos += snprintf(output + pos, max_len - pos,
                "  // Gate count: %u\n", dev->total_gates);
            pos += snprintf(output + pos, max_len - pos,
                "}\n");
            break;
        }
        default:
            pos += snprintf(output, max_len,
                "// HDL generation for %s not yet implemented\n",
                hdl_lang_name(target));
            break;
    }
    
    return (int32_t)pos;
}
