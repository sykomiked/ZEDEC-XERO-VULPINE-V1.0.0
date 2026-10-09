/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_adapter_audio.c — stereo PCM with a side channel, cancellation
 * detection and a wavefolder. See zt_adapters.h U1-U4. */
#include "zt_adapters.h"

static int16_t lo16(uint32_t w)
{
    return (int16_t) (uint16_t) w;
}
static int16_t hi16(uint32_t w)
{
    return (int16_t) (uint16_t) (w >> 16);
}
static uint32_t pack16(int32_t lo, int32_t hi)
{
    return (uint32_t) (uint16_t) lo | (uint32_t) (uint16_t) hi << 16;
}
static int32_t side_of(uint32_t w)
{
    return ((int32_t) lo16(w) - (int32_t) hi16(w)) >> 1; /* (L - R) / 2 fits int16 */
}

static int audio_pump(zt_adapter_t *self, const void *dc_src, size_t len, zt_ubh168_frame_t *dst,
                      size_t max_frames)
{
    const uint8_t *src = (const uint8_t *) dc_src;
    if (!self || !src || !dst) return ZT_ADAPTER_EARG;
    if (len % 4u || len > 0x7FFFFFF0u) return ZT_ADAPTER_EDOMAIN;
    uint32_t n = (uint32_t) (len / 4u); /* stereo pairs */
    size_t need = 1u + (n + 2u) / 3u;
    if (need > max_frames) return ZT_ADAPTER_ESPACE;
    zt_adapter_header(self->kind, self->trunk, (uint32_t) len, (uint32_t) (need - 1u),
                      zt_adapter_fnv1a(src, len, 2166136261u), &dst[0]);
    for (uint32_t f = 0; f + 1u < need; f++) {
        uint32_t s[3];
        for (uint32_t k = 0; k < 3u; k++) {
            uint32_t i = 3u * f + k;
            s[k] = i < n ? zt_ld_le32(src + 4u * i) : 0u;
        }
        const uint32_t sm[2] = {pack16(side_of(s[0]), side_of(s[1])), pack16(side_of(s[2]), 0)};
        zt_wire_pack_tagged(zt_adapter_data_tag(self->trunk, f), s, sm, &dst[f + 1u]);
    }
    return (int) need;
}

static int audio_drain(zt_adapter_t *self, const zt_ubh168_frame_t *src, size_t n_frames,
                       zt_truth_state_t truth, void *dc_dst, size_t max_len)
{
    uint8_t *dst = (uint8_t *) dc_dst;
    uint32_t len, frames, sum;
    if (!self || !src) return ZT_ADAPTER_EARG;
    int e = zt_adapter_read_header(&src[0], self->kind, n_frames, &len, &frames, &sum);
    if (e) return e;
    uint32_t n = len / 4u;
    if (len % 4u || frames != (n + 2u) / 3u) return ZT_ADAPTER_EFORMAT;
    if (truth == ZT_TRUTH_UNKNOWN) return 0;
    if (!dst || len > max_len) return ZT_ADAPTER_ESPACE;
    uint32_t h = 2166136261u;
    for (uint32_t f = 0; f < frames; f++) {
        zt_unpacked_rails_t r;
        zt_wire_rails_init(&r);
        zt_wire_unpack_ubh168(&src[f + 1u], &r);
        if (r.sync) return ZT_ADAPTER_EFORMAT;
        const int32_t side[3] = {lo16(r.s_minus[0]), hi16(r.s_minus[0]), lo16(r.s_minus[1])};
        for (uint32_t k = 0; k < 3u; k++) {
            uint32_t i = 3u * f + k;
            if (i >= n) break;
            uint8_t b[4];
            zt_st_le32(b, r.s_plus[k]);
            h = zt_adapter_fnv1a(b, 4, h);
            if (side_of(r.s_plus[k]) != side[k]) return ZT_ADAPTER_EFORMAT; /* witness */
            uint32_t out = r.s_plus[k];
            if (truth == ZT_TRUTH_FALSE) out = 0;                         /* retracted: silence */
            if (zt_truth_is_held(truth)) out = pack16(side[k], -side[k]); /* S0 side channel */
            zt_st_le32(dst + 4u * i, out);
        }
    }
    if (h != sum) return ZT_ADAPTER_EFORMAT;
    return (int) len;
}

/* U3: energy of 3 stereo samples, and of a + b. */
static zt_truth_state_t audio_evaluate(zt_adapter_t *self, const zt_ubh168_frame_t *a,
                                       const zt_ubh168_frame_t *b)
{
    if (!self || !a || !b) return ZT_TRUTH_UNKNOWN;
    if (zt_adapter_is_claim(a) || zt_adapter_is_claim(b))
        return zt_adapter_evaluate_default(self, a, b);
    zt_unpacked_rails_t ra, rb;
    zt_wire_rails_init(&ra);
    zt_wire_rails_init(&rb);
    zt_wire_unpack_ubh168(a, &ra);
    zt_wire_unpack_ubh168(b, &rb);
    int64_t ea = 0, eb = 0, es = 0;
    for (uint32_t k = 0; k < 3u; k++) {
        int64_t al = lo16(ra.s_plus[k]), ar = hi16(ra.s_plus[k]);
        int64_t bl = lo16(rb.s_plus[k]), br = hi16(rb.s_plus[k]);
        ea += al * al + ar * ar;
        eb += bl * bl + br * br;
        es += (al + bl) * (al + bl) + (ar + br) * (ar + br);
    }
    zt_truth_state_t s;
    if (ea == 0 && eb == 0)
        s = ZT_TRUTH_UNKNOWN;
    else if (ea && eb && es * 8 < ea + eb)
        s = ZT_TRUTH_GLUT; /* they would cancel: hold both */
    else
        s = ZT_TRUTH_TRUE;
    return zt_wire_persist(&self->conflict_run, s);
}

int16_t zt_audio_soft_fold(int32_t x, uint32_t *folds)
{
    int64_t v = x;
    bool folded = false;
    for (int i = 0; i < 8 && (v > 32767 || v < -32768); i++) {
        folded = true;
        if (v > 32767)
            v = 32767 - (((v - 32767) * 40503) >> 16); /* reflect, excess / phi */
        else
            v = -32768 + (((-32768 - v) * 40503) >> 16);
    }
    if (v > 32767) v = 32767; /* only after 8 reflections of a huge input */
    if (v < -32768) v = -32768;
    if (folded && folds) (*folds)++;
    return (int16_t) v;
}

zt_truth_state_t zt_audio_mix(const int16_t *a, const int16_t *b, size_t n, int16_t *out,
                              uint32_t *folds)
{
    int64_t ea = 0, eb = 0, es = 0;
    for (size_t i = 0; i < 2u * n; i++) {
        int64_t x = a[i], y = b[i];
        ea += x * x;
        eb += y * y;
        es += (x + y) * (x + y);
    }
    if (ea && eb && es * 8 < ea + eb) {
        for (size_t i = 0; i < n; i++) {
            out[2 * i] = (int16_t) (((int32_t) a[2 * i] + a[2 * i + 1]) >> 1);
            out[2 * i + 1] = (int16_t) (((int32_t) b[2 * i] + b[2 * i + 1]) >> 1);
        }
        return ZT_TRUTH_GLUT;
    }
    for (size_t i = 0; i < 2u * n; i++) out[i] = zt_audio_soft_fold((int32_t) a[i] + b[i], folds);
    return ZT_TRUTH_TRUE;
}

void zt_adapter_audio_init(zt_adapter_t *a, zt_audio_adapter_state_t *st, uint8_t trunk)
{
    a->name = "audio";
    a->state = st;
    a->kind = ZT_ADAPTER_KIND_AUDIO;
    a->trunk = (uint8_t) (trunk % 10u);
    a->conflict_run = 0;
    a->threshold_q16 = ZT_ADAPTER_THRESHOLD;
    a->pump_in = audio_pump;
    a->drain_out = audio_drain;
    a->evaluate_interference = audio_evaluate;
    if (st) st->folds = 0;
}
