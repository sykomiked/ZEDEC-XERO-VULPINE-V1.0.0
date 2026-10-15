/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx_vec.c — vector nodes, integer distances and the shape embedding
 * (alx_vec.h). */
#include "alx_vec.h"
#include "alx_int.h"
#include "alx_matrix.h"
#include "../robin_debanks/sha256.h"

static uint64_t be64(const uint8_t *p)
{
    return (uint64_t) alx_be32(p) << 32 | alx_be32(p + 4);
}

static bool raw_cid(const uint8_t *c)
{
    return c[0] == 0x01 && c[1] == 0x55 && c[2] == 0x12 && c[3] == 0x20;
}

static uint32_t rows_at(uint32_t n)
{
    return (ALX_VEC_HEAD + n * ALX_VEC_REC + 63u) & ~63u;
}

int8_t alx_vec_q(const uint8_t *row, uint32_t j)
{
    return (int8_t) row[(j / 32u) * ALX_VEC_BLOCK + 2u + j % 32u];
}

int32_t alx_vec_dot(const uint8_t *row, const int8_t *q, uint32_t nb)
{
    int32_t s = 0; /* |s| <= 4096 * 128 * 128 = 2^26 */
    for (uint32_t b = 0; b < nb; b++, row += ALX_VEC_BLOCK, q += 32)
        for (uint32_t i = 0; i < 32; i++) s += (int32_t) (int8_t) row[2 + i] * q[i];
    return s;
}

uint32_t alx_vec_norm(const uint8_t *row, uint32_t nb)
{
    uint32_t s = 0;
    for (uint32_t b = 0; b < nb; b++, row += ALX_VEC_BLOCK)
        for (uint32_t i = 0; i < 32; i++) {
            int32_t x = (int8_t) row[2 + i];
            s += (uint32_t) (x * x);
        }
    return s;
}

uint32_t alx_vec_qnorm(const int8_t *q, uint32_t nb)
{
    uint32_t s = 0;
    for (uint32_t i = 0; i < nb * 32u; i++) s += (uint32_t) ((int32_t) q[i] * q[i]);
    return s;
}

static uint32_t sat32(uint64_t v)
{
    return v > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t) v;
}

/* scales: host-order words (a builder's array) or big-endian fields of the
 * records of a node, stride bytes apart */
typedef struct {
    const uint8_t *p;
    uint32_t stride;
    bool be;
} scales_t;

static uint32_t scale_at(const scales_t *sc, uint32_t i)
{
    const uint8_t *p = sc->p + (uint64_t) i * sc->stride;
    if (sc->be) return alx_be32(p);
    return *(const uint32_t *) (const void *) p; /* a builder's aligned array */
}

static int64_t elem_sum(const uint8_t *rows, uint32_t rl, uint32_t n, uint32_t j,
                        const scales_t *sc)
{
    int64_t s = 0; /* |q scale| < 2^39, n < 2^15: no overflow */
    for (uint32_t i = 0; i < n; i++) s += (int64_t) alx_vec_q(rows + i * rl, j) * scale_at(sc, i);
    return s;
}

/* the summary's largest |sum|, its element j and its scale */
static uint64_t sum_max(const uint8_t *rows, uint32_t n, uint32_t nb, const scales_t *sc)
{
    uint64_t m = 0;
    for (uint32_t j = 0; j < nb * 32u; j++) {
        int64_t s = elem_sum(rows, nb * ALX_VEC_BLOCK, n, j, sc);
        uint64_t a = s < 0 ? (uint64_t) -s : (uint64_t) s;
        if (a > m) m = a;
    }
    return m;
}

static int8_t sum_q(const uint8_t *rows, uint32_t n, uint32_t nb, uint32_t j, uint64_t m,
                    const scales_t *sc)
{
    /* truncate toward zero: the same on every machine */
    return m ? (int8_t) (elem_sum(rows, nb * ALX_VEC_BLOCK, n, j, sc) * 127 / (int64_t) m) : 0;
}

static uint32_t sum_scale(uint64_t m, uint32_t n)
{
    return m && n ? sat32((m + 127ull * n / 2u) / (127ull * n)) : 0;
}

void alx_vec_summary(const uint8_t *rows, const uint32_t *scales, uint32_t n, uint32_t nb,
                     uint8_t *out_row, uint32_t *out_scale)
{
    scales_t sc = {(const uint8_t *) scales, 4u, false};
    uint64_t m = sum_max(rows, n, nb, &sc);
    for (uint32_t b = 0; b < nb; b++)
        out_row[b * ALX_VEC_BLOCK] = 0x00, out_row[b * ALX_VEC_BLOCK + 1] = 0x3C;
    for (uint32_t j = 0; j < nb * 32u; j++)
        out_row[(j / 32u) * ALX_VEC_BLOCK + 2u + j % 32u] = (uint8_t) sum_q(rows, n, nb, j, m, &sc);
    *out_scale = sum_scale(m, n);
}

const uint8_t *alx_vec_target(const alx_vec_t *v, uint32_t i)
{
    return v->b + ALX_VEC_HEAD + i * ALX_VEC_REC;
}

uint64_t alx_vec_word(const alx_vec_t *v, uint32_t i)
{
    return be64(alx_vec_target(v, i) + 36);
}

uint32_t alx_vec_scale(const alx_vec_t *v, uint32_t i)
{
    return i == v->n ? alx_be32(v->b + 88) : alx_be32(alx_vec_target(v, i) + 44);
}

const uint8_t *alx_vec_row(const alx_vec_t *v, uint32_t i)
{
    return v->rows + (uint64_t) i * v->nb * ALX_VEC_BLOCK;
}

int alx_vec_open(alx_vec_t *v, const uint8_t *b, uint32_t len)
{
    if (!v || !b || len < ALX_VEC_HEAD || len > ALX_BOOK_MAX || b[0] != 'A' || b[1] != 'L' ||
        b[2] != 'X' || b[3] != 'V' || b[4] != 1 || b[5] > ALX_VEC_SUMMARY || !alx_zero(b + 14, 2) ||
        !alx_zero(b + 92, 36) || !raw_cid(b + 16))
        return ALX_E_FORM;
    uint32_t kind = b[5], layer = alx_be16(b + 6), n = alx_be32(b + 8), dim = alx_be16(b + 12);
    if (n == 0 || n > (len - ALX_VEC_HEAD) / ALX_VEC_REC || dim < 32u || dim > ALX_VEC_MAXDIM ||
        dim % 32u)
        return ALX_E_FORM;
    uint32_t nb = dim / 32u, at = rows_at(n), recs = ALX_VEC_HEAD + n * ALX_VEC_REC;
    if ((uint64_t) len != at + (uint64_t) (n + 1u) * nb * ALX_VEC_BLOCK ||
        !alx_zero(b + recs, at - recs))
        return ALX_E_FORM;
    /* a leaf belongs to a layer and names its glue; a summary has neither
     * (layer 145: a summary over the layers) */
    if (kind == ALX_VEC_LEAF ? layer >= ALX_MX_LAYERS || !raw_cid(b + 52)
                             : layer > ALX_MX_LAYERS || !alx_zero(b + 52, 36))
        return ALX_E_FORM;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *r = b + ALX_VEC_HEAD + i * ALX_VEC_REC;
        uint64_t w = be64(r + 36);
        if (!raw_cid(r) || (w && (w >> 56) != ALX_G_VEC)) return ALX_E_FORM;
    }
    const uint8_t *rows = b + at;
    for (uint32_t k = 0; k < (n + 1u) * nb; k++)
        if (rows[k * ALX_VEC_BLOCK] != 0x00 || rows[k * ALX_VEC_BLOCK + 1] != 0x3C)
            return ALX_E_FORM;
    /* row n is the summary of the others, recomputed exactly */
    scales_t sc = {b + ALX_VEC_HEAD + 44u, ALX_VEC_REC, true};
    uint64_t m = sum_max(rows, n, nb, &sc);
    const uint8_t *sum = rows + (uint64_t) n * nb * ALX_VEC_BLOCK;
    for (uint32_t j = 0; j < nb * 32u; j++)
        if (alx_vec_q(sum, j) != sum_q(rows, n, nb, j, m, &sc)) return ALX_E_HASH;
    if (alx_be32(b + 88) != sum_scale(m, n)) return ALX_E_HASH;
    v->b = b, v->len = len, v->kind = kind, v->layer = layer, v->n = n, v->dim = dim, v->nb = nb;
    v->model = b + 16, v->source = b + 52, v->rows = rows;
    return ALX_OK;
}

bool alx_vec_better(uint32_t metric, int32_t da, uint32_t na, uint32_t ia, int32_t db, uint32_t nbn,
                    uint32_t ib, uint32_t qnorm)
{
    if (metric == ALX_VEC_DOT) {
        if (da != db) return da > db;
    } else if (metric == ALX_VEC_L2) {
        int64_t xa = (int64_t) na + qnorm - 2 * (int64_t) da,
                xb = (int64_t) nbn + qnorm - 2 * (int64_t) db;
        if (xa != xb) return xa < xb;
    } else {
        /* cosine: zero rows last; then by sign; then d|d|/norm, crossed */
        if (!na || !nbn) {
            if (!na != !nbn) return na != 0;
        } else if ((da > 0) != (db > 0) || (da < 0) != (db < 0)) {
            int sa = (da > 0) - (da < 0), sb = (db > 0) - (db < 0);
            return sa > sb;
        } else if (da) {
            unsigned __int128 a = (unsigned __int128) ((uint64_t) ((int64_t) da * da)) * nbn,
                              c = (unsigned __int128) ((uint64_t) ((int64_t) db * db)) * na;
            if (a != c) return da > 0 ? a > c : a < c;
        }
    }
    return ia < ib;
}

int32_t alx_vec_search(const alx_vec_t *v, uint32_t metric, const int8_t *q, uint32_t k,
                       uint32_t *out, int32_t *dots, alx_vec_dots_fn fn, void *fctx)
{
    if (!v || !v->b || !q || !out || !dots || metric < ALX_VEC_COS || metric > ALX_VEC_L2)
        return ALX_E_FORM;
    uint32_t n = v->n, nb = v->nb, w = 0, qn = alx_vec_qnorm(q, nb);
    if (!fn || fn(fctx, v->rows, n, nb, q, dots) != 0)
        for (uint32_t i = 0; i < n; i++) dots[i] = alx_vec_dot(alx_vec_row(v, i), q, nb);
    if (k > n) k = n;
    /* insertion into the best k; norms are recomputed, which keeps this
     * allocation-free */
    for (uint32_t i = 0; i < n; i++) {
        uint32_t ni = alx_vec_norm(alx_vec_row(v, i), nb), at = w;
        while (at > 0) {
            uint32_t o = out[at - 1u];
            if (!alx_vec_better(metric, dots[i], ni, i, dots[o],
                                alx_vec_norm(alx_vec_row(v, o), nb), o, qn))
                break;
            at--;
        }
        if (at >= k) continue;
        uint32_t end = w < k ? w : k - 1u;
        for (uint32_t j = end; j > at; j--) out[j] = out[j - 1u];
        out[at] = i;
        if (w < k) w++;
    }
    return (int32_t) w;
}

static uint64_t mix(uint64_t z)
{
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static bool id_start(uint8_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool id_char(uint8_t c)
{
    return id_start(c) || (c >= '0' && c <= '9');
}

uint32_t alx_vec_shape(const uint8_t *b, uint32_t len, uint32_t dim, uint8_t *out_row,
                       int32_t *work)
{
    uint32_t nb = dim / 32u, *seen = (uint32_t *) (work + dim);
    for (uint32_t j = 0; j < dim; j++) work[j] = 0;
    for (uint32_t j = 0; j < ALX_VEC_SEEN; j++) seen[j] = 0;
    /* each distinct identifier once: hashed to one of 2^16 ids, and each id
     * to a bucket and a sign */
    for (uint32_t i = 0; i < len;) {
        if (!id_start(b[i]) || (i && id_char(b[i - 1]))) {
            i++;
            continue;
        }
        uint64_t h = 0xcbf29ce484222325ull; /* FNV-1a over the identifier */
        uint32_t j = i;
        while (j < len && id_char(b[j])) h = (h ^ b[j++]) * 0x100000001b3ull;
        i = j;
        uint32_t id = (uint32_t) (mix(h) >> 48);
        if (seen[id >> 5] >> (id & 31u) & 1u) continue;
        seen[id >> 5] |= 1u << (id & 31u);
        uint64_t f = mix((id + 1ull) * 0x9E3779B97F4A7C15ull);
        work[f & (dim - 1u)] += (f >> 63) ? -1 : 1;
    }
    uint64_t m = 0;
    for (uint32_t j = 0; j < dim; j++) {
        uint64_t a = work[j] < 0 ? (uint64_t) - (int64_t) work[j] : (uint64_t) work[j];
        if (a > m) m = a;
    }
    for (uint32_t k = 0; k < nb; k++)
        out_row[k * ALX_VEC_BLOCK] = 0x00, out_row[k * ALX_VEC_BLOCK + 1] = 0x3C;
    for (uint32_t j = 0; j < dim; j++)
        out_row[(j / 32u) * ALX_VEC_BLOCK + 2u + j % 32u] =
            (uint8_t) (int8_t) (m ? (int64_t) work[j] * 127 / (int64_t) m : 0);
    return m ? sat32((m * 65536u + 63u) / 127u) : 0;
}

void alx_vec_shape_model(uint32_t dim, uint8_t cid[36])
{
    /* the definition, as text: changing any rule changes the CID */
    static const char DEF[] =
        "zxv.alexandria.shape.v2: each distinct identifier [A-Za-z_][A-Za-z0-9_]* "
        "once; id = top 16 bits of splitmix64 finaliser of FNV-1a 64; f = finaliser "
        "of (id + 1) * 0x9E3779B97F4A7C15, bucket f mod dim, sign - if bit 63; "
        "int8 = sum * 127 / max |sum| truncated; dim 0000";
    uint8_t def[sizeof DEF];
    uint32_t n = sizeof DEF - 1u, at = n - 4u;
    /* the last four characters are the dimension (a copy that is not a
     * plain copy, so no library call is needed) */
    for (uint32_t i = 0; i < n; i++)
        def[i] = i < at ? (uint8_t) DEF[i]
                        : (uint8_t) ('0' + dim /
                                               (i == at        ? 1000u
                                                : i == at + 1u ? 100u
                                                : i == at + 2u ? 10u
                                                               : 1u) %
                                               10u);
    cid[0] = 0x01, cid[1] = 0x55, cid[2] = 0x12, cid[3] = 0x20;
    sha256(def, n, cid + 4);
}

uint32_t alx_vec_stale(const alx_vec_t *v, const uint8_t model[36], const uint8_t *glue)
{
    if (!alx_same(v->model, model, 36) || v->kind != ALX_VEC_LEAF) return v->n;
    uint32_t stale = 0;
    for (uint32_t i = 0; i < v->n; i++) stale += alx_glue_find(glue, alx_vec_target(v, i)) < 0;
    return stale;
}
