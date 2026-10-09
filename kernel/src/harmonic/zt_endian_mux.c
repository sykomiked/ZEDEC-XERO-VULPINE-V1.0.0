/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_endian_mux.c — I/Q lanes over the wire frame. See zt_endian_mux.h. */
#include "zt_endian_mux.h"

int zt_mux_pack_frame(uint8_t carrier_tag, const uint32_t i_lane[3], const uint32_t q_lane[2],
                      zt_ubh168_frame_t *dst_frame)
{
    if (!i_lane || !q_lane || !dst_frame) return -1;
    zt_wire_pack_tagged(carrier_tag, i_lane, q_lane, dst_frame);
    return 0;
}

int zt_mux_unpack_frame(const zt_ubh168_frame_t *src_frame, zt_demux_bundle_t *dst_bundle)
{
    if (!src_frame || !dst_bundle) return -1;
    zt_wire_unpack_ubh168(src_frame, dst_bundle);
    return 0;
}

static int32_t sat32(int64_t v)
{
    if (v > INT32_MAX) return INT32_MAX;
    if (v < INT32_MIN) return INT32_MIN;
    return (int32_t) v;
}

zt_hd_projection_t zt_mux_project(const zt_demux_bundle_t *b, int32_t threshold_q16)
{
    zt_hd_projection_t p;
    int64_t i0 = (int32_t) b->i_channel[0], i1 = (int32_t) b->i_channel[1];
    int64_t q0 = (int32_t) b->q_channel[0], q1 = (int32_t) b->q_channel[1];
    p.real_axis = zt_wire_correlation_q16(b);
    p.imag_axis = sat32(((i0 * q1) >> 16) - ((i1 * q0) >> 16));
    p.truth = zt_wire_resolve_rails(b, threshold_q16);
    return p;
}

zt_hd_projection_t zt_mux_resolve_perpendicularity(const zt_demux_bundle_t *bundle)
{
    return zt_mux_project(bundle, ZT_MUX_THRESHOLD_Q16);
}

uint8_t zt_mux_channel_mask(const zt_demux_bundle_t *b)
{
    uint8_t m = 0;
    bool i = (b->i_channel[0] | b->i_channel[1] | b->i_channel[2]) != 0u;
    bool q = (b->q_channel[0] | b->q_channel[1]) != 0u;
    if (i) m |= ZT_MUX_CHAN_IN_PHASE;
    if (q) m |= ZT_MUX_CHAN_QUADRATURE;
    if (i && q && b->q_channel[0] == 0u - b->i_channel[0] &&
        b->q_channel[1] == 0u - b->i_channel[1])
        m |= ZT_MUX_CHAN_CONJUGATE;
    return m;
}
