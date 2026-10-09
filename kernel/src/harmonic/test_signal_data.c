/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_signal_data.c — the signal-data spec's T1-T4 through the facade.
 * T3's "no heap allocation" is proven by the build: verify-all links the
 * module freestanding (no libc) and checks `nm -u` lists no malloc, and the
 * ZT_HTEST_BARE build of this test links with no C library at all. */
#include "zt_htest.h"
#include "zt_signal_data.h"

static int16_t pcm[ZT_TRUNK_FRAME_SAMPLES + 2u * ZT_TRUNK_BLOCK];

static uint32_t xorshift(uint32_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 17;
    *s ^= *s << 5;
    return *s;
}

static void t1(void)
{
    zt_frame_t arr[2];
    HT_CHECK(sizeof(zt_frame_t) == 21u, "T1 sizeof(zt_frame_t) == 21");
    HT_CHECK((uintptr_t) &arr[1] - (uintptr_t) &arr[0] == 21u, "T1 stride 21");
}

static void t2(void)
{
    uint32_t seed = 0xC0FFEE11u;
    bool rt = true, rev = true;
    for (int n = 0; n < 10000; n++) {
        uint32_t sp[3] = {xorshift(&seed), xorshift(&seed), xorshift(&seed)};
        uint32_t sm[2] = {xorshift(&seed), xorshift(&seed)};
        uint8_t trunk = (uint8_t) (xorshift(&seed) % 10u);
        zt_frame_t f;
        zt_dual_rail_t r;
        uint8_t o[21];
        zt_core_pack_frame(trunk, sp, sm, &f);
        zt_wire_rails_init(&r);
        zt_core_unpack_frame(&f, &r);
        rt = rt && r.s_plus[0] == sp[0] && r.s_plus[1] == sp[1] && r.s_plus[2] == sp[2] &&
             r.s_minus[0] == sm[0] && r.s_minus[1] == sm[1] && r.trunk_id == trunk &&
             r.is_axis_trunk == (trunk < 9);
        zt_wire_to_octets(&f, o);
        for (int k = 0; k < 4; k++) {
            rev = rev && o[5 + k] == (uint8_t) (sm[0] >> (24 - 8 * k));  /* W1 big-endian */
            rev = rev && o[13 + k] == (uint8_t) (sm[1] >> (24 - 8 * k)); /* W3 big-endian */
            rev = rev && o[1 + k] == (uint8_t) (sp[0] >> (8 * k));       /* W0 little */
        }
    }
    HT_CHECK(rt, "T2 10000 pseudorandom frames: zero bit errors");
    HT_CHECK(rev, "T2 W1 and W3 stored byte-reversed relative to W0");
}

static void t3(void)
{
    const uint32_t sp[3] = {0x00030000u, 0x00050000u, 0x00070000u};
    const uint32_t sm[2] = {0u - 0x00030000u, 0u - 0x00050000u};
    zt_frame_t f;
    zt_dual_rail_t r;
    zt_core_pack_frame(7, sp, sm, &f);
    zt_wire_rails_init(&r);
    zt_core_unpack_frame(&f, &r);
    zt_paraconsistent_state_t s = zt_core_resolve_interference(&r, 0x100);
    HT_CHECK(s == ZT_STATE_GLUT || s == ZT_STATE_PARADOX, "T3 S+ = -S- is GLUT or PARADOX");
    zt_paraconsistent_state_t a = zt_core_resolve_step(&r, 0x100);
    zt_core_unpack_frame(&f, &r); /* next shell traversal: counter kept */
    zt_paraconsistent_state_t b = zt_core_resolve_step(&r, 0x100);
    zt_core_unpack_frame(&f, &r);
    zt_paraconsistent_state_t c = zt_core_resolve_step(&r, 0x100);
    HT_CHECK(a == ZT_STATE_GLUT && b == ZT_STATE_GLUT && c == ZT_STATE_PARADOX &&
                 r.state == ZT_STATE_PARADOX,
             "T3 conflict persisting over 3 traversals is PARADOX");
    HT_CHECK(ZT_STATE_PARADOX == 5 && ZT_STATE_GLUT == 4 && ZT_STATE_UNKNOWN == 0,
             "T3 state values as specified");
}

static void t4(void)
{
    zt_trunk_mux_bank_t mux;
    zt_filter_bank_t bank;
    zt_trunk_mux_init(&mux);
    zt_core_init_bank(&bank);
    for (size_t i = 0; i < sizeof pcm / sizeof pcm[0]; i++) pcm[i] = 0;
    const uint32_t sp[3] = {0x55555555u, 0xAAAAAAAAu, 0x0F0F0F0Fu}, sm[2] = {1u, 2u};
    zt_frame_t f;
    zt_core_pack_frame(4, sp, sm, &f);
    mux.amplitude[4] = mux.amplitude[9] = 12000;
    /* two steady blocks, then the phase-modulated bursts */
    zt_trunk_mux_tone(&mux, ZT_TRK_PULSE, pcm, 2u * ZT_TRUNK_BLOCK);
    zt_trunk_mux_tone(&mux, ZT_TRK_FLEET, pcm, 2u * ZT_TRUNK_BLOCK);
    zt_trunk_mux_transmit_frame(&mux, ZT_TRK_PULSE, &f, pcm + 2u * ZT_TRUNK_BLOCK,
                                ZT_TRUNK_FRAME_SAMPLES);
    const uint32_t sp9[3] = {0xDEADBEEFu, 2u, 3u};
    zt_core_pack_frame(9, sp9, sm, &f);
    zt_trunk_mux_transmit_frame(&mux, ZT_TRK_FLEET, &f, pcm + 2u * ZT_TRUNK_BLOCK,
                                ZT_TRUNK_FRAME_SAMPLES);
    /* feed in 160-sample reports, the spec's cadence */
    size_t total = sizeof pcm / sizeof pcm[0];
    bool seen_on = false;
    for (size_t at = 0; at < total; at += ZT_WINDOW_SAMPLES) {
        zt_core_demux_stream(&bank, pcm + at, ZT_WINDOW_SAMPLES);
        if (at == 2u * ZT_TRUNK_BLOCK - ZT_WINDOW_SAMPLES)
            seen_on = bank.status[4] == ZT_TRUNK_ON_HOOK && bank.status[9] == ZT_TRUNK_ON_HOOK;
    }
    HT_CHECK(seen_on, "T4 steady 555 and 1111 Hz before the burst: ON_HOOK");
    HT_CHECK(bank.status[4] == ZT_TRUNK_OFF_HOOK && bank.status[9] == ZT_TRUNK_OFF_HOOK,
             "T4 555 and 1111 Hz bursts: trunks 4 and 9 OFF_HOOK");
    bool quiet = true;
    for (uint32_t t = 0; t < 10; t++)
        if (t != 4 && t != 9)
            quiet = quiet && bank.offhook_blocks[t] == 0 &&
                    (bank.status[t] == ZT_TRUNK_DEAD || bank.status[t] == ZT_TRUNK_ON_HOOK);
    HT_CHECK(quiet, "T4 trunks 0-3 and 5-8 stay DEAD or ON_HOOK throughout");
    HT_CHECK(bank.offhook_blocks[4] == ZT_TRUNK_FRAME_SYMBOLS &&
                 bank.offhook_blocks[9] == ZT_TRUNK_FRAME_SYMBOLS,
             "T4 every symbol block of both bursts reads OFF_HOOK");
}

int main(void)
{
    t1();
    t2();
    t3();
    t4();
    return ht_finish("test_signal_data");
}
