/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_adapter_core.c — stream framing, claims and the plain byte adapter. See
 * zt_adapter_core.h. */
#include "zt_adapter_core.h"

uint32_t zt_ld_le32(const uint8_t *p)
{
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

void zt_st_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
}

uint32_t zt_adapter_fnv1a(const uint8_t *p, size_t n, uint32_t h)
{
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

size_t zt_adapter_frames_for(size_t len)
{
    return 1u + (len + ZT_ADAPTER_PAYLOAD - 1u) / ZT_ADAPTER_PAYLOAD;
}

void zt_adapter_header(zt_adapter_kind_t kind, uint8_t trunk, uint32_t length, uint32_t frames,
                       uint32_t checksum, zt_ubh168_frame_t *dst)
{
    const uint32_t sp[3] = {length, ZT_ADAPTER_KIND_WORD(kind), frames};
    const uint32_t sm[2] = {checksum, ~length};
    zt_wire_pack_tagged(zt_wire_make_tag(trunk, 0, true), sp, sm, dst);
}

int zt_adapter_read_header(const zt_ubh168_frame_t *f, zt_adapter_kind_t kind, size_t n_frames,
                           uint32_t *length, uint32_t *frames, uint32_t *checksum)
{
    zt_unpacked_rails_t r;
    if (!f || n_frames < 1u) return ZT_ADAPTER_EARG;
    zt_wire_rails_init(&r);
    zt_wire_unpack_ubh168(f, &r);
    if (!r.sync || r.shell != 0 || r.s_plus[1] != ZT_ADAPTER_KIND_WORD(kind) ||
        r.s_minus[1] != ~r.s_plus[0])
        return ZT_ADAPTER_EFORMAT;
    if ((size_t) r.s_plus[2] > n_frames - 1u) return ZT_ADAPTER_EFORMAT;
    *length = r.s_plus[0];
    *frames = r.s_plus[2];
    *checksum = r.s_minus[0];
    return 0;
}

uint8_t zt_adapter_data_tag(uint8_t trunk, uint32_t index)
{
    return zt_wire_make_tag(trunk, (uint8_t) (index % 10u), false);
}

/* ===== A4: claims ===== */

void zt_adapter_claim_frame(uint8_t trunk, uint32_t proposition, int32_t evidence_q16,
                            zt_ubh168_frame_t *dst)
{
    const uint32_t sp[3] = {(uint32_t) evidence_q16, proposition, ZT_ADAPTER_CLAIM_MAGIC};
    const uint32_t sm[2] = {(uint32_t) evidence_q16, proposition};
    zt_wire_pack_tagged(zt_wire_make_tag(trunk, 9, true), sp, sm, dst);
}

bool zt_adapter_is_claim(const zt_ubh168_frame_t *f)
{
    zt_unpacked_rails_t r;
    zt_wire_rails_init(&r);
    zt_wire_unpack_ubh168(f, &r);
    return r.sync && r.shell == 9 && r.s_plus[2] == ZT_ADAPTER_CLAIM_MAGIC &&
           r.s_minus[0] == r.s_plus[0] && r.s_minus[1] == r.s_plus[1];
}

zt_truth_state_t zt_adapter_evaluate_default(zt_adapter_t *self, const zt_ubh168_frame_t *a,
                                             const zt_ubh168_frame_t *b)
{
    if (!self || !a || !b) return ZT_TRUTH_UNKNOWN;
    zt_unpacked_rails_t ra, rb, x;
    zt_wire_rails_init(&ra);
    zt_wire_rails_init(&rb);
    zt_wire_unpack_ubh168(a, &ra);
    zt_wire_unpack_ubh168(b, &rb);
    zt_truth_state_t s;
    if (zt_adapter_is_claim(a) && zt_adapter_is_claim(b)) {
        if (ra.s_plus[1] != rb.s_plus[1]) /* different propositions: no dialectic */
            s = ZT_TRUTH_UNKNOWN;
        else
            s = zt_wire_resolve_evidence((int32_t) ra.s_plus[0], (int32_t) rb.s_plus[0],
                                         self->threshold_q16);
    } else {
        zt_wire_rails_init(&x);
        x.s_plus[0] = ra.s_plus[0];
        x.s_plus[1] = ra.s_plus[1];
        x.s_plus[2] = ra.s_plus[2];
        x.s_minus[0] = rb.s_plus[0];
        x.s_minus[1] = rb.s_plus[1];
        s = zt_wire_resolve_rails(&x, self->threshold_q16);
    }
    return zt_wire_persist(&self->conflict_run, s);
}

/* ===== A2: the plain byte adapter ===== */

static int bytes_pump(zt_adapter_t *self, const void *dc_src, size_t len, zt_ubh168_frame_t *dst,
                      size_t max_frames)
{
    const uint8_t *src = (const uint8_t *) dc_src;
    if (!self || (!src && len) || !dst || len > 0x7FFFFFF0u) return ZT_ADAPTER_EARG;
    size_t need = zt_adapter_frames_for(len);
    if (need > max_frames || need > 0x7FFFFFFFu) return ZT_ADAPTER_ESPACE;
    zt_adapter_header(self->kind, self->trunk, (uint32_t) len, (uint32_t) (need - 1u),
                      zt_adapter_fnv1a(src, len, 2166136261u), &dst[0]);
    for (size_t f = 0; f + 1u < need; f++) {
        uint8_t blk[ZT_ADAPTER_PAYLOAD];
        size_t off = f * ZT_ADAPTER_PAYLOAD;
        for (size_t k = 0; k < ZT_ADAPTER_PAYLOAD; k++) blk[k] = off + k < len ? src[off + k] : 0;
        const uint32_t sp[3] = {zt_ld_le32(blk), zt_ld_le32(blk + 8), zt_ld_le32(blk + 16)};
        const uint32_t sm[2] = {zt_ld_le32(blk + 4), zt_ld_le32(blk + 12)};
        zt_wire_pack_tagged(zt_adapter_data_tag(self->trunk, (uint32_t) f), sp, sm, &dst[f + 1u]);
    }
    return (int) need;
}

static int bytes_drain(zt_adapter_t *self, const zt_ubh168_frame_t *src, size_t n_frames,
                       zt_truth_state_t truth, void *dc_dst, size_t max_len)
{
    uint8_t *dst = (uint8_t *) dc_dst;
    uint32_t len, frames, sum;
    if (!self || !src) return ZT_ADAPTER_EARG;
    int e = zt_adapter_read_header(&src[0], self->kind, n_frames, &len, &frames, &sum);
    if (e) return e;
    if ((size_t) frames != (len + ZT_ADAPTER_PAYLOAD - 1u) / ZT_ADAPTER_PAYLOAD)
        return ZT_ADAPTER_EFORMAT;
    if (truth == ZT_TRUTH_FALSE || truth == ZT_TRUTH_UNKNOWN) return 0;
    if (zt_truth_is_held(truth)) return ZT_ADAPTER_EHELD;
    if (len > max_len || (!dst && len)) return ZT_ADAPTER_ESPACE;
    uint32_t h = 2166136261u;
    for (uint32_t f = 0; f < frames; f++) {
        zt_unpacked_rails_t r;
        uint8_t blk[ZT_ADAPTER_PAYLOAD];
        zt_wire_rails_init(&r);
        zt_wire_unpack_ubh168(&src[f + 1u], &r);
        if (r.sync || r.shell != f % 10u) return ZT_ADAPTER_EFORMAT;
        zt_st_le32(blk, r.s_plus[0]);
        zt_st_le32(blk + 4, r.s_minus[0]);
        zt_st_le32(blk + 8, r.s_plus[1]);
        zt_st_le32(blk + 12, r.s_minus[1]);
        zt_st_le32(blk + 16, r.s_plus[2]);
        uint32_t off = f * ZT_ADAPTER_PAYLOAD;
        uint32_t n = len - off < ZT_ADAPTER_PAYLOAD ? len - off : ZT_ADAPTER_PAYLOAD;
        h = zt_adapter_fnv1a(blk, n, h);
        for (uint32_t k = 0; k < n; k++) dst[off + k] = blk[k];
    }
    if (h != sum) return ZT_ADAPTER_EFORMAT;
    return (int) len;
}

void zt_adapter_bytes_init(zt_adapter_t *a, uint8_t trunk)
{
    a->name = "bytes";
    a->state = 0;
    a->kind = ZT_ADAPTER_KIND_BYTES;
    a->trunk = (uint8_t) (trunk % 10u);
    a->conflict_run = 0;
    a->threshold_q16 = ZT_ADAPTER_THRESHOLD;
    a->pump_in = bytes_pump;
    a->drain_out = bytes_drain;
    a->evaluate_interference = zt_adapter_evaluate_default;
}
