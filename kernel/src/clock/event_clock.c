/* event_clock.c — ZEDEC XERO pqOS Event-Driven Clock implementation
 *
 * Pure logic. No MMIO, no ops struct, no hardware: the physical timer is
 * read by arch code inside the ISR and enters this module only as a
 * nanosecond value passed to event_clock_sync() (see
 * ARCHITECTURE_EXTERNAL_CLOCK_BRIDGE.md). Everything below is arithmetic
 * over the event count and whatever reference the caller supplied.
 *
 * Two invariants carry the whole module and are asserted by
 * event_clock_verify_coverage():
 *
 *   event_ordinal never decreases, across sync and across mode changes;
 *   event_ordinal >= local_counter, because every time-base step is also
 *   an orderable event but not every orderable event advances time.
 *
 * Author: 36N9 Genetics, LLC
 * License: SEL-3.3 (kernel component)
 */

/* Deliberately NO "#include <string.h> / freestanding.h" pair here, unlike
 * the neighbouring modules. This file uses no libc and no math functions at
 * all — ec_memset() and ec_fabs() below are the entire dependency — so
 * pulling in freestanding.h would buy nothing and cost portability:
 * freestanding.h does `#define sqrt fs_sqrt`, `#define exp fs_exp` and
 * friends, and event_clock.h -> m5_types.h -> <math.h>. On a target whose
 * CFLAGS do not also supply include/freestanding_stubs (build_system/
 * Makefile.arm32 does not), those macros rewrite the real math.h
 * prototypes and the translation unit stops compiling. Verified: adding
 * the include breaks the arm32-style flag set; leaving it out compiles
 * clean under all three configurations. */
#include "event_clock.h"

/* ===== Small freestanding-safe helpers ===== */

static void ec_memset(void *dst, int v, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < n; i++) d[i] = (uint8_t)v;
}

/* Named ec_fabs, not fabs: freestanding.h #defines fabs to fs_fabs. */
static double ec_fabs(double x) {
    return (x < 0.0) ? -x : x;
}

/* True only for values that compare inside a finite range — this is how the
 * module rejects NaN (every comparison with NaN is false) without libm. */
static bool ec_in_range(double x, double lo, double hi) {
    return (x >= lo) && (x <= hi);
}

/* Clamp a non-negative double into a uint64_t. Negative, NaN and
 * out-of-range inputs saturate instead of invoking undefined behaviour on
 * the cast. */
static uint64_t ec_ns_from_double(double v) {
    if (!(v > 0.0)) return 0;                       /* also catches NaN */
    if (v >= (double)EVENT_CLOCK_NS_CEILING) return EVENT_CLOCK_NS_CEILING;
    return (uint64_t)v;
}

static uint64_t ec_add_sat(uint64_t a, uint64_t b) {
    uint64_t s = a + b;
    return (s < a) ? UINT64_MAX : s;
}

static uint64_t ec_mul_sat(uint64_t a, uint64_t b) {
    if (a == 0 || b == 0) return 0;
    if (a > UINT64_MAX / b) return UINT64_MAX;
    return a * b;
}

/* ===== The kernel-wide default clock ===== */

static event_clock_t g_default_clock;
static bool g_default_ready = false;

/* Forward declaration: init needs the resolver, the resolver needs init. */
static void ec_reset(event_clock_t *clk, clock_mode_t mode);

event_clock_t *event_clock_default(void) {
    if (!g_default_ready) {
        /* A default clock that was never initialised would have mode 0
         * (CLOCK_DISABLED) and correction_factor 0.0, which fails coverage.
         * Bring it up in the documented default state on first touch. */
        ec_reset(&g_default_clock, CLOCK_EXTERNAL_SYNC);
        g_default_ready = true;
    }
    return &g_default_clock;
}

static event_clock_t *ec_resolve(event_clock_t *clk) {
    return clk ? clk : event_clock_default();
}

/* ===== Derived state ===== */

static bool ec_mode_valid(clock_mode_t m) {
    return (int)m >= (int)CLOCK_DISABLED && (int)m <= (int)CLOCK_MODE_MAX;
}

static bool ec_iface_valid(clock_iface_t s) {
    return (int)s >= (int)CLOCK_IFACE_NONE && (int)s <= (int)CLOCK_IFACE_MAX;
}

/* Does this mode keep the clock hardware/interface engaged between syncs?
 * DISABLED: never. EXTERNAL_SYNC: only transiently, during a sync request,
 * which is the entire point of the mode. PERIODIC/HYBRID: yes. */
static bool ec_active_for_mode(clock_mode_t m) {
    return (m == CLOCK_PERIODIC) || (m == CLOCK_HYBRID);
}

static bool ec_mode_keeps_time(clock_mode_t m) {
    return m != CLOCK_DISABLED;
}

/* M5 coordinates are STORED, not recomputed on demand, so that coverage
 * verification has something independent to check. Every mutator that
 * changes an input here must call this. */
static void ec_refresh_m5(event_clock_t *clk) {
    double conf;

    clk->m5.omega = (uint32_t)(clk->event_ordinal & 0xFFFFFFFFu);
    clk->m5.r     = SR_FROM_FLOAT(clk->correction_factor);

    /* ell = confidence in the time estimate, in [0,1]. Zero until a
     * reference has ever been supplied; then it decays with measured drift:
     * 0 ppm -> 1.0, 1e6 ppm -> 0.5. */
    if (clk->sync_count == 0) {
        conf = 0.0;
    } else {
        conf = 1.0 / (1.0 + ec_fabs(clk->drift_ppm) / 1.0e6);
    }
    clk->m5.ell = SR_FROM_FLOAT(conf);
    clk->m5.phi = SR_FROM_FLOAT(clk->drift_ppm / 1.0e6);
    clk->m5.chi = (uint32_t)clk->mode;
}

/* Advance the logical clock by one orderable event. Saturates, never wraps:
 * wrapping would silently reorder every event in the system. */
static void ec_advance_ordinal(event_clock_t *clk) {
    if (clk->event_ordinal != UINT64_MAX) clk->event_ordinal++;
}

/* ===== Init ===== */

static void ec_reset(event_clock_t *clk, clock_mode_t mode) {
    ec_memset(clk, 0, (uint32_t)sizeof(*clk));

    clk->mode = ec_mode_valid(mode) ? mode : CLOCK_EXTERNAL_SYNC;
    clk->external_time_ns  = 0;
    clk->last_sync_ns      = 0;
    clk->last_sync_source  = CLOCK_IFACE_NONE;
    clk->event_ordinal     = 0;
    clk->local_counter     = 0;
    clk->sync_counter      = 0;
    clk->needs_sync        = false;
    clk->clock_active      = ec_active_for_mode(clk->mode);
    clk->sync_count        = 0;
    clk->drift_ppm         = 0.0;
    clk->correction_factor = 1.0;
    clk->tick_interval_ns  = ec_active_for_mode(clk->mode)
                             ? EVENT_CLOCK_DEFAULT_TICK_NS : 0;
    clk->last_tick_ns      = 0;

    ec_refresh_m5(clk);
}

void event_clock_init(event_clock_t *clk, clock_mode_t mode) {
    if (!clk) {
        /* Explicitly requested initialisation of the kernel default clock.
         * Mark it ready FIRST so ec_reset's state is not overwritten by a
         * lazy bring-up on the next touch. */
        g_default_ready = true;
        ec_reset(&g_default_clock, mode);
        return;
    }
    ec_reset(clk, mode);
}

/* ===== Event sequence ===== */

uint64_t event_clock_next_ordinal(event_clock_t *clk) {
    clk = ec_resolve(clk);
    ec_advance_ordinal(clk);
    if (clk->local_counter != UINT64_MAX) clk->local_counter++;
    ec_refresh_m5(clk);
    return clk->event_ordinal;
}

uint64_t event_clock_get_ordinal(event_clock_t *clk) {
    clk = ec_resolve(clk);
    return clk->event_ordinal;
}

/* ===== Projection ===== */

/* Internal nanoseconds accumulated since the last accepted sync:
 * events * tick_interval_ns * correction_factor, saturating. */
static uint64_t ec_span_since_sync(const event_clock_t *clk) {
    uint64_t events;
    double   span;

    if (clk->tick_interval_ns == 0) return 0;
    if (clk->local_counter <= clk->sync_counter) return 0;

    events = clk->local_counter - clk->sync_counter;
    span = (double)events * (double)clk->tick_interval_ns
           * clk->correction_factor;
    return ec_ns_from_double(span);
}

/* Total internal nanoseconds since init: local_counter * interval * factor. */
static uint64_t ec_internal_elapsed(const event_clock_t *clk) {
    double span;
    if (clk->tick_interval_ns == 0) return 0;
    if (clk->correction_factor == 1.0) {
        /* Exact path — avoids losing counts above 2^53 in double. */
        return ec_mul_sat(clk->local_counter, clk->tick_interval_ns);
    }
    span = (double)clk->local_counter * (double)clk->tick_interval_ns
           * clk->correction_factor;
    return ec_ns_from_double(span);
}

/* ===== External sync ===== */

/* The one and only drift computation. Returns the ppm value, or
 * EVENT_CLOCK_DRIFT_UNMEASURABLE when there is no real span on both sides.
 * Does NOT write to clk — callers decide whether to keep the result. */
static double ec_compute_drift(const event_clock_t *clk, uint64_t reference_ns) {
    uint64_t span_ref_u;
    double   span_ref, span_local;

    if (clk->sync_count == 0)          return EVENT_CLOCK_DRIFT_UNMEASURABLE;
    if (clk->tick_interval_ns == 0)    return EVENT_CLOCK_DRIFT_UNMEASURABLE;
    if (clk->local_counter <= clk->sync_counter)
                                       return EVENT_CLOCK_DRIFT_UNMEASURABLE;
    if (reference_ns <= clk->last_sync_ns)
                                       return EVENT_CLOCK_DRIFT_UNMEASURABLE;

    span_ref_u = reference_ns - clk->last_sync_ns;
    span_ref   = (double)span_ref_u;
    span_local = (double)ec_span_since_sync(clk);

    /* Multiply before dividing: (delta * 1e6) / span keeps the common cases
     * exact in binary floating point, where delta / span * 1e6 does not. */
    return ((span_local - span_ref) * 1.0e6) / span_ref;
}

void event_clock_sync(event_clock_t *clk, uint64_t external_ns, clock_iface_t source) {
    double drift;

    clk = ec_resolve(clk);

    /* A sync in CLOCK_DISABLED is a caller bug: the mode means "no clock".
     * Refuse it outright rather than half-applying it. sync_count must not
     * move for a sync that did not happen. */
    if (clk->mode == CLOCK_DISABLED) return;

    /* An out-of-range source means the caller is confused about who is
     * talking to us; do not record garbage as provenance. */
    if (!ec_iface_valid(source) || source == CLOCK_IFACE_NONE) return;

    /* Measure BEFORE moving the baseline — the span being measured is the
     * one that just ended. */
    drift = ec_compute_drift(clk, external_ns);
    if (drift != EVENT_CLOCK_DRIFT_UNMEASURABLE) {
        clk->drift_ppm = drift;
    }

    clk->last_sync_ns     = external_ns;
    clk->external_time_ns = external_ns;
    clk->sync_counter     = clk->local_counter;
    clk->last_sync_source = source;
    clk->sync_count++;
    clk->needs_sync       = false;
    clk->clock_active     = ec_active_for_mode(clk->mode);

    /* A sync is itself an orderable event, so it takes an ordinal — but it
     * is not an event of the internal time base, so local_counter stands. */
    ec_advance_ordinal(clk);
    ec_refresh_m5(clk);
}

uint64_t event_clock_get_time(event_clock_t *clk) {
    uint64_t now;

    clk = ec_resolve(clk);

    if (clk->sync_count == 0) {
        /* We have never been told what time it is, so we do not know. Asking
         * IS the external demand this clock is built around: latch the sync
         * request, and return 0 rather than a fabricated value. */
        if (ec_mode_keeps_time(clk->mode)) clk->needs_sync = true;
        return 0;
    }

    if (clk->mode == CLOCK_DISABLED) {
        /* Frozen at the last thing we knew. No projection in this mode. */
        return clk->external_time_ns;
    }

    now = ec_add_sat(clk->last_sync_ns, ec_span_since_sync(clk));
    clk->external_time_ns = now;
    return now;
}

bool event_clock_needs_sync(event_clock_t *clk) {
    clk = ec_resolve(clk);

    if (clk->needs_sync) return true;

    /* A periodic clock running without a reference accumulates unbounded
     * drift. Latch a resync demand once the span since the last sync is
     * long enough for that to matter. */
    if (ec_active_for_mode(clk->mode) && clk->sync_count > 0 &&
        clk->local_counter >= clk->sync_counter &&
        (clk->local_counter - clk->sync_counter) >= EVENT_CLOCK_RESYNC_EVENTS) {
        clk->needs_sync = true;
        return true;
    }
    return false;
}

void event_clock_request_sync(event_clock_t *clk, clock_iface_t source) {
    clk = ec_resolve(clk);

    /* No clock means no sync requests. Silently latching one here would make
     * event_clock_needs_sync() true in a mode that can never satisfy it. */
    if (clk->mode == CLOCK_DISABLED) return;
    if (!ec_iface_valid(source) || source == CLOCK_IFACE_NONE) return;

    clk->needs_sync   = true;
    clk->clock_active = true;   /* we are about to touch an external interface */
}

/* ===== Mode management ===== */

void event_clock_set_mode(event_clock_t *clk, clock_mode_t mode) {
    clk = ec_resolve(clk);

    if (!ec_mode_valid(mode)) return;
    if (mode == clk->mode) return;          /* no event, nothing changed */

    clk->mode = mode;

    /* Entering a mode that needs a period without one is not usable; adopt
     * the documented default rather than leaving a broken configuration. */
    if (ec_active_for_mode(mode) && clk->tick_interval_ns == 0) {
        clk->tick_interval_ns = EVENT_CLOCK_DEFAULT_TICK_NS;
    }

    if (mode == CLOCK_DISABLED) {
        clk->needs_sync = false;            /* nothing can satisfy it now */
    }
    clk->clock_active = ec_active_for_mode(mode);

    /* Ordinals survive mode changes untouched except for this single
     * advance — the mode change is itself an orderable event. Nothing here
     * resets event_ordinal or local_counter. */
    ec_advance_ordinal(clk);
    ec_refresh_m5(clk);
}

clock_mode_t event_clock_get_mode(event_clock_t *clk) {
    clk = ec_resolve(clk);
    return clk->mode;
}

/* ===== Drift ===== */

double event_clock_measure_drift(event_clock_t *clk, uint64_t reference_ns) {
    double drift;

    clk = ec_resolve(clk);
    drift = ec_compute_drift(clk, reference_ns);

    if (drift == EVENT_CLOCK_DRIFT_UNMEASURABLE) {
        /* Nothing was measured, so nothing is recorded. */
        return EVENT_CLOCK_DRIFT_UNMEASURABLE;
    }

    clk->drift_ppm = drift;
    ec_refresh_m5(clk);         /* m5.ell / m5.phi are functions of drift */
    return drift;
}

void event_clock_apply_correction(event_clock_t *clk, double correction) {
    clk = ec_resolve(clk);

    /* Range test also rejects NaN and both infinities. */
    if (!ec_in_range(correction, EVENT_CLOCK_CORRECTION_MIN,
                                 EVENT_CLOCK_CORRECTION_MAX)) return;
    if (correction == clk->correction_factor) return;   /* no change, no event */

    clk->correction_factor = correction;
    ec_advance_ordinal(clk);
    ec_refresh_m5(clk);
}

/* ===== Legacy tick compatibility ===== */

uint64_t event_clock_get_ticks(event_clock_t *clk) {
    uint64_t elapsed, ticks;

    clk = ec_resolve(clk);
    if (clk->tick_interval_ns == 0) return 0;   /* no period defined */

    elapsed = ec_internal_elapsed(clk);
    ticks   = elapsed / clk->tick_interval_ns;
    clk->last_tick_ns = ec_mul_sat(ticks, clk->tick_interval_ns);
    return ticks;
}

void event_clock_set_tick_interval(event_clock_t *clk, uint64_t interval_ns) {
    clk = ec_resolve(clk);
    if (interval_ns == clk->tick_interval_ns) return;   /* no change, no event */

    clk->tick_interval_ns = interval_ns;
    ec_advance_ordinal(clk);
    ec_refresh_m5(clk);
}

/* ===== Coverage =====
 *
 * This function CAN fail, and test_event_clock.c makes it fail eight
 * different ways. It checks stored state against the invariants this module
 * claims to maintain; it recomputes nothing, so a corrupted or stale field
 * is caught rather than papered over.
 */
bool event_clock_verify_coverage(event_clock_t *clk) {
    clk = ec_resolve(clk);

    /* 1. Enumerations are in range. */
    if (!ec_mode_valid(clk->mode)) return false;
    if (!ec_iface_valid(clk->last_sync_source)) return false;

    /* 2. Every time-base step is an orderable event, so ordinals lead. */
    if (clk->event_ordinal < clk->local_counter) return false;

    /* 3. The drift baseline cannot be in the future of the time base. */
    if (clk->sync_counter > clk->local_counter) return false;

    /* 4. Sync bookkeeping agrees with itself. */
    if (clk->sync_count == 0) {
        if (clk->sync_counter != 0) return false;
        if (clk->last_sync_ns != 0) return false;
        if (clk->last_sync_source != CLOCK_IFACE_NONE) return false;
    } else {
        /* An accepted sync always recorded a real provenance. */
        if (clk->last_sync_source == CLOCK_IFACE_NONE) return false;
    }

    /* 5. Correction is a sane multiplier (also rejects NaN). */
    if (!ec_in_range(clk->correction_factor, EVENT_CLOCK_CORRECTION_MIN,
                                             EVENT_CLOCK_CORRECTION_MAX))
        return false;

    /* 6. Drift is a real measurement. The local span is non-negative, so a
     *    genuine measurement is >= -1e6 ppm; anything below that is either
     *    corruption or the EVENT_CLOCK_DRIFT_UNMEASURABLE sentinel leaking
     *    into stored state. Written as a negated >= so NaN fails too.
     *    There is deliberately no upper bound: an arbitrarily fast local
     *    time base produces an arbitrarily large positive ppm, and clamping
     *    a measurement would be falsifying it. */
    if (!(clk->drift_ppm >= -1.0e6)) return false;

    /* 7. Mode consistency. */
    if (clk->mode == CLOCK_DISABLED) {
        if (clk->clock_active) return false;
        if (clk->needs_sync) return false;
    }
    if (clk->mode == CLOCK_PERIODIC && clk->tick_interval_ns == 0) return false;

    /* 8. Stored M5 coordinates still describe this clock. */
    if (clk->m5.omega != (uint32_t)(clk->event_ordinal & 0xFFFFFFFFu)) return false;
    if (clk->m5.chi   != (uint32_t)clk->mode) return false;
    if (clk->m5.ell < SR_ZERO || clk->m5.ell > SR_ONE) return false;
    if (clk->sync_count == 0 && clk->m5.ell != SR_ZERO) return false;

    return true;
}
