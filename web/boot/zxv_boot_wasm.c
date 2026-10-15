/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_boot_wasm.c — the browser's door to zui_boot: the same boot screen,
 * the same sounds, the same vault as bare metal and the desktop app, built
 * to WebAssembly (tools/web/build_boot_wasm.sh). boot.js streams the sealed
 * assets, tells this how much has really loaded, and pulls frames and sound.
 *
 * Exports (all integer but zb_load's counts, which are JS numbers):
 *   zb_init(reduce_motion, sound)   start a boot
 *   zb_size(w, h)                   the canvas size; returns the RGBA buffer
 *   zb_logo_buf(n)                  room for one logo's ZXVG bytes
 *   zb_logo_set(which, n)           hand over the bytes just written; 0 / -1
 *   zb_load(done, total)            the real load; 0 / -1
 *   zb_ready(), zb_fail()
 *   zb_frame(ms)                    advance and draw; RGBA ready to show
 *   zb_audio(frames)                pull 48 kHz stereo int16; returns them
 *   zb_phase(), zb_progress()
 *
 * No allocation: every buffer is static. */
#include "zui_boot.h"
#include "zui_login.h"
#include "zui_tokens.h"
#include "truetype.h"

#define EXPORT(name) __attribute__((export_name(#name)))

#define MAX_W    1920
#define MAX_H    1200
#define LOGO_MAX (2u * 1024u * 1024u) /* the five logos' ZXVG, together */
#define FONT_MAX (256u * 1024u)       /* the login screen's one font    */
#define AUD_MAX  8192u

static zui_boot_t BOOT;
static uint32_t PX[MAX_W * MAX_H], FACE[MAX_W * MAX_H], BEHIND[MAX_W * MAX_H];
static uint8_t RGBA[MAX_W * MAX_H * 4];
static zui_surface_t S, F, B;
static uint8_t LOGO[LOGO_MAX];
static uint32_t logo_used, logo_at;
static int16_t AUD[AUD_MAX * 2];

/* What the vault opens onto: the login screen, drawn into the behind
 * surface. The browser door seats a visitor as a guest with no lock; the
 * screen's shapes draw even before the font arrives. */
static uint8_t FONT[FONT_MAX];
static ttf_font_t TF;
static zui_glyph_cache_t GC;
static zui_text_t T_BIG, T_SMALL, T_INI;
static zui_login_fonts_t FONTS;
static bool have_font;
static const zui_login_person_t PEOPLE[] = {
    {"Guest", 0, ZUI_LOCK_NONE, true},
};
static zui_login_t LOGIN = {PEOPLE, 1, 0, 0, 0, false, false, 0, 0, 0, 0, 0, 0, 0};

static void draw_login(zui_surface_t *b, int32_t w, int32_t h)
{
    /* the same scale as the vault: 1280x800 is reference size (sc 256) */
    int32_t sx = (int32_t)((int64_t) w * 256 / 1280),
            sy = (int32_t)((int64_t) h * 256 / 800),
            sc = sx < sy ? sx : sy;
    if (sc < 128) sc = 128;
    if (sc > 1024) sc = 1024;
    if (have_font) {
        T_BIG.size_px   = (uint32_t)((int64_t) sc * 34 / 256);
        T_SMALL.size_px = (uint32_t)((int64_t) sc * 15 / 256);
        T_INI.size_px   = (uint32_t)((int64_t) sc * 44 / 256);
        FONTS = (zui_login_fonts_t){&T_BIG, &T_SMALL, &T_INI, &T_SMALL};
    }
    zui_login_draw(b, (zui_rect_t){0, 0, w, h}, sc, &LOGIN, have_font ? &FONTS : 0, 0);
}

/* the freestanding C library pieces the compiler may call for */
void *memcpy(void *d, const void *s, unsigned long n)
{
    uint8_t *a = d;
    const uint8_t *b = s;
    while (n--) *a++ = *b++;
    return d;
}
void *memset(void *d, int c, unsigned long n)
{
    uint8_t *a = d;
    while (n--) *a++ = (uint8_t) c;
    return d;
}
void *memmove(void *d, const void *s, unsigned long n)
{
    uint8_t *a = d;
    const uint8_t *b = s;
    if (a < b)
        while (n--) *a++ = *b++;
    else
        while (n--) a[n] = b[n];
    return d;
}
int memcmp(const void *x, const void *y, unsigned long n)
{
    const uint8_t *a = x, *b = y;
    for (; n; n--, a++, b++)
        if (*a != *b) return *a < *b ? -1 : 1;
    return 0;
}

EXPORT(zb_init) void zb_init(int reduce_motion, int sound)
{
    zxl_prefs_t p = zxl_prefs_default();
    if (!sound) p.sounds = 0;
    zui_boot_init(&BOOT, &p, reduce_motion != 0);
    logo_used = 0;
}

EXPORT(zb_size) uint8_t *zb_size(int32_t w, int32_t h)
{
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > MAX_W) w = MAX_W;
    if (h > MAX_H) h = MAX_H;
    zui_surface_init(&S, PX, w, h, w);
    zui_surface_init(&F, FACE, w, h, w);
    zui_surface_init(&B, BEHIND, w, h, w);
    draw_login(&B, w, h); /* behind the doors: the login screen */
    return RGBA;
}

EXPORT(zb_font_buf) uint8_t *zb_font_buf(uint32_t n)
{
    return n <= FONT_MAX ? FONT : 0;
}

EXPORT(zb_font_set) int zb_font_set(uint32_t n)
{
    if (n > FONT_MAX || !ttf_parse(&TF, FONT, n) || !TF.valid) return -1;
    zui_glyph_cache_init(&GC);
    T_BIG = (zui_text_t){&TF, &GC, 34, false};
    T_SMALL = (zui_text_t){&TF, &GC, 15, false};
    T_INI = (zui_text_t){&TF, &GC, 44, false};
    have_font = true;
    if (B.px) draw_login(&B, B.w, B.h);
    return 0;
}

EXPORT(zb_width) int32_t zb_width(void)
{
    return S.w;
}
EXPORT(zb_height) int32_t zb_height(void)
{
    return S.h;
}

EXPORT(zb_logo_buf) uint8_t *zb_logo_buf(uint32_t n)
{
    if (n > LOGO_MAX - logo_used) return 0;
    logo_at = logo_used;
    return LOGO + logo_at;
}

EXPORT(zb_logo_set) int zb_logo_set(uint32_t which, uint32_t n)
{
    if (n > LOGO_MAX - logo_at) return -1;
    if (zui_boot_logo(&BOOT, which, LOGO + logo_at, n)) return -1;
    logo_used = logo_at + n;
    return 0;
}

EXPORT(zb_load) int zb_load(double done, double total)
{
    if (!(done >= 0) || !(total >= 0) || total > 9.0e15) return -1;
    return zui_boot_load(&BOOT, (uint64_t) done, (uint64_t) total);
}

EXPORT(zb_ready) void zb_ready(void)
{
    zui_boot_ready(&BOOT);
}
EXPORT(zb_fail) void zb_fail(void)
{
    zui_boot_fail(&BOOT);
}

EXPORT(zb_frame) void zb_frame(uint32_t ms)
{
    if (!S.px) return;
    zui_boot_tick(&BOOT, ms);
    zui_boot_draw(&BOOT, &S, &F, &B);
    uint32_t n = (uint32_t) (S.w * S.h);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t c = PX[i];
        RGBA[4 * i] = (uint8_t) (c >> 16);
        RGBA[4 * i + 1] = (uint8_t) (c >> 8);
        RGBA[4 * i + 2] = (uint8_t) c;
        RGBA[4 * i + 3] = 255;
    }
}

EXPORT(zb_audio) int16_t *zb_audio(uint32_t frames)
{
    if (frames > AUD_MAX) frames = AUD_MAX;
    zui_boot_audio(&BOOT, AUD, frames);
    return AUD;
}

EXPORT(zb_phase) int zb_phase(void)
{
    return (int) zui_boot_phase(&BOOT);
}
EXPORT(zb_progress) uint32_t zb_progress(void)
{
    return zui_boot_progress(&BOOT);
}
