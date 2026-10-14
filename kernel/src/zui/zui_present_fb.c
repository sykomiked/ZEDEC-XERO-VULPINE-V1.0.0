/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_present_fb.c — damage rectangles, and the framebuffer present layer
 * for bare metal (UEFI GOP, ramfb) and headless runs (zui_present.h).
 *
 * The firmware hands the kernel a linear framebuffer: a base address, a
 * pitch, a size and a channel order. Presenting is copying the damaged
 * rectangles into it, converting XRGB to the panel's order on the way (P2).
 * Input arrives from the kernel's own keyboard and mouse drivers, which push
 * into a zui queue; zui_key_from_set1 turns PC scan codes into zui keys.
 */
#include "zui_present.h"

/* ---- damage ------------------------------------------------------------- */

static int32_t imin(int32_t a, int32_t b)
{
    return a < b ? a : b;
}

static int32_t imax(int32_t a, int32_t b)
{
    return a > b ? a : b;
}

static zui_rect_t unite(zui_rect_t a, zui_rect_t b)
{
    int32_t x0 = imin(a.x, b.x), y0 = imin(a.y, b.y);
    int32_t x1 = imax(a.x + a.w, b.x + b.w), y1 = imax(a.y + a.h, b.y + b.h);
    zui_rect_t r = {x0, y0, x1 - x0, y1 - y0};
    return r;
}

static int64_t area(zui_rect_t r)
{
    return (int64_t) r.w * (int64_t) r.h;
}

static bool contains(zui_rect_t a, zui_rect_t b)
{
    return b.x >= a.x && b.y >= a.y && b.x + b.w <= a.x + a.w && b.y + b.h <= a.y + a.h;
}

static bool overlaps(zui_rect_t a, zui_rect_t b)
{
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

void zui_damage_clear(zui_damage_t *d)
{
    d->n = 0;
}

void zui_damage_add(zui_damage_t *d, zui_rect_t r)
{
    if (r.w <= 0 || r.h <= 0) return;
    for (uint32_t i = 0; i < d->n; i++) {
        if (contains(d->r[i], r)) return;
        if (contains(r, d->r[i]) || overlaps(d->r[i], r)) {
            /* absorb it, then let the grown rectangle absorb any others */
            zui_rect_t g = unite(d->r[i], r);
            d->r[i] = d->r[--d->n];
            zui_damage_add(d, g);
            return;
        }
    }
    if (d->n < ZUI_DAMAGE_MAX) {
        d->r[d->n++] = r;
        return;
    }
    /* full: merge into the rectangle that grows least */
    uint32_t best = 0;
    int64_t best_cost = -1;
    for (uint32_t i = 0; i < d->n; i++) {
        int64_t cost = area(unite(d->r[i], r)) - area(d->r[i]);
        if (best_cost < 0 || cost < best_cost) {
            best_cost = cost;
            best = i;
        }
    }
    zui_rect_t g = unite(d->r[best], r);
    d->r[best] = d->r[--d->n];
    zui_damage_add(d, g);
}

void zui_damage_all(zui_damage_t *d, int32_t w, int32_t h)
{
    d->n = 0;
    zui_rect_t r = {0, 0, w, h};
    zui_damage_add(d, r);
}

zui_rect_t zui_damage_bounds(const zui_damage_t *d)
{
    zui_rect_t r = {0, 0, 0, 0};
    for (uint32_t i = 0; i < d->n; i++) r = i ? unite(r, d->r[i]) : d->r[i];
    return r;
}

int64_t zui_damage_area(const zui_damage_t *d)
{
    int64_t a = 0;
    for (uint32_t i = 0; i < d->n; i++) a += area(d->r[i]);
    return a;
}

/* ---- the framebuffer backend -------------------------------------------- */

static zui_fb_t *FB(zui_present_t *p)
{
    return (zui_fb_t *) p->impl;
}

static int fb_open(zui_present_t *p, const char *title, int32_t w_pt, int32_t h_pt)
{
    (void) title;
    (void) w_pt;
    (void) h_pt;
    zui_fb_t *fb = FB(p);
    if (!fb || !fb->base || fb->w == 0 || fb->h == 0 || fb->pitch_px < fb->w)
        return ZUI_PRESENT_E_UNAVAILABLE;
    p->screen.px_w = fb->w;
    p->screen.px_h = fb->h;
    p->screen.scale_q8 = fb->scale_q8 ? fb->scale_q8 : 256u;
    p->screen.input = ZUI_INPUT_POINTER;
    return ZUI_PRESENT_OK;
}

static void fb_close(zui_present_t *p)
{
    (void) p;
}

static int fb_present(zui_present_t *p, const zui_surface_t *s, const zui_damage_t *d)
{
    zui_fb_t *fb = FB(p);
    if (!fb || !fb->base || !s || !d) return ZUI_PRESENT_E_ARG;
    uint64_t copied = 0;
    int32_t cw = imin(s->w, (int32_t) fb->w), ch = imin(s->h, (int32_t) fb->h);
    for (uint32_t i = 0; i < d->n; i++) {
        int32_t x0 = imax(d->r[i].x, 0), y0 = imax(d->r[i].y, 0);
        int32_t x1 = imin(d->r[i].x + d->r[i].w, cw), y1 = imin(d->r[i].y + d->r[i].h, ch);
        for (int32_t y = y0; y < y1; y++) {
            const uint32_t *src = s->px + (int64_t) y * s->stride;
            volatile uint32_t *dst = fb->base + (uint64_t) y * fb->pitch_px;
            if (fb->format == ZUI_FB_XRGB) {
                for (int32_t x = x0; x < x1; x++) dst[x] = src[x] & 0xFFFFFFu;
            } else {
                for (int32_t x = x0; x < x1; x++) {
                    uint32_t c = src[x];
                    dst[x] = ((c & 0xFFu) << 16) | (c & 0xFF00u) | ((c >> 16) & 0xFFu);
                }
            }
            if (x1 > x0) copied += (uint64_t) (x1 - x0);
        }
    }
    if (copied) {
        p->frames++;
        p->px_copied += copied;
    }
    return ZUI_PRESENT_OK;
}

static int fb_poll(zui_present_t *p, zui_queue_t *q, uint32_t wait_ms)
{
    (void) wait_ms; /* bare metal never blocks here: the scheduler idles */
    zui_fb_t *fb = FB(p);
    if (!fb || !fb->drv_queue || fb->drv_queue == q) return 0;
    int n = 0;
    zui_event_t e;
    while (zui_pop(fb->drv_queue, &e)) {
        zui_push(q, &e);
        n++;
    }
    return n;
}

static uint32_t fb_now(zui_present_t *p)
{
    zui_fb_t *fb = FB(p);
    if (!fb) return 0;
    return fb->clock_ms ? fb->clock_ms() : fb->fake_ms;
}

const zui_present_ops_t zui_present_fb_ops = {"framebuffer", fb_open, fb_close,
                                              fb_present,    fb_poll, fb_now};

void zui_present_fb_bind(zui_present_t *p, zui_fb_t *fb)
{
    p->ops = &zui_present_fb_ops;
    p->impl = fb;
    p->screen.px_w = fb ? fb->w : 0;
    p->screen.px_h = fb ? fb->h : 0;
    p->screen.scale_q8 = fb && fb->scale_q8 ? fb->scale_q8 : 256u;
    p->screen.input = ZUI_INPUT_POINTER;
    p->frames = 0;
    p->px_copied = 0;
}

/* ---- PC scan code set 1 --------------------------------------------------- */

/* Make codes 0x00..0x3A to zui keys (US layout, unshifted). */
static const uint8_t SET1[0x3B] = {0,
                                   ZUI_KEY_ESCAPE,
                                   '1',
                                   '2',
                                   '3',
                                   '4',
                                   '5',
                                   '6',
                                   '7',
                                   '8',
                                   '9',
                                   '0',
                                   '-',
                                   '=',
                                   ZUI_KEY_BACKSPACE,
                                   ZUI_KEY_TAB,
                                   'q',
                                   'w',
                                   'e',
                                   'r',
                                   't',
                                   'y',
                                   'u',
                                   'i',
                                   'o',
                                   'p',
                                   '[',
                                   ']',
                                   ZUI_KEY_ENTER,
                                   0 /* ctrl */,
                                   'a',
                                   's',
                                   'd',
                                   'f',
                                   'g',
                                   'h',
                                   'j',
                                   'k',
                                   'l',
                                   ';',
                                   '\'',
                                   '`',
                                   0 /* lshift */,
                                   '\\',
                                   'z',
                                   'x',
                                   'c',
                                   'v',
                                   'b',
                                   'n',
                                   'm',
                                   ',',
                                   '.',
                                   '/',
                                   0 /* rshift */,
                                   '*',
                                   0 /* alt */,
                                   ' ',
                                   0 /* caps */};

bool zui_key_from_set1(uint8_t byte, bool *e0, uint16_t *key, bool *down, uint8_t *mods)
{
    if (byte == 0xE0) {
        *e0 = true;
        return false;
    }
    bool ext = *e0;
    *e0 = false;
    bool up = (byte & 0x80u) != 0;
    uint8_t code = byte & 0x7Fu;
    uint8_t bit = 0;
    if (code == 0x2A || code == 0x36) bit = ZUI_MOD_SHIFT;
    if (code == 0x1D) bit = ZUI_MOD_CTRL | ZUI_MOD_CMD;
    if (code == 0x38) bit = ZUI_MOD_ALT;
    if (ext && (code == 0x5B || code == 0x5C)) bit = ZUI_MOD_META;
    if (bit) {
        if (ext && (code == 0x2A || code == 0x36)) return false; /* fake shifts */
        if (up)
            *mods &= (uint8_t) ~bit;
        else
            *mods |= bit;
        return false;
    }
    uint16_t k = 0;
    if (ext) {
        switch (code) {
        case 0x48:
            k = ZUI_KEY_UP;
            break;
        case 0x50:
            k = ZUI_KEY_DOWN;
            break;
        case 0x4B:
            k = ZUI_KEY_LEFT;
            break;
        case 0x4D:
            k = ZUI_KEY_RIGHT;
            break;
        case 0x47:
            k = ZUI_KEY_HOME;
            break;
        case 0x4F:
            k = ZUI_KEY_END;
            break;
        case 0x49:
            k = ZUI_KEY_PAGE_UP;
            break;
        case 0x51:
            k = ZUI_KEY_PAGE_DOWN;
            break;
        case 0x52:
            k = ZUI_KEY_INSERT;
            break;
        case 0x53:
            k = ZUI_KEY_DELETE;
            break;
        case 0x1C:
            k = ZUI_KEY_ENTER;
            break; /* keypad enter */
        case 0x5D:
            k = ZUI_KEY_MENU;
            break;
        default:
            return false;
        }
    } else if (code < sizeof SET1) {
        k = SET1[code];
    } else if (code >= 0x3B && code <= 0x44) {
        k = (uint16_t) (ZUI_KEY_F1 + (code - 0x3B));
    } else if (code == 0x57 || code == 0x58) {
        k = (uint16_t) (ZUI_KEY_F1 + 10 + (code - 0x57));
    }
    if (!k) return false;
    *key = k;
    *down = !up;
    return true;
}
