/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* zxv_shell.h — the ZEDEC pqOS desktop shell: the consumer graphical face.
 *
 * A self-contained, integer-only, STATEFUL compositor. It draws the desktop —
 * menu bar + ZEDEC wordmark, a dock of native .zxvc programs, a live terminal,
 * the Chiglet and Vino panels, a taskbar and cursor — and it RESPONDS: left-
 * clicks hit-test the dock/taskbar to launch/focus apps, and keyboard keycodes
 * feed a working command line. Renders into a linear XRGB8888 framebuffer via
 * the vbe primitives (ramfb on QEMU / UEFI GOP on hardware). No libc, no float.
 */
#ifndef ZXV_SHELL_H
#define ZXV_SHELL_H

#include <stdint.h>
#include "vbe.h"

#define SHELL_APPS       5      /* Vino Fleet Studio Chiglet Refinery */
#define SHELL_TERM_LINES 13
#define SHELL_LINE_LEN   72
#define SHELL_CMD_MAX    56

typedef struct {
    int32_t  active_app;                              /* highlighted dock app, -1 none */
    uint32_t prev_buttons;                            /* for click-edge detection      */
    char     term[SHELL_TERM_LINES][SHELL_LINE_LEN];  /* terminal scrollback           */
    int32_t  term_count;
    char     cmd[SHELL_CMD_MAX];                      /* live command line             */
    int32_t  cmdlen;
    uint32_t click_count;
} zxv_shell_state_t;

/* Bring the shell up (welcome banner in the terminal, no app focused). */
void zxv_shell_init(zxv_shell_state_t *st);

/* Feed one keyboard key-down (a Linux evdev keycode) to the command line. */
void zxv_shell_key(zxv_shell_state_t *st, int32_t keycode);

/* Process pointer input (hit-test a left-click edge) then render one frame.
 * `buttons` bit0 = left. `cx/cy` is the cursor; `tick` drives the caret blink. */
void zxv_shell_frame(zxv_shell_state_t *st, vbe_state_t *v,
                     int32_t cx, int32_t cy, uint32_t buttons, uint32_t tick);

#endif /* ZXV_SHELL_H */
