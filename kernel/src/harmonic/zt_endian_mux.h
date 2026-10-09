/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_endian_mux.h — orthogonal I/Q multiplexing over the canonical frame.
 *
 * Thin functions over zt_harmonic_wire.h: the frame, the rails and the truth
 * enum are the wire's own (the spec declared them a second time; here they
 * are one). The I lane is the S+ rail (W0, W2, W4, little-endian), the Q lane
 * the S- rail (W1, W3, big-endian).
 *
 *   M1  zt_mux_pack_frame / zt_mux_unpack_frame are zt_wire_pack_tagged and
 *       zt_wire_unpack_ubh168; the carrier tag is the full octet-0 tag.
 *   M2  PROJECTION.  For I = (i0, i1) and Q = (q0, q1) in Q16.16:
 *         real_axis = I . Q        (the W5 dot product: the parallel part)
 *         imag_axis = i0*q1 - i1*q0 (the 2-D wedge: the perpendicular part)
 *       both saturated to int32 Q16.16, and truth is the W5 resolver at the
 *       bundle's threshold. "Hyperdimensional" in the spec means no more than
 *       these two numbers.
 *   M3  CHANNEL MASK.  IN_PHASE if any I word is non-zero, QUADRATURE if any
 *       Q word is, CONJUGATE if both are and q_k = -i_k for k = 0, 1.
 */
#ifndef ZT_ENDIAN_MUX_H
#define ZT_ENDIAN_MUX_H

#include "zt_harmonic_wire.h"

#define ZT_MUX_CHAN_IN_PHASE   0x01u /* little-endian S+ lane carries data */
#define ZT_MUX_CHAN_QUADRATURE 0x02u /* big-endian S- lane carries data */
#define ZT_MUX_CHAN_CONJUGATE  0x04u /* Q = -I on the paired words */

#define ZT_MUX_THRESHOLD_Q16 0x00001000 /* default |dot| threshold, 1/16 */

typedef zt_unpacked_rails_t zt_demux_bundle_t; /* i_channel, q_channel, carrier_tag */

typedef struct {
    int32_t real_axis; /* M2, Q16.16 */
    int32_t imag_axis; /* M2, Q16.16 */
    zt_truth_state_t truth;
} zt_hd_projection_t;

/* M1. Return 0, or -1 for a NULL pointer. */
int zt_mux_pack_frame(uint8_t carrier_tag, const uint32_t i_lane[3], const uint32_t q_lane[2],
                      zt_ubh168_frame_t *dst_frame);
int zt_mux_unpack_frame(const zt_ubh168_frame_t *src_frame, zt_demux_bundle_t *dst_bundle);

/* M2 at ZT_MUX_THRESHOLD_Q16, and at an explicit threshold. */
zt_hd_projection_t zt_mux_resolve_perpendicularity(const zt_demux_bundle_t *bundle);
zt_hd_projection_t zt_mux_project(const zt_demux_bundle_t *bundle, int32_t threshold_q16);

/* M3. */
uint8_t zt_mux_channel_mask(const zt_demux_bundle_t *bundle);

#endif /* ZT_ENDIAN_MUX_H */
