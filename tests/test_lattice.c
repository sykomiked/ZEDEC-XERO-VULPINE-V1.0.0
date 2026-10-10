/* test_lattice.c — OS Lattice Layer Tests
 * Tests scale-generic scheduler at s=1, s=phi, s=phi^2.
 * Verifies phyllotactic positioning, Fibonacci bounds, IPC routing.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "lattice_core.h"
#include "lattice_scheduler.h"
#include "lattice_ipc.h"
#include "axiom_matrix_core.h"

#define PHI 1.61803398874989484820
#define PHI2 2.61803398874989484820

static void test_fibonacci(void) {
    assert(lattice_fib(0) == 0);
    assert(lattice_fib(1) == 1);
    assert(lattice_fib(2) == 1);
    assert(lattice_fib(3) == 2);
    assert(lattice_fib(4) == 3);
    assert(lattice_fib(5) == 5);
    assert(lattice_fib(6) == 8);
    assert(lattice_fib(7) == 13);
    assert(lattice_fib(10) == 55);
    printf("  [PASS] Fibonacci sequence\n");
}

static void test_position(void) {
    double complex pos0 = lattice_position(0, 0);
    assert(cabs(pos0) > 0.99 && cabs(pos0) < 1.01);
    double complex pos1 = lattice_position(1, 0);
    assert(cabs(pos1) > 1.5 && cabs(pos1) < 1.7);
    double complex pos2 = lattice_position(2, 0);
    assert(cabs(pos2) > 2.5 && cabs(pos2) < 2.7);
    printf("  [PASS] Phyllotactic positioning (radius grows by phi per level)\n");
}

static void test_scale_generic(double s, const char *name) {
    axiom_matrix_t matrix;
    matrix.size = 1024;
    zxv_cq16_t entries[1024];
    memset(entries, 0, sizeof(entries));
    matrix.entries = entries;

    /* static: the state is larger than the default 8 MB stack */
    static lattice_graph_t graph;
    memset(&graph, 0, sizeof graph);
    lattice_init(&graph, s, &matrix);

    lattice_node_id_t id0 = {0, 5};
    lattice_node_id_t id1 = {1, 5};
    lattice_node_id_t id2 = {2, 5};

    uint32_t n0 = lattice_add_node(&graph, 0, id0);
    uint32_t n1 = lattice_add_node(&graph, 1, id1);
    uint32_t n2 = lattice_add_node(&graph, 2, id2);

    assert(n0 == 0 && n1 == 1 && n2 == 2);
    assert(graph.num_nodes == 3);
    assert(graph.scale_factor == s);

    lattice_add_edge(&graph, n0, n1, EDGE_ORDINAL, (rational_t){1, 1}, TRIT_TRUE, (phase_t){0, 0}, (collapse_t){{0, 0}});
    lattice_add_edge(&graph, n1, n2, EDGE_RATIONAL, (rational_t){2, 1}, TRIT_TRUE, (phase_t){1, 0}, (collapse_t){{1, 0}});
    lattice_add_edge(&graph, n0, n2, EDGE_PHASE, (rational_t){1, 1}, TRIT_TRUE, (phase_t){0, 1}, (collapse_t){{0, 1}});

    assert(graph.nodes[0].num_edges == 2);
    assert(graph.nodes[1].num_edges == 1);

    lattice_scheduler_t sched;
    lattice_sched_init(&sched, &graph);

    assert(lattice_sched_check_bound(&sched));

    sched_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = SCHED_EVENT_ARRIVAL;
    ev.source_node_idx = n0;
    ev.target_node_idx = n1;
    ev.ordinal = 0;
    ev.magnitude = (rational_t){2, 1};
    ev.attestation = TRIT_TRUE;
    ev.telemetry_value = 1.0 + 0.5 * I;

    assert(lattice_sched_enqueue(&sched, &ev));
    assert(lattice_sched_pending(&sched));

    phase_tick_t tick;
    int rc = lattice_sched_tick(&sched, &tick);
    assert(rc == 0);
    assert(tick.omega == 0);
    assert(rational_mag(tick.r) == 2.0);

    lattice_ipc_t ipc;
    lattice_ipc_init(&ipc, &graph);
    int irc = lattice_ipc_send(&ipc, n0, n1, EDGE_ORDINAL, "hello", 5);
    assert(irc == 0);
    assert(lattice_ipc_msg_count(&ipc) == 1);

    lattice_msg_t *msg = lattice_ipc_recv(&ipc, n1);
    assert(msg != NULL);
    assert(msg->src_idx == n0);
    assert(msg->dst_idx == n1);
    assert(msg->payload_len == 5);
    assert(memcmp(msg->payload, "hello", 5) == 0);

    printf("  [PASS] Scale s=%.6f (%s): nodes, edges, scheduler, IPC\n", s, name);
}

static void test_ipc_typed(void) {
    axiom_matrix_t matrix;
    matrix.size = 256;
    zxv_cq16_t entries[256];
    memset(entries, 0, sizeof(entries));
    matrix.entries = entries;

    /* static: the state is larger than the default 8 MB stack */
    static lattice_graph_t graph;
    memset(&graph, 0, sizeof graph);
    lattice_init(&graph, 1.0, &matrix);

    uint32_t n0 = lattice_add_node(&graph, 0, (lattice_node_id_t){0, 5});
    uint32_t n1 = lattice_add_node(&graph, 1, (lattice_node_id_t){1, 5});

    lattice_add_edge(&graph, n0, n1, EDGE_CHOICE, (rational_t){1, 1}, TRIT_TRUE, (phase_t){1, 1}, (collapse_t){{0, 0}});

    lattice_ipc_t ipc;
    lattice_ipc_init(&ipc, &graph);

    int rc = lattice_ipc_send_typed(&ipc, n0, n1, EDGE_CHOICE,
                                     42, (rational_t){3, 2}, TRIT_TRUE,
                                     (phase_t){1, 1}, (collapse_t){{0, 0}},
                                     2.5 + 1.5 * I);
    assert(rc == 0);
    lattice_msg_t *msg = lattice_ipc_recv(&ipc, n1);
    assert(msg != NULL);
    assert(msg->ordinal == 42);
    assert(rational_mag(msg->quota) == 1.5);
    assert(msg->telemetry == 2.5 + 1.5 * I);

    printf("  [PASS] Typed IPC message with M5 axis metadata\n");
}

int main(void) {
    printf("=== OS Lattice Layer Tests ===\n");
    test_fibonacci();
    test_position();
    test_scale_generic(LATTICE_SCALE_NORMAL, "Normal");
    test_scale_generic(LATTICE_SCALE_QUANTUM, "Quantum");
    test_scale_generic(LATTICE_SCALE_POSTQUANTUM, "Post-Quantum");
    test_ipc_typed();
    printf("=== All lattice tests passed ===\n\n");
    return 0;
}
