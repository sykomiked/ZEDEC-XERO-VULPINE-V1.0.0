/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* zxv_shell.h — the ZEDEC pqOS desktop shell: the consumer graphical face.
 *
 * A self-contained, integer-only compositor that draws the whole desktop —
 * menu bar with the ZEDEC wordmark, app icons (each a native .zxvc program),
 * a terminal window, a taskbar, and the mouse cursor — into a linear XRGB8888
 * framebuffer via the vbe primitives. The framebuffer reaches a real screen
 * through ramfb (QEMU) or the UEFI GOP (hardware). No libc, no float. */
#ifndef ZXV_SHELL_H
#define ZXV_SHELL_H

#include <stdint.h>
#include "vbe.h"

/* Render one full desktop frame into `v`'s framebuffer. `cursor_x/y` place the
 * mouse pointer; `tick` drives the caret blink and status. Draws OVER whatever
 * background (e.g. the prism_break field) is already in the framebuffer. */
void zxv_shell_render(vbe_state_t *v, int32_t cursor_x, int32_t cursor_y, uint32_t tick);

#endif /* ZXV_SHELL_H */
