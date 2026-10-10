/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_bootinfo.h — the boot-handoff contract shared by the EFI stub and the kernel.
 *
 * When ZXV is launched by UEFI (BOOTAA64.EFI) rather than QEMU -kernel, the stub
 * captures the firmware's GOP linear framebuffer, then publishes it in a fixed
 * record 64KB BELOW the kernel's load address (0x40070000) — inside identity-
 * mapped RAM, outside the kernel image so BSS-clear cannot wipe it. The kernel
 * checks for the magic at boot: present => adopt the GOP fb (skip ramfb); absent
 * (the -kernel path) => nothing changes. This keeps ONE binary, both boot modes.
 */
#ifndef ZXV_BOOTINFO_H
#define ZXV_BOOTINFO_H

#include <stdint.h>

#define ZXV_BOOTINFO_ADDR   0x40070000ULL          /* fixed, 64KB below kernel @0x40080000 */
#define ZXV_BOOTINFO_MAGIC   0x5A585642464F4F49ULL /* "IOOFBVXZ" LE — distinctive sentinel  */

/* GOP PixelFormat values we care about (UEFI EFI_GRAPHICS_PIXEL_FORMAT):
 *   0 = PixelRedGreenBlueReserved8BitPerColor  (byte order R,G,B,x -> needs R/B swap)
 *   1 = PixelBlueGreenRedReserved8BitPerColor  (byte order B,G,R,x -> matches ZXV XRGB) */
#define ZXV_PIXFMT_RGB 0
#define ZXV_PIXFMT_BGR 1

typedef struct {
    uint64_t magic;      /* ZXV_BOOTINFO_MAGIC when valid                       */
    uint64_t fb;         /* GOP framebuffer physical base (== virtual, identity)*/
    uint32_t width;      /* horizontal resolution in pixels                     */
    uint32_t height;     /* vertical resolution in pixels                       */
    uint32_t stride;     /* pixels per scanline (PixelsPerScanLine)             */
    uint32_t pixfmt;     /* ZXV_PIXFMT_*                                        */
    uint64_t reserved[4];
} zxv_bootinfo_t;

#endif /* ZXV_BOOTINFO_H */
