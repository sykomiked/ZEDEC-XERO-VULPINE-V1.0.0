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
 * License: SEL-3.3 + CC BY 4.0 + OPL v1.1
 * 36N9 Genetics, LLC
 */
#ifndef EVENT_CLOCK_H
#define EVENT_CLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"

/* ===== Clock modes ===== */
typedef enum {
    CLOCK_DISABLED = 0,     /* No clock — pure event-driven */
    CLOCK_EXTERNAL_SYNC,    /* Sync only on external interface */
    CLOCK_PERIODIC,         /* Traditional periodic (fallback compat) */
    CLOCK_HYBRID,           /* Event-driven with periodic fallback */
} clock_mode_t;

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

/* ===== Event clock state ===== */
typedef struct {
    clock_mode_t mode;

    /* External time (only updated on sync) */
    uint64_t external_time_ns;
    uint64_t last_sync_ns;
    clock_iface_t last_sync_source;

    /* Internal event sequence (ordinal-based, not time-based) */
    uint64_t event_ordinal;     /* Monotonically increasing event counter */
    uint64_t local_counter;     /* Free-running counter */

    /* Sync tracking */
    bool needs_sync;
    bool clock_active;
    uint32_t sync_count;
    double drift_ppm;           /* Measured drift */
    double correction_factor;   /* Applied correction */

    /* Periodic fallback (for compatibility) */
    uint64_t tick_interval_ns;
    uint64_t last_tick_ns;

    /* M5 coordinates */
    m5_coords_t m5;
} event_clock_t;

/* ===== API ===== */
void event_clock_init(event_clock_t *clk, clock_mode_t mode);

/* Event sequence (primary timekeeping) */
uint64_t event_clock_next_ordinal(event_clock_t *clk);
uint64_t event_clock_get_ordinal(event_clock_t *clk);

/* External sync (only when needed) */
void event_clock_sync(event_clock_t *clk, uint64_t external_ns, clock_iface_t source);
uint64_t event_clock_get_time(event_clock_t *clk);
bool event_clock_needs_sync(event_clock_t *clk);
void event_clock_request_sync(event_clock_t *clk, clock_iface_t source);

/* Mode management */
void event_clock_set_mode(event_clock_t *clk, clock_mode_t mode);
clock_mode_t event_clock_get_mode(event_clock_t *clk);

/* Drift measurement */
double event_clock_measure_drift(event_clock_t *clk, uint64_t reference_ns);
void event_clock_apply_correction(event_clock_t *clk, double correction);

/* Compatibility (for legacy code that expects ticks) */
uint64_t event_clock_get_ticks(event_clock_t *clk);
void event_clock_set_tick_interval(event_clock_t *clk, uint64_t interval_ns);

/* Coverage */
bool event_clock_verify_coverage(event_clock_t *clk);

#endif /* EVENT_CLOCK_H */
