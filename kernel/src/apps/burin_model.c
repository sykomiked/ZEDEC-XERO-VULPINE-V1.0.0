/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* burin_model.c — Burin's model (burin_model.h). */
#include <string.h>

#include "burin_model.h"

/* ---- the drawing ---- */

void burin_init(burin_t *b, zxm_link_t *link)
{
    memset(b, 0, sizeof *b);
    b->link = link;
}

int burin_new(burin_t *b, int64_t width, int64_t height)
{
    if (!burin_path_limits_ok(2, width, height, 1)) return ZXM_E_ARG;
    zxm_link_t *l = b->link;
    burin_init(b, l);
    b->width = (int32_t) width;
    b->height = (int32_t) height;
    b->dirty = true;
    return ZXM_OK;
}

int64_t burin_points(const burin_path_t *p)
{
    int64_t n = 1;
    for (uint32_t i = 0; i < p->nseg; i++) n += p->seg[i].kind == BURIN_CUBIC ? 3 : 1;
    return n;
}

static bool coord_ok(int64_t v)
{
    return v >= -BURIN_COORD_MAX && v <= BURIN_COORD_MAX;
}

int burin_move_to(burin_t *b, int64_t x, int64_t y, int64_t stroke)
{
    if (!b->width) return ZXM_E_STATE;
    if (!coord_ok(x) || !coord_ok(y) || !burin_path_limits_ok(2, b->width, b->height, stroke))
        return ZXM_E_ARG;
    if (b->npaths >= BURIN_PATHS) return ZXM_E_TOOBIG;
    burin_path_t *p = &b->p[b->npaths++];
    p->x0 = (int32_t) x;
    p->y0 = (int32_t) y;
    p->stroke = (int32_t) stroke;
    p->nseg = 0;
    b->dirty = true;
    return ZXM_OK;
}

/* Append s to the last path when the path stays inside BM2. */
static int append(burin_t *b, const burin_seg_t *s)
{
    if (!b->width || !b->npaths) return ZXM_E_STATE;
    burin_path_t *p = &b->p[b->npaths - 1u];
    int64_t pts = burin_points(p) + (s->kind == BURIN_CUBIC ? 3 : 1);
    if (p->nseg >= BURIN_SEGS || !burin_path_limits_ok(pts, b->width, b->height, p->stroke))
        return ZXM_E_ARG;
    p->seg[p->nseg++] = *s;
    b->dirty = true;
    return ZXM_OK;
}

int burin_line_to(burin_t *b, int64_t x, int64_t y)
{
    if (!coord_ok(x) || !coord_ok(y)) return ZXM_E_ARG;
    burin_seg_t s = {BURIN_LINE, 0, 0, 0, 0, (int32_t) x, (int32_t) y};
    return append(b, &s);
}

int burin_curve_to(burin_t *b, int64_t x1, int64_t y1, int64_t x2, int64_t y2, int64_t x, int64_t y)
{
    if (!coord_ok(x1) || !coord_ok(y1) || !coord_ok(x2) || !coord_ok(y2) || !coord_ok(x) ||
        !coord_ok(y))
        return ZXM_E_ARG;
    burin_seg_t s = {BURIN_CUBIC,  (int32_t) x1, (int32_t) y1, (int32_t) x2,
                     (int32_t) y2, (int32_t) x,  (int32_t) y};
    return append(b, &s);
}

/* ---- BM3: flattening ---- */

static int64_t abs64(int64_t v)
{
    return v < 0 ? -v : v;
}

static int64_t second_diff(int64_t a, int64_t b, int64_t c, int64_t d)
{
    int64_t u = abs64(a - 2 * b + c), v = abs64(b - 2 * c + d);
    return u > v ? u : v;
}

int64_t burin_flat_steps(const burin_seg_t *s, int64_t x0, int64_t y0, int64_t tol)
{
    if (s->kind != BURIN_CUBIC) return 1;
    if (tol < 1) tol = 1;
    if (tol > BURIN_COORD_MAX) tol = BURIN_COORD_MAX;
    int64_t dx = second_diff(x0, s->x1, s->x2, s->x), dy = second_diff(y0, s->y1, s->y2, s->y);
    int64_t d = dx > dy ? dx : dy;
    for (int64_t n = 1; n < BURIN_STEPS_MAX; n++)
        if (3 * d <= 4 * tol * n * n) return n;
    return BURIN_STEPS_MAX;
}

int burin_flatten(const burin_t *b, uint32_t i, int64_t tol, int32_t *x, int32_t *y, uint32_t cap,
                  uint32_t *n)
{
    if (!x || !y || !n) return ZXM_E_ARG;
    if (i >= b->npaths) return ZXM_E_ARG;
    const burin_path_t *p = &b->p[i];
    uint32_t k = 0;
    if (k >= cap) return ZXM_E_TOOBIG;
    x[k] = p->x0;
    y[k++] = p->y0;
    int64_t cx = p->x0, cy = p->y0;
    for (uint32_t j = 0; j < p->nseg; j++) {
        const burin_seg_t *s = &p->seg[j];
        int64_t steps = burin_flat_steps(s, cx, cy, tol);
        for (int64_t t = 1; t <= steps; t++) {
            if (k >= cap) return ZXM_E_TOOBIG;
            if (s->kind == BURIN_CUBIC) {
                x[k] = (int32_t) burin_bezier_at(cx, s->x1, s->x2, s->x, t, steps);
                y[k] = (int32_t) burin_bezier_at(cy, s->y1, s->y2, s->y, t, steps);
            } else {
                x[k] = s->x;
                y[k] = s->y;
            }
            k++;
        }
        cx = s->x;
        cy = s->y;
    }
    *n = k;
    return ZXM_OK;
}

/* ---- BM4: hatching ---- */

/* A drawing is open, and the rectangle and stroke are inside BM2. */
static int hatch_check(const burin_t *b, int64_t x0, int64_t y0, int64_t x1, int64_t y1,
                       int64_t stroke)
{
    if (!b->width) return ZXM_E_STATE;
    if (!coord_ok(x0) || !coord_ok(y0) || !coord_ok(x1) || !coord_ok(y1) || x0 >= x1 || y0 > y1 ||
        !burin_path_limits_ok(2, b->width, b->height, stroke))
        return ZXM_E_ARG;
    return ZXM_OK;
}

static void hatch_line(burin_t *b, int64_t x0, int64_t x1, int64_t y, int64_t stroke)
{
    burin_path_t *p = &b->p[b->npaths++];
    p->x0 = (int32_t) x0;
    p->y0 = (int32_t) y;
    p->stroke = (int32_t) stroke;
    p->nseg = 1;
    p->seg[0] = (burin_seg_t){BURIN_LINE, 0, 0, 0, 0, (int32_t) x1, (int32_t) y};
}

int burin_hatch_spaced(burin_t *b, int64_t x0, int64_t y0, int64_t x1, int64_t y1, int64_t gap,
                       int64_t stroke, uint32_t *added)
{
    int rc = hatch_check(b, x0, y0, x1, y1, stroke);
    if (rc) return rc;
    int64_t lines = burin_hatch_lines(y1 - y0, gap);
    if (lines < 0) return ZXM_E_ARG;
    if ((uint64_t) lines > BURIN_PATHS - b->npaths) return ZXM_E_TOOBIG;
    for (int64_t k = 1; k <= lines; k++) hatch_line(b, x0, x1, y0 + k * gap, stroke);
    if (lines) b->dirty = true;
    if (added) *added = (uint32_t) lines;
    return ZXM_OK;
}

int burin_hatch_even(burin_t *b, int64_t x0, int64_t y0, int64_t x1, int64_t y1, int64_t lines,
                     int64_t stroke, uint32_t *added)
{
    int rc = hatch_check(b, x0, y0, x1, y1, stroke);
    if (rc) return rc;
    int64_t num, den;
    if (!burin_hatch_gap(y1 - y0, lines, &num, &den)) return ZXM_E_ARG;
    if ((uint64_t) lines > BURIN_PATHS - b->npaths) return ZXM_E_TOOBIG;
    for (int64_t k = 1; k <= lines; k++) hatch_line(b, x0, x1, y0 + k * num / den, stroke);
    b->dirty = true;
    if (added) *added = (uint32_t) lines;
    return ZXM_OK;
}

bool burin_path_record(const burin_t *b, uint32_t i, int64_t mtime)
{
    if (i >= b->npaths) return false;
    return burin_record_ok(i, burin_points(&b->p[i]), b->p[i].stroke, mtime);
}

/* ---- text out ---- */

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

/* v in decimal, at least `width` digits (zeros in front) */
static void put_int_w(out_t *o, int64_t v, uint32_t width)
{
    char t[24];
    uint32_t k = 0;
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    if (v < 0) put_ch(o, '-');
    do {
        t[k++] = (char) ('0' + u % 10u);
        u /= 10u;
    } while (u && k < sizeof t);
    while (k < width && k < sizeof t) t[k++] = '0';
    while (k) put_ch(o, t[--k]);
}

static void put_int(out_t *o, int64_t v)
{
    put_int_w(o, v, 1);
}

/* hundredths as a decimal with two places: -125 is -1.25 */
static void put_fixed(out_t *o, int64_t v)
{
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    if (v < 0) put_ch(o, '-');
    put_int(o, (int64_t) (u / 100u));
    put_ch(o, '.');
    put_int_w(o, (int64_t) (u % 100u), 2);
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
           strlen(path) <= BURIN_PATH_MAX;
}

static char TEXT[BURIN_FILE_MAX];

/* BM1: the drawing as text; its length, or -1 when it does not fit. */
static int32_t compose(const burin_t *b)
{
    out_t o = {TEXT, 0, sizeof TEXT, false};
    put_str(&o, "burin 1 ");
    put_int(&o, b->width);
    put_ch(&o, ' ');
    put_int(&o, b->height);
    put_ch(&o, '\n');
    for (uint32_t i = 0; i < b->npaths; i++) {
        const burin_path_t *p = &b->p[i];
        put_str(&o, "p ");
        put_int(&o, p->stroke);
        put_ch(&o, ' ');
        put_int(&o, p->x0);
        put_ch(&o, ' ');
        put_int(&o, p->y0);
        put_ch(&o, '\n');
        for (uint32_t j = 0; j < p->nseg; j++) {
            const burin_seg_t *s = &p->seg[j];
            if (s->kind == BURIN_CUBIC) {
                const int32_t v[4] = {s->x1, s->y1, s->x2, s->y2};
                put_ch(&o, 'c');
                for (uint32_t k = 0; k < 4; k++) {
                    put_ch(&o, ' ');
                    put_int(&o, v[k]);
                }
            } else {
                put_ch(&o, 'l');
            }
            put_ch(&o, ' ');
            put_int(&o, s->x);
            put_ch(&o, ' ');
            put_int(&o, s->y);
            put_ch(&o, '\n');
        }
    }
    return o.over ? -1 : (int32_t) o.n;
}

static int save_to(burin_t *b, const char *space, const char *path)
{
    if (!b->width) return ZXM_E_STATE;
    int32_t len = compose(b);
    if (len < 0) return ZXM_E_TOOBIG;
    int rc = write_text(b->link, space, path, TEXT, (uint32_t) len);
    if (rc) return rc;
    if (b->space != space) memcpy(b->space, space, strlen(space) + 1u);
    if (b->path != path) memcpy(b->path, path, strlen(path) + 1u);
    b->dirty = false;
    return ZXM_OK;
}

int burin_save(burin_t *b)
{
    if (!b->path[0]) return ZXM_E_STATE;
    return save_to(b, b->space, b->path);
}

int burin_save_as(burin_t *b, const char *space, const char *path)
{
    if (!name_ok(space, path)) return ZXM_E_ARG;
    return save_to(b, space, path);
}

/* ---- BM1: reading a drawing back ---- */

typedef struct {
    const char *s;
    uint32_t n, at;
} in_t;

/* The next integer on the line, after one space (first: none). */
static bool get_int(in_t *in, bool first, int64_t *v)
{
    if (!first) {
        if (in->at >= in->n || in->s[in->at] != ' ') return false;
        in->at++;
    }
    bool neg = in->at < in->n && in->s[in->at] == '-';
    if (neg) in->at++;
    uint32_t st = in->at;
    int64_t u = 0;
    while (in->at < in->n && in->s[in->at] >= '0' && in->s[in->at] <= '9') {
        if (u > (BURIN_COORD_MAX * 10)) return false; /* past every value a drawing holds */
        u = u * 10 + (in->s[in->at] - '0');
        in->at++;
    }
    if (in->at == st || in->at - st > 9u) return false;
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

static burin_t NEXT; /* a drawing as it is read, before it is kept */

static bool parse(burin_t *d, const char *s, uint32_t n)
{
    in_t in = {s, n, 0};
    int64_t w, h;
    if (!get_word(&in, "burin 1") || !get_int(&in, false, &w) || !get_int(&in, false, &h) ||
        !line_end(&in) || burin_new(d, w, h))
        return false;
    while (in.at < in.n) {
        int64_t v[6];
        if (get_word(&in, "p")) {
            if (!get_int(&in, false, &v[0]) || !get_int(&in, false, &v[1]) ||
                !get_int(&in, false, &v[2]) || !line_end(&in) || burin_move_to(d, v[1], v[2], v[0]))
                return false;
        } else if (get_word(&in, "l")) {
            if (!get_int(&in, false, &v[0]) || !get_int(&in, false, &v[1]) || !line_end(&in) ||
                burin_line_to(d, v[0], v[1]))
                return false;
        } else if (get_word(&in, "c")) {
            for (uint32_t k = 0; k < 6; k++)
                if (!get_int(&in, false, &v[k])) return false;
            if (!line_end(&in) || burin_curve_to(d, v[0], v[1], v[2], v[3], v[4], v[5]))
                return false;
        } else {
            return false;
        }
    }
    return true;
}

int burin_open(burin_t *b, const char *space, const char *path)
{
    static char chunk[BURIN_FILE_MAX];
    if (!name_ok(space, path)) return ZXM_E_ARG;
    zxm_link_t *l = b->link;
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
        if (size >= BURIN_FILE_MAX) return ZXM_E_TOOBIG;
        int32_t n = w4_json_str(l->resp, &l->tok[t], chunk, sizeof chunk);
        if (n < 0 || len + (uint32_t) n >= BURIN_FILE_MAX) return ZXM_E_TOOBIG;
        memcpy(TEXT + len, chunk, (size_t) n);
        len += (uint32_t) n;
        if (eof) break;
        if (next_off <= off) return ZXM_E_PROTOCOL;
        off = next_off;
    }
    burin_init(&NEXT, l);
    if (!parse(&NEXT, TEXT, len)) return ZXM_E_ARG; /* BM1: the open drawing is kept */
    int64_t job = b->job;
    char state[BURIN_STATE_MAX];
    memcpy(state, b->job_state, sizeof state);
    memcpy(b, &NEXT, sizeof *b);
    memcpy(b->space, space, strlen(space) + 1u);
    memcpy(b->path, path, strlen(path) + 1u);
    b->dirty = false;
    b->job = job;
    memcpy(b->job_state, state, sizeof state);
    return ZXM_OK;
}

/* ---- BM5: the PDF, and printing it ---- */

#define FLAT_MAX (1u + BURIN_SEGS * (uint32_t) BURIN_STEPS_MAX)

static char BODY[BURIN_FILE_MAX];
static int32_t FX[FLAT_MAX], FY[FLAT_MAX];

static bool ends_pdf(const char *s)
{
    size_t n = strlen(s);
    if (n < 5) return false;
    const char *e = s + n - 4;
    return e[0] == '.' && (e[1] | 32) == 'p' && (e[2] | 32) == 'd' && (e[3] | 32) == 'f';
}

/* The page's drawing operators; their length, or -1 when they do not fit. */
static int32_t pdf_body(const burin_t *b, int64_t tol)
{
    out_t o = {BODY, 0, sizeof BODY, false};
    put_str(&o, "1 J 1 j\n");
    for (uint32_t i = 0; i < b->npaths && !o.over; i++) {
        uint32_t n = 0;
        if (burin_flatten(b, i, tol, FX, FY, FLAT_MAX, &n)) return -1;
        put_fixed(&o, b->p[i].stroke);
        put_str(&o, " w\n");
        for (uint32_t k = 0; k < n; k++) {
            put_fixed(&o, FX[k]);
            put_ch(&o, ' ');
            put_fixed(&o, FY[k]);
            put_str(&o, k ? " l\n" : " m\n");
        }
        put_str(&o, "S\n");
    }
    return o.over ? -1 : (int32_t) o.n;
}

static int32_t pdf_compose(const burin_t *b, int64_t tol)
{
    int32_t blen = pdf_body(b, tol);
    if (blen < 0) return -1;
    out_t o = {TEXT, 0, sizeof TEXT, false};
    uint32_t at[5] = {0};
    put_str(&o, "%PDF-1.4\n");
    at[1] = o.n;
    put_str(&o, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    at[2] = o.n;
    put_str(&o, "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");
    at[3] = o.n;
    put_str(&o, "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ");
    put_fixed(&o, b->width);
    put_ch(&o, ' ');
    put_fixed(&o, b->height);
    put_str(&o, "] /Contents 4 0 R >>\nendobj\n");
    at[4] = o.n;
    put_str(&o, "4 0 obj\n<< /Length ");
    put_int(&o, blen);
    put_str(&o, " >>\nstream\n");
    for (int32_t k = 0; k < blen; k++) put_ch(&o, BODY[k]);
    put_str(&o, "endstream\nendobj\n");
    uint32_t xref = o.n;
    put_str(&o, "xref\n0 5\n0000000000 65535 f \n");
    for (uint32_t k = 1; k < 5; k++) {
        put_int_w(&o, at[k], 10);
        put_str(&o, " 00000 n \n");
    }
    put_str(&o, "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n");
    put_int(&o, xref);
    put_str(&o, "\n%%EOF\n");
    return o.over ? -1 : (int32_t) o.n;
}

int burin_export_pdf(burin_t *b, const char *pdf, int64_t tol)
{
    if (!b->width) return ZXM_E_STATE;
    if (!name_ok(b->space, pdf) || !ends_pdf(pdf) || tol < 1 || tol > BURIN_COORD_MAX)
        return ZXM_E_ARG;
    int32_t len = pdf_compose(b, tol);
    if (len < 0) return ZXM_E_TOOBIG;
    return write_text(b->link, b->space, pdf, TEXT, (uint32_t) len);
}

int burin_print(burin_t *b, const char *uri, const char *pdf, uint32_t copies)
{
    if (!uri || !uri[0] || strlen(uri) > BURIN_URI_MAX || !name_ok(b->space, pdf) || copies > 999)
        return ZXM_E_ARG;
    zxm_link_t *l = b->link;
    zxm_begin(l, "print.submit");
    zxm_p_space(l, b->space);
    zxm_p_str(l, "uri", uri);
    zxm_p_str(l, "path", pdf);
    if (copies) zxm_p_u64(l, "copies", copies);
    int rc = zxm_call(l);
    if (rc) return rc;
    uint64_t id;
    char state[BURIN_STATE_MAX];
    if (zxm_get_u64(l, l->result, "job_id", &id) || id == 0 || id > INT32_MAX ||
        zxm_get_str(l, l->result, "state", state, sizeof state))
        return ZXM_E_PROTOCOL;
    b->job = (int64_t) id;
    memcpy(b->job_state, state, sizeof state);
    return ZXM_OK;
}
