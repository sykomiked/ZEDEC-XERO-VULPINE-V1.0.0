/* render_boot.h — portable ramfb desktop bring-up. See render_boot.c.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 */
#ifndef ZXV_RENDER_BOOT_H
#define ZXV_RENDER_BOOT_H

/* Bring up the ramfb scanout and draw the ZEDEC desktop into it. `log` is the
 * arch's console line printer (may be 0). Returns 0 when the desktop was drawn
 * to a live scanout; <0 when no framebuffer could be brought up (the caller
 * then continues headless on serial — this is NOT a boot failure). */
int zxv_render_boot(void (*log)(const char *));

#endif /* ZXV_RENDER_BOOT_H */
