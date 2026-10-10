/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* stream_sim.h — a deterministic discrete-time network simulator for the
 * swarm scheduler (stream_swarm.h) and a client-server baseline.
 *
 * MODEL. Time advances in ticks (default 100 per second). One origin with a
 * limited uplink produces one segment (nfr freights of k rows) every
 * seg_ticks and runs the real ssw_* code as a node. N viewers join over a
 * window, each with a declared upload and download cap drawn from four
 * classes, an access latency (one-way link latency = sum of both ends),
 * independent loss per piece message (8 rows; requests, rejects, notices
 * and adverts are not lost: they model a reliable control stream), optional
 * departures (churn, with rejoin as a fresh viewer) and optional
 * zero-upload free riders. Neighbour graph: every joiner first takes a free
 * origin slot (origin_slots), then picks random viewers until it has
 * `degree` neighbours; leavers are replaced by rewiring. Every message is
 * an event with its link's latency; adverts are built and parsed with the
 * real wire codec: a delta every 10 ticks, a full advert every 2 s and to
 * every new neighbour. Control bytes are counted but NOT charged to the
 * upload or download caps (only pieces are), so the caps model the data
 * path only. An optional
 * PROBE joins late in VOD (start-over) mode at segment 0 with its own
 * download cap: its catch-up rate is the "achievable rate" of a viewer for
 * content the swarm already holds.
 *
 * The simulator checks the scheduler independently of the scheduler's own
 * bookkeeping: per-node sliding one-second upload totals against the
 * declared cap, ids requested twice, requests for freights already
 * complete, and duplicate row arrivals.
 *
 * The client-server baseline (ssim_run_cs) gives every viewer a fair share
 * of the same origin uplink with the same latency, loss, download caps and
 * playback clock, and no peer exchange.
 *
 * Deterministic: the same parameters give the same result on every
 * platform (integer only, xorshift PRNG, no clock). Freestanding like the
 * library (no libc, no division); memory is one caller arena. Results are
 * raw counters; the caller turns them into rates.
 */
#ifndef STREAM_SIM_H
#define STREAM_SIM_H

#include <stddef.h>
#include <stdint.h>
#include "stream_swarm.h"

#define SSIM_MAX_VIEWERS 160
#define SSIM_MAX_CHURN   160

typedef struct {
    uint32_t n_viewers; /* audience, 0..SSIM_MAX_VIEWERS */
    uint64_t seed;
    uint32_t ticks;     /* run length */
    uint32_t tps;       /* ticks per second */
    uint32_t seg_ticks; /* segment duration */
    uint8_t k, nfr;     /* freight geometry of each segment */
    uint32_t origin_up; /* origin uplink, rows/s */
    uint32_t up[4];     /* viewer upload classes, rows/s (25% / 50% / 25% via class 0,1|2,3) */
    uint32_t dn[4];     /* viewer download classes, rows/s */
    uint32_t lat_min, lat_max;   /* access latency, ticks (link = sum of both ends) */
    uint32_t loss_ppm;           /* loss per piece message (a row in the baseline), ppm */
    uint32_t join_from, join_to; /* audience join window, ticks */
    uint32_t degree;             /* target neighbours per viewer */
    uint32_t origin_slots;       /* origin neighbours (push targets) */
    uint32_t free_rider_pct;     /* % of viewers with zero upload */
    uint8_t probe;               /* add a start-over joiner */
    uint32_t probe_tick, probe_dn, probe_up, probe_segs, probe_start;
    uint32_t churn_leaves; /* departures in [churn_from, churn_to) */
    uint32_t churn_from, churn_to;
    uint8_t churn_rejoin; /* each leaver is replaced by a fresh joiner */
    uint16_t n_push;
    uint8_t push_fanout;
    uint8_t live_back, startup_segs;
    uint32_t urgent_ticks, rescue_ticks;
} ssim_params_t;

typedef struct {
    /* audience (every viewer session, departed ones included) */
    uint32_t sessions, started, not_started;
    uint64_t useful_rows; /* rows of completed freights (k per freight) */
    uint64_t rx_rows;     /* all rows received */
    uint64_t active_ticks;
    uint64_t startup_ticks_sum;
    uint32_t startup_max;
    uint32_t stalls, stalled_sessions;
    uint32_t zero_up_sessions, zero_up_stalled; /* free riders */
    uint64_t stall_ticks;
    uint64_t played_segs;
    /* load */
    uint64_t origin_up_rows, peer_up_rows, probe_up_rows;
    uint64_t lost_rows;
    /* probe */
    uint8_t probe_done, probe_started;
    uint32_t probe_done_ticks, probe_startup_ticks;
    uint64_t probe_rx_rows;
    /* control traffic, bytes */
    uint64_t advert_bytes, request_bytes, notice_bytes;
    /* invariants (must all be zero) */
    uint64_t cap_violations, dn_violations, req_twice, req_after_done, dup_rx, event_overflow;
    uint64_t timeouts, late_rows, rejects;
    uint64_t freights_completed;
} ssim_result_t;

typedef struct ssim ssim_t;

void ssim_defaults(ssim_params_t *p);
size_t ssim_arena_bytes(const ssim_params_t *p);
/* Build the simulation in arena (16-byte aligned, ssim_arena_bytes long). */
ssim_t *ssim_create(const ssim_params_t *p, void *arena, size_t len);
int ssim_run(ssim_t *s, ssim_result_t *r);
/* After a run: node i (1..n for the audience, n+1 for the probe) or 0. */
const ssw_node_t *ssim_node(const ssim_t *s, uint32_t i);
bool ssim_active(const ssim_t *s, uint32_t i);
uint32_t ssim_produced(const ssim_t *s);

/* Client-server baseline. */
size_t ssim_cs_arena_bytes(const ssim_params_t *p);
int ssim_run_cs(const ssim_params_t *p, void *arena, size_t len, ssim_result_t *r);

#endif /* STREAM_SIM_H */
