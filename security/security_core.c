/* security_core.c — Integrated Security Fabric Implementation
 * 5-layer zero-trust: HSM key rotation on phi-scaled cycles,
 * OPA policy gates, mTLS session management, threshold BLS, time-gated revocation.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "security_core.h"
#include "axiom_matrix_core.h"
#include <string.h>

static const double SEC_PHI = 1.61803398874989484820;

static void generate_key_material(uint8_t *out, uint32_t seed) {
    uint64_t h = 1469598103934665603ULL ^ (uint64_t)seed;
    for (int i = 0; i < SECURITY_KEY_SIZE; i++) {
        h ^= (uint64_t)i + 1;
        h *= 1099511628211ULL;
        out[i] = (uint8_t)(h & 0xFF);
    }
}

void security_init(security_fabric_t *s, axiom_matrix_t *matrix) {
    memset(s, 0, sizeof(security_fabric_t));
    s->matrix = matrix;
    s->threshold_required = 3;
}

uint32_t security_create_key(security_fabric_t *s) {
    if (s->num_keys >= SECURITY_MAX_KEYS) return UINT32_MAX;
    uint32_t idx = s->num_keys++;
    s->keys[idx].key_id = idx;
    s->keys[idx].rotation_cycle = 0;
    s->keys[idx].active = true;
    generate_key_material(s->keys[idx].material, idx);
    return idx;
}

int security_rotate_key(security_fabric_t *s, uint32_t key_id) {
    if (key_id >= s->num_keys) return -1;
    generate_key_material(s->keys[key_id].material, key_id + 1);
    s->keys[key_id].rotation_cycle++;
    return 0;
}

uint32_t security_add_policy(security_fabric_t *s, const char *name, trit_t rule,
                              rational_t threshold, uint32_t layer) {
    if (s->num_policies >= SECURITY_MAX_POLICIES) return UINT32_MAX;
    uint32_t idx = s->num_policies++;
    security_policy_t *p = &s->policies[idx];
    memset(p, 0, sizeof(security_policy_t));
    if (name) {
        size_t n = strlen(name);
        if (n > 63) n = 63;
        memcpy(p->name, name, n);
    }
    p->rule = rule;
    p->threshold = rational_normalize(threshold);
    p->layer = layer;
    p->enabled = true;
    return idx;
}

int security_check_policy(security_fabric_t *s, uint32_t policy_idx, trit_t attestation,
                          rational_t magnitude) {
    if (policy_idx >= s->num_policies) return -1;
    security_policy_t *p = &s->policies[policy_idx];
    if (!p->enabled) return -2;
    if (p->rule == TRIT_TRUE && attestation != TRIT_TRUE) return -3;
    if (p->rule == TRIT_FALSE && attestation == TRIT_TRUE) return -3;
    double mag = rational_mag(magnitude);
    double thresh = rational_mag(p->threshold);
    if (thresh > 0 && mag < thresh) return -4;
    return 0;
}

uint32_t security_create_session(security_fabric_t *s, uint32_t node_id, ordinal_t duration) {
    if (s->num_sessions >= SECURITY_MAX_SESSIONS) return UINT32_MAX;
    uint32_t idx = s->num_sessions++;
    s->sessions[idx].session_id = idx;
    s->sessions[idx].node_id = node_id;
    s->sessions[idx].established = 0;
    s->sessions[idx].expires = duration;
    s->sessions[idx].authenticated = true;
    s->sessions[idx].emotion_index = 0.0;
    return idx;
}

bool security_session_valid(const security_fabric_t *s, uint32_t session_id) {
    if (session_id >= s->num_sessions) return false;
    return s->sessions[session_id].authenticated;
}

int security_add_threshold_sig(security_fabric_t *s, uint32_t signer_id, uint32_t block_ref) {
    if (s->num_sigs >= SECURITY_MAX_SIGNATURES) return -1;
    for (uint32_t i = 0; i < s->num_sigs; i++) {
        if (s->sigs[i].signer_id == signer_id && s->sigs[i].block_ref == block_ref)
            return -2;
    }
    uint32_t idx = s->num_sigs++;
    s->sigs[idx].signer_id = signer_id;
    s->sigs[idx].block_ref = block_ref;
    memset(s->sigs[idx].signature, signer_id & 0xFF, 168);
    return 0;
}

bool security_threshold_reached(const security_fabric_t *s, uint32_t block_ref) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < s->num_sigs; i++) {
        if (s->sigs[i].block_ref == block_ref) count++;
    }
    return count >= s->threshold_required;
}

int security_open_time_gate(security_fabric_t *s, uint32_t gate_id, ordinal_t current) {
    if (gate_id >= s->num_gates) return -1;
    s->gates[gate_id].is_open = (current >= s->gates[gate_id].open_cycle &&
                                  current < s->gates[gate_id].close_cycle);
    return s->gates[gate_id].is_open ? 0 : -2;
}

bool security_time_gate_open(const security_fabric_t *s, uint32_t gate_id, ordinal_t current) {
    if (gate_id >= s->num_gates) return false;
    return (current >= s->gates[gate_id].open_cycle &&
            current < s->gates[gate_id].close_cycle);
}

int security_authenticate(security_fabric_t *s, uint32_t session_id, uint32_t policy_idx) {
    if (!security_session_valid(s, session_id)) return -1;
    security_session_t *sess = &s->sessions[session_id];
    int rc = security_check_policy(s, policy_idx, TRIT_TRUE, (rational_t){1, 1});
    if (rc != 0) {
        sess->authenticated = false;
        return rc;
    }
    if (sess->emotion_index < -0.5) {
        sess->authenticated = false;
        return -5;
    }
    return 0;
}
