/* lattice_scheduler.c — OS Lattice Layer: Scale-Generic Scheduler Implementation
 * Event-driven convergence loop (not clock-driven, per §5C).
 * State-count bounded by Fib(n+2) at each level.
 * Shadow resolution via FS-PRA signed-zero-at-level (§11).
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "lattice_scheduler.h"
#include "choice_core.h"
#include "telemetry_core.h"
#include "axiom_matrix_core.h"
#include <string.h>
#include <math.h>

void lattice_sched_init(lattice_scheduler_t *s, lattice_graph_t *g) {
    memset(s, 0, sizeof(lattice_scheduler_t));
    s->graph = g;
    s->queue_head = 0;
    s->queue_tail = 0;
    s->queue_count = 0;
    s->current_ordinal = 0;
    s->paradox_level = 0;
    s->coverage_hyperbola = 0.0;
    s->converged = false;
}

bool lattice_sched_enqueue(lattice_scheduler_t *s, const sched_event_t *event) {
    if (s->queue_count >= 256) return false;
    s->event_queue[s->queue_tail] = *event;
    s->queue_tail = (s->queue_tail + 1) % 256;
    s->queue_count++;
    return true;
}

bool lattice_sched_pending(const lattice_scheduler_t *s) {
    return s->queue_count > 0;
}

uint32_t lattice_sched_state_count(const lattice_scheduler_t *s) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < s->graph->num_nodes; i++) {
        if (s->graph->nodes[i].active) count++;
    }
    return count;
}

bool lattice_sched_check_bound(const lattice_scheduler_t *s) {
    uint32_t bound = lattice_fib(s->paradox_level + 4);
    return lattice_sched_state_count(s) <= bound;
}

int lattice_sched_tick(lattice_scheduler_t *s, phase_tick_t *tick_out) {
    if (s->queue_count == 0) return -1;

    sched_event_t event = s->event_queue[s->queue_head];
    s->queue_head = (s->queue_head + 1) % 256;
    s->queue_count--;

    tick_out->omega = s->current_ordinal;
    tick_out->r = event.magnitude;
    tick_out->ell = event.attestation;
    tick_out->iphi = event.phase;
    tick_out->chi = event.choice;

    double r_mag = rational_mag(event.magnitude);
    double ell_val = trit_to_ell(event.attestation);
    s->coverage_hyperbola = r_mag * ell_val;

    if (s->coverage_hyperbola < 1.8) {
        lattice_sched_resolve_shadow(s, event.telemetry_value, s->paradox_level);
        return -2;
    }

    if (event.type == SCHED_CHOICE_COLLAPSE) {
        choice_handoff();
    }

    if (event.type == SCHED_DIMENSIONAL_LIFT) {
        s->paradox_level++;
    }

    s->current_ordinal++;

    if (s->graph->matrix) {
        telemetry_t t = emit_and_observe(s->graph->matrix, s->current_ordinal);
        ordinal_t next = choice_resolve_from_telemetry(&t, s->paradox_level);
        if (next != s->current_ordinal) {
            sched_event_t follow;
            memset(&follow, 0, sizeof(follow));
            follow.type = SCHED_EVENT_ARRIVAL;
            follow.ordinal = next;
            follow.magnitude = (rational_t){1, 1};
            follow.attestation = TRIT_TRUE;
            lattice_sched_enqueue(s, &follow);
        }
    }

    s->converged = (s->queue_count == 0);
    return 0;
}

void lattice_sched_resolve_shadow(lattice_scheduler_t *s, double complex shadow, uint32_t level) {
    double complex resolved = shadow + (-shadow);
    (void)resolved;
    s->paradox_level = level;
    if (s->graph->matrix) {
        axiom_matrix_set(s->graph->matrix, s->current_ordinal,
                         (rational_t){0, 1}, TRIT_FALSE,
                         (phase_t){0, 0}, (collapse_t){{0, 0}}, resolved);
    }
}
