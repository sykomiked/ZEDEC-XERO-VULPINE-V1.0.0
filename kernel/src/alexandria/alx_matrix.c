/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx_matrix.c — booklets, fan-out nodes, glue and the root (alx_matrix.h). */
#include "alx_matrix.h"
#include "alx_int.h"
#include "../robin_debanks/sha256.h"

static bool raw_cid(const uint8_t *c)
{
    return c[0] == 0x01 && c[1] == 0x55 && c[2] == 0x12 && c[3] == 0x20;
}

static bool cid_of(const uint8_t *c, const uint8_t *b, uint32_t len)
{
    uint8_t h[32];
    sha256(b, len, h);
    return raw_cid(c) && alx_same(c + 4, h, 32);
}

/* the CID compare that orders glue atoms */
static int cmp36(const uint8_t *a, const uint8_t *b)
{
    for (uint32_t i = 0; i < 36; i++)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}

static bool head(const uint8_t *b, uint32_t len, const char *magic, uint8_t ver)
{
    return b && len >= 64u && len <= ALX_BOOK_MAX && b[0] == (uint8_t) magic[0] &&
           b[1] == (uint8_t) magic[1] && b[2] == (uint8_t) magic[2] && b[3] == (uint8_t) magic[3] &&
           b[4] == ver;
}

int alx_bk2_check(const uint8_t *b, uint32_t len, uint32_t *layer, uint32_t *atoms)
{
    if (!head(b, len, "ALXB", 2) || b[5] > 2 || alx_be32(b + 12) != len || !alx_zero(b + 16, 48))
        return ALX_E_FORM;
    uint32_t lay = alx_be16(b + 6), n = alx_be32(b + 8);
    if (lay >= ALX_MX_LAYERS || n == 0 || n > (len - 64u) / ALX_MX_DIR) return ALX_E_FORM;
    uint32_t dir = 64u + n * ALX_MX_DIR, at = (dir + 63u) & ~63u;
    if (at > len || !alx_zero(b + dir, at - dir)) return ALX_E_FORM;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *d = b + 64u + i * ALX_MX_DIR;
        uint32_t off = alx_be32(d + 36), l = alx_be32(d + 40);
        if (off != at || l == 0 || l > len - off || !alx_zero(d + 44, 4)) return ALX_E_FORM;
        uint32_t end = off + l, next = (end + 63u) & ~63u;
        if (i + 1u == n ? end != len : next > len || !alx_zero(b + end, next - end))
            return ALX_E_FORM; /* atoms on 64-byte boundaries, zero between, none after */
        if (!cid_of(d, b + off, l)) return ALX_E_HASH;
        at = next;
    }
    if (layer) *layer = lay;
    if (atoms) *atoms = n;
    return ALX_OK;
}

void alx_bk2_atom(const uint8_t *b, uint32_t i, const uint8_t **cid, uint32_t *off, uint32_t *len)
{
    const uint8_t *d = b + 64u + i * ALX_MX_DIR;
    if (cid) *cid = d;
    if (off) *off = alx_be32(d + 36);
    if (len) *len = alx_be32(d + 40);
}

int alx_node_check(const uint8_t *b, uint32_t len, uint32_t *level, uint32_t *n, uint64_t *beneath)
{
    if (!head(b, len, "ALXN", 1) || !alx_zero(b + 12, 4) || !alx_zero(b + 24, 40))
        return ALX_E_FORM;
    uint32_t lv = b[5], c = alx_be32(b + 8), lay = alx_be16(b + 6);
    uint64_t under = (uint64_t) alx_be32(b + 16) << 32 | alx_be32(b + 20);
    if (lv >= ALX_MX_DEPTH || lay >= ALX_MX_LAYERS || c == 0 || c > ALX_MX_FAN ||
        len != 64u + c * 36u)
        return ALX_E_FORM;
    /* every child but the last is full: 256^level booklets each */
    uint64_t full = 1ull << (8u * lv);
    if (under <= (c - 1u) * full || under > c * full) return ALX_E_FORM;
    for (uint32_t i = 0; i < c; i++)
        if (!raw_cid(b + 64u + i * 36u)) return ALX_E_FORM;
    if (level) *level = lv;
    if (n) *n = c;
    if (beneath) *beneath = under;
    return ALX_OK;
}

const uint8_t *alx_node_child(const uint8_t *b, uint32_t i)
{
    return b + 64u + i * 36u;
}

uint32_t alx_mx_depth(uint64_t n)
{
    uint32_t d = 1;
    while (d < ALX_MX_DEPTH && n > (1ull << (8u * d))) d++;
    return d;
}

uint32_t alx_mx_digit(uint64_t k, uint32_t level)
{
    return (uint32_t) (k >> (8u * level)) & 255u;
}

int alx_mx_check(const uint8_t *b, uint32_t len)
{
    uint32_t body = 64u + ALX_MX_LAYERS * ALX_MX_LAYER;
    if (!b || len != body + 32u || b[0] != 'A' || b[1] != 'L' || b[2] != 'X' || b[3] != 'M' ||
        b[4] != 2 || b[5] != 0 || alx_be16(b + 6) != ALX_MX_LAYERS ||
        alx_be32(b + 8) != ALX_MX_FAN || !alx_zero(b + 12, 52))
        return ALX_E_FORM;
    uint8_t h[32];
    sha256(b, body, h);
    if (!alx_same(h, b + body, 32)) return ALX_E_HASH;
    for (uint32_t k = 0; k < ALX_MX_LAYERS; k++) {
        const uint8_t *r = b + 64u + k * ALX_MX_LAYER;
        uint32_t n = alx_be32(r + 108);
        /* every layer has its glue (an empty one's is marked N); a tree
         * exactly when it holds booklets */
        bool empty = alx_zero(r, 36);
        if (!raw_cid(r + 36) || (empty ? n != 0 : n == 0 || !raw_cid(r))) return ALX_E_FORM;
        if (!alx_zero(r + 72, 36) && !raw_cid(r + 72)) return ALX_E_FORM;
    }
    /* the core boots first: it is never empty */
    return alx_zero(b + 64, 36) ? ALX_E_FORM : ALX_OK;
}

void alx_mx_layer(const uint8_t *b, uint32_t k, alx_mx_layer_t *l)
{
    const uint8_t *r = b + 64u + k * ALX_MX_LAYER;
    l->root = r;
    l->glue = r + 36;
    l->icon = r + 72;
    l->booklets = alx_be32(r + 108);
}

static const uint8_t *g_file(const uint8_t *b, uint32_t i)
{
    return b + 64u + i * ALX_MX_FILE;
}

static const uint8_t *g_edge(const uint8_t *b, uint32_t i)
{
    return b + 64u + alx_be32(b + 8) * ALX_MX_FILE + i * ALX_MX_EDGE;
}

static const uint8_t *g_ref(const uint8_t *b, uint32_t i)
{
    return g_edge(b, alx_be32(b + 12)) + i * ALX_MX_REF;
}

/* the prime dimensional layers: F(i) for i = 0..12 (F1 = F2 = 1) */
static const uint8_t FIBS[] = {0, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144};

int alx_mx_anchor(uint32_t k)
{
    for (uint32_t i = 0; i < sizeof FIBS; i++)
        if (FIBS[i] == k) return (int) (i < 2u ? i : i + 1u);
    return -1;
}

uint32_t alx_mx_anchor_below(uint32_t k)
{
    uint32_t a = 0;
    for (uint32_t i = 0; i < sizeof FIBS; i++)
        if (FIBS[i] < k) a = FIBS[i];
    return a;
}

void alx_mx_split(uint32_t k, uint32_t *anchor, uint32_t *pattern)
{
    if (k == 0 || alx_mx_anchor(k) >= 0) {
        *anchor = alx_mx_anchor_below(k);
        *pattern = 0;
        return;
    }
    uint64_t a = 1, b = 2; /* the largest Fibonacci number at most k */
    while (b <= k) {
        uint64_t c = a + b;
        a = b;
        b = c;
    }
    *anchor = (uint32_t) a;
    *pattern = k - (uint32_t) a;
}

bool alx_mx_draws(uint32_t k, uint32_t l)
{
    while (k) {
        if (alx_mx_anchor(k) >= 0) return alx_mx_anchor(l) >= 0 && l < k;
        uint32_t a, m;
        alx_mx_split(k, &a, &m);
        if ((alx_mx_anchor(l) >= 0 && l <= a) || l == m) return true;
        k = m;
    }
    return false;
}

/* the frame: three star tetrahedra, scales 1, phi^4 = 2 + 3 phi and
 * phi^8 = 13 + 21 phi; a vertex is the scale times a corner (a, b, c, 1, 1,
 * 1, 1, 1) of the star's front tetrahedron in doubled E8 coordinates, the +
 * one (even sign parity) for stars 0 and 2, the - one (odd) for star 1 */
static const int8_t TETRA[4][3] = {{1, 1, 1}, {1, -1, -1}, {-1, 1, -1}, {-1, -1, 1}};
static const int64_t STAR_SCALE[3][2] = {{1, 0}, {2, 3}, {13, 21}};

int alx_mx_pos(uint32_t k, int64_t out[16])
{
    if (k >= ALX_MX_LAYERS) return ALX_E_FORM;
    uint32_t a, m;
    alx_mx_split(k, &a, &m);
    uint32_t f = alx_mx_anchor(k) >= 0 ? k : a, at = 0;
    while (FIBS[at] != f) at++;
    uint32_t g = at / 4u, i = at % 4u; /* star g, corner i */
    for (uint32_t j = 0; j < 8; j++) {
        /* the front tetrahedron alternates by star: +, -, + */
        int64_t c = j < 3 ? (g == 1u ? -TETRA[i][j] : TETRA[i][j]) : 1;
        out[2 * j] = c * STAR_SCALE[g][0];
        out[2 * j + 1] = c * STAR_SCALE[g][1];
    }
    if (f == k) return ALX_OK;
    int64_t sub[16];
    int r = alx_mx_pos(m, sub);
    if (r != ALX_OK) return r;
    /* minus (p + q phi) / phi = minus ((q - p) + p phi) */
    for (uint32_t j = 0; j < 8; j++) {
        out[2 * j] -= sub[2 * j + 1] - sub[2 * j];
        out[2 * j + 1] -= sub[2 * j];
    }
    return ALX_OK;
}

uint32_t alx_mx_fill(uint32_t i)
{
    /* core out: the core, then each star's anchors followed by the branch
     * layers below that star's last anchor and above the star before */
    if (i >= ALX_MX_LAYERS) return ALX_MX_LAYERS;
    uint32_t step = 0, lo = 0;
    for (uint32_t g = 0; g < 3u; g++) {
        for (uint32_t c = 0; c < 4u; c++, step++)
            if (step == i) return FIBS[g * 4u + c];
        uint32_t hi = FIBS[g * 4u + 3u];
        for (uint32_t k = lo + 1u; k < hi; k++)
            if (alx_mx_anchor(k) < 0 && step++ == i) return k;
        lo = hi;
    }
    return ALX_MX_LAYERS;
}

uint32_t alx_mx_gather(uint32_t i)
{
    return i < ALX_MX_LAYERS ? alx_mx_fill(ALX_MX_LAYERS - 1u - i) : ALX_MX_LAYERS;
}

int alx_glue_check(const uint8_t *b, uint32_t len, uint32_t *layer, uint32_t *files,
                   uint32_t *edges, uint32_t *refs)
{
    /* state: lpres N (0, an empty scaffold layer) or T (1, it holds content) */
    if (!head(b, len, "ALXG", 3) || b[5] > 1 || !alx_zero(b + 60, 4)) return ALX_E_FORM;
    uint32_t lay = alx_be16(b + 6), n = alx_be32(b + 8), m = alx_be32(b + 12), r = alx_be32(b + 16);
    if (lay >= ALX_MX_LAYERS) return ALX_E_FORM;
    /* the anchor it hangs from and its pattern layer, exactly as the rule says */
    uint32_t up = alx_be16(b + 56), pat = alx_be16(b + 58), wa, wp;
    alx_mx_split(lay, &wa, &wp);
    if (up != wa || pat != wp || (lay == 0 ? !alx_zero(b + 20, 36) : !raw_cid(b + 20)))
        return ALX_E_FORM;
    uint64_t want = 64u + (uint64_t) n * ALX_MX_FILE + (uint64_t) m * ALX_MX_EDGE +
                    (uint64_t) r * ALX_MX_REF + (pat ? 36u : 0u);
    /* N carries nothing; T may hold only chunks its descendants share */
    if (want != len || r < n || (b[5] == 0 && (n || m || r)) || (n == 0 && (m || r)))
        return ALX_E_FORM;
    if (pat && !raw_cid(b + len - 36u)) return ALX_E_FORM;
    uint32_t next = 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *f = g_file(b, i);
        /* each file's refs follow the last one's, and none is empty */
        if (!raw_cid(f) || (i && cmp36(g_file(b, i - 1u), f) >= 0) || alx_be32(f + 36) != next ||
            alx_be32(f + 40) == 0 || alx_be32(f + 40) > r - next || !alx_zero(f + 44, 4))
            return ALX_E_FORM;
        next += alx_be32(f + 40);
    }
    if (next != r) return ALX_E_FORM;
    uint64_t prev = 0;
    bool anchor = alx_mx_anchor(lay) >= 0;
    for (uint32_t i = 0; i < m; i++) {
        const uint8_t *e = g_edge(b, i);
        uint32_t from = alx_be32(e), to_l = alx_be16(e + 4), type = alx_be16(e + 6),
                 to = alx_be32(e + 8), w = alx_be16(e + 12);
        uint64_t key = (uint64_t) from << 40 | (uint64_t) to_l << 32 | to;
        if (from >= n || to_l >= ALX_MX_LAYERS || type < ALX_GLUE_INCLUDE || type > ALX_GLUE_SAME ||
            w == 0 || !alx_zero(e + 14, 2) || (to_l == lay && (to >= n || to == from)) ||
            (i && key <= prev) || (anchor && alx_mx_anchor(to_l) < 0))
            return ALX_E_FORM;
        prev = key;
    }
    /* a layer's chunks are its own or its lineage's, never a stranger's */
    for (uint32_t i = 0; i < r; i++) {
        uint32_t l = alx_be16(g_ref(b, i));
        if (l >= ALX_MX_LAYERS || !alx_zero(g_ref(b, i) + 2, 2) ||
            (l != lay && !alx_mx_draws(lay, l)))
            return ALX_E_FORM;
    }
    if (layer) *layer = lay;
    if (files) *files = n;
    if (edges) *edges = m;
    if (refs) *refs = r;
    return ALX_OK;
}

int alx_glue_state(const uint8_t *b)
{
    return b[5];
}

const uint8_t *alx_glue_anchor(const uint8_t *b, uint32_t *layer)
{
    if (layer) *layer = alx_be16(b + 56);
    return alx_be16(b + 6) ? b + 20 : 0;
}

const uint8_t *alx_glue_pattern(const uint8_t *b, uint32_t len, uint32_t *layer)
{
    if (layer) *layer = alx_be16(b + 58);
    return alx_be16(b + 58) ? b + len - 36u : 0;
}

int32_t alx_glue_find(const uint8_t *b, const uint8_t cid[36])
{
    uint32_t lo = 0, hi = alx_be32(b + 8);
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        int c = cmp36(g_file(b, mid), cid);
        if (c == 0) return (int32_t) mid;
        if (c < 0)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return -1;
}

uint32_t alx_glue_nrefs(const uint8_t *b, uint32_t f)
{
    return alx_be32(g_file(b, f) + 40);
}

void alx_glue_ref(const uint8_t *b, uint32_t f, uint32_t i, uint32_t *layer, uint32_t *booklet,
                  uint32_t *atom)
{
    const uint8_t *r = g_ref(b, alx_be32(g_file(b, f) + 36) + i);
    if (layer) *layer = alx_be16(r);
    if (booklet) *booklet = alx_be32(r + 4);
    if (atom) *atom = alx_be32(r + 8);
}

/* whether file f has a chunk in booklet bk of this layer */
static bool needs(const uint8_t *b, uint32_t f, uint32_t lay, uint32_t bk)
{
    for (uint32_t i = 0, n = alx_glue_nrefs(b, f); i < n; i++) {
        uint32_t l, k;
        alx_glue_ref(b, f, i, &l, &k, 0);
        if (l == lay && k == bk) return true;
    }
    return false;
}

uint32_t alx_glue_prefetch(const uint8_t *b, uint32_t f, uint32_t *out, uint32_t cap,
                           uint32_t *other_layers)
{
    uint32_t m = alx_be32(b + 12), lay = alx_be16(b + 6), lo = 0, hi = m, k = 0, other = 0;
    while (lo < hi) { /* the first edge from f */
        uint32_t mid = lo + (hi - lo) / 2u;
        if (alx_be32(g_edge(b, mid)) < f)
            lo = mid + 1u;
        else
            hi = mid;
    }
    uint32_t first = lo, end = lo;
    while (end < m && alx_be32(g_edge(b, end)) == f) {
        if (alx_be16(g_edge(b, end) + 4) != lay) other++;
        end++;
    }
    /* edges heaviest first (ties in edge order), each booklet once */
    uint32_t done_w = 0x10000u, done_i = 0;
    for (;;) {
        uint32_t bw = 0, bi = 0;
        for (uint32_t i = first; i < end; i++) {
            const uint8_t *e = g_edge(b, i);
            uint32_t w = alx_be16(e + 12);
            if (alx_be16(e + 4) != lay || w > done_w || (w == done_w && i <= done_i)) continue;
            if (w > bw) bw = w, bi = i;
        }
        if (!bw) break;
        done_w = bw, done_i = bi;
        uint32_t to = alx_be32(g_edge(b, bi) + 8);
        for (uint32_t i = 0, n = alx_glue_nrefs(b, to); i < n && k < cap; i++) {
            uint32_t l, bk;
            alx_glue_ref(b, to, i, &l, &bk, 0);
            bool seen = l != lay || needs(b, f, lay, bk);
            for (uint32_t j = 0; j < k && !seen; j++) seen = out[j] == bk;
            if (!seen) out[k++] = bk;
        }
        if (k == cap) break;
    }
    if (other_layers) *other_layers = other;
    return k;
}
