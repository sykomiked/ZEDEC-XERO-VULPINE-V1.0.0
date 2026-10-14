/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* efifb_con.h — x86-64 text console on the UEFI GOP framebuffer.
 *
 * On a real PC there is usually no COM1 to read, so the serial console alone
 * leaves the screen blank after the firmware hands off. When BOOTX64.EFI found
 * a GOP framebuffer it publishes it in a handoff record at ZXV_X86_BOOTINFO_ADDR
 * (same layout as the arm64 zxv_bootinfo_t); efifb_init adopts it and from then
 * on every byte written to the serial console is also drawn on screen. On the
 * QEMU -kernel path there is no record and nothing changes. */
#ifndef ZXV_EFIFB_CON_H
#define ZXV_EFIFB_CON_H

#include <stdint.h>

/* Below the kernel's 1 MB load address, outside its image (so _entry32's BSS
 * clear cannot wipe it), inside the identity-mapped first 2 MB. Written by the
 * stub after ExitBootServices, when firmware no longer owns low memory. */
#define ZXV_X86_BOOTINFO_ADDR 0x80000ULL
#define ZXV_BOOTINFO_MAGIC    0x5A585642464F4F49ULL

typedef struct {
    uint64_t magic;  /* ZXV_BOOTINFO_MAGIC when valid                   */
    uint64_t fb;     /* GOP framebuffer physical base                   */
    uint32_t width;  /* visible pixels per row                          */
    uint32_t height; /* rows                                            */
    uint32_t stride; /* pixels per scanline                             */
    uint32_t pixfmt; /* 0 = RGBx, 1 = BGRx                              */
    uint64_t check;  /* magic ^ fb ^ width ^ height ^ stride: rejects stale bytes */
    /* reserved[0] = ZXV_BOOTINFO_RSDP_MAGIC when [1] holds the ACPI RSDP's
     * physical address (UEFI configuration table, ACPI 2.0 GUID first, then
     * 1.0) and [2] = magic ^ [1]. Published even when there is no GOP. */
    uint64_t reserved[3];
} zxv_x86_bootinfo_t;

#define ZXV_BOOTINFO_RSDP_MAGIC 0x5A58565253445021ULL

/* Returns 1 when a valid record was found, mapped and the screen cleared. */
int efifb_init(void);
int efifb_active(void);
void efifb_putc(char c);

#endif /* ZXV_EFIFB_CON_H */
