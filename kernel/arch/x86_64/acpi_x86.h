/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* acpi_x86.h — ACPI on x86_64 bare metal: the RSDP from the UEFI handoff
 * record (or the legacy BIOS scan), the AML interpreter over real port I/O,
 * PCI configuration space and mapped memory, and the boot report.
 *
 * The boot never sleeps or powers off on its own. Under QEMU only (ACPI OEM
 * "BOCHS " and a fw_cfg signature), three fw_cfg files ask for a test action:
 *   opt/zxv/acpi_poweroff  "1": enter S5 right after the report
 *   opt/zxv/acpi_s3        "1": enter S3 right after the report
 *   opt/zxv/acpi_pwrbtn    "1": poll for the power button (up to 60 s), then S5 */
#ifndef ZXV_ACPI_X86_H
#define ZXV_ACPI_X86_H

#include "hwdrv/hwdrv.h"

/* Bring ACPI up and print the report. Returns 1 when an AML namespace loaded. */
int acpi_x86_boot(const hw_plat_t *hp, uint64_t tsc_per_us, void (*log)(const char *));

#endif
