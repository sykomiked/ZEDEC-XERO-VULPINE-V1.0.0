/* security_core.h — Integrated Security Fabric
 * 5 layers: HSM key-simplex, OPA policy hyperplanes, mTLS mesh,
 * threshold BLS signatures, time-gated revocation.
 * Per Cosmic AI Master Build Plan Ch. 4/13.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef SECURITY_CORE_H
#define SECURITY_CORE_H

#include "m5_types.h"

#define SECURITY_MAX_KEYS 256
#define SECURITY_MAX_POLICIES 128
#define SECURITY_MAX_SESSIONS 1024
#define SECURITY_MAX_SIGNATURES 64
#define SECURITY_KEY_SIZE 32

typedef enum {
    SEC_LAYER_HSM = 0,
    SEC_LAYER_POLICY = 1,
    SEC_LAYER_MTLS = 2,
    SEC_LAYER_THRESHOLD = 3,
    SEC_LAYER_TIME_GATE = 4
} security_layer_t;

typedef struct security_key {
    uint8_t material[SECURITY_KEY_SIZE];
    uint32_t key_id;
    ordinal_t rotation_cycle;
    bool active;
} security_key_t;

typedef struct security_policy {
    char name[64];
    trit_t rule;
    rational_t threshold;
    uint32_t layer;
    bool enabled;
} security_policy_t;

typedef struct security_session {
    uint32_t session_id;
    uint32_t node_id;
    ordinal_t established;
    ordinal_t expires;
    bool authenticated;
    double emotion_index;
} security_session_t;

typedef struct security_threshold_sig {
    uint32_t signer_id;
    uint8_t signature[168];
    uint32_t block_ref;
} security_threshold_sig_t;

typedef struct security_time_gate {
    ordinal_t open_cycle;
    ordinal_t close_cycle;
    uint32_t gate_id;
    bool is_open;
} security_time_gate_t;

typedef struct security_fabric {
    security_key_t keys[SECURITY_MAX_KEYS];
    uint32_t num_keys;
    security_policy_t policies[SECURITY_MAX_POLICIES];
    uint32_t num_policies;
    security_session_t sessions[SECURITY_MAX_SESSIONS];
    uint32_t num_sessions;
    security_threshold_sig_t sigs[SECURITY_MAX_SIGNATURES];
    uint32_t num_sigs;
    security_time_gate_t gates[32];
    uint32_t num_gates;
    uint32_t threshold_required;
    axiom_matrix_t *matrix;
} security_fabric_t;

void security_init(security_fabric_t *s, axiom_matrix_t *matrix);
uint32_t security_create_key(security_fabric_t *s);
int security_rotate_key(security_fabric_t *s, uint32_t key_id);
uint32_t security_add_policy(security_fabric_t *s, const char *name, trit_t rule,
                              rational_t threshold, uint32_t layer);
int security_check_policy(security_fabric_t *s, uint32_t policy_idx, trit_t attestation,
                          rational_t magnitude);
uint32_t security_create_session(security_fabric_t *s, uint32_t node_id, ordinal_t duration);
bool security_session_valid(const security_fabric_t *s, uint32_t session_id);
int security_add_threshold_sig(security_fabric_t *s, uint32_t signer_id, uint32_t block_ref);
bool security_threshold_reached(const security_fabric_t *s, uint32_t block_ref);
int security_open_time_gate(security_fabric_t *s, uint32_t gate_id, ordinal_t current);
bool security_time_gate_open(const security_fabric_t *s, uint32_t gate_id, ordinal_t current);
int security_authenticate(security_fabric_t *s, uint32_t session_id, uint32_t policy_idx);

#endif
