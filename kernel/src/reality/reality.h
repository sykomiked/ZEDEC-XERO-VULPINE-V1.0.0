/* reality.h — The Reality Engine: sigil circuits run against live variables
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 *
 * WHAT THIS IS
 * -----------
 * src/cards/sigil.* recovers, from a drawn spell shape, a real dataflow
 * circuit: nodes on a star polygon {N/k} whose arithmetic (gcd(N,k)) sets how
 * many INDEPENDENT PARALLEL LANES the process has, and a dependency schedule of
 * waves. Until now that circuit computed over nothing — it was a topology.
 *
 * The Reality Engine BINDS the circuit's input nodes to LIVE VARIABLES — a
 * sensor reading, a pushed sample, a synthetic signal — and EVALUATES the whole
 * circuit once per PHASE TICK. The nonlinear combination the shape describes is
 * then computed continuously against whatever the machine can sense. A sigil
 * stops being a diagram and becomes a running instrument that reacts to the
 * world: cross a threshold on an output node and a reaction fires.
 *
 * This is a capability the architecture uniquely affords, because the sigil IS
 * the program and the phase sequence IS the clock. The same drawn figure that a
 * practitioner chose for its symmetry is a scheduling policy AND, now, a live
 * signal-processing graph.
 *
 * WHAT IS REAL NOW vs WHAT NEEDS HARDWARE
 * --------------------------------------
 * Synthetic sources (constant, ramp, oscillator, sequence) and PUSHED samples
 * are fully real and testable now — the circuit math is the sigil model, which
 * is already tested. A REAL sensor needs a driver, so sensor sources go through
 * an ops boundary (reality_sensor_ops_t): with no backend bound, a sensor read
 * returns "not bound" and the engine flags it — it never invents a reading.
 * When a real driver is written (a thermometer, an ADC, an RNG, a NIC counter),
 * it registers as the ops backend and the same sigil runs against real signal.
 *
 * Evaluation is INTEGER (scaled fixed values, "milli-units" by convention). No
 * float, no allocation, freestanding.
 */
#ifndef ZXV_REALITY_H
#define ZXV_REALITY_H

#include <stdint.h>
#include <stdbool.h>
#include "../cards/sigil.h"

#define REALITY_MAX_SOURCES   16u
#define REALITY_MAX_REACTIONS 16u

/* A live value. Scaled integer; callers pick the scale (e.g. milli-units). */
typedef int32_t reality_val_t;

/* ---- how a source produces its value each tick ---- */
typedef enum {
    REAL_SRC_CONST = 0, /* p0                                                  */
    REAL_SRC_RAMP,      /* p0 + p1*ordinal                                     */
    REAL_SRC_OSC,       /* triangle wave, amplitude p0, period p1 (in ticks)   */
    REAL_SRC_SEQ,       /* seq[ordinal % seq_len]                              */
    REAL_SRC_FEED,      /* whatever was last pushed with reality_feed()        */
    REAL_SRC_SENSOR     /* read from the hardware ops backend, or NOT-BOUND    */
} reality_src_kind_t;

typedef struct {
    reality_src_kind_t kind;
    bool     in_use;
    int32_t  p0, p1;                 /* parameters (see kinds above)           */
    const reality_val_t *seq;        /* for REAL_SRC_SEQ                       */
    uint32_t seq_len;
    uint32_t sensor_id;              /* for REAL_SRC_SENSOR                    */
    reality_val_t value;             /* last sampled value                     */
    bool     valid;                  /* false if a sensor read was not bound   */
} reality_source_t;

/* ---- what a non-input node computes from its earlier-wave neighbours ---- */
typedef enum {
    REAL_OP_INPUT = 0,  /* value is set from a bound source, not computed      */
    REAL_OP_SUM,        /* sum of predecessor values                          */
    REAL_OP_MAX,
    REAL_OP_MIN,
    REAL_OP_MEAN,       /* integer mean                                       */
    REAL_OP_DIFF,       /* first predecessor minus the rest                   */
    REAL_OP_ABS,        /* abs(sum)                                           */
    REAL_OP_THRESH      /* NONLINEAR gate: (sum >= p_thresh) ? p_hi : 0        */
} reality_op_t;

/* Real-hardware sensor backend. read() fills *out and returns 0 on success,
 * non-zero if it cannot read (which the engine treats as NOT-BOUND for that
 * sample). With ops==NULL, every sensor source is NOT-BOUND. */
typedef struct {
    int (*read)(uint32_t sensor_id, reality_val_t *out, void *ctx);
    void *ctx;
} reality_sensor_ops_t;

/* comparison for a reaction trigger */
typedef enum { REAL_CMP_GT=0, REAL_CMP_GE, REAL_CMP_LT, REAL_CMP_LE, REAL_CMP_EQ } reality_cmp_t;

typedef void (*reality_react_fn)(uint8_t node, reality_val_t value, void *ctx);

typedef struct {
    bool     in_use;
    uint8_t  node;
    reality_cmp_t cmp;
    reality_val_t threshold;
    reality_react_fn fn;
    void    *ctx;
    bool     armed;      /* edge-trigger: fire on the rising edge of the cond  */
} reality_reaction_t;

typedef struct {
    const sigil_t *sig;
    sig_schedule_t sched;
    bool     scheduled;

    uint8_t  node_wave[SIG_MAX_NODES];      /* which wave each node fires in    */
    reality_op_t op[SIG_MAX_NODES];         /* node operation                   */
    reality_val_t val[SIG_MAX_NODES];       /* current node value               */
    bool     node_valid[SIG_MAX_NODES];     /* was this value from valid data?  */
    int8_t   bound[SIG_MAX_NODES];          /* source index bound to node, -1   */
    int32_t  thresh_p[SIG_MAX_NODES];       /* p_thresh for REAL_OP_THRESH      */
    int32_t  thresh_hi[SIG_MAX_NODES];      /* p_hi for REAL_OP_THRESH          */

    reality_source_t   src[REALITY_MAX_SOURCES];
    reality_reaction_t rx[REALITY_MAX_REACTIONS];
    reality_sensor_ops_t ops;

    uint64_t last_ordinal;
    uint32_t ticks;
} reality_engine_t;

/* ---- lifecycle ---- */
void reality_init(reality_engine_t *e, const sigil_t *s);
void reality_set_sensor_ops(reality_engine_t *e, const reality_sensor_ops_t *ops);

/* ---- sources ---- */
int32_t reality_add_const (reality_engine_t *e, reality_val_t v);
int32_t reality_add_ramp  (reality_engine_t *e, int32_t base, int32_t per_tick);
int32_t reality_add_osc   (reality_engine_t *e, int32_t amplitude, int32_t period);
int32_t reality_add_seq   (reality_engine_t *e, const reality_val_t *seq, uint32_t len);
int32_t reality_add_feed  (reality_engine_t *e, reality_val_t initial);
int32_t reality_add_sensor(reality_engine_t *e, uint32_t sensor_id);
/* Push a new sample into a FEED source (a driver or app calls this). */
bool    reality_feed(reality_engine_t *e, int32_t source, reality_val_t v);

/* ---- wiring the circuit ---- */
/* Bind a source to a node, making it an input. */
bool reality_bind(reality_engine_t *e, uint8_t node, int32_t source);
/* Set a node's operation. THRESH also needs its two parameters. */
bool reality_set_op(reality_engine_t *e, uint8_t node, reality_op_t op);
bool reality_set_thresh(reality_engine_t *e, uint8_t node, int32_t thresh, int32_t hi);

/* ---- reactions ---- */
int32_t reality_on(reality_engine_t *e, uint8_t node, reality_cmp_t cmp,
                   reality_val_t threshold, reality_react_fn fn, void *ctx);

/* ---- run ---- */
/* Sample every source at `ordinal`, evaluate the whole circuit wave by wave,
 * and fire any reactions whose condition just became true. Returns the number
 * of nodes evaluated, or 0 on error. Drive this from the phase sequence. */
uint32_t reality_tick(reality_engine_t *e, uint64_t ordinal);

/* Read a node's current value. */
reality_val_t reality_read(const reality_engine_t *e, uint8_t node);
/* True if a node's value this tick came from valid data (a sensor that was
 * bound, or any non-sensor source). */
bool reality_node_valid(const reality_engine_t *e, uint8_t node);

/* How many independent parallel lanes the reality computation runs in — this
 * is gcd(N,k) of the sigil's fabric, straight from the shape. */
uint32_t reality_lanes(const reality_engine_t *e);

#endif /* ZXV_REALITY_H */
