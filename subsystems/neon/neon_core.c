/* neon_core.c — Neon Harmony Orchestrator Implementation
 * Pipeline processes data through registered plugins in ordinal order.
 * Ethics module (MEEMO) applies magneto-electric gates on every data edge.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "neon_core.h"
#include "axiom_matrix_core.h"
#include "choice_core.h"
#include <string.h>

static const double NEON_PHI = 1.61803398874989484820;

void neon_init(neon_orchestrator_t *orch, axiom_matrix_t *matrix) {
    memset(orch, 0, sizeof(neon_orchestrator_t));
    orch->matrix = matrix;
    orch->cycle_count = 0;
}

uint32_t neon_register_plugin(neon_orchestrator_t *orch, neon_plugin_type_t type,
                               const char *name, neon_plugin_fn fn, void *ctx,
                               double phi_weight) {
    if (orch->num_plugins >= NEON_MAX_PLUGINS) return UINT32_MAX;
    uint32_t idx = orch->num_plugins++;
    neon_plugin_t *p = &orch->plugins[idx];
    memset(p, 0, sizeof(neon_plugin_t));
    p->type = type;
    p->process = fn;
    p->ctx = ctx;
    p->phi_weight = phi_weight;
    p->enabled = true;
    if (name) {
        size_t n = strlen(name);
        if (n > 31) n = 31;
        memcpy(p->name, name, n);
    }
    return idx;
}

int neon_execute(neon_orchestrator_t *orch, neon_data_t *data) {
    for (uint32_t i = 0; i < orch->num_plugins; i++) {
        neon_plugin_t *p = &orch->plugins[i];
        if (!p->enabled) continue;
        if (p->process) {
            int rc = p->process(data, p->ctx);
            if (rc != 0) return rc;
        }
        if (orch->matrix) {
            axiom_matrix_set(orch->matrix, orch->cycle_count,
                           data->resource_quota, data->attestation,
                           data->phase, data->choice, data->telemetry);
        }
    }
    orch->cycle_count++;
    return 0;
}

int neon_execute_reverse(neon_orchestrator_t *orch, neon_data_t *data) {
    for (int32_t i = (int32_t)orch->num_plugins - 1; i >= 0; i--) {
        neon_plugin_t *p = &orch->plugins[i];
        if (!p->enabled) continue;
        if (p->process) {
            int rc = p->process(data, p->ctx);
            if (rc != 0) return rc;
        }
    }
    return 0;
}

int neon_plugin_filter(neon_data_t *data, void *ctx) {
    (void)ctx;
    if (data->payload_len == 0) {
        data->filtered = true;
        return -1;
    }
    data->filtered = false;
    return 0;
}

int neon_plugin_consent(neon_data_t *data, void *ctx) {
    (void)ctx;
    if (!data->consent_granted) {
        return -2;
    }
    data->attestation = TRIT_TRUE;
    return 0;
}

int neon_plugin_transmute(neon_data_t *data, void *ctx) {
    (void)ctx;
    for (uint32_t i = 0; i < data->payload_len / 2; i++) {
        uint8_t tmp = data->payload[i];
        data->payload[i] = data->payload[data->payload_len - 1 - i];
        data->payload[data->payload_len - 1 - i] = tmp;
    }
    return 0;
}

int neon_plugin_ascend(neon_data_t *data, void *ctx) {
    (void)ctx;
    data->telemetry = (double complex)data->ordinal * (1.0 + 0.5 * I);
    return 0;
}

int neon_plugin_reflect(neon_data_t *data, void *ctx) {
    (void)ctx;
    data->choice = choice_get_state();
    return 0;
}

int neon_plugin_log(neon_data_t *data, void *ctx) {
    (void)ctx;
    (void)data;
    return 0;
}

int neon_plugin_persist(neon_data_t *data, void *ctx) {
    (void)ctx;
    data->persisted = true;
    return 0;
}

int neon_plugin_ethics(neon_data_t *data, void *ctx) {
    (void)ctx;
    if (data->emotion_index < -0.5) {
        return -3;
    }
    return 0;
}
