/* net_mirror.c — the triadic network space (see net_mirror.h).
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * Freestanding: no libc, no float, no allocation. The ISF is exact Q16.16
 * integer arithmetic; the arbiter is a pure function of the frame digest.
 */
#include "net_mirror.h"

/* freestanding zero-fill (no libc memset in kernel images). */
static void nm_zero(void *dst, uint32_t n)
{
    uint8_t *d = (uint8_t *) dst;
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}

NM_KEEP void net_mirror_init(net_mirror_t *nm)
{
    if (!nm) return;
    nm_zero(nm, (uint32_t) sizeof *nm);
    mirror_ui_init(&nm->mirror);
}

NM_KEEP int net_mirror_add_session(net_mirror_t *nm, mirror_pq_view_t *view)
{
    char name[16];
    uint32_t i;
    if (!nm || !view) return -1;
    if (nm->state.session_count >= NET_MIRROR_MAX_VIEWS) return -1;
    nm->state.sessions[nm->state.session_count] = *view;
    /* bind the session into the shared mirror so it projects into the frame
     * with the SAME valence machinery as every other register. The name is
     * built by hand ("pq-sess0".."pq-sess15") — freestanding, no snprintf. */
    name[0] = 'p';
    name[1] = 'q';
    name[2] = '-';
    name[3] = 's';
    name[4] = 'e';
    name[5] = 's';
    name[6] = 's';
    {
        uint32_t v = nm->state.session_count;
        char buf[8];
        uint32_t n = 0u;
        do {
            buf[n++] = (char) ('0' + (v % 10u));
            v /= 10u;
        } while (v && n < 7u);
        for (i = 0; i < n; i++) name[7u + i] = buf[n - 1u - i];
        name[7u + n] = 0;
    }
    if (mirror_bind(&nm->mirror, MIRROR_KIND_PQ, name, &nm->state.sessions[nm->state.session_count],
                    0xD4u + nm->state.session_count) < 0)
        return -1;
    return (int) nm->state.session_count++;
}

NM_KEEP int net_mirror_bind_topology(net_mirror_t *nm, mirror_net_view_t *view)
{
    if (!nm || !view) return -1;
    return mirror_bind(&nm->mirror, MIRROR_KIND_NET, "net-topology", view, 0xA1u);
}

NM_KEEP int net_mirror_bind_identity(net_mirror_t *nm, mirror_cid_view_t *view)
{
    if (!nm || !view) return -1;
    return mirror_bind(&nm->mirror, MIRROR_KIND_CID, "net-identity", view, 0xB2u);
}

NM_KEEP int net_mirror_bind_clock(net_mirror_t *nm, mirror_clock_view_t *view)
{
    if (!nm || !view) return -1;
    return mirror_bind(&nm->mirror, MIRROR_KIND_CLOCK, "net-clock", view, 0xC3u);
}

/* u = 1 - dot^2, exact Q16.16. */
NM_KEEP bool net_mirror_isf(uint32_t dot_q16, uint32_t *u_q16_out)
{
    uint64_t sq64;
    uint32_t sq_hi, sq_lo;
    uint32_t u;

    if (!u_q16_out) return false;
    *u_q16_out = Q16_ONE;

    /* precondition: |dot| <= 1, i.e. dot_q16 <= Q16_ONE. A malformed pair
     * never allocates. */
    if (dot_q16 > (uint32_t) Q16_ONE) return false;

    /* dot^2: (d/2^16)^2 = d^2/2^32, held as Q64.64 in (hi, lo). */
    sq64 = (uint64_t) dot_q16 * (uint64_t) dot_q16;
    sq_hi = (uint32_t) (sq64 >> 32);
    sq_lo = (uint32_t) sq64;

    /* u = 1 - dot^2. 1 is Q16.16 = 2^16; the square's Q16.16 head is
     * sq64 >> 16, so u = (2^32 - d^2) >> 16. */
    {
        uint64_t one64 = (uint64_t) Q16_ONE << 16; /* 2^32                 */
        uint64_t sq = ((uint64_t) sq_hi << 32) | sq_lo;
        uint64_t diff = (sq > one64) ? 0u : one64 - sq;
        u = (uint32_t) (diff >> 16);
        if (u > (uint32_t) Q16_ONE) u = (uint32_t) Q16_ONE;
    }
    *u_q16_out = u;
    return true;
}

NM_KEEP void net_mirror_project(const net_mirror_t *nm, uint64_t tick, mirror_frame_t *out)
{
    if (!nm || !out) return;
    mirror_project(&nm->mirror, tick, out);
}

/* The triadic arbiter: a deterministic fold over the frame digest decides
 * which space proceeds on this tick. Same state, same tick -> same answer. */
NM_KEEP net_space_id_t net_mirror_arbiter(const net_mirror_t *nm, uint64_t tick)
{
    mirror_frame_t f;
    uint32_t d;

    if (!nm) return NET_SPACE_HUMAN;
    net_mirror_project(nm, tick, &f);
    d = f.digest;

    /* fold the digest onto a 3-way decision without a wall clock. */
    d ^= d >> 8;
    d *= 0x85EBCA6Bu;
    d ^= d >> 13;
    return (net_space_id_t) (d % NET_SPACE_COUNT);
}