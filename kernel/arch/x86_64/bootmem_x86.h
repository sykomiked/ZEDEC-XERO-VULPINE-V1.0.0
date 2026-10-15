/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bootmem_x86.h — x86_64 early memory from the firmware map (zxv_bootmem.h). */
#ifndef ZXV_BOOTMEM_X86_H
#define ZXV_BOOTMEM_X86_H

#include <stdint.h>

#include "../zxv_handoff.h"

/* Read the evidence once: handoff v2, else the Multiboot1 map (magic and
 * info as the loader left them in eax/ebx). Prints one [BOOTMEM] line.
 * Returns the number of usable ranges (0 = no evidence: every take fails). */
uint32_t bootmem_x86_init(uint32_t mb_magic, uint64_t mb_info);

/* bytes of zeroed, page-aligned, identity-mapped memory past the kernel,
 * or NULL. `what` names the buffer in the boot log when it cannot be met. */
void *bootmem_x86_take(uint64_t bytes, const char *what);

/* The stub's handoff v2 record when the kernel accepted it, else NULL (no
 * record: a direct boot; or a damaged one, which is never trusted). ACPI
 * code gates on its flags (ZXH_F_ACPI_*) and takes the RSDP from it. */
const zxh_t *zxv_handoff(void);

#endif
