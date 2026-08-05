/* neon_core.h — Neon Harmony Orchestrator
 * Plugin pipeline: FILTER -> CONSENT -> TRANSMUTE -> ASCEND -> REFLECT -> LOG -> PERSIST
 * Each plugin at a vertex of a Platonic lattice, edges weighted by phi.
 * Per Cosmic AI Master Build Plan Ch. 2/10.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef NEON_CORE_H
#define NEON_CORE_H

#include "m5_types.h"

#define NEON_MAX_PLUGINS 32
#define NEON_MAX_DATA_LEN 4096

typedef enum {
    NEON_FILTER = 0,
    NEON_CONSENT = 1,
    NEON_TRANSMUTE = 2,
    NEON_ASCEND = 3,
    NEON_REFLECT = 4,
    NEON_LOG = 5,
    NEON_PERSIST = 6,
    NEON_ETHICS = 7,
    NEON_AUDIOGIN = 8,
    NEON_CUSTOM = 9
} neon_plugin_type_t;

typedef struct neon_data {
    uint8_t payload[NEON_MAX_DATA_LEN];
    uint32_t payload_len;
    ordinal_t ordinal;
    rational_t resource_quota;
    trit_t attestation;
    phase_t phase;
    collapse_t choice;
    double complex telemetry;
    double emotion_index;
    char origin[64];
    bool consent_granted;
    bool filtered;
    bool persisted;
} neon_data_t;

typedef int (*neon_plugin_fn)(neon_data_t *data, void *ctx);

typedef struct neon_plugin {
    neon_plugin_type_t type;
    char name[32];
    neon_plugin_fn process;
    void *ctx;
    double phi_weight;
    bool enabled;
} neon_plugin_t;

typedef struct neon_orchestrator {
    neon_plugin_t plugins[NEON_MAX_PLUGINS];
    uint32_t num_plugins;
    uint32_t cycle_count;
    axiom_matrix_t *matrix;
} neon_orchestrator_t;

void neon_init(neon_orchestrator_t *orch, axiom_matrix_t *matrix);
uint32_t neon_register_plugin(neon_orchestrator_t *orch, neon_plugin_type_t type,
                               const char *name, neon_plugin_fn fn, void *ctx,
                               double phi_weight);
int neon_execute(neon_orchestrator_t *orch, neon_data_t *data);
int neon_execute_reverse(neon_orchestrator_t *orch, neon_data_t *data);

int neon_plugin_filter(neon_data_t *data, void *ctx);
int neon_plugin_consent(neon_data_t *data, void *ctx);
int neon_plugin_transmute(neon_data_t *data, void *ctx);
int neon_plugin_ascend(neon_data_t *data, void *ctx);
int neon_plugin_reflect(neon_data_t *data, void *ctx);
int neon_plugin_log(neon_data_t *data, void *ctx);
int neon_plugin_persist(neon_data_t *data, void *ctx);
int neon_plugin_ethics(neon_data_t *data, void *ctx);

#endif
