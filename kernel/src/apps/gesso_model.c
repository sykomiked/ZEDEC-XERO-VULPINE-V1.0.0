/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gesso_model.c — Gesso's model (gesso_model.h). */
#include <string.h>

#include "gesso_model.h"

#define CHUNK 4096u /* values turned to int64 at a time for a counting rule */

static uint32_t nbytes(const gesso_t *g)
{
    return (uint32_t) g->width * (uint32_t) g->height * (uint32_t) g->channels;
}

/* GM3: the values an edit left at 0 and 255, by witness.kind_count */
static void count_held(gesso_t *g)
{
    static int64_t v[CHUNK];
    uint32_t n = nbytes(g);
    g->held_dark = g->held_light = 0;
    for (uint32_t at = 0; at < n; at += CHUNK) {
        uint32_t k = n - at < CHUNK ? n - at : CHUNK;
        for (uint32_t i = 0; i < k; i++) v[i] = g->px[at + i];
        g->held_dark += witness_kind_count(v, k, 0);
        g->held_light += witness_kind_count(v, k, GESSO_PX_MAX);
    }
}

void gesso_init(gesso_t *g, zxm_link_t *link)
{
    memset(g, 0, sizeof *g);
    g->link = link;
}

int gesso_new(gesso_t *g, int64_t width, int64_t height, int64_t channels, int64_t grey)
{
    if (!gesso_canvas_ok(width, height) || (channels != 1 && channels != GESSO_CHANNELS_MAX) ||
        grey < 0 || grey > GESSO_PX_MAX)
        return ZXM_E_ARG;
    g->width = (int32_t) width;
    g->height = (int32_t) height;
    g->channels = (int32_t) channels;
    memset(g->px, (int) grey, nbytes(g));
    g->space[0] = g->path[0] = g->saved[0] = 0;
    g->nlayers = 0;
    g->dirty = true;
    count_held(g);
    return ZXM_OK;
}

/* ---- GM3: edits ---- */

int gesso_brightness(gesso_t *g, int64_t delta)
{
    if (!g->width) return ZXM_E_STATE;
    if (gesso_bright_px(0, delta) == GESSO_REFUSED) return ZXM_E_ARG;
    uint32_t n = nbytes(g);
    for (uint32_t i = 0; i < n; i++) g->px[i] = (uint8_t) gesso_bright_px(g->px[i], delta);
    if (delta && n) g->dirty = true;
    count_held(g);
    return ZXM_OK;
}

int gesso_contrast(gesso_t *g, int64_t gain)
{
    if (!g->width) return ZXM_E_STATE;
    if (gesso_contrast_px(0, gain) == GESSO_REFUSED) return ZXM_E_ARG;
    uint32_t n = nbytes(g);
    for (uint32_t i = 0; i < n; i++) g->px[i] = (uint8_t) gesso_contrast_px(g->px[i], gain);
    if (gain != GESSO_UNITY && n) g->dirty = true;
    count_held(g);
    return ZXM_OK;
}

int gesso_wash(gesso_t *g, int64_t grey, int64_t alpha)
{
    if (!g->width) return ZXM_E_STATE;
    if (gesso_blend_px(0, grey, alpha) == GESSO_REFUSED) return ZXM_E_ARG;
    uint32_t n = nbytes(g);
    for (uint32_t i = 0; i < n; i++) g->px[i] = (uint8_t) gesso_blend_px(g->px[i], grey, alpha);
    if (alpha && n) g->dirty = true;
    count_held(g);
    return ZXM_OK;
}

/* The opacity a dab lays at a squared (doubled) distance d2 from its
 * centre: full inside the core h2, falling with d2 to 0 at the rim r2. */
static int64_t dab_alpha(int64_t d2, int64_t r2, int64_t h2, int64_t opacity)
{
    if (d2 > r2) return 0;
    if (d2 <= h2) return opacity;
    return opacity * (r2 - d2) / (r2 - h2);
}

int gesso_dab(gesso_t *g, int64_t x, int64_t y, int64_t size, const uint8_t *color, int64_t opacity,
              int64_t hardness)
{
    if (!g->width) return ZXM_E_STATE;
    if (!color || !gesso_brush_ok(size, opacity, hardness)) return ZXM_E_ARG;
    int64_t x0 = x - size / 2, y0 = y - size / 2;
    int64_t cols = hollow_shown(x0, size, g->width), rows = hollow_shown(y0, size, g->height);
    if (cols == HOLLOW_REFUSED || rows == HOLLOW_REFUSED) return ZXM_E_ARG;
    int64_t c0 = x0 > 0 ? x0 : 0, r0 = y0 > 0 ? y0 : 0;
    /* in doubled units, so the centre of an even brush is whole */
    int64_t cx = 2 * x0 + size - 1, cy = 2 * y0 + size - 1;
    int64_t r2 = size * size, h2 = r2 * hardness * hardness / (GESSO_HARD_MAX * GESSO_HARD_MAX);
    uint32_t ch = (uint32_t) g->channels;
    for (int64_t r = r0; r < r0 + rows; r++) {
        int64_t dy = 2 * r - cy;
        for (int64_t c = c0; c < c0 + cols; c++) {
            int64_t dx = 2 * c - cx;
            int64_t a = dab_alpha(dx * dx + dy * dy, r2, h2, opacity);
            uint8_t *p = g->px + ((uint32_t) r * (uint32_t) g->width + (uint32_t) c) * ch;
            for (uint32_t k = 0; k < ch; k++) p[k] = (uint8_t) gesso_blend_px(p[k], color[k], a);
        }
    }
    if (rows && cols && opacity) g->dirty = true;
    count_held(g);
    return ZXM_OK;
}

/* ---- GR4: counts ---- */

int gesso_range(const gesso_t *g, int64_t channel, int64_t lo, int64_t hi, int64_t *count)
{
    static int64_t v[CHUNK];
    if (!g->width) return ZXM_E_STATE;
    if (!count || channel < 0 || channel >= g->channels) return ZXM_E_ARG;
    uint32_t ch = (uint32_t) g->channels, n = (uint32_t) g->width * (uint32_t) g->height;
    *count = 0;
    for (uint32_t at = 0; at < n; at += CHUNK) {
        uint32_t k = n - at < CHUNK ? n - at : CHUNK;
        for (uint32_t i = 0; i < k; i++) v[i] = g->px[(at + i) * ch + (uint32_t) channel];
        *count += gesso_range_count(v, k, lo, hi);
    }
    return ZXM_OK;
}

int gesso_histogram(const gesso_t *g, int64_t channel, int64_t width, int64_t *bins,
                    uint32_t *nbins)
{
    if (!g->width) return ZXM_E_STATE;
    if (!bins || !nbins || channel < 0 || channel >= g->channels || width < 1 ||
        width > (int64_t) GESSO_BINS_MAX)
        return ZXM_E_ARG;
    uint32_t ch = (uint32_t) g->channels, n = (uint32_t) g->width * (uint32_t) g->height;
    uint32_t w = (uint32_t) width, nb = (GESSO_BINS_MAX + w - 1u) / w;
    for (uint32_t b = 0; b < nb; b++) bins[b] = 0;
    /* one pass: value v lies in bin v / w, the one range of GR4 that holds it */
    for (uint32_t i = 0; i < n; i++) bins[g->px[i * ch + (uint32_t) channel] / w]++;
    *nbins = nb;
    return ZXM_OK;
}

/* ---- GM4: layers ---- */

int gesso_layer_add(gesso_t *g, int64_t kind, int64_t amount, int64_t opacity)
{
    if (!g->width) return ZXM_E_STATE;
    if (g->nlayers >= GESSO_LAYERS_MAX) return ZXM_E_STATE;
    if (!gesso_layer_ok(kind, amount, opacity, 1) ||
        !gesso_record_fits(g->nlayers, kind, amount, opacity, 1))
        return ZXM_E_ARG;
    gesso_layer_t *l = &g->layer[g->nlayers++];
    l->kind = (int32_t) kind;
    l->amount = (int32_t) amount;
    l->opacity = (int32_t) opacity;
    l->visible = true;
    return ZXM_OK;
}

int gesso_layer_show(gesso_t *g, uint32_t place, bool visible)
{
    if (place >= g->nlayers) return ZXM_E_ARG;
    g->layer[place].visible = visible;
    return ZXM_OK;
}

int gesso_layer_drop(gesso_t *g, uint32_t place)
{
    if (place >= g->nlayers) return ZXM_E_ARG;
    memmove(g->layer + place, g->layer + place + 1,
            (size_t) (g->nlayers - place - 1u) * sizeof g->layer[0]);
    g->nlayers--;
    return ZXM_OK;
}

/* One layer's adjusted value of v (GR1, GR2, or the wash's grey). */
static int64_t layer_value(const gesso_layer_t *l, int64_t v)
{
    if (l->kind == GESSO_LAYER_BRIGHT) return gesso_bright_px(v, l->amount);
    if (l->kind == GESSO_LAYER_CONTRAST) return gesso_contrast_px(v, l->amount);
    return l->amount;
}

int gesso_flatten(gesso_t *g)
{
    if (!g->width) return ZXM_E_STATE;
    uint32_t n = nbytes(g);
    bool changed = false;
    for (uint32_t k = 0; k < g->nlayers; k++) {
        const gesso_layer_t *l = &g->layer[k];
        if (!l->visible) continue;
        for (uint32_t i = 0; i < n; i++)
            g->px[i] = (uint8_t) gesso_blend_px(g->px[i], layer_value(l, g->px[i]), l->opacity);
        changed = changed || (l->opacity && n);
    }
    g->nlayers = 0;
    if (changed) g->dirty = true;
    count_held(g);
    return ZXM_OK;
}

/* ---- GM1: PGM and PPM, as base64 ---- */

static uint8_t FILEBUF[GESSO_FILE_MAX];

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* RFC 4648, padded; out holds 4 * ceil(n / 3) bytes. Its length. */
static uint32_t b64_enc(const uint8_t *in, uint32_t n, char *out)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t) in[i] << 16;
        if (i + 1 < n) v |= (uint32_t) in[i + 1] << 8;
        if (i + 2 < n) v |= in[i + 2];
        out[k++] = B64[(v >> 18) & 63u];
        out[k++] = B64[(v >> 12) & 63u];
        out[k++] = i + 1 < n ? B64[(v >> 6) & 63u] : '=';
        out[k++] = i + 2 < n ? B64[v & 63u] : '=';
    }
    return k;
}

static int32_t b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Padded base64 into out; the bytes decoded, or -1 (not base64, or no room). */
static int32_t b64_dec(const char *in, uint32_t n, uint8_t *out, uint32_t cap)
{
    uint32_t k = 0;
    if (n % 4u) return -1;
    for (uint32_t i = 0; i < n; i += 4) {
        int32_t v[4];
        uint32_t pad = 0;
        for (uint32_t j = 0; j < 4; j++) {
            bool tail = i + 4 == n && j >= 2 && in[i + j] == '=';
            if (tail) {
                if (j == 2 && in[i + 3] != '=') return -1;
                pad++;
                v[j] = 0;
                continue;
            }
            v[j] = b64_val(in[i + j]);
            if (v[j] < 0) return -1;
        }
        uint32_t w =
            (uint32_t) v[0] << 18 | (uint32_t) v[1] << 12 | (uint32_t) v[2] << 6 | (uint32_t) v[3];
        uint32_t got = 3u - pad;
        if (k + got > cap) return -1;
        out[k++] = (uint8_t) (w >> 16);
        if (got > 1) out[k++] = (uint8_t) (w >> 8);
        if (got > 2) out[k++] = (uint8_t) w;
    }
    return (int32_t) k;
}

static uint32_t put_num(uint8_t *p, uint32_t v)
{
    char d[10];
    uint32_t n = 0, k = 0;
    do {
        d[n++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n) p[k++] = (uint8_t) d[--n];
    return k;
}

/* The image as a PGM or PPM in FILEBUF; its length. */
static uint32_t pnm_encode(const gesso_t *g)
{
    uint32_t k = 0;
    FILEBUF[k++] = 'P';
    FILEBUF[k++] = g->channels == 1 ? '5' : '6';
    FILEBUF[k++] = '\n';
    k += put_num(FILEBUF + k, (uint32_t) g->width);
    FILEBUF[k++] = ' ';
    k += put_num(FILEBUF + k, (uint32_t) g->height);
    FILEBUF[k++] = '\n';
    k += put_num(FILEBUF + k, GESSO_PX_MAX);
    FILEBUF[k++] = '\n';
    memcpy(FILEBUF + k, g->px, nbytes(g));
    return k + nbytes(g);
}

static bool pnm_space(uint8_t c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

/* The next header number at *at (whitespace and # comments before it):
 * 1..99999, or -1. */
static int64_t pnm_num(uint32_t len, uint32_t *at)
{
    uint32_t p = *at;
    for (;;) {
        while (p < len && p < GESSO_HEAD_MAX && pnm_space(FILEBUF[p])) p++;
        if (p >= len || p >= GESSO_HEAD_MAX) return -1;
        if (FILEBUF[p] != '#') break;
        while (p < len && p < GESSO_HEAD_MAX && FILEBUF[p] != '\n') p++;
    }
    int64_t v = 0;
    uint32_t digits = 0;
    while (p < len && FILEBUF[p] >= '0' && FILEBUF[p] <= '9') {
        if (++digits > 5) return -1;
        v = v * 10 + (FILEBUF[p++] - '0');
    }
    if (!digits || v < 1) return -1;
    *at = p;
    return v;
}

/* A PGM or PPM of len bytes in FILEBUF: its shape and where its raster is. */
static int pnm_decode(uint32_t len, int64_t *w, int64_t *h, int64_t *ch, uint32_t *at)
{
    uint32_t p = 2;
    if (len < 2 || FILEBUF[0] != 'P' || (FILEBUF[1] != '5' && FILEBUF[1] != '6')) return ZXM_E_ARG;
    *ch = FILEBUF[1] == '5' ? 1 : GESSO_CHANNELS_MAX;
    *w = pnm_num(len, &p);
    *h = pnm_num(len, &p);
    int64_t maxval = pnm_num(len, &p);
    if (*w < 0 || *h < 0 || maxval != GESSO_PX_MAX || !gesso_canvas_ok(*w, *h)) return ZXM_E_ARG;
    if (p >= len || !pnm_space(FILEBUF[p])) return ZXM_E_ARG; /* one byte ends the header */
    p++;
    if ((uint64_t) len - p != (uint64_t) *w * (uint64_t) *h * (uint64_t) *ch) return ZXM_E_ARG;
    *at = p;
    return ZXM_OK;
}

static bool path_ok(const char *path)
{
    size_t n = path ? strlen(path) : 0;
    return n && n <= GESSO_PATH_MAX;
}

/* Bytes in one request: a multiple of 3, so every piece is whole base64. */
#define PIECE 9000u

/* GM5 and GM6: the image written whole to path in g's space. */
static int write_all(gesso_t *g, const char *path, bool exclusive)
{
    static char enc[(PIECE / 3u) * 4u];
    zxm_link_t *l = g->link;
    uint32_t len = pnm_encode(g), at = 0;
    bool first = true;
    do {
        uint32_t n = len - at < PIECE ? len - at : PIECE;
        bool last = at + n >= len;
        uint32_t k = b64_enc(FILEBUF + at, n, enc);
        zxm_begin(l, "files.write");
        zxm_p_space(l, g->space);
        zxm_p_str(l, "path", path);
        zxm_p_strn(l, "base64", enc, k);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        if (exclusive) zxm_p_bool(l, "exclusive", true);
        int rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
        zxm_progress(l, at, len);
    } while (at < len);
    return ZXM_OK;
}

int gesso_save_as(gesso_t *g, const char *path)
{
    if (!g->width) return ZXM_E_STATE;
    if (!path_ok(path)) return ZXM_E_ARG;
    int rc = write_all(g, path, true);
    if (rc) return rc;
    memcpy(g->saved, path, strlen(path) + 1);
    g->dirty = false;
    return ZXM_OK;
}

int gesso_save_over(gesso_t *g, bool owner_confirmed)
{
    if (!g->width || !g->path[0] || !owner_confirmed) return ZXM_E_STATE;
    int rc = write_all(g, g->path, false);
    if (rc) return rc;
    g->dirty = false;
    return ZXM_OK;
}

int gesso_open(gesso_t *g, const char *space, const char *path)
{
    static char chunk[66u * 1024u]; /* base64 of one 48 KiB read */
    zxm_link_t *l = g->link;
    uint32_t len = 0;
    uint64_t off = 0;
    if (!path_ok(path) || !space || strlen(space) > ZXM_SPACE_MAX) return ZXM_E_ARG;
    for (;;) {
        zxm_begin(l, "files.read");
        zxm_p_space(l, space);
        zxm_p_str(l, "path", path);
        zxm_p_u64(l, "offset", off);
        zxm_p_str(l, "encoding", "base64");
        int rc = zxm_call(l);
        if (rc) return rc;
        uint64_t size, next;
        bool eof;
        int32_t t = zxm_get(l, l->result, "base64");
        if (zxm_get_u64(l, l->result, "size", &size) || zxm_get_u64(l, l->result, "next", &next) ||
            zxm_get_bool(l, l->result, "eof", &eof) || t < 0)
            return ZXM_E_PROTOCOL;
        if (size > GESSO_FILE_MAX) return ZXM_E_TOOBIG;
        int32_t n = w4_json_str(l->resp, &l->tok[t], chunk, sizeof chunk);
        if (n < 0) return ZXM_E_TOOBIG;
        int32_t got = b64_dec(chunk, (uint32_t) n, FILEBUF + len, GESSO_FILE_MAX - len);
        if (got < 0) return ZXM_E_PROTOCOL;
        len += (uint32_t) got;
        zxm_progress(l, len, size);
        if (eof) break;
        if (next <= off) return ZXM_E_PROTOCOL;
        off = next;
    }
    /* checked apart, so a file that is no image leaves the image as it was */
    int64_t w = 0, h = 0, ch = 0;
    uint32_t at = 0;
    int rc = pnm_decode(len, &w, &h, &ch, &at);
    if (rc) return rc;
    g->width = (int32_t) w;
    g->height = (int32_t) h;
    g->channels = (int32_t) ch;
    memcpy(g->px, FILEBUF + at, nbytes(g));
    memcpy(g->space, space, strlen(space) + 1);
    if (!strcmp(g->space, "home")) g->space[0] = 0;
    memcpy(g->path, path, strlen(path) + 1);
    g->saved[0] = 0;
    g->nlayers = 0;
    g->dirty = false;
    count_held(g);
    return ZXM_OK;
}
