/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_signal_data.h — the "unified signal-data computation" names, as a thin
 * facade. There is no second implementation: every type below is a typedef of
 * the wire or trunk-bank type, and every function forwards.
 *
 *   zt_frame_t                 = zt_ubh168_wire_frame_t (21 octets)
 *   zt_paraconsistent_state_t  = zt_truth_state_t (same values: UNKNOWN 0,
 *                                TRUE 1, FALSE 2, NEUTRAL 3, GLUT 4, PARADOX 5)
 *   zt_dual_rail_t             = zt_unpacked_rails_t (s_plus, s_minus,
 *                                trunk_id, is_axis_trunk, state, plus the
 *                                conflict_run counter)
 *   zt_trunk_status_t          = zt_line_status_t
 *   zt_filter_bank_t           = zt_trunk_demux_bank_t (field "status" is a
 *                                union alias of "line_state")
 *
 * PARADOX RULE.  zt_core_resolve_interference is stateless and never returns
 * PARADOX (its rail is const). zt_core_resolve_step keeps the per-rail
 * counter conflict_run: a GLUT on ZT_WIRE_PARADOX_RUN (3) consecutive
 * evaluations of the same rail (one per shell traversal) becomes PARADOX;
 * any other result resets it. Initialise a rail with zt_wire_rails_init (or
 * zero it) before the first step; zt_core_unpack_frame keeps the counter.
 *
 * WINDOW.  ZT_WINDOW_SAMPLES (160, 20 ms) is the reporting cadence. The bank
 * analyses 320-sample Blackman-Harris blocks (zt_trunk_bank.h B2), because a
 * 160-sample rectangular block cannot keep 444/777 Hz below the floor next to
 * full-scale 555/666 Hz; states therefore update every second report.
 *
 * OFF_HOOK.  A steady tone, however short, reads ON_HOOK; OFF_HOOK means the
 * carrier's phase moved between blocks (a pi/4-DQPSK data burst, B4/B5).
 */
#ifndef ZT_SIGNAL_DATA_H
#define ZT_SIGNAL_DATA_H

#include "zt_trunk_bank.h"

#ifndef ZT_FRAME_OCTETS /* tensor/zt.h defines the same value */
#    define ZT_FRAME_OCTETS 21u
#endif
#define ZT_TRUNK_COUNT    10u
#define ZT_HARMONIC_CYCLE 27720u /* swarm_harmonic fundamental, ticks */
#define ZT_SAMPLE_RATE_HZ 8000u
#define ZT_WINDOW_SAMPLES 160u /* reporting cadence; analysis block 320 */

typedef zt_ubh168_wire_frame_t zt_frame_t;
typedef zt_truth_state_t zt_paraconsistent_state_t;
#define ZT_STATE_UNKNOWN ZT_TRUTH_UNKNOWN
#define ZT_STATE_TRUE    ZT_TRUTH_TRUE
#define ZT_STATE_FALSE   ZT_TRUTH_FALSE
#define ZT_STATE_NEUTRAL ZT_TRUTH_NEUTRAL
#define ZT_STATE_GLUT    ZT_TRUTH_GLUT
#define ZT_STATE_PARADOX ZT_TRUTH_PARADOX

typedef zt_unpacked_rails_t zt_dual_rail_t;

typedef zt_line_status_t zt_trunk_status_t;
#define ZT_TRUNK_DEAD     ZT_LINE_DEAD
#define ZT_TRUNK_ON_HOOK  ZT_LINE_ON_HOOK
#define ZT_TRUNK_OFF_HOOK ZT_LINE_OFF_HOOK

typedef zt_trunk_demux_bank_t zt_filter_bank_t;

_Static_assert(sizeof(zt_frame_t) == ZT_FRAME_OCTETS, "Frame must be strictly 21 octets");
_Static_assert(ZT_TRUNK_COUNT == ZT_NUM_TRUNKS, "ten trunks");

void zt_core_init_bank(zt_filter_bank_t *bank);
/* tag = trunk_id % 10 */
void zt_core_pack_frame(uint8_t trunk_id, const uint32_t s_plus[3], const uint32_t s_minus[2],
                        zt_frame_t *dst_frame);
void zt_core_unpack_frame(const zt_frame_t *src_frame, zt_dual_rail_t *dst_rail);
zt_paraconsistent_state_t zt_core_resolve_interference(const zt_dual_rail_t *rail,
                                                       int32_t threshold_q16);
zt_paraconsistent_state_t zt_core_resolve_step(zt_dual_rail_t *rail, int32_t threshold_q16);
void zt_core_demux_stream(zt_filter_bank_t *bank, const int16_t *pcm_in, size_t sample_count);

#endif /* ZT_SIGNAL_DATA_H */
