/* ai_layer.h — AI Integration & Remote Compute Layer
 *
 * Native ZXV AI integration subsystem. Enables on-device and remote
 * AI model inference using the M5 Axiomatic architecture. Key design
 * principles (post-quantum, native ZXV):
 *
 *   - AI models are identified by 168-bit content hashes (same as
 *     Community Chest app packages)
 *   - Model packages must be signed; verification uses the same
 *     pluggable signature scheme as Count House / Community Chest
 *   - Remote compute peers are gated by Porter House admission control
 *   - On-device inference runs within the post-quantum security boundary
 *   - No inference data leaves the device without explicit user consent
 *     and Porter House admission
 *   - Uses the event-driven scheduler for task dispatch and budget
 *     tracking
 *   - AI models can be distributed through Community Chest as signed
 *     packages (cc_app_category_t::CC_APP_AI_MODEL)
 *   - Remote compute offload targets trusted peer nodes, alliance-tier
 *     Count House nodes, or enterprise infrastructure
 *
 * This is NOT a wrapper around external AI frameworks. It is a native
 * kernel-level AI dispatch and inference coordination layer built on
 * the M5 Axiomatic architecture.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * License: SEL-3.3 (kernel component)
 */
#ifndef AI_LAYER_H
#define AI_LAYER_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"
#include "surplus.h"
#include "edp_risk.h"
#include "porter_house.h"

/* ===== Constants ===== */

#define AI_MAX_MODELS          32
#define AI_MAX_TASKS           64
#define AI_MAX_NAME_LEN        64
#define AI_MAX_PEERS           16
#define AI_SIG_LEN             64    /* post-quantum signature */
#define AI_PUBKEY_LEN          32    /* model provider public key */
#define AI_CONTENT_HASH_LEN    21    /* 168-bit content hash */
#define AI_MAX_INPUT_SIZE      4096
#define AI_MAX_OUTPUT_SIZE     4096
#define AI_REMOTE_PORT         8900  /* Porter House port for AI remote compute */
#define AI_TASK_TIMEOUT_CYCLES 5000  /* cycles before remote task times out */

/* ===== Model Types ===== */

typedef enum {
    AI_MODEL_UNUSED      = 0,
    AI_MODEL_LOADED      = 1,   /* loaded into local memory */
    AI_MODEL_AVAILABLE   = 2,   /* available on remote peer */
    AI_MODEL_DOWNLOADING = 3,   /* being fetched from Community Chest */
    AI_MODEL_VERIFIED    = 4,   /* signature verified, ready */
    AI_MODEL_REJECTED    = 5,   /* signature verification failed */
    AI_MODEL_UNLOADED    = 6    /* was loaded but has been unloaded */
} ai_model_state_t;

typedef enum {
    AI_MODEL_LLM         = 0,   /* large language model */
    AI_MODEL_VISION      = 1,   /* computer vision model */
    AI_MODEL_AUDIO       = 2,   /* audio processing model */
    AI_MODEL_PREDICTIVE  = 3,   /* predictive analytics model */
    AI_MODEL_EMBEDDING   = 4,   /* embedding/encoding model */
    AI_MODEL_CLASSIFIER  = 5,   /* classification model */
    AI_MODEL_CUSTOM      = 6    /* custom model type */
} ai_model_type_t;

/* ===== Task States ===== */

typedef enum {
    AI_TASK_UNUSED     = 0,
    AI_TASK_QUEUED     = 1,   /* queued for execution */
    AI_TASK_RUNNING    = 2,   /* currently executing */
    AI_TASK_COMPLETE   = 3,   /* completed successfully */
    AI_TASK_FAILED     = 4,   /* execution failed */
    AI_TASK_TIMEOUT    = 5,   /* timed out */
    AI_TASK_REJECTED   = 6    /* rejected by Porter House */
} ai_task_state_t;

typedef enum {
    AI_TASK_LOCAL      = 0,   /* on-device inference */
    AI_TASK_REMOTE     = 1    /* remote compute offload */
} ai_task_mode_t;

/* ===== Model Record ===== */

typedef struct ai_model {
    uint32_t id;
    char name[AI_MAX_NAME_LEN];
    ai_model_type_t type;
    ai_model_state_t state;

    word168_t provider_id;           /* 168-bit model provider peer ID */
    uint8_t content_hash[AI_CONTENT_HASH_LEN];
    uint8_t provider_pubkey[AI_PUBKEY_LEN];
    uint8_t signature[AI_SIG_LEN];
    bool sig_verified;

    uint64_t param_count;            /* model parameter count */
    uint64_t memory_bytes;           /* memory footprint in bytes */
    uint32_t inference_count;        /* total inferences run */
    uint64_t total_cycles;           /* total compute cycles consumed */

    bool active;
} ai_model_t;

/* ===== Inference Task ===== */

typedef struct ai_task {
    uint32_t id;
    uint32_t model_id;
    ai_task_mode_t mode;             /* local or remote */
    ai_task_state_t state;

    uint8_t input[AI_MAX_INPUT_SIZE];
    uint32_t input_len;
    uint8_t output[AI_MAX_OUTPUT_SIZE];
    uint32_t output_len;

    word168_t remote_peer;           /* target peer for remote tasks */
    uint64_t created_cycle;
    uint64_t completed_cycle;
    uint64_t cycles_consumed;

    bool active;
} ai_task_t;

/* ===== Remote Compute Peer ===== */

typedef struct ai_peer {
    word168_t peer_id;
    uint64_t available_memory;       /* bytes available for compute */
    uint32_t trust_weight;           /* Porter House trust weight */
    uint64_t tasks_completed;        /* tasks completed on this peer */
    uint64_t tasks_failed;
    bool active;
} ai_peer_t;

/* ===== AI Engine ===== */

typedef struct ai_engine {
    uint32_t device_id;
    char name[AI_MAX_NAME_LEN];

    ai_model_t models[AI_MAX_MODELS];
    uint32_t num_models;
    uint32_t next_model_id;

    ai_task_t tasks[AI_MAX_TASKS];
    uint32_t num_tasks;
    uint32_t next_task_id;

    ai_peer_t peers[AI_MAX_PEERS];
    uint32_t num_peers;

    porter_house_t *porter;          /* for remote peer admission */

    /* Stats */
    uint64_t total_inferences;
    uint64_t total_local_inferences;
    uint64_t total_remote_inferences;
    uint64_t total_cycles_consumed;
    uint64_t total_remote_failures;
    uint64_t total_timeouts;

    /* M5 coordinates */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;
} ai_engine_t;

/* ===== API ===== */

void ai_init(ai_engine_t *ai, uint32_t device_id, const char *name,
              porter_house_t *porter);

/* Register a model. Returns model ID on success, -1 if full, -2 if sig fails. */
int32_t ai_register_model(ai_engine_t *ai, const char *name,
                           ai_model_type_t type,
                           const word168_t *provider_id,
                           const uint8_t pubkey[AI_PUBKEY_LEN],
                           const uint8_t content_hash[AI_CONTENT_HASH_LEN],
                           const uint8_t signature[AI_SIG_LEN],
                           uint64_t param_count, uint64_t memory_bytes);

/* Verify a model's signature. */
bool ai_verify_model(ai_engine_t *ai, uint32_t model_id);

/* Add a remote compute peer. Returns 0 on success, -1 if full,
 * -2 if Porter House rejects. */
int32_t ai_add_peer(ai_engine_t *ai, const word168_t *peer_id,
                     uint32_t trust_weight, uint64_t available_memory);

/* Submit a local inference task. Returns task ID on success, -1 if full. */
int32_t ai_submit_local(ai_engine_t *ai, uint32_t model_id,
                         const uint8_t *input, uint32_t input_len,
                         uint64_t current_cycle);

/* Submit a remote inference task. Returns task ID on success,
 * -1 if full, -2 if no peers, -3 if Porter House rejects. */
int32_t ai_submit_remote(ai_engine_t *ai, uint32_t model_id,
                          const word168_t *remote_peer,
                          const uint8_t *input, uint32_t input_len,
                          uint64_t current_cycle);

/* Execute a local task (simulated inference). Returns 0 on success. */
int32_t ai_execute_local(ai_engine_t *ai, uint32_t task_id, uint64_t current_cycle);

/* Complete a remote task (simulated remote response). Returns 0 on success. */
int32_t ai_complete_remote(ai_engine_t *ai, uint32_t task_id,
                            const uint8_t *output, uint32_t output_len,
                            uint64_t current_cycle);

/* Check for timed-out remote tasks. Returns count of timed-out tasks. */
uint32_t ai_check_timeouts(ai_engine_t *ai, uint64_t current_cycle);

/* Get model by ID. */
ai_model_t *ai_get_model(ai_engine_t *ai, uint32_t model_id);

/* Get task by ID. */
ai_task_t *ai_get_task(ai_engine_t *ai, uint32_t task_id);

/* Update M5 coverage. */
surplus_real_t ai_update_coverage(ai_engine_t *ai);

#endif /* AI_LAYER_H */
