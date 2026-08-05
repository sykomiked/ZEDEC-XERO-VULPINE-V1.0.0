/* audiogenomics_core.h — Audiogenomics Sensory-Inference Layer
 * Voice/audio spectral-genomic fingerprinting, Emotional Genomic Vector (EGV)
 * extraction, real-time intent estimation via magneto-electric coupling.
 * Per Cosmic AI Master Build Plan Audiogenomics Addendum.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#ifndef AUDIOGENOMICS_CORE_H
#define AUDIOGENOMICS_CORE_H

#include "m5_types.h"

#define AUDIO_MAX_SAMPLES 4096
#define AUDIO_MAX_EGV 168
#define AUDIO_MAX_SESSIONS 256

typedef struct audio_sample {
    double real;
    double imag;
} audio_sample_t;

typedef struct egv_vector {
    uint8_t genomic_hash[AUDIO_MAX_EGV];
    double emotion_index;
    double coherence_score;
    double intent_estimate;
    phase_t phase;
    collapse_t choice;
    ordinal_t timestamp;
} egv_vector_t;

typedef struct audio_session {
    uint32_t session_id;
    char user_id[64];
    audio_sample_t samples[AUDIO_MAX_SAMPLES];
    uint32_t num_samples;
    egv_vector_t current_egv;
    egv_vector_t history[32];
    uint32_t history_count;
    bool consent_granted;
} audio_session_t;

typedef struct audiogenomics_state {
    audio_session_t sessions[AUDIO_MAX_SESSIONS];
    uint32_t num_sessions;
    axiom_matrix_t *matrix;
    double sample_rate;
} audiogenomics_state_t;

void audiogenomics_init(audiogenomics_state_t *a, axiom_matrix_t *matrix);
uint32_t audiogenomics_create_session(audiogenomics_state_t *a, const char *user_id);
int audiogenomics_add_samples(audiogenomics_state_t *a, uint32_t session_idx,
                               const audio_sample_t *samples, uint32_t count);
int audiogenomics_extract_egv(audiogenomics_state_t *a, uint32_t session_idx);
double audiogenomics_compute_emotion_index(const audio_sample_t *samples, uint32_t count);
double audiogenomics_compute_coherence(const egv_vector_t *egv);
double audiogenomics_estimate_intent(const audio_sample_t *samples, uint32_t count,
                                      double emotion_index);
int audiogenomics_encode_egv(const egv_vector_t *egv, word168_t *word);
int audiogenomics_decode_egv(const word168_t *word, egv_vector_t *egv);

#endif
