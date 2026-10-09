/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_adapter_tensor.c — weights and activations to Q16.16 frames. See
 * zt_adapters.h T1-T4. */
#include "zt_adapters.h"
#include "zt_phi.h"

/* ===== T1: float bits -> Q16.16 ===== */

/* sign * sig * 2^shift, rounded half away from zero, saturated. */
static int32_t scale_sig(bool neg, uint32_t sig, int32_t shift, uint32_t *flags)
{
    uint64_t mag;
    if (shift >= 0) {
        if (shift > 31 || ((uint64_t) sig << shift) > 0x80000000ull) {
            *flags |= ZT_Q16_SAT;
            return neg ? INT32_MIN : INT32_MAX;
        }
        mag = (uint64_t) sig << shift;
    } else {
        int32_t r = -shift;
        if (r > 32) return 0;
        mag = ((uint64_t) sig + (1ull << (r - 1))) >> r;
    }
    if (neg) return mag >= 0x80000000ull ? INT32_MIN : -(int32_t) mag;
    if (mag > 0x7FFFFFFFull) {
        *flags |= ZT_Q16_SAT;
        return INT32_MAX;
    }
    return (int32_t) mag;
}

int32_t zt_fp32_bits_to_q16(uint32_t bits, uint32_t *flags)
{
    uint32_t dummy = 0;
    if (!flags) flags = &dummy;
    bool neg = (bits >> 31) != 0;
    uint32_t e = (bits >> 23) & 0xFFu, m = bits & 0x7FFFFFu;
    if (e == 0xFFu) {
        if (m) {
            *flags |= ZT_Q16_NAN;
            return 0;
        }
        *flags |= ZT_Q16_SAT;
        return neg ? INT32_MIN : INT32_MAX;
    }
    if (e == 0) return 0; /* zero and subnormals (< 2^-126) */
    /* value = (m | 2^23) * 2^(e - 150); Q16 = that * 2^16 */
    return scale_sig(neg, m | 0x800000u, (int32_t) e - 134, flags);
}

int32_t zt_bf16_bits_to_q16(uint16_t bits, uint32_t *flags)
{
    return zt_fp32_bits_to_q16((uint32_t) bits << 16, flags);
}

int32_t zt_fp16_bits_to_q16(uint16_t bits, uint32_t *flags)
{
    uint32_t dummy = 0;
    if (!flags) flags = &dummy;
    bool neg = (bits >> 15) != 0;
    uint32_t e = (bits >> 10) & 0x1Fu, m = bits & 0x3FFu;
    if (e == 0x1Fu) {
        if (m) {
            *flags |= ZT_Q16_NAN;
            return 0;
        }
        *flags |= ZT_Q16_SAT;
        return neg ? INT32_MIN : INT32_MAX;
    }
    if (e == 0) return scale_sig(neg, m, -8, flags); /* m * 2^-24 */
    return scale_sig(neg, m | 0x400u, (int32_t) e - 9, flags);
}

/* ===== T3: tiling ===== */

typedef struct {
    zt_tile_t stack[64];
    uint32_t sp;
} tile_iter_t;

static void tiles_begin(tile_iter_t *it, uint32_t rows, uint32_t cols)
{
    it->sp = 0;
    if (rows && cols) {
        zt_tile_t t = {0, 0, (uint16_t) rows, (uint16_t) cols};
        it->stack[it->sp++] = t;
    }
}

/* Next leaf tile in depth-first order, first part first. */
static bool tiles_next(tile_iter_t *it, zt_tile_t *out)
{
    while (it->sp) {
        zt_tile_t t = it->stack[--it->sp];
        if (t.nr <= ZT_TILE_MAX && t.nc <= ZT_TILE_MAX) {
            *out = t;
            return true;
        }
        bool by_rows = t.nr >= t.nc;
        uint32_t L = by_rows ? t.nr : t.nc;
        uint32_t cut = (L * 40503u + 32768u) >> 16; /* round(L / phi) */
        if (cut < 1u) cut = 1u;
        if (cut >= L) cut = L - 1u;
        zt_tile_t a = t, b = t;
        if (by_rows) {
            a.nr = (uint16_t) cut;
            b.r0 = (uint16_t) (t.r0 + cut);
            b.nr = (uint16_t) (L - cut);
        } else {
            a.nc = (uint16_t) cut;
            b.c0 = (uint16_t) (t.c0 + cut);
            b.nc = (uint16_t) (L - cut);
        }
        if (it->sp + 2u > 64u) return false; /* cannot happen for 16-bit sides */
        it->stack[it->sp++] = b;
        it->stack[it->sp++] = a;
    }
    return false;
}

uint32_t zt_tensor_tile_plan(uint32_t rows, uint32_t cols, zt_tile_t *out, uint32_t cap)
{
    if (rows > 65535u || cols > 65535u) return 0;
    tile_iter_t it;
    zt_tile_t t;
    uint32_t n = 0;
    tiles_begin(&it, rows, cols);
    while (tiles_next(&it, &t)) {
        if (out && n < cap) out[n] = t;
        n++;
    }
    return n;
}

/* Position walk: tile by tile, row-major inside each tile. */
typedef struct {
    tile_iter_t it;
    zt_tile_t cur;
    uint32_t r, c;
    bool live;
    uint32_t cols;
} pos_walk_t;

static void walk_begin(pos_walk_t *w, uint32_t rows, uint32_t cols)
{
    tiles_begin(&w->it, rows, cols);
    w->cols = cols;
    w->live = tiles_next(&w->it, &w->cur);
    w->r = w->c = 0;
}

static bool walk_next(pos_walk_t *w, uint32_t *index)
{
    if (!w->live) return false;
    *index = (uint32_t) (w->cur.r0 + w->r) * w->cols + w->cur.c0 + w->c;
    if (++w->c == w->cur.nc) {
        w->c = 0;
        if (++w->r == w->cur.nr) {
            w->r = 0;
            w->live = tiles_next(&w->it, &w->cur);
        }
    }
    return true;
}

/* ===== the adapter ===== */

size_t zt_tensor_frames_for(uint32_t elements)
{
    return 1u + ((size_t) elements + 2u) / 3u;
}

static uint32_t elem_size(zt_tensor_dtype_t d)
{
    return d == ZT_DTYPE_FP32 ? 4u : d == ZT_DTYPE_INT8 ? 1u : 2u;
}

static int32_t convert(zt_tensor_adapter_state_t *st, const uint8_t *src, uint32_t i)
{
    uint32_t fl = 0;
    int32_t v = 0;
    switch (st->dtype) {
    case ZT_DTYPE_FP32:
        v = zt_fp32_bits_to_q16(zt_ld_le32(src + 4u * i), &fl);
        break;
    case ZT_DTYPE_BF16:
        v = zt_bf16_bits_to_q16((uint16_t) (src[2u * i] | src[2u * i + 1u] << 8), &fl);
        break;
    case ZT_DTYPE_FP16:
        v = zt_fp16_bits_to_q16((uint16_t) (src[2u * i] | src[2u * i + 1u] << 8), &fl);
        break;
    case ZT_DTYPE_INT8: {
        int32_t scale = st->block_scale_q16[i / st->block];
        if (st->snap_phi) scale = zt_phi_snap_q16(scale, 0);
        int64_t p = (int64_t) (int8_t) src[i] * scale;
        if (p > INT32_MAX) {
            p = INT32_MAX;
            fl |= ZT_Q16_SAT;
        } else if (p < INT32_MIN) {
            p = INT32_MIN;
            fl |= ZT_Q16_SAT;
        }
        v = (int32_t) p;
        break;
    }
    }
    if (fl & ZT_Q16_NAN) st->nan_count++;
    if (fl & ZT_Q16_SAT) st->sat_count++;
    return v;
}

static int tensor_pump(zt_adapter_t *self, const void *dc_src, size_t len, zt_ubh168_frame_t *dst,
                       size_t max_frames)
{
    zt_tensor_adapter_state_t *st = self ? (zt_tensor_adapter_state_t *) self->state : 0;
    const uint8_t *src = (const uint8_t *) dc_src;
    if (!st || !src || !dst || st->rows > 65535u || st->cols > 65535u) return ZT_ADAPTER_EARG;
    uint32_t n = st->rows * st->cols;
    if (n == 0 || n > (1u << 28) || len != (size_t) n * elem_size(st->dtype))
        return ZT_ADAPTER_EDOMAIN; /* 2^28 elements keeps every count inside int */
    if (st->dtype == ZT_DTYPE_INT8 && (!st->block_scale_q16 || st->block == 0))
        return ZT_ADAPTER_EARG;
    size_t need = zt_tensor_frames_for(n);
    if (need > max_frames || need > 0x7FFFFFFFu) return ZT_ADAPTER_ESPACE;
    st->nan_count = st->sat_count = 0;
    pos_walk_t w;
    walk_begin(&w, st->rows, st->cols);
    uint32_t h = 2166136261u, pos = 0;
    for (size_t f = 1; f < need; f++) {
        uint32_t v[3] = {0, 0, 0};
        for (uint32_t k = 0; k < 3u; k++) {
            uint32_t idx;
            if (!walk_next(&w, &idx)) break;
            v[k] = (uint32_t) convert(st, src, idx);
            uint8_t b[4];
            zt_st_le32(b, v[k]);
            h = zt_adapter_fnv1a(b, 4, h);
        }
        const uint32_t sm[2] = {pos, 0u - (v[0] + v[1] + v[2])};
        zt_wire_pack_tagged(zt_adapter_data_tag(self->trunk, (uint32_t) (f - 1u)), v, sm, &dst[f]);
        pos += 3u;
    }
    zt_adapter_header(self->kind, self->trunk, n, (uint32_t) (need - 1u), h, &dst[0]);
    return (int) need;
}

static int tensor_drain(zt_adapter_t *self, const zt_ubh168_frame_t *src, size_t n_frames,
                        zt_truth_state_t truth, void *dc_dst, size_t max_len)
{
    zt_tensor_adapter_state_t *st = self ? (zt_tensor_adapter_state_t *) self->state : 0;
    int32_t *dst = (int32_t *) dc_dst;
    uint32_t n, frames, sum;
    if (!st || !src) return ZT_ADAPTER_EARG;
    int e = zt_adapter_read_header(&src[0], self->kind, n_frames, &n, &frames, &sum);
    if (e) return e;
    if (n != st->rows * st->cols || n > (1u << 28) ||
        (size_t) frames + 1u != zt_tensor_frames_for(n))
        return ZT_ADAPTER_EFORMAT;
    if (truth == ZT_TRUTH_FALSE || truth == ZT_TRUTH_UNKNOWN) return 0;
    if (zt_truth_is_held(truth)) return ZT_ADAPTER_EHELD;
    if (!dst || (size_t) n * 4u > max_len) return ZT_ADAPTER_ESPACE;
    pos_walk_t w;
    walk_begin(&w, st->rows, st->cols);
    uint32_t h = 2166136261u;
    for (uint32_t f = 0; f < frames; f++) {
        zt_unpacked_rails_t r;
        zt_wire_rails_init(&r);
        zt_wire_unpack_ubh168(&src[f + 1u], &r);
        if (r.sync || r.s_minus[0] != 3u * f ||
            r.s_minus[1] != 0u - (r.s_plus[0] + r.s_plus[1] + r.s_plus[2]))
            return ZT_ADAPTER_EFORMAT;
        for (uint32_t k = 0; k < 3u; k++) {
            uint32_t idx;
            if (!walk_next(&w, &idx)) break;
            uint8_t b[4];
            zt_st_le32(b, r.s_plus[k]);
            h = zt_adapter_fnv1a(b, 4, h);
            dst[idx] = (int32_t) r.s_plus[k];
        }
    }
    if (h != sum) return ZT_ADAPTER_EFORMAT;
    return (int) (n * 4u);
}

void zt_adapter_tensor_init(zt_adapter_t *a, zt_tensor_adapter_state_t *st, uint8_t trunk)
{
    a->name = "tensor";
    a->state = st;
    a->kind = ZT_ADAPTER_KIND_TENSOR;
    a->trunk = (uint8_t) (trunk % 10u);
    a->conflict_run = 0;
    a->threshold_q16 = ZT_ADAPTER_THRESHOLD;
    a->pump_in = tensor_pump;
    a->drain_out = tensor_drain;
    a->evaluate_interference = zt_adapter_evaluate_default;
    st->nan_count = st->sat_count = 0;
}
