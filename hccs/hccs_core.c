/* hccs_core.c — HCCS Engine Implementation
 * Circuit simulation with auto-tuning feedback loop.
 * Performance score = f(max_freq) / f(power) — higher is better.
 * Auto-tuner adjusts frequencies and delays via nested PID-like loop.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "hccs_core.h"
#include "axiom_matrix_core.h"
#include "rmag_core.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

void hccs_init(hccs_state_t *h, axiom_matrix_t *matrix) {
    memset(h, 0, sizeof(hccs_state_t));
    h->matrix = matrix;
    h->auto_tune_target = 1.0;
    h->auto_tune_best_score = 0.0;
    h->best_circuit_idx = 0;
}

uint32_t hccs_create_circuit(hccs_state_t *h, const char *name) {
    if (h->num_circuits >= HCCS_MAX_CIRCUITS) return UINT32_MAX;
    uint32_t idx = h->num_circuits++;
    hccs_circuit_t *c = &h->circuits[idx];
    memset(c, 0, sizeof(hccs_circuit_t));
    c->id = idx;
    if (name) {
        size_t n = strlen(name);
        if (n > 63) n = 63;
        memcpy(c->name, name, n);
    }
    c->matrix = h->matrix;
    return idx;
}

uint32_t hccs_add_component(hccs_state_t *h, uint32_t circuit_idx,
                            hccs_component_type_t type, const char *name,
                            double frequency, rational_t power) {
    if (circuit_idx >= h->num_circuits) return UINT32_MAX;
    hccs_circuit_t *c = &h->circuits[circuit_idx];
    if (c->num_components >= HCCS_MAX_COMPONENTS) return UINT32_MAX;
    uint32_t id = c->num_components++;
    hccs_component_t *comp = &c->components[id];
    memset(comp, 0, sizeof(hccs_component_t));
    comp->id = id;
    comp->type = type;
    comp->frequency = frequency;
    comp->delay = 1.0 / (frequency > 0 ? frequency : 1.0);
    comp->power = rational_normalize(power);
    comp->phase = (phase_t){0, 0};
    comp->integrity = TRIT_TRUE;
    comp->num_inputs = 0;
    comp->output = id;
    if (name) {
        size_t n = strlen(name);
        if (n > 31) n = 31;
        memcpy(comp->name, name, n);
    }
    if (frequency > c->max_frequency) c->max_frequency = frequency;
    c->total_power = rmag_add_quotas(c->total_power, comp->power);
    return id;
}

int hccs_connect(hccs_state_t *h, uint32_t circuit_idx,
                 uint32_t src_comp, uint32_t src_out,
                 uint32_t dst_comp, uint32_t dst_in) {
    if (circuit_idx >= h->num_circuits) return -1;
    hccs_circuit_t *c = &h->circuits[circuit_idx];
    if (c->num_connections >= HCCS_MAX_CONNECTIONS) return -1;
    if (src_comp >= c->num_components || dst_comp >= c->num_components) return -1;
    hccs_connection_t *conn = &c->connections[c->num_connections++];
    conn->src_component = src_comp;
    conn->src_output = src_out;
    conn->dst_component = dst_comp;
    conn->dst_input = dst_in;
    hccs_component_t *dst = &c->components[dst_comp];
    if (dst_in < 8 && dst->num_inputs < 8) {
        dst->inputs[dst->num_inputs++] = src_comp;
    }
    return 0;
}

int hccs_simulate(hccs_state_t *h, uint32_t circuit_idx, uint32_t steps) {
    if (circuit_idx >= h->num_circuits) return -1;
    hccs_circuit_t *c = &h->circuits[circuit_idx];
    for (uint32_t step = 0; step < steps; step++) {
        for (uint32_t i = 0; i < c->num_components; i++) {
            hccs_component_t *comp = &c->components[i];
            comp->phase.r += comp->frequency * 0.001;
            comp->phase.i += comp->delay * 0.001;
            if (comp->phase.r > 2.0 * M_PI) comp->phase.r -= 2.0 * M_PI;
        }
        if (c->matrix) {
            double complex val = c->max_frequency + I * (double)step;
            axiom_matrix_set(c->matrix, step, c->total_power, TRIT_TRUE,
                           c->components[0].phase, (collapse_t){{step, 0}}, val);
        }
    }
    c->performance_score = hccs_evaluate(c);
    return 0;
}

double hccs_evaluate(const hccs_circuit_t *c) {
    double freq_score = c->max_frequency / 1e9;
    double power_mag = rational_mag(c->total_power);
    double power_score = power_mag > 0 ? 1.0 / (1.0 + power_mag / 1000.0) : 1.0;
    double integrity_score = 0.0;
    for (uint32_t i = 0; i < c->num_components; i++) {
        if (c->components[i].integrity == TRIT_TRUE) integrity_score += 1.0;
    }
    integrity_score /= (c->num_components > 0 ? c->num_components : 1);
    return freq_score * power_score * integrity_score;
}

int hccs_auto_tune(hccs_state_t *h, uint32_t circuit_idx, uint32_t iterations) {
    if (circuit_idx >= h->num_circuits) return -1;
    hccs_circuit_t *c = &h->circuits[circuit_idx];
    double best_score = c->performance_score;
    double prev_score = best_score;
    double integral = 0.0;
    double prev_error = h->auto_tune_target - best_score;

    for (uint32_t it = 0; it < iterations && it < HCCS_MAX_TUNING_ITERATIONS; it++) {
        double error = h->auto_tune_target - prev_score;
        integral += error;
        double derivative = error - prev_error;
        double kp = 0.1, ki = 0.01, kd = 0.05;
        double adjustment = kp * error + ki * integral + kd * derivative;

        for (uint32_t i = 0; i < c->num_components; i++) {
            c->components[i].frequency *= (1.0 + adjustment * 0.01);
            if (c->components[i].frequency < 1.0) c->components[i].frequency = 1.0;
            c->components[i].delay = 1.0 / c->components[i].frequency;
        }
        c->max_frequency = 0;
        for (uint32_t i = 0; i < c->num_components; i++) {
            if (c->components[i].frequency > c->max_frequency)
                c->max_frequency = c->components[i].frequency;
        }

        double score = hccs_evaluate(c);
        if (score > best_score) {
            best_score = score;
            if (best_score > h->auto_tune_best_score) {
                h->auto_tune_best_score = best_score;
                h->best_circuit_idx = circuit_idx;
            }
        } else {
            for (uint32_t i = 0; i < c->num_components; i++) {
                c->components[i].frequency /= (1.0 + adjustment * 0.01);
                c->components[i].delay = 1.0 / c->components[i].frequency;
            }
        }
        prev_score = score;
        prev_error = error;
        c->tuning_iterations++;
    }
    c->performance_score = best_score;
    return 0;
}

int hccs_export_bitstream(const hccs_circuit_t *c, uint8_t *out, uint32_t max_len) {
    if (!c || !out || max_len < 168) return -1;
    memset(out, 0, 168);
    out[0] = (uint8_t)(c->id & 0xFF);
    out[1] = (uint8_t)(c->num_components & 0xFF);
    out[2] = (uint8_t)(c->num_connections & 0xFF);
    uint32_t offset = 3;
    for (uint32_t i = 0; i < c->num_components && offset < 168; i++) {
        out[offset++] = (uint8_t)c->components[i].type;
        out[offset++] = (uint8_t)(c->components[i].frequency > 0 ? 1 : 0);
    }
    return (int)offset;
}

int hccs_export_verilog(const hccs_circuit_t *c, char *out, uint32_t max_len) {
    if (!c || !out || max_len < 256) return -1;
    int written = 0;
    written += snprintf(out + written, max_len - written,
                        "module %s;\n", c->name[0] ? c->name : "circuit");
    for (uint32_t i = 0; i < c->num_components && written < (int)max_len - 64; i++) {
        const char *type_name = "wire";
        switch (c->components[i].type) {
            case HCCS_COMP_OSCILLATOR: type_name = "osc"; break;
            case HCCS_COMP_ADDER: type_name = "adder"; break;
            case HCCS_COMP_MULTIPLIER: type_name = "mul"; break;
            case HCCS_COMP_MUX: type_name = "mux"; break;
            case HCCS_COMP_REGISTER: type_name = "reg"; break;
            case HCCS_COMP_GATE: type_name = "gate"; break;
            case HCCS_COMP_MEMORY: type_name = "mem"; break;
            case HCCS_COMP_PHOTONIC: type_name = "photonic"; break;
        }
        written += snprintf(out + written, max_len - written,
                            "  %s #(%d) u%d (.freq(%f));\n",
                            type_name, c->components[i].id, i,
                            c->components[i].frequency);
    }
    written += snprintf(out + written, max_len - written, "endmodule\n");
    return written;
}
