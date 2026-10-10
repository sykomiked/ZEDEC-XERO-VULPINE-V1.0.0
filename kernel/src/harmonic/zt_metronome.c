/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_metronome.c — see zt_metronome.h. */
#include "zt_metronome.h"
#include "../swarm/swarm_harmonic.h"
#include "../swarm/swarm_budget.h" /* swarm_muldiv: 64-bit quotient without libgcc */

_Static_assert(ZT_METRONOME_CYCLE == SWARM_FUNDAMENTAL_TICKS, "one clock");

void zt_metronome_frame(uint64_t tick, zt_ubh168_frame_t *dst_frame)
{
    uint64_t rem;
    uint64_t fund = swarm_muldiv(tick, 1, ZT_METRONOME_CYCLE, &rem);
    uint32_t phase = (uint32_t) rem;
    uint32_t due = swarm_harmonics_due(tick);
    const uint32_t sp[3] = {phase, due, (uint32_t) fund};
    const uint32_t sm[2] = {phase, due};
    zt_wire_pack_tagged(zt_wire_make_tag(ZT_METRONOME_TRUNK, 0, phase == 0), sp, sm, dst_frame);
}

bool zt_metronome_read(const zt_ubh168_frame_t *frame, uint64_t *tick, uint32_t *due_mask)
{
    zt_unpacked_rails_t r;
    zt_wire_rails_init(&r);
    zt_wire_unpack_ubh168(frame, &r);
    uint32_t phase = r.s_plus[0], due = r.s_plus[1];
    if (r.trunk_id != ZT_METRONOME_TRUNK || phase >= ZT_METRONOME_CYCLE) return false;
    if (r.s_minus[0] != phase || r.s_minus[1] != due || r.sync != (phase == 0)) return false;
    if (due != swarm_harmonics_due(phase)) return false; /* due depends on the phase only */
    if (tick) *tick = (uint64_t) r.s_plus[2] * ZT_METRONOME_CYCLE + phase;
    if (due_mask) *due_mask = due;
    return true;
}
