/* hccs_core.h — HCCS Engine: Hardware Co-Design and Circuit Synthesis
 * Circuit DSL, auto-tuning via nested feedback loops, FPGA bitstream generation.
 * Reuses the same five kernel subsystems (OSEQ/RMAG/LPRES/IPHASE/CHOICE).
 * Per Cosmic AI Master Build Plan Ch. 6/11, Kernel Spec §5.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef HCCS_CORE_H
#define HCCS_CORE_H

#include "m5_host_float.h" /* host-only module: double / double complex */

#define HCCS_MAX_CIRCUITS 256
#define HCCS_MAX_COMPONENTS 512
#define HCCS_MAX_CONNECTIONS 1024
#define HCCS_MAX_TUNING_ITERATIONS 100

typedef enum {
    HCCS_COMP_OSCILLATOR = 0,
    HCCS_COMP_ADDER = 1,
    HCCS_COMP_MULTIPLIER = 2,
    HCCS_COMP_MUX = 3,
    HCCS_COMP_REGISTER = 4,
    HCCS_COMP_GATE = 5,
    HCCS_COMP_MEMORY = 6,
    HCCS_COMP_PHOTONIC = 7
} hccs_component_type_t;

typedef struct hccs_component {
    uint32_t id;
    hccs_component_type_t type;
    char name[32];
    double frequency;
    double delay;
    rational_t power;
    phase_t phase;
    trit_t integrity;
    uint32_t inputs[8];
    uint32_t num_inputs;
    uint32_t output;
} hccs_component_t;

typedef struct hccs_connection {
    uint32_t src_component;
    uint32_t src_output;
    uint32_t dst_component;
    uint32_t dst_input;
} hccs_connection_t;

typedef struct hccs_circuit {
    uint32_t id;
    char name[64];
    hccs_component_t components[HCCS_MAX_COMPONENTS];
    uint32_t num_components;
    hccs_connection_t connections[HCCS_MAX_CONNECTIONS];
    uint32_t num_connections;
    rational_t total_power;
    double max_frequency;
    double performance_score;
    uint32_t tuning_iterations;
    axiom_matrix_t *matrix;
} hccs_circuit_t;

typedef struct hccs_state {
    hccs_circuit_t circuits[HCCS_MAX_CIRCUITS];
    uint32_t num_circuits;
    double auto_tune_target;
    double auto_tune_best_score;
    uint32_t best_circuit_idx;
    axiom_matrix_t *matrix;
} hccs_state_t;

void hccs_init(hccs_state_t *h, axiom_matrix_t *matrix);
uint32_t hccs_create_circuit(hccs_state_t *h, const char *name);
uint32_t hccs_add_component(hccs_state_t *h, uint32_t circuit_idx,
                            hccs_component_type_t type, const char *name,
                            double frequency, rational_t power);
int hccs_connect(hccs_state_t *h, uint32_t circuit_idx,
                 uint32_t src_comp, uint32_t src_out,
                 uint32_t dst_comp, uint32_t dst_in);
int hccs_simulate(hccs_state_t *h, uint32_t circuit_idx, uint32_t steps);
int hccs_auto_tune(hccs_state_t *h, uint32_t circuit_idx, uint32_t iterations);
double hccs_evaluate(const hccs_circuit_t *c);
int hccs_export_bitstream(const hccs_circuit_t *c, uint8_t *out, uint32_t max_len);
int hccs_export_verilog(const hccs_circuit_t *c, char *out, uint32_t max_len);

#endif
