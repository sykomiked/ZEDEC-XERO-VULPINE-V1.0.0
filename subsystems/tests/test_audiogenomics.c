/* test_audiogenomics.c — Audiogenomics Layer Tests
 * Tests session creation, sample ingestion, EGV extraction, emotion index,
 * coherence, intent estimation, CRIT encoding.
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "audiogenomics_core.h"
#include "axiom_matrix_core.h"

int main(void) {
    printf("=== Audiogenomics Layer Tests ===\n");

    axiom_matrix_t matrix;
    matrix.size = 512;
    double complex entries[512];
    memset(entries, 0, sizeof(entries));
    matrix.entries = entries;

    audiogenomics_state_t ag;
    audiogenomics_init(&ag, &matrix);
    assert(ag.sample_rate == 16000.0);

    uint32_t s0 = audiogenomics_create_session(&ag, "user_001");
    assert(s0 == 0);
    assert(ag.num_sessions == 1);
    printf("  [PASS] Session creation\n");

    audio_sample_t samples[100];
    for (uint32_t i = 0; i < 100; i++) {
        double t = (double)i / 16000.0;
        samples[i].real = sin(2.0 * M_PI * 440.0 * t);
        samples[i].imag = cos(2.0 * M_PI * 440.0 * t);
    }
    int added = audiogenomics_add_samples(&ag, s0, samples, 100);
    assert(added == 100);
    assert(ag.sessions[s0].num_samples == 100);
    printf("  [PASS] Sample ingestion (100 samples)\n");

    double emotion = audiogenomics_compute_emotion_index(samples, 100);
    assert(emotion >= -1.0 && emotion <= 1.0);
    printf("  [PASS] Emotion index computation (%.4f)\n", emotion);

    assert(audiogenomics_extract_egv(&ag, s0) == 0);
    egv_vector_t *egv = &ag.sessions[s0].current_egv;
    assert(egv->emotion_index == emotion);
    assert(egv->coherence_score >= 0.0);
    assert(egv->intent_estimate >= -1.0 && egv->intent_estimate <= 1.0);
    printf("  [PASS] EGV extraction (emotion=%.4f, coherence=%.4f, intent=%.4f)\n",
           egv->emotion_index, egv->coherence_score, egv->intent_estimate);

    double coherence = audiogenomics_compute_coherence(egv);
    assert(coherence >= 0.0);
    printf("  [PASS] Coherence computation (%.4f)\n", coherence);

    word168_t word;
    assert(audiogenomics_encode_egv(egv, &word) == 0);
    egv_vector_t decoded;
    assert(audiogenomics_decode_egv(&word, &decoded) == 0);
    assert(memcmp(egv->genomic_hash, decoded.genomic_hash, 21) == 0);
    printf("  [PASS] CRIT encode/decode round-trip\n");

    assert(ag.sessions[s0].history_count == 1);
    printf("  [PASS] EGV history tracking\n");

    printf("=== All audiogenomics tests passed ===\n\n");
    return 0;
}
