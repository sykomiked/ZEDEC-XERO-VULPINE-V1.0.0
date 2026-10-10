/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_geom_router.c — the router spec's three tests, stride traces, and the
 * ordering rule. */
#include "zt_htest.h"
#include "zt_geom_router.h"

static void t1_wrap(void)
{
    zt_geom_router_t r;
    zt_geom_router_init(&r);
    zt_edge_trajectory_t t = zt_geom_calculate_trajectory(&r, 0, 2, 0x10000);
    HT_CHECK(t.distance_q16 == ZT_DIST_ORTHO_Q16 && t.transit_ticks == 1,
             "T1 node 0 -> node 2 is one unit via the wrap");
    t = zt_geom_calculate_trajectory(&r, 0, 6, 0x10000);
    HT_CHECK(t.distance_q16 == ZT_DIST_ORTHO_Q16, "T1 node 0 -> node 6 wraps vertically");
    t = zt_geom_calculate_trajectory(&r, 0, 8, 0x10000);
    HT_CHECK(t.distance_q16 == ZT_DIST_DIAG_Q16, "T1 node 0 -> node 8 is one diagonal");
    bool k9 = true;
    for (uint8_t a = 0; a < 9; a++)
        for (uint8_t b = 0; b < 9; b++) {
            uint32_t d = zt_geom_calculate_trajectory(&r, a, b, 0x10000).distance_q16;
            k9 = k9 && (a == b ? d == 0 : (d == ZT_DIST_ORTHO_Q16 || d == ZT_DIST_DIAG_Q16));
        }
    HT_CHECK(k9, "T1 every pair is one hop (K9)");
}

static void t2_diagonal(void)
{
    zt_geom_router_t r;
    zt_geom_router_init(&r);
    const uint32_t vel[4] = {0x10000, 0x8000, 0x4000, 0x2000};
    bool ok = true;
    for (int i = 0; i < 4; i++) {
        uint32_t d = zt_geom_calculate_trajectory(&r, 0, 4, vel[i]).transit_ticks;
        uint32_t c = zt_geom_calculate_trajectory(&r, 0, 1, vel[i]).transit_ticks;
        ok = ok && d > c;
    }
    HT_CHECK(ok, "T2 diagonal strictly slower than cardinal at velocities 1, 1/2, 1/4, 1/8");
    /* Whole ticks cannot separate them once one tick covers a diagonal. */
    HT_CHECK(zt_geom_calculate_trajectory(&r, 0, 4, 0x18000).transit_ticks ==
                     zt_geom_calculate_trajectory(&r, 0, 1, 0x18000).transit_ticks &&
                 zt_geom_calculate_trajectory(&r, 0, 4, 0xC000).transit_ticks ==
                     zt_geom_calculate_trajectory(&r, 0, 1, 0xC000).transit_ticks,
             "documented limit: at velocity 3/2 and 3/4 both round to the same ticks");
    HT_CHECK(zt_geom_calculate_trajectory(&r, 0, 4, 0x10000).transit_ticks == 2 &&
                 zt_geom_calculate_trajectory(&r, 0, 1, 0x10000).transit_ticks == 1,
             "T2 ceiling: 2 ticks vs 1 (floor would give 1 vs 1)");
    HT_CHECK(zt_geom_calculate_trajectory(&r, 0, 4, 0).transit_ticks == UINT32_MAX,
             "velocity 0 never arrives");
}

static void t3_overflow(void)
{
    zt_geom_router_t r;
    zt_geom_router_init(&r);
    const uint32_t s3[3] = {1, 2, 3}, s2[2] = {4, 5};
    zt_ubh168_frame_t f, copy;
    zt_wire_pack_ubh168(4, s3, s2, &f);
    copy = f;
    r.queue_occupancy[4] = r.queue_capacity[4]; /* saturate node 4 */
    uint32_t occ[9];
    for (int i = 0; i < 9; i++) occ[i] = r.queue_occupancy[i];
    const uint8_t path[3] = {0, 4, 8};
    HT_CHECK(!zt_geom_dispatch_trace(&r, path, 3, &f), "T3 path through full node 4 refused");
    uint8_t a[21], b[21];
    zt_wire_to_octets(&f, a);
    zt_wire_to_octets(&copy, b);
    bool same = true;
    for (int i = 0; i < 21; i++) same = same && a[i] == b[i];
    HT_CHECK(same, "T3 input frame untouched");
    bool unchanged = r.ordinal == 0;
    for (int i = 0; i < 9; i++) unchanged = unchanged && occ[i] == r.queue_occupancy[i];
    HT_CHECK(unchanged, "T3 no queue changed (all-or-nothing)");
    zt_geom_release(&r, 4);
    HT_CHECK(zt_geom_dispatch_trace(&r, path, 3, &f), "T3 after a release the path goes");
    HT_CHECK(r.ordinal == 4, "T3 ordinal advanced by 2 + 2 diagonal ticks");
}

static void strides(void)
{
    uint8_t t[12];
    zt_geom_stride_trace(1, t, 12);
    const uint8_t want[12] = {1, 2, 4, 8, 7, 5, 1, 2, 4, 8, 7, 5};
    bool ok = true;
    for (int i = 0; i < 12; i++) ok = ok && t[i] == want[i];
    HT_CHECK(ok, "1-2-4-8-7-5 doubling walk");
    zt_geom_stride_trace(3, t, 4);
    HT_CHECK(t[0] == 3 && t[1] == 6 && t[2] == 3 && t[3] == 6, "3-6 axis walk");
    zt_geom_router_t r;
    zt_geom_router_init(&r);
    const uint32_t s3[3] = {0}, s2[2] = {0};
    zt_ubh168_frame_t f;
    zt_wire_pack_ubh168(0, s3, s2, &f);
    zt_geom_stride_trace(1, t, 7);
    uint64_t last = 0;
    bool mono = true;
    for (int k = 0; k < 4; k++) {
        mono = mono && zt_geom_dispatch_trace(&r, t, 7, &f) && r.ordinal > last;
        last = r.ordinal;
    }
    HT_CHECK(mono, "ordinal strictly increases per dispatch");
    /* 1-2 ortho(1), 2-4 diag(2), 4-8 diag(2), 8-7 ortho(1), 7-5 diag(2), 5-1 diag(2) = 10 */
    HT_CHECK(r.ordinal == 40, "stride trace costs 10 ticks");
    HT_CHECK(r.queue_occupancy[1] == 8 && !zt_geom_dispatch_trace(&r, t, 7, &f),
             "node 1 visited twice per trace fills at 4 traces");
    const uint8_t stay[2] = {3, 3}, bad[2] = {3, 9};
    HT_CHECK(!zt_geom_dispatch_trace(&r, stay, 2, &f) && !zt_geom_dispatch_trace(&r, bad, 2, &f),
             "self-hop and invalid node refused");
}

int main(void)
{
    t1_wrap();
    t2_diagonal();
    t3_overflow();
    strides();
    return ht_finish("test_geom_router");
}
