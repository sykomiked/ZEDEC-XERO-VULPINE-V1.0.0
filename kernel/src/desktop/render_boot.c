/* render_boot.c — portable ramfb desktop bring-up shared by the arches whose
 * kernel_main does NOT carry its own compositor wiring (riscv64, riscv32,
 * arm32). arm64 has its own richer path (GOP adoption + holographic present)
 * inline in kernel_arch/arm64/kernel_main_arm64.c and does NOT use this file.
 *
 * WHAT THIS DOES, and WHY it is a separate translation unit
 * --------------------------------------------------------
 * The full render chain (prism_break compositor -> display negotiation ->
 * ramfb scanout over fw_cfg -> vbe surface -> zxv_shell + lattice_dim draw ->
 * blit to scanout) was linked into every arch but CALLED from arm64 alone.
 * That left four of five platforms unable to draw a pixel. This single
 * function is the missing call site, written once and invoked from the riscv
 * and arm32 mains, so the same proven chain runs on each.
 *
 * NO HARDCODED RESOLUTION. The ramfb device has no native geometry — the guest
 * TELLS QEMU the width/height it wants over fw_cfg. So the geometry is chosen,
 * not read, and the choice is bounded by two REAL capability limits, never a
 * magic number:
 *   1. the static scanout budget  ZXV_DISPLAY_MAX_W x ZXV_DISPLAY_MAX_H, and
 *   2. the prism_break compositor's back-buffer capacity PB_MAX_WIDTH x
 *      PB_MAX_HEIGHT (1920x1080).
 * We negotiate the largest standard mode that fits the SMALLER of the two, so
 * the scanout, the vbe surface, the shell field, and the compositor buffer all
 * agree on one geometry and nothing draws past its array. (The arm64 inline
 * path negotiates to the full scanout budget and lets the shell overrun the
 * 1920x1080 prism buffer into the adjacent backbuffer field — it stays inside
 * the struct there, but this shared path refuses to rely on that.)
 *
 * Freestanding: integer only, no libc, no float, no allocation. The scanout is
 * a static buffer (no allocator in this kernel). rv32/arm32 safe: the whole
 * chain is the same object code those Makefiles already link; this only calls
 * it.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 */
#include "m5_types.h"                 /* pulls <stdint.h> (uint32_t, uintptr_t) */
#include "../prism_break/prism_break.h"
#include "../vbe/vbe.h"
#include "zxv_shell.h"
#include "../display/display.h"
#include "../video/ramfb.h"
#include "../emu/lattice_dimensions.h"
#include "render_boot.h"

/* Static scanout: the buffer QEMU's ramfb scans out. Sized to the build budget;
 * the negotiated mode is always <= this. Aligned for wide stores. */
static uint32_t          rb_scanout[ZXV_DISPLAY_MAX_W * ZXV_DISPLAY_MAX_H]
                             __attribute__((aligned(64)));
static prism_break_t     rb_prism;
static zxv_display_t     rb_disp;
static zxv_shell_state_t rb_shell;
static vbe_state_t       rb_vbe;

int zxv_render_boot(void (*log)(const char *)) {
    if (log) log("[BOOT] ramfb display scanout (fw_cfg)...");

    /* One composed frame from the prism_break compositor gives us a live back
     * buffer to draw the desktop into. pb_init clamps to PB_MAX_WIDTH/HEIGHT. */
    pb_init(&rb_prism, PB_MAX_WIDTH, PB_MAX_HEIGHT);
    pb_render_frame(&rb_prism);
    uint32_t *fb = pb_get_framebuffer(&rb_prism);   /* the 1920x1080 back buffer */
    if (!fb) { if (log) log("  [SKIP] compositor back buffer unavailable"); return -1; }

    /* Negotiate geometry. Ceiling = min(scanout budget, compositor capacity),
     * expressed as a REQUEST so display.c returns the largest standard mode that
     * fits it. No firmware scanout on the -kernel path (fw_* = 0). */
    uint32_t cap_w = ZXV_DISPLAY_MAX_W < PB_MAX_WIDTH  ? ZXV_DISPLAY_MAX_W : PB_MAX_WIDTH;
    uint32_t cap_h = ZXV_DISPLAY_MAX_H < PB_MAX_HEIGHT ? ZXV_DISPLAY_MAX_H : PB_MAX_HEIGHT;
    if (zxv_display_negotiate(&rb_disp, 0, 0, 0, cap_w, cap_h) != 0) {
        /* Refused rather than clamped: fall back to a mode we can certainly back. */
        if (zxv_display_negotiate(&rb_disp, 0, 0, 0, 1280, 720) != 0) {
            if (log) log("  [SKIP] no usable display mode");
            return -1;
        }
    }

    /* Register the scanout with QEMU over fw_cfg DMA at the negotiated geometry.
     * -2 => no ramfb device (launch with -device ramfb); we stay on serial. */
    int rc = ramfb_init(rb_scanout, rb_disp.w, rb_disp.h);
    if (rc == -2) { if (log) log("  [SKIP] no ramfb device (launch QEMU with -device ramfb)"); return rc; }
    if (rc != 0)  { if (log) log("  [SKIP] ramfb init failed; staying on serial console");     return rc; }

    /* Compose the desktop into the back buffer at the negotiated geometry, then
     * present (blit) the finished frame into the scanout. */
    vbe_init_fb(&rb_vbe, (uint16_t)rb_disp.w, (uint16_t)rb_disp.h, 32, (uintptr_t)fb);
    zxv_shell_init(&rb_shell);
    zxv_shell_set_ui_scale(rb_disp.scale_permille);
    (void)zxv_shell_set_field_geometry(rb_disp.w, rb_disp.h);
    zxv_shell_frame(&rb_shell, &rb_vbe,
                    (int32_t)(rb_disp.w / 2 - 8), (int32_t)(rb_disp.h / 2),
                    0, rb_prism.frames_rendered, 0, 0);
    lattice_dim_render(fb, (int)rb_disp.w, (int)rb_disp.h);

    /* Present: copy the composed frame into the live scanout. Both are the same
     * negotiated geometry, so this stays in bounds on every arch. */
    {
        uint32_t n = rb_disp.w * rb_disp.h;
        for (uint32_t i = 0; i < n; i++) rb_scanout[i] = fb[i];
    }

    if (log) {
        /* Report the mode actually negotiated, not a literal. */
        char m[96]; uint32_t o = 0;
        const char *pre = "  [DRIVER ONLINE] ramfb ";
        for (const char *q = pre; *q && o < 60; q++) m[o++] = *q;
        const char *nm = rb_disp.name ? rb_disp.name : "?";
        for (const char *q = nm; *q && o < 78; q++) m[o++] = *q;
        const char *suf = " — ZEDEC desktop on screen";
        for (const char *q = suf; *q && o < 95; q++) m[o++] = *q;
        m[o] = 0; log(m);
    }
    return 0;
}
