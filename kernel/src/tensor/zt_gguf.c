/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* zt_gguf.c — GGUF parsing and weight decoding (T18). See zt_gguf.h. */
#include "zt_gguf.h"

#define GGUF_MAGIC 0x46554747u /* "GGUF" little-endian */

/* ---- little-endian reads, bounds-checked through a cursor ---- */

typedef struct {
    const uint8_t *b;
    uint64_t size, pos;
    bool bad;
} cur_t;

static bool need(cur_t *c, uint64_t n)
{
    if (c->bad || n > c->size - c->pos) {
        c->bad = true;
        return false;
    }
    return true;
}

static uint64_t rd(cur_t *c, uint32_t n)
{
    if (!need(c, n)) return 0;
    uint64_t v = 0;
    for (uint32_t i = 0; i < n; i++) v |= (uint64_t) c->b[c->pos + i] << (8 * i);
    c->pos += n;
    return v;
}

static zt_gguf_str_t rd_str(cur_t *c)
{
    zt_gguf_str_t s = {0, 0};
    uint64_t n = rd(c, 8);
    if (!need(c, n)) return s;
    s.p = c->b + c->pos;
    s.len = n;
    c->pos += n;
    return s;
}

static uint32_t scalar_size(uint32_t t)
{
    switch (t) {
    case ZT_GGUF_U8:
    case ZT_GGUF_I8:
    case ZT_GGUF_BOOL:
        return 1;
    case ZT_GGUF_U16:
    case ZT_GGUF_I16:
        return 2;
    case ZT_GGUF_U32:
    case ZT_GGUF_I32:
    case ZT_GGUF_F32:
        return 4;
    case ZT_GGUF_U64:
    case ZT_GGUF_I64:
    case ZT_GGUF_F64:
        return 8;
    default:
        return 0;
    }
}

/* ---- float bits to Q16, integer only ---- */

static zt_fx sat_q16(bool neg, uint64_t mag)
{
    if (mag > 0x7FFFFFFFull) mag = neg ? 0x80000000ull : 0x7FFFFFFFull;
    return neg ? (zt_fx) - (int64_t) mag : (zt_fx) mag;
}

/* value = m * 2^e; return round(value * 2^16). */
static zt_fx scaled_q16(bool neg, uint64_t m, int32_t e)
{
    int32_t sh = e + 16;
    if (m == 0) return 0;
    if (sh >= 0) {
        if (sh > 40 || (m >> (63 - sh)) != 0) return sat_q16(neg, ~0ull);
        return sat_q16(neg, m << sh);
    }
    if (-sh > 63) return 0;
    uint32_t r = (uint32_t) -sh;
    return sat_q16(neg, (m + (1ull << (r - 1))) >> r);
}

zt_fx zt_f32_to_q16(uint32_t bits)
{
    bool neg = bits >> 31;
    int32_t ex = (int32_t) ((bits >> 23) & 0xFF);
    uint64_t m = bits & 0x7FFFFFu;
    if (ex == 0xFF) return sat_q16(neg, m ? 0 : ~0ull); /* NaN -> 0, inf saturates */
    if (ex == 0) return scaled_q16(neg, m, -149);
    return scaled_q16(neg, m | 0x800000u, ex - 150);
}

zt_fx zt_f16_to_q16(uint16_t bits)
{
    bool neg = bits >> 15;
    int32_t ex = (bits >> 10) & 0x1F;
    uint64_t m = bits & 0x3FFu;
    if (ex == 0x1F) return sat_q16(neg, m ? 0 : ~0ull);
    if (ex == 0) return scaled_q16(neg, m, -24);
    return scaled_q16(neg, m | 0x400u, ex - 25);
}

zt_fx zt_bf16_to_q16(uint16_t bits)
{
    return zt_f32_to_q16((uint32_t) bits << 16);
}

static zt_fx f64_to_q16(uint64_t bits)
{
    bool neg = bits >> 63;
    int32_t ex = (int32_t) ((bits >> 52) & 0x7FF);
    uint64_t m = bits & 0xFFFFFFFFFFFFFull;
    if (ex == 0x7FF) return sat_q16(neg, m ? 0 : ~0ull);
    if (ex == 0) return 0; /* subnormal doubles are far below Q16 */
    return scaled_q16(neg, m | (1ull << 52), ex - 1075);
}

/* An f16 as an exact (signed 12-bit mantissa, binary exponent) pair:
 * value = m * 2^e. NaN/inf become 0. */
static void f16_split(uint16_t bits, int32_t *m, int32_t *e)
{
    int32_t ex = (bits >> 10) & 0x1F, mm = bits & 0x3FF;
    if (ex == 0x1F) {
        *m = 0;
        *e = 0;
        return;
    }
    if (ex == 0) {
        *e = -24;
    } else {
        mm |= 0x400;
        *e = ex - 25;
    }
    *m = (bits >> 15) ? -mm : mm;
}

/* round(a * 2^e) as Q16 for an integer a, saturating. */
static zt_fx mul_pow2_q16(int64_t a, int32_t e)
{
    bool neg = a < 0;
    return scaled_q16(neg, (uint64_t) (neg ? -a : a), e);
}

/* ---- header walk ---- */

static bool skip_value(cur_t *c, uint32_t type, uint32_t depth)
{
    if (type == ZT_GGUF_STRING) {
        rd_str(c);
        return !c->bad;
    }
    if (type == ZT_GGUF_ARRAY) {
        if (depth > 2) return false;
        uint32_t et = (uint32_t) rd(c, 4);
        uint64_t n = rd(c, 8);
        if (c->bad) return false;
        uint32_t es = scalar_size(et);
        if (es) {
            if (n > (c->size - c->pos) / es) return false;
            c->pos += n * es;
            return true;
        }
        if (et != ZT_GGUF_STRING && et != ZT_GGUF_ARRAY) return false;
        for (uint64_t i = 0; i < n; i++)
            if (!skip_value(c, et, depth + 1)) return false;
        return true;
    }
    uint32_t s = scalar_size(type);
    if (!s || !need(c, s)) return false;
    c->pos += s;
    return true;
}

uint32_t zt_ggml_block(uint32_t type)
{
    switch (type) {
    case ZT_GGML_F32:
    case ZT_GGML_F16:
    case ZT_GGML_BF16:
        return 1;
    case ZT_GGML_Q4_0:
    case ZT_GGML_Q8_0:
        return 32;
    case ZT_GGML_Q4_K:
    case ZT_GGML_Q6_K:
        return 256;
    default:
        return 0;
    }
}

static uint64_t block_bytes(uint32_t type)
{
    switch (type) {
    case ZT_GGML_F32:
        return 4;
    case ZT_GGML_F16:
    case ZT_GGML_BF16:
        return 2;
    case ZT_GGML_Q4_0:
        return 18;
    case ZT_GGML_Q8_0:
        return 34;
    case ZT_GGML_Q4_K:
        return 144;
    case ZT_GGML_Q6_K:
        return 210;
    default:
        return 0;
    }
}

uint64_t zt_ggml_bytes(uint32_t type, uint64_t n)
{
    uint32_t blk = zt_ggml_block(type);
    if (!blk || n % blk) return 0;
    return (n / blk) * block_bytes(type);
}

/* Read one tensor info at the cursor; *rel is its offset into the data
 * section. n_bytes is 0 for unsupported types. */
static bool rd_tinfo(cur_t *c, zt_gguf_tensor_t *t, uint64_t *rel)
{
    t->name = rd_str(c);
    t->n_dims = (uint32_t) rd(c, 4);
    if (c->bad || t->n_dims == 0 || t->n_dims > ZT_GGUF_MAX_DIMS) return false;
    t->n_elems = 1;
    for (uint32_t d = 0; d < ZT_GGUF_MAX_DIMS; d++) t->dims[d] = 1;
    for (uint32_t d = 0; d < t->n_dims; d++) {
        uint64_t v = rd(c, 8);
        if (v == 0 || v > (1ull << 40) || t->n_elems > (1ull << 40) / v) return false;
        t->dims[d] = v;
        t->n_elems *= v;
    }
    t->type = (uint32_t) rd(c, 4);
    *rel = rd(c, 8);
    t->n_bytes = zt_ggml_bytes(t->type, t->n_elems);
    t->data = 0;
    return !c->bad;
}

int32_t zt_gguf_open(zt_gguf_t *g, const uint8_t *buf, uint64_t size)
{
    cur_t c = {buf, size, 0, false};
    if (!g || !buf) return ZT_GGUF_ERANGE;
    if (rd(&c, 4) != GGUF_MAGIC) return c.bad ? ZT_GGUF_ETRUNC : ZT_GGUF_EMAGIC;
    g->version = (uint32_t) rd(&c, 4);
    if (g->version != 2 && g->version != 3) return ZT_GGUF_EVERSION;
    g->n_tensors = rd(&c, 8);
    g->n_kv = rd(&c, 8);
    if (c.bad) return ZT_GGUF_ETRUNC;
    /* each entry needs at least 12 bytes; cheap guard against huge counts */
    if (g->n_kv > size / 12 || g->n_tensors > size / 24) return ZT_GGUF_ERANGE;
    g->buf = buf;
    g->size = size;
    g->kv_off = c.pos;
    g->alignment = 32;
    for (uint64_t i = 0; i < g->n_kv; i++) {
        zt_gguf_str_t k = rd_str(&c);
        uint32_t type = (uint32_t) rd(&c, 4);
        if (c.bad) return ZT_GGUF_ETRUNC;
        if (type == ZT_GGUF_U32 && zt_gguf_str_eq(k, "general.alignment")) {
            uint64_t save = c.pos;
            uint32_t a = (uint32_t) rd(&c, 4);
            c.pos = save;
            if (a == 0 || (a & (a - 1)) || a > 65536) return ZT_GGUF_ERANGE;
            g->alignment = a;
        }
        if (!skip_value(&c, type, 0)) return c.bad ? ZT_GGUF_ETRUNC : ZT_GGUF_ETYPE;
    }
    g->tinfo_off = c.pos;
    for (uint64_t i = 0; i < g->n_tensors; i++) {
        zt_gguf_tensor_t t;
        uint64_t rel;
        if (!rd_tinfo(&c, &t, &rel)) return c.bad ? ZT_GGUF_ETRUNC : ZT_GGUF_ERANGE;
    }
    uint64_t a = g->alignment;
    uint64_t d = (c.pos + a - 1) & ~(a - 1);
    if (d > size) return ZT_GGUF_ETRUNC;
    g->data_off = d;
    /* every supported tensor must lie inside the buffer and be aligned */
    for (uint64_t i = 0; i < g->n_tensors; i++) {
        zt_gguf_tensor_t t;
        int32_t r = zt_gguf_tensor(g, i, &t);
        if (r != ZT_GGUF_OK && r != ZT_GGUF_EUNSUPPORTED) return r;
    }
    return ZT_GGUF_OK;
}

bool zt_gguf_str_eq(zt_gguf_str_t s, const char *z)
{
    uint64_t i = 0;
    for (; i < s.len; i++)
        if (!z[i] || z[i] != (char) s.p[i]) return false;
    return z[i] == 0;
}

/* Fill val from the value at the cursor (type already read). */
static bool rd_value(const zt_gguf_t *g, cur_t *c, uint32_t type, zt_gguf_val_t *v)
{
    uint64_t start = c->pos;
    v->type = type;
    v->u = 0;
    v->i = 0;
    v->q16 = 0;
    v->str.p = 0;
    v->str.len = 0;
    v->elem_type = 0;
    v->count = 0;
    v->data_off = 0;
    switch (type) {
    case ZT_GGUF_U8:
    case ZT_GGUF_U16:
    case ZT_GGUF_U32:
    case ZT_GGUF_U64:
    case ZT_GGUF_BOOL:
        v->u = rd(c, scalar_size(type));
        v->i = (int64_t) v->u;
        break;
    case ZT_GGUF_I8:
        v->i = (int8_t) rd(c, 1);
        v->u = (uint64_t) v->i;
        break;
    case ZT_GGUF_I16:
        v->i = (int16_t) rd(c, 2);
        v->u = (uint64_t) v->i;
        break;
    case ZT_GGUF_I32:
        v->i = (int32_t) rd(c, 4);
        v->u = (uint64_t) v->i;
        break;
    case ZT_GGUF_I64:
        v->i = (int64_t) rd(c, 8);
        v->u = (uint64_t) v->i;
        break;
    case ZT_GGUF_F32:
        v->u = rd(c, 4);
        v->q16 = zt_f32_to_q16((uint32_t) v->u);
        break;
    case ZT_GGUF_F64:
        v->u = rd(c, 8);
        v->q16 = f64_to_q16(v->u);
        break;
    case ZT_GGUF_STRING:
        v->str = rd_str(c);
        break;
    case ZT_GGUF_ARRAY:
        v->elem_type = (uint32_t) rd(c, 4);
        v->count = rd(c, 8);
        v->data_off = c->pos;
        c->pos = start;
        return skip_value(c, ZT_GGUF_ARRAY, 0);
    default:
        return false;
    }
    (void) g;
    return !c->bad;
}

int32_t zt_gguf_kv(const zt_gguf_t *g, uint64_t index, zt_gguf_str_t *key, zt_gguf_val_t *val)
{
    if (index >= g->n_kv) return ZT_GGUF_ENOTFOUND;
    cur_t c = {g->buf, g->size, g->kv_off, false};
    for (uint64_t i = 0;; i++) {
        zt_gguf_str_t k = rd_str(&c);
        uint32_t type = (uint32_t) rd(&c, 4);
        if (i == index) {
            if (key) *key = k;
            zt_gguf_val_t tmp;
            return rd_value(g, &c, type, val ? val : &tmp) ? ZT_GGUF_OK : ZT_GGUF_ETYPE;
        }
        if (!skip_value(&c, type, 0)) return ZT_GGUF_ETRUNC;
    }
}

int32_t zt_gguf_find(const zt_gguf_t *g, const char *key, zt_gguf_val_t *val)
{
    cur_t c = {g->buf, g->size, g->kv_off, false};
    for (uint64_t i = 0; i < g->n_kv; i++) {
        zt_gguf_str_t k = rd_str(&c);
        uint32_t type = (uint32_t) rd(&c, 4);
        if (zt_gguf_str_eq(k, key)) {
            zt_gguf_val_t tmp;
            return rd_value(g, &c, type, val ? val : &tmp) ? ZT_GGUF_OK : ZT_GGUF_ETYPE;
        }
        if (!skip_value(&c, type, 0)) return ZT_GGUF_ETRUNC;
    }
    return ZT_GGUF_ENOTFOUND;
}

int64_t zt_gguf_get_int(const zt_gguf_t *g, const char *key, int64_t def)
{
    zt_gguf_val_t v;
    if (zt_gguf_find(g, key, &v) != ZT_GGUF_OK) return def;
    if (v.type == ZT_GGUF_STRING || v.type == ZT_GGUF_ARRAY || v.type == ZT_GGUF_F32 ||
        v.type == ZT_GGUF_F64)
        return def;
    return v.i;
}

int32_t zt_gguf_arr_int(const zt_gguf_t *g, const zt_gguf_val_t *arr, uint64_t i, int64_t *out)
{
    uint32_t es = scalar_size(arr->elem_type);
    if (arr->type != ZT_GGUF_ARRAY || !es || arr->elem_type == ZT_GGUF_F32 ||
        arr->elem_type == ZT_GGUF_F64)
        return ZT_GGUF_ETYPE;
    if (i >= arr->count) return ZT_GGUF_ERANGE;
    cur_t c = {g->buf, g->size, arr->data_off + i * es, false};
    zt_gguf_val_t v;
    if (!rd_value(g, &c, arr->elem_type, &v)) return ZT_GGUF_ETRUNC;
    *out = v.i;
    return ZT_GGUF_OK;
}

int32_t zt_gguf_arr_q16(const zt_gguf_t *g, const zt_gguf_val_t *arr, uint64_t i, zt_fx *out)
{
    if (arr->type != ZT_GGUF_ARRAY ||
        (arr->elem_type != ZT_GGUF_F32 && arr->elem_type != ZT_GGUF_F64))
        return ZT_GGUF_ETYPE;
    if (i >= arr->count) return ZT_GGUF_ERANGE;
    uint32_t es = scalar_size(arr->elem_type);
    cur_t c = {g->buf, g->size, arr->data_off + i * es, false};
    zt_gguf_val_t v;
    if (!rd_value(g, &c, arr->elem_type, &v)) return ZT_GGUF_ETRUNC;
    *out = v.q16;
    return ZT_GGUF_OK;
}

int32_t zt_gguf_arr_strings(const zt_gguf_t *g, const zt_gguf_val_t *arr,
                            bool (*fn)(void *ctx, uint64_t i, zt_gguf_str_t s), void *ctx)
{
    if (arr->type != ZT_GGUF_ARRAY || arr->elem_type != ZT_GGUF_STRING) return ZT_GGUF_ETYPE;
    cur_t c = {g->buf, g->size, arr->data_off, false};
    for (uint64_t i = 0; i < arr->count; i++) {
        zt_gguf_str_t s = rd_str(&c);
        if (c.bad) return ZT_GGUF_ETRUNC;
        if (!fn(ctx, i, s)) break;
    }
    return ZT_GGUF_OK;
}

typedef struct {
    uint64_t want;
    zt_gguf_str_t got;
} nth_t;

static bool take_nth(void *ctx, uint64_t i, zt_gguf_str_t s)
{
    nth_t *n = ctx;
    if (i != n->want) return true;
    n->got = s;
    return false;
}

int32_t zt_gguf_arr_str(const zt_gguf_t *g, const zt_gguf_val_t *arr, uint64_t i,
                        zt_gguf_str_t *out)
{
    if (i >= arr->count) return ZT_GGUF_ERANGE;
    nth_t n = {i, {0, 0}};
    int32_t r = zt_gguf_arr_strings(g, arr, take_nth, &n);
    if (r != ZT_GGUF_OK) return r;
    *out = n.got;
    return ZT_GGUF_OK;
}

int32_t zt_gguf_tensor(const zt_gguf_t *g, uint64_t index, zt_gguf_tensor_t *t)
{
    if (index >= g->n_tensors) return ZT_GGUF_ENOTFOUND;
    cur_t c = {g->buf, g->size, g->tinfo_off, false};
    uint64_t rel = 0;
    for (uint64_t i = 0; i <= index; i++)
        if (!rd_tinfo(&c, t, &rel)) return ZT_GGUF_ETRUNC;
    if (!t->n_bytes) return ZT_GGUF_EUNSUPPORTED;
    if (rel % g->alignment) return ZT_GGUF_ERANGE;
    if (rel > g->size - g->data_off || t->n_bytes > g->size - g->data_off - rel)
        return ZT_GGUF_ETRUNC;
    t->data = g->buf + g->data_off + rel;
    return ZT_GGUF_OK;
}

int32_t zt_gguf_find_tensor(const zt_gguf_t *g, const char *name, zt_gguf_tensor_t *t)
{
    cur_t c = {g->buf, g->size, g->tinfo_off, false};
    for (uint64_t i = 0; i < g->n_tensors; i++) {
        uint64_t save = c.pos;
        zt_gguf_str_t s = rd_str(&c);
        c.pos = save;
        if (zt_gguf_str_eq(s, name)) return zt_gguf_tensor(g, i, t);
        zt_gguf_tensor_t skip;
        uint64_t rel;
        if (!rd_tinfo(&c, &skip, &rel)) return ZT_GGUF_ETRUNC;
    }
    return ZT_GGUF_ENOTFOUND;
}

/* ---- weight decoding ---- */

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t) (p[0] | (p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

/* ggml's 6-bit scale/min unpacking for Q4_K. */
static void k_scale_min(int j, const uint8_t *q, uint8_t *d, uint8_t *m)
{
    if (j < 4) {
        *d = q[j] & 63;
        *m = q[j + 4] & 63;
    } else {
        *d = (uint8_t) ((q[j + 4] & 0xF) | ((q[j - 4] >> 6) << 4));
        *m = (uint8_t) ((q[j + 4] >> 4) | ((q[j] >> 6) << 4));
    }
}

int32_t zt_gguf_dequant(const zt_gguf_tensor_t *t, uint64_t first, uint64_t n, zt_fx *out)
{
    uint32_t blk = zt_ggml_block(t->type);
    if (!blk || !t->data) return ZT_GGUF_EUNSUPPORTED;
    if (first % blk || n % blk || first > t->n_elems || n > t->n_elems - first)
        return ZT_GGUF_ERANGE;
    const uint8_t *p = t->data + (first / blk) * block_bytes(t->type);
    uint64_t nb = n / blk;
    for (uint64_t b = 0; b < nb; b++, p += block_bytes(t->type)) {
        zt_fx *o = out + b * blk;
        int32_t m, e, mm, em;
        switch (t->type) {
        case ZT_GGML_F32:
            o[0] = zt_f32_to_q16(le32(p));
            break;
        case ZT_GGML_F16:
            o[0] = zt_f16_to_q16(le16(p));
            break;
        case ZT_GGML_BF16:
            o[0] = zt_bf16_to_q16(le16(p));
            break;
        case ZT_GGML_Q8_0:
            f16_split(le16(p), &m, &e);
            for (int i = 0; i < 32; i++) o[i] = mul_pow2_q16((int64_t) m * (int8_t) p[2 + i], e);
            break;
        case ZT_GGML_Q4_0:
            f16_split(le16(p), &m, &e);
            for (int i = 0; i < 16; i++) {
                o[i] = mul_pow2_q16((int64_t) m * ((p[2 + i] & 0xF) - 8), e);
                o[i + 16] = mul_pow2_q16((int64_t) m * ((p[2 + i] >> 4) - 8), e);
            }
            break;
        case ZT_GGML_Q4_K: {
            /* d, dmin (f16), 12 bytes of 6-bit scales/mins, 128 bytes of nibbles */
            f16_split(le16(p), &m, &e);
            f16_split(le16(p + 2), &mm, &em);
            const uint8_t *sc = p + 4, *q = p + 16;
            int is = 0;
            for (int j = 0; j < 256; j += 64, q += 32, is += 2) {
                uint8_t s1, m1, s2, m2;
                k_scale_min(is, sc, &s1, &m1);
                k_scale_min(is + 1, sc, &s2, &m2);
                /* value = d*s*q - dmin*mn; both terms exact, in a common exponent */
                int32_t ec = e < em ? e : em;
                for (int l = 0; l < 32; l++) {
                    int64_t a = ((int64_t) m * s1 * (q[l] & 0xF)) << (e - ec);
                    int64_t b2 = ((int64_t) mm * m1) << (em - ec);
                    o[j + l] = mul_pow2_q16(a - b2, ec);
                    a = ((int64_t) m * s2 * (q[l] >> 4)) << (e - ec);
                    b2 = ((int64_t) mm * m2) << (em - ec);
                    o[j + 32 + l] = mul_pow2_q16(a - b2, ec);
                }
            }
            break;
        }
        case ZT_GGML_Q6_K: {
            /* ql[128], qh[64], scales int8[16], d f16 */
            const uint8_t *ql = p, *qh = p + 128;
            const int8_t *sc = (const int8_t *) (p + 192);
            f16_split(le16(p + 208), &m, &e);
            for (int h = 0; h < 2; h++, ql += 64, qh += 32, sc += 8) {
                zt_fx *y = o + h * 128;
                for (int l = 0; l < 32; l++) {
                    int is = l / 16;
                    int q1 = (int) ((ql[l] & 0xF) | (((qh[l] >> 0) & 3) << 4)) - 32;
                    int q2 = (int) ((ql[l + 32] & 0xF) | (((qh[l] >> 2) & 3) << 4)) - 32;
                    int q3 = (int) ((ql[l] >> 4) | (((qh[l] >> 4) & 3) << 4)) - 32;
                    int q4 = (int) ((ql[l + 32] >> 4) | (((qh[l] >> 6) & 3) << 4)) - 32;
                    y[l] = mul_pow2_q16((int64_t) m * sc[is] * q1, e);
                    y[l + 32] = mul_pow2_q16((int64_t) m * sc[is + 2] * q2, e);
                    y[l + 64] = mul_pow2_q16((int64_t) m * sc[is + 4] * q3, e);
                    y[l + 96] = mul_pow2_q16((int64_t) m * sc[is + 6] * q4, e);
                }
            }
            break;
        }
        default:
            return ZT_GGUF_EUNSUPPORTED;
        }
    }
    return ZT_GGUF_OK;
}

int32_t zt_gguf_to_q8(const zt_gguf_tensor_t *t, uint64_t first, uint64_t n, zt_q8_t *out,
                      zt_fx *scratch)
{
    if (first % 32 || n % 32) return ZT_GGUF_ERANGE;
    if (t->type != ZT_GGML_Q8_0) {
        /* convert in pieces of one source block (or 32 elements) at a time */
        uint64_t step = zt_ggml_block(t->type);
        if (!step) return ZT_GGUF_EUNSUPPORTED;
        if (step < 32) step = 32;
        if (first % step || n % step) return ZT_GGUF_ERANGE;
        for (uint64_t k = 0; k < n; k += step) {
            int32_t r = zt_gguf_dequant(t, first + k, step, scratch);
            if (r) return r;
            zt_quantize(scratch, (uint32_t) step, out + k / 32, false);
        }
        return ZT_GGUF_OK;
    }
    if (!t->data || first > t->n_elems || n > t->n_elems - first) return ZT_GGUF_ERANGE;
    (void) scratch;
    const uint8_t *p = t->data + (first / 32) * 34;
    for (uint64_t b = 0; b < n / 32; b++, p += 34) {
        int32_t m, e;
        f16_split(le16(p), &m, &e);
        /* value = m * 2^e * q; zt value_q16 = q * (scale >> shift) */
        zt_q8_t *o = &out[b];
        o->phi_k = 0;
        /* zt scales are non-negative; ggml writes d = amax / 127 >= 0, but a
         * negative d is folded into the signs (-128 saturates to 127). */
        bool flip = m < 0;
        if (flip) m = -m;
        int32_t sh = e + 16;
        if (sh >= 0) {
            if (sh > 19) sh = 19; /* |m| < 2^11: m << 19 still fits int32 */
            o->scale = m << sh;
            o->shift = 0;
        } else if (-sh <= 63) {
            o->scale = m;
            o->shift = (uint8_t) -sh;
        } else {
            o->scale = 0;
            o->shift = 0;
        }
        for (int i = 0; i < 32; i++) {
            int32_t q = (int8_t) p[2 + i];
            if (flip) q = q == -128 ? 127 : -q;
            o->q[i] = (int8_t) q;
        }
    }
    return ZT_GGUF_OK;
}
