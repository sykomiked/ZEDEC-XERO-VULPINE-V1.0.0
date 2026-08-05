/* ai_layer.c — AI Integration & Remote Compute implementation
 *
 * See ai_layer.h for design rationale. Follows the same conventions
 * as community_chest.c / porter_house.c: SR_* fixed-point, M5
 * coordinates, pluggable signature verification, Porter House
 * admission for remote peers.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * License: SEL-3.3 (kernel component)
 */
#include "ai_layer.h"
#include <string.h>
#include "../robin_debanks/ed25519_verify.h"

static bool ai_default_verify_sig(const ai_model_t *model) {
    if (!model) return false;
    /* Ed25519 asymmetric verification against embedded PUBLIC key.
     * Private key held offline/HSM-backed. */
    uint8_t msg[128]; uint32_t pos=0;
    for (uint32_t j=0; j<64 && model->name[j]; j++) msg[pos++]=(uint8_t)model->name[j];
    for (uint32_t j=0; j<AI_PUBKEY_LEN && pos<sizeof(msg); j++) msg[pos++]=model->provider_pubkey[j];
    return ed25519_verify(msg, pos, model->signature, ED25519_PUBKEY_AI_LAYER);
}

void ai_init(ai_engine_t *ai, uint32_t device_id, const char *name,
              porter_house_t *porter) {
    if (!ai) return;
    memset(ai, 0, sizeof(*ai));
    ai->device_id = device_id;

    uint32_t i;
    for (i = 0; i + 1 < AI_MAX_NAME_LEN && name && name[i]; i++) {
        ai->name[i] = name[i];
    }
    ai->name[i] = '\0';

    ai->porter = porter;
    ai->num_models = 0;
    ai->num_tasks = 0;
    ai->num_peers = 0;
    ai->next_model_id = 1;
    ai->next_task_id = 1;

    ai->m5.omega = device_id;
    ai->m5.chi = device_id;
    ai->m5.phi = SR_ZERO;

    ai_update_coverage(ai);
}

int32_t ai_register_model(ai_engine_t *ai, const char *name,
                           ai_model_type_t type,
                           const word168_t *provider_id,
                           const uint8_t pubkey[AI_PUBKEY_LEN],
                           const uint8_t content_hash[AI_CONTENT_HASH_LEN],
                           const uint8_t signature[AI_SIG_LEN],
                           uint64_t param_count, uint64_t memory_bytes) {
    if (!ai || !name || !provider_id) return -1;

    uint32_t slot = AI_MAX_MODELS;
    for (uint32_t i = 0; i < AI_MAX_MODELS; i++) {
        if (!ai->models[i].active) {
            slot = i;
            break;
        }
    }
    if (slot >= AI_MAX_MODELS) return -1;

    ai_model_t *m = &ai->models[slot];
    memset(m, 0, sizeof(*m));
    m->id = ai->next_model_id++;
    m->active = true;
    m->type = type;

    uint32_t j;
    for (j = 0; j + 1 < AI_MAX_NAME_LEN && name[j]; j++) m->name[j] = name[j];
    m->name[j] = '\0';

    m->provider_id = *provider_id;
    if (pubkey) memcpy(m->provider_pubkey, pubkey, AI_PUBKEY_LEN);
    if (content_hash) memcpy(m->content_hash, content_hash, AI_CONTENT_HASH_LEN);
    if (signature) memcpy(m->signature, signature, AI_SIG_LEN);

    m->param_count = param_count;
    m->memory_bytes = memory_bytes;

    m->sig_verified = ai->verify_sig ? ai->verify_sig(m) : ai_default_verify_sig(m);
    if (!m->sig_verified) {
        m->state = AI_MODEL_REJECTED;
        ai->num_models++;
        ai_update_coverage(ai);
        return -2;
    }

    m->state = AI_MODEL_LOADED;
    ai->num_models++;
    ai_update_coverage(ai);
    return (int32_t)m->id;
}

bool ai_verify_model(ai_engine_t *ai, uint32_t model_id) {
    if (!ai) return false;
    ai_model_t *m = ai_get_model(ai, model_id);
    if (!m) return false;
    m->sig_verified = ai->verify_sig ? ai->verify_sig(m) : ai_default_verify_sig(m);
    if (m->sig_verified && m->state == AI_MODEL_REJECTED) {
        m->state = AI_MODEL_LOADED;
    }
    return m->sig_verified;
}

int32_t ai_add_peer(ai_engine_t *ai, const word168_t *peer_id,
                     uint32_t trust_weight, uint64_t available_memory) {
    if (!ai || !peer_id) return -1;

    uint32_t slot = AI_MAX_PEERS;
    for (uint32_t i = 0; i < AI_MAX_PEERS; i++) {
        if (!ai->peers[i].active) {
            slot = i;
            break;
        }
    }
    if (slot >= AI_MAX_PEERS) return -1;

    /* Porter House admission check for remote compute peer */
    if (ai->porter) {
        if (!porter_house_admit(ai->porter, AI_REMOTE_PORT, peer_id, trust_weight)) {
            return -2;
        }
    }

    ai_peer_t *p = &ai->peers[slot];
    memset(p, 0, sizeof(*p));
    p->peer_id = *peer_id;
    p->trust_weight = trust_weight;
    p->available_memory = available_memory;
    p->active = true;

    ai->num_peers++;
    ai_update_coverage(ai);
    return 0;
}

int32_t ai_submit_local(ai_engine_t *ai, uint32_t model_id,
                         const uint8_t *input, uint32_t input_len,
                         uint64_t current_cycle) {
    if (!ai) return -1;
    ai_model_t *m = ai_get_model(ai, model_id);
    if (!m || m->state != AI_MODEL_LOADED) return -1;

    uint32_t slot = AI_MAX_TASKS;
    for (uint32_t i = 0; i < AI_MAX_TASKS; i++) {
        if (!ai->tasks[i].active) {
            slot = i;
            break;
        }
    }
    if (slot >= AI_MAX_TASKS) return -1;

    ai_task_t *t = &ai->tasks[slot];
    memset(t, 0, sizeof(*t));
    t->id = ai->next_task_id++;
    t->model_id = model_id;
    t->mode = AI_TASK_LOCAL;
    t->state = AI_TASK_QUEUED;
    t->created_cycle = current_cycle;
    t->active = true;

    if (input && input_len > 0) {
        uint32_t copy_len = input_len;
        if (copy_len > AI_MAX_INPUT_SIZE) copy_len = AI_MAX_INPUT_SIZE;
        memcpy(t->input, input, copy_len);
        t->input_len = copy_len;
    }

    ai->num_tasks++;
    return (int32_t)t->id;
}

int32_t ai_submit_remote(ai_engine_t *ai, uint32_t model_id,
                          const word168_t *remote_peer,
                          const uint8_t *input, uint32_t input_len,
                          uint64_t current_cycle) {
    if (!ai || !remote_peer) return -1;
    ai_model_t *m = ai_get_model(ai, model_id);
    if (!m) return -1;

    /* Check if peer is registered */
    bool peer_found = false;
    for (uint32_t i = 0; i < AI_MAX_PEERS; i++) {
        if (ai->peers[i].active) {
            bool match = true;
            for (int j = 0; j < WORD168_OCTETS; j++) {
                if (ai->peers[i].peer_id.bytes[j] != remote_peer->bytes[j]) {
                    match = false;
                    break;
                }
            }
            if (match) { peer_found = true; break; }
        }
    }
    if (!peer_found && ai->num_peers == 0) return -2;

    /* Porter House admission for remote compute */
    if (ai->porter) {
        uint32_t trust = 0;
        for (uint32_t i = 0; i < AI_MAX_PEERS; i++) {
            if (ai->peers[i].active) {
                bool match = true;
                for (int j = 0; j < WORD168_OCTETS; j++) {
                    if (ai->peers[i].peer_id.bytes[j] != remote_peer->bytes[j]) {
                        match = false;
                        break;
                    }
                }
                if (match) { trust = ai->peers[i].trust_weight; break; }
            }
        }
        if (!porter_house_admit(ai->porter, AI_REMOTE_PORT, remote_peer, trust)) {
            return -3;
        }
    }

    uint32_t slot = AI_MAX_TASKS;
    for (uint32_t i = 0; i < AI_MAX_TASKS; i++) {
        if (!ai->tasks[i].active) {
            slot = i;
            break;
        }
    }
    if (slot >= AI_MAX_TASKS) return -1;

    ai_task_t *t = &ai->tasks[slot];
    memset(t, 0, sizeof(*t));
    t->id = ai->next_task_id++;
    t->model_id = model_id;
    t->mode = AI_TASK_REMOTE;
    t->state = AI_TASK_QUEUED;
    t->remote_peer = *remote_peer;
    t->created_cycle = current_cycle;
    t->active = true;

    if (input && input_len > 0) {
        uint32_t copy_len = input_len;
        if (copy_len > AI_MAX_INPUT_SIZE) copy_len = AI_MAX_INPUT_SIZE;
        memcpy(t->input, input, copy_len);
        t->input_len = copy_len;
    }

    ai->num_tasks++;
    return (int32_t)t->id;
}

int32_t ai_execute_local(ai_engine_t *ai, uint32_t task_id, uint64_t current_cycle) {
    if (!ai) return -1;
    ai_task_t *t = ai_get_task(ai, task_id);
    if (!t || t->state != AI_TASK_QUEUED) return -1;
    if (t->mode != AI_TASK_LOCAL) return -1;

    ai_model_t *m = ai_get_model(ai, t->model_id);
    if (!m) return -1;

    /* Simulate inference: produce a simple output */
    t->state = AI_TASK_RUNNING;
    t->output_len = 1;
    t->output[0] = (uint8_t)(t->input_len & 0xFF);
    t->completed_cycle = current_cycle;
    t->cycles_consumed = current_cycle - t->created_cycle + 1;
    t->state = AI_TASK_COMPLETE;

    m->inference_count++;
    m->total_cycles += t->cycles_consumed;
    ai->total_inferences++;
    ai->total_local_inferences++;
    ai->total_cycles_consumed += t->cycles_consumed;

    ai_update_coverage(ai);
    return 0;
}

int32_t ai_complete_remote(ai_engine_t *ai, uint32_t task_id,
                            const uint8_t *output, uint32_t output_len,
                            uint64_t current_cycle) {
    if (!ai) return -1;
    ai_task_t *t = ai_get_task(ai, task_id);
    if (!t || t->state != AI_TASK_QUEUED) return -1;
    if (t->mode != AI_TASK_REMOTE) return -1;

    t->state = AI_TASK_RUNNING;

    if (output && output_len > 0) {
        uint32_t copy_len = output_len;
        if (copy_len > AI_MAX_OUTPUT_SIZE) copy_len = AI_MAX_OUTPUT_SIZE;
        memcpy(t->output, output, copy_len);
        t->output_len = copy_len;
    }

    t->completed_cycle = current_cycle;
    t->cycles_consumed = current_cycle - t->created_cycle;
    t->state = AI_TASK_COMPLETE;

    ai_model_t *m = ai_get_model(ai, t->model_id);
    if (m) {
        m->inference_count++;
        m->total_cycles += t->cycles_consumed;
    }

    /* Update peer stats */
    for (uint32_t i = 0; i < AI_MAX_PEERS; i++) {
        if (ai->peers[i].active) {
            bool match = true;
            for (int j = 0; j < WORD168_OCTETS; j++) {
                if (ai->peers[i].peer_id.bytes[j] != t->remote_peer.bytes[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                ai->peers[i].tasks_completed++;
                break;
            }
        }
    }

    ai->total_inferences++;
    ai->total_remote_inferences++;
    ai->total_cycles_consumed += t->cycles_consumed;

    ai_update_coverage(ai);
    return 0;
}

uint32_t ai_check_timeouts(ai_engine_t *ai, uint64_t current_cycle) {
    if (!ai) return 0;
    uint32_t timed_out = 0;

    for (uint32_t i = 0; i < AI_MAX_TASKS; i++) {
        ai_task_t *t = &ai->tasks[i];
        if (!t->active) continue;
        if (t->state != AI_TASK_QUEUED) continue;
        if (t->mode != AI_TASK_REMOTE) continue;

        if (current_cycle - t->created_cycle >= AI_TASK_TIMEOUT_CYCLES) {
            t->state = AI_TASK_TIMEOUT;
            ai->total_timeouts++;
            timed_out++;

            /* Update peer failure stats */
            for (uint32_t j = 0; j < AI_MAX_PEERS; j++) {
                if (ai->peers[j].active) {
                    bool match = true;
                    for (int k = 0; k < WORD168_OCTETS; k++) {
                        if (ai->peers[j].peer_id.bytes[k] != t->remote_peer.bytes[k]) {
                            match = false;
                            break;
                        }
                    }
                    if (match) {
                        ai->peers[j].tasks_failed++;
                        break;
                    }
                }
            }
        }
    }

    if (timed_out > 0) ai_update_coverage(ai);
    return timed_out;
}

ai_model_t *ai_get_model(ai_engine_t *ai, uint32_t model_id) {
    if (!ai) return NULL;
    for (uint32_t i = 0; i < AI_MAX_MODELS; i++) {
        if (ai->models[i].active && ai->models[i].id == model_id) {
            return &ai->models[i];
        }
    }
    return NULL;
}

ai_task_t *ai_get_task(ai_engine_t *ai, uint32_t task_id) {
    if (!ai) return NULL;
    for (uint32_t i = 0; i < AI_MAX_TASKS; i++) {
        if (ai->tasks[i].active && ai->tasks[i].id == task_id) {
            return &ai->tasks[i];
        }
    }
    return NULL;
}

surplus_real_t ai_update_coverage(ai_engine_t *ai) {
    if (!ai) return SR_ZERO;

    /* r: success rate = completed / (completed + failed + timeout + rejected) */
    uint64_t total_resolved = 0;
    uint64_t completed = 0;
    for (uint32_t i = 0; i < AI_MAX_TASKS; i++) {
        if (!ai->tasks[i].active) continue;
        if (ai->tasks[i].state == AI_TASK_COMPLETE) {
            completed++;
            total_resolved++;
        } else if (ai->tasks[i].state == AI_TASK_FAILED ||
                   ai->tasks[i].state == AI_TASK_TIMEOUT ||
                   ai->tasks[i].state == AI_TASK_REJECTED) {
            total_resolved++;
        }
    }
    ai->m5.r = (total_resolved == 0) ? SR_ONE
        : SR_DIV(SR_FROM_INT((int64_t)completed), SR_FROM_INT((int64_t)total_resolved));

    /* ell: active inference rate = running+queued / total tasks */
    uint32_t in_progress = 0;
    uint32_t total_active = 0;
    for (uint32_t i = 0; i < AI_MAX_TASKS; i++) {
        if (!ai->tasks[i].active) continue;
        total_active++;
        if (ai->tasks[i].state == AI_TASK_QUEUED ||
            ai->tasks[i].state == AI_TASK_RUNNING) {
            in_progress++;
        }
    }
    ai->m5.ell = (total_active == 0) ? SR_ZERO
        : SR_DIV(SR_FROM_INT((int64_t)in_progress), SR_FROM_INT((int64_t)total_active));

    surplus_real_t product = SR_MUL(ai->m5.r, ai->m5.ell);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(COVERAGE_FLOOR_NUM), SR_FROM_INT(COVERAGE_FLOOR_DEN));
    ai->coverage_ratio = SR_DIV(product, floor);

    return ai->coverage_ratio;
}
