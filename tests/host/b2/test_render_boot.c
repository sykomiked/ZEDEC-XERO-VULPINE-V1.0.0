/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_render_boot.c: unit tests for kernel/src/desktop/render_boot.c.
 *
 * zxv_render_boot() only sequences other subsystems, so this file links it
 * against recording stubs of those eleven entry points (compositor, display
 * negotiation, ramfb, vbe, shell, lattice renderer) and checks every call's
 * arguments, every early-exit path and its log line and return code, the
 * fallback mode, the blit into the scanout (exactly w*h pixels), and the
 * bounded construction of the final log line. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "m5_types.h"
#include "../prism_break/prism_break.h"
#include "../vbe/vbe.h"
#include "zxv_shell.h"
#include "../display/display.h"
#include "../video/ramfb.h"
#include "../emu/lattice_dimensions.h"
#include "render_boot.h"

static int checks, failures;
#define CHECK(c)                                                              \
    do {                                                                      \
        checks++;                                                             \
        if (!(c)) {                                                           \
            failures++;                                                       \
            printf("FAIL line %d: %s\n", __LINE__, #c);                       \
        }                                                                     \
    } while (0)

/* ---- recording stubs ---------------------------------------------------- */
#define FBW 64
#define FBH 48
static uint32_t g_fb[FBW * FBH + 8];
static int g_fb_null;

static int n_pb_init, n_pb_frame, n_pb_getfb;
static uint32_t pb_w, pb_h;
static prism_break_t *pb_ptr;

void pb_init(prism_break_t *pb, uint32_t width, uint32_t height) {
    n_pb_init++;
    pb_ptr = pb;
    pb_w = width;
    pb_h = height;
    pb->frames_rendered = 0;
}

void pb_render_frame(prism_break_t *pb) {
    n_pb_frame++;
    pb->frames_rendered += 4242;
}

uint32_t *pb_get_framebuffer(prism_break_t *pb) {
    n_pb_getfb++;
    CHECK(pb == pb_ptr);
    return g_fb_null ? NULL : g_fb;
}

/* negotiate: per-call return codes, the mode it reports */
static int neg_rc[2];
static int n_neg;
static uint32_t neg_req_w[2], neg_req_h[2], neg_fw[2][3];
static uint32_t mode_w, mode_h, mode_scale;
static const char *mode_name;

int zxv_display_negotiate(zxv_display_t *d, uint32_t fw_w, uint32_t fw_h, uint32_t fw_stride,
                          uint32_t req_w, uint32_t req_h) {
    int i = n_neg++;
    if (i < 2) {
        neg_fw[i][0] = fw_w;
        neg_fw[i][1] = fw_h;
        neg_fw[i][2] = fw_stride;
        neg_req_w[i] = req_w;
        neg_req_h[i] = req_h;
    }
    int rc = i < 2 ? neg_rc[i] : -9;
    if (rc == 0) {
        d->w = mode_w;
        d->h = mode_h;
        d->stride = mode_w;
        d->scale_permille = mode_scale;
        d->name = mode_name;
    }
    return rc;
}

static int ramfb_rc, n_ramfb;
static volatile uint32_t *ramfb_fb;
static uint32_t ramfb_w, ramfb_h;

int ramfb_init(volatile uint32_t *fb, uint32_t width, uint32_t height) {
    n_ramfb++;
    ramfb_fb = fb;
    ramfb_w = width;
    ramfb_h = height;
    return ramfb_rc;
}

static int n_vbe;
static uint16_t vbe_w, vbe_h;
static uint8_t vbe_bpp;
static uintptr_t vbe_addr;
static vbe_state_t *vbe_ptr;

void vbe_init_fb(vbe_state_t *vbe, uint16_t width, uint16_t height, uint8_t bpp, uintptr_t fb_addr) {
    n_vbe++;
    vbe_ptr = vbe;
    vbe_w = width;
    vbe_h = height;
    vbe_bpp = bpp;
    vbe_addr = fb_addr;
}

static int n_shell_init, n_scale, n_geom, n_frame;
static zxv_shell_state_t *shell_ptr;
static uint32_t scale_arg, geom_w, geom_h;
static int32_t frame_cx, frame_cy;
static uint32_t frame_buttons, frame_tick;
static uint8_t *frame_fphase, *frame_fdepth;
static vbe_state_t *frame_vbe;

void zxv_shell_init(zxv_shell_state_t *st) {
    n_shell_init++;
    shell_ptr = st;
}

void zxv_shell_set_ui_scale(uint32_t permille) {
    n_scale++;
    scale_arg = permille;
}

uint32_t zxv_shell_set_field_geometry(uint32_t panel_w, uint32_t panel_h) {
    n_geom++;
    geom_w = panel_w;
    geom_h = panel_h;
    return 7;
}

void zxv_shell_frame(zxv_shell_state_t *st, vbe_state_t *v, int32_t cx, int32_t cy,
                     uint32_t buttons, uint32_t tick, uint8_t *fphase, uint8_t *fdepth) {
    n_frame++;
    CHECK(st == shell_ptr);
    frame_vbe = v;
    frame_cx = cx;
    frame_cy = cy;
    frame_buttons = buttons;
    frame_tick = tick;
    frame_fphase = fphase;
    frame_fdepth = fdepth;
}

static int n_lat;
static uint32_t *lat_fb;
static int lat_w, lat_h;

void lattice_dim_render(uint32_t *fb, int w, int h) {
    n_lat++;
    lat_fb = fb;
    lat_w = w;
    lat_h = h;
}

/* ---- log capture -------------------------------------------------------- */
static char logs[8][160];
static int n_logs;

static void cap_log(const char *s) {
    if (n_logs < 8) {
        size_t i = 0;
        for (; s[i] && i + 1 < sizeof(logs[0]); i++) logs[n_logs][i] = s[i];
        logs[n_logs][i] = 0;
    }
    n_logs++;
}

static void reset(void) {
    n_pb_init = n_pb_frame = n_pb_getfb = 0;
    pb_w = pb_h = 0;
    g_fb_null = 0;
    n_neg = 0;
    neg_rc[0] = neg_rc[1] = 0;
    memset(neg_req_w, 0xFF, sizeof(neg_req_w));
    memset(neg_req_h, 0xFF, sizeof(neg_req_h));
    memset(neg_fw, 0xFF, sizeof(neg_fw));
    mode_w = 40;
    mode_h = 30;
    mode_scale = 1500;
    mode_name = "40x30";
    ramfb_rc = 0;
    n_ramfb = 0;
    ramfb_fb = NULL;
    ramfb_w = ramfb_h = 0;
    n_vbe = n_shell_init = n_scale = n_geom = n_frame = n_lat = 0;
    vbe_w = vbe_h = 0;
    vbe_bpp = 0;
    vbe_addr = 0;
    frame_cx = frame_cy = -1;
    frame_buttons = frame_tick = 99;
    frame_fphase = frame_fdepth = (uint8_t *)g_fb;
    n_logs = 0;
    memset(logs, 0, sizeof(logs));
    for (uint32_t i = 0; i < FBW * FBH + 8; i++) g_fb[i] = 0xC0DE0000u + i;
}

static void test_no_framebuffer(void) {
    reset();
    g_fb_null = 1;
    CHECK(zxv_render_boot(cap_log) == -1);
    CHECK(n_pb_init == 1 && n_pb_frame == 1 && n_pb_getfb == 1);
    CHECK(pb_w == PB_MAX_WIDTH && pb_h == PB_MAX_HEIGHT);
    CHECK(n_neg == 0 && n_ramfb == 0);
    CHECK(n_logs == 2);
    CHECK(strcmp(logs[0], "[BOOT] ramfb display scanout (fw_cfg)...") == 0);
    CHECK(strcmp(logs[1], "  [SKIP] compositor back buffer unavailable") == 0);
    reset();
    g_fb_null = 1;
    CHECK(zxv_render_boot(NULL) == -1);
    CHECK(n_pb_getfb == 1 && n_neg == 0);
}

static void test_negotiation(void) {
    /* the request is the smaller of the scanout budget and the compositor */
    reset();
    CHECK(zxv_render_boot(cap_log) == 0);
    CHECK(n_neg == 1);
    CHECK(neg_fw[0][0] == 0 && neg_fw[0][1] == 0 && neg_fw[0][2] == 0);
    CHECK(neg_req_w[0] == 1920 && neg_req_h[0] == 1080);

    /* refused: fall back to 1280x720 */
    reset();
    neg_rc[0] = -1;
    mode_name = "1280x720";
    CHECK(zxv_render_boot(cap_log) == 0);
    CHECK(n_neg == 2);
    CHECK(neg_req_w[1] == 1280 && neg_req_h[1] == 720);
    CHECK(neg_fw[1][0] == 0 && neg_fw[1][1] == 0 && neg_fw[1][2] == 0);
    CHECK(n_ramfb == 1);
    CHECK(n_logs == 2);
    CHECK(strcmp(logs[1], "  [DRIVER ONLINE] ramfb 1280x720 — ZEDEC desktop on screen") == 0);

    /* both refused */
    reset();
    neg_rc[0] = -1;
    neg_rc[1] = -3;
    CHECK(zxv_render_boot(cap_log) == -1);
    CHECK(n_neg == 2 && n_ramfb == 0 && n_vbe == 0);
    CHECK(n_logs == 2);
    CHECK(strcmp(logs[1], "  [SKIP] no usable display mode") == 0);
    reset();
    neg_rc[0] = 1;
    neg_rc[1] = 1;
    CHECK(zxv_render_boot(NULL) == -1);
    CHECK(n_neg == 2 && n_ramfb == 0);
    /* a positive code also counts as refused */
    reset();
    neg_rc[0] = 2;
    CHECK(zxv_render_boot(NULL) == 0);
    CHECK(n_neg == 2);
}

static void test_ramfb_failures(void) {
    reset();
    ramfb_rc = -2;
    CHECK(zxv_render_boot(cap_log) == -2);
    CHECK(n_ramfb == 1 && n_vbe == 0 && n_shell_init == 0);
    CHECK(n_logs == 2);
    CHECK(strcmp(logs[1], "  [SKIP] no ramfb device (launch QEMU with -device ramfb)") == 0);
    reset();
    ramfb_rc = -2;
    CHECK(zxv_render_boot(NULL) == -2);
    CHECK(n_vbe == 0);

    reset();
    ramfb_rc = -5;
    CHECK(zxv_render_boot(cap_log) == -5);
    CHECK(n_vbe == 0);
    CHECK(n_logs == 2);
    CHECK(strcmp(logs[1], "  [SKIP] ramfb init failed; staying on serial console") == 0);
    reset();
    ramfb_rc = 3;
    CHECK(zxv_render_boot(NULL) == 3);
    CHECK(n_vbe == 0);
    reset();
    ramfb_rc = -1;
    CHECK(zxv_render_boot(cap_log) == -1);
    CHECK(strcmp(logs[1], "  [SKIP] ramfb init failed; staying on serial console") == 0);
    reset();
    ramfb_rc = -3;
    CHECK(zxv_render_boot(cap_log) == -3);
    CHECK(strcmp(logs[1], "  [SKIP] ramfb init failed; staying on serial console") == 0);
}

static void test_success(void) {
    reset();
    mode_w = 40;
    mode_h = 30;
    mode_scale = 1500;
    CHECK(zxv_render_boot(cap_log) == 0);
    CHECK(n_ramfb == 1);
    CHECK(ramfb_fb != NULL);
    CHECK(ramfb_w == 40 && ramfb_h == 30);
    CHECK(n_vbe == 1);
    CHECK(vbe_w == 40 && vbe_h == 30 && vbe_bpp == 32);
    CHECK(vbe_addr == (uintptr_t)g_fb);
    CHECK(n_shell_init == 1 && shell_ptr != NULL);
    CHECK(n_scale == 1 && scale_arg == 1500);
    CHECK(n_geom == 1 && geom_w == 40 && geom_h == 30);
    CHECK(n_frame == 1);
    CHECK(frame_vbe == vbe_ptr);
    CHECK(frame_cx == 12 && frame_cy == 15);
    CHECK(frame_buttons == 0);
    CHECK(frame_tick == 4242);
    CHECK(frame_fphase == NULL && frame_fdepth == NULL);
    CHECK(n_lat == 1 && lat_fb == g_fb && lat_w == 40 && lat_h == 30);
    /* blit: exactly w*h pixels */
    int same = 1;
    for (uint32_t i = 0; i < 40 * 30; i++)
        if (ramfb_fb[i] != g_fb[i]) same = 0;
    CHECK(same);
    CHECK(ramfb_fb[1200] == 0);
    CHECK(n_logs == 2);
    CHECK(strcmp(logs[0], "[BOOT] ramfb display scanout (fw_cfg)...") == 0);
    CHECK(strcmp(logs[1], "  [DRIVER ONLINE] ramfb 40x30 — ZEDEC desktop on screen") == 0);

    /* no log callback: same work, no crash */
    reset();
    mode_w = 41;
    mode_h = 31;
    for (uint32_t i = 0; i < FBW * FBH + 8; i++) g_fb[i] = 0xBEEF0000u + i;
    CHECK(zxv_render_boot(NULL) == 0);
    CHECK(frame_cx == 12 && frame_cy == 15);
    CHECK(ramfb_fb[41 * 31 - 1] == 0xBEEF0000u + 41 * 31 - 1);
    CHECK(ramfb_fb[41 * 31] == 0); /* one past w*h is never written */
    CHECK(n_lat == 1 && lat_w == 41 && lat_h == 31);
    /* odd geometry: cx = w/2 - 8 truncates, frames keep counting */
    reset();
    mode_w = 17;
    mode_h = 3;
    CHECK(zxv_render_boot(NULL) == 0);
    CHECK(frame_cx == 0 && frame_cy == 1);
    CHECK(frame_tick == 4242);
}

static void test_log_line(void) {
    /* NULL mode name prints "?" */
    reset();
    mode_name = NULL;
    CHECK(zxv_render_boot(cap_log) == 0);
    CHECK(strcmp(logs[1], "  [DRIVER ONLINE] ramfb ? — ZEDEC desktop on screen") == 0);
    /* empty name */
    reset();
    mode_name = "";
    CHECK(zxv_render_boot(cap_log) == 0);
    CHECK(strcmp(logs[1], "  [DRIVER ONLINE] ramfb  — ZEDEC desktop on screen") == 0);
    /* a long name stops at 78 bytes, the suffix at 95 */
    reset();
    mode_name = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    CHECK(zxv_render_boot(cap_log) == 0);
    const char *want = "  [DRIVER ONLINE] ramfb "
                       "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz01"
                       " — ZEDEC deskto";
    CHECK(strcmp(logs[1], want) == 0);
    CHECK(strlen(logs[1]) == 95);
    /* a name that just fits: 54 bytes, then the suffix up to 95 */
    reset();
    mode_name = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0";
    CHECK(zxv_render_boot(cap_log) == 0);
    CHECK(strcmp(logs[1], "  [DRIVER ONLINE] ramfb "
                          "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0"
                          " — ZEDEC desktop") == 0);
    CHECK(strlen(logs[1]) == 95);
    reset();
    mode_name = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456";
    CHECK(zxv_render_boot(cap_log) == 0);
    CHECK(strlen(logs[1]) == 95);
    CHECK(strncmp(logs[1] + 24, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz01 ", 55) == 0);
}

int main(void) {
    test_no_framebuffer();
    test_negotiation();
    test_ramfb_failures();
    test_success();
    test_log_line();
    if (failures) {
        printf("%d of %d checks failed\n", failures, checks);
        return 1;
    }
    printf("%d checks passed\n", checks);
    return 0;
}
