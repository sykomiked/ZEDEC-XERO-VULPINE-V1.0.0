/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_metronome.c — swarm_harmonic ticks on the 555 Hz pulse trunk. */
#include "zt_htest.h"
#include "zt_metronome.h"
#include "zt_trunk_bank.h"
#include "../swarm/swarm_harmonic.h"

static int16_t pcm[ZT_TRUNK_FRAME_SAMPLES];

int main(void)
{
    const uint64_t ticks[6] = {0, 1, 13860, 27719, 27720, 5ull * 27720 + 2520};
    bool ok = true;
    for (int i = 0; i < 6; i++) {
        zt_ubh168_frame_t f;
        uint64_t t;
        uint32_t due;
        zt_metronome_frame(ticks[i], &f);
        ok = ok && zt_metronome_read(&f, &t, &due) && t == ticks[i] &&
             due == swarm_harmonics_due(ticks[i]);
    }
    HT_CHECK(ok, "metronome frames round trip tick and due mask");
    zt_ubh168_frame_t f, g;
    zt_metronome_frame(27720, &f);
    uint64_t t;
    uint32_t due;
    HT_CHECK(zt_metronome_read(&f, &t, &due) && due == 0x7FFu && f.tag == 104,
             "fundamental boundary: all 11 harmonics due, sync tag 104");
    zt_metronome_frame(13860, &f); /* half cycle: harmonics 2,4,6,8,10 */
    zt_metronome_read(&f, 0, &due);
    HT_CHECK(due == (1u << 1 | 1u << 3 | 1u << 5 | 1u << 7 | 1u << 9),
             "half cycle: even harmonics");
    zt_unpacked_rails_t r;
    zt_wire_rails_init(&r);
    zt_wire_unpack_ubh168(&f, &r);
    HT_CHECK(zt_wire_resolve_rails(&r, 0x100) == ZT_TRUTH_TRUE, "witness copy resolves TRUE");
    /* tamper: a flipped witness bit is rejected */
    uint8_t o[21];
    zt_wire_to_octets(&f, o);
    o[8] ^= 1u;
    zt_wire_from_octets(o, &g);
    HT_CHECK(!zt_metronome_read(&g, 0, 0), "tampered witness rejected");
    /* over the air on trunk 4 */
    zt_trunk_mux_bank_t mux;
    zt_trunk_demux_bank_t bank;
    zt_trunk_mux_init(&mux);
    zt_trunk_bank_init(&bank);
    for (size_t i = 0; i < ZT_TRUNK_FRAME_SAMPLES; i++) pcm[i] = 0;
    zt_metronome_frame(3ull * 27720 + 9240, &f);
    zt_trunk_mux_transmit_frame(&mux, ZT_TRK_PULSE, &f, pcm, ZT_TRUNK_FRAME_SAMPLES);
    HT_CHECK(zt_trunk_read_frame(&bank, ZT_TRK_PULSE, pcm, ZT_TRUNK_FRAME_SAMPLES, &g) &&
                 zt_metronome_read(&g, &t, &due) && t == 3ull * 27720 + 9240,
             "metronome frame survives the 555 Hz trunk");
    return ht_finish("test_metronome");
}
