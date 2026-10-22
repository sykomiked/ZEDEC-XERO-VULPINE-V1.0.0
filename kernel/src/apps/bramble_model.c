/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bramble_model.c — Bramble, whiteboard and mind maps (see bramble_model.h). */
#include <string.h>

#include "bramble_model.h"
#include "gesso_rules.h"

/* ----- BR2: the ring, as ring_x.cpp and ring_y.cpp run it ----- */

static bool ring_domain(int64_t depth, int64_t order, int64_t count, int64_t step)
{
    return depth >= 0 && depth <= BRAMBLE_RING_DEPTH_MAX && step >= 1 &&
           step <= BRAMBLE_RING_STEP_MAX && count >= 1 && count <= BRAMBLE_RING_COUNT_MAX &&
           order >= 0 && order < count;
}

int64_t bramble_ring_x(int64_t depth, int64_t order, int64_t count, int64_t step)
{
    if (!ring_domain(depth, order, count, step)) return BRAMBLE_OFF;
    int64_t r = depth * step, p = order * 8 * r / count;
    if (p < 2 * r) return p - r;
    if (p < 4 * r) return r;
    if (p < 6 * r) return 5 * r - p;
    return -r;
}

int64_t bramble_ring_y(int64_t depth, int64_t order, int64_t count, int64_t step)
{
    if (!ring_domain(depth, order, count, step)) return BRAMBLE_OFF;
    int64_t r = depth * step, p = order * 8 * r / count;
    if (p < 2 * r) return -r;
    if (p < 4 * r) return p - 3 * r;
    if (p < 6 * r) return r;
    return 7 * r - p;
}

/* ----- BR3, BR4 ----- */

bool bramble_limits_ok(int64_t nodes, int64_t depth, int64_t step)
{
    return nodes >= 1 && nodes <= BRAMBLE_NODES_MAX && depth >= 0 && depth <= BRAMBLE_DEPTH_MAX &&
           step >= BRAMBLE_STEP_MIN && step <= BRAMBLE_STEP_MAX;
}

bool bramble_fits_canvas(int64_t width, int64_t height, int64_t depth, int64_t step)
{
    if (!gesso_canvas_ok(width, height) || depth < 0 || depth > BRAMBLE_DEPTH_MAX ||
        step < BRAMBLE_STEP_MIN || step > BRAMBLE_STEP_MAX)
        return false;
    int64_t side = width < height ? width : height;
    return 2 * depth * step + 1 <= side;
}

static bool fits_digits(int64_t v, int64_t top)
{
    return v > -top && v < top;
}

bool bramble_node_ok(int64_t node, int64_t parent, int64_t depth, int64_t order, int64_t x,
                     int64_t y)
{
    return fits_digits(node, 1000) && fits_digits(parent, 1000) && fits_digits(depth, 100) &&
           fits_digits(order, 1000) && fits_digits(x, 10000) && fits_digits(y, 10000);
}

/* ----- labels: 1..BRAMBLE_LABEL_MAX bytes of strict UTF-8, no controls ----- */

static bool label_ok(const char *p, uint32_t n)
{
    const uint8_t *s = (const uint8_t *) p;
    if (n == 0 || n > BRAMBLE_LABEL_MAX) return false;
    for (uint32_t i = 0; i < n;) {
        uint8_t c = s[i];
        if (c < 0x20 || c == 0x7f) return false;
        if (c < 0x80) {
            i++;
            continue;
        }
        uint32_t k = 0;
        if (c >= 0xc2 && c <= 0xdf) k = 1;
        if (c >= 0xe0 && c <= 0xef) k = 2;
        if (c >= 0xf0 && c <= 0xf4) k = 3;
        if (!k) return false;
        uint32_t cp = c & (k == 1 ? 0x1fu : k == 2 ? 0x0fu : 0x07u);
        for (uint32_t j = 1; j <= k; j++) {
            if (i + j >= n || (s[i + j] & 0xc0) != 0x80) return false;
            cp = cp << 6 | (s[i + j] & 0x3fu);
        }
        if ((k == 2 && cp < 0x800) || (k == 3 && (cp < 0x10000 || cp > 0x10ffff)) ||
            (cp >= 0xd800 && cp <= 0xdfff))
            return false;
        i += k + 1;
    }
    return true;
}

static bool label_cstr_ok(const char *label)
{
    if (!label) return false;
    uint32_t n = 0;
    while (n <= BRAMBLE_LABEL_MAX && label[n]) n++;
    return label_ok(label, n);
}

static void set_label(bramble_node_t *nd, const char *s, uint32_t n)
{
    memcpy(nd->label, s, n);
    nd->label[n] = 0;
}

/* ----- BR2: laying the whole tree out ----- */

/* Orders and places every node of nodes[0..n), parents before children.
 * ZXM_E_ARG when the map is past BR3. */
static int lay_out(bramble_node_t *nd, uint32_t n, int64_t width, int64_t height, int64_t step,
                   int32_t *deepest)
{
    static uint32_t count[BRAMBLE_DEPTH_MAX + 1];
    int32_t deep = 0;
    for (uint32_t i = 0; i < n; i++)
        if (nd[i].depth > deep) deep = nd[i].depth;
    if (!bramble_limits_ok(n, deep, step) || !bramble_fits_canvas(width, height, deep, step))
        return ZXM_E_ARG;
    memset(count, 0, sizeof count);
    for (uint32_t i = 0; i < n; i++) count[nd[i].depth]++;
    nd[0].order = 0;
    for (int32_t d = 1; d <= deep; d++) {
        int32_t k = 0;
        for (uint32_t po = 0; po < count[d - 1]; po++)
            for (uint32_t i = 1; i < n; i++)
                if (nd[i].depth == d && nd[nd[i].parent].order == (int32_t) po) nd[i].order = k++;
    }
    for (uint32_t i = 0; i < n; i++) {
        nd[i].x = bramble_ring_x(nd[i].depth, nd[i].order, count[nd[i].depth], step);
        nd[i].y = bramble_ring_y(nd[i].depth, nd[i].order, count[nd[i].depth], step);
        if (nd[i].x == BRAMBLE_OFF || nd[i].y == BRAMBLE_OFF) return ZXM_E_ARG;
    }
    *deepest = deep;
    return ZXM_OK;
}

/* ----- editing ----- */

int bramble_new(bramble_t *b, zxm_link_t *link, int64_t width, int64_t height, int64_t step,
                const char *root_label)
{
    if (!bramble_fits_canvas(width, height, 0, step) || !label_cstr_ok(root_label))
        return ZXM_E_ARG;
    b->link = link;
    b->space[0] = 0;
    b->path[0] = 0;
    b->width = width;
    b->height = height;
    b->step = step;
    b->n = 1;
    b->depth = 0;
    memset(&b->node[0], 0, sizeof b->node[0]);
    b->node[0].parent = -1;
    set_label(&b->node[0], root_label, (uint32_t) strlen(root_label));
    b->dirty = true;
    return ZXM_OK;
}

int bramble_add(bramble_t *b, uint32_t parent, const char *label, uint32_t *id)
{
    if (b->n == 0) return ZXM_E_STATE;
    if (parent >= b->n || !label_cstr_ok(label) || !id) return ZXM_E_ARG;
    if (b->n >= BRAMBLE_NODES_MAX) return ZXM_E_ARG;
    int32_t depth = b->node[parent].depth + 1;
    if (!bramble_limits_ok(b->n + 1, depth, b->step) ||
        !bramble_fits_canvas(b->width, b->height, depth, b->step))
        return ZXM_E_ARG;
    bramble_node_t *nd = &b->node[b->n];
    memset(nd, 0, sizeof *nd);
    nd->parent = (int32_t) parent;
    nd->depth = depth;
    set_label(nd, label, (uint32_t) strlen(label));
    int32_t deep = b->depth;
    int rc = lay_out(b->node, b->n + 1, b->width, b->height, b->step, &deep);
    if (rc) {
        lay_out(b->node, b->n, b->width, b->height, b->step, &deep);
        return rc;
    }
    *id = b->n;
    b->n++;
    b->depth = deep;
    b->dirty = true;
    return ZXM_OK;
}

int bramble_rename(bramble_t *b, uint32_t node, const char *label)
{
    if (b->n == 0) return ZXM_E_STATE;
    if (node >= b->n || !label_cstr_ok(label)) return ZXM_E_ARG;
    set_label(&b->node[node], label, (uint32_t) strlen(label));
    b->dirty = true;
    return ZXM_OK;
}

int bramble_remove(bramble_t *b, uint32_t node)
{
    static bool gone[BRAMBLE_NODES_MAX];
    static int32_t to[BRAMBLE_NODES_MAX];
    if (b->n == 0) return ZXM_E_STATE;
    if (node == 0 || node >= b->n) return ZXM_E_ARG;
    memset(gone, 0, sizeof gone);
    gone[node] = true;
    for (uint32_t i = node + 1; i < b->n; i++) gone[i] = gone[b->node[i].parent];
    uint32_t k = 0;
    for (uint32_t i = 0; i < b->n; i++) {
        to[i] = gone[i] ? -1 : (int32_t) k;
        if (gone[i]) continue;
        bramble_node_t nd = b->node[i];
        if (nd.parent >= 0) nd.parent = to[nd.parent];
        b->node[k++] = nd;
    }
    int32_t deep = 0;
    int rc = lay_out(b->node, k, b->width, b->height, b->step, &deep);
    if (rc) return rc; /* fewer nodes, no deeper: cannot happen */
    b->n = k;
    b->depth = deep;
    b->dirty = true;
    return ZXM_OK;
}

/* ----- BR1: the board's text ----- */

static bool where_ok(const char *space, const char *path)
{
    size_t n = strlen(path);
    return n && n <= BRAMBLE_PATH_MAX && strlen(space) <= ZXM_SPACE_MAX;
}

static void set_where(bramble_t *b, const char *space, const char *path)
{
    memcpy(b->space, space, strlen(space) + 1);
    if (!strcmp(b->space, "home")) b->space[0] = 0;
    memcpy(b->path, path, strlen(path) + 1);
}

static uint32_t put(char *out, uint32_t at, const char *s, uint32_t n)
{
    if (at + n > BRAMBLE_FILE_MAX) return BRAMBLE_FILE_MAX + 1u;
    memcpy(out + at, s, n);
    return at + n;
}

static uint32_t put_int(char *out, uint32_t at, int64_t v)
{
    char d[24];
    uint32_t n = 0;
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    do {
        d[n++] = (char) ('0' + (char) (u % 10u));
        u /= 10u;
    } while (u);
    if (v < 0) d[n++] = '-';
    char r[24];
    for (uint32_t i = 0; i < n; i++) r[i] = d[n - 1u - i];
    return put(out, at, r, n);
}

/* The board as its file; the length, or BRAMBLE_FILE_MAX + 1 when it does
 * not fit (it always does: 512 lines of at most 74 bytes). */
static uint32_t to_text(const bramble_t *b, char *out)
{
    uint32_t at = put(out, 0, "bramble 1\ncanvas ", 17);
    at = put_int(out, at, b->width);
    at = put(out, at, " ", 1);
    at = put_int(out, at, b->height);
    at = put(out, at, "\nstep ", 6);
    at = put_int(out, at, b->step);
    at = put(out, at, "\n", 1);
    for (uint32_t i = 0; i < b->n; i++) {
        at = put(out, at, "node ", 5);
        at = put_int(out, at, b->node[i].parent);
        at = put(out, at, " ", 1);
        at = put(out, at, b->node[i].label, (uint32_t) strlen(b->node[i].label));
        at = put(out, at, "\n", 1);
    }
    return at;
}

/* One line of text[at..n), without its newline; false at the end. */
static bool next_line(const char *text, uint32_t n, uint32_t *at, const char **line, uint32_t *len)
{
    if (*at >= n) return false;
    uint32_t e = *at;
    while (e < n && text[e] != '\n') e++;
    if (e == n) return false; /* every line ends in a newline */
    *line = text + *at;
    *len = e - *at;
    *at = e + 1;
    return true;
}

/* A decimal integer at s[*i..n), optionally negative, at most 6 digits. */
static bool read_int(const char *s, uint32_t n, uint32_t *i, int64_t *v)
{
    bool neg = false;
    int64_t x = 0;
    uint32_t digits = 0;
    if (*i < n && s[*i] == '-') {
        neg = true;
        (*i)++;
    }
    while (*i < n && s[*i] >= '0' && s[*i] <= '9' && digits < 7) {
        x = x * 10 + (s[*i] - '0');
        (*i)++;
        digits++;
    }
    if (digits == 0 || digits > 6) return false;
    *v = neg ? -x : x;
    return true;
}

static bool word(const char *s, uint32_t n, uint32_t *i, const char *w)
{
    uint32_t k = (uint32_t) strlen(w);
    if (*i + k > n || memcmp(s + *i, w, k)) return false;
    *i += k;
    return true;
}

/* Parses a board into nodes (BR1, BR3); false when it is not one. */
static bool from_text(const char *text, uint32_t n, int64_t *width, int64_t *height, int64_t *step,
                      bramble_node_t *nd, uint32_t *count)
{
    uint32_t at = 0, len = 0, i = 0;
    const char *line = 0;
    if (!next_line(text, n, &at, &line, &len) || len != 9 || memcmp(line, "bramble 1", 9))
        return false;
    if (!next_line(text, n, &at, &line, &len)) return false;
    i = 0;
    if (!word(line, len, &i, "canvas ") || !read_int(line, len, &i, width) ||
        !word(line, len, &i, " ") || !read_int(line, len, &i, height) || i != len)
        return false;
    if (!next_line(text, n, &at, &line, &len)) return false;
    i = 0;
    if (!word(line, len, &i, "step ") || !read_int(line, len, &i, step) || i != len) return false;
    if (!bramble_fits_canvas(*width, *height, 0, *step)) return false;
    uint32_t k = 0;
    while (next_line(text, n, &at, &line, &len)) {
        int64_t parent = 0;
        i = 0;
        if (k >= BRAMBLE_NODES_MAX || !word(line, len, &i, "node ") ||
            !read_int(line, len, &i, &parent) || !word(line, len, &i, " "))
            return false;
        if (k == 0 ? parent != -1 : (parent < 0 || parent >= (int64_t) k)) return false;
        if (!label_ok(line + i, len - i)) return false;
        memset(&nd[k], 0, sizeof nd[k]);
        nd[k].parent = (int32_t) parent;
        nd[k].depth = k == 0 ? 0 : nd[parent].depth + 1;
        if (nd[k].depth > BRAMBLE_DEPTH_MAX) return false;
        set_label(&nd[k], line + i, len - i);
        k++;
    }
    if (at != n || k == 0) return false;
    *count = k;
    return true;
}

int bramble_open(bramble_t *b, const char *space, const char *path)
{
    static char text[BRAMBLE_FILE_MAX + 1];
    static char chunk[BRAMBLE_FILE_MAX + 1];
    static bramble_node_t into[BRAMBLE_NODES_MAX];
    zxm_link_t *l = b->link;
    uint32_t len = 0;
    uint64_t off = 0;
    if (!l || !where_ok(space, path)) return ZXM_E_ARG;
    for (;;) {
        zxm_begin(l, "files.read");
        zxm_p_space(l, space);
        zxm_p_str(l, "path", path);
        zxm_p_u64(l, "offset", off);
        int rc = zxm_call(l);
        if (rc) return rc;
        uint64_t size, next;
        bool eof;
        int32_t t = zxm_get(l, l->result, "text");
        if (zxm_get_u64(l, l->result, "size", &size) || zxm_get_u64(l, l->result, "next", &next) ||
            zxm_get_bool(l, l->result, "eof", &eof) || t < 0)
            return ZXM_E_PROTOCOL;
        if (size > BRAMBLE_FILE_MAX) return ZXM_E_ARG;
        int32_t got = w4_json_str(l->resp, &l->tok[t], chunk, sizeof chunk);
        if (got < 0 || len + (uint32_t) got > BRAMBLE_FILE_MAX) return ZXM_E_ARG;
        memcpy(text + len, chunk, (size_t) got);
        len += (uint32_t) got;
        if (eof) break;
        if (next <= off) return ZXM_E_PROTOCOL;
        off = next;
    }
    int64_t w = 0, h = 0, s = 0;
    uint32_t k = 0;
    int32_t deep = 0;
    if (!from_text(text, len, &w, &h, &s, into, &k)) return ZXM_E_ARG;
    if (lay_out(into, k, w, h, s, &deep)) return ZXM_E_ARG;
    memcpy(b->node, into, k * sizeof into[0]);
    b->n = k;
    b->depth = deep;
    b->width = w;
    b->height = h;
    b->step = s;
    set_where(b, space, path);
    b->dirty = false;
    return ZXM_OK;
}

/* One request's text: whole characters whose escaped form fits the line. */
#define PIECE 9000u

static int write_all(bramble_t *b, const char *space, const char *path, bool exclusive)
{
    static char text[BRAMBLE_FILE_MAX + 2];
    static char esc[ZXM_REQ_MAX];
    zxm_link_t *l = b->link;
    for (uint32_t i = 0; i < b->n; i++)
        if (!bramble_node_ok(i, b->node[i].parent, b->node[i].depth, b->node[i].order, b->node[i].x,
                             b->node[i].y))
            return ZXM_E_STATE;
    uint32_t len = to_text(b, text);
    if (len > BRAMBLE_FILE_MAX) return ZXM_E_TOOBIG;
    uint32_t at = 0;
    bool first = true;
    do {
        uint32_t n = len - at < PIECE ? len - at : PIECE;
        n = at + n < len ? zxm_utf8_cut(text + at, n) : n;
        while (n && zxm_escape(text + at, n, esc, 12000u) < 0) n = zxm_utf8_cut(text + at, n / 2u);
        if (!n) return ZXM_E_TOOBIG;
        bool last = at + n >= len;
        zxm_begin(l, "files.write");
        zxm_p_space(l, space);
        zxm_p_str(l, "path", path);
        zxm_p_strn(l, "text", text + at, n);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        if (exclusive) zxm_p_bool(l, "exclusive", true);
        int rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
    } while (at < len);
    return ZXM_OK;
}

int bramble_save(bramble_t *b, bool owner_confirmed)
{
    if (b->n == 0 || !b->link || !b->path[0]) return ZXM_E_STATE;
    if (!owner_confirmed) return ZXM_E_STATE;
    int rc = write_all(b, b->space, b->path, false);
    if (!rc) b->dirty = false;
    return rc;
}

int bramble_save_as(bramble_t *b, const char *space, const char *path, bool owner_confirmed)
{
    if (b->n == 0 || !b->link) return ZXM_E_STATE;
    if (!where_ok(space, path)) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    int rc = write_all(b, space, path, true);
    if (rc) return rc;
    set_where(b, space, path);
    b->dirty = false;
    return ZXM_OK;
}
