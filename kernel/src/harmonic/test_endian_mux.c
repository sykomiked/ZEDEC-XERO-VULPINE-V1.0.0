/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_endian_mux.c — the endian-mux spec's four tests over the wire frame. */
#include "zt_htest.h"
#include "zt_endian_mux.h"

static uint32_t lcg(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s;
}

/* Test 1: bit-exact round trip (many pseudorandom lanes). */
static void t1_round_trip(void)
{
    uint32_t seed = 0x2468ACE1u;
    bool ok = true;
    for (int n = 0; n < 4096; n++) {
        uint32_t i[3] = {lcg(&seed), lcg(&seed), lcg(&seed)}, q[2] = {lcg(&seed), lcg(&seed)};
        uint8_t tag = (uint8_t) lcg(&seed);
        zt_ubh168_frame_t f;
        zt_demux_bundle_t b;
        ok = ok && zt_mux_pack_frame(tag, i, q, &f) == 0 && zt_mux_unpack_frame(&f, &b) == 0;
        ok = ok && b.i_channel[0] == i[0] && b.i_channel[1] == i[1] && b.i_channel[2] == i[2];
        ok = ok && b.q_channel[0] == q[0] && b.q_channel[1] == q[1] && b.carrier_tag == tag;
    }
    HT_CHECK(ok, "T1 4096 random frames round trip bit-exact");
    HT_CHECK(zt_mux_pack_frame(0, 0, 0, 0) == -1, "T1 NULL refused");
}

/* Test 2: word 0 LE, word 1 reversed (BE), word 2 LE again. */
static void t2_raw_endian(void)
{
    const uint32_t i[3] = {0x0A0B0C0Du, 0x01020304u, 0xF1F2F3F4u}, q[2] = {0x0A0B0C0Du, 0x1u};
    zt_ubh168_frame_t f;
    uint8_t o[21];
    zt_mux_pack_frame(0x21, i, q, &f);
    zt_wire_to_octets(&f, o);
    HT_CHECK(o[0] == 0x21, "T2 tag octet");
    HT_CHECK(o[1] == 0x0D && o[2] == 0x0C && o[3] == 0x0B && o[4] == 0x0A, "T2 W0 little-endian");
    HT_CHECK(o[5] == 0x0A && o[6] == 0x0B && o[7] == 0x0C && o[8] == 0x0D, "T2 W1 big-endian");
    HT_CHECK(o[9] == 0x04 && o[10] == 0x03 && o[11] == 0x02 && o[12] == 0x01, "T2 W2 LE again");
    HT_CHECK(o[13] == 0 && o[16] == 1 && o[17] == 0xF4 && o[20] == 0xF1, "T2 W3 BE, W4 LE");
}

/* Test 3: active I lane, zero Q lane: the Q decoder reads silence. */
static void t3_isolation(void)
{
    uint32_t seed = 99u;
    bool ok = true;
    for (int n = 0; n < 1000; n++) {
        uint32_t i[3] = {lcg(&seed) | 1u, lcg(&seed), 0xFFFFFFFFu}, q[2] = {0, 0};
        zt_ubh168_frame_t f;
        zt_demux_bundle_t b;
        zt_mux_pack_frame(5, i, q, &f);
        zt_mux_unpack_frame(&f, &b);
        ok = ok && b.q_channel[0] == 0 && b.q_channel[1] == 0;
        ok = ok && zt_mux_channel_mask(&b) == ZT_MUX_CHAN_IN_PHASE;
        zt_hd_projection_t p = zt_mux_resolve_perpendicularity(&b);
        ok = ok && p.real_axis == 0 && p.imag_axis == 0 && p.truth == ZT_TRUTH_NEUTRAL;
    }
    HT_CHECK(ok, "T3 no I->Q leakage; Q silent reads NEUTRAL with zero projection");
    /* and the converse */
    uint32_t i0[3] = {0, 0, 0}, q1[2] = {0x10000u, 0x20000u};
    zt_ubh168_frame_t f;
    zt_demux_bundle_t b;
    zt_mux_pack_frame(5, i0, q1, &f);
    zt_mux_unpack_frame(&f, &b);
    HT_CHECK(b.i_channel[0] == 0 && b.i_channel[1] == 0 && b.i_channel[2] == 0 &&
                 zt_mux_channel_mask(&b) == ZT_MUX_CHAN_QUADRATURE,
             "T3 no Q->I leakage");
}

/* Test 4: identical, maximally conflicting claim on both lanes. */
static void t4_sump(void)
{
    uint32_t i[3] = {0x7FFFFFFFu, 0x7FFFFFFFu, 0x7FFFFFFFu};
    uint32_t q[2] = {0u - 0x7FFFFFFFu, 0u - 0x7FFFFFFFu};
    zt_ubh168_frame_t f;
    zt_demux_bundle_t b;
    zt_mux_pack_frame(7, i, q, &f);
    zt_mux_unpack_frame(&f, &b);
    zt_hd_projection_t p = zt_mux_resolve_perpendicularity(&b);
    HT_CHECK(p.truth == ZT_TRUTH_GLUT || p.truth == ZT_TRUTH_PARADOX, "T4 conflict held");
    HT_CHECK(p.real_axis == INT32_MIN, "T4 dot saturates, no overflow");
    HT_CHECK(zt_mux_channel_mask(&b) ==
                 (ZT_MUX_CHAN_IN_PHASE | ZT_MUX_CHAN_QUADRATURE | ZT_MUX_CHAN_CONJUGATE),
             "T4 conjugate lanes flagged");
    /* constructive and orthogonal cases of M2 */
    uint32_t i2[3] = {0x10000u, 0, 0}, q2[2] = {0x10000u, 0};
    zt_mux_pack_frame(7, i2, q2, &f);
    zt_mux_unpack_frame(&f, &b);
    p = zt_mux_resolve_perpendicularity(&b);
    HT_CHECK(p.truth == ZT_TRUTH_TRUE && p.real_axis == 0x10000 && p.imag_axis == 0, "parallel");
    q2[0] = 0;
    q2[1] = 0x10000u;
    zt_mux_pack_frame(7, i2, q2, &f);
    zt_mux_unpack_frame(&f, &b);
    p = zt_mux_resolve_perpendicularity(&b);
    HT_CHECK(p.truth == ZT_TRUTH_GLUT && p.real_axis == 0 && p.imag_axis == 0x10000,
             "perpendicular: wedge carries it, held as GLUT");
}

int main(void)
{
    t1_round_trip();
    t2_raw_endian();
    t3_isolation();
    t4_sump();
    return ht_finish("test_endian_mux");
}
