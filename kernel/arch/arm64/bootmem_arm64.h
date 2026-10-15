/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bootmem_arm64.h — arm64 early memory from the firmware's own map
 * (zxv_bootmem.h). */
#ifndef ZXV_BOOTMEM_ARM64_H
#define ZXV_BOOTMEM_ARM64_H

#include <stdint.h>

#include "../zxv_handoff.h"

/* Fill the table from handoff v2 (UEFI disc), else from the device tree the
 * entry x0 pointed at (QEMU -kernel). Prints one [BOOTMEM] line. */
uint32_t bootmem_arm64_init(uint64_t dtb);

/* `bytes` of zeroed memory the firmware called usable, past the kernel; NULL
 * (and a line naming `what`) when there is none. Never freed. */
void *bootmem_arm64_take(uint64_t bytes, const char *what);

/* The accepted handoff v2 record, or NULL. */
const zxh_t *zxv_handoff(void);

#endif /* ZXV_BOOTMEM_ARM64_H */
