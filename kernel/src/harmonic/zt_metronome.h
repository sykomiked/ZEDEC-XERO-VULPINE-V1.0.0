/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_metronome.h — the swarm_harmonic clock carried on the 555 Hz pulse
 * trunk (trunk 4).
 *
 * A metronome frame states one tick of kernel/src/swarm/swarm_harmonic's
 * clock: 27,720 ticks per fundamental, harmonics 1..11 due when the tick is a
 * multiple of 27720 / n.
 *   tag  = 100 * sync + 4, sync set on the first tick of a fundamental
 *   W0   = tick mod 27720 (phase in the fundamental)        S+
 *   W2   = swarm_harmonics_due(tick), bit n-1 = harmonic n  S+
 *   W4   = tick / 27720, low 32 bits (fundamentals so far)  S+
 *   W1   = W0, W3 = W2: the S- witness repeats the claim, so a reader can
 *          check the frame (zt_metronome_read) and the W5 resolver reads it
 *          TRUE whenever the phase or mask is non-zero.
 * The trunk carries 50 bit/s (zt_trunk_bank.h B5), so one frame per 3.4 s:
 * it is a reference for drift checks, not a per-tick clock.
 */
#ifndef ZT_METRONOME_H
#define ZT_METRONOME_H

#include "zt_harmonic_wire.h"

#define ZT_METRONOME_TRUNK 4u /* 555 Hz */
#define ZT_METRONOME_CYCLE 27720u

void zt_metronome_frame(uint64_t tick, zt_ubh168_frame_t *dst_frame);
/* True if the frame is a well-formed metronome frame (trunk 4, witnesses
 * equal, mask consistent with the phase). Outputs may be NULL. */
bool zt_metronome_read(const zt_ubh168_frame_t *frame, uint64_t *tick, uint32_t *due_mask);

#endif /* ZT_METRONOME_H */
