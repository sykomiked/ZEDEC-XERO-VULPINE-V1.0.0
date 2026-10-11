/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_rtl_device.c — known-answer tests for the RTL/HDL abstraction layer
 * (rtl_device.h): name tables and their out-of-range fallbacks, registry and
 * device defaults, the 128/64/8/16 capacity limits, register addressing and
 * access rights, the gate-count estimates, DMA ring full/empty/wrap, edge vs
 * level IRQ clearing, gate create/annihilate clamping, power and reset, the
 * r*ell >= 1.8 coverage boundary, and the exact HDL text in every language
 * including truncation into a short buffer. */
#include <stdio.h>
#include <string.h>
#include "rtl_device.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);                                   \
        }                                                                                          \
    } while (0)

static rtl_registry_t R;

static void test_names(void)
{
    static const char *hdl[] = {"Chisel", "SystemVerilog", "VHDL", "Verilog", "PyMTL", "Amaranth"};
    static const char *bus[] = {"AXI4", "APB", "AHB", "Wishbone", "TileLink", "NoC"};
    for (int i = 0; i < 6; i++) {
        CHECK(strcmp(hdl_lang_name((hdl_lang_t) i), hdl[i]) == 0, "hdl name");
        CHECK(strcmp(bus_type_name((bus_type_t) i), bus[i]) == 0, "bus name");
    }
    CHECK(strcmp(hdl_lang_name(HDL_MAX), "Unknown") == 0, "HDL_MAX is unknown");
    CHECK(strcmp(bus_type_name(BUS_MAX), "Unknown") == 0, "BUS_MAX is unknown");
    CHECK(strcmp(hdl_lang_name((hdl_lang_t) 77), "Unknown") == 0, "hdl 77 is unknown");
    CHECK(strcmp(bus_type_name((bus_type_t) 77), "Unknown") == 0, "bus 77 is unknown");
}

static void test_registry(void)
{
    memset(&R, 0xA5, sizeof R);
    rtl_registry_init(&R);
    CHECK(R.num_devices == 0, "init: no devices");
    CHECK(R.next_addr == 0x10000000u, "init: peripheral base");
    CHECK(!R.fpga_bitstream_ready, "init: no bitstream");
    CHECK(!R.asic_gdsii_ready, "init: no gdsii");

    uint32_t id = rtl_device_create(&R, "uart0", HDL_VHDL, BUS_APB, 50000000u);
    CHECK(id == 0, "first id 0");
    CHECK(R.num_devices == 1, "count 1");
    rtl_device_t *d = &R.devices[0];
    CHECK(d->device_id == 0, "device_id");
    CHECK(strcmp(d->module_name, "uart0") == 0, "module name");
    CHECK(d->target_lang == HDL_VHDL, "lang");
    CHECK(d->bus == BUS_APB, "bus");
    CHECK(d->clock_domain == 0, "clock domain");
    CHECK(d->clock_freq_hz == 50000000u, "clock freq");
    CHECK(d->num_registers == 0 && d->num_dma == 0 && d->num_irqs == 0, "empty");
    CHECK(d->datapath_width == 32, "datapath 32");
    CHECK(!d->pipeline_stages && d->num_pipeline_stages == 0, "no pipeline");
    CHECK(d->m5.omega == 0, "omega");
    CHECK(d->m5.r == SR_ONE && d->m5.ell == SR_ONE && d->m5.phi == SR_ZERO, "m5 r ell phi");
    CHECK(d->m5.chi == 0, "chi");
    CHECK(d->coverage_ratio == SR_ONE, "coverage 1");
    CHECK(d->total_gates == 0 && d->active_gates == 0, "no gates");
    CHECK(!d->powered_on && !d->clock_enabled && d->reset_asserted, "power-on state");
    CHECK(d->fpga_target && !d->asic_target, "targets");
    CHECK(d->target_process_nm == 28, "28 nm");

    id = rtl_device_create(&R, NULL, HDL_CHISEL, BUS_AXI4, 1);
    CHECK(id == 1, "second id 1");
    CHECK(R.devices[1].module_name[0] == 0, "NULL name is empty");
    CHECK(R.devices[1].clock_domain == 1 && R.devices[1].m5.omega == 1, "domain/omega follow id");

    char longname[100];
    memset(longname, 'x', sizeof longname);
    longname[99] = 0;
    id = rtl_device_create(&R, longname, HDL_CHISEL, BUS_AXI4, 1);
    CHECK(id == 2, "third id");
    CHECK(strlen(R.devices[2].module_name) == 63, "name truncated to 63");

    while (R.num_devices < 127) rtl_device_create(&R, "d", HDL_CHISEL, BUS_AXI4, 1);
    id = rtl_device_create(&R, "last", HDL_CHISEL, BUS_AXI4, 1);
    CHECK(id == 127, "128th device fits");
    CHECK(rtl_device_create(&R, "over", HDL_CHISEL, BUS_AXI4, 1) == 0xFFFFFFFFu, "129th refused");
    CHECK(R.num_devices == 128, "count stays 128");
}

static rtl_device_t *fresh(void)
{
    rtl_registry_init(&R);
    rtl_device_create(&R, "dut", HDL_SYSTEMVERILOG, BUS_AXI4, 100);
    return &R.devices[0];
}

static void test_registers(void)
{
    rtl_device_t *d = fresh();
    CHECK(rtl_device_add_register(d, "ctrl", 32, true, true, 0x11) == 0, "reg 0");
    CHECK(rtl_device_add_register(d, "status", 8, true, false, 0x22) == 1, "reg 1");
    CHECK(rtl_device_add_register(d, "cmd", 16, false, true, 0x33) == 2, "reg 2");
    CHECK(d->registers[0].address == 0 && d->registers[1].address == 4 &&
              d->registers[2].address == 8,
          "4-byte aligned addresses");
    CHECK(d->registers[1].width == 8, "width");
    CHECK(d->registers[1].readable && !d->registers[1].writable, "rights");
    CHECK(d->registers[2].reset_value == 0x33 && d->registers[2].current_value == 0x33, "reset");
    CHECK(strcmp(d->registers[1].name, "status") == 0, "reg name");
    CHECK(d->total_gates == (32 + 8 + 16) * 4, "gates = 4 per bit");
    CHECK(d->active_gates == d->total_gates, "all active");

    CHECK(rtl_register_read(d, 0) == 0x11, "read ctrl");
    CHECK(rtl_register_read(d, 4) == 0x22, "read status");
    CHECK(rtl_register_read(d, 8) == 0, "write-only reads 0");
    CHECK(rtl_register_read(d, 2) == 0, "unaligned reads 0");
    CHECK(rtl_register_read(d, 12) == 0, "unmapped reads 0");

    CHECK(rtl_register_write(d, 0, 0xDEADBEEFCAFEull) == 0, "write ctrl");
    CHECK(rtl_register_read(d, 0) == 0xDEADBEEFCAFEull, "readback 64-bit");
    CHECK(rtl_register_write(d, 4, 9) == -1, "read-only refuses");
    CHECK(d->registers[1].current_value == 0x22, "read-only unchanged");
    CHECK(rtl_register_write(d, 8, 0x77) == 0, "write-only accepts");
    CHECK(d->registers[2].current_value == 0x77, "write-only stored");
    CHECK(rtl_register_write(d, 12, 1) == -1, "unmapped write refused");

    /* a stale slot just past num_registers must never be matched */
    d->registers[3].address = 12;
    d->registers[3].readable = true;
    d->registers[3].writable = true;
    d->registers[3].current_value = 0x5555;
    CHECK(rtl_register_read(d, 12) == 0, "slot past the count is not read");
    CHECK(rtl_register_write(d, 12, 1) == -1, "slot past the count is not written");
    CHECK(d->registers[3].current_value == 0x5555, "stale slot untouched");

    rtl_device_t *e = fresh();
    char nm[40];
    memset(nm, 'r', sizeof nm);
    nm[39] = 0;
    CHECK(rtl_device_add_register(e, nm, 1, true, true, 0) == 0, "long reg name");
    CHECK(strlen(e->registers[0].name) == 31, "reg name truncated to 31");
    CHECK(rtl_device_add_register(e, NULL, 1, true, true, 0) == 1, "NULL reg name");
    CHECK(e->registers[1].name[0] == 0, "NULL reg name empty");
    while (e->num_registers < 63) rtl_device_add_register(e, "r", 1, true, true, 0);
    CHECK(rtl_device_add_register(e, "r63", 2, true, true, 5) == 63, "64th fits");
    CHECK(e->registers[63].address == 252, "last address");
    CHECK(rtl_register_read(e, 252) == 5, "read last register");
    CHECK(rtl_device_add_register(e, "r64", 1, true, true, 0) == -1, "65th refused");
    CHECK(e->num_registers == 64, "count 64");
    CHECK(e->total_gates == 63 * 4 + 8, "gate total");
}

static void test_dma(void)
{
    rtl_device_t *d = fresh();
    CHECK(rtl_device_add_dma(d, "z", 0, 8, false) == -1, "depth 0 refused");
    CHECK(rtl_device_add_dma(d, "z", 8, 0, false) == -1, "width 0 refused");
    CHECK(d->num_dma == 0 && d->total_gates == 0, "nothing added");
    CHECK(rtl_device_add_dma(d, "rx", 4, 8, false) == 0, "dma 0");
    CHECK(rtl_device_add_dma(d, "tx", 3, 16, true) == 1, "dma 1");
    CHECK(d->dma_channels[0].base_addr == 0 && d->dma_channels[1].base_addr == 0x1000, "bases");
    CHECK(d->dma_channels[1].depth == 3 && d->dma_channels[1].width == 16, "geometry");
    CHECK(!d->dma_channels[0].circular && d->dma_channels[1].circular, "circular flag");
    CHECK(d->dma_channels[0].head == 0 && d->dma_channels[0].tail == 0, "empty ring");
    CHECK(strcmp(d->dma_channels[1].name, "tx") == 0, "dma name");
    CHECK(d->total_gates == 4 * 8 + 3 * 16 && d->active_gates == 80, "depth*width gates");

    /* non-circular, depth 4: holds 3 */
    CHECK(rtl_dma_push(d, 0, 1) == 0, "push 1");
    CHECK(rtl_dma_push(d, 0, 2) == 0, "push 2");
    CHECK(rtl_dma_push(d, 0, 3) == 0, "push 3");
    CHECK(d->dma_channels[0].tail == 3, "tail 3");
    CHECK(rtl_dma_push(d, 0, 4) == -1, "full");
    CHECK(d->dma_channels[0].tail == 3, "tail unchanged when full");
    CHECK(rtl_dma_pop(d, 0) == 0, "pop");
    CHECK(d->dma_channels[0].head == 1, "head 1");
    CHECK(rtl_dma_push(d, 0, 5) == 0, "room again");
    CHECK(d->dma_channels[0].tail == 0, "tail wraps to 0");
    CHECK(rtl_dma_push(d, 0, 6) == -1, "full again");
    rtl_dma_pop(d, 0);
    rtl_dma_pop(d, 0);
    rtl_dma_pop(d, 0);
    CHECK(d->dma_channels[0].head == 0, "head wraps to 0");
    uint32_t h = d->dma_channels[0].head;
    CHECK(rtl_dma_pop(d, 0) == 0 && d->dma_channels[0].head == h, "pop empty is no-op");

    /* circular, depth 3: never full */
    for (int i = 0; i < 5; i++) CHECK(rtl_dma_push(d, 1, 0) == 0, "circular push");
    CHECK(d->dma_channels[1].tail == 5 % 3, "circular tail");
    CHECK(rtl_dma_pop(d, 1) == 0 && d->dma_channels[1].head == 1, "circular pop");

    CHECK(rtl_dma_push(d, 2, 0) == -1, "bad channel push");
    d->dma_channels[2].depth = 4;
    d->dma_channels[2].head = 1;
    d->dma_channels[2].tail = 3;
    CHECK(rtl_dma_pop(d, 2) == 0, "bad channel pop");
    CHECK(d->dma_channels[2].head == 1, "bad channel pop leaves stale slot");
    CHECK(rtl_dma_push(d, 2, 0) == -1 && d->dma_channels[2].tail == 3, "bad push leaves slot");

    char dn[40];
    memset(dn, 'q', sizeof dn);
    dn[39] = 0;
    CHECK(rtl_device_add_dma(d, dn, 1, 1, false) == 2, "long dma name");
    CHECK(strlen(d->dma_channels[2].name) == 31, "dma name truncated to 31");
    CHECK(d->dma_channels[2].head == 0 && d->dma_channels[2].tail == 0, "dma 2 fresh");
    while (d->num_dma < 7) rtl_device_add_dma(d, NULL, 1, 1, false);
    CHECK(d->dma_channels[3].name[0] == 0, "NULL dma name");
    CHECK(rtl_device_add_dma(d, "d7", 2, 2, false) == 7, "8th dma fits");
    CHECK(d->dma_channels[7].base_addr == 0x7000, "8th base");
    CHECK(rtl_device_add_dma(d, "d8", 2, 2, false) == -1, "9th refused");
    CHECK(d->num_dma == 8, "count 8");

    /* depth 1 non-circular: (0+1)%1 == head -> always full */
    CHECK(rtl_dma_push(d, 2, 0) == -1, "depth-1 ring is full");
}

static void test_irq(void)
{
    rtl_device_t *d = fresh();
    CHECK(rtl_device_add_irq(d, "edge", false) == 0, "irq 0");
    CHECK(rtl_device_add_irq(d, "level", true) == 1, "irq 1");
    CHECK(d->irq_lines[1].irq_number == 1 && d->irq_lines[1].level_triggered, "irq fields");
    CHECK(!d->irq_lines[0].active && !d->irq_lines[0].pending, "idle");
    CHECK(strcmp(d->irq_lines[0].name, "edge") == 0, "irq name");
    CHECK(!rtl_irq_pending(d, 0), "not pending");

    rtl_irq_raise(d, 0);
    rtl_irq_raise(d, 1);
    CHECK(rtl_irq_pending(d, 0) && rtl_irq_pending(d, 1), "both pending");
    CHECK(d->irq_lines[0].active && d->irq_lines[1].active, "both active");
    rtl_irq_clear(d, 0);
    rtl_irq_clear(d, 1);
    CHECK(!rtl_irq_pending(d, 0) && !rtl_irq_pending(d, 1), "both cleared");
    CHECK(!d->irq_lines[0].active, "edge deasserts");
    CHECK(d->irq_lines[1].active, "level stays active");

    /* raise on unregistered slot must not touch it */
    memset(&d->irq_lines[2], 0, sizeof d->irq_lines[2]);
    rtl_irq_raise(d, 2);
    CHECK(!d->irq_lines[2].pending && !d->irq_lines[2].active, "raise out of range ignored");
    d->irq_lines[2].pending = true;
    d->irq_lines[2].active = true;
    rtl_irq_clear(d, 2);
    CHECK(d->irq_lines[2].pending && d->irq_lines[2].active, "clear out of range ignored");
    CHECK(!rtl_irq_pending(d, 2), "pending out of range is false");

    char in[40];
    memset(in, 'i', sizeof in);
    in[39] = 0;
    CHECK(rtl_device_add_irq(d, in, false) == 2, "long irq name");
    CHECK(strlen(d->irq_lines[2].name) == 31, "irq name truncated to 31");
    CHECK(!d->irq_lines[2].pending && !d->irq_lines[2].active, "irq 2 idle");
    while (d->num_irqs < 15) rtl_device_add_irq(d, NULL, false);
    CHECK(d->irq_lines[3].name[0] == 0, "NULL irq name");
    CHECK(rtl_device_add_irq(d, "i15", false) == 15, "16th irq fits");
    rtl_irq_raise(d, 15);
    CHECK(rtl_irq_pending(d, 15), "last irq pending");
    CHECK(rtl_device_add_irq(d, "i16", false) == -1, "17th refused");
    CHECK(d->num_irqs == 16, "count 16");
}

static void test_gates_power(void)
{
    rtl_device_t *d = fresh();
    rtl_device_add_register(d, "a", 25, true, true, 7); /* 100 gates */
    rtl_device_add_dma(d, "b", 2, 2, false);            /* 4 gates */
    rtl_device_add_irq(d, "c", true);
    CHECK(d->total_gates == 104, "104 gates");
    rtl_device_add_register(d, "a2", 0, true, true, 8);
    rtl_device_add_dma(d, "b2", 3, 1, true);
    rtl_device_add_irq(d, "c2", false);
    d->total_gates = 104;
    d->active_gates = 104;

    rtl_gate_annihilate(d, 4);
    CHECK(d->active_gates == 100, "annihilate 4");
    rtl_gate_annihilate(d, 100);
    CHECK(d->active_gates == 0, "annihilate exact");
    rtl_gate_create(d, 30);
    CHECK(d->active_gates == 30, "create 30");
    rtl_gate_annihilate(d, 31);
    CHECK(d->active_gates == 0, "annihilate more than active clamps");
    rtl_gate_create(d, 104);
    CHECK(d->active_gates == 104, "create to exactly total");
    rtl_gate_create(d, 1);
    CHECK(d->active_gates == 104, "create clamps at total");
    rtl_gate_annihilate(d, 103);
    CHECK(d->active_gates == 1, "annihilate to 1");

    rtl_device_power_on(d);
    CHECK(d->powered_on && d->clock_enabled && !d->reset_asserted, "on");
    CHECK(d->active_gates == 104, "on: all gates");
    rtl_device_power_off(d);
    CHECK(!d->powered_on && !d->clock_enabled && d->active_gates == 0, "off");
    CHECK(!d->reset_asserted, "off keeps reset");

    rtl_register_write(d, 0, 99);
    rtl_dma_push(d, 0, 1);
    rtl_dma_pop(d, 0);
    rtl_irq_raise(d, 0);
    d->dma_channels[0].tail = 1;
    rtl_register_write(d, 4, 98);
    d->dma_channels[1].head = 2;
    d->dma_channels[1].tail = 1;
    rtl_irq_raise(d, 1);
    /* stale slots just past each count must survive a reset untouched */
    d->registers[2].reset_value = 1;
    d->registers[2].current_value = 2;
    d->dma_channels[2].head = 3;
    d->dma_channels[2].tail = 4;
    d->irq_lines[2].active = true;
    d->irq_lines[2].pending = true;
    rtl_device_reset(d);
    CHECK(rtl_register_read(d, 4) == 8, "reset restores second register");
    CHECK(d->dma_channels[1].head == 0 && d->dma_channels[1].tail == 0, "reset second dma");
    CHECK(!d->irq_lines[1].active && !d->irq_lines[1].pending, "reset second irq");
    CHECK(d->registers[2].current_value == 2, "reset stops at num_registers");
    CHECK(d->dma_channels[2].head == 3 && d->dma_channels[2].tail == 4, "reset stops at num_dma");
    CHECK(d->irq_lines[2].active && d->irq_lines[2].pending, "reset stops at num_irqs");
    CHECK(rtl_register_read(d, 0) == 7, "reset restores value");
    CHECK(d->dma_channels[0].head == 0 && d->dma_channels[0].tail == 0, "reset empties dma");
    CHECK(!d->irq_lines[0].active && !d->irq_lines[0].pending, "reset clears irq");
    CHECK(!d->reset_asserted, "reset released");
    CHECK(d->total_gates == 104, "reset keeps gates");
}

static void test_coverage(void)
{
    rtl_device_t *d = fresh();
    CHECK(!rtl_device_verify_coverage(d), "1*1 < 1.8");
    CHECK(d->coverage_ratio > 0.5555 && d->coverage_ratio < 0.5556, "ratio 1/1.8");
    d->m5.r = 1.8;
    CHECK(rtl_device_verify_coverage(d), "1.8*1 meets the floor");
    CHECK(d->coverage_ratio == 1.0, "ratio exactly 1");
    d->m5.r = 1.7999;
    CHECK(!rtl_device_verify_coverage(d), "just below");
    d->m5.r = 3.0;
    d->m5.ell = 1.2;
    CHECK(rtl_device_verify_coverage(d), "3*1.2 above");
    CHECK(d->coverage_ratio > 1.9999 && d->coverage_ratio < 2.0001, "ratio 2");
}

static rtl_device_t *hdl_dev(void)
{
    rtl_device_t *d = fresh();
    rtl_device_add_register(d, "ctrl", 32, true, true, 0);
    rtl_device_add_register(d, "flag", 1, true, true, 0);
    rtl_device_add_dma(d, "rx", 16, 8, false);
    rtl_device_add_dma(d, "tx", 2, 4, false);
    return d; /* gates 132 + 128 + 8 = 268 */
}

static const char SV[] = "// Auto-generated RTL: dut\n// License: Apache-2.0\n"
                         "module dut (\n"
                         "  input  logic clk,\n  input  logic rst_n,\n"
                         "  // Bus: AXI4\n"
                         "  // Clock: 100 Hz\n"
                         ");\n\n"
                         "  logic [31:0] ctrl;\n"
                         "  logic [0:0] flag;\n"
                         "  logic [7:0] rx_mem [0:15];\n"
                         "  logic [3:0] tx_mem [0:1];\n"
                         "\n  // Gate count: 268\n"
                         "endmodule\n";

static void test_hdl(void)
{
    static char out[2048];
    rtl_device_t *d = hdl_dev();
    CHECK(d->total_gates == 268, "hdl device gates");

    CHECK(rtl_device_generate_hdl(d, HDL_SYSTEMVERILOG, NULL, 10) == -1, "NULL out");
    CHECK(rtl_device_generate_hdl(d, HDL_SYSTEMVERILOG, out, 0) == -1, "zero len");

    int32_t n = rtl_device_generate_hdl(d, HDL_SYSTEMVERILOG, out, sizeof out);
    CHECK(strcmp(out, SV) == 0, "systemverilog text");
    CHECK(n == (int32_t) strlen(SV), "sv length");
    n = rtl_device_generate_hdl(d, HDL_VERILOG, out, sizeof out);
    CHECK(strcmp(out, SV) == 0 && n == (int32_t) strlen(SV), "verilog shares sv text");

    static const char VH[] = "-- Auto-generated RTL: dut\n-- License: Apache-2.0\n"
                             "library ieee;\nuse ieee.std_logic_1164.all;\n\n"
                             "entity dut is\n"
                             "  port (\n    clk : in std_logic;\n    rst_n : in std_logic\n"
                             "  );\nend entity;\n\n"
                             "architecture rtl of dut is\n"
                             "begin\nend architecture;\n";
    n = rtl_device_generate_hdl(d, HDL_VHDL, out, sizeof out);
    CHECK(strcmp(out, VH) == 0 && n == (int32_t) strlen(VH), "vhdl text");

    static const char CH[] = "// Auto-generated RTL: dut\n// License: Apache-2.0\n"
                             "import chisel3._\nimport chisel3.util._\n\n"
                             "class dut extends Module {\n"
                             "  val io = IO(new Bundle {\n"
                             "    // Bus: AXI4, Clock: 100 Hz\n"
                             "  })\n\n"
                             "  // Gate count: 268\n"
                             "}\n";
    n = rtl_device_generate_hdl(d, HDL_CHISEL, out, sizeof out);
    CHECK(strcmp(out, CH) == 0 && n == (int32_t) strlen(CH), "chisel text");

    static const char PY[] = "// HDL generation for PyMTL not yet implemented\n";
    n = rtl_device_generate_hdl(d, HDL_PYMTL, out, sizeof out);
    CHECK(strcmp(out, PY) == 0 && n == (int32_t) strlen(PY), "pymtl stub");
    n = rtl_device_generate_hdl(d, HDL_AMARANTH, out, sizeof out);
    CHECK(strcmp(out, "// HDL generation for Amaranth not yet implemented\n") == 0 && n == 51,
          "amaranth stub");

    /* truncation: the text stops at max_len-1 and pos never runs past it */
    char small[41];
    memset(small, '#', sizeof small);
    n = rtl_device_generate_hdl(d, HDL_SYSTEMVERILOG, small, 40);
    CHECK(n == 39, "truncated length is max_len-1");
    CHECK(strlen(small) == 39 && memcmp(small, SV, 39) == 0, "truncated prefix");
    CHECK(small[40] == '#', "nothing written past max_len");

    /* exact fit: buffer is strlen+1 -> whole text, no clamp */
    static char fit[sizeof SV];
    n = rtl_device_generate_hdl(d, HDL_SYSTEMVERILOG, fit, sizeof fit);
    CHECK(n == (int32_t) strlen(SV) && strcmp(fit, SV) == 0, "exact fit");
    /* one short: the final newline is dropped */
    n = rtl_device_generate_hdl(d, HDL_SYSTEMVERILOG, fit, sizeof fit - 1);
    CHECK(n == (int32_t) strlen(SV) - 1, "one short");
    CHECK(strncmp(fit, SV, strlen(SV) - 1) == 0 && fit[strlen(SV) - 1] == 0, "one short text");

    char one[2] = {'#', '#'};
    n = rtl_device_generate_hdl(d, HDL_VHDL, one, 1);
    CHECK(n == 0 && one[0] == 0 && one[1] == '#', "max_len 1 gives empty string");
}

int main(void)
{
    test_names();
    test_registry();
    test_registers();
    test_dma();
    test_irq();
    test_gates_power();
    test_coverage();
    test_hdl();
    printf("test_rtl_device: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
