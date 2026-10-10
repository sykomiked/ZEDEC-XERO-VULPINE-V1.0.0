/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
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
#include "display.h"
#include "vbe.h"

#define SHELL_APPS       5      /* Vino Fleet Studio Chiglet Refinery */
#define SHELL_TERM_LINES 13
#define SHELL_LINE_LEN   72
#define SHELL_CMD_MAX    56

/* The LATTICE: the layer ABOVE the base desktop. 13 nodes in the Fruit-of-Life
 * arrangement (1 center = the base/home plane, 6 inner-ring + 6 outer-ring
 * spaces), wired by Metatron's-Cube edges (spokes + two hexagons + cross-links)
 * — the "adjacent and perpendicular" connections. The depth field is the Z-cue:
 * the FOCUSED node comes warm-near, the rest recede cool-far. This is the first
 * screen stacked on top of the base — you pull back to it, then push into a node. */
#define LATTICE_NODES 13

/* The FIELD MAP: a coarse per-16px-tile grid the shell fills as it draws, so the
 * holographic present can give each object its OWN phase (independent shimmer)
 * and its OWN depth (chromostereopsis: warm=near / cool=far). This is the M5
 * state per region — the nonlinear, multi-axiom layer painted deliberately. */
/* GEOMETRY IS DERIVED, NEVER FIXED.
 *
 * These were `#define FIELD_TX 80 / FIELD_TY 45` -- 1280x720 divided by 16,
 * hardcoded. On any panel larger than that the tile index runs past the array:
 * measured at ti = 7279 against 3600 entries, corrupting 43.3% of the first
 * screen (624 of 1440 rows, reproduced). The defect was never the NUMBERS. It
 * was that a number existed at all where a device property belongs.
 *
 * The panel is not known until zxv_display_negotiate() has run, so the compile
 * time constant can only be CAPACITY. The live geometry is computed from the
 * negotiated mode at runtime -- see zxv_shell_set_field_geometry(). Same
 * discipline as an LCD's own addressing: the controller is told the panel's
 * dimensions, it does not assume them. */
#define FIELD_TILE      16u                                   /* tile edge, px */
#define FIELD_TX_MAX    (ZXV_DISPLAY_MAX_W / FIELD_TILE)       /* capacity only */
#define FIELD_TY_MAX    (ZXV_DISPLAY_MAX_H / FIELD_TILE)
#define FIELD_CELLS_MAX (FIELD_TX_MAX * FIELD_TY_MAX)

/* Live tile geometry, derived from the negotiated panel. Both are clamped to
 * capacity, so a mode larger than the build supports degrades to a smaller
 * field rather than writing past the array. Returns the cell count in use. */
uint32_t zxv_shell_set_field_geometry(uint32_t panel_w, uint32_t panel_h);
uint32_t zxv_shell_field_tx(void);
uint32_t zxv_shell_field_ty(void);

/* An open space is a live surface: SPACE_LINES of readout filled by calling the
 * space's REAL subsystem on entry / on an action, up to SPACE_ACTS action
 * buttons, and 6 sub-spaces (the next layer of the tree). */
#define SPACE_LINES 6
#define SPACE_ACTS  3
#define SPACE_SUBS  6

typedef struct {
    int32_t  active_app;                              /* highlighted dock app, -1 none */
    uint32_t prev_buttons;                            /* for click-edge detection      */
    char     term[SHELL_TERM_LINES][SHELL_LINE_LEN];  /* terminal scrollback           */
    int32_t  term_count;
    char     cmd[SHELL_CMD_MAX];                      /* live command line             */
    int32_t  cmdlen;
    uint32_t click_count;
    int32_t  view;                                    /* 0=base desktop 1=lattice 2=space */
    int32_t  focus_node;                              /* 0..12 focused lattice node    */
    int32_t  space;                                   /* open space node when view==2  */
    int32_t  trans;                                   /* push-in transition frames left */
    /* --- functional space state --- */
    char     sview[SPACE_LINES][SHELL_LINE_LEN];      /* live readout of the open space */
    int32_t  sview_count;
    int32_t  subsel;                                  /* selected sub-space 0..5, -1 none */
    int32_t  act_hot;                                 /* last action pressed (highlight)  */
} zxv_shell_state_t;

/* push-in transition length (frames): a depth sweep far->near when entering a space */
#define LATTICE_TRANS 10

/* Bring the shell up (welcome banner in the terminal, no app focused). */
void zxv_shell_init(zxv_shell_state_t *st);

/* Feed one keyboard key-down (a Linux evdev keycode) to the command line. */
void zxv_shell_key(zxv_shell_state_t *st, int32_t keycode);

/* Process pointer input (hit-test a left-click edge) then render one frame.
 * `buttons` bit0 = left. `cx/cy` is the cursor; `tick` drives the caret blink. */
/* fphase/fdepth are FIELD_TX*FIELD_TY byte maps the shell fills: per-tile phase
 * offset and depth for the holographic present. Pass 0/0 to skip field tagging. */
/* Set the UI scale from the negotiated display mode (permille; 1000 = 1x).
 * Without this the dock is drawn at its 1x reference size on every panel, so a
 * 4K desktop shows the same layout at a quarter of the apparent size. */
void zxv_shell_set_ui_scale(uint32_t permille);

void zxv_shell_frame(zxv_shell_state_t *st, vbe_state_t *v,
                     int32_t cx, int32_t cy, uint32_t buttons, uint32_t tick,
                     uint8_t *fphase, uint8_t *fdepth);

#endif /* ZXV_SHELL_H */
