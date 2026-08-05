/* lattice_scheduler.h — OS Lattice Layer: Scale-Generic Scheduler
 * Single implementation parameterized by scale factor s.
 * s=1 (Normal), s=phi (Quantum), s=phi^2 (Post-Quantum).
 * Per OS_LATTICE_LAYER_SPEC.md §3 — no #ifdef per-layer duplication.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef LATTICE_SCHEDULER_H
#define LATTICE_SCHEDULER_H

#include "lattice_core.h"

typedef enum {
    SCHED_EVENT_ARRIVAL = 0,
    SCHED_ATTESTATION_RESOLVED = 1,
    SCHED_THRESHOLD_CROSSED = 2,
    SCHED_DIMENSIONAL_LIFT = 3,
    SCHED_CHOICE_COLLAPSE = 4
} sched_event_type_t;

typedef struct sched_event {
    sched_event_type_t type;
    uint32_t source_node_idx;
    uint32_t target_node_idx;
    ordinal_t ordinal;
    rational_t magnitude;
    trit_t attestation;
    phase_t phase;
    collapse_t choice;
    double complex telemetry_value;
} sched_event_t;

typedef struct lattice_scheduler {
    lattice_graph_t *graph;
    sched_event_t event_queue[256];
    uint32_t queue_head;
    uint32_t queue_tail;
    uint32_t queue_count;
    ordinal_t current_ordinal;
    uint32_t paradox_level;
    double coverage_hyperbola;
    bool converged;
} lattice_scheduler_t;

void lattice_sched_init(lattice_scheduler_t *s, lattice_graph_t *g);
bool lattice_sched_enqueue(lattice_scheduler_t *s, const sched_event_t *event);
bool lattice_sched_pending(const lattice_scheduler_t *s);
int lattice_sched_tick(lattice_scheduler_t *s, phase_tick_t *tick_out);
uint32_t lattice_sched_state_count(const lattice_scheduler_t *s);
bool lattice_sched_check_bound(const lattice_scheduler_t *s);
void lattice_sched_resolve_shadow(lattice_scheduler_t *s, double complex shadow, uint32_t level);

#endif
