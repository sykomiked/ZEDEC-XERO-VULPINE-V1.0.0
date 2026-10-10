/* event_clock.h — ZEDEC XERO pqOS Event-Driven Clock
 *
 * Unlike traditional OSes that are bogged down by continuous clock ticks,
 * ZEDEC XERO uses an event-driven clock that ONLY activates when interfacing
 * with external systems. The kernel runs on its own internal event sequence
 * (ordinal-based, not time-based) and only syncs to wall-clock time when
 * an external interface requires it.
 *
 * This means:
 * - No timer interrupt flooding when idle
 * - No clock drift during computation
 * - External sync only on demand (network, UI, storage I/O)
 * - Native event sequence uses M5 ordinals, not seconds
 * - Power efficiency: CPU can truly idle without clock ticks
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 * 36N9 Genetics, LLC
 */
#ifndef EVENT_CLOCK_H
#define EVENT_CLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"
#include "edp_risk.h"   /* m5_coords_t + surplus_real_t (host double / target Q32.32) */

/* ======================= WHAT THIS MODULE IS =======================
 *
 * This is a LOGICAL clock with a HYBRID projection, in the Lamport sense.
 * It owns no oscillator, no timer, no RTC and no radio. Everything it
 * knows about wall-clock time was handed to it by a caller through
 * event_clock_sync(); everything else it reports is derived arithmetic
 * over its own event count. There is deliberately no hardware ops struct
 * here, because there is no hardware to bind: the ISR that reads the
 * physical timer lives in arch code (see ARCHITECTURE_EXTERNAL_CLOCK_BRIDGE.md)
 * and calls into this module from above the ISR boundary.
 *
 * The two counters are NOT the same thing:
 *
 *   event_ordinal  The logical clock. Advances by exactly one for every
 *                  orderable event: an explicit next_ordinal(), an accepted
 *                  sync, an accepted mode change, an accepted correction and
 *                  an accepted tick-interval change. It never decreases and
 *                  it is never reset by any call except event_clock_init().
 *
 *   local_counter  The TIME BASE. Advances only on next_ordinal(), because
 *                  only a real event represents the passage of internal
 *                  time — reconfiguring the clock does not make time pass.
 *
 * Hence event_ordinal >= local_counter always, and the difference is the
 * number of control events the clock has seen. Both saturate at UINT64_MAX
 * rather than wrapping (see LIMITATIONS).
 *
 * ======================= THE TIME MODEL =======================
 *
 * One event is nominally tick_interval_ns nanoseconds of internal time,
 * scaled by correction_q32 (a Q32.32 multiplier, 2^32 == 1.0). So, since
 * the last sync:
 *
 *   span_local = (local_counter - sync_counter)
 *                * tick_interval_ns * correction_q32 / 2^32
 *   get_time() = last_sync_ns + span_local
 *
 * and drift against an external reference over that same span is a
 * MEASURED ratio, not an estimate or a constant:
 *
 *   span_ref  = reference_ns - last_sync_ns
 *   drift_ppm = (span_local - span_ref) * 1e6 / span_ref
 *
 * All of it is INTEGER arithmetic (kernel images have no floating point):
 * drift_ppm is an int64_t truncated toward zero, computed with a 128-bit
 * intermediate so it is exact and saturates at INT64_MAX. *
 * Positive ppm means our internal time base ran FAST relative to the
 * reference. The measurement requires a real span on both sides; when it
 * does not exist, event_clock_measure_drift() returns
 * EVENT_CLOCK_DRIFT_UNMEASURABLE and leaves drift_ppm untouched. It does
 * NOT return 0, because 0 ppm is a perfect clock and that would be a lie.
 */

/* ===== Clock modes ===== */
typedef enum {
    CLOCK_DISABLED = 0,     /* No clock — pure event-driven */
    CLOCK_EXTERNAL_SYNC,    /* Sync only on external interface */
    CLOCK_PERIODIC,         /* Traditional periodic (fallback compat) */
    CLOCK_HYBRID,           /* Event-driven with periodic fallback */
} clock_mode_t;

#define CLOCK_MODE_MAX  CLOCK_HYBRID

/* ===== External interface types that trigger clock sync ===== */
typedef enum {
    CLOCK_IFACE_NONE = 0,
    CLOCK_IFACE_NETWORK,
    CLOCK_IFACE_STORAGE,
    CLOCK_IFACE_UI,
    CLOCK_IFACE_AUDIO,
    CLOCK_IFACE_BLUETOOTH,
    CLOCK_IFACE_USB,
    CLOCK_IFACE_RTC,        /* Hardware RTC */
    CLOCK_IFACE_NTP,        /* Network time protocol */
} clock_iface_t;

#define CLOCK_IFACE_MAX  CLOCK_IFACE_NTP

/* ===== Tunables / sentinels ===== */

/* Returned by event_clock_measure_drift() when no measurement is possible.
 * Chosen below -1e6 ppm, which a real measurement can never reach: the
 * local span is non-negative, so drift_ppm >= -1e6 by construction. */
#define EVENT_CLOCK_DRIFT_UNMEASURABLE (-1000000000LL)

/* The multiplicative correction is Q32.32: EVENT_CLOCK_CORRECTION_ONE is 1.0.
 * A correction outside [MIN, MAX] = [0.5, 2.0] is not a clock correction, it
 * is a caller bug, and is REJECTED (the stored factor is left alone). */
#define EVENT_CLOCK_CORRECTION_ONE ((uint64_t) 1 << 32)
#define EVENT_CLOCK_CORRECTION_MIN (EVENT_CLOCK_CORRECTION_ONE / 2u)
#define EVENT_CLOCK_CORRECTION_MAX (EVENT_CLOCK_CORRECTION_ONE * 2u)
/* Q32.32 correction from a ratio num/den, for callers and tests. */
#define EVENT_CLOCK_CORRECTION(num, den) ((uint64_t) (((uint64_t) (num) << 32) / (uint64_t) (den)))

/* Interval assumed when a mode that needs a period is entered without one. */
#define EVENT_CLOCK_DEFAULT_TICK_NS     1000000ULL   /* 1 ms */

/* In CLOCK_PERIODIC / CLOCK_HYBRID, an unbounded run without a reference is
 * unbounded drift. After this many events since the last sync,
 * event_clock_needs_sync() latches true on its own. */
#define EVENT_CLOCK_RESYNC_EVENTS       1000000ULL

/* Saturation ceiling for any nanosecond quantity scaled by a correction
 * other than exactly 1.0. Below 2^63. */
#define EVENT_CLOCK_NS_CEILING          9000000000000000000ULL

/* ===== Event clock state ===== */
typedef struct {
    clock_mode_t mode;

    /* External time. external_time_ns is the clock's CURRENT best estimate:
     * set by sync() and refreshed by get_time(). last_sync_ns is the raw
     * reference value captured at the last accepted sync and is the fixed
     * base the projection is added to — the two are equal immediately after
     * a sync and diverge as events accumulate. */
    uint64_t external_time_ns;
    uint64_t last_sync_ns;
    clock_iface_t last_sync_source;

    /* The interface event_clock_request_sync() was last asked to sync from,
     * and which has not been served yet. CLOCK_IFACE_NONE means "nothing
     * outstanding". Cleared by an accepted sync and by entering
     * CLOCK_DISABLED. This exists so request_sync()'s `source` argument is
     * recorded rather than merely validated and thrown away. */
    clock_iface_t pending_sync_source;

    /* Internal event sequence (ordinal-based, not time-based) */
    uint64_t event_ordinal;     /* Monotonically increasing event counter */
    uint64_t local_counter;     /* Free-running counter (the time base) */

    /* Sync tracking */
    bool needs_sync;
    bool clock_active;
    uint32_t sync_count;        /* Counts ACCEPTED syncs only; SATURATES at
                                 * UINT32_MAX (a wrap to 0 would claim
                                 * "never synced" on a clock that names a
                                 * source, which verify_coverage rejects). */
    int64_t drift_ppm;          /* Measured drift, ppm, truncated toward 0 */
    uint64_t correction_q32;    /* Applied correction, Q32.32 (2^32 == 1.0) */

    /* Drift needs a baseline, and a baseline needs a field: local_counter as
     * it stood at the last accepted sync. Without this there is no span to
     * measure over and "drift" degenerates into a guess. */
    uint64_t sync_counter;

    /* Periodic fallback (for compatibility) */
    uint64_t tick_interval_ns;
    uint64_t last_tick_ns;      /* Internal ns at the last whole tick boundary */

    /* M5 coordinates — a DERIVED CACHE of the fields above, rewritten by
     * every mutator. event_clock_verify_coverage() re-derives all five axes
     * and compares, so a stale or corrupt m5 FAILS.
     *
     * m5.phi is the signed relative drift, drift_ppm/1e6, CLAMPED to
     * +/-1e9 before conversion. drift_ppm itself is only bounded by int64
     * and surplus_real_t is Q32.32 int64 on the target, which cannot hold
     * |value| >= 2^31 — so the M5 projection saturates while the stored
     * drift_ppm does not. Read drift_ppm, not m5.phi, if you
     * need the raw measurement. */
    m5_coords_t m5;
} event_clock_t;

/* ===== API =====
 *
 * EVERY function below accepts a NULL clock and resolves it to the single
 * kernel-wide default clock returned by event_clock_default(). This is a
 * deliberate contract, not a null-guard: arch/arm32/kernel_main_arm32.c
 * boots with event_clock_init(0, CLOCK_EXTERNAL_SYNC), and a silent no-op
 * behind a "[BOOT] Initializing event-driven clock..." banner would be a
 * lie. With NULL resolved, that call really does initialise a real clock.
 */
event_clock_t *event_clock_default(void);

void event_clock_init(event_clock_t *clk, clock_mode_t mode);

/* Event sequence (primary timekeeping) */
uint64_t event_clock_next_ordinal(event_clock_t *clk);
uint64_t event_clock_get_ordinal(event_clock_t *clk);

/* External sync (only when needed) */
void event_clock_sync(event_clock_t *clk, uint64_t external_ns, clock_iface_t source);
uint64_t event_clock_get_time(event_clock_t *clk);
bool event_clock_needs_sync(event_clock_t *clk);

/* Latch a demand for an external sync and record WHICH interface should
 * serve it in clk->pending_sync_source. Refused (no latch, no record) in
 * CLOCK_DISABLED, for CLOCK_IFACE_NONE and for out-of-range sources. */
void event_clock_request_sync(event_clock_t *clk, clock_iface_t source);

/* Mode management */
void event_clock_set_mode(event_clock_t *clk, clock_mode_t mode);
clock_mode_t event_clock_get_mode(event_clock_t *clk);

/* Drift measurement */
int64_t event_clock_measure_drift(event_clock_t *clk, uint64_t reference_ns); /* ppm */
void event_clock_apply_correction(event_clock_t *clk, uint64_t correction_q32);

/* Compatibility (for legacy code that expects ticks) */
uint64_t event_clock_get_ticks(event_clock_t *clk);
void event_clock_set_tick_interval(event_clock_t *clk, uint64_t interval_ns);

/* Coverage */
bool event_clock_verify_coverage(event_clock_t *clk);

/* ======================= LIMITATIONS =======================
 * Read this before believing anything this module reports.
 *
 * 1. THERE IS NO OSCILLATOR. This module cannot tell you what time it is.
 *    It can only tell you what time it was when somebody last told it,
 *    plus events * tick_interval_ns * correction. If nobody ever
 *    calls event_clock_sync(), event_clock_get_time() returns 0 and
 *    event_clock_needs_sync() latches true. It never invents a value.
 *
 * 2. "TICKS" ARE EVENTS, NOT SECONDS. event_clock_get_ticks() returns
 *    floor(local_counter * correction) — the number of whole
 *    tick_interval_ns periods of INTERNAL time. With the correction
 *    at its default 1.0 that is exactly the event count. Legacy code that
 *    polls it gets monotonic progress; it does not get real elapsed time
 *    unless the caller drives one event per real tick_interval_ns.
 *    With tick_interval_ns == 0 there is no defined period and it returns 0.
 *    CAVEAT: it is computed as elapsed_ns / tick_interval_ns, and elapsed_ns
 *    saturates (see 7). Once local_counter * tick_interval_ns would exceed
 *    UINT64_MAX the result is LOWER than floor(local_counter *
 *    correction). It stays monotonic non-decreasing; it stops being
 *    the exact product.
 *
 * 3. ORDINALS SATURATE, THEY DO NOT WRAP. At UINT64_MAX,
 *    event_clock_next_ordinal() keeps returning UINT64_MAX. The guarantee
 *    is therefore "never returns less than a previously returned value",
 *    and it is strictly increasing until saturation. At 1e9 events/second
 *    saturation is ~584 years away.
 *
 * 4. WALL TIME MAY STEP BACKWARDS ACROSS A SYNC. If the external reference
 *    steps back (NTP does this), event_clock_get_time() steps back with it.
 *    Ordinals never do. If you need monotonic ordering, order by ordinal.
 *
 * 5. DRIFT IS ONLY MEASURABLE OVER A REAL SPAN. It needs at least one prior
 *    accepted sync, a non-zero tick_interval_ns, at least one event since
 *    that sync, and a reference strictly after last_sync_ns. Otherwise
 *    EVENT_CLOCK_DRIFT_UNMEASURABLE comes back and drift_ppm is unchanged.
 *
 * 6. CLOCK_DISABLED REALLY IS DISABLED. sync() and request_sync() are both
 *    refused in that mode (sync_count does not move), and get_time() returns
 *    the frozen last-known estimate. Switch modes first.
 *
 * 6b. CLOCK_IFACE_NONE IS NOT A SYNC SOURCE. sync() and request_sync()
 *    refuse it, along with any out-of-range value, because a counted sync
 *    must carry real provenance — event_clock_verify_coverage() enforces
 *    that a clock with sync_count > 0 names the interface it synced from.
 *
 * 7. NANOSECOND ARITHMETIC SATURATES at EVENT_CLOCK_NS_CEILING /
 *    UINT64_MAX rather than overflowing. A saturated reading is wrong but
 *    bounded; it is not wrapped. Which ceiling applies depends on the path:
 *    with a correction of exactly 1.0 the span is an exact integer product
 *    and saturates at UINT64_MAX; otherwise it is a 128-bit product scaled by
 *    the Q32.32 correction (truncated) and saturates at
 *    EVENT_CLOCK_NS_CEILING. Both paths are exact integer arithmetic.
 *
 * 7b. A PERIOD-DRIVEN MODE MAY NOT HAVE ITS PERIOD REMOVED.
 *    event_clock_set_tick_interval(clk, 0) is REFUSED in CLOCK_PERIODIC and
 *    CLOCK_HYBRID (no ordinal is spent), because the resulting clock is one
 *    event_clock_verify_coverage() rejects. Leave those modes first. Zero is
 *    accepted in CLOCK_DISABLED and CLOCK_EXTERNAL_SYNC, which have no period
 *    by definition.
 *
 * 8. NOT THREAD/IRQ SAFE. No locks, no atomics. Call it from one context,
 *    or serialise it yourself.
 *
 * 9. NAME CLASH: src/desktop/desktop.h defines a DIFFERENT, unrelated
 *    struct also called event_clock_t. Do not include both headers in one
 *    translation unit.
 */

#endif /* EVENT_CLOCK_H */
