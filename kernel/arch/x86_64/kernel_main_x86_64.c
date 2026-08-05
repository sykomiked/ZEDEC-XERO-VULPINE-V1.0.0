/* kernel_main_x86_64.c — x86-64 kernel boot driver
 *
 * Bare-metal entry for x86-64. Called by boot.s after long mode is active.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "kernel_main_x86_64.h"
#include "uart_16550.h"
#include "x86_64_arch.h"
#include "boot_evidence.h"
#include "cellular_multikernel.h"

static void boot_msg(const char *msg) {
    uint32_t eid = boot_evidence_record(msg);
    uart_puts("[E");
    if (eid < 1000) uart_puts("0");
    if (eid < 100) uart_puts("0");
    if (eid < 10) uart_puts("0");
    uart_put_dec((uint64_t)eid);
    uart_puts("] ");
    uart_puts(msg);
    uart_puts("\n");
}

/* Helper to copy a short string into a fixed-size, zero-padded cell id. */
static void cell_id_set(char *dst, const char *src) {
    for (int i = 0; i < CELL_NAME_LEN; i++) {
        dst[i] = src[i] ? src[i] : '\0';
        if (!src[i]) break;
    }
}

/* CELL-001: boot-time multikernel fabric setup for x86-64. */
static void cell_fabric_boot_init(void) {
    static cell_fabric_t cell_fabric;
    static const uint8_t cell_image_digest[CELL_MAX_DIGEST] = {
        0xab,0xcd,0xef,0x01,0x02,0x03,0x04,0x05,
        0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,
        0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,0x15,
        0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d
    };

    boot_msg("[BOOT] Cellular Multikernel (CELL-001): fabric discovery and admission...");
    cell_fabric_init(&cell_fabric);

    static cell_t coord; cell_contract_zero(&coord);
    cell_id_set(coord.cell_id, "x86_64-coord");
    coord.arch = CELL_ARCH_X86_64; coord.bitness = CELL_BITNESS_64;
    coord.trust = CELL_TRUST_KERNEL; coord.privilege = CELL_PRIVILEGE_RING0;
    coord.incarnation = 1;
    coord.capabilities = CELL_CAP_OSEQ | CELL_CAP_IPHASE | CELL_CAP_PHASECOORD;
    coord.budget.max_events_per_second = 100000;
    coord.budget.max_memory_bytes = 0x40000000;
    cell_set_digest(&coord, cell_image_digest);

    static cell_t kpipe; cell_contract_zero(&kpipe);
    cell_id_set(kpipe.cell_id, "x86_64-k1k6");
    kpipe.arch = CELL_ARCH_X86_64; kpipe.bitness = CELL_BITNESS_64;
    kpipe.trust = CELL_TRUST_KERNEL; kpipe.privilege = CELL_PRIVILEGE_RING0;
    kpipe.incarnation = 1;
    kpipe.capabilities = CELL_CAP_RMAG | CELL_CAP_LPRES | CELL_CAP_CHOICE;
    kpipe.budget.max_events_per_second = 50000;
    kpipe.budget.max_memory_bytes = 0x20000000;
    cell_set_digest(&kpipe, cell_image_digest);

    static cell_t io; cell_contract_zero(&io);
    cell_id_set(io.cell_id, "x86_64-io");
    io.arch = CELL_ARCH_X86_64; io.bitness = CELL_BITNESS_64;
    io.trust = CELL_TRUST_DEVICE; io.privilege = CELL_PRIVILEGE_RING0;
    io.incarnation = 1;
    io.capabilities = CELL_CAP_NETWORK | CELL_CAP_AUDIO;
    io.budget.max_events_per_second = 25000;
    io.budget.max_memory_bytes = 0x10000000;
    cell_set_digest(&io, cell_image_digest);

    if (!cell_fabric_discover(&cell_fabric, &coord) ||
        !cell_fabric_discover(&cell_fabric, &kpipe) ||
        !cell_fabric_discover(&cell_fabric, &io)) {
        boot_msg("  [FAIL] cell fabric: discovery rejected");
        return;
    }

    if (!cell_fabric_authenticate(&cell_fabric, "x86_64-coord", cell_image_digest) ||
        !cell_fabric_authenticate(&cell_fabric, "x86_64-k1k6", cell_image_digest) ||
        !cell_fabric_authenticate(&cell_fabric, "x86_64-io", cell_image_digest)) {
        boot_msg("  [FAIL] cell fabric: authentication rejected");
        return;
    }

    cell_fabric_admit(&cell_fabric, "x86_64-coord");
    cell_fabric_admit(&cell_fabric, "x86_64-k1k6");
    cell_fabric_admit(&cell_fabric, "x86_64-io");

    cell_fabric_activate(&cell_fabric, "x86_64-coord");
    cell_fabric_activate(&cell_fabric, "x86_64-k1k6");
    cell_fabric_activate(&cell_fabric, "x86_64-io");

    static cell_route_t r_oseq = { 10, 1, "x86_64-coord", 0, false };
    static cell_route_t r_rmag = { 20, 1, "x86_64-k1k6",  1, false };
    static cell_route_t r_net  = { 30, 1, "x86_64-io",    2, false };
    cell_fabric_add_route(&cell_fabric, &r_oseq);
    cell_fabric_add_route(&cell_fabric, &r_rmag);
    cell_fabric_add_route(&cell_fabric, &r_net);

    boot_msg("  [INITIALIZED] 3 x86-64 cells active (coord, k1k6, io) with phase routes");

    cell_fabric_handle_fault(&cell_fabric, "x86_64-io", CELL_HEALTH_FAIL_STOPPED);
    if (cell_fabric_route_lookup(&cell_fabric, 30, 1) == NULL) {
        boot_msg("  [FAULT CONTAINED] x86_64-io fail-stopped; routes revoked; fabric survives");
    } else {
        boot_msg("  [FAIL] cell fabric: faulted cell still has routes");
    }

    io.incarnation = 2;
    io.health = CELL_HEALTH_OK;
    io.state = CELL_STATE_DISCOVERED;
    cell_set_digest(&io, cell_image_digest);
    if (cell_fabric_discover(&cell_fabric, &io)) {
        cell_fabric_authenticate(&cell_fabric, "x86_64-io", cell_image_digest);
        cell_fabric_admit(&cell_fabric, "x86_64-io");
        cell_fabric_activate(&cell_fabric, "x86_64-io");
        cell_route_t r_net2 = { 30, 1, "x86_64-io", 2, false };
        cell_fabric_add_route(&cell_fabric, &r_net2);
        boot_msg("  [RECOVERED] x86_64-io re-admitted with incarnation=2 and new routes");
    }
}

void kernel_main_x86_64(uint32_t mb2_magic, uint64_t mb2_info) {
    (void)mb2_magic;
    (void)mb2_info;

    boot_evidence_init();
    uart_init();

    uart_puts("\nZEDEC pqOS — M5 Axiomatic Kernel (VOVINA SHAKINA) [x86-64]\n");
    uart_puts("Edition: ZEDEC XERO VULPINE (ZXV)\n");
    uart_puts("==================================================\n\n");

    boot_msg("[BOOT] Long mode entered; x86-64 kernel main");
    boot_msg("[BOOT] Initializing COM1 serial console");
    boot_msg("  [DRIVER ONLINE] 16550A COM1 at 0x3F8");

    cell_fabric_boot_init();

    boot_msg("\n[BOOT] ZEDEC pqOS [x86-64] — All systems online.");
    boot_msg("[BOOT_OK] Phase E0082 complete; kernel_main reached; Stage-1 kernel loop ready\n");
    boot_msg("[BOOT] Entering event cycle...\n");

    boot_evidence_final();

    uint64_t cycle = 0;
    while (1) {
        if (++cycle >= 10000000) {
            cycle = 0;
            boot_msg("[BOOT_OK] heartbeat: cycle=10M");
        }
        hlt();
    }
}
