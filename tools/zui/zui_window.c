/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_window.c — opens a real window through a zui present layer and checks
 * that pixels go out and input comes back.
 *
 * It takes the first backend whose open() succeeds (or the one named with
 * --backend), paints a test pattern with zui_canvas into a surface it owns
 * (the green ground, an engraved panel, a gold rounded rectangle, and a gold
 * marker that moves each frame so later frames send only a small damage
 * area), and prints every input event as one line on stdout. Escape or the
 * window's close button ends it.
 *
 *   zui_window [--backend x11|vulkan|d3d11|metal] [--frames N]
 *              [--screenshot out.ppm] [--size W H] [--still]
 *
 * --frames N presents N frames and exits. --still keeps the marker in place
 * (every frame after the first has no damage), so a capture of the window
 * can be compared with the surface while the program runs. --screenshot writes the app's own
 * surface as a binary PPM on exit; it reads nothing back from the screen,
 * which is the point: P2 says every backend shows exactly these bytes.
 *
 * Build (from the repo root):
 *
 *   Linux (Vulkan + X11; Vulkan is loaded at run time, so the program runs
 *   without it):
 *     gcc -std=c11 -O2 -Wall -Wextra -Werror \
 *       -Ikernel/src/zui -Ikernel/src/font -Ikernel/include -Ikernel/arch/hosted \
 *       tools/zui/zui_window.c kernel/arch/hosted/zui_present_host.c \
 *       kernel/arch/hosted/zui_present_vulkan.c kernel/arch/hosted/zui_present_x11.c \
 *       kernel/arch/hosted/zui_x11_window.c \
 *       kernel/src/zui/zui_canvas.c kernel/src/zui/zui_layout.c kernel/src/zui/zui_input.c \
 *       kernel/src/zui/zui_present_fb.c kernel/src/font/truetype.c \
 *       -lX11 -lXext -ldl -lm -o /tmp/zui_window
 *   (X11 only: add -DZUI_HAVE_VULKAN=0 and drop zui_present_vulkan.c and -ldl.)
 *
 *   Windows (MinGW-w64; MSVC takes the same files):
 *     x86_64-w64-mingw32-gcc -std=c11 -O2 -Wall -Wextra -Werror \
 *       -Ikernel/src/zui -Ikernel/src/font -Ikernel/include -Ikernel/arch/hosted \
 *       -I<dir with vulkan/vulkan.h> \
 *       tools/zui/zui_window.c kernel/arch/hosted/zui_present_host.c \
 *       kernel/arch/hosted/zui_present_d3d11.c kernel/arch/hosted/zui_present_vulkan.c \
 *       kernel/arch/hosted/zui_win32_window.c \
 *       kernel/src/zui/zui_canvas.c kernel/src/zui/zui_layout.c kernel/src/zui/zui_input.c \
 *       kernel/src/zui/zui_present_fb.c kernel/src/font/truetype.c \
 *       -ld3d11 -ldxgi -ldxguid -limm32 -luser32 -lgdi32 -o zui_window.exe
 *   (D3D11 only: -DZUI_HAVE_VULKAN=0 and drop zui_present_vulkan.c.)
 *
 *   macOS:
 *     clang -std=c11 -O2 -Wall -Wextra -Werror -fobjc-arc \
 *       -Ikernel/src/zui -Ikernel/src/font -Ikernel/include -Ikernel/arch/hosted \
 *       tools/zui/zui_window.c kernel/arch/hosted/zui_present_host.c \
 *       kernel/arch/hosted/zui_present_metal.m \
 *       kernel/src/zui/zui_canvas.c kernel/src/zui/zui_layout.c kernel/src/zui/zui_input.c \
 *       kernel/src/zui/zui_present_fb.c kernel/src/font/truetype.c \
 *       -framework Cocoa -framework Metal -framework QuartzCore -o /tmp/zui_window
 *
 * kernel/arch/hosted/test_zui_present.sh builds and runs it under Xvfb.
 */
#if !defined(_WIN32)
#    define _POSIX_C_SOURCE 200809L
#endif
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zui_canvas.h"
#include "zui_input.h"
#include "zui_present.h"
#include "zui_present_host.h"
#include "zui_tokens.h"

typedef struct {
    zui_surface_t s;
    uint32_t *px;
    zui_screen_t scr;
    zui_rect_t marker; /* where the moving marker was last drawn */
    zui_rect_t track;  /* the strip it moves along               */
} app_t;

static void alloc_surface(app_t *a, int32_t w, int32_t h)
{
    free(a->px);
    a->px = malloc((size_t) w * (size_t) h * 4u);
    if (!a->px) {
        fprintf(stderr, "zui_window: out of memory for %dx%d\n", (int) w, (int) h);
        exit(2);
    }
    zui_surface_init(&a->s, a->px, w, h, w);
}

static zui_rect_t marker_at(const app_t *a, uint64_t frame)
{
    int32_t mw = zui_px(&a->scr, 24);
    int32_t span = a->track.w - mw;
    if (span < 1) span = 1;
    int32_t pos = (int32_t) (frame * (uint64_t) zui_px(&a->scr, 4) % (uint64_t) (2 * span));
    if (pos > span) pos = 2 * span - pos;
    zui_rect_t r = {a->track.x + pos, a->track.y, mw, a->track.h};
    return r;
}

/* The whole test pattern: ground, engraved panel, gold block, track. */
static void paint_all(app_t *a, uint64_t frame)
{
    zui_surface_t *s = &a->s;
    zui_unclip(s);
    zui_rect_t all = {0, 0, s->w, s->h};
    zui_fill(s, all, ZUI_VOID);

    int32_t g = zui_px(&a->scr, ZUI_S5);
    zui_rect_t panel = {g, g, s->w - 2 * g, s->h - 2 * g};
    if (panel.w > 0 && panel.h > 0) {
        int32_t rad = zui_px(&a->scr, ZUI_R_PANEL);
        zui_fill_rrect(s, panel, rad, ZUI_PANEL, 255);
        zui_engrave(s, panel, rad, ZUI_ENGRAVE_LO, ZUI_ENGRAVE_HI, 60);

        int32_t in = zui_px(&a->scr, ZUI_S4);
        zui_rect_t gold = {panel.x + in, panel.y + in, panel.w / 3, panel.h / 3};
        zui_fill_rrect(s, gold, zui_px(&a->scr, ZUI_R_CONTROL), ZUI_GOLD, 255);
        zui_stroke_rrect(s, gold, zui_px(&a->scr, ZUI_R_CONTROL), zui_px(&a->scr, 1), ZUI_GOLD_HI,
                         255);

        zui_rect_t track = {panel.x + in, panel.y + panel.h - in - zui_px(&a->scr, 12),
                            panel.w - 2 * in, zui_px(&a->scr, 12)};
        a->track = track;
        zui_fill_rrect(s, track, zui_px(&a->scr, 3), ZUI_RAISED, 255);
        zui_hline(s, panel.x + in, gold.y + gold.h + in, panel.w - 2 * in, 1, ZUI_HAIRLINE);
    } else {
        zui_rect_t none = {0, 0, 0, 0};
        a->track = none;
    }
    a->marker = marker_at(a, frame);
    if (a->track.w > 0) zui_fill_rrect(s, a->marker, zui_px(&a->scr, 3), ZUI_GOLD, 255);
}

/* Move the marker: repaint the track under the old place and draw the new
 * one; damage is just those two rectangles. */
static void paint_step(app_t *a, uint64_t frame, zui_damage_t *d)
{
    if (a->track.w <= 0) return;
    zui_surface_t *s = &a->s;
    zui_rect_t old = a->marker;
    zui_clip(s, old);
    zui_fill(s, old, ZUI_PANEL);
    zui_fill_rrect(s, a->track, zui_px(&a->scr, 3), ZUI_RAISED, 255);
    zui_unclip(s);
    a->marker = marker_at(a, frame);
    zui_fill_rrect(s, a->marker, zui_px(&a->scr, 3), ZUI_GOLD, 255);
    zui_damage_add(d, old);
    zui_damage_add(d, a->marker);
}

static const char *ev_name(uint8_t t)
{
    static const char *const names[] = {
        "none",   "key_down", "key_up",      "text",       "preedit",   "ptr_down",     "ptr_move",
        "ptr_up", "scroll",   "touch_begin", "touch_move", "touch_end", "touch_cancel", "resize",
        "scale",  "focus_in", "focus_out",   "close",      "tick"};
    return t < sizeof names / sizeof names[0] ? names[t] : "?";
}

static void echo(const zui_event_t *e)
{
    printf("event %s", ev_name(e->type));
    switch (e->type) {
    case ZUI_EV_KEY_DOWN:
    case ZUI_EV_KEY_UP:
        printf(" key=0x%x mods=0x%x repeat=%u", e->key, e->mods, e->repeat);
        break;
    case ZUI_EV_TEXT:
    case ZUI_EV_PREEDIT:
        printf(" text=\"%s\"", e->text);
        break;
    case ZUI_EV_PTR_DOWN:
    case ZUI_EV_PTR_MOVE:
    case ZUI_EV_PTR_UP:
        printf(" x=%d y=%d button=0x%x", (int) e->x, (int) e->y, e->button);
        break;
    case ZUI_EV_SCROLL:
        printf(" x=%d y=%d dx=%d dy=%d", (int) e->x, (int) e->y, (int) e->dx, (int) e->dy);
        break;
    case ZUI_EV_TOUCH_BEGIN:
    case ZUI_EV_TOUCH_MOVE:
    case ZUI_EV_TOUCH_END:
    case ZUI_EV_TOUCH_CANCEL:
        printf(" id=%u x=%d y=%d", e->touch_id, (int) e->x, (int) e->y);
        break;
    case ZUI_EV_RESIZE:
        printf(" w=%d h=%d", (int) e->x, (int) e->y);
        break;
    case ZUI_EV_SCALE:
        printf(" scale_q8=%d", (int) e->x);
        break;
    default:
        break;
    }
    printf("\n");
}

static int write_ppm(const char *path, const zui_surface_t *s)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", (int) s->w, (int) s->h);
    for (int32_t y = 0; y < s->h; y++) {
        for (int32_t x = 0; x < s->w; x++) {
            uint32_t p = s->px[(size_t) y * (size_t) s->stride + (size_t) x];
            unsigned char rgb[3] = {(unsigned char) (p >> 16), (unsigned char) (p >> 8),
                                    (unsigned char) p};
            fwrite(rgb, 1, 3, f);
        }
    }
    return fclose(f);
}

static void usage(void)
{
    fprintf(stderr, "usage: zui_window [--backend x11|vulkan|d3d11|metal] [--frames N]\n"
                    "                  [--screenshot out.ppm] [--size W H] [--still]\n");
    exit(2);
}

int main(int argc, char **argv)
{
    const char *want = NULL, *shot = NULL;
    long frames_max = -1;
    bool still = false;
    int32_t w_pt = 640, h_pt = 400;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--backend") && i + 1 < argc)
            want = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames_max = strtol(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--still"))
            still = true;
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc)
            shot = argv[++i];
        else if (!strcmp(argv[i], "--size") && i + 2 < argc) {
            w_pt = (int32_t) strtol(argv[++i], NULL, 10);
            h_pt = (int32_t) strtol(argv[++i], NULL, 10);
        } else
            usage();
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    setlocale(LC_CTYPE, ""); /* the input method commits text in the locale */

    /* the first backend that opens */
    const zui_present_ops_t *cand[8];
    uint32_t nc = zui_present_candidates(cand, 8);
    if (nc > 8) nc = 8;
    static zui_present_t p;
    const zui_present_ops_t *ops = NULL;
    for (uint32_t i = 0; i < nc; i++) {
        if (want && strcmp(cand[i]->name, want)) continue;
        memset(&p, 0, sizeof p);
        p.ops = cand[i];
        int rc = cand[i]->open(&p, "zui_window", w_pt, h_pt);
        if (rc == ZUI_PRESENT_OK) {
            ops = cand[i];
            break;
        }
        fprintf(stderr, "zui_window: backend %s unavailable (%d)\n", cand[i]->name, rc);
    }
    if (!ops) {
        fprintf(stderr, "zui_window: no present backend opened%s%s\n", want ? " for " : "",
                want ? want : "");
        return 3;
    }
    printf("backend %s %ux%u scale_q8=%u\n", ops->name, p.screen.px_w, p.screen.px_h,
           p.screen.scale_q8);

    static app_t a;
    a.scr = p.screen;
    alloc_surface(&a, (int32_t) p.screen.px_w, (int32_t) p.screen.px_h);
    static zui_queue_t q;
    zui_queue_init(&q);

    uint64_t frame = 0;
    zui_damage_t dmg;
    zui_damage_clear(&dmg);
    paint_all(&a, frame);
    zui_damage_all(&dmg, a.s.w, a.s.h);
    const char *why = "frames";
    int rc = 0;
    for (;;) {
        if (frames_max >= 0 && (long) frame >= frames_max) break;
        int pr = ops->present(&p, &a.s, &dmg);
        if (pr != ZUI_PRESENT_OK) {
            fprintf(stderr, "zui_window: present failed (%d)\n", pr);
            why = "present-failed";
            rc = 4;
            break;
        }
        frame++;
        printf("frame %llu presented\n", (unsigned long long) frame);

        int n = ops->poll(&p, &q, 16);
        if (n < 0) {
            why = "lost";
            break;
        }
        bool done = false, repaint = false;
        zui_event_t e;
        while (zui_pop(&q, &e)) {
            echo(&e);
            if (e.type == ZUI_EV_CLOSE) why = "close", done = true;
            if (e.type == ZUI_EV_KEY_DOWN && e.key == ZUI_KEY_ESCAPE) why = "escape", done = true;
            if (e.type == ZUI_EV_RESIZE || e.type == ZUI_EV_SCALE) repaint = true;
        }
        if (done) break;

        zui_damage_clear(&dmg);
        if (repaint) {
            /* P3: the app re-inits its own surface for the new size */
            a.scr = p.screen;
            alloc_surface(&a, (int32_t) p.screen.px_w, (int32_t) p.screen.px_h);
            paint_all(&a, frame);
            zui_damage_all(&dmg, a.s.w, a.s.h);
        } else if (!still) {
            paint_step(&a, frame, &dmg);
        }
    }
    printf("exit %s frames=%llu px_copied=%llu\n", why, (unsigned long long) p.frames,
           (unsigned long long) p.px_copied);
    if (shot && write_ppm(shot, &a.s) != 0) {
        fprintf(stderr, "zui_window: cannot write %s\n", shot);
        rc = 5;
    }
    ops->close(&p);
    free(a.px);
    return rc;
}
