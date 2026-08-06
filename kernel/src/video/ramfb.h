/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* ramfb.h — the universal scanout: QEMU 'ramfb' via the fw_cfg DMA interface.
 *
 * WHY THIS IS THE UNIVERSAL DISPLAY
 * --------------------------------
 * `ramfb` is a QEMU device present on EVERY machine type we target — arm64
 * `virt`, x86_64, riscv64 `virt`. The guest hands QEMU a pointer to a linear
 * framebuffer (via fw_cfg) and QEMU scans that memory straight out to the
 * display, every frame, with no per-arch GPU driver. One mechanism, all chips.
 * On real hardware the same linear framebuffer comes from the UEFI GOP; the
 * compositor above does not care which handed it the pixels.
 *
 * The kernel is identity-mapped (virtual == physical), so the addresses we give
 * QEMU are the physical addresses it DMAs against directly.
 *
 * Requires QEMU launched with `-device ramfb`.
 */
#ifndef ZXV_RAMFB_H
#define ZXV_RAMFB_H

#include <stdint.h>
#include <stdbool.h>

/* Point QEMU's ramfb scanout at `fb` (a WIDTH*HEIGHT XRGB8888 buffer, stride =
 * WIDTH*4 bytes). Returns 0 on success, negative if fw_cfg / etc/ramfb is
 * absent (then nothing scans out — honest fail, no fabricated display). */
int ramfb_init(volatile uint32_t *fb, uint32_t width, uint32_t height);

/* True once ramfb_init succeeded and a live scanout is configured. */
bool ramfb_is_live(void);

/* The fw_cfg MMIO/IO base for this arch. Defaults to the QEMU arm64 `virt`
 * address; override at build time (-DRAMFB_FWCFG_BASE=...) for other arches. */
#ifndef RAMFB_FWCFG_BASE
#define RAMFB_FWCFG_BASE 0x09020000UL   /* QEMU arm64/riscv 'virt' fw_cfg MMIO */
#endif

#endif /* ZXV_RAMFB_H */
