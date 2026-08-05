/* audiogenomics_pro.c — Audio Genomics Pro Engine Implementation
 *
 * Freestanding C port of Audio Genomics Pro: DNA/RNA-to-Audio synthesis,
 * FM/AM modulation, electromagnetic genomics processing, Hebrew/Gematria
 * encoding, and data-to-audio pipeline.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */

#include "audiogenomics_pro.h"

#ifdef TEST_HOST
#include <string.h>
#include <math.h>
#include <stdio.h>
#else
#include "freestanding.h"
#endif

/* ===== Constants ===== */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define AGP_TWO_PI (2.0 * M_PI)

/* 432 Hz retuning ratio */
#define AGP_432_RATIO (432.0 / 440.0)

/* ===== Genetic Code Table (standard) ===== */

static const agp_genetic_code_t s_genetic_code[64] = {
    {"UUU",'F'}, {"UUC",'F'}, {"UUA",'L'}, {"UUG",'L'},
    {"UCU",'S'}, {"UCC",'S'}, {"UCA",'S'}, {"UCG",'S'},
    {"UAU",'Y'}, {"UAC",'Y'}, {"UAA",'*'}, {"UAG",'*'},
    {"UGU",'C'}, {"UGC",'C'}, {"UGA",'*'}, {"UGG",'W'},
    {"CUU",'L'}, {"CUC",'L'}, {"CUA",'L'}, {"CUG",'L'},
    {"CCU",'P'}, {"CCC",'P'}, {"CCA",'P'}, {"CCG",'P'},
    {"CAU",'H'}, {"CAC",'H'}, {"CAA",'Q'}, {"CAG",'Q'},
    {"CGU",'R'}, {"CGC",'R'}, {"CGA",'R'}, {"CGG",'R'},
    {"AUU",'I'}, {"AUC",'I'}, {"AUA",'I'}, {"AUG",'M'},
    {"ACU",'T'}, {"ACC",'T'}, {"ACA",'T'}, {"ACG",'T'},
    {"AAU",'N'}, {"AAC",'N'}, {"AAA",'K'}, {"AAG",'K'},
    {"AGU",'S'}, {"AGC",'S'}, {"AGA",'R'}, {"AGG",'R'},
    {"GUU",'V'}, {"GUC",'V'}, {"GUA",'V'}, {"GUG",'V'},
    {"GCU",'A'}, {"GCC",'A'}, {"GCA",'A'}, {"GCG",'A'},
    {"GAU",'D'}, {"GAC",'D'}, {"GAA",'E'}, {"GAG",'E'},
    {"GGU",'G'}, {"GGC",'G'}, {"GGA",'G'}, {"GGG",'G'}
};

/* Reverse genetic code (amino acid -> preferred codon, RNA) */
static __attribute__((unused)) const char s_aa_to_codon[26] = {
    'A', 'R', 'N', 'D', 'C', 'E', 'Q', 'G', 'H', 'I',
    'L', 'K', 'M', 'F', 'P', 'S', 'T', 'W', 'Y', 'V',
    'S', 'N', '*', 'R', 'A', 'L'
};

static const char *s_aa_codon_map[] = {
    "GCU","CGU","AAU","GAU","UGU","GAA","CAA","GGU","CAU","AUU",
    "CUG","AAA","AUG","UUU","CCU","UCU","ACU","UGG","UAU","GUU",
    "AGU","AAU","UAA","AGA","GCA","CUA"
};

/* Hebrew gematria values (22 letters + 5 final forms) */
static const uint32_t s_gematria[27] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
    20, 30, 40, 50, 60, 70, 80, 90, 100, 200,
    300, 400, 500, 600, 700, 800, 900
};

/* Binary to DNA mapping */
static const char s_bin_to_dna[4] = {'A', 'C', 'G', 'T'};
static const char s_bin_to_rna[4] = {'A', 'C', 'G', 'U'};

/* ===== Initialization ===== */

void agp_init_freq_map(agp_freq_map_t *fm, bool retune_432) {
    fm->base_freq[0] = AGP_FREQ_A;  /* A */
    fm->base_freq[1] = AGP_FREQ_T;  /* T/U */
    fm->base_freq[2] = AGP_FREQ_C;  /* C */
    fm->base_freq[3] = AGP_FREQ_G;  /* G */
    fm->base_freq[4] = AGP_EM_FREQ_N; /* N (unknown) */
    fm->separator_freq = AGP_FREQ_SEP;
    fm->codon_sep_freq = AGP_FREQ_CODON_SEP;
    fm->retune_432 = retune_432;

    if (retune_432) {
        for (int i = 0; i < 5; i++)
            fm->base_freq[i] *= AGP_432_RATIO;
        fm->separator_freq *= AGP_432_RATIO;
        fm->codon_sep_freq *= AGP_432_RATIO;
    }
}

void agp_init_modulation(agp_modulation_t *mod) {
    mod->fm_carrier_freq = AGP_FM_CARRIER_DEFAULT;
    mod->fm_modulation_index = AGP_FM_INDEX_DEFAULT;
    mod->am_modulation_depth = AGP_AM_DEPTH_DEFAULT;
    mod->subaudible = true;
    mod->target_db = AGP_TARGET_DB_SUBAUDIBLE;
}

void agp_init_em_params(agp_em_params_t *em) {
    em->electric_amplitude = 0.6;
    em->magnetic_amplitude = 0.4;
    em->electric_phase = 0.0;
    em->magnetic_phase = M_PI / 2.0;  /* 90° phase shift (Maxwell) */
}

void agp_init_config(agp_pipeline_config_t *cfg) {
    cfg->sample_rate = AGP_SAMPLE_RATE_HQ;
    cfg->bit_depth = 32;
    cfg->base_cycles = 3;
    cfg->waveform = AGP_WAVE_SINE;
    agp_init_modulation(&cfg->modulation);
    agp_init_freq_map(&cfg->freq_map, true);  /* Default: 432 Hz */
    cfg->use_hebrew = false;
    cfg->normalize_output = true;
}

/* ===== DNA Conversion ===== */

uint32_t agp_binary_to_dna(const uint8_t *data, uint32_t data_len,
                            char *out, uint32_t out_max, agp_mode_t mode) {
    const char *map = (mode == AGP_MODE_RNA) ? s_bin_to_rna : s_bin_to_dna;
    uint32_t out_len = 0;

    for (uint32_t i = 0; i < data_len && out_len + 4 <= out_max; i++) {
        uint8_t byte = data[i];
        for (int j = 6; j >= 0; j -= 2) {
            uint8_t idx = (byte >> j) & 0x03;
            out[out_len++] = map[idx];
        }
    }

    if (mode == AGP_MODE_RNA) {
        for (uint32_t i = 0; i < out_len; i++)
            if (out[i] == 'T') out[i] = 'U';
    }

    out[out_len] = '\0';
    return out_len;
}

uint32_t agp_text_to_dna(const char *text, char *out, uint32_t out_max,
                          agp_mode_t mode) {
    uint32_t len = 0;
    const char *p = text;

    while (*p && len + 4 <= out_max) {
        uint8_t ch = (uint8_t)*p++;
        const char *map = (mode == AGP_MODE_RNA) ? s_bin_to_rna : s_bin_to_dna;
        for (int j = 6; j >= 0; j -= 2) {
            uint8_t idx = (ch >> j) & 0x03;
            out[len++] = map[idx];
        }
    }

    out[len] = '\0';
    return len;
}

uint32_t agp_text_to_dna_hebrew(const char *text, char *out, uint32_t out_max,
                                 agp_mode_t mode) {
    /* Simple transliteration: each character's ASCII value mod 27
     * maps to a Hebrew letter index, which maps to a codon.
     * In full implementation, would use proper Hebrew mapping. */
    uint32_t len = 0;
    const char *p = text;

    while (*p && len + 3 <= out_max) {
        uint32_t gem_val = ((uint32_t)(uint8_t)*p++) % 27;
        uint32_t codon_idx = gem_val % 64;

        /* Use genetic code table to get codon bases */
        char rna_codon[3] = {
            s_genetic_code[codon_idx].codon[0],
            s_genetic_code[codon_idx].codon[1],
            s_genetic_code[codon_idx].codon[2]
        };

        for (int i = 0; i < 3; i++) {
            char base = rna_codon[i];
            if (mode == AGP_MODE_DNA && base == 'U') base = 'T';
            out[len++] = base;
        }
    }

    out[len] = '\0';
    return len;
}

uint32_t agp_protein_to_dna(const char *protein, char *out, uint32_t out_max,
                             agp_mode_t mode) {
    uint32_t len = 0;
    const char *p = protein;

    while (*p && len + 3 <= out_max) {
        char aa = *p++;
        if (aa >= 'A' && aa <= 'Z') {
            int idx = aa - 'A';
            if (idx < 26 && s_aa_codon_map[idx]) {
                const char *codon = s_aa_codon_map[idx];
                for (int i = 0; i < 3; i++) {
                    char base = codon[i];
                    if (mode == AGP_MODE_DNA && base == 'U') base = 'T';
                    out[len++] = base;
                }
            }
        }
    }

    out[len] = '\0';
    return len;
}

uint32_t agp_dna_to_protein(const char *dna, char *out, uint32_t out_max) {
    uint32_t len = 0;
    uint32_t dna_len = 0;
    while (dna[dna_len]) dna_len++;

    for (uint32_t i = 0; i + 2 < dna_len && len + 1 < out_max; i += 3) {
        char codon[3];
        codon[0] = dna[i];
        codon[1] = dna[i+1];
        codon[2] = dna[i+2];

        /* Convert T to U for RNA lookup */
        char rna[3];
        for (int j = 0; j < 3; j++)
            rna[j] = (codon[j] == 'T') ? 'U' : codon[j];

        out[len++] = agp_translate_codon(rna);
        if (out[len - 1] == '*') {
            len--;
            break;
        }
    }

    out[len] = '\0';
    return len;
}

/* ===== Validation ===== */

bool agp_validate_sequence(const char *seq, uint32_t len) {
    bool has_t = false, has_u = false;
    for (uint32_t i = 0; i < len; i++) {
        char c = seq[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c != 'A' && c != 'T' && c != 'C' && c != 'G' &&
            c != 'U' && c != 'N')
            return false;
        if (c == 'T') has_t = true;
        if (c == 'U') has_u = true;
    }
    if (has_t && has_u) return false;
    return true;
}

void agp_sequence_stats(const char *seq, uint32_t len,
                         uint32_t *gc_content, uint32_t *base_counts) {
    /* base_counts: [0]=A, [1]=T, [2]=C, [3]=G, [4]=U, [5]=N */
    for (int i = 0; i < 6; i++) base_counts[i] = 0;

    for (uint32_t i = 0; i < len; i++) {
        char c = seq[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        switch (c) {
            case 'A': base_counts[0]++; break;
            case 'T': base_counts[1]++; break;
            case 'C': base_counts[2]++; break;
            case 'G': base_counts[3]++; break;
            case 'U': base_counts[4]++; break;
            default:  base_counts[5]++; break;
        }
    }

    uint32_t gc = base_counts[2] + base_counts[3];
    uint32_t total = len - base_counts[5];
    *gc_content = (total > 0) ? (gc * 100 / total) : 0;
}

/* ===== Frequency Mapping ===== */

double agp_get_base_frequency(const agp_freq_map_t *fm, char base, int harmonic) {
    if (base >= 'a' && base <= 'z') base -= 32;
    int idx;
    switch (base) {
        case 'A': idx = 0; break;
        case 'T': case 'U': idx = 1; break;
        case 'C': idx = 2; break;
        case 'G': idx = 3; break;
        default:  idx = 4; break;
    }
    return fm->base_freq[idx] * (double)harmonic;
}

void agp_get_sequence_frequencies(const agp_freq_map_t *fm,
                                   const char *seq, uint32_t len,
                                   double *freqs, uint32_t *count, uint32_t max) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < len && n < max; i++) {
        char c = seq[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c == 'A' || c == 'T' || c == 'U' || c == 'C' || c == 'G' || c == 'N') {
            freqs[n++] = agp_get_base_frequency(fm, c, 1);
            if (i < len - 1 && n < max)
                freqs[n++] = fm->separator_freq;
            if ((i + 1) % 3 == 0 && i < len - 1 && n < max)
                freqs[n++] = fm->codon_sep_freq;
        }
    }
    *count = n;
}

double agp_get_codon_frequency(const agp_freq_map_t *fm, const char *codon) {
    double sum = 0;
    int count = 0;
    for (int i = 0; i < 3; i++) {
        char c = codon[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c == 'A' || c == 'T' || c == 'U' || c == 'C' || c == 'G') {
            sum += agp_get_base_frequency(fm, c, 1);
            count++;
        }
    }
    return (count > 0) ? (sum / count) : 0.0;
}

double agp_calculate_beat_frequency(const agp_freq_map_t *fm, char b1, char b2) {
    double f1 = agp_get_base_frequency(fm, b1, 1);
    double f2 = agp_get_base_frequency(fm, b2, 1);
    double diff = f1 - f2;
    if (diff < 0) diff = -diff;
    return diff;
}

/* ===== Tone Generation ===== */

static double agp_waveform_value(agp_waveform_t wave, double phase) {
    switch (wave) {
        case AGP_WAVE_SQUARE:
            return (phase < M_PI) ? 1.0 : -1.0;
        case AGP_WAVE_SAWTOOTH:
            return (2.0 * (phase / AGP_TWO_PI)) - 1.0;
        case AGP_WAVE_TRIANGLE: {
            double p = phase / AGP_TWO_PI;
            return (p < 0.5) ? (4.0 * p - 1.0) : (3.0 - 4.0 * p);
        }
        case AGP_WAVE_SINE:
        default:
            return sin(phase);
    }
}

void agp_generate_tone(double freq, double duration, uint32_t sample_rate,
                       agp_waveform_t wave, float *out, uint32_t *out_len,
                       uint32_t max_samples) {
    uint32_t n = (uint32_t)(sample_rate * duration);
    if (n > max_samples) n = max_samples;
    if (n < 2) { *out_len = 0; return; }

    double phase_inc = AGP_TWO_PI * freq / (double)sample_rate;
    double phase = 0;

    for (uint32_t i = 0; i < n; i++) {
        out[i] = (float)agp_waveform_value(wave, phase);
        phase += phase_inc;
        if (phase >= AGP_TWO_PI) phase -= AGP_TWO_PI;
    }

    agp_apply_adsr(out, n, sample_rate, 5.0, 10.0);
    *out_len = n;
}

void agp_generate_sequence_audio(const agp_freq_map_t *fm,
                                  const char *seq, uint32_t len,
                                  uint32_t sample_rate, uint32_t base_cycles,
                                  agp_waveform_t wave,
                                  agp_audio_buffer_t *buf) {
    if (len == 0 || !seq || !buf) { buf->length = 0; return; }

    double base_duration = (double)base_cycles / (double)len;
    if (base_duration * len < 3.0) base_duration = 3.0 / len;
    if (base_duration * len > 6.0) base_duration = 6.0 / len;

    uint32_t total = 0;
    for (uint32_t i = 0; i < len && total < AGP_MAX_AUDIO; i++) {
        char c = seq[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        double freq = agp_get_base_frequency(fm, c, 1);
        uint32_t n = (uint32_t)(sample_rate * base_duration);
        if (total + n > AGP_MAX_AUDIO) n = AGP_MAX_AUDIO - total;
        if (n == 0) break;

        double phase_inc = AGP_TWO_PI * freq / (double)sample_rate;
        double phase = 0;
        for (uint32_t j = 0; j < n; j++) {
            buf->samples[total + j] = (float)agp_waveform_value(wave, phase);
            phase += phase_inc;
            if (phase >= AGP_TWO_PI) phase -= AGP_TWO_PI;
        }
        total += n;
    }

    buf->length = total;
    buf->sample_rate = sample_rate;

    if (total > 0) {
        agp_apply_adsr(buf->samples, total, sample_rate, 5.0, 10.0);
    }
}

/* ===== Modulation ===== */

void agp_fm_modulate(const float *modulator, uint32_t len,
                     double carrier_freq, double mod_index,
                     uint32_t sample_rate, float *out) {
    double freq_dev = carrier_freq * mod_index;
    double integral = 0;

    for (uint32_t i = 0; i < len; i++) {
        double mod = modulator[i];
        if (mod > 1.0) mod = 1.0;
        if (mod < -1.0) mod = -1.0;
        integral += mod / (double)sample_rate;
        double t = (double)i / (double)sample_rate;
        out[i] = (float)sin(AGP_TWO_PI * carrier_freq * t +
                            AGP_TWO_PI * freq_dev * integral);
    }
}

void agp_am_modulate(const float *carrier, const float *modulator,
                     uint32_t len, double depth, float *out) {
    for (uint32_t i = 0; i < len; i++) {
        double mod = modulator[i];
        if (mod > 1.0) mod = 1.0;
        if (mod < -1.0) mod = -1.0;
        out[i] = (float)(carrier[i] * (1.0 + depth * mod));
    }
    /* Prevent clipping */
    float max_val = 0;
    for (uint32_t i = 0; i < len; i++) {
        float v = out[i];
        if (v < 0) v = -v;
        if (v > max_val) max_val = v;
    }
    if (max_val > 0.95f) {
        float scale = 0.95f / max_val;
        for (uint32_t i = 0; i < len; i++)
            out[i] *= scale;
    }
}

void agp_subaudible_embed(float *signal, uint32_t len, double target_db) {
    double rms = agp_rms(signal, len);
    if (rms < 1e-15) return;

    double target_amp = pow(10.0, target_db / 20.0);
    double scale = target_amp / rms;

    for (uint32_t i = 0; i < len; i++)
        signal[i] = (float)(signal[i] * scale);
}

void agp_nested_modulation(agp_audio_buffer_t *layers, uint32_t num_layers,
                            const agp_modulation_t *mod,
                            agp_audio_buffer_t *out) {
    if (num_layers == 0 || !layers || !out) { out->length = 0; return; }
    if (num_layers > AGP_MAX_LAYERS) num_layers = AGP_MAX_LAYERS;

    /* Find shortest layer length */
    uint32_t min_len = layers[0].length;
    for (uint32_t i = 1; i < num_layers; i++)
        if (layers[i].length < min_len) min_len = layers[i].length;

    /* FM modulate each layer with different Solfeggio carriers */
    double carriers[] = {528.0, 639.0, 741.0, 852.0, 396.0, 417.0, 639.0, 741.0};
    float fm_buf[AGP_MAX_AUDIO];

    /* Start with first FM-modulated layer */
    agp_fm_modulate(layers[0].samples, min_len,
                    carriers[0 % 8], mod->fm_modulation_index,
                    layers[0].sample_rate, out->samples);
    out->length = min_len;
    out->sample_rate = layers[0].sample_rate;

    /* Nest AM modulation for remaining layers */
    float temp[AGP_MAX_AUDIO];
    for (uint32_t i = 1; i < num_layers; i++) {
        agp_fm_modulate(layers[i].samples, min_len,
                        carriers[i % 8], mod->fm_modulation_index,
                        layers[i].sample_rate, fm_buf);

        double depth = mod->am_modulation_depth;
        for (uint32_t j = 1; j < i; j++) depth *= 0.5;  /* Decreasing depth */

        agp_am_modulate(out->samples, fm_buf, min_len, depth, temp);
        for (uint32_t j = 0; j < min_len; j++)
            out->samples[j] = temp[j];
    }

    if (mod->subaudible) {
        agp_subaudible_embed(out->samples, out->length, mod->target_db);
    }
}

/* ===== Electromagnetic Genomics ===== */

void agp_em_generate_tone(double freq, double duration, uint32_t sample_rate,
                           bool is_electric, const agp_em_params_t *em,
                           float *out, uint32_t *out_len, uint32_t max_samples) {
    uint32_t n = (uint32_t)(sample_rate * duration);
    if (n > max_samples) n = max_samples;
    if (n < 2) { *out_len = 0; return; }

    double amplitude = is_electric ? em->electric_amplitude : em->magnetic_amplitude;
    double phase_offset = is_electric ? em->electric_phase : em->magnetic_phase;
    double omega = AGP_TWO_PI * freq;

    for (uint32_t i = 0; i < n; i++) {
        double t = (double)i / (double)sample_rate;
        double wave = amplitude * sin(omega * t + phase_offset);
        /* Second harmonic (electromagnetic resonance) */
        wave += 0.15 * amplitude * sin(2.0 * omega * t + phase_offset);
        /* Impedance modulation */
        wave *= 1.0 + 0.05 * sin(AGP_TWO_PI * freq * 0.1 * t);
        out[i] = (float)wave;
    }

    agp_apply_adsr(out, n, sample_rate, 5.0, 10.0);
    *out_len = n;
}

void agp_em_generate_sequence(const char *dna, uint32_t len,
                               uint32_t sample_rate,
                               const agp_em_params_t *em,
                               agp_audio_buffer_t *buf) {
    if (len == 0 || !dna || !buf) { buf->length = 0; return; }

    double target_duration = 4.5;
    double base_duration = target_duration / (double)len;
    if (base_duration * len < 3.0) base_duration = 3.0 / len;
    if (base_duration * len > 6.0) base_duration = 6.0 / len;

    uint32_t total = 0;
    for (uint32_t i = 0; i < len; i++) {
        char c = dna[i];
        if (c >= 'a' && c <= 'z') c -= 32;

        double freq;
        bool is_electric;
        switch (c) {
            case 'A': freq = AGP_EM_FREQ_A; is_electric = true; break;
            case 'T': case 'U': freq = AGP_EM_FREQ_T; is_electric = false; break;
            case 'G': freq = AGP_EM_FREQ_G; is_electric = true; break;
            case 'C': freq = AGP_EM_FREQ_C; is_electric = false; break;
            default:  freq = AGP_EM_FREQ_N; is_electric = true; break;
        }

        uint32_t n = (uint32_t)(sample_rate * base_duration);
        if (total + n > AGP_MAX_AUDIO) n = AGP_MAX_AUDIO - total;
        if (n == 0) break;

        double amplitude = is_electric ? em->electric_amplitude : em->magnetic_amplitude;
        double phase_offset = is_electric ? em->electric_phase : em->magnetic_phase;
        double omega = AGP_TWO_PI * freq;

        for (uint32_t j = 0; j < n; j++) {
            double t = (double)j / (double)sample_rate;
            double wave = amplitude * sin(omega * t + phase_offset);
            wave += 0.15 * amplitude * sin(2.0 * omega * t + phase_offset);
            wave *= 1.0 + 0.05 * sin(AGP_TWO_PI * freq * 0.1 * t);
            buf->samples[total + j] = (float)wave;
        }
        total += n;
    }

    buf->length = total;
    buf->sample_rate = sample_rate;

    if (total > 0) {
        agp_normalize(buf->samples, total, 0.95);
    }
}

/* ===== Sonic Chemistry (Making Chemistry with Sound) ===== */

/* Element symbols for first 118 elements */
static const char *s_element_symbols[AGP_MAX_ELEMENTS] = {
    "n","H","He","Li","Be","B","C","N","O","F","Ne",
    "Na","Mg","Al","Si","P","S","Cl","Ar","K","Ca",
    "Sc","Ti","V","Cr","Mn","Fe","Co","Ni","Cu","Zn",
    "Ga","Ge","As","Se","Br","Kr","Rb","Sr","Y","Zr",
    "Nb","Mo","Tc","Ru","Rh","Pd","Ag","Cd","In","Sn",
    "Sb","Te","I","Xe","Cs","Ba","La","Ce","Pr","Nd",
    "Pm","Sm","Eu","Gd","Tb","Dy","Ho","Er","Tm","Yb",
    "Lu","Hf","Ta","W","Re","Os","Ir","Pt","Au","Hg",
    "Tl","Pb","Bi","Po","At","Rn","Fr","Ra","Ac","Th",
    "Pa","U","Np","Pu","Am","Cm","Bk","Cf","Es","Fm",
    "Md","No","Lr","Rf","Db","Sg","Bh","Hs","Mt","Ds",
    "Rg","Cn","Nh","Fl","Mc","Lv","Ts","Og"
};

/* Element names for first 118 elements */
static const char *s_element_names[AGP_MAX_ELEMENTS] = {
    "Neutron","Hydrogen","Helium","Lithium","Beryllium","Boron","Carbon",
    "Nitrogen","Oxygen","Fluorine","Neon","Sodium","Magnesium","Aluminium",
    "Silicon","Phosphorus","Sulfur","Chlorine","Argon","Potassium","Calcium",
    "Scandium","Titanium","Vanadium","Chromium","Manganese","Iron","Cobalt",
    "Nickel","Copper","Zinc","Gallium","Germanium","Arsenic","Selenium",
    "Bromine","Krypton","Rubidium","Strontium","Yttrium","Zirconium",
    "Niobium","Molybdenum","Technetium","Ruthenium","Rhodium","Palladium",
    "Silver","Cadmium","Indium","Tin","Antimony","Tellurium","Iodine",
    "Xenon","Caesium","Barium","Lanthanum","Cerium","Praseodymium",
    "Neodymium","Promethium","Samarium","Europium","Gadolinium","Terbium",
    "Dysprosium","Holmium","Erbium","Thulium","Ytterbium","Lutetium",
    "Hafnium","Tantalum","Tungsten","Rhenium","Osmium","Iridium","Platinum",
    "Gold","Mercury","Thallium","Lead","Bismuth","Polonium","Astatine",
    "Radon","Francium","Radium","Actinium","Thorium","Protactinium",
    "Uranium","Neptunium","Plutonium","Americium","Curium","Berkelium",
    "Californium","Einsteinium","Fermium","Mendelevium","Nobelium",
    "Lawrencium","Rutherfordium","Dubnium","Seaborgium","Bohrium","Hassium",
    "Meitnerium","Darmstadtium","Roentgenium","Copernicium","Nihonium",
    "Flerovium","Moscovium","Livermorium","Tennessine","Oganesson"
};

double agp_chemistry_element_frequency(uint8_t atomic_number) {
    /* Formula: [(N / Phi) * 1.125]^2 = E (Hz)
     * N = atomic_number (proton count)
     * Phi = golden ratio
     * 1.125 = symmetry factor (360° + 45° symmetry breaking)
     */
    if (atomic_number == 0) return 0.0;
    double n = (double)atomic_number;
    double val = (n * AGP_PHI_INV) * AGP_SYMMETRY;
    return val * val;
}

void agp_chemistry_init_table(agp_element_t *table, uint32_t *count) {
    uint32_t n = (AGP_MAX_ELEMENTS < 119) ? AGP_MAX_ELEMENTS : 119;
    for (uint32_t i = 0; i < n; i++) {
        table[i].atomic_number = (uint8_t)i;
        table[i].frequency = agp_chemistry_element_frequency((uint8_t)i);
        /* Copy symbol */
        const char *sym = s_element_symbols[i];
        int j;
        for (j = 0; j < 2 && sym[j]; j++)
            table[i].symbol[j] = sym[j];
        table[i].symbol[j] = '\0';
        /* Copy name */
        const char *name = s_element_names[i];
        for (j = 0; j < 15 && name[j]; j++)
            table[i].name[j] = name[j];
        table[i].name[j] = '\0';
    }
    *count = n;
}

double agp_chemistry_compound_frequency(const agp_compound_component_t *components,
                                         uint8_t num_components, uint16_t total_atoms) {
    /* Formula: {[E1^(P1/W)] * [E2^(P2/W)] * ...}^2 = C (Hz)
     * E = elemental frequency, P = atom count, W = total atoms
     */
    if (num_components == 0 || total_atoms == 0) return 0.0;

    double product = 1.0;
    for (uint8_t i = 0; i < num_components; i++) {
        double e = components[i].element_freq;
        double p = (double)components[i].atom_count;
        double w = (double)total_atoms;
        if (e > 0.0) {
            /* E^(P/W) using exp and log for freestanding compatibility */
            product *= exp((p / w) * log(e));
        }
    }
    return product * product;  /* Square the result */
}

int agp_chemistry_build_compound(const uint8_t *atomic_numbers,
                                  const uint8_t *atom_counts,
                                  uint8_t num_types,
                                  agp_compound_t *out) {
    if (!atomic_numbers || !atom_counts || !out || num_types == 0)
        return -1;
    if (num_types > AGP_MAX_COMPOUND) return -2;

    out->num_components = num_types;
    out->total_atoms = 0;

    for (uint8_t i = 0; i < num_types; i++) {
        out->components[i].atomic_number = atomic_numbers[i];
        out->components[i].atom_count = atom_counts[i];
        out->components[i].element_freq =
            agp_chemistry_element_frequency(atomic_numbers[i]);
        out->total_atoms += atom_counts[i];
    }

    out->frequency = agp_chemistry_compound_frequency(
        out->components, out->num_components, out->total_atoms);

    return 0;
}

void agp_chemistry_generate_tone(double freq, double duration,
                                  uint32_t sample_rate, agp_waveform_t wave,
                                  float *out, uint32_t *out_len, uint32_t max) {
    /* Reuse standard tone generation with harmonic overtones */
    agp_generate_tone(freq, duration, sample_rate, wave, out, out_len, max);

    /* Add second harmonic for richness (chemical resonance) */
    if (*out_len > 0 && *out_len < max) {
        uint32_t base_len = *out_len;
        float harm2[AGP_MAX_AUDIO];
        uint32_t h2_len;
        agp_generate_tone(freq * 2.0, duration, sample_rate, wave,
                          harm2, &h2_len, AGP_MAX_AUDIO);
        uint32_t blend = (base_len < h2_len) ? base_len : h2_len;
        for (uint32_t i = 0; i < blend; i++)
            out[i] += harm2[i] * 0.3f;
        /* Add third harmonic (Phi-weighted) */
        agp_generate_tone(freq * 3.0, duration, sample_rate, wave,
                          harm2, &h2_len, AGP_MAX_AUDIO);
        blend = (base_len < h2_len) ? base_len : h2_len;
        for (uint32_t i = 0; i < blend; i++)
            out[i] += harm2[i] * 0.15f;
    }
}

void agp_chemistry_compound_to_audio(const agp_compound_t *compound,
                                      double duration, uint32_t sample_rate,
                                      agp_waveform_t wave,
                                      agp_audio_buffer_t *buf) {
    if (!compound || !buf || compound->frequency <= 0.0) {
        buf->length = 0;
        return;
    }

    /* Generate the compound's fundamental frequency */
    uint32_t len;
    agp_chemistry_generate_tone(compound->frequency, duration, sample_rate,
                                 wave, buf->samples, &len, AGP_MAX_AUDIO);
    buf->length = len;
    buf->sample_rate = sample_rate;

    /* Layer each element's frequency as harmonics, weighted by atom proportion */
    float layer[AGP_MAX_AUDIO];
    for (uint8_t i = 0; i < compound->num_components; i++) {
        double e_freq = compound->components[i].element_freq;
        double weight = (double)compound->components[i].atom_count /
                        (double)compound->total_atoms;

        uint32_t l_len;
        agp_chemistry_generate_tone(e_freq, duration, sample_rate, wave,
                                     layer, &l_len, AGP_MAX_AUDIO);

        uint32_t blend = (len < l_len) ? len : l_len;
        float w = (float)(weight * 0.3);
        for (uint32_t j = 0; j < blend; j++)
            buf->samples[j] += layer[j] * w;
    }

    /* Normalize final output */
    if (buf->length > 0)
        agp_normalize(buf->samples, buf->length, 0.95);
}

/* Relationship frequency: converts any numeric relationship to Hz.
 * Uses the same Phi-spiral principle: the relationship between two values
 * is itself a frequency. This generalizes the chemistry formula to any
 * paired relationship (social, economic, physical, abstract).
 *
 * freq = {[(A / Phi) * 1.125]^(Wa/W) * [(B / Phi) * 1.125]^(Wb/W)}^2
 * where W = Wa + Wb (total weight)
 */
double agp_relationship_frequency(double value_a, double value_b,
                                   double weight_a, double weight_b) {
    if (value_a <= 0.0 || value_b <= 0.0) return 0.0;
    double w = weight_a + weight_b;
    if (w <= 0.0) return 0.0;

    double e_a = ((value_a * AGP_PHI_INV) * AGP_SYMMETRY);
    e_a = e_a * e_a;  /* Square for equilateral symmetry */

    double e_b = ((value_b * AGP_PHI_INV) * AGP_SYMMETRY);
    e_b = e_b * e_b;

    /* Weighted geometric mean, squared */
    double product = exp((weight_a / w) * log(e_a > 0 ? e_a : 1.0)) *
                     exp((weight_b / w) * log(e_b > 0 ? e_b : 1.0));

    return product * product;
}

void agp_relationship_to_audio(double freq, double duration, uint32_t sample_rate,
                                agp_waveform_t wave, agp_audio_buffer_t *buf) {
    if (!buf || freq <= 0.0) { buf->length = 0; return; }
    uint32_t len;
    agp_chemistry_generate_tone(freq, duration, sample_rate, wave,
                                 buf->samples, &len, AGP_MAX_AUDIO);
    buf->length = len;
    buf->sample_rate = sample_rate;
}

/* ===== Utility ===== */

void agp_normalize(float *signal, uint32_t len, double target_peak) {
    float max_val = 0;
    for (uint32_t i = 0; i < len; i++) {
        float v = signal[i];
        if (v < 0) v = -v;
        if (v > max_val) max_val = v;
    }
    if (max_val > 0) {
        float scale = (float)(target_peak / (double)max_val);
        for (uint32_t i = 0; i < len; i++)
            signal[i] *= scale;
    }
}

double agp_rms(const float *signal, uint32_t len) {
    if (len == 0) return 0.0;
    double sum_sq = 0;
    for (uint32_t i = 0; i < len; i++)
        sum_sq += (double)signal[i] * (double)signal[i];
    return sqrt(sum_sq / (double)len);
}

void agp_apply_adsr(float *signal, uint32_t len, uint32_t sample_rate,
                    double attack_ms, double release_ms) {
    uint32_t attack_n = (uint32_t)(sample_rate * attack_ms / 1000.0);
    uint32_t release_n = (uint32_t)(sample_rate * release_ms / 1000.0);
    if (attack_n > len / 8) attack_n = len / 8;
    if (release_n > len / 8) release_n = len / 8;

    for (uint32_t i = 0; i < attack_n && i < len; i++)
        signal[i] *= (float)((double)i / (double)attack_n);

    for (uint32_t i = 0; i < release_n && (len - 1 - i) > attack_n; i++) {
        uint32_t idx = len - 1 - i;
        signal[idx] *= (float)((double)i / (double)release_n);
    }
}

/* ===== Pipeline ===== */

int agp_run_pipeline(const agp_pipeline_config_t *cfg,
                     const char *input_data, uint32_t input_len,
                     agp_audio_buffer_t *output) {
    if (!cfg || !input_data || !output) return -1;

    /* Step 1: Convert input to DNA sequence */
    char dna_seq[AGP_MAX_SEQUENCE + 1];
    uint32_t dna_len;

    if (cfg->use_hebrew) {
        dna_len = agp_text_to_dna_hebrew(input_data, dna_seq,
                                          AGP_MAX_SEQUENCE, AGP_MODE_DNA);
    } else {
        dna_len = agp_binary_to_dna((const uint8_t *)input_data, input_len,
                                     dna_seq, AGP_MAX_SEQUENCE, AGP_MODE_DNA);
    }

    if (dna_len == 0) return -2;

    /* Step 2: Generate audio from DNA sequence */
    agp_generate_sequence_audio(&cfg->freq_map, dna_seq, dna_len,
                                 cfg->sample_rate, cfg->base_cycles,
                                 cfg->waveform, output);

    if (output->length == 0) return -3;

    /* Step 3: FM modulation */
    float fm_buf[AGP_MAX_AUDIO];
    agp_fm_modulate(output->samples, output->length,
                    cfg->modulation.fm_carrier_freq,
                    cfg->modulation.fm_modulation_index,
                    cfg->sample_rate, fm_buf);

    /* Step 4: AM modulation (self-modulate for embedding) */
    agp_am_modulate(output->samples, fm_buf, output->length,
                    cfg->modulation.am_modulation_depth, output->samples);

    /* Step 5: Subaudible embedding */
    if (cfg->modulation.subaudible) {
        agp_subaudible_embed(output->samples, output->length,
                             cfg->modulation.target_db);
    }

    /* Step 6: Normalize */
    if (cfg->normalize_output) {
        agp_normalize(output->samples, output->length, 0.95);
    }

    return 0;
}

/* ===== Reporting ===== */

void agp_print_info(const agp_pipeline_config_t *cfg) {
(void)cfg;
#ifdef TEST_HOST
    printf("=== Audio Genomics Pro Configuration ===\n");
    printf("  Sample rate:    %u Hz\n", cfg->sample_rate);
    printf("  Bit depth:      %u\n", cfg->bit_depth);
    printf("  Base cycles:    %u\n", cfg->base_cycles);
    printf("  Waveform:       %d\n", cfg->waveform);
    printf("  FM carrier:     %.1f Hz\n", cfg->modulation.fm_carrier_freq);
    printf("  FM index:       %.3f\n", cfg->modulation.fm_modulation_index);
    printf("  AM depth:       %.3f\n", cfg->modulation.am_modulation_depth);
    printf("  432 Hz retune:  %s\n", cfg->freq_map.retune_432 ? "yes" : "no");
    printf("  Hebrew mode:    %s\n", cfg->use_hebrew ? "yes" : "no");
    printf("  Normalize:      %s\n", cfg->normalize_output ? "yes" : "no");
#else
    extern void fb_puts(const char *str);
    fb_puts("=== Audio Genomics Pro ===\n");
    fb_puts("  Engine initialized\n");
#endif
}

void agp_print_sequence_stats(const char *seq, uint32_t len) {
    uint32_t gc_content, base_counts[6];
    agp_sequence_stats(seq, len, &gc_content, base_counts);

#ifdef TEST_HOST
    printf("=== Sequence Statistics ===\n");
    printf("  Length:       %u\n", len);
    printf("  GC content:   %u%%\n", gc_content);
    printf("  A:%u  T:%u  C:%u  G:%u  U:%u  N:%u\n",
           base_counts[0], base_counts[1], base_counts[2],
           base_counts[3], base_counts[4], base_counts[5]);
#else
    (void)gc_content;
    (void)base_counts;
#endif
}

/* ===== Genetic Code Access ===== */

const agp_genetic_code_t *agp_get_genetic_code(void) {
    return s_genetic_code;
}

char agp_translate_codon(const char *codon) {
    /* Convert to uppercase RNA */
    char rna[3];
    for (int i = 0; i < 3; i++) {
        char c = codon[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c == 'T') c = 'U';
        rna[i] = c;
    }

    /* Linear search through genetic code table */
    for (int i = 0; i < 64; i++) {
        if (s_genetic_code[i].codon[0] == rna[0] &&
            s_genetic_code[i].codon[1] == rna[1] &&
            s_genetic_code[i].codon[2] == rna[2]) {
            return s_genetic_code[i].amino_acid;
        }
    }
    return 'X';  /* Unknown */
}

/* ===== Gematria / Hebrew ===== */

uint32_t agp_hebrew_gematria(const char *hebrew_text) {
    uint32_t total = 0;
    const char *p = hebrew_text;
    while (*p) {
        uint32_t idx = ((uint32_t)(uint8_t)*p++) % 27;
        total += s_gematria[idx];
    }
    return total;
}

char agp_gematria_to_base(uint32_t value) {
    switch (value % 4) {
        case 0: return 'A';
        case 1: return 'T';
        case 2: return 'C';
        default: return 'G';
    }
}
