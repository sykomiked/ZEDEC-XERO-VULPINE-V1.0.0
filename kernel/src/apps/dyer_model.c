/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* dyer_model.c — Dyer, the colour picker and palettes (see dyer_model.h). */
#include <string.h>

#include "dyer_model.h"

/* ---- colours ---------------------------------------------------------- */

int64_t dyer_join(int64_t r, int64_t g, int64_t b)
{
    if (!dyer_channel_ok(r) || !dyer_channel_ok(g) || !dyer_channel_ok(b)) return DYER_REFUSED;
    return (r << 16) | (g << 8) | b;
}

int dyer_split(int64_t color, int64_t *r, int64_t *g, int64_t *b)
{
    if (!r || !g || !b || color < 0 || color > DYER_COLOR_MAX) return ZXM_E_ARG;
    *r = (color >> 16) & 0xff;
    *g = (color >> 8) & 0xff;
    *b = color & 0xff;
    return ZXM_OK;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int dyer_hex_parse(const char *text, int64_t *color)
{
    if (!text || !color) return ZXM_E_ARG;
    if (*text == '#') text++;
    size_t n = strlen(text);
    if (n != 3u && n != 6u) return ZXM_E_ARG;
    int64_t v = 0;
    for (size_t i = 0; i < n; i++) {
        int x = hex_digit(text[i]);
        if (x < 0) return ZXM_E_ARG;
        v = n == 3u ? (v << 8) | (int64_t) ((x << 4) | x) : (v << 4) | x;
    }
    *color = v;
    return ZXM_OK;
}

int dyer_hex_text(int64_t color, char *out)
{
    static const char HEX[] = "0123456789abcdef";
    if (!out || color < 0 || color > DYER_COLOR_MAX) return ZXM_E_ARG;
    out[0] = '#';
    for (uint32_t i = 0; i < 6u; i++) out[1u + i] = HEX[(color >> (20u - 4u * i)) & 0xf];
    out[DYER_HEX_LEN] = 0;
    return ZXM_OK;
}

int dyer_to_hsl(int64_t color, dyer_hsl_t *hsl)
{
    int64_t r, g, b;
    if (!hsl || dyer_split(color, &r, &g, &b)) return ZXM_E_ARG;
    hsl->h = dyer_hsl_part(r, g, b, DYER_HUE);
    hsl->s = dyer_hsl_part(r, g, b, DYER_SATURATION);
    hsl->l = dyer_hsl_part(r, g, b, DYER_LIGHTNESS);
    return ZXM_OK;
}

int dyer_from_hsl(const dyer_hsl_t *hsl, int64_t *color)
{
    if (!hsl || !color) return ZXM_E_ARG;
    int64_t r = dyer_rgb_part(hsl->h, hsl->s, hsl->l, DYER_RED);
    if (r == DYER_REFUSED) return ZXM_E_ARG;
    *color = dyer_join(r, dyer_rgb_part(hsl->h, hsl->s, hsl->l, DYER_GREEN),
                       dyer_rgb_part(hsl->h, hsl->s, hsl->l, DYER_BLUE));
    return ZXM_OK;
}

int dyer_contrast(int64_t fore, int64_t back, dyer_contrast_t *c)
{
    int64_t fr, fg, fb, br, bg, bb;
    if (!c || dyer_split(fore, &fr, &fg, &fb) || dyer_split(back, &br, &bg, &bb)) return ZXM_E_ARG;
    c->fore_lum = dyer_luminance(fr, fg, fb);
    c->back_lum = dyer_luminance(br, bg, bb);
    c->ratio = dyer_contrast_x100(c->fore_lum, c->back_lum);
    c->grade = dyer_contrast_grade(c->ratio);
    return ZXM_OK;
}

bool dyer_name_ok(const char *name)
{
    size_t n = name ? strlen(name) : 0;
    if (!n || n > DYER_NAME_MAX) return false;
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_'))
            return false;
    }
    return true;
}

/* ---- a palette as text (DM2) ------------------------------------------ */

static const char HEAD[] = "dyer 1\n";

static void path_of(const char *name, char *out)
{
    size_t a = strlen(DYER_DIR), b = strlen(name), c = strlen(DYER_EXT);
    memcpy(out, DYER_DIR, a);
    out[a] = '/';
    memcpy(out + a + 1u, name, b);
    memcpy(out + a + 1u + b, DYER_EXT, c + 1u);
}

/* The whole palette's text; its length, or -1 when a swatch is not one. */
static int32_t compose(const dyer_t *d, char *out, uint32_t cap)
{
    uint32_t at = (uint32_t) strlen(HEAD);
    if (at + d->n * (DYER_HEX_LEN + 1u) + 1u > cap) return -1;
    memcpy(out, HEAD, at);
    for (uint32_t i = 0; i < d->n; i++) {
        if (!dyer_swatch_ok(i, d->color[i]) || dyer_hex_text(d->color[i], out + at)) return -1;
        at += DYER_HEX_LEN;
        out[at++] = '\n';
    }
    out[at] = 0;
    return (int32_t) at;
}

/* Parse text into color/n (DM2): false when it is not a palette. */
static bool parse(const char *s, uint32_t len, int64_t *color, uint32_t *n)
{
    uint32_t at = (uint32_t) strlen(HEAD), k = 0;
    if (len < at || memcmp(s, HEAD, at)) return false;
    while (at < len) {
        char hex[DYER_HEX_LEN + 1];
        if (len - at < DYER_HEX_LEN + 1u || s[at] != '#' || s[at + DYER_HEX_LEN] != '\n' ||
            k >= DYER_PLACES)
            return false;
        for (uint32_t i = 1; i < DYER_HEX_LEN; i++)
            if (!((s[at + i] >= '0' && s[at + i] <= '9') || (s[at + i] >= 'a' && s[at + i] <= 'f')))
                return false;
        memcpy(hex, s + at, DYER_HEX_LEN);
        hex[DYER_HEX_LEN] = 0;
        if (dyer_hex_parse(hex, &color[k]) || !dyer_swatch_ok(k, color[k])) return false;
        k++;
        at += DYER_HEX_LEN + 1u;
    }
    *n = k;
    return true;
}

static bool not_found(const zxm_link_t *l, int rc)
{
    return rc == ZXM_E_REMOTE && l->err_code == DYER_E_FILES && strstr(l->err, "no such");
}

/* ---- palettes (DM3-DM5) ------------------------------------------------ */

void dyer_init(dyer_t *d, zxm_link_t *link)
{
    memset(d, 0, sizeof *d);
    d->link = link;
}

int dyer_list(dyer_t *d)
{
    char name[DYER_NAME_MAX + sizeof DYER_EXT + 1u];
    zxm_link_t *l = d->link;
    zxm_begin(l, "files.list");
    zxm_p_str(l, "path", DYER_DIR);
    int rc = zxm_call(l);
    if (not_found(l, rc)) {
        d->nnames = 0; /* DM5: no folder yet, no palettes */
        return ZXM_OK;
    }
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "entries");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t total = zxm_len(l, arr), k = 0;
    size_t ext = strlen(DYER_EXT);
    for (uint32_t i = 0; i < total; i++) {
        int32_t e = zxm_at(l, arr, i);
        bool dir = false;
        if (zxm_get_bool(l, e, "dir", &dir)) return ZXM_E_PROTOCOL;
        if (dir) continue;
        rc = zxm_get_str(l, e, "name", name, sizeof name);
        if (rc == ZXM_E_TOOBIG) continue; /* longer than any palette's name */
        if (rc) return ZXM_E_PROTOCOL;
        size_t n = strlen(name);
        if (n <= ext || strcmp(name + n - ext, DYER_EXT)) continue;
        name[n - ext] = 0;
        if (!dyer_name_ok(name)) continue;
        if (k >= DYER_LIST_MAX) return ZXM_E_TOOBIG;
        memcpy(d->names[k++], name, n - ext + 1u);
    }
    d->nnames = k;
    return ZXM_OK;
}

int dyer_open(dyer_t *d, const char *name)
{
    static char text[DYER_TEXT_MAX];
    static char chunk[DYER_TEXT_MAX];
    static int64_t color[DYER_PLACES];
    char path[DYER_PATH_MAX];
    uint32_t len = 0, n = 0;
    uint64_t off = 0;
    if (!dyer_name_ok(name)) return ZXM_E_ARG;
    path_of(name, path);
    zxm_link_t *l = d->link;
    for (;;) {
        zxm_begin(l, "files.read");
        zxm_p_str(l, "path", path);
        zxm_p_u64(l, "offset", off);
        int rc = zxm_call(l);
        if (!off && not_found(l, rc)) break; /* DM3: not made yet, empty */
        if (rc) return rc;
        uint64_t size, next;
        bool eof;
        int32_t t = zxm_get(l, l->result, "text");
        if (zxm_get_u64(l, l->result, "size", &size) || zxm_get_u64(l, l->result, "next", &next) ||
            zxm_get_bool(l, l->result, "eof", &eof) || t < 0)
            return ZXM_E_PROTOCOL;
        if (size >= DYER_TEXT_MAX) return ZXM_E_TOOBIG;
        int32_t k = w4_json_str(l->resp, &l->tok[t], chunk, sizeof chunk);
        if (k < 0 || len + (uint32_t) k >= DYER_TEXT_MAX) return ZXM_E_TOOBIG;
        memcpy(text + len, chunk, (size_t) k);
        len += (uint32_t) k;
        if (eof) break;
        if (next <= off) return ZXM_E_PROTOCOL;
        off = next;
    }
    if (len && !parse(text, len, color, &n)) {
        d->loaded = false; /* DM3: never written over */
        d->n = 0;
        return ZXM_E_PROTOCOL;
    }
    memcpy(d->name, name, strlen(name) + 1u);
    memcpy(d->color, color, (size_t) n * sizeof color[0]);
    d->n = n;
    d->loaded = true;
    d->dirty = false;
    return ZXM_OK;
}

int dyer_add(dyer_t *d, int64_t color)
{
    if (color < 0 || color > DYER_COLOR_MAX) return ZXM_E_ARG;
    if (!d->loaded) return ZXM_E_STATE;
    if (d->n >= DYER_PLACES || !dyer_swatch_ok(d->n, color)) return ZXM_E_TOOBIG;
    d->color[d->n++] = color;
    d->dirty = true;
    return ZXM_OK;
}

int dyer_remove(dyer_t *d, uint32_t place)
{
    if (!d->loaded) return ZXM_E_STATE;
    if (place >= d->n) return ZXM_E_ARG;
    memmove(&d->color[place], &d->color[place + 1u],
            (size_t) (d->n - place - 1u) * sizeof d->color[0]);
    d->n--;
    d->dirty = true;
    return ZXM_OK;
}

int dyer_save(dyer_t *d, bool owner_confirmed)
{
    static char text[DYER_TEXT_MAX];
    char path[DYER_PATH_MAX];
    if (!d->loaded) return ZXM_E_STATE;
    int32_t len = compose(d, text, sizeof text);
    if (len < 0) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE; /* DM4: nothing is sent */
    zxm_link_t *l = d->link;
    zxm_begin(l, "files.mkdir");
    zxm_p_str(l, "path", DYER_DIR);
    int rc = zxm_call(l);
    if (rc &&
        !(rc == ZXM_E_REMOTE && l->err_code == DYER_E_FILES && strstr(l->err, "already exists")))
        return rc;
    path_of(d->name, path);
    zxm_begin(l, "files.write");
    zxm_p_str(l, "path", path);
    zxm_p_strn(l, "text", text, (uint32_t) len);
    rc = zxm_call(l);
    if (rc) return rc;
    d->dirty = false;
    return ZXM_OK;
}
