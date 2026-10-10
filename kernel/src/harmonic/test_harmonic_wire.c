/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_harmonic_wire.c — the canonical frame: the user's four tests, the tag,
 * the idle pilot, both resolvers, persistence and the truth-enum maps. Every
 * byte expectation is written as octets, so the same test passes unchanged on
 * little- and big-endian hosts. */
#include "zt_htest.h"
#include "zt_harmonic_wire.h"
#include "zt_harmonic_logic.h"

static uint32_t rd_le(const uint8_t *p)
{
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}
static uint32_t rd_be(const uint8_t *p)
{
    return (uint32_t) p[3] | (uint32_t) p[2] << 8 | (uint32_t) p[1] << 16 | (uint32_t) p[0] << 24;
}

/* T1: size and stride. */
static void t1_size_stride(void)
{
    zt_ubh168_wire_frame_t arr[3];
    HT_CHECK(sizeof(zt_ubh168_wire_frame_t) == 21u, "T1 sizeof == 21");
    HT_CHECK((uintptr_t) &arr[1] - (uintptr_t) &arr[0] == 21u, "T1 stride 0->1 == 21");
    HT_CHECK((uintptr_t) &arr[2] - (uintptr_t) &arr[1] == 21u, "T1 stride 1->2 == 21");
    HT_CHECK(sizeof arr == 63u, "T1 array of 3 == 63");
}

/* T2: bit-exact pack with the user's vectors. */
static void t2_bit_exact(void)
{
    const uint32_t sp[3] = {0x11223344u, 0x55667788u, 0x99AABBCCu};
    const uint32_t sm[2] = {0xDDEEFF00u, 0x12345678u};
    static const uint8_t want[21] = {0x03, 0x44, 0x33, 0x22, 0x11, 0xDD, 0xEE,
                                     0xFF, 0x00, 0x88, 0x77, 0x66, 0x55, 0x12,
                                     0x34, 0x56, 0x78, 0xCC, 0xBB, 0xAA, 0x99};
    zt_ubh168_wire_frame_t f;
    uint8_t o[21];
    zt_wire_pack_ubh168(13, sp, sm, &f); /* 13 mod 10 = 3 */
    zt_wire_to_octets(&f, o);
    bool same = true;
    for (int i = 0; i < 21; i++) same = same && o[i] == want[i];
    HT_CHECK(same, "T2 raw octets match the L-B-L-B-L layout");
    /* W1 and W3 are stored byte-reversed relative to the LE words */
    HT_CHECK(rd_be(o + 5) == sm[0] && rd_le(o + 5) == __builtin_bswap32(sm[0]), "T2 W1 reversed");
    HT_CHECK(rd_be(o + 13) == sm[1] && rd_le(o + 13) == __builtin_bswap32(sm[1]), "T2 W3 reversed");
    HT_CHECK(rd_le(o + 1) == sp[0] && rd_le(o + 9) == sp[1] && rd_le(o + 17) == sp[2],
             "T2 W0 W2 W4 little-endian");
    zt_unpacked_rails_t r;
    zt_wire_rails_init(&r);
    zt_wire_unpack_ubh168(&f, &r);
    HT_CHECK(r.s_plus_lane[0] == sp[0] && r.s_plus_lane[1] == sp[1] && r.s_plus_lane[2] == sp[2],
             "T2 S+ round trip");
    HT_CHECK(r.s_minus_lane[0] == sm[0] && r.s_minus_lane[1] == sm[1], "T2 S- round trip");
    HT_CHECK(r.trunk_id == 3 && r.is_axis_trunk, "T2 trunk from tag");
    /* the same frame rebuilt from octets decodes identically */
    zt_ubh168_wire_frame_t g;
    zt_unpacked_rails_t r2;
    zt_wire_from_octets(want, &g);
    zt_wire_rails_init(&r2);
    zt_wire_unpack_ubh168(&g, &r2);
    HT_CHECK(r2.s_plus[0] == sp[0] && r2.s_minus[1] == sm[1], "T2 decode from canonical octets");
}

/* T3: axis trunks 0-8 (roots 3, 6, 9), expansion trunk 9 (root 4). */
static void t3_trunks(void)
{
    const uint32_t z3[3] = {1, 2, 3}, z2[2] = {4, 5};
    for (uint8_t t = 0; t < 10; t++) {
        zt_ubh168_wire_frame_t f;
        zt_unpacked_rails_t r;
        zt_wire_pack_ubh168(t, z3, z2, &f);
        zt_wire_rails_init(&r);
        zt_wire_unpack_ubh168(&f, &r);
        uint32_t root = zt_wire_digital_root(zt_wire_trunk_hz(t));
        if (t < 9) {
            HT_CHECK(r.is_axis_trunk && (root == 3 || root == 6 || root == 9), "T3 axis trunk");
        } else {
            HT_CHECK(!r.is_axis_trunk && root == 4, "T3 trunk 9 is the expansion line");
        }
        HT_CHECK(r.trunk_id == t, "T3 trunk id");
    }
    /* every tag value decodes trunk = tag mod 10 */
    bool ok = true;
    for (uint32_t tag = 0; tag < 256; tag++) {
        zt_ubh168_wire_frame_t f;
        zt_unpacked_rails_t r;
        zt_wire_pack_tagged((uint8_t) tag, z3, z2, &f);
        zt_wire_unpack_ubh168(&f, &r);
        ok = ok && r.trunk_id == tag % 10u;
    }
    HT_CHECK(ok, "T3 trunk = tag mod 10 for all 256 tags");
    uint8_t tg = zt_wire_make_tag(7, 4, true);
    HT_CHECK(tg == 147 && zt_wire_tag_trunk(tg) == 7 && zt_wire_tag_shell(tg) == 4 &&
                 zt_wire_tag_sync(tg),
             "tag = 100*sync + 10*shell + trunk");
}

/* T4: identical, mutually destructive values; no crash, adjacent memory intact. */
static void t4_paradox(void)
{
    struct {
        uint32_t guard0[4];
        zt_unpacked_rails_t r;
        uint32_t guard1[4];
    } box;
    for (int i = 0; i < 4; i++) box.guard0[i] = box.guard1[i] = 0xA5A5A5A5u;
    const uint32_t sp[3] = {0x00010000u, 0x00020000u, 0x00030000u};
    const uint32_t sm[2] = {0u - 0x00010000u, 0u - 0x00020000u}; /* S- = -S+ */
    zt_ubh168_wire_frame_t f;
    zt_wire_pack_ubh168(4, sp, sm, &f);
    zt_wire_rails_init(&box.r);
    zt_wire_unpack_ubh168(&f, &box.r);
    zt_truth_state_t s = zt_wire_resolve_rails(&box.r, 0x1000);
    HT_CHECK(s == ZT_TRUTH_GLUT || s == ZT_TRUTH_PARADOX, "T4 S+ = -S- is held (GLUT)");
    zt_truth_state_t a = zt_wire_resolve_step(&box.r, 0x1000);
    zt_truth_state_t b = zt_wire_resolve_step(&box.r, 0x1000);
    zt_truth_state_t c = zt_wire_resolve_step(&box.r, 0x1000);
    HT_CHECK(a == ZT_TRUTH_GLUT && b == ZT_TRUTH_GLUT && c == ZT_TRUTH_PARADOX,
             "T4 third consecutive GLUT is PARADOX");
    /* extreme words: INT32_MIN everywhere, no overflow, still held */
    const uint32_t mn3[3] = {0x80000000u, 0x80000000u, 0x80000000u};
    const uint32_t mx2[2] = {0x7FFFFFFFu, 0x7FFFFFFFu};
    zt_wire_pack_ubh168(4, mn3, mx2, &f);
    zt_wire_unpack_ubh168(&f, &box.r);
    s = zt_wire_resolve_rails(&box.r, 0x7FFFFFFF);
    HT_CHECK(s == ZT_TRUTH_GLUT, "T4 full-scale opposition is GLUT");
    bool intact = true;
    for (int i = 0; i < 4; i++)
        intact = intact && box.guard0[i] == 0xA5A5A5A5u && box.guard1[i] == 0xA5A5A5A5u;
    HT_CHECK(intact, "T4 adjacent memory intact");
}

static void resolver_table(void)
{
    zt_unpacked_rails_t r;
    zt_wire_rails_init(&r);
    HT_CHECK(zt_wire_resolve_rails(&r, 0x1000) == ZT_TRUTH_UNKNOWN, "all zero -> UNKNOWN");
    r.s_plus[0] = 0x10000;
    HT_CHECK(zt_wire_resolve_rails(&r, 0x1000) == ZT_TRUTH_NEUTRAL, "S- silent -> NEUTRAL");
    r.s_minus[0] = 0x10000;
    HT_CHECK(zt_wire_resolve_rails(&r, 0x1000) == ZT_TRUTH_TRUE, "parallel witness -> TRUE");
    r.s_minus[0] = 0u - 0x80000u; /* witness 8x stronger, opposed */
    HT_CHECK(zt_wire_resolve_rails(&r, 0x1000) == ZT_TRUTH_FALSE, "dominant refutation -> FALSE");
    r.s_minus[0] = 0;
    r.s_minus[1] = 0x10000; /* orthogonal */
    HT_CHECK(zt_wire_resolve_rails(&r, 0x1000) == ZT_TRUTH_GLUT, "orthogonal, active -> GLUT");
    HT_CHECK(zt_wire_resolve_evidence(0x10000, 0x10000, 0x100) == ZT_TRUTH_GLUT, "A and not-A");
    HT_CHECK(zt_wire_resolve_evidence(0x10000, 0, 0x100) == ZT_TRUTH_TRUE, "A only");
    HT_CHECK(zt_wire_resolve_evidence(0, 0x10000, 0x100) == ZT_TRUTH_FALSE, "not-A only");
    HT_CHECK(zt_wire_resolve_evidence(0, 0, 0x100) == ZT_TRUTH_UNKNOWN, "no evidence");
    uint8_t run = 0;
    zt_wire_persist(&run, ZT_TRUTH_GLUT);
    zt_wire_persist(&run, ZT_TRUTH_GLUT);
    HT_CHECK(zt_wire_persist(&run, ZT_TRUTH_TRUE) == ZT_TRUTH_TRUE && run == 0,
             "persistence resets on a resolved state");
}

/* W4: the idle pilot carries the alternation; an all-zero frame cannot. */
static void idle_pilot(void)
{
    const uint32_t z3[3] = {0, 0, 0}, z2[2] = {0, 0};
    zt_ubh168_wire_frame_t f;
    uint8_t o[21];
    zt_wire_pack_ubh168(0, z3, z2, &f);
    zt_wire_to_octets(&f, o);
    bool all_zero = true;
    for (int i = 1; i < 21; i++) all_zero = all_zero && o[i] == 0;
    HT_CHECK(all_zero, "zero frame: LE and BE bytes identical (no alternation)");

    zt_wire_idle_frame(0, &f);
    zt_wire_to_octets(&f, o);
    bool alt = true;
    for (int w = 0; w < 5; w++) {
        const uint8_t *p = o + 1 + 4 * w;
        uint32_t v = (w & 1) ? rd_be(p) : rd_le(p);
        alt = alt && v == ZT_WIRE_IDLE_PILOT;
        /* adjacent words hold the same value in opposite byte order */
        if (w < 4)
            for (int k = 0; k < 4; k++) alt = alt && p[k] == p[4 + 3 - k];
    }
    HT_CHECK(alt, "idle pilot: adjacent words are byte mirrors (L-B-L-B-L visible)");
    HT_CHECK(o[1] == '1' && o[5] == 'Z' && o[9] == '1' && o[13] == 'Z' && o[17] == '1',
             "idle pilot octets read 1VXZ ZXV1 1VXZ ZXV1 1VXZ");
    bool differ = false;
    for (int k = 0; k < 4; k++) differ = differ || o[1 + k] != o[5 + k];
    HT_CHECK(differ, "idle pilot: LE and BE serialisations differ");
    zt_unpacked_rails_t r;
    zt_wire_rails_init(&r);
    zt_wire_unpack_ubh168(&f, &r);
    HT_CHECK(zt_wire_is_idle(&r) && r.sync, "idle frame decodes as idle with sync");
    HT_CHECK(zt_wire_resolve_rails(&r, 0x1000) == ZT_TRUTH_NEUTRAL, "idle resolves NEUTRAL");
}

static void logic_maps(void)
{
    HT_CHECK(zt_truth_to_lpres(ZT_TRUTH_GLUT) == LPRES_STATE_BOTH, "GLUT -> lpres BOTH");
    HT_CHECK(zt_truth_to_lpres(ZT_TRUTH_PARADOX) == LPRES_STATE_BOTH, "PARADOX -> lpres BOTH");
    HT_CHECK(zt_truth_to_lpres(ZT_TRUTH_NEUTRAL) == LPRES_STATE_NEITHER, "NEUTRAL -> NEITHER");
    HT_CHECK(zt_truth_from_lpres(LPRES_STATE_BOTH) == ZT_TRUTH_GLUT, "lpres BOTH -> GLUT");
    bool rt = true;
    for (int s = 0; s <= 5; s++)
        rt = rt && zt_truth_from_hk(zt_truth_to_hk((zt_truth_state_t) s)) == (zt_truth_state_t) s;
    HT_CHECK(rt, "swarm_hk map is a bijection");
    HT_CHECK(zt_truth_to_hk(ZT_TRUTH_TRUE) == SWARM_HK_TRUE && SWARM_HK_TRUE == 0,
             "hk order differs (TRUE = 0)");
    const lpres_state_t L[4] = {LPRES_STATE_NEITHER, LPRES_STATE_TRUE, LPRES_STATE_FALSE,
                                LPRES_STATE_BOTH};
    bool lp = true;
    for (int i = 0; i < 4; i++) lp = lp && zt_truth_to_lpres(zt_truth_from_lpres(L[i])) == L[i];
    HT_CHECK(lp, "lpres -> wire -> lpres is the identity");
}

int main(void)
{
    t1_size_stride();
    t2_bit_exact();
    t3_trunks();
    t4_paradox();
    resolver_table();
    idle_pilot();
    logic_maps();
    return ht_finish("test_harmonic_wire");
}
