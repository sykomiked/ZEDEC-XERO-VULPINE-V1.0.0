/* test_integration.c — Cross-Layer Integration Tests
 * Tests all OS layers working together through the axiom_matrix_t backing store.
 * Verifies the "Transcendent convergence" principle: single scale-generic
 * implementation works across all three scale factors.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "lattice_core.h"
#include "lattice_scheduler.h"
#include "lattice_ipc.h"
#include "neon_core.h"
#include "gridchain_core.h"
#include "security_core.h"
#include "physics_core.h"
#include "hccs_core.h"
#include "audiogenomics_core.h"
#include "governance_core.h"
#include "axiom_matrix_core.h"

static void test_full_stack(double scale, const char *scale_name) {
    printf("  --- Integration at scale s=%.6f (%s) ---\n", scale, scale_name);

    axiom_matrix_t matrix;
    matrix.size = 4096;
    static zxv_cq16_t entries[4096];
    memset(entries, 0, sizeof(entries));
    matrix.entries = entries;

    /* 1. Lattice */
    static lattice_graph_t graph;
    lattice_init(&graph, scale, &matrix);
    uint32_t n0 = lattice_add_node(&graph, 0, (lattice_node_id_t){0, 5});
    uint32_t n1 = lattice_add_node(&graph, 1, (lattice_node_id_t){1, 5});
    lattice_add_edge(&graph, n0, n1, EDGE_ORDINAL, (rational_t){1, 1}, TRIT_TRUE, (phase_t){0, 0}, (collapse_t){{0, 0}});
    assert(graph.num_nodes == 2);
    assert(graph.scale_factor == scale);

    /* 2. Scheduler */
    lattice_scheduler_t sched;
    lattice_sched_init(&sched, &graph);
    sched_event_t ev = {0};
    ev.type = SCHED_EVENT_ARRIVAL;
    ev.magnitude = (rational_t){2, 1};
    ev.attestation = TRIT_TRUE;
    ev.telemetry_value = 1.0 + I;
    lattice_sched_enqueue(&sched, &ev);
    phase_tick_t tick;
    assert(lattice_sched_tick(&sched, &tick) == 0);

    /* 3. IPC */
    lattice_ipc_t ipc;
    lattice_ipc_init(&ipc, &graph);
    assert(lattice_ipc_send(&ipc, n0, n1, EDGE_ORDINAL, "data", 4) == 0);

    /* 4. Neon */
    neon_orchestrator_t orch;
    neon_init(&orch, &matrix);
    neon_register_plugin(&orch, NEON_FILTER, "f", neon_plugin_filter, NULL, 1.0);
    neon_register_plugin(&orch, NEON_CONSENT, "c", neon_plugin_consent, NULL, 1.0);
    neon_register_plugin(&orch, NEON_ETHICS, "e", neon_plugin_ethics, NULL, 1.618);
    neon_register_plugin(&orch, NEON_PERSIST, "p", neon_plugin_persist, NULL, 1.0);
    neon_data_t ndata = {0};
    memcpy(ndata.payload, "test", 4);
    ndata.payload_len = 4;
    ndata.consent_granted = true;
    ndata.emotion_index = 0.5;
    assert(neon_execute(&orch, &ndata) == 0);
    assert(ndata.persisted);

    /* 5. GridChain */
    static gridchain_state_t gc;
    gridchain_init(&gc, &matrix, 2);
    gridchain_commit_block(&gc);
    gridchain_tx_t tx = {0};
    tx.ordinal = 1;
    tx.amount = (rational_t){100, 1};
    tx.attestation = TRIT_TRUE;
    tx.emotion_index = 0.5;
    gridchain_add_tx(&gc, &tx);
    assert(gridchain_verify_chain(&gc));

    /* 6. Security */
    static security_fabric_t sec;
    security_init(&sec, &matrix);
    security_create_key(&sec);
    uint32_t pol = security_add_policy(&sec, "default", TRIT_TRUE, (rational_t){1, 1}, SEC_LAYER_POLICY);
    uint32_t sess = security_create_session(&sec, 0, 100);
    assert(security_authenticate(&sec, sess, pol) == 0);

    /* 7. Physics */
    static physics_state_t phys;
    physics_init(&phys, &matrix);
    physics_add_body(&phys, 0.0, 0.0, (rational_t){1000, 1}, (rational_t){1, 1});
    physics_add_body(&phys, 1.0, 0.0, (rational_t){1, 1}, (rational_t){1, 1});
    assert(physics_step(&phys) == 0);

    /* 8. HCCS */
    static hccs_state_t hccs;
    hccs_init(&hccs, &matrix);
    uint32_t c = hccs_create_circuit(&hccs, "test");
    hccs_add_component(&hccs, c, HCCS_COMP_OSCILLATOR, "clk", 100e6, (rational_t){10, 1});
    hccs_simulate(&hccs, c, 10);
    assert(hccs.circuits[c].performance_score > 0);

    /* 9. Audiogenomics */
    static audiogenomics_state_t ag;
    audiogenomics_init(&ag, &matrix);
    uint32_t as = audiogenomics_create_session(&ag, "user");
    audio_sample_t smp[10] = {0};
    for (int i = 0; i < 10; i++) { smp[i].real = 0.5 * sin(i); smp[i].imag = 0.5 * cos(i); }
    audiogenomics_add_samples(&ag, as, smp, 10);
    assert(audiogenomics_extract_egv(&ag, as) == 0);

    /* 10. Governance */
    static governance_state_t gov;
    governance_init(&gov, &matrix);
    uint32_t h = governance_register_holder(&gov, 1000, 500000);
    uint32_t p = governance_create_proposal(&gov, "test", GOV_BRANCH_KERNEL, 5);
    governance_cast_vote(&gov, p, h, TRIT_TRUE, 0.5);
    gov.current_cycle = 5;
    governance_close_proposal(&gov, p);
    assert(gov.proposals[p].passed);

    /* Verify axiom matrix has been written by all layers */
    bool has_data = false;
    for (uint64_t i = 0; i < matrix.size; i++) {
        if (!cq16_is_zero(entries[i])) {
            has_data = true;
            break;
        }
    }
    assert(has_data);

    printf("  [PASS] All 10 layers integrated at %s\n", scale_name);
}

int main(void) {
    printf("=== Cross-Layer Integration Tests ===\n");
    test_full_stack(LATTICE_SCALE_NORMAL, "Normal (s=1)");
    test_full_stack(LATTICE_SCALE_QUANTUM, "Quantum (s=phi)");
    test_full_stack(LATTICE_SCALE_POSTQUANTUM, "Post-Quantum (s=phi^2)");
    printf("=== Transcendent convergence: pass/fail parity across all scales ===\n\n");
    return 0;
}
