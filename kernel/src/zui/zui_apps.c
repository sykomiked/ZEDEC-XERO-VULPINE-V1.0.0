/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_apps.c — the app catalogue and icon pack (zui_apps.h AP1..AP4). */
#include "zui_apps.h"

static uint32_t rd16(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8);
}
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

/* the string at offset o of the area ends with a NUL before the area ends */
static bool string_ok(const zui_apps_t *a, uint32_t end, uint32_t o)
{
    if (o >= end - a->strings) return false;
    for (uint32_t i = a->strings + o; i < end; i++)
        if (a->d[i] == 0) return true;
    return false;
}

int zui_apps_bind(zui_apps_t *a, const uint8_t *pack, uint32_t n)
{
    a->d = 0;
    a->n = 0;
    a->count = 0;
    a->strings = 0;
    if (!pack || n < ZUI_APPS_HEAD) return -1;
    if (pack[0] != 'Z' || pack[1] != 'X' || pack[2] != 'A' || pack[3] != 'P') return -1;
    if (rd16(pack + 4) != ZUI_APPS_VERSION) return -1;
    uint32_t count = rd16(pack + 6), so = rd32(pack + 8), total = rd32(pack + 12);
    if (count == 0 || count > ZUI_APPS_MAX || total != n) return -1;
    if (so != ZUI_APPS_HEAD + count * ZUI_APPS_ENTRY || so >= n) return -1;
    zui_apps_t t = {pack, n, count, so};
    uint32_t send = n; /* the string area ends where the first icon starts */
    for (uint32_t i = 0; i < count; i++) {
        uint32_t io = rd32(pack + ZUI_APPS_HEAD + i * ZUI_APPS_ENTRY + 28);
        if (io < send) send = io;
    }
    if (send <= so) return -1;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = pack + ZUI_APPS_HEAD + i * ZUI_APPS_ENTRY;
        if (rd16(e) != i + 1) return -1;
        if (e[2] < 1 || e[2] > 3 || e[3] != 0) return -1;
        for (uint32_t k = 0; k < 6; k++)
            if (!string_ok(&t, send, rd32(e + 4 + 4 * k))) return -1;
        uint32_t io = rd32(e + 28), il = rd32(e + 32);
        if (io & 3u || il == 0 || io > n || il > n - io) return -1;
        if (zui_zxvg_check(pack + io, il) != 0) return -1;
    }
    *a = t;
    return 0;
}

uint32_t zui_apps_count(const zui_apps_t *a)
{
    return a->d ? a->count : 0;
}

bool zui_apps_get(const zui_apps_t *a, uint32_t i, zui_app_t *out)
{
    if (!a->d || i >= a->count) return false;
    const uint8_t *e = a->d + ZUI_APPS_HEAD + i * ZUI_APPS_ENTRY;
    const char *s = (const char *) (a->d + a->strings);
    out->number = rd16(e);
    out->layer = e[2];
    out->name = s + rd32(e + 4);
    out->category = s + rd32(e + 8);
    out->does = s + rd32(e + 12);
    out->built_from = s + rd32(e + 16);
    out->pillars = s + rd32(e + 20);
    out->paraconsistent = s + rd32(e + 24);
    out->icon = a->d + rd32(e + 28);
    out->icon_len = rd32(e + 32);
    return true;
}

static char lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
}

static bool same(const char *x, const char *y)
{
    for (; *x && *y; x++, y++)
        if (lower(*x) != lower(*y)) return false;
    return *x == *y;
}

static bool contains(const char *hay, const char *needle)
{
    for (; *hay; hay++) {
        uint32_t k = 0;
        while (needle[k] && hay[k] && lower(hay[k]) == lower(needle[k])) k++;
        if (!needle[k]) return true;
    }
    return false;
}

int32_t zui_apps_find(const zui_apps_t *a, const char *name)
{
    zui_app_t app;
    for (uint32_t i = 0; name && zui_apps_get(a, i, &app); i++)
        if (same(app.name, name)) return (int32_t) i;
    return -1;
}

uint32_t zui_apps_in_layer(const zui_apps_t *a, uint32_t layer)
{
    uint32_t n = 0;
    zui_app_t app;
    for (uint32_t i = 0; zui_apps_get(a, i, &app); i++)
        if (!layer || app.layer == layer) n++;
    return n;
}

bool zui_apps_match(const zui_app_t *app, const char *needle)
{
    if (!needle || !needle[0]) return true;
    return contains(app->name, needle) || contains(app->category, needle) ||
           contains(app->does, needle);
}

int zui_app_icon(zui_surface_t *s, zui_vec_work_t *w, const zui_app_t *app, zui_rect_t box,
                 uint32_t opacity)
{
    if (!app || !app->icon || box.w <= 0 || box.h <= 0) return -1;
    zui_vec_opts_t o = {0};
    o.opacity = opacity;
    return zui_vec_draw_zxvg(s, w, app->icon, app->icon_len, box, &o);
}
