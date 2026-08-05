/* test_hccs.c — HCCS Engine Tests
 * Tests circuit creation, component addition, simulation, auto-tuning, export.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hccs_core.h"
#include "axiom_matrix_core.h"

int main(void) {
    printf("=== HCCS Engine Tests ===\n");

    axiom_matrix_t matrix;
    matrix.size = 512;
    double complex entries[512];
    memset(entries, 0, sizeof(entries));
    matrix.entries = entries;

    hccs_state_t hccs;
    hccs_init(&hccs, &matrix);

    uint32_t c0 = hccs_create_circuit(&hccs, "harmonic_adder");
    assert(c0 == 0);
    assert(hccs.num_circuits == 1);
    printf("  [PASS] Circuit creation\n");

    uint32_t osc = hccs_add_component(&hccs, c0, HCCS_COMP_OSCILLATOR, "clk", 100e6, (rational_t){10, 1});
    uint32_t add = hccs_add_component(&hccs, c0, HCCS_COMP_ADDER, "add1", 50e6, (rational_t){5, 1});
    uint32_t mul = hccs_add_component(&hccs, c0, HCCS_COMP_MULTIPLIER, "mul1", 50e6, (rational_t){8, 1});
    assert(osc == 0 && add == 1 && mul == 2);
    assert(hccs.circuits[c0].num_components == 3);
    printf("  [PASS] Component addition (oscillator, adder, multiplier)\n");

    assert(hccs_connect(&hccs, c0, osc, 0, add, 0) == 0);
    assert(hccs_connect(&hccs, c0, osc, 0, mul, 0) == 0);
    assert(hccs_connect(&hccs, c0, add, 0, mul, 1) == 0);
    assert(hccs.circuits[c0].num_connections == 3);
    printf("  [PASS] Component connections\n");

    assert(hccs_simulate(&hccs, c0, 100) == 0);
    double score = hccs.circuits[c0].performance_score;
    assert(score > 0);
    printf("  [PASS] Circuit simulation (score=%.4f)\n", score);

    double score_before = hccs.circuits[c0].performance_score;
    assert(hccs_auto_tune(&hccs, c0, 50) == 0);
    double score_after = hccs.circuits[c0].performance_score;
    assert(score_after >= score_before - 1e-9);
    printf("  [PASS] Auto-tune (before=%.4f, after=%.4f)\n", score_before, score_after);

    uint8_t bitstream[168];
    int blen = hccs_export_bitstream(&hccs.circuits[c0], bitstream, 168);
    assert(blen > 0);
    assert(bitstream[0] == 0);
    assert(bitstream[1] == 3);
    printf("  [PASS] Bitstream export (%d bytes)\n", blen);

    char verilog[1024];
    int vlen = hccs_export_verilog(&hccs.circuits[c0], verilog, sizeof(verilog));
    assert(vlen > 0);
    assert(strstr(verilog, "module") != NULL);
    assert(strstr(verilog, "endmodule") != NULL);
    printf("  [PASS] Verilog export (%d chars)\n", vlen);

    printf("=== All HCCS tests passed ===\n\n");
    return 0;
}
