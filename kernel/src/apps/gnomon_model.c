/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gnomon_model.c — Gnomon's model (gnomon_model.h). */
#include <string.h>

#include "gnomon_model.h"

/* ---- the figure ---- */

void gnomon_init(gnomon_t *g, zxm_link_t *link)
{
    memset(g, 0, sizeof *g);
    g->link = link;
    g->kind = GNOMON_KIND_POLY;
}

void gnomon_clear(gnomon_t *g)
{
    g->n = 0;
    g->kind = GNOMON_KIND_POLY;
    g->heading = 0;
    g->tx = 0;
    g->ty = 0;
    g->closed = false;
    g->dirty = true;
}

/* GM2: one vertex at the end, checked first */
static int put_vertex(gnomon_t *g, int64_t x, int64_t y)
{
    if (!gnomon_coord_ok(x) || !gnomon_coord_ok(y)) return ZXM_E_ARG;
    if (g->n >= GNOMON_VERTS_MAX) return ZXM_E_TOOBIG;
    g->x[g->n] = x;
    g->y[g->n] = y;
    g->n++;
    g->dirty = true;
    return ZXM_OK;
}

int gnomon_add_point(gnomon_t *g, int64_t x, int64_t y)
{
    if (g->kind != GNOMON_KIND_POLY) return ZXM_E_STATE;
    return put_vertex(g, x, y);
}

/* ---- GM3: the turtle ---- */

void gnomon_turtle_start(gnomon_t *g)
{
    gnomon_clear(g);
    g->kind = GNOMON_KIND_TURTLE;
    (void) put_vertex(g, 0, 0);
}

int gnomon_turtle_turn(gnomon_t *g, int64_t quarters)
{
    if (g->kind != GNOMON_KIND_TURTLE || g->closed) return ZXM_E_STATE;
    int64_t h = (g->heading + quarters % 4) % 4;
    if (h < 0) h += 4;
    g->heading = (int32_t) h;
    return ZXM_OK;
}

int gnomon_turtle_forward(gnomon_t *g, int64_t steps)
{
    static const int8_t DX[4] = {1, 0, -1, 0}, DY[4] = {0, 1, 0, -1};
    if (g->kind != GNOMON_KIND_TURTLE || g->closed) return ZXM_E_STATE;
    if (steps < 1 || steps > 2 * GNOMON_COORD_MAX) return ZXM_E_ARG;
    int64_t x = g->tx + DX[g->heading] * steps, y = g->ty + DY[g->heading] * steps;
    if (!gnomon_coord_ok(x) || !gnomon_coord_ok(y)) return ZXM_E_ARG;
    if (x == g->x[0] && y == g->y[0]) { /* back at the start: the path closes */
        g->tx = x;
        g->ty = y;
        g->closed = true;
        g->dirty = true;
        return ZXM_OK;
    }
    int rc = put_vertex(g, x, y);
    if (rc) return rc;
    g->tx = x;
    g->ty = y;
    return ZXM_OK;
}

/* ---- GM4: measures ---- */

int gnomon_area2(const gnomon_t *g, int64_t *twice)
{
    if (!twice) return ZXM_E_ARG;
    if (g->n < 3u) return ZXM_E_STATE;
    return gnomon_polygon_area2(g->x, g->y, g->n, twice) ? ZXM_OK : ZXM_E_ARG;
}

int gnomon_area(const gnomon_t *g, int64_t *num, int64_t *den)
{
    if (!num || !den) return ZXM_E_ARG;
    if (g->n < 3u) return ZXM_E_STATE;
    return gnomon_polygon_area(g->x, g->y, g->n, num, den) ? ZXM_OK : ZXM_E_ARG;
}

static uint32_t after(const gnomon_t *g, uint32_t i)
{
    return i + 1u == g->n ? 0u : i + 1u;
}

static uint32_t before(const gnomon_t *g, uint32_t i)
{
    return i ? i - 1u : g->n - 1u;
}

int gnomon_side_sq(const gnomon_t *g, uint32_t i, int64_t *sq)
{
    if (!sq || i >= g->n) return ZXM_E_ARG;
    if (g->n < 2u) return ZXM_E_STATE;
    uint32_t j = after(g, i);
    return gnomon_side_len_sq(g->x[i], g->y[i], g->x[j], g->y[j], sq) ? ZXM_OK : ZXM_E_ARG;
}

int gnomon_slope(const gnomon_t *g, uint32_t i, int64_t *num, int64_t *den)
{
    if (!num || !den || i >= g->n) return ZXM_E_ARG;
    if (g->n < 2u) return ZXM_E_STATE;
    uint32_t j = after(g, i);
    int64_t dx = g->x[j] - g->x[i], dy = g->y[j] - g->y[i];
    if (!dx) return ZXM_E_STATE; /* a vertical side has no slope */
    rat_t s = rat_make(dy, dx);
    if (!s.valid) return ZXM_E_ARG;
    *num = s.num;
    *den = s.den;
    return ZXM_OK;
}

int gnomon_turn_at(const gnomon_t *g, uint32_t i, int64_t *twice)
{
    if (!twice || i >= g->n) return ZXM_E_ARG;
    if (g->n < 3u) return ZXM_E_STATE;
    uint32_t p = before(g, i), q = after(g, i);
    return gnomon_triangle_area2(g->x[p], g->y[p], g->x[i], g->y[i], g->x[q], g->y[q], twice)
               ? ZXM_OK
               : ZXM_E_ARG;
}

int gnomon_convex(const gnomon_t *g, bool *convex)
{
    if (!convex) return ZXM_E_ARG;
    if (g->n < 3u) return ZXM_E_STATE;
    bool left = false, right = false;
    for (uint32_t i = 0; i < g->n; i++) {
        int64_t t = 0;
        int rc = gnomon_turn_at(g, i, &t);
        if (rc) return rc;
        left = left || t > 0;
        right = right || t < 0;
    }
    *convex = left != right;
    return ZXM_OK;
}

/* ---- GM5 ---- */

bool gnomon_record(const gnomon_t *g, int64_t figure, int64_t mtime)
{
    return gnomon_figure_record_ok(figure, g->n, g->kind, mtime);
}

/* ---- GM1: text out ---- */

typedef struct {
    char *s;
    uint32_t n, cap;
    bool over;
} out_t;

static void put_ch(out_t *o, char c)
{
    if (o->n + 1u >= o->cap) {
        o->over = true;
        return;
    }
    o->s[o->n++] = c;
}

static void put_str(out_t *o, const char *s)
{
    while (*s) put_ch(o, *s++);
}

static void put_int(out_t *o, int64_t v)
{
    char t[24];
    uint32_t k = 0;
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    if (v < 0) put_ch(o, '-');
    do {
        t[k++] = (char) ('0' + u % 10u);
        u /= 10u;
    } while (u && k < sizeof t);
    while (k) put_ch(o, t[--k]);
}

static char TEXT[GNOMON_FILE_MAX];

/* GM1: the figure as text; its length, or -1 when it does not fit. */
static int32_t compose(const gnomon_t *g)
{
    out_t o = {TEXT, 0, sizeof TEXT, false};
    put_str(&o, "gnomon 1 ");
    put_int(&o, g->kind);
    put_ch(&o, ' ');
    put_int(&o, g->n);
    put_ch(&o, ' ');
    put_int(&o, g->closed ? 1 : 0);
    put_ch(&o, '\n');
    for (uint32_t i = 0; i < g->n; i++) {
        put_str(&o, "p ");
        put_int(&o, g->x[i]);
        put_ch(&o, ' ');
        put_int(&o, g->y[i]);
        put_ch(&o, '\n');
    }
    return o.over ? -1 : (int32_t) o.n;
}

/* ---- zxvd ---- */

/* One request's text: whole lines of at most this many bytes; each byte
 * escapes to at most two ("\n"), well inside the request line. */
#define PIECE 6000u

static int write_text(zxm_link_t *l, const char *space, const char *path, const char *text,
                      uint32_t len)
{
    uint32_t at = 0;
    bool first = true;
    do {
        uint32_t n = len - at;
        if (n > PIECE) {
            n = PIECE;
            while (n && text[at + n - 1u] != '\n') n--;
            if (!n) return ZXM_E_TOOBIG;
        }
        bool last = at + n >= len;
        zxm_begin(l, "files.write");
        zxm_p_space(l, space);
        zxm_p_str(l, "path", path);
        zxm_p_strn(l, "text", text + at, n);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        int rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
    } while (at < len);
    return ZXM_OK;
}

static bool name_ok(const char *space, const char *path)
{
    return space && path && path[0] && strlen(space) <= ZXM_SPACE_MAX &&
           strlen(path) <= GNOMON_PATH_MAX;
}

static int save_to(gnomon_t *g, const char *space, const char *path)
{
    if (!g->n) return ZXM_E_STATE;
    int32_t len = compose(g);
    if (len < 0) return ZXM_E_TOOBIG;
    int rc = write_text(g->link, space, path, TEXT, (uint32_t) len);
    if (rc) return rc;
    if (g->space != space) memcpy(g->space, space, strlen(space) + 1u);
    if (g->path != path) memcpy(g->path, path, strlen(path) + 1u);
    g->dirty = false;
    return ZXM_OK;
}

int gnomon_save(gnomon_t *g)
{
    if (!g->path[0]) return ZXM_E_STATE;
    return save_to(g, g->space, g->path);
}

int gnomon_save_as(gnomon_t *g, const char *space, const char *path)
{
    if (!name_ok(space, path)) return ZXM_E_ARG;
    return save_to(g, space, path);
}

/* ---- GM1: reading a figure back ---- */

typedef struct {
    const char *s;
    uint32_t n, at;
} in_t;

/* The next integer on the line, after one space. */
static bool get_int(in_t *in, int64_t *v)
{
    if (in->at >= in->n || in->s[in->at] != ' ') return false;
    in->at++;
    bool neg = in->at < in->n && in->s[in->at] == '-';
    if (neg) in->at++;
    uint32_t st = in->at;
    int64_t u = 0;
    while (in->at < in->n && in->s[in->at] >= '0' && in->s[in->at] <= '9') {
        if (in->at - st >= 9u) return false; /* past every value a figure holds */
        u = u * 10 + (in->s[in->at] - '0');
        in->at++;
    }
    if (in->at == st) return false;
    *v = neg ? -u : u;
    return true;
}

static bool get_word(in_t *in, const char *w)
{
    uint32_t k = (uint32_t) strlen(w);
    if (in->n - in->at < k || memcmp(in->s + in->at, w, k)) return false;
    in->at += k;
    return true;
}

static bool line_end(in_t *in)
{
    if (in->at >= in->n || in->s[in->at] != '\n') return false;
    in->at++;
    return true;
}

static gnomon_t NEXT; /* a figure as it is read, before it is kept */

static bool parse(gnomon_t *d, const char *s, uint32_t n)
{
    in_t in = {s, n, 0};
    int64_t kind, count, closed, x, y;
    if (!get_word(&in, "gnomon 1") || !get_int(&in, &kind) || !get_int(&in, &count) ||
        !get_int(&in, &closed) || !line_end(&in) || kind < GNOMON_KIND_POLY ||
        kind > GNOMON_KIND_TURTLE || count < 1 || count > (int64_t) GNOMON_VERTS_MAX ||
        closed < 0 || closed > 1 || (closed && kind != GNOMON_KIND_TURTLE))
        return false;
    for (int64_t i = 0; i < count; i++)
        if (!get_word(&in, "p") || !get_int(&in, &x) || !get_int(&in, &y) || !line_end(&in) ||
            put_vertex(d, x, y))
            return false;
    d->kind = (int32_t) kind;
    d->closed = closed == 1;
    d->tx = d->closed ? d->x[0] : d->x[d->n - 1u];
    d->ty = d->closed ? d->y[0] : d->y[d->n - 1u];
    return in.at == in.n;
}

int gnomon_open(gnomon_t *g, const char *space, const char *path)
{
    static char chunk[GNOMON_FILE_MAX];
    if (!name_ok(space, path)) return ZXM_E_ARG;
    zxm_link_t *l = g->link;
    uint32_t len = 0;
    uint64_t off = 0;
    for (;;) {
        zxm_begin(l, "files.read");
        zxm_p_space(l, space);
        zxm_p_str(l, "path", path);
        zxm_p_u64(l, "offset", off);
        int rc = zxm_call(l);
        if (rc) return rc;
        uint64_t size, next_off;
        bool eof;
        int32_t t = zxm_get(l, l->result, "text");
        if (zxm_get_u64(l, l->result, "size", &size) ||
            zxm_get_u64(l, l->result, "next", &next_off) ||
            zxm_get_bool(l, l->result, "eof", &eof) || t < 0)
            return ZXM_E_PROTOCOL;
        if (size >= GNOMON_FILE_MAX) return ZXM_E_TOOBIG;
        int32_t n = w4_json_str(l->resp, &l->tok[t], chunk, sizeof chunk);
        if (n < 0 || len + (uint32_t) n >= GNOMON_FILE_MAX) return ZXM_E_TOOBIG;
        memcpy(TEXT + len, chunk, (size_t) n);
        len += (uint32_t) n;
        if (eof) break;
        if (next_off <= off) return ZXM_E_PROTOCOL;
        off = next_off;
    }
    gnomon_init(&NEXT, l);
    if (!parse(&NEXT, TEXT, len)) return ZXM_E_ARG; /* GM1: the open figure is kept */
    memcpy(g->x, NEXT.x, NEXT.n * sizeof NEXT.x[0]);
    memcpy(g->y, NEXT.y, NEXT.n * sizeof NEXT.y[0]);
    g->n = NEXT.n;
    g->kind = NEXT.kind;
    g->closed = NEXT.closed;
    g->heading = 0;
    g->tx = NEXT.tx;
    g->ty = NEXT.ty;
    if (g->space != space) memcpy(g->space, space, strlen(space) + 1u);
    if (g->path != path) memcpy(g->path, path, strlen(path) + 1u);
    g->dirty = false;
    return ZXM_OK;
}
