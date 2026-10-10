/* audiogenomics_pro.c — Audio Genomics Pro Engine Implementation
 *
 * Freestanding C port of Audio Genomics Pro: DNA/RNA-to-Audio synthesis,
 * FM/AM modulation, electromagnetic genomics processing, Hebrew/Gematria
 * encoding, and data-to-audio pipeline.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#include "audiogenomics_pro.h"

#ifdef TEST_HOST
#    include <string.h>
#    include <stdio.h>
#else
#include "freestanding.h"
#endif

/* ===== Constants ===== */

/* 432 Hz retuning ratio, 432/440 = 54/55 (applied exactly in integers) */
#define AGP_432_NUM 54
#define AGP_432_DEN 55

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
            fm->base_freq[i] = fx_sdiv64(fm->base_freq[i] * AGP_432_NUM, AGP_432_DEN);
        fm->separator_freq = fx_sdiv64(fm->separator_freq * AGP_432_NUM, AGP_432_DEN);
        fm->codon_sep_freq = fx_sdiv64(fm->codon_sep_freq * AGP_432_NUM, AGP_432_DEN);
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
    em->electric_amplitude = Q16_CONST(3, 5); /* 0.6 */
    em->magnetic_amplitude = Q16_CONST(2, 5); /* 0.4 */
    em->electric_phase = 0;
    em->magnetic_phase = 0x40000000u; /* quarter turn: 90° phase shift (Maxwell) */
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

/* ===== Fixed-point helpers ===== */

static agp_sample_t agp_sat(int64_t v)
{
    if (v > INT32_MAX) return INT32_MAX;
    if (v < INT32_MIN) return INT32_MIN;
    return (agp_sample_t) v;
}

static int64_t agp_abs64(int64_t v)
{
    return v < 0 ? -v : v;
}

/* Samples in duration_ms at sample_rate (floor), saturating at UINT32_MAX. */
static uint32_t agp_samples_for(uint32_t sample_rate, uint32_t duration_ms)
{
    uint64_t n = fx_udiv64((uint64_t) sample_rate * duration_ms, 1000u, 0);
    return n > UINT32_MAX ? UINT32_MAX : (uint32_t) n;
}

/* Per-sample phase step of freq at sample_rate, in binary turns. Whole cycles
 * per sample wrap away, exactly as the old 2*pi wrap did. */
static uint32_t agp_turn_step(agp_hz_t freq, uint32_t sample_rate)
{
    if (sample_rate == 0) return 0;
    uint64_t f = (uint64_t) agp_abs64(freq);
    uint64_t rem;
    (void) fx_udiv64(f, (uint64_t) sample_rate << 16, &rem);
    uint32_t step = (uint32_t) fx_udiv64(rem << 16, sample_rate, 0);
    return freq < 0 ? (uint32_t) 0 - step : step;
}

/* v * num / den for a sample and non-negative ratio, truncating toward zero. */
static agp_sample_t agp_scale(agp_sample_t v, int64_t num, int64_t den)
{
    return agp_sat(fx_sdiv64((int64_t) v * num, den));
}

/* ===== Frequency Mapping ===== */

agp_hz_t agp_get_base_frequency(const agp_freq_map_t *fm, char base, int harmonic)
{
    if (base >= 'a' && base <= 'z') base -= 32;
    int idx;
    switch (base) {
        case 'A': idx = 0; break;
        case 'T': case 'U': idx = 1; break;
        case 'C': idx = 2; break;
        case 'G': idx = 3; break;
        default:  idx = 4; break;
    }
    return fm->base_freq[idx] * (agp_hz_t) harmonic;
}

void agp_get_sequence_frequencies(const agp_freq_map_t *fm, const char *seq, uint32_t len,
                                  agp_hz_t *freqs, uint32_t *count, uint32_t max)
{
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

agp_hz_t agp_get_codon_frequency(const agp_freq_map_t *fm, const char *codon)
{
    agp_hz_t sum = 0;
    int count = 0;
    for (int i = 0; i < 3; i++) {
        char c = codon[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c == 'A' || c == 'T' || c == 'U' || c == 'C' || c == 'G') {
            sum += agp_get_base_frequency(fm, c, 1);
            count++;
        }
    }
    return (count > 0) ? fx_sdiv64(sum, count) : 0;
}

agp_hz_t agp_calculate_beat_frequency(const agp_freq_map_t *fm, char b1, char b2)
{
    agp_hz_t f1 = agp_get_base_frequency(fm, b1, 1);
    agp_hz_t f2 = agp_get_base_frequency(fm, b2, 1);
    return agp_abs64(f1 - f2);
}

/* ===== Tone Generation ===== */

static agp_sample_t agp_waveform_value(agp_waveform_t wave, uint32_t phase)
{
    switch (wave) {
        case AGP_WAVE_SQUARE:
            return (phase < 0x80000000u) ? Q16_ONE : -Q16_ONE;
        case AGP_WAVE_SAWTOOTH:
            return (agp_sample_t) (phase >> 15) - Q16_ONE; /* 2p - 1 */
        case AGP_WAVE_TRIANGLE: {
            agp_sample_t q = (agp_sample_t) (phase >> 14); /* 4p, Q16.16 */
            return (phase < 0x80000000u) ? (q - Q16_ONE) : (3 * Q16_ONE - q);
        }
        case AGP_WAVE_SINE:
        default:
            return fx_sin_turn(phase);
    }
}

void agp_generate_tone(agp_hz_t freq, uint32_t duration_ms, uint32_t sample_rate,
                       agp_waveform_t wave, agp_sample_t *out, uint32_t *out_len,
                       uint32_t max_samples)
{
    uint32_t n = agp_samples_for(sample_rate, duration_ms);
    if (n > max_samples) n = max_samples;
    if (n < 2) { *out_len = 0; return; }

    uint32_t step = agp_turn_step(freq, sample_rate);
    uint32_t phase = 0;

    for (uint32_t i = 0; i < n; i++) {
        out[i] = agp_waveform_value(wave, phase);
        phase += step; /* wraps at one full cycle */
    }

    agp_apply_adsr(out, n, sample_rate, 5u, 10u);
    *out_len = n;
}

void agp_generate_sequence_audio(const agp_freq_map_t *fm,
                                  const char *seq, uint32_t len,
                                  uint32_t sample_rate, uint32_t base_cycles,
                                  agp_waveform_t wave,
                                  agp_audio_buffer_t *buf) {
    if (len == 0 || !seq || !buf) {
        if (buf) buf->length = 0;
        return;
    }

    /* Total duration base_cycles seconds, clamped to [3, 6] s, shared evenly
     * between the bases. */
    uint32_t total_s = base_cycles;
    if (total_s < 3u) total_s = 3u;
    if (total_s > 6u) total_s = 6u;
    uint32_t per_base = (uint32_t) fx_udiv64((uint64_t) sample_rate * total_s, len, 0);

    uint32_t total = 0;
    for (uint32_t i = 0; i < len && total < AGP_MAX_AUDIO; i++) {
        char c = seq[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        agp_hz_t freq = agp_get_base_frequency(fm, c, 1);
        uint32_t n = per_base;
        if (total + n > AGP_MAX_AUDIO) n = AGP_MAX_AUDIO - total;
        if (n == 0) break;

        uint32_t step = agp_turn_step(freq, sample_rate);
        uint32_t phase = 0;
        for (uint32_t j = 0; j < n; j++) {
            buf->samples[total + j] = agp_waveform_value(wave, phase);
            phase += step;
        }
        total += n;
    }

    buf->length = total;
    buf->sample_rate = sample_rate;

    if (total > 0) {
        agp_apply_adsr(buf->samples, total, sample_rate, 5u, 10u);
    }
}

/* ===== Modulation ===== */

void agp_fm_modulate(const agp_sample_t *modulator, uint32_t len, agp_hz_t carrier_freq,
                     agp_q16_t mod_index, uint32_t sample_rate, agp_sample_t *out)
{
    if (sample_rate == 0) {
        for (uint32_t i = 0; i < len; i++) out[i] = 0;
        return;
    }
    /* phase(i) = fc*i/sr + fdev * sum_{k<=i} mod[k]/sr, in turns */
    int64_t freq_dev = fx_mul_q16(carrier_freq, mod_index); /* Hz, Q16.16 */
    const int64_t dev_cap = (int64_t) 1 << 40;              /* keeps dev*mod in int64 */
    if (freq_dev > dev_cap) freq_dev = dev_cap;
    if (freq_dev < -dev_cap) freq_dev = -dev_cap;
    uint32_t carrier_step = agp_turn_step(carrier_freq, sample_rate);
    uint32_t carrier = 0, integral = 0;

    for (uint32_t i = 0; i < len; i++) {
        int64_t mod = modulator[i];
        if (mod > Q16_ONE) mod = Q16_ONE;
        if (mod < -Q16_ONE) mod = -Q16_ONE;
        /* fdev*mod/sr turns = (fdev_q16 * mod_q16) / sr in 2^-32 turns */
        integral += (uint32_t) (uint64_t) fx_sdiv64(freq_dev * mod, (int64_t) sample_rate);
        out[i] = fx_sin_turn(carrier + integral);
        carrier += carrier_step;
    }
}

void agp_am_modulate(const agp_sample_t *carrier, const agp_sample_t *modulator, uint32_t len,
                     agp_q16_t depth, agp_sample_t *out)
{
    for (uint32_t i = 0; i < len; i++) {
        int64_t mod = modulator[i];
        if (mod > Q16_ONE) mod = Q16_ONE;
        if (mod < -Q16_ONE) mod = -Q16_ONE;
        int64_t gain = Q16_ONE + fx_mul_q16(depth, mod);
        out[i] = agp_sat(fx_mul_q16(carrier[i], gain));
    }
    /* Prevent clipping: scale the peak down to 0.95 */
    const int64_t limit = Q16_CONST(19, 20);
    int64_t max_val = 0;
    for (uint32_t i = 0; i < len; i++) {
        int64_t v = agp_abs64(out[i]);
        if (v > max_val) max_val = v;
    }
    if (max_val > limit) {
        for (uint32_t i = 0; i < len; i++) out[i] = agp_scale(out[i], limit, max_val);
    }
}

agp_q16_t agp_db_to_amplitude(int32_t db)
{
    /* 10^(db/20) = 2^(db * log2(10) / 20); log2(10) = 217706 / 65536 */
    int64_t y = fx_sdiv64((int64_t) db * 217706, 20);
    uint64_t a = fx_exp2_q16(y);
    return a > (uint64_t) INT32_MAX ? INT32_MAX : (agp_q16_t) a;
}

void agp_subaudible_embed(agp_sample_t *signal, uint32_t len, int32_t target_db)
{
    agp_q16_t rms = agp_rms(signal, len);
    if (rms <= 0) return;

    agp_q16_t target_amp = agp_db_to_amplitude(target_db);
    for (uint32_t i = 0; i < len; i++) signal[i] = agp_scale(signal[i], target_amp, rms);
}

void agp_nested_modulation(agp_audio_buffer_t *layers, uint32_t num_layers,
                            const agp_modulation_t *mod,
                            agp_audio_buffer_t *out) {
    if (num_layers == 0 || !layers || !out) {
        if (out) out->length = 0;
        return;
    }
    if (num_layers > AGP_MAX_LAYERS) num_layers = AGP_MAX_LAYERS;

    /* Find shortest layer length */
    uint32_t min_len = layers[0].length;
    for (uint32_t i = 1; i < num_layers; i++)
        if (layers[i].length < min_len) min_len = layers[i].length;

    /* FM modulate each layer with different Solfeggio carriers */
    static const agp_hz_t carriers[] = {AGP_HZ(528), AGP_HZ(639), AGP_HZ(741), AGP_HZ(852),
                                        AGP_HZ(396), AGP_HZ(417), AGP_HZ(639), AGP_HZ(741)};
    agp_sample_t fm_buf[AGP_MAX_AUDIO];

    /* Start with first FM-modulated layer */
    agp_fm_modulate(layers[0].samples, min_len,
                    carriers[0 % 8], mod->fm_modulation_index,
                    layers[0].sample_rate, out->samples);
    out->length = min_len;
    out->sample_rate = layers[0].sample_rate;

    /* Nest AM modulation for remaining layers */
    agp_sample_t temp[AGP_MAX_AUDIO];
    for (uint32_t i = 1; i < num_layers; i++) {
        agp_fm_modulate(layers[i].samples, min_len,
                        carriers[i % 8], mod->fm_modulation_index,
                        layers[i].sample_rate, fm_buf);

        agp_q16_t depth = mod->am_modulation_depth;
        for (uint32_t j = 1; j < i; j++) depth /= 2; /* Decreasing depth */

        agp_am_modulate(out->samples, fm_buf, min_len, depth, temp);
        for (uint32_t j = 0; j < min_len; j++)
            out->samples[j] = temp[j];
    }

    if (mod->subaudible) {
        agp_subaudible_embed(out->samples, out->length, mod->target_db);
    }
}

/* ===== Electromagnetic Genomics ===== */

/* One EM sample: A*sin(p1) + 0.15*A*sin(p2), times (1 + 0.05*sin(p3)). */
static agp_sample_t agp_em_sample(agp_q16_t amplitude, uint32_t p1, uint32_t p2, uint32_t p3)
{
    int64_t wave = fx_mul_q16(amplitude, fx_sin_turn(p1));
    wave += fx_mul_q16(fx_mul_q16(Q16_CONST(3, 20), amplitude), fx_sin_turn(p2));
    int64_t imp = Q16_ONE + fx_mul_q16(Q16_CONST(1, 20), fx_sin_turn(p3));
    return agp_sat(fx_mul_q16(wave, imp));
}

/* Writes n EM samples of freq into out (phase offset from em). */
static void agp_em_fill(agp_hz_t freq, uint32_t n, uint32_t sample_rate, bool is_electric,
                        const agp_em_params_t *em, agp_sample_t *out)
{
    agp_q16_t amplitude = is_electric ? em->electric_amplitude : em->magnetic_amplitude;
    uint32_t offset = is_electric ? em->electric_phase : em->magnetic_phase;
    uint32_t s1 = agp_turn_step(freq, sample_rate);
    uint32_t s2 = agp_turn_step(freq * 2, sample_rate);            /* 2nd harmonic */
    uint32_t s3 = agp_turn_step(fx_sdiv64(freq, 10), sample_rate); /* impedance mod */
    uint32_t p1 = offset, p2 = offset, p3 = 0;
    for (uint32_t i = 0; i < n; i++) {
        out[i] = agp_em_sample(amplitude, p1, p2, p3);
        p1 += s1;
        p2 += s2;
        p3 += s3;
    }
}

void agp_em_generate_tone(agp_hz_t freq, uint32_t duration_ms, uint32_t sample_rate,
                          bool is_electric, const agp_em_params_t *em, agp_sample_t *out,
                          uint32_t *out_len, uint32_t max_samples)
{
    uint32_t n = agp_samples_for(sample_rate, duration_ms);
    if (n > max_samples) n = max_samples;
    if (n < 2) { *out_len = 0; return; }

    agp_em_fill(freq, n, sample_rate, is_electric, em, out);

    agp_apply_adsr(out, n, sample_rate, 5u, 10u);
    *out_len = n;
}

void agp_em_generate_sequence(const char *dna, uint32_t len,
                               uint32_t sample_rate,
                               const agp_em_params_t *em,
                               agp_audio_buffer_t *buf) {
    if (len == 0 || !dna || !buf) {
        if (buf) buf->length = 0;
        return;
    }

    /* 4.5 s in total (inside the [3, 6] s window), shared between the bases */
    uint32_t per_base = (uint32_t) fx_udiv64((uint64_t) sample_rate * 9u, (uint64_t) len * 2u, 0);

    uint32_t total = 0;
    for (uint32_t i = 0; i < len; i++) {
        char c = dna[i];
        if (c >= 'a' && c <= 'z') c -= 32;

        agp_hz_t freq;
        bool is_electric;
        switch (c) {
            case 'A': freq = AGP_EM_FREQ_A; is_electric = true; break;
            case 'T': case 'U': freq = AGP_EM_FREQ_T; is_electric = false; break;
            case 'G': freq = AGP_EM_FREQ_G; is_electric = true; break;
            case 'C': freq = AGP_EM_FREQ_C; is_electric = false; break;
            default:  freq = AGP_EM_FREQ_N; is_electric = true; break;
        }

        uint32_t n = per_base;
        if (total + n > AGP_MAX_AUDIO) n = AGP_MAX_AUDIO - total;
        if (n == 0) break;

        agp_em_fill(freq, n, sample_rate, is_electric, em, buf->samples + total);
        total += n;
    }

    buf->length = total;
    buf->sample_rate = sample_rate;

    if (total > 0) {
        agp_normalize(buf->samples, total, Q16_CONST(19, 20));
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

agp_hz_t agp_chemistry_element_frequency(uint8_t atomic_number)
{
    /* Formula: [(N / Phi) * 1.125]^2 = E (Hz)
     * N = atomic_number (proton count)
     * Phi = golden ratio
     * 1.125 = symmetry factor (360° + 45° symmetry breaking)
     * Integer form: E = N^2 * (1.125/Phi)^2, the constant held in Q0.32,
     * rounded to Q16.16 Hz.
     */
    if (atomic_number == 0) return 0;
    uint64_t n2 = (uint64_t) atomic_number * atomic_number;
    return (agp_hz_t) ((n2 * AGP_ELEM_K_Q32 + ((uint64_t) 1 << 15)) >> 16);
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

agp_hz_t agp_chemistry_compound_frequency(const agp_compound_component_t *components,
                                          uint8_t num_components, uint16_t total_atoms)
{
    /* Formula: {[E1^(P1/W)] * [E2^(P2/W)] * ...}^2 = C (Hz)
     * E = elemental frequency, P = atom count, W = total atoms.
     * Integer form, in the log2 domain: log2 C = 2 * sum(P_i * log2 E_i) / W.
     */
    if (num_components == 0 || total_atoms == 0) return 0;

    int64_t acc = 0; /* sum of P_i * log2(E_i), Q16.16 */
    for (uint8_t i = 0; i < num_components; i++) {
        agp_hz_t e = components[i].element_freq;
        if (e > 0) acc += (int64_t) components[i].atom_count * fx_log2_q16((uint64_t) e);
    }
    uint64_t c = fx_exp2_q16(fx_sdiv64(2 * acc, (int64_t) total_atoms));
    return c > (uint64_t) INT64_MAX ? INT64_MAX : (agp_hz_t) c;
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

/* out[i] += h[i] * w (Q16.16), for i < n, saturating. */
static void agp_mix(agp_sample_t *out, const agp_sample_t *h, uint32_t n, agp_q16_t w)
{
    for (uint32_t i = 0; i < n; i++) out[i] = agp_sat((int64_t) out[i] + fx_mul_q16(h[i], w));
}

void agp_chemistry_generate_tone(agp_hz_t freq, uint32_t duration_ms, uint32_t sample_rate,
                                 agp_waveform_t wave, agp_sample_t *out, uint32_t *out_len,
                                 uint32_t max)
{
    /* Reuse standard tone generation with harmonic overtones */
    agp_generate_tone(freq, duration_ms, sample_rate, wave, out, out_len, max);

    /* Add second harmonic for richness (chemical resonance) */
    if (*out_len > 0 && *out_len < max) {
        uint32_t base_len = *out_len;
        agp_sample_t harm2[AGP_MAX_AUDIO];
        uint32_t h2_len;
        agp_generate_tone(freq * 2, duration_ms, sample_rate, wave, harm2, &h2_len, AGP_MAX_AUDIO);
        uint32_t blend = (base_len < h2_len) ? base_len : h2_len;
        agp_mix(out, harm2, blend, Q16_CONST(3, 10));
        /* Add third harmonic (Phi-weighted) */
        agp_generate_tone(freq * 3, duration_ms, sample_rate, wave, harm2, &h2_len, AGP_MAX_AUDIO);
        blend = (base_len < h2_len) ? base_len : h2_len;
        agp_mix(out, harm2, blend, Q16_CONST(3, 20));
    }
}

void agp_chemistry_compound_to_audio(const agp_compound_t *compound, uint32_t duration_ms,
                                     uint32_t sample_rate, agp_waveform_t wave,
                                     agp_audio_buffer_t *buf)
{
    if (!compound || !buf || compound->frequency <= 0 || compound->total_atoms == 0) {
        if (buf) buf->length = 0;
        return;
    }

    /* Generate the compound's fundamental frequency */
    uint32_t len;
    agp_chemistry_generate_tone(compound->frequency, duration_ms, sample_rate, wave, buf->samples,
                                &len, AGP_MAX_AUDIO);
    buf->length = len;
    buf->sample_rate = sample_rate;

    /* Layer each element's frequency as harmonics, weighted by atom proportion */
    agp_sample_t layer[AGP_MAX_AUDIO];
    for (uint8_t i = 0; i < compound->num_components; i++) {
        agp_hz_t e_freq = compound->components[i].element_freq;

        uint32_t l_len;
        agp_chemistry_generate_tone(e_freq, duration_ms, sample_rate, wave, layer, &l_len,
                                    AGP_MAX_AUDIO);

        uint32_t blend = (len < l_len) ? len : l_len;
        /* weight = atom_count / total_atoms * 0.3 */
        agp_q16_t w = (agp_q16_t) ((uint32_t) compound->components[i].atom_count *
                                   (uint32_t) Q16_CONST(3, 10) / compound->total_atoms);
        agp_mix(buf->samples, layer, blend, w);
    }

    /* Normalize final output */
    if (buf->length > 0) agp_normalize(buf->samples, buf->length, Q16_CONST(19, 20));
}

/* Relationship frequency: converts any numeric relationship to Hz.
 * Uses the same Phi-spiral principle: the relationship between two values
 * is itself a frequency. This generalizes the chemistry formula to any
 * paired relationship (social, economic, physical, abstract).
 *
 * freq = {[(A / Phi) * 1.125]^(Wa/W) * [(B / Phi) * 1.125]^(Wb/W)}^2
 * where W = Wa + Wb (total weight)
 *
 * Integer form: with e_x = [(x / Phi) * 1.125]^2,
 *   log2 freq = 2 * (Wa * log2 e_a + Wb * log2 e_b) / W
 *             = 4 * (Wa * log2(a * 1.125/Phi) + Wb * log2(b * 1.125/Phi)) / W
 */
static int64_t agp_weighted_log2(int64_t value_q16, uint32_t weight, uint64_t total)
{
    uint64_t v = fx_umuldiv64((uint64_t) value_q16, AGP_SYM_PHI_Q32, (uint64_t) 1 << 32);
    if (v == 0) v = 1; /* below Q16.16 resolution: smallest representable value */
    int64_t l = fx_log2_q16(v);
    uint64_t m = fx_umuldiv64((uint64_t) agp_abs64(l), weight, total);
    return l < 0 ? -(int64_t) m : (int64_t) m;
}

agp_hz_t agp_relationship_frequency(int64_t value_a_q16, int64_t value_b_q16, uint32_t weight_a,
                                    uint32_t weight_b)
{
    if (value_a_q16 <= 0 || value_b_q16 <= 0) return 0;
    uint64_t w = (uint64_t) weight_a + weight_b;
    if (w == 0) return 0;

    int64_t y = 4 * (agp_weighted_log2(value_a_q16, weight_a, w) +
                     agp_weighted_log2(value_b_q16, weight_b, w));
    uint64_t f = fx_exp2_q16(y);
    return f > (uint64_t) INT64_MAX ? INT64_MAX : (agp_hz_t) f;
}

void agp_relationship_to_audio(agp_hz_t freq, uint32_t duration_ms, uint32_t sample_rate,
                               agp_waveform_t wave, agp_audio_buffer_t *buf)
{
    if (!buf || freq <= 0) {
        if (buf) buf->length = 0;
        return;
    }
    uint32_t len;
    agp_chemistry_generate_tone(freq, duration_ms, sample_rate, wave, buf->samples, &len,
                                AGP_MAX_AUDIO);
    buf->length = len;
    buf->sample_rate = sample_rate;
}

/* ===== Utility ===== */

void agp_normalize(agp_sample_t *signal, uint32_t len, agp_q16_t target_peak)
{
    int64_t max_val = 0;
    for (uint32_t i = 0; i < len; i++) {
        int64_t v = agp_abs64(signal[i]);
        if (v > max_val) max_val = v;
    }
    if (max_val > 0) {
        for (uint32_t i = 0; i < len; i++) signal[i] = agp_scale(signal[i], target_peak, max_val);
    }
}

agp_q16_t agp_rms(const agp_sample_t *signal, uint32_t len)
{
    if (len == 0) return 0;
    /* Pre-shift so that len squared samples cannot overflow the sum. */
    uint64_t peak = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint64_t v = (uint64_t) agp_abs64(signal[i]);
        if (v > peak) peak = v;
    }
    unsigned sh = 0;
    while ((peak >> sh) != 0 && ((peak >> sh) * (peak >> sh)) > (UINT64_MAX >> 1) / len) sh++;
    uint64_t sum_sq = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint64_t v = (uint64_t) agp_abs64(signal[i]) >> sh;
        sum_sq += v * v;
    }
    uint64_t r = (uint64_t) fx_isqrt64(fx_udiv64(sum_sq, len, 0)) << sh;
    return r > (uint64_t) INT32_MAX ? INT32_MAX : (agp_q16_t) r;
}

void agp_apply_adsr(agp_sample_t *signal, uint32_t len, uint32_t sample_rate, uint32_t attack_ms,
                    uint32_t release_ms)
{
    uint32_t attack_n = agp_samples_for(sample_rate, attack_ms);
    uint32_t release_n = agp_samples_for(sample_rate, release_ms);
    if (attack_n > len / 8) attack_n = len / 8;
    if (release_n > len / 8) release_n = len / 8;

    for (uint32_t i = 0; i < attack_n && i < len; i++)
        signal[i] = agp_scale(signal[i], i, attack_n);

    for (uint32_t i = 0; i < release_n && (len - 1 - i) > attack_n; i++) {
        uint32_t idx = len - 1 - i;
        signal[idx] = agp_scale(signal[idx], i, release_n);
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
    agp_sample_t fm_buf[AGP_MAX_AUDIO];
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
        agp_normalize(output->samples, output->length, Q16_CONST(19, 20));
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
    printf("  FM carrier:     %lld Hz\n", (long long) (cfg->modulation.fm_carrier_freq >> 16));
    printf("  FM index:       %d/65536\n", (int) cfg->modulation.fm_modulation_index);
    printf("  AM depth:       %d/65536\n", (int) cfg->modulation.am_modulation_depth);
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
