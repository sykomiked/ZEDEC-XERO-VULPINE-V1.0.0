/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_residual_tap.c — the residual carrier's tap against the real tensor
 * engine (zt.h T15), and the tensor engine's own T16 frame against the
 * canonical harmonic layout. It compares octets directly and so needs only
 * zt.h (see zt_residual_tap.h for the former T14 name collision). */
#include "zt_htest.h"
#include "../tensor/zt.h"
#include "zt_residual_tap.h"
#include "zt_phi.h"

#define MAXP 8192u
static int64_t v[MAXP], r0[MAXP], r1[MAXP];

static void tap_matches_engine(void)
{
    zt_coil_t c;
    HT_CHECK(zt_coil_init(&c, 7, ZT_WIND_GOLDEN), "coil");
    HT_CHECK(c.total <= MAXP, "coil fits the test buffers");
    uint32_t s = 12345u;
    for (uint32_t i = 0; i < c.total && i < MAXP; i++) {
        s = s * 1664525u + 1013904223u;
        v[i] = (int64_t) (int32_t) s; /* Q16 values up to +-32768 */
    }
    zt_shell_tap_t ref;
    for (int k = 0; k < 10; k++) ref.acc[k] = 0;
    zt_set_shell_tap(&ref);
    zt_holo_encode(&c, v, r0);
    zt_set_shell_tap(NULL);

    zt_residual_tap_t mine;
    for (int k = 0; k < 10; k++) mine.acc[k] = 0;
    zt_residual_tap_attach(&mine);
    zt_holo_encode(&c, v, r1);
    zt_residual_tap_attach(NULL);
    bool same = true, nonzero = false;
    for (int k = 0; k < 10; k++) {
        same = same && mine.acc[k] == ref.acc[k];
        nonzero = nonzero || mine.acc[k] != 0;
    }
    HT_CHECK(same && nonzero, "engine fills zt_residual_tap_t exactly as zt_shell_tap_t");
    bool res = true;
    for (uint32_t i = 0; i < c.total; i++) res = res && r0[i] == r1[i];
    HT_CHECK(res, "tap does not change the residuals");
}

/* zt.h T16 with phase 0 puts words in the same octets as the harmonic frame;
 * only the tag octet means something else (phase | count << 1 | check4 << 4
 * there; 100 * sync + 10 * shell + trunk here). */
static void t16_layout(void)
{
    const uint32_t w[5] = {0x11223344u, 0xDDEEFF00u, 0x55667788u, 0x12345678u, 0x99AABBCCu};
    static const uint8_t want[20] = {0x44, 0x33, 0x22, 0x11, 0xDD, 0xEE, 0xFF, 0x00, 0x88, 0x77,
                                     0x66, 0x55, 0x12, 0x34, 0x56, 0x78, 0xCC, 0xBB, 0xAA, 0x99};
    uint8_t fr[2 * ZT_FRAME_OCTETS];
    HT_CHECK(zt_frame_pack(w, 5, 0, fr) == 1, "T16 one frame");
    bool same = true;
    for (int i = 0; i < 20; i++) same = same && fr[1 + i] == want[i];
    HT_CHECK(same, "T16 phase-0 octets 1..20 equal the harmonic L-B-L-B-L layout");
    HT_CHECK((fr[0] & 1u) == 0 && ((fr[0] >> 1) & 7u) == 5u, "T16 tag holds phase and count");
}

/* zt_phi.h and the tensor engine's zt_phi_pow: one quantity, two derivations. */
static void phi_agrees(void)
{
    bool ok = true;
    for (int32_t n = ZT_PHI_MIN_EXP; n <= ZT_PHI_MAX_EXP; n++) {
        int64_t d = (int64_t) zt_phi_pow_q16(n) - zt_phi_pow(n);
        ok = ok && d <= 1 && d >= -1;
    }
    HT_CHECK(ok, "zt_phi_pow_q16 agrees with tensor zt_phi_pow to 1 LSB for n = -24 .. 21");
}

int main(void)
{
    phi_agrees();
    tap_matches_engine();
    t16_layout();
    return ht_finish("test_residual_tap");
}
