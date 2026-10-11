/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxe_clock.c — drive a zxv_phase clock from microseconds, exactly.
 *
 * ticks = floor((elapsed_us * tps + rem) / 1e6), and the new rem is the
 * remainder, so N pumps add exactly floor(total_us * tps / 1e6) ticks
 * whatever the call pattern: no drift, no floating point. The product is
 * split (whole seconds and the sub-second part) so it cannot overflow. */
#include "zxe.h"

#define US_PER_S 1000000u

int32_t zxe_clock_init(zxe_clock *c, uint32_t ticks_per_second)
{
    if (!c || ticks_per_second == 0) return ZXV_E_ARG;
    c->last_us = 0;
    c->rem = 0;
    c->tps = ticks_per_second;
    c->started = 0;
    return ZXV_OK;
}

int32_t zxe_clock_pump_at(zxe_clock *c, const zxv_cap *cap, zxv_phase *phase, uint64_t now_us,
                          uint64_t *out_ticks_added, uint64_t *out_now_tick,
                          uint32_t *out_cleared)
{
    if (!c || !c->tps || !phase) return ZXV_E_ARG;
    int32_t rc = zxv_cap_check(cap, ZXV_CAP_PHASE);
    if (rc) return rc;
    if (out_ticks_added) *out_ticks_added = 0;
    if (out_cleared) *out_cleared = 0;
    if (!c->started) {
        c->started = 1;
        c->last_us = now_us;
        return out_now_tick ? zxv_phase_now(phase, out_now_tick) : ZXV_OK;
    }
    if (now_us < c->last_us) return ZXV_E_STATE;
    uint64_t dt = now_us - c->last_us;
    uint64_t whole_s = dt / US_PER_S, sub_us = dt % US_PER_S;
    if (whole_s && (uint64_t) c->tps > UINT64_MAX / whole_s) return ZXV_E_RANGE;
    uint64_t ticks = whole_s * c->tps;
    /* sub_us * tps < 1e6 * 2^32 and rem < 1e6: no overflow. */
    uint64_t frac = sub_us * c->tps + c->rem;
    uint64_t more = frac / US_PER_S;
    if (ticks > UINT64_MAX - more) return ZXV_E_RANGE;
    ticks += more;
    uint64_t now_tick = 0;
    uint32_t cleared = 0;
    if (ticks) {
        rc = zxv_phase_advance(cap, phase, ticks, &now_tick, &cleared);
        if (rc) return rc; /* nothing consumed: the next pump retries the same span */
    } else if (out_now_tick) {
        rc = zxv_phase_now(phase, &now_tick);
        if (rc) return rc;
    }
    c->rem = frac % US_PER_S;
    c->last_us = now_us;
    if (out_ticks_added) *out_ticks_added = ticks;
    if (out_now_tick) *out_now_tick = now_tick;
    if (out_cleared) *out_cleared = cleared;
    return ZXV_OK;
}

int32_t zxe_clock_pump(zxe_clock *c, const zxv_cap *cap, zxv_phase *phase,
                       uint64_t *out_ticks_added, uint64_t *out_now_tick, uint32_t *out_cleared)
{
    return zxe_clock_pump_at(c, cap, phase, zxp_monotonic_us(), out_ticks_added, out_now_tick,
                             out_cleared);
}
