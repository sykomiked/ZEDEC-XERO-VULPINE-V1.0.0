/* audiogenomics_core.c — Audiogenomics Implementation
 * Spectral analysis via CRIT transform, EGV extraction from voice patterns.
 * Emotion index from spectral energy distribution.
 * Intent estimation via magneto-electric coupling (phase coherence).
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "audiogenomics_core.h"
#include "axiom_matrix_core.h"
#include "choice_core.h"
#include "crit_168_word.h"
#include <string.h>
#include <math.h>

void audiogenomics_init(audiogenomics_state_t *a, axiom_matrix_t *matrix) {
    memset(a, 0, sizeof(audiogenomics_state_t));
    a->matrix = matrix;
    a->sample_rate = 16000.0;
}

uint32_t audiogenomics_create_session(audiogenomics_state_t *a, const char *user_id) {
    if (a->num_sessions >= AUDIO_MAX_SESSIONS) return UINT32_MAX;
    uint32_t idx = a->num_sessions++;
    audio_session_t *s = &a->sessions[idx];
    memset(s, 0, sizeof(audio_session_t));
    s->session_id = idx;
    if (user_id) {
        size_t n = strlen(user_id);
        if (n > 63) n = 63;
        memcpy(s->user_id, user_id, n);
    }
    s->num_samples = 0;
    s->history_count = 0;
    s->consent_granted = false;
    return idx;
}

int audiogenomics_add_samples(audiogenomics_state_t *a, uint32_t session_idx,
                               const audio_sample_t *samples, uint32_t count) {
    if (session_idx >= a->num_sessions) return -1;
    audio_session_t *s = &a->sessions[session_idx];
    if (s->num_samples + count > AUDIO_MAX_SAMPLES) {
        count = AUDIO_MAX_SAMPLES - s->num_samples;
    }
    memcpy(&s->samples[s->num_samples], samples, count * sizeof(audio_sample_t));
    s->num_samples += count;
    return (int)count;
}

double audiogenomics_compute_emotion_index(const audio_sample_t *samples, uint32_t count) {
    if (count == 0) return 0.0;
    double energy_low = 0.0, energy_high = 0.0;
    uint32_t mid = count / 2;
    for (uint32_t i = 0; i < count; i++) {
        double mag = sqrt(samples[i].real * samples[i].real + samples[i].imag * samples[i].imag);
        if (i < mid) energy_low += mag;
        else energy_high += mag;
    }
    double total = energy_low + energy_high;
    if (total < 1e-15) return 0.0;
    double ratio = energy_high / total;
    return (ratio - 0.5) * 2.0;
}

double audiogenomics_compute_coherence(const egv_vector_t *egv) {
    double phase_mag = sqrt(egv->phase.r * egv->phase.r + egv->phase.i * egv->phase.i);
    double emotion_factor = (egv->emotion_index + 1.0) / 2.0;
    return phase_mag * emotion_factor * egv->coherence_score;
}

double audiogenomics_estimate_intent(const audio_sample_t *samples, uint32_t count,
                                      double emotion_index) {
    if (count == 0) return 0.0;
    double phase_sum = 0.0;
    for (uint32_t i = 0; i < count; i++) {
        double phase = atan2(samples[i].imag, samples[i].real);
        phase_sum += phase;
    }
    double avg_phase = phase_sum / count;
    return sin(avg_phase) * emotion_index;
}

int audiogenomics_extract_egv(audiogenomics_state_t *a, uint32_t session_idx) {
    if (session_idx >= a->num_sessions) return -1;
    audio_session_t *s = &a->sessions[session_idx];
    if (s->num_samples == 0) return -2;

    egv_vector_t *egv = &s->current_egv;
    memset(egv, 0, sizeof(egv_vector_t));

    egv->emotion_index = audiogenomics_compute_emotion_index(s->samples, s->num_samples);
    egv->intent_estimate = audiogenomics_estimate_intent(s->samples, s->num_samples,
                                                          egv->emotion_index);
    egv->coherence_score = 1.0 - fabs(egv->emotion_index) * 0.5;
    if (egv->coherence_score < 0) egv->coherence_score = 0;

    double phase_sum_r = 0, phase_sum_i = 0;
    for (uint32_t i = 0; i < s->num_samples; i++) {
        phase_sum_r += s->samples[i].real;
        phase_sum_i += s->samples[i].imag;
    }
    egv->phase.r = phase_sum_r / s->num_samples;
    egv->phase.i = phase_sum_i / s->num_samples;

    egv->choice = choice_get_state();
    egv->timestamp = s->history_count;

    uint64_t hash = 1469598103934665603ULL;
    for (uint32_t i = 0; i < s->num_samples; i++) {
        hash ^= (uint64_t) (int64_t) (s->samples[i].real * 1e6);
        hash *= 1099511628211ULL;
        hash ^= (uint64_t) (int64_t) (s->samples[i].imag * 1e6);
        hash *= 1099511628211ULL;
    }
    for (int i = 0; i < AUDIO_MAX_EGV; i++) {
        egv->genomic_hash[i] = (uint8_t)((hash >> (i % 8 * 8)) & 0xFF);
        hash = hash * 1099511628211ULL + i;
    }

    if (s->history_count < 32) {
        s->history[s->history_count++] = *egv;
    } else {
        memmove(&s->history[0], &s->history[1], 31 * sizeof(egv_vector_t));
        s->history[31] = *egv;
    }

    if (a->matrix) {
        double complex val = egv->emotion_index + I * egv->intent_estimate;
        axiom_matrix_set(a->matrix, egv->timestamp, (rational_t){1, 1},
                       TRIT_TRUE, egv->phase, egv->choice, val);
    }

    return 0;
}

int audiogenomics_encode_egv(const egv_vector_t *egv, word168_t *word) {
    if (!egv || !word) return -1;
    memset(word->bytes, 0, WORD168_OCTETS);
    for (int i = 0; i < WORD168_OCTETS && i < AUDIO_MAX_EGV; i++) {
        word->bytes[i] = egv->genomic_hash[i];
    }
    word168_alternate_endianness(word);
    return 0;
}

int audiogenomics_decode_egv(const word168_t *word, egv_vector_t *egv) {
    if (!word || !egv) return -1;
    word168_t tmp = *word;
    word168_alternate_endianness(&tmp);
    memset(egv->genomic_hash, 0, AUDIO_MAX_EGV);
    for (int i = 0; i < WORD168_OCTETS; i++) {
        egv->genomic_hash[i] = tmp.bytes[i];
    }
    return 0;
}
