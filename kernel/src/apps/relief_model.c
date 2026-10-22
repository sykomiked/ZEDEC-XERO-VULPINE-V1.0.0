/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* relief_model.c — Relief's model (relief_model.h). */
#include <string.h>

#include "relief_model.h"

/* ---- the mesh ---- */

void relief_init(relief_t *r, zxm_link_t *link)
{
    memset(r, 0, sizeof *r);
    r->link = link;
    r->scale = 100;
    r->focal = 100000; /* a metre */
}

void relief_clear(relief_t *r)
{
    r->nv = 0;
    r->nf = 0;
    r->dirty = true;
}

static bool coord_ok(int64_t v)
{
    return relief_in(v, -RELIEF_COORD_MAX, RELIEF_COORD_MAX);
}

int relief_add_vertex(relief_t *r, int64_t x, int64_t y, int64_t z, uint32_t *index)
{
    if (!coord_ok(x) || !coord_ok(y) || !coord_ok(z)) return ZXM_E_ARG;
    if ((int64_t) r->nv >= RELIEF_VERTS_MAX) return ZXM_E_TOOBIG;
    r->v[r->nv] = (relief_vertex_t){(int32_t) x, (int32_t) y, (int32_t) z};
    if (index) *index = r->nv;
    r->nv++;
    r->dirty = true;
    return ZXM_OK;
}

int relief_add_face(relief_t *r, int64_t a, int64_t b, int64_t c)
{
    if (a < 0 || b < 0 || c < 0 || a >= r->nv || b >= r->nv || c >= r->nv || a == b || b == c ||
        a == c)
        return ZXM_E_ARG;
    if ((int64_t) r->nf >= RELIEF_FACES_MAX) return ZXM_E_TOOBIG;
    r->f[r->nf++] = (relief_face_t){(uint16_t) a, (uint16_t) b, (uint16_t) c};
    r->dirty = true;
    return ZXM_OK;
}

int relief_add_box(relief_t *r, int64_t x0, int64_t y0, int64_t z0, int64_t x1, int64_t y1,
                   int64_t z1)
{
    static const uint8_t faces[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7},
                                         {0, 1, 5}, {0, 5, 4}, {3, 7, 6}, {3, 6, 2},
                                         {0, 4, 7}, {0, 7, 3}, {1, 2, 6}, {1, 6, 5}};
    if (!coord_ok(x0) || !coord_ok(y0) || !coord_ok(z0) || !coord_ok(x1) || !coord_ok(y1) ||
        !coord_ok(z1) || x0 >= x1 || y0 >= y1 || z0 >= z1)
        return ZXM_E_ARG;
    if ((int64_t) r->nv + 8 > RELIEF_VERTS_MAX || (int64_t) r->nf + 12 > RELIEF_FACES_MAX)
        return ZXM_E_TOOBIG;
    uint32_t base = r->nv;
    for (uint32_t k = 0; k < 8; k++) {
        int64_t x = (k == 1 || k == 2 || k == 5 || k == 6) ? x1 : x0;
        int64_t y = (k == 2 || k == 3 || k == 6 || k == 7) ? y1 : y0;
        int64_t z = k >= 4 ? z1 : z0;
        (void) relief_add_vertex(r, x, y, z, NULL);
    }
    for (uint32_t k = 0; k < 12; k++)
        (void) relief_add_face(r, base + faces[k][0], base + faces[k][1], base + faces[k][2]);
    return ZXM_OK;
}

/* How many faces run the edge from u to v. */
static uint32_t edge_runs(const relief_t *r, uint32_t u, uint32_t v)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < r->nf; i++) {
        const relief_face_t *f = &r->f[i];
        n += (f->a == u && f->b == v) + (f->b == u && f->c == v) + (f->c == u && f->a == v);
    }
    return n;
}

bool relief_closed(const relief_t *r)
{
    if (!r->nf) return false;
    for (uint32_t i = 0; i < r->nf; i++) {
        const relief_face_t *f = &r->f[i];
        const uint32_t e[3][2] = {{f->a, f->b}, {f->b, f->c}, {f->c, f->a}};
        for (uint32_t k = 0; k < 3; k++)
            if (edge_runs(r, e[k][0], e[k][1]) != 1 || edge_runs(r, e[k][1], e[k][0]) != 1)
                return false;
    }
    return true;
}

/* ---- RM3: the view ---- */

/* sin of 0 .. 90 whole degrees in 4096ths, each rounded to the nearest */
static const int16_t SIN90[91] = {
    0,    71,   143,  214,  286,  357,  428,  499,  570,  641,  711,  782,  852,  921,  991,  1060,
    1129, 1198, 1266, 1334, 1401, 1468, 1534, 1600, 1666, 1731, 1796, 1860, 1923, 1986, 2048, 2110,
    2171, 2231, 2290, 2349, 2408, 2465, 2522, 2578, 2633, 2687, 2741, 2793, 2845, 2896, 2946, 2996,
    3044, 3091, 3138, 3183, 3228, 3271, 3314, 3355, 3396, 3435, 3474, 3511, 3547, 3582, 3617, 3650,
    3681, 3712, 3742, 3770, 3798, 3824, 3849, 3873, 3896, 3917, 3937, 3956, 3974, 3991, 4006, 4021,
    4034, 4046, 4056, 4065, 4074, 4080, 4086, 4090, 4094, 4095, 4096};

/* sin of 0 .. 359 degrees from the quarter table */
static int64_t sin_deg(int64_t d)
{
    if (d <= 90) return SIN90[d];
    if (d <= 180) return SIN90[180 - d];
    if (d <= 270) return -SIN90[d - 180];
    return -SIN90[360 - d];
}

void relief_turn(int64_t degrees, int64_t *cos, int64_t *sin)
{
    int64_t d = degrees % RELIEF_YAW_TURN;
    if (d < 0) d += RELIEF_YAW_TURN;
    *sin = sin_deg(d);
    *cos = sin_deg((d + 90) % RELIEF_YAW_TURN);
}

int relief_set_view(relief_t *r, int64_t yaw, int64_t scale, int64_t focal)
{
    if (!relief_in(scale, 1, RELIEF_SCALE_MAX) || !relief_in(focal, 1, RELIEF_FOCAL_MAX))
        return ZXM_E_ARG;
    int64_t d = yaw % RELIEF_YAW_TURN;
    r->yaw = (int32_t) (d < 0 ? d + RELIEF_YAW_TURN : d);
    r->scale = (int32_t) scale;
    r->focal = (int32_t) focal;
    return ZXM_OK;
}

int relief_project(const relief_t *r, int64_t *sx, int64_t *sy, uint32_t cap)
{
    if (!sx || !sy) return ZXM_E_ARG;
    if (cap < r->nv) return ZXM_E_TOOBIG;
    int64_t c, s, depth;
    relief_turn(r->yaw, &c, &s);
    for (uint32_t i = 0; i < r->nv; i++) /* every vertex in front first: nothing half drawn */
        if (!relief_view_ok(r->v[i].x, r->v[i].z, c, s, r->scale, r->focal, &depth))
            return ZXM_E_ARG;
    for (uint32_t i = 0; i < r->nv; i++) {
        const relief_vertex_t *v = &r->v[i];
        if (!relief_screen_x(v->x, v->z, c, s, r->scale, r->focal, &sx[i]) ||
            !relief_screen_y(v->x, v->y, v->z, c, s, r->scale, r->focal, &sy[i]))
            return ZXM_E_ARG;
    }
    return ZXM_OK;
}

/* ---- RM4: the depth effect ---- */

int relief_depth_layers(const relief_t *r, uint32_t layers, int64_t shift, uint8_t *layer,
                        int64_t *offset, uint32_t cap)
{
    if (!layer || !offset || layers < 2 || layers > RELIEF_LAYERS_MAX ||
        !relief_in(shift, -RELIEF_SHIFT_MAX, RELIEF_SHIFT_MAX) || !r->nv)
        return ZXM_E_ARG;
    if (cap < r->nv) return ZXM_E_TOOBIG;
    int64_t c, s, near = 0, far = 0;
    relief_turn(r->yaw, &c, &s);
    for (uint32_t i = 0; i < r->nv; i++) {
        int64_t d = r->v[i].z * c - r->v[i].x * s; /* the turned depth, in 4096ths */
        if (!i || d < near) near = d;
        if (!i || d > far) far = d;
    }
    for (uint32_t i = 0; i < r->nv; i++) {
        int64_t d = r->v[i].z * c - r->v[i].x * s, k = 0, off = 0;
        if (far > near) k = (d - near) * (int64_t) (layers - 1u) / (far - near);
        if (!relief_depth_offset(shift, k, (int64_t) layers - 1, &off)) return ZXM_E_ARG;
        layer[i] = (uint8_t) k;
        offset[i] = off;
    }
    return ZXM_OK;
}

/* ---- RM5, RM6 ---- */

/* the corners three to a face, and every face as wound */
static int64_t CX[3 * RELIEF_FACES_MAX], CY[3 * RELIEF_FACES_MAX], CZ[3 * RELIEF_FACES_MAX],
    ORIENT[RELIEF_FACES_MAX];

int relief_volume(const relief_t *r, int64_t *num, int64_t *den)
{
    if (!num || !den) return ZXM_E_ARG;
    if (!relief_mesh_limits_ok(r->nv, r->nf) || !relief_closed(r)) return ZXM_E_STATE;
    for (uint32_t i = 0; i < r->nf; i++) {
        const uint16_t at[3] = {r->f[i].a, r->f[i].b, r->f[i].c};
        for (uint32_t k = 0; k < 3; k++) {
            CX[3u * i + k] = r->v[at[k]].x;
            CY[3u * i + k] = r->v[at[k]].y;
            CZ[3u * i + k] = r->v[at[k]].z;
        }
        ORIENT[i] = 1;
    }
    return relief_mesh_volume(CX, CY, CZ, 3u * r->nf, ORIENT, r->nf, num, den) ? ZXM_OK : ZXM_E_ARG;
}

bool relief_record(const relief_t *r, int64_t mesh, int64_t mtime)
{
    return relief_mesh_record_ok(mesh, r->nv, r->nf, mtime);
}

/* ---- RM1: text out ---- */

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

static char TEXT[RELIEF_FILE_MAX];

/* RM1: the mesh as text; its length, or -1 when it does not fit. */
static int32_t compose(const relief_t *r)
{
    out_t o = {TEXT, 0, sizeof TEXT, false};
    put_str(&o, "relief 1 ");
    put_int(&o, r->nv);
    put_ch(&o, ' ');
    put_int(&o, r->nf);
    put_ch(&o, '\n');
    for (uint32_t i = 0; i < r->nv; i++) {
        put_str(&o, "v ");
        put_int(&o, r->v[i].x);
        put_ch(&o, ' ');
        put_int(&o, r->v[i].y);
        put_ch(&o, ' ');
        put_int(&o, r->v[i].z);
        put_ch(&o, '\n');
    }
    for (uint32_t i = 0; i < r->nf; i++) {
        put_str(&o, "f ");
        put_int(&o, r->f[i].a);
        put_ch(&o, ' ');
        put_int(&o, r->f[i].b);
        put_ch(&o, ' ');
        put_int(&o, r->f[i].c);
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
           strlen(path) <= RELIEF_PATH_MAX;
}

static int save_to(relief_t *r, const char *space, const char *path)
{
    if (!relief_mesh_limits_ok(r->nv, r->nf)) return ZXM_E_STATE;
    int32_t len = compose(r);
    if (len < 0) return ZXM_E_TOOBIG;
    int rc = write_text(r->link, space, path, TEXT, (uint32_t) len);
    if (rc) return rc;
    if (r->space != space) memcpy(r->space, space, strlen(space) + 1u);
    if (r->path != path) memcpy(r->path, path, strlen(path) + 1u);
    r->dirty = false;
    return ZXM_OK;
}

int relief_save(relief_t *r)
{
    if (!r->path[0]) return ZXM_E_STATE;
    return save_to(r, r->space, r->path);
}

int relief_save_as(relief_t *r, const char *space, const char *path)
{
    if (!name_ok(space, path)) return ZXM_E_ARG;
    return save_to(r, space, path);
}

/* ---- RM1: reading a mesh back ---- */

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
        if (in->at - st >= 9u) return false; /* past every value a mesh holds */
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

static relief_t NEXT; /* a mesh as it is read, before it is kept */

static bool parse(relief_t *d, const char *s, uint32_t n)
{
    in_t in = {s, n, 0};
    int64_t nv, nf, v[3];
    if (!get_word(&in, "relief 1") || !get_int(&in, &nv) || !get_int(&in, &nf) || !line_end(&in) ||
        !relief_mesh_limits_ok(nv, nf))
        return false;
    for (int64_t i = 0; i < nv; i++)
        if (!get_word(&in, "v") || !get_int(&in, &v[0]) || !get_int(&in, &v[1]) ||
            !get_int(&in, &v[2]) || !line_end(&in) || relief_add_vertex(d, v[0], v[1], v[2], NULL))
            return false;
    for (int64_t i = 0; i < nf; i++)
        if (!get_word(&in, "f") || !get_int(&in, &v[0]) || !get_int(&in, &v[1]) ||
            !get_int(&in, &v[2]) || !line_end(&in) || relief_add_face(d, v[0], v[1], v[2]))
            return false;
    return in.at == in.n;
}

int relief_open(relief_t *r, const char *space, const char *path)
{
    static char chunk[RELIEF_FILE_MAX];
    if (!name_ok(space, path)) return ZXM_E_ARG;
    zxm_link_t *l = r->link;
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
        if (size >= RELIEF_FILE_MAX) return ZXM_E_TOOBIG;
        int32_t n = w4_json_str(l->resp, &l->tok[t], chunk, sizeof chunk);
        if (n < 0 || len + (uint32_t) n >= RELIEF_FILE_MAX) return ZXM_E_TOOBIG;
        memcpy(TEXT + len, chunk, (size_t) n);
        len += (uint32_t) n;
        if (eof) break;
        if (next_off <= off) return ZXM_E_PROTOCOL;
        off = next_off;
    }
    relief_init(&NEXT, l);
    if (!parse(&NEXT, TEXT, len)) return ZXM_E_ARG; /* RM1: the open mesh is kept */
    memcpy(r->v, NEXT.v, NEXT.nv * sizeof NEXT.v[0]);
    memcpy(r->f, NEXT.f, NEXT.nf * sizeof NEXT.f[0]);
    r->nv = NEXT.nv;
    r->nf = NEXT.nf;
    if (r->space != space) memcpy(r->space, space, strlen(space) + 1u);
    if (r->path != path) memcpy(r->path, path, strlen(path) + 1u);
    r->dirty = false;
    return ZXM_OK;
}
