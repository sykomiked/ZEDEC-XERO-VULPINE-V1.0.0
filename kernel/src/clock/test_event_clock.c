/* test_event_clock.c — the event clock against its own arithmetic.
 *
 * The anchors here are COMPUTED VALUES, not control flow. "sync returned"
 * proves nothing; "1001 events of 1000 ns against a 1000000 ns reference is
 * exactly +1000.0 ppm" proves the drift formula. Every projection, every
 * drift, every tick count below is asserted against a number worked out by
 * hand from the model documented in event_clock.h.
 *
 * Sections 13 and 17 exist because event_clock_verify_coverage() is a verify
 * function, and a verify function that cannot fail is a lie. Between them
 * they drive all 23 of its rejection branches to false.
 *
 * Section 17 is the regression wall for defects that were live in this file
 * and are not allowed back:
 *   17a  set_tick_interval(0) in PERIODIC/HYBRID built a live clock that
 *        verify_coverage() rejected — the public API could reach a state its
 *        own verifier called corrupt.
 *   17b  request_sync()'s source was range-checked and then thrown away.
 *   17c  sync_count wrapped uint32 to 0, i.e. to "never synced" on a clock
 *        that names a sync source.
 *   17d  m5.r and m5.phi were written by every mutator and checked by none.
 *   17e/f the M5 projection was fed unbounded drift values that do not fit
 *        Q32.32 on the target, and corrupt corrections reached the span.
 *
 * The clock is integer-only (kernel images have no floating point):
 * drift_ppm is an int64_t and the correction is Q32.32 (correction_q32,
 * 2^32 == 1.0). Only the M5 projection is double under TEST_HOST, which is
 * why near() survives for m5 axes. `make fuzz` runs this file under
 * ASan+UBSan.
 */
#include <stdio.h>
#include "event_clock.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

#define U(x) ((unsigned long long) (x))

static int near(double a, double b)
{
    double d = a - b;
    if (d < 0) d = -d;
    return d <= 1e-9;
}

#define CORR_ONE EVENT_CLOCK_CORRECTION_ONE
#define CORR(n, d) EVENT_CLOCK_CORRECTION(n, d)

/* A clock that has really been through a sync, used as the healthy baseline
 * for the coverage-failure section. */
static void make_healthy(event_clock_t *c)
{
    event_clock_init(c, CLOCK_EXTERNAL_SYNC);
    event_clock_set_tick_interval(c, 1000);
    event_clock_sync(c, 1000000, CLOCK_IFACE_NTP);
    for (int i = 0; i < 10; i++) (void) event_clock_next_ordinal(c);
}

int main(void)
{
    event_clock_t c;
    printf("=== event clock (logical ordinals + measured drift) ===\n");

    /* ================= 0. the lazily brought-up default clock ============= */
    {
        event_clock_t *d = event_clock_default();
        CHECK(d != NULL, "event_clock_default() hands back a real clock");
        CHECK(d->mode == CLOCK_EXTERNAL_SYNC,
              "an untouched default clock comes up in CLOCK_EXTERNAL_SYNC");
        CHECK(d->correction_q32 == CORR_ONE,
              "and with correction 1.0 (2^32), not the 0 a bare memset would leave");
        CHECK(event_clock_verify_coverage(d),
              "so the default clock passes coverage before anyone initialises it");
    }

    /* ================= 1. init establishes exact state ==================== */
    event_clock_init(&c, CLOCK_EXTERNAL_SYNC);
    CHECK(c.mode == CLOCK_EXTERNAL_SYNC, "init records the requested mode");
    CHECK(c.event_ordinal == 0 && c.local_counter == 0, "both counters start at 0");
    CHECK(c.sync_count == 0 && c.sync_counter == 0 && c.last_sync_ns == 0,
          "nothing has been synced yet");
    CHECK(c.correction_q32 == CORR_ONE, "correction starts at exactly 1.0 (2^32)");
    CHECK(c.drift_ppm == 0, "drift starts at 0 ppm (nothing measured yet)");
    CHECK(c.tick_interval_ns == 0, "CLOCK_EXTERNAL_SYNC gets no tick interval — it has no period");
    CHECK(c.clock_active == false && c.needs_sync == false,
          "an idle event-driven clock is neither active nor demanding a sync");
    CHECK(c.m5.omega == 0 && c.m5.chi == (uint32_t) CLOCK_EXTERNAL_SYNC,
          "M5 omega/chi mirror ordinal 0 and the mode");
    CHECK(c.m5.ell == 0.0, "M5 ell is 0.0: zero confidence with no reference");
    CHECK(event_clock_get_time(&c) == 0,
          "get_time() with no reference returns 0 — it never invents a time");
    CHECK(c.needs_sync == true, "and asking for the time IS the demand: needs_sync latched true");

    {
        event_clock_t p;
        event_clock_init(&p, CLOCK_PERIODIC);
        CHECK(p.tick_interval_ns == EVENT_CLOCK_DEFAULT_TICK_NS,
              "CLOCK_PERIODIC init adopts the 1 ms default interval");
        CHECK(p.clock_active == true, "and a periodic clock is active");

        event_clock_t bad;
        event_clock_init(&bad, (clock_mode_t) 77);
        CHECK(bad.mode == CLOCK_EXTERNAL_SYNC,
              "an out-of-range mode falls back to CLOCK_EXTERNAL_SYNC, not 77");

        event_clock_t dis;
        event_clock_init(&dis, CLOCK_DISABLED);
        CHECK(dis.clock_active == false && dis.tick_interval_ns == 0,
              "CLOCK_DISABLED init leaves the clock inert");
        CHECK(event_clock_get_time(&dis) == 0 && dis.needs_sync == false,
              "get_time() in DISABLED does not latch a sync it can never get");
    }

    /* ================= 2. ordinals are strictly monotonic ================= */
    event_clock_init(&c, CLOCK_EXTERNAL_SYNC);
    {
        int strict = 1;
        uint64_t prev = 0;
        for (uint64_t i = 1; i <= 1000; i++) {
            uint64_t o = event_clock_next_ordinal(&c);
            if (o != i || o <= prev) strict = 0;
            prev = o;
        }
        CHECK(strict, "1000 next_ordinal() calls yield exactly 1,2,...,1000");
        CHECK(event_clock_get_ordinal(&c) == 1000, "get_ordinal() reads 1000 without advancing it");
        CHECK(event_clock_get_ordinal(&c) == 1000, "and reading again still reads 1000");
        CHECK(c.local_counter == 1000, "the time base advanced in step");
    }

    /* ================= 3. ordinals vs the time base ======================= */
    event_clock_set_tick_interval(&c, 1000);
    CHECK(event_clock_get_ordinal(&c) == 1001 && c.local_counter == 1000,
          "a tick-interval change takes ordinal 1001 but does NOT advance time");
    event_clock_set_mode(&c, CLOCK_HYBRID);
    CHECK(event_clock_get_ordinal(&c) == 1002 && c.local_counter == 1000,
          "a mode change takes ordinal 1002, time base still 1000");
    event_clock_apply_correction(&c, CORR(3, 2));
    CHECK(event_clock_get_ordinal(&c) == 1003 && c.correction_q32 == CORR(3, 2),
          "an accepted correction takes ordinal 1003");
    event_clock_sync(&c, 5000000, CLOCK_IFACE_NTP);
    CHECK(event_clock_get_ordinal(&c) == 1004 && c.local_counter == 1000 && c.sync_counter == 1000,
          "a sync takes ordinal 1004 and pins the drift baseline at 1000");

    event_clock_set_mode(&c, CLOCK_HYBRID);
    event_clock_apply_correction(&c, CORR(3, 2));
    event_clock_set_tick_interval(&c, 1000);
    CHECK(event_clock_get_ordinal(&c) == 1004,
          "re-setting a value to what it already is is not an event: still 1004");
    CHECK(c.event_ordinal >= c.local_counter,
          "invariant holds: ordinals lead the time base (1004 >= 1000)");

    /* ============ 4. monotonic across 20000 mixed operations ============== */
    {
        event_clock_t m;
        uint64_t expect = 0, prev = 0, ref = 1000000;
        int strict = 1, exact = 1, toggle = 1;

        event_clock_init(&m, CLOCK_EXTERNAL_SYNC);
        event_clock_set_tick_interval(&m, 1000);
        expect++;

        for (int i = 0; i < 20000; i++) {
            uint64_t o;
            (void) event_clock_next_ordinal(&m);
            expect++;

            if ((i % 100) == 0) {
                event_clock_sync(&m, ref, CLOCK_IFACE_NTP);
                ref += 1000000;
                expect++;
            }
            if ((i % 250) == 0) {
                clock_mode_t want = toggle ? CLOCK_HYBRID : CLOCK_EXTERNAL_SYNC;
                if (want != event_clock_get_mode(&m)) expect++;
                event_clock_set_mode(&m, want);
                toggle = !toggle;
            }
            o = event_clock_get_ordinal(&m);
            if (o < prev) strict = 0;
            if (o != expect) exact = 0;
            prev = o;
        }
        CHECK(strict, "20000 mixed events/syncs/mode-changes never step backwards");
        CHECK(exact, "and the ordinal matches the running event count at every step");
        CHECK(event_clock_get_ordinal(&m) == 20281,
              "final ordinal is exactly 20000 events + 200 syncs + 80 mode changes + 1");
        CHECK(m.local_counter == 20000, "the time base counted the 20000 events and nothing else");
        CHECK(m.sync_counter == 19901,
              "the drift baseline sits at the event count of the last sync (19901)");
        CHECK(m.sync_count == 200, "exactly 200 syncs were accepted and counted");
        CHECK(event_clock_verify_coverage(&m), "and the clock still verifies");
    }

    /* ================= 5. saturation, not wraparound ====================== */
    {
        event_clock_t s;
        uint64_t o1, o2;
        event_clock_init(&s, CLOCK_EXTERNAL_SYNC);
        s.event_ordinal = UINT64_MAX - 1;
        s.local_counter = UINT64_MAX - 1;
        o1 = event_clock_next_ordinal(&s);
        o2 = event_clock_next_ordinal(&s);
        CHECK(o1 == UINT64_MAX, "the ordinal reaches UINT64_MAX");
        CHECK(o2 == UINT64_MAX, "and then SATURATES — it does not wrap to 0");
        CHECK(o2 >= o1, "so the never-decreasing guarantee survives saturation");
        CHECK(s.local_counter == UINT64_MAX, "the time base saturates too");
    }

    /* ================= 6. refused syncs are not counted ==================== */
    {
        event_clock_t r;
        event_clock_init(&r, CLOCK_DISABLED);
        event_clock_sync(&r, 12345, CLOCK_IFACE_NTP);
        CHECK(r.sync_count == 0 && r.last_sync_ns == 0 && r.event_ordinal == 0,
              "a sync in CLOCK_DISABLED is refused outright — no count, no ordinal");
        event_clock_request_sync(&r, CLOCK_IFACE_NTP);
        CHECK(r.needs_sync == false,
              "and a sync REQUEST in DISABLED is refused too (nothing could serve it)");

        event_clock_init(&r, CLOCK_EXTERNAL_SYNC);
        event_clock_sync(&r, 12345, (clock_iface_t) 99);
        CHECK(r.sync_count == 0 && r.event_ordinal == 0,
              "an out-of-range interface id is refused, not recorded");
        event_clock_sync(&r, 12345, CLOCK_IFACE_NONE);
        CHECK(r.sync_count == 0 && r.last_sync_source == CLOCK_IFACE_NONE,
              "CLOCK_IFACE_NONE is not provenance — refused");
        event_clock_request_sync(&r, (clock_iface_t) 200);
        CHECK(r.needs_sync == false, "a request from a bogus interface is refused");

        event_clock_sync(&r, 12345, CLOCK_IFACE_RTC);
        CHECK(r.sync_count == 1 && r.last_sync_ns == 12345 &&
                  r.last_sync_source == CLOCK_IFACE_RTC && r.event_ordinal == 1,
              "a well-formed sync IS accepted: count 1, ns 12345, source RTC, ordinal 1");
    }

    /* ================= 7. the time projection, to the nanosecond ========== */
    {
        event_clock_t t;
        uint64_t frozen;
        event_clock_init(&t, CLOCK_EXTERNAL_SYNC);
        event_clock_set_tick_interval(&t, 1000);
        event_clock_sync(&t, 1000000000ULL, CLOCK_IFACE_NTP);
        CHECK(event_clock_get_time(&t) == 1000000000ULL,
              "right after a sync, get_time() is the reference itself");

        for (int i = 0; i < 500; i++) (void) event_clock_next_ordinal(&t);
        CHECK(event_clock_get_time(&t) == 1000500000ULL,
              "500 events x 1000 ns projects to 1,000,500,000 ns");

        event_clock_apply_correction(&t, CORR(1, 2));
        CHECK(event_clock_get_time(&t) == 1000250000ULL,
              "a 0.5 correction halves the projected span: 1,000,250,000 ns");

        event_clock_set_mode(&t, CLOCK_DISABLED);
        frozen = event_clock_get_time(&t);
        CHECK(frozen == 1000250000ULL, "switching to DISABLED freezes the estimate");
        for (int i = 0; i < 500; i++) (void) event_clock_next_ordinal(&t);
        CHECK(event_clock_get_time(&t) == 1000250000ULL,
              "and 500 more events do NOT move it — DISABLED means disabled");

        event_clock_set_mode(&t, CLOCK_EXTERNAL_SYNC);
        CHECK(event_clock_get_time(&t) == 1000500000ULL,
              "re-enabling picks the projection back up: 1000 events x 1000 x 0.5");
    }

    /* ================= 8. drift is a measured ratio ======================= */
    {
        event_clock_t d;
        int64_t drift;

        event_clock_init(&d, CLOCK_EXTERNAL_SYNC);
        event_clock_set_tick_interval(&d, 1000);
        event_clock_sync(&d, 0, CLOCK_IFACE_RTC);
        for (int i = 0; i < 1001; i++) (void) event_clock_next_ordinal(&d);
        drift = event_clock_measure_drift(&d, 1000000);
        CHECK(drift == 1000,
              "1,001,000 ns of local time over a 1,000,000 ns reference = +1000 ppm");
        CHECK(d.drift_ppm == 1000, "and the measurement is stored");

        event_clock_init(&d, CLOCK_EXTERNAL_SYNC);
        event_clock_set_tick_interval(&d, 1000);
        event_clock_sync(&d, 0, CLOCK_IFACE_RTC);
        for (int i = 0; i < 999; i++) (void) event_clock_next_ordinal(&d);
        drift = event_clock_measure_drift(&d, 1000000);
        CHECK(drift == -1000,
              "999,000 ns over the same reference = -1000 ppm (we ran slow)");

        event_clock_init(&d, CLOCK_EXTERNAL_SYNC);
        event_clock_set_tick_interval(&d, 1000);
        event_clock_sync(&d, 0, CLOCK_IFACE_RTC);
        for (int i = 0; i < 1000; i++) (void) event_clock_next_ordinal(&d);
        drift = event_clock_measure_drift(&d, 1000000);
        CHECK(drift == 0, "a clock that matches the reference measures 0 ppm");

        /* a correction that exactly cancels a 2x-fast time base */
        event_clock_init(&d, CLOCK_EXTERNAL_SYNC);
        event_clock_set_tick_interval(&d, 1000);
        event_clock_sync(&d, 0, CLOCK_IFACE_RTC);
        for (int i = 0; i < 2000; i++) (void) event_clock_next_ordinal(&d);
        drift = event_clock_measure_drift(&d, 1000000);
        CHECK(drift == 1000000,
              "2,000,000 ns over 1,000,000 ns = +1,000,000 ppm (twice too fast)");
        CHECK(near(d.m5.ell, 0.5),
              "M5 ell halves at 1e6 ppm: confidence 0.5, computed not asserted");
        CHECK(near(d.m5.phi, 1.0), "M5 phi carries the signed relative drift 1.0");
        event_clock_apply_correction(&d, CORR(1, 2));
        drift = event_clock_measure_drift(&d, 1000000);
        CHECK(drift == 0, "a 0.5 correction cancels it exactly: 0 ppm");
        CHECK(near(d.m5.ell, 1.0), "and confidence returns to 1.0");

        /* drift measured by sync() itself, not only by measure_drift() */
        event_clock_init(&d, CLOCK_EXTERNAL_SYNC);
        event_clock_set_tick_interval(&d, 1000);
        event_clock_sync(&d, 0, CLOCK_IFACE_RTC);
        for (int i = 0; i < 1001; i++) (void) event_clock_next_ordinal(&d);
        event_clock_sync(&d, 1000000, CLOCK_IFACE_RTC);
        CHECK(d.drift_ppm == 1000, "sync() measures the span that just ended: +1000 ppm");
        CHECK(d.sync_count == 2 && d.sync_counter == 1001,
              "and then re-pins the baseline at event 1001");
        CHECK(event_clock_get_time(&d) == 1000000ULL,
              "the projection restarts from the new reference");

        /* integer ppm truncates toward zero: 1,000,001 ns over 3,000,000 ns
         * is -666,666.33 ppm -> -666,666 (not -666,667) */
        event_clock_init(&d, CLOCK_EXTERNAL_SYNC);
        event_clock_set_tick_interval(&d, 1);
        event_clock_sync(&d, 0, CLOCK_IFACE_RTC);
        d.local_counter = 1000001ULL;
        d.event_ordinal = 1000001ULL + 2;
        drift = event_clock_measure_drift(&d, 3000000);
        CHECK(drift == -666666, "integer drift truncates toward zero: -666,666 ppm");
        drift = event_clock_measure_drift(&d, 999999);
        CHECK(drift == 2, "1,000,001 over 999,999 ns = +2.000002 ppm -> +2");
    }

    /* ========== 9. unmeasurable drift says so, and changes nothing ======== */
    {
        event_clock_t u;
        int64_t r;

        event_clock_init(&u, CLOCK_EXTERNAL_SYNC);
        event_clock_set_tick_interval(&u, 1000);
        for (int i = 0; i < 100; i++) (void) event_clock_next_ordinal(&u);
        r = event_clock_measure_drift(&u, 1000000);
        CHECK(r == EVENT_CLOCK_DRIFT_UNMEASURABLE,
              "no sync ever happened -> UNMEASURABLE, not a confident 0");
        CHECK(u.drift_ppm == 0, "and drift_ppm was not written");

        event_clock_init(&u, CLOCK_EXTERNAL_SYNC);
        event_clock_sync(&u, 0, CLOCK_IFACE_RTC);
        for (int i = 0; i < 100; i++) (void) event_clock_next_ordinal(&u);
        CHECK(event_clock_measure_drift(&u, 1000000) == EVENT_CLOCK_DRIFT_UNMEASURABLE,
              "tick_interval_ns == 0 -> no ns-per-event -> UNMEASURABLE");

        event_clock_init(&u, CLOCK_EXTERNAL_SYNC);
        event_clock_set_tick_interval(&u, 1000);
        event_clock_sync(&u, 0, CLOCK_IFACE_RTC);
        CHECK(event_clock_measure_drift(&u, 1000000) == EVENT_CLOCK_DRIFT_UNMEASURABLE,
              "zero events since the sync -> no local span -> UNMEASURABLE");

        /* a real measurement, then references that cannot bound a span */
        event_clock_init(&u, CLOCK_EXTERNAL_SYNC);
        event_clock_set_tick_interval(&u, 1000);
        event_clock_sync(&u, 1000000, CLOCK_IFACE_RTC);
        for (int i = 0; i < 1001; i++) (void) event_clock_next_ordinal(&u);
        r = event_clock_measure_drift(&u, 2000000);
        CHECK(r == 1000, "a valid span measures +1000 ppm");
        CHECK(event_clock_measure_drift(&u, 1000000) == EVENT_CLOCK_DRIFT_UNMEASURABLE,
              "a reference equal to last_sync_ns spans nothing -> UNMEASURABLE");
        CHECK(event_clock_measure_drift(&u, 999999) == EVENT_CLOCK_DRIFT_UNMEASURABLE,
              "a reference BEFORE last_sync_ns -> UNMEASURABLE, not a negative span");
        CHECK(u.drift_ppm == 1000,
              "and the good measurement survived both failed ones untouched");
    }

    /* ================= 10. corrections are validated ====================== */
    {
        event_clock_t k;
        uint64_t ord;
        event_clock_init(&k, CLOCK_EXTERNAL_SYNC);
        ord = event_clock_get_ordinal(&k);

        event_clock_apply_correction(&k, CORR(2, 5));
        CHECK(k.correction_q32 == CORR_ONE && k.event_ordinal == ord,
              "0.4 is below EVENT_CLOCK_CORRECTION_MIN -> rejected, no ordinal spent");
        event_clock_apply_correction(&k, CORR(21, 10));
        CHECK(k.correction_q32 == CORR_ONE, "2.1 is above the max -> rejected");
        event_clock_apply_correction(&k, 0);
        CHECK(k.correction_q32 == CORR_ONE, "0 (a stopped clock) is rejected");
        event_clock_apply_correction(&k, EVENT_CLOCK_CORRECTION_MIN - 1u);
        CHECK(k.correction_q32 == CORR_ONE, "one LSB below 0.5 is rejected");
        event_clock_apply_correction(&k, UINT64_MAX);
        CHECK(k.correction_q32 == CORR_ONE, "UINT64_MAX is rejected");
        event_clock_apply_correction(&k, EVENT_CLOCK_CORRECTION_MAX + 1u);
        CHECK(k.correction_q32 == CORR_ONE, "one LSB above 2.0 is rejected");
        CHECK(k.event_ordinal == ord, "six rejected corrections spent zero ordinals");

        event_clock_apply_correction(&k, EVENT_CLOCK_CORRECTION_MAX);
        CHECK(k.correction_q32 == CORR(2, 1) && k.event_ordinal == ord + 1,
              "2.0 is exactly at the limit -> accepted, one ordinal spent");
        event_clock_apply_correction(&k, CORR(2, 1));
        CHECK(k.event_ordinal == ord + 1, "re-applying the same factor is not an event");
        event_clock_apply_correction(&k, EVENT_CLOCK_CORRECTION_MIN);
        CHECK(k.correction_q32 == CORR(1, 2) && k.event_ordinal == ord + 2,
              "0.5 is exactly at the other limit -> accepted");
        CHECK(k.m5.r == 0.5, "M5 r tracks the correction factor");
    }

    /* ================= 11. legacy ticks ================================== */
    {
        event_clock_t g;
        event_clock_init(&g, CLOCK_EXTERNAL_SYNC);
        for (int i = 0; i < 100; i++) (void) event_clock_next_ordinal(&g);
        CHECK(event_clock_get_ticks(&g) == 0,
              "with no tick interval there is no period, so ticks are 0 — not the event count");

        event_clock_set_tick_interval(&g, 1000);
        CHECK(event_clock_get_ticks(&g) == 100, "100 events at 1000 ns/event = 100 whole ticks");
        CHECK(g.last_tick_ns == 100000ULL,
              "and the last tick boundary sits at 100,000 internal ns");

        for (int i = 0; i < 900; i++) (void) event_clock_next_ordinal(&g);
        CHECK(event_clock_get_ticks(&g) == 1000, "1000 events -> 1000 ticks");
        event_clock_apply_correction(&g, CORR(1, 2));
        CHECK(event_clock_get_ticks(&g) == 500, "a 0.5 correction halves the tick count to 500");
        CHECK(g.last_tick_ns == 500000ULL, "boundary follows: 500,000 ns");
        event_clock_set_tick_interval(&g, 0);
        CHECK(event_clock_get_ticks(&g) == 0, "clearing the interval takes the period away again");
    }

    /* ================= 12. the resync demand latches exactly ============== */
    {
        event_clock_t n;
        event_clock_init(&n, CLOCK_EXTERNAL_SYNC);
        CHECK(event_clock_needs_sync(&n) == false, "a fresh clock demands nothing");
        event_clock_request_sync(&n, CLOCK_IFACE_NETWORK);
        CHECK(event_clock_needs_sync(&n) == true && n.clock_active == true,
              "a request latches the demand and lights the interface");
        event_clock_sync(&n, 777, CLOCK_IFACE_NETWORK);
        CHECK(event_clock_needs_sync(&n) == false, "the sync clears the demand");
        CHECK(n.clock_active == false,
              "and CLOCK_EXTERNAL_SYNC puts the interface back to sleep afterwards");

        event_clock_init(&n, CLOCK_PERIODIC);
        event_clock_sync(&n, 0, CLOCK_IFACE_RTC);
        CHECK(n.clock_active == true, "a PERIODIC clock stays lit between syncs");
        for (uint64_t i = 0; i < EVENT_CLOCK_RESYNC_EVENTS - 1; i++)
            (void) event_clock_next_ordinal(&n);
        CHECK(event_clock_needs_sync(&n) == false,
              "999,999 events since the sync is still inside the resync window");
        (void) event_clock_next_ordinal(&n);
        CHECK(event_clock_needs_sync(&n) == true,
              "the 1,000,000th event latches the resync demand on its own");
        CHECK(event_clock_verify_coverage(&n),
              "a periodic clock demanding a resync is still a valid clock");
    }

    /* ================= 13. verify_coverage MUST be able to fail =========== */
    {
        event_clock_t h;

        make_healthy(&h);
        CHECK(event_clock_verify_coverage(&h), "the healthy baseline clock passes coverage");

        /* 1 */ make_healthy(&h);
        h.mode = (clock_mode_t) 9;
        CHECK(!event_clock_verify_coverage(&h), "FAILS on an out-of-range mode");

        /* 2 */ make_healthy(&h);
        h.last_sync_source = (clock_iface_t) 42;
        CHECK(!event_clock_verify_coverage(&h), "FAILS on an out-of-range sync source");

        /* 3 */ make_healthy(&h);
        h.local_counter = h.event_ordinal + 1;
        CHECK(!event_clock_verify_coverage(&h), "FAILS when the time base overtakes the ordinal");

        /* 4 */ make_healthy(&h);
        h.sync_counter = h.local_counter + 1;
        CHECK(!event_clock_verify_coverage(&h),
              "FAILS when the drift baseline is ahead of the time base");

        /* 5 */ make_healthy(&h);
        h.sync_count = 0;
        h.last_sync_ns = 0;
        h.last_sync_source = CLOCK_IFACE_NONE;
        h.m5.ell = 0.0;
        h.sync_counter = 7;
        CHECK(!event_clock_verify_coverage(&h),
              "FAILS on a never-synced clock that still holds a sync baseline");

        /* 6 */ make_healthy(&h);
        h.sync_count = 0;
        h.sync_counter = 0;
        h.last_sync_source = CLOCK_IFACE_NONE;
        h.m5.ell = 0.0;
        h.last_sync_ns = 12345;
        CHECK(!event_clock_verify_coverage(&h),
              "FAILS on a never-synced clock that still holds a sync timestamp");

        /* 7 */ make_healthy(&h);
        h.sync_count = 0;
        h.sync_counter = 0;
        h.last_sync_ns = 0;
        h.m5.ell = 0.0;
        CHECK(!event_clock_verify_coverage(&h),
              "FAILS on a never-synced clock that names a sync source");

        /* 8 */ make_healthy(&h);
        h.last_sync_source = CLOCK_IFACE_NONE;
        CHECK(!event_clock_verify_coverage(&h),
              "FAILS on a synced clock with no provenance for the sync");

        /* 9 */ make_healthy(&h);
        h.correction_q32 = 0;
        CHECK(!event_clock_verify_coverage(&h),
              "FAILS on a zero correction factor (a stopped clock)");

        /* 10 */ make_healthy(&h);
        h.correction_q32 = EVENT_CLOCK_CORRECTION_MAX + 1u;
        CHECK(!event_clock_verify_coverage(&h), "FAILS on a correction one LSB above 2.0");

        /* 11 */ make_healthy(&h);
        h.drift_ppm = -2000000;
        CHECK(!event_clock_verify_coverage(&h),
              "FAILS on a drift below -1e6 ppm, which no real span can produce");

        /* 12 */ make_healthy(&h);
        h.drift_ppm = EVENT_CLOCK_DRIFT_UNMEASURABLE;
        CHECK(!event_clock_verify_coverage(&h),
              "FAILS if the UNMEASURABLE sentinel ever leaks into stored drift");

        /* 13 */ make_healthy(&h);
        h.mode = CLOCK_DISABLED;
        h.m5.chi = (uint32_t) CLOCK_DISABLED;
        h.clock_active = true;
        h.needs_sync = false;
        CHECK(!event_clock_verify_coverage(&h), "FAILS on a DISABLED clock that is somehow active");

        /* 14 */ make_healthy(&h);
        h.mode = CLOCK_DISABLED;
        h.m5.chi = (uint32_t) CLOCK_DISABLED;
        h.clock_active = false;
        h.needs_sync = true;
        CHECK(!event_clock_verify_coverage(&h),
              "FAILS on a DISABLED clock holding a sync demand nothing can serve");

        /* 15 */ make_healthy(&h);
        h.mode = CLOCK_PERIODIC;
        h.m5.chi = (uint32_t) CLOCK_PERIODIC;
        h.clock_active = true;
        h.tick_interval_ns = 0;
        CHECK(!event_clock_verify_coverage(&h), "FAILS on a PERIODIC clock with no period");

        /* 16 */ make_healthy(&h);
        h.m5.omega = h.m5.omega + 1;
        CHECK(!event_clock_verify_coverage(&h),
              "FAILS on stale M5 omega — coverage checks state, it does not recompute it");

        /* 17 */ make_healthy(&h);
        h.m5.chi = 3;
        CHECK(!event_clock_verify_coverage(&h), "FAILS on stale M5 chi");

        /* 18 */ make_healthy(&h);
        h.m5.ell = 2.0;
        CHECK(!event_clock_verify_coverage(&h), "FAILS on M5 ell outside [0,1]");

        /* 19 */ make_healthy(&h);
        h.sync_count = 0;
        h.sync_counter = 0;
        h.last_sync_ns = 0;
        h.last_sync_source = CLOCK_IFACE_NONE;
        h.m5.ell = 0.5;
        CHECK(!event_clock_verify_coverage(&h),
              "FAILS on non-zero confidence with no reference ever received");

        /* and it still passes for every mode after a clean init */
        {
            int all = 1;
            clock_mode_t modes[4] = {CLOCK_DISABLED, CLOCK_EXTERNAL_SYNC, CLOCK_PERIODIC,
                                     CLOCK_HYBRID};
            for (int i = 0; i < 4; i++) {
                event_clock_t q;
                event_clock_init(&q, modes[i]);
                if (!event_clock_verify_coverage(&q)) all = 0;
            }
            CHECK(all, "a freshly initialised clock verifies in all four modes");
        }
    }

    /* ================= 14. NULL means the kernel default clock ============ */
    {
        event_clock_t *d;
        event_clock_init(NULL, CLOCK_EXTERNAL_SYNC);
        d = event_clock_default();
        CHECK(event_clock_get_ordinal(NULL) == 0,
              "event_clock_init(NULL, ...) really initialised the default clock");
        CHECK(event_clock_next_ordinal(NULL) == 1 && d->event_ordinal == 1,
              "and NULL routes to that same instance (arm32 boots this way)");
        event_clock_set_tick_interval(NULL, 1000);
        event_clock_sync(NULL, 5000, CLOCK_IFACE_RTC);
        CHECK(d->event_ordinal == 3 && d->sync_count == 1,
              "control events on the default clock land on ordinals 2 and 3");
        CHECK(event_clock_get_time(NULL) == 5000ULL,
              "get_time(NULL) reads the default clock's reference");
        CHECK(event_clock_get_mode(NULL) == CLOCK_EXTERNAL_SYNC,
              "get_mode(NULL) reads the default clock's mode");
        CHECK(event_clock_verify_coverage(NULL) == true,
              "verify_coverage(NULL) verifies the default clock");
        CHECK(event_clock_get_ticks(NULL) == 1, "and its tick count is the one event it has seen");
    }

    /* ================= 15. nanosecond arithmetic saturates ================ */
    {
        event_clock_t z;

        /* correction 1.0 -> the EXACT integer path: 1 event of
         * UINT64_MAX ns saturates the product at UINT64_MAX, and adding the
         * 1000 ns reference saturates again. */
        event_clock_init(&z, CLOCK_EXTERNAL_SYNC);
        event_clock_set_tick_interval(&z, UINT64_MAX);
        event_clock_sync(&z, 1000, CLOCK_IFACE_RTC);
        (void) event_clock_next_ordinal(&z);
        CHECK(event_clock_get_time(&z) == UINT64_MAX,
              "an exact-path span past UINT64_MAX saturates there, it does not wrap");

        /* correction != 1.0 -> the SCALED path, which saturates at
         * the lower EVENT_CLOCK_NS_CEILING: 1 x 1.8446744e19 x 0.5 =
         * 9.2233720e18 ns, over the 9e18 ceiling. */
        event_clock_init(&z, CLOCK_EXTERNAL_SYNC);
        event_clock_set_tick_interval(&z, UINT64_MAX);
        event_clock_apply_correction(&z, CORR(1, 2));
        event_clock_sync(&z, 1000, CLOCK_IFACE_RTC);
        (void) event_clock_next_ordinal(&z);
        CHECK(event_clock_get_time(&z) == 9000000000000001000ULL,
              "a scaled-path span past the ns ceiling clamps to 9e18 + the reference");

        event_clock_init(&z, CLOCK_EXTERNAL_SYNC);
        event_clock_set_tick_interval(&z, 1000000000000000000ULL);
        event_clock_sync(&z, UINT64_MAX - 10, CLOCK_IFACE_RTC);
        (void) event_clock_next_ordinal(&z);
        CHECK(event_clock_get_time(&z) == UINT64_MAX,
              "adding 1e18 ns to a near-max reference saturates at UINT64_MAX");

        event_clock_init(&z, CLOCK_EXTERNAL_SYNC);
        event_clock_set_tick_interval(&z, 3);
        z.local_counter = UINT64_MAX;
        CHECK(event_clock_get_ticks(&z) == 6148914691236517205ULL,
              "tick math saturates the ns product then divides: UINT64_MAX/3");
    }

    /* ===== 16. the projection is exact ABOVE 2^53, not just below it ======
     *
     * 2^53 + 1 = 9007199254740993 events of 100 ns is exactly
     * 900719925474099300 ns. (double)(2^53+1) rounds to 2^53, so a
     * double-arithmetic projection returns 900719925474099200 — off by
     * 100 ns while claiming nanosecond accuracy. This is the regression for
     * that: with correction 1.0 the span must go through the exact
     * integer path. */
    {
        event_clock_t e;
        event_clock_init(&e, CLOCK_EXTERNAL_SYNC);
        event_clock_set_tick_interval(&e, 100);
        event_clock_sync(&e, 0, CLOCK_IFACE_RTC);
        e.local_counter = 9007199254740993ULL; /* 2^53 + 1 */
        e.event_ordinal = e.local_counter + 8;
        e.m5.omega = (uint32_t) (e.event_ordinal & 0xFFFFFFFFu);
        CHECK(event_clock_get_time(&e) == 900719925474099300ULL,
              "(2^53+1) events x 100 ns projects EXACTLY, not to the nearest 256 ns");
        CHECK(event_clock_get_time(&e) != 900719925474099200ULL,
              "and specifically not to the double-rounded 900719925474099200");
        CHECK(event_clock_get_ticks(&e) == 9007199254740993ULL,
              "get_ticks agrees with the exact span: 2^53+1 whole ticks");
        CHECK(event_clock_verify_coverage(&e),
              "the clock is still coherent after the large-count projection");
    }

    /* ===== 17. defects this suite exists to keep fixed ====================
     *
     * Every case below was a live bug: a state the PUBLIC API could reach
     * that event_clock_verify_coverage() rejected, a parameter that was
     * validated and discarded, a counter that wrapped, or a derived field
     * nothing checked. A verify function that its own API can drive into a
     * false is not a verifier, it is a tripwire on the caller.
     */
    {
        event_clock_t v;

        /* --- 17a. removing the period of a period-driven mode ---------- */
        event_clock_init(&v, CLOCK_PERIODIC);
        CHECK(event_clock_verify_coverage(&v), "a fresh PERIODIC clock verifies");
        {
            uint64_t ord = event_clock_get_ordinal(&v);
            event_clock_set_tick_interval(&v, 0);
            CHECK(v.tick_interval_ns == EVENT_CLOCK_DEFAULT_TICK_NS,
                  "set_tick_interval(0) is REFUSED in PERIODIC — the period stands");
            CHECK(event_clock_get_ordinal(&v) == ord,
                  "and a refused interval change spends no ordinal");
            CHECK(event_clock_verify_coverage(&v),
                  "so a PERIODIC clock cannot be driven into a state coverage rejects");
        }
        event_clock_init(&v, CLOCK_HYBRID);
        event_clock_set_tick_interval(&v, 0);
        CHECK(v.tick_interval_ns == EVENT_CLOCK_DEFAULT_TICK_NS,
              "HYBRID refuses it too — both period-driven modes, not just PERIODIC");
        CHECK(event_clock_verify_coverage(&v), "and HYBRID still verifies");

        /* the same call IS allowed where there is no period to remove */
        event_clock_init(&v, CLOCK_EXTERNAL_SYNC);
        event_clock_set_tick_interval(&v, 1000);
        event_clock_set_tick_interval(&v, 0);
        CHECK(v.tick_interval_ns == 0,
              "CLOCK_EXTERNAL_SYNC has no period, so clearing it is accepted");

        /* a period-less HYBRID clock must FAIL coverage, symmetrically with
         * PERIODIC — this branch used to be blind to HYBRID entirely */
        make_healthy(&v);
        v.mode = CLOCK_HYBRID;
        v.m5.chi = (uint32_t) CLOCK_HYBRID;
        v.clock_active = true;
        v.tick_interval_ns = 0;
        CHECK(!event_clock_verify_coverage(&v),
              "FAILS on a HYBRID clock with no period, not only a PERIODIC one");

        /* a period-driven mode keeps its interface engaged; a dark one is
         * incoherent (only CLOCK_EXTERNAL_SYNC is allowed to be either) */
        make_healthy(&v);
        v.mode = CLOCK_PERIODIC;
        v.m5.chi = (uint32_t) CLOCK_PERIODIC;
        v.clock_active = false;
        CHECK(!event_clock_verify_coverage(&v),
              "FAILS on a PERIODIC clock whose interface is somehow dark");
        make_healthy(&v);
        v.mode = CLOCK_HYBRID;
        v.m5.chi = (uint32_t) CLOCK_HYBRID;
        v.clock_active = false;
        CHECK(!event_clock_verify_coverage(&v), "FAILS on a dark HYBRID clock as well");
        make_healthy(&v);
        CHECK(v.mode == CLOCK_EXTERNAL_SYNC && v.clock_active == false &&
                  event_clock_verify_coverage(&v),
              "but a dark CLOCK_EXTERNAL_SYNC clock is the normal idle case, and passes");

        /* --- 17b. request_sync records its source, it does not just check it */
        event_clock_init(&v, CLOCK_EXTERNAL_SYNC);
        CHECK(v.pending_sync_source == CLOCK_IFACE_NONE,
              "a fresh clock has no outstanding sync request");
        event_clock_request_sync(&v, CLOCK_IFACE_BLUETOOTH);
        CHECK(v.pending_sync_source == CLOCK_IFACE_BLUETOOTH,
              "request_sync RECORDS which interface was asked for, not just that one was");
        event_clock_request_sync(&v, (clock_iface_t) 250);
        CHECK(v.pending_sync_source == CLOCK_IFACE_BLUETOOTH,
              "a bogus follow-up request does not overwrite the real one");
        event_clock_sync(&v, 4242, CLOCK_IFACE_BLUETOOTH);
        CHECK(v.pending_sync_source == CLOCK_IFACE_NONE,
              "an accepted sync serves the request and clears it");

        event_clock_init(&v, CLOCK_HYBRID);
        event_clock_request_sync(&v, CLOCK_IFACE_USB);
        CHECK(v.pending_sync_source == CLOCK_IFACE_USB, "request pending in HYBRID");
        event_clock_set_mode(&v, CLOCK_DISABLED);
        CHECK(v.pending_sync_source == CLOCK_IFACE_NONE,
              "entering DISABLED drops a request nothing can serve");
        CHECK(event_clock_verify_coverage(&v),
              "and the disabled clock verifies rather than holding a dead request");

        event_clock_init(&v, CLOCK_DISABLED);
        event_clock_request_sync(&v, CLOCK_IFACE_NTP);
        CHECK(v.pending_sync_source == CLOCK_IFACE_NONE,
              "a request in DISABLED records nothing at all");

        make_healthy(&v);
        v.pending_sync_source = (clock_iface_t) 77;
        CHECK(!event_clock_verify_coverage(&v), "FAILS on an out-of-range pending sync source");
        make_healthy(&v);
        v.mode = CLOCK_DISABLED;
        v.m5.chi = (uint32_t) CLOCK_DISABLED;
        v.clock_active = false;
        v.needs_sync = false;
        v.pending_sync_source = CLOCK_IFACE_NTP;
        CHECK(!event_clock_verify_coverage(&v),
              "FAILS on a DISABLED clock holding an unserveable pending request");

        /* --- 17c. sync_count saturates instead of wrapping to "never synced" */
        event_clock_init(&v, CLOCK_EXTERNAL_SYNC);
        event_clock_sync(&v, 1000, CLOCK_IFACE_NTP);
        v.sync_count = 0xFFFFFFFFu;
        event_clock_sync(&v, 2000, CLOCK_IFACE_NTP);
        CHECK(v.sync_count == 0xFFFFFFFFu,
              "sync_count SATURATES at UINT32_MAX — it does not wrap to 0");
        CHECK(v.last_sync_ns == 2000ULL,
              "and the sync itself was still fully applied at saturation");
        CHECK(event_clock_verify_coverage(&v), "so a saturated clock is still a coherent clock");

        /* --- 17d. all five M5 axes are checked, not two ----------------- */
        make_healthy(&v);
        v.m5.r = SR_FROM_FLOAT(1.75);
        CHECK(!event_clock_verify_coverage(&v),
              "FAILS on a stale M5 r that no longer matches correction_q32");
        make_healthy(&v);
        v.m5.phi = SR_FROM_FLOAT(-0.25);
        CHECK(!event_clock_verify_coverage(&v),
              "FAILS on a stale M5 phi that no longer matches drift_ppm");
        make_healthy(&v);
        v.m5.ell = SR_FROM_FLOAT(0.75);
        CHECK(!event_clock_verify_coverage(&v),
              "FAILS on an in-range but WRONG M5 ell (0.75 with 0 ppm drift)");

        /* the honest positive control: a real correction really does move r */
        make_healthy(&v);
        event_clock_apply_correction(&v, CORR(7, 4));
        CHECK(v.m5.r == SR_FROM_FLOAT(1.75) && event_clock_verify_coverage(&v),
              "and a genuine correction updates m5.r in step, still verifying");

        /* --- 17e. m5.phi saturates so the Q32.32 target conversion is safe */
        event_clock_init(&v, CLOCK_EXTERNAL_SYNC);
        event_clock_set_tick_interval(&v, UINT64_MAX);
        event_clock_sync(&v, 0, CLOCK_IFACE_RTC);
        (void) event_clock_next_ordinal(&v);
        {
            int64_t huge = event_clock_measure_drift(&v, 1);
            CHECK(huge == INT64_MAX,
                  "a 1 ns reference span against a saturated local span saturates at INT64_MAX ppm");
            CHECK(v.drift_ppm == huge, "drift_ppm keeps the full (saturated) measurement");
            CHECK(v.m5.phi == SR_FROM_FLOAT(1.0e9),
                  "but m5.phi CLAMPS at 1e9 — |phi|*2^32 must stay inside int64 on target");
            CHECK(event_clock_verify_coverage(&v), "and the clamped-phi clock still verifies");
        }
    }

    /* ===== 17f. corrupt fields must give defined, bounded values ========
     *
     * There is no parser here to feed malformed bytes to, so the equivalent
     * hostile input is a poisoned field in the struct: a correction outside
     * [0.5, 2.0] and drift at the int64 edges. Each must produce a defined,
     * bounded value (no overflow, no out-of-range Q32.32). Run under UBSan.
     */
    {
        event_clock_t p;

        event_clock_init(&p, CLOCK_EXTERNAL_SYNC);
        event_clock_set_tick_interval(&p, 1000);
        event_clock_sync(&p, 7777, CLOCK_IFACE_RTC);
        for (int i = 0; i < 50; i++) (void) event_clock_next_ordinal(&p);
        p.correction_q32 = 0; /* a stopped clock */
        CHECK(event_clock_get_time(&p) == 7777ULL,
              "a zero correction yields a ZERO span, not garbage");

        p.correction_q32 = UINT64_MAX; /* ~2^32 x: 50,000 ns -> 50000*2^32 - 1 */
        CHECK(event_clock_get_time(&p) == 214748364799999ULL + 7777ULL,
              "a UINT64_MAX correction is exact Q32.32: floor(50000 * (2^64-1) / 2^32)");

        p.local_counter = 1000000000000ULL; /* 1e12 events x 1000 ns = 1e15 ns */
        p.event_ordinal = p.local_counter + 4;
        CHECK(event_clock_get_time(&p) == EVENT_CLOCK_NS_CEILING + 7777ULL &&
                  EVENT_CLOCK_NS_CEILING + 7777ULL == 9000000000000007777ULL,
              "an absurd correction clamps at the ns ceiling, not a wrapped value");

        /* drift_ppm poisoned in both directions: the M5 projection must stay
         * inside the Q32.32 range and inside [0,1] for ell */
        event_clock_init(&p, CLOCK_EXTERNAL_SYNC);
        event_clock_set_tick_interval(&p, 1000);
        event_clock_sync(&p, 0, CLOCK_IFACE_RTC);
        p.drift_ppm = INT64_MIN;
        (void) event_clock_next_ordinal(&p); /* forces a m5 refresh */
        CHECK(p.m5.phi == SR_FROM_FLOAT(-1.0e9),
              "a hugely NEGATIVE drift clamps m5.phi at -1e9, the other Q32.32 edge");
        CHECK(p.m5.ell >= SR_ZERO && p.m5.ell <= SR_ONE, "and m5.ell stays inside [0,1]");

        p.drift_ppm = INT64_MAX;
        (void) event_clock_next_ordinal(&p);
        CHECK(p.m5.phi == SR_FROM_FLOAT(1.0e9),
              "a hugely POSITIVE drift clamps m5.phi at +1e9");
        CHECK(p.m5.ell >= SR_ZERO && p.m5.ell < SR_FROM_FLOAT(0.001),
              "and confidence collapses toward 0 without leaving [0,1]");

        /* the zero-operand path of the saturating multiply */
        event_clock_init(&p, CLOCK_EXTERNAL_SYNC);
        event_clock_set_tick_interval(&p, 1000);
        CHECK(event_clock_get_ticks(&p) == 0 && p.last_tick_ns == 0,
              "zero events at a real interval is 0 ticks and a 0 ns boundary");

        /* the low half of the m5.ell range check in verify_coverage */
        make_healthy(&p);
        p.m5.ell = SR_FROM_FLOAT(-0.5);
        CHECK(!event_clock_verify_coverage(&p),
              "FAILS on a NEGATIVE M5 ell, not only one above 1.0");
    }

    /* ===== 18. every entry point really does accept NULL ==================
     *
     * The header says EVERY function resolves NULL to the default clock.
     * Section 14 proved it for nine of them; these are the other five. Each
     * is checked by an OBSERVABLE effect on the default instance, so a
     * silent no-op fails.
     */
    {
        event_clock_t *d;
        event_clock_init(NULL, CLOCK_PERIODIC);
        d = event_clock_default();

        event_clock_set_mode(NULL, CLOCK_HYBRID);
        CHECK(d->mode == CLOCK_HYBRID,
              "set_mode(NULL, ...) really changed the default clock's mode");
        event_clock_set_tick_interval(NULL, 1000); /* 1000 ns per event */

        event_clock_request_sync(NULL, CLOCK_IFACE_AUDIO);
        CHECK(d->pending_sync_source == CLOCK_IFACE_AUDIO,
              "request_sync(NULL, ...) latched a real request on the default clock");
        CHECK(event_clock_needs_sync(NULL) == true, "needs_sync(NULL) reads that same demand back");

        event_clock_sync(NULL, 1000000, CLOCK_IFACE_AUDIO);
        for (int i = 0; i < 1001; i++) (void) event_clock_next_ordinal(NULL);
        CHECK(event_clock_measure_drift(NULL, 2000000) == 1000,
              "measure_drift(NULL, ...) measured the default clock: +1000 ppm");

        event_clock_apply_correction(NULL, CORR(5, 4));
        CHECK(d->correction_q32 == CORR(5, 4),
              "apply_correction(NULL, ...) really wrote the default clock");
        CHECK(event_clock_verify_coverage(NULL),
              "the default clock survives all five NULL-routed mutators");

        /* and the resolver is a singleton, not a fresh object each call */
        CHECK(event_clock_default() == d,
              "event_clock_default() returns the SAME instance every time");
    }

    /* ===== 19. get_mode reports every mode, and set_mode is range-checked = */
    {
        event_clock_t g;
        clock_mode_t modes[4] = {CLOCK_DISABLED, CLOCK_EXTERNAL_SYNC, CLOCK_PERIODIC, CLOCK_HYBRID};
        int all = 1;
        for (int i = 0; i < 4; i++) {
            event_clock_init(&g, CLOCK_EXTERNAL_SYNC);
            event_clock_set_mode(&g, modes[i]);
            if (event_clock_get_mode(&g) != modes[i]) all = 0;
        }
        CHECK(all, "get_mode() reports back each of the four modes it was set to");

        event_clock_init(&g, CLOCK_HYBRID);
        {
            uint64_t ord = event_clock_get_ordinal(&g);
            uint64_t lc = g.local_counter;
            event_clock_set_mode(&g, (clock_mode_t) 123);
            CHECK(event_clock_get_mode(&g) == CLOCK_HYBRID,
                  "an out-of-range set_mode is refused — the mode is unchanged");
            CHECK(event_clock_get_ordinal(&g) == ord && g.local_counter == lc,
                  "and a refused mode change spends no ordinal and no time");
        }

        /* set_mode never resets a counter, in either direction */
        event_clock_init(&g, CLOCK_EXTERNAL_SYNC);
        for (int i = 0; i < 37; i++) (void) event_clock_next_ordinal(&g);
        event_clock_set_mode(&g, CLOCK_PERIODIC);
        event_clock_set_mode(&g, CLOCK_DISABLED);
        event_clock_set_mode(&g, CLOCK_HYBRID);
        CHECK(g.local_counter == 37 && event_clock_get_ordinal(&g) == 40,
              "three mode changes cost 3 ordinals and left the time base at 37");
    }

    /* ===== 20. the resync latch fires in HYBRID and never in EXTERNAL_SYNC */
    {
        event_clock_t y;

        event_clock_init(&y, CLOCK_HYBRID);
        event_clock_sync(&y, 0, CLOCK_IFACE_RTC);
        y.local_counter = y.sync_counter + EVENT_CLOCK_RESYNC_EVENTS - 1;
        y.event_ordinal = y.local_counter + 4;
        CHECK(event_clock_needs_sync(&y) == false,
              "HYBRID at 999,999 events past the sync is still inside the window");
        y.local_counter++;
        CHECK(event_clock_needs_sync(&y) == true,
              "HYBRID latches the resync demand at exactly 1,000,000 events too");

        event_clock_init(&y, CLOCK_EXTERNAL_SYNC);
        event_clock_set_tick_interval(&y, 1000);
        event_clock_sync(&y, 0, CLOCK_IFACE_RTC);
        y.local_counter = y.sync_counter + 10 * EVENT_CLOCK_RESYNC_EVENTS;
        y.event_ordinal = y.local_counter + 4;
        CHECK(event_clock_needs_sync(&y) == false,
              "CLOCK_EXTERNAL_SYNC never auto-latches — 10x the window, still quiet");
        CHECK(event_clock_needs_sync(&y) == false,
              "and asking twice does not manufacture a demand either");
    }

    /* ===== 21. wall time may step backwards; ordinals may not ============= */
    {
        event_clock_t b;
        uint64_t ord_before, t_before, t_after;

        event_clock_init(&b, CLOCK_EXTERNAL_SYNC);
        event_clock_set_tick_interval(&b, 1000);
        event_clock_sync(&b, 5000000000ULL, CLOCK_IFACE_NTP);
        for (int i = 0; i < 100; i++) (void) event_clock_next_ordinal(&b);
        t_before = event_clock_get_time(&b);
        ord_before = event_clock_get_ordinal(&b);
        CHECK(t_before == 5000100000ULL, "100 events past a 5 s reference: 5,000,100,000 ns");

        /* NTP steps the reference back by a full second */
        event_clock_sync(&b, 4000000000ULL, CLOCK_IFACE_NTP);
        t_after = event_clock_get_time(&b);
        CHECK(t_after == 4000000000ULL,
              "wall time really does step BACK with the reference (LIMITATION 4)");
        CHECK(t_after < t_before, "so get_time() is not monotonic, as documented");
        CHECK(event_clock_get_ordinal(&b) == ord_before + 1,
              "but the ordinal only went forward, by the one the sync spent");
        CHECK(event_clock_verify_coverage(&b), "and a backwards step leaves a coherent clock");
    }

    /* ===== 22. randomised API stress: no reachable state fails coverage ====
     *
     * This module has no parser and no array to overflow, so the analogue of
     * a fuzz corpus is a long random SEQUENCE of public calls, including
     * out-of-range enums, out-of-range corrections and extreme intervals. After
     * every single call three things must hold: the ordinal never decreases,
     * ordinals lead the time base, and verify_coverage() still passes. That
     * last one is the real assertion — it is what caught the PERIODIC
     * set_tick_interval(0) hole. Run under ASan+UBSan in CI.
     */
    {
        event_clock_t f;
        uint64_t seed = 0x9E3779B97F4A7C15ULL;
        uint64_t prev_ord = 0;
        int monotonic = 1, leads = 1, coherent = 1;
        long ops = 0;

        event_clock_init(&f, CLOCK_EXTERNAL_SYNC);

        for (int i = 0; i < 200000; i++) {
            uint64_t rnd;
            seed ^= seed << 13;
            seed ^= seed >> 7;
            seed ^= seed << 17;
            rnd = seed;

            switch (rnd % 9u) {
            case 0:
                (void) event_clock_next_ordinal(&f);
                break;
            case 1:
                event_clock_sync(&f, (rnd >> 8) % 100000000ULL,
                                 (clock_iface_t) ((rnd >> 32) % 256u));
                break;
            case 2:
                event_clock_set_mode(&f, (clock_mode_t) ((rnd >> 32) % 8u));
                break;
            case 3:
                event_clock_request_sync(&f, (clock_iface_t) ((rnd >> 32) % 256u));
                break;
            case 4: {
                /* a spread that straddles both limits, plus the edges */
                uint64_t corr;
                switch ((rnd >> 40) % 6u) {
                case 0:
                    corr = 0;
                    break;
                case 1:
                    corr = UINT64_MAX;
                    break;
                case 2:
                    corr = EVENT_CLOCK_CORRECTION_MIN - 1u;
                    break;
                case 3:
                    corr = CORR(250u + (rnd >> 16) % 2000u, 1000u);
                    break;
                case 4:
                    corr = EVENT_CLOCK_CORRECTION_MAX + 1u;
                    break;
                default:
                    corr = CORR_ONE;
                    break;
                }
                event_clock_apply_correction(&f, corr);
                break;
            }
            case 5:
                event_clock_set_tick_interval(
                    &f, ((rnd >> 24) % 4u == 0) ? 0 : (rnd >> 24) % 1000000000ULL);
                break;
            case 6:
                (void) event_clock_get_time(&f);
                break;
            case 7:
                (void) event_clock_get_ticks(&f);
                break;
            default:
                (void) event_clock_measure_drift(&f, (rnd >> 8) % 100000000ULL);
                break;
            }
            ops++;

            if (f.event_ordinal < prev_ord) monotonic = 0;
            if (f.event_ordinal < f.local_counter) leads = 0;
            if (!event_clock_verify_coverage(&f)) coherent = 0;
            prev_ord = f.event_ordinal;
        }
        CHECK(ops == 200000, "the stress loop really executed 200,000 operations");
        CHECK(monotonic, "200,000 random API calls never stepped the ordinal backwards");
        CHECK(leads, "and ordinals led the time base at every one of them");
        CHECK(coherent, "and NO sequence of public calls reached a state coverage rejects");
        CHECK(f.event_ordinal > 20000,
              "the stress actually exercised the clock (>20,000 ordinals spent)");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    (void) U(0);
    return failures ? 1 : 0;
}
