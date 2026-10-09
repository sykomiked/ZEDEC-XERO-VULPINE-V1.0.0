/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_adapter_graphics.c — RGBA pixels with an exact undo residual. See
 * zt_adapters.h G1-G2. */
#include "zt_adapters.h"

static uint32_t prev_px(const zt_graphics_adapter_state_t *st, uint32_t i)
{
    return st->prev ? zt_ld_le32(st->prev + 4u * i) : 0u;
}

static int gfx_pump(zt_adapter_t *self, const void *dc_src, size_t len, zt_ubh168_frame_t *dst,
                    size_t max_frames)
{
    zt_graphics_adapter_state_t *st = self ? (zt_graphics_adapter_state_t *) self->state : 0;
    const uint8_t *src = (const uint8_t *) dc_src;
    if (!st || !src || !dst || st->width == 0) return ZT_ADAPTER_EARG;
    if (len % 4u || len / 4u > (size_t) st->width * st->height || len > 0x7FFFFFF0u)
        return ZT_ADAPTER_EDOMAIN;
    uint32_t px = (uint32_t) (len / 4u);
    size_t need = 1u + (px + 1u) / 2u;
    if (need > max_frames) return ZT_ADAPTER_ESPACE;
    zt_adapter_header(self->kind, self->trunk, (uint32_t) len, (uint32_t) (need - 1u),
                      zt_adapter_fnv1a(src, len, 2166136261u), &dst[0]);
    for (uint32_t f = 0; f + 1u < need; f++) {
        uint32_t i = 2u * f;
        uint32_t a = zt_ld_le32(src + 4u * i), b = i + 1u < px ? zt_ld_le32(src + 4u * i + 4u) : 0;
        uint32_t pb = i + 1u < px ? prev_px(st, i + 1u) : 0;
        const uint32_t sp[3] = {a, b, i};
        const uint32_t sm[2] = {a ^ prev_px(st, i), b ^ pb};
        zt_wire_pack_tagged(zt_adapter_data_tag(self->trunk, f), sp, sm, &dst[f + 1u]);
    }
    return (int) need;
}

static int gfx_drain(zt_adapter_t *self, const zt_ubh168_frame_t *src, size_t n_frames,
                     zt_truth_state_t truth, void *dc_dst, size_t max_len)
{
    zt_graphics_adapter_state_t *st = self ? (zt_graphics_adapter_state_t *) self->state : 0;
    uint8_t *dst = (uint8_t *) dc_dst;
    uint32_t len, frames, sum;
    if (!st || !src || st->width == 0) return ZT_ADAPTER_EARG;
    int e = zt_adapter_read_header(&src[0], self->kind, n_frames, &len, &frames, &sum);
    if (e) return e;
    uint32_t px = len / 4u;
    if (len % 4u || frames != (px + 1u) / 2u) return ZT_ADAPTER_EFORMAT;
    if (truth == ZT_TRUTH_UNKNOWN) return 0;
    if (!dst || len > max_len) return ZT_ADAPTER_ESPACE;
    bool held = zt_truth_is_held(truth);
    uint32_t h = 2166136261u;
    for (uint32_t f = 0; f < frames; f++) {
        zt_unpacked_rails_t r;
        zt_wire_rails_init(&r);
        zt_wire_unpack_ubh168(&src[f + 1u], &r);
        if (r.sync || r.s_plus[2] != 2u * f) return ZT_ADAPTER_EFORMAT;
        for (uint32_t k = 0; k < 2u; k++) {
            uint32_t i = 2u * f + k;
            if (i >= px) break;
            uint32_t cur = r.s_plus[k], old = cur ^ r.s_minus[k];
            uint8_t b[4];
            zt_st_le32(b, cur);
            h = zt_adapter_fnv1a(b, 4, h);
            uint32_t out = cur;
            if (truth == ZT_TRUTH_FALSE) out = old; /* exact undo */
            if (held) {
                uint32_t x = i % st->width, y = i / st->width;
                out = ((x + y) & 1u) ? old : cur; /* checkerboard, never an average */
                if (st->depth) st->depth[i] = 0;  /* S0 at depth plane 0 */
            }
            zt_st_le32(dst + 4u * i, out);
        }
    }
    if (h != sum) return ZT_ADAPTER_EFORMAT;
    return (int) len;
}

void zt_gfx_render_conflict(uint8_t *rgba, uint32_t w, uint32_t x0, uint32_t y0, uint32_t x1,
                            uint32_t y1, uint32_t color_a, uint32_t color_b, zt_truth_state_t truth)
{
    if (!rgba || !w) return;
    for (uint32_t y = y0; y < y1; y++)
        for (uint32_t x = x0; x < x1 && x < w; x++) {
            uint32_t c = truth == ZT_TRUTH_FALSE ? color_b : color_a;
            if (zt_truth_is_held(truth)) c = ((x + y) & 1u) ? color_b : color_a;
            zt_st_le32(rgba + 4u * ((size_t) y * w + x), c);
        }
}

void zt_adapter_graphics_init(zt_adapter_t *a, zt_graphics_adapter_state_t *st, uint8_t trunk)
{
    a->name = "graphics";
    a->state = st;
    a->kind = ZT_ADAPTER_KIND_GRAPHICS;
    a->trunk = (uint8_t) (trunk % 10u);
    a->conflict_run = 0;
    a->threshold_q16 = ZT_ADAPTER_THRESHOLD;
    a->pump_in = gfx_pump;
    a->drain_out = gfx_drain;
    a->evaluate_interference = zt_adapter_evaluate_default;
}
