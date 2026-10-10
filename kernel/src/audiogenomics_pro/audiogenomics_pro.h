/* audiogenomics_pro.h — Audio Genomics Pro Engine
 * DNA/RNA-to-Audio synthesis, FM/AM modulation, electromagnetic genomics,
 * Hebrew/Gematria encoding, frequency mapping, and data-to-audio pipeline.
 *
 * Port of Audio Genomics Pro (Python) to freestanding C for ZEDEC pqOS.
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef AUDIOGENOMICS_PRO_H
#define AUDIOGENOMICS_PRO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "zxv_fixed.h"

/* INTEGER ONLY. Kernel images forbid floating point, so every quantity here
 * is fixed point:
 *   agp_hz_t      frequency in Hz, Q16.16 (AGP_HZ_ONE == 1 Hz)
 *   agp_sample_t  audio sample, Q16.16 (Q16_ONE == full scale 1.0)
 *   agp_q16_t     dimensionless ratio, gain, depth or index, Q16.16
 *   durations     whole milliseconds (uint32_t ..._ms)
 *   phases        binary turns (uint32_t, 2^32 == one full cycle)
 * Results match the former double code within fixed-point precision (about
 * 1.5e-5 relative for frequencies and ratios); sine is the Q16.16 table-free
 * series in zxv_fixed.h. */
typedef int64_t agp_hz_t;
typedef int32_t agp_sample_t;
typedef int32_t agp_q16_t;
#define AGP_HZ_ONE    ((agp_hz_t) 65536)
#define AGP_HZ(whole) ((agp_hz_t) (whole) * AGP_HZ_ONE)

/* ===== Constants ===== */

#define AGP_MAX_SEQUENCE    4096
#define AGP_MAX_AUDIO       65536
#define AGP_MAX_LAYERS      8
#define AGP_SAMPLE_RATE     44100
#define AGP_SAMPLE_RATE_HQ  192000

/* DNA base frequency mappings (scientific pitch), Hz in Q16.16 */
#define AGP_FREQ_A         ((agp_hz_t) 9622807)  /* D3 146.832384 Hz — Adenine */
#define AGP_FREQ_T         ((agp_hz_t) 11443511) /* F3 174.614116 Hz — Thymine */
#define AGP_FREQ_U         ((agp_hz_t) 11443511) /* F3 174.614116 Hz — Uracil (RNA) */
#define AGP_FREQ_C         ((agp_hz_t) 17145893) /* C4 261.625565 Hz — Cytosine */
#define AGP_FREQ_G         ((agp_hz_t) 25689813) /* G4 391.995436 Hz — Guanine */
#define AGP_FREQ_SEP       AGP_HZ(528)           /* Separator (Love frequency) */
#define AGP_FREQ_CODON_SEP AGP_HZ(639)           /* Codon separator */

/* Electromagnetic frequencies, Hz in Q16.16 */
#define AGP_EM_FREQ_A ((agp_hz_t) 35756442) /* 545.6 Hz Electric component */
#define AGP_EM_FREQ_T ((agp_hz_t) 35612262) /* 543.4 Hz Magnetic component */
#define AGP_EM_FREQ_G AGP_HZ(550)           /* 550.0 Hz Electric component */
#define AGP_EM_FREQ_C ((agp_hz_t) 35245261) /* 537.8 Hz Magnetic component */
#define AGP_EM_FREQ_N ((agp_hz_t) 35664691) /* 544.2 Hz Equilibrium */

/* Physical constants (reference values; nothing in this module computes
 * with them): c = 299792458 m/s exactly, mu0 = 1.2566370614e-6 H/m,
 * epsilon0 = 8.854187817e-12 F/m, Z0 = 376.730313668 ohm. */
#define AGP_C_LIGHT_M_PER_S 299792458u

/* Modulation defaults */
#define AGP_FM_CARRIER_DEFAULT   AGP_HZ(528)
#define AGP_FM_INDEX_DEFAULT     Q16_CONST(1, 10) /* 0.1 */
#define AGP_AM_DEPTH_DEFAULT     Q16_CONST(1, 20) /* 0.05 */
#define AGP_TARGET_DB_SUBAUDIBLE (-40)            /* whole dB */

/* Genetic code table size */
#define AGP_CODON_TABLE_SIZE 64

/* ===== Types ===== */

typedef enum {
    AGP_MODE_DNA = 0,
    AGP_MODE_RNA = 1
} agp_mode_t;

typedef enum {
    AGP_WAVE_SINE = 0,
    AGP_WAVE_SQUARE = 1,
    AGP_WAVE_SAWTOOTH = 2,
    AGP_WAVE_TRIANGLE = 3
} agp_waveform_t;

typedef struct agp_freq_map {
    agp_hz_t base_freq[5]; /* A, T/U, C, G, N */
    agp_hz_t separator_freq;
    agp_hz_t codon_sep_freq;
    bool retune_432;
} agp_freq_map_t;

typedef struct agp_modulation {
    agp_hz_t fm_carrier_freq;
    agp_q16_t fm_modulation_index;
    agp_q16_t am_modulation_depth;
    bool subaudible;
    int32_t target_db; /* whole dB relative to full scale */
} agp_modulation_t;

typedef struct agp_em_params {
    agp_q16_t electric_amplitude;
    agp_q16_t magnetic_amplitude;
    uint32_t electric_phase; /* binary turn */
    uint32_t magnetic_phase; /* binary turn */
} agp_em_params_t;

typedef struct agp_audio_buffer {
    agp_sample_t samples[AGP_MAX_AUDIO];
    uint32_t length;
    uint32_t sample_rate;
} agp_audio_buffer_t;

typedef struct agp_sequence {
    char bases[AGP_MAX_SEQUENCE + 1];  /* data + null terminator */
    uint32_t length;
    agp_mode_t mode;
} agp_sequence_t;

typedef struct agp_pipeline_config {
    uint32_t sample_rate;
    uint8_t bit_depth;
    uint32_t base_cycles;
    agp_waveform_t waveform;
    agp_modulation_t modulation;
    agp_freq_map_t freq_map;
    bool use_hebrew;
    bool normalize_output;
} agp_pipeline_config_t;

/* A codon is exactly three bases with NO NUL terminator by design — these
 * arrays are intentionally non-strings. `nonstring` documents that and
 * silences -Wunterminated-string-initialization without changing layout. */
#if defined(__has_attribute)
#  if __has_attribute(nonstring)
#    define ZXV_NONSTRING __attribute__((nonstring))
#  endif
#endif
#ifndef ZXV_NONSTRING
#  define ZXV_NONSTRING
#endif

typedef struct agp_genetic_code {
    char codon[3] ZXV_NONSTRING;
    char amino_acid;
} agp_genetic_code_t;

/* ===== API ===== */

/* Initialization */
void agp_init_freq_map(agp_freq_map_t *fm, bool retune_432);
void agp_init_modulation(agp_modulation_t *mod);
void agp_init_em_params(agp_em_params_t *em);
void agp_init_config(agp_pipeline_config_t *cfg);

/* DNA conversion */
uint32_t agp_binary_to_dna(const uint8_t *data, uint32_t data_len,
                            char *out, uint32_t out_max, agp_mode_t mode);
uint32_t agp_text_to_dna(const char *text, char *out, uint32_t out_max,
                          agp_mode_t mode);
uint32_t agp_text_to_dna_hebrew(const char *text, char *out, uint32_t out_max,
                                 agp_mode_t mode);
uint32_t agp_protein_to_dna(const char *protein, char *out, uint32_t out_max,
                             agp_mode_t mode);
uint32_t agp_dna_to_protein(const char *dna, char *out, uint32_t out_max);

/* Validation */
bool agp_validate_sequence(const char *seq, uint32_t len);
void agp_sequence_stats(const char *seq, uint32_t len,
                         uint32_t *gc_content, uint32_t *base_counts);

/* Frequency mapping */
agp_hz_t agp_get_base_frequency(const agp_freq_map_t *fm, char base, int harmonic);
void agp_get_sequence_frequencies(const agp_freq_map_t *fm, const char *seq, uint32_t len,
                                  agp_hz_t *freqs, uint32_t *count, uint32_t max);
agp_hz_t agp_get_codon_frequency(const agp_freq_map_t *fm, const char *codon);
agp_hz_t agp_calculate_beat_frequency(const agp_freq_map_t *fm, char b1, char b2);

/* Tone generation */
void agp_generate_tone(agp_hz_t freq, uint32_t duration_ms, uint32_t sample_rate,
                       agp_waveform_t wave, agp_sample_t *out, uint32_t *out_len,
                       uint32_t max_samples);
void agp_generate_sequence_audio(const agp_freq_map_t *fm,
                                  const char *seq, uint32_t len,
                                  uint32_t sample_rate, uint32_t base_cycles,
                                  agp_waveform_t wave,
                                  agp_audio_buffer_t *buf);

/* Modulation */
void agp_fm_modulate(const agp_sample_t *modulator, uint32_t len, agp_hz_t carrier_freq,
                     agp_q16_t mod_index, uint32_t sample_rate, agp_sample_t *out);
void agp_am_modulate(const agp_sample_t *carrier, const agp_sample_t *modulator, uint32_t len,
                     agp_q16_t depth, agp_sample_t *out);
void agp_subaudible_embed(agp_sample_t *signal, uint32_t len, int32_t target_db);
/* 10^(db/20) as Q16.16 (amplitude ratio for a whole-dB level). */
agp_q16_t agp_db_to_amplitude(int32_t db);
void agp_nested_modulation(agp_audio_buffer_t *layers, uint32_t num_layers,
                            const agp_modulation_t *mod,
                            agp_audio_buffer_t *out);

/* Electromagnetic genomics */
void agp_em_generate_tone(agp_hz_t freq, uint32_t duration_ms, uint32_t sample_rate,
                          bool is_electric, const agp_em_params_t *em, agp_sample_t *out,
                          uint32_t *out_len, uint32_t max_samples);
void agp_em_generate_sequence(const char *dna, uint32_t len,
                               uint32_t sample_rate,
                               const agp_em_params_t *em,
                               agp_audio_buffer_t *buf);

/* ===== Sonic Chemistry (Making Chemistry with Sound) ===== */
/* Formula by Michael Laurence Curzi (2022):
 *   Elemental:  [(N / Phi) * 1.125]^2 = E   (Hz)
 *   Compound:   {[E1^(P1/W)] * [E2^(P2/W)] * ...}^2 = C  (Hz)
 * Where N=proton count, E=elemental freq, P=atom count, W=total atoms.
 * Extends beyond chemistry: converts relationships (bonds, structures,
 * interactions) into precise harmonic frequencies.
 */

#define AGP_PHI_Q16      ((agp_q16_t) 106039) /* Golden ratio 1.6180340, Q16.16 */
#define AGP_PHI_INV_Q16  ((agp_q16_t) 40503)  /* 1/Phi 0.6180340, Q16.16 */
#define AGP_PHI_INV_Q32  2654435769u          /* 1/Phi, Q0.32 */
#define AGP_PHI_INV2_Q32 1640531527u          /* 1/Phi^2, Q0.32 */
#define AGP_SYMMETRY_Q16 ((agp_q16_t) 73728)  /* 1.125: 360° turn + 45° symmetry breaking */
/* (1.125 / Phi) in Q0.32 and its square: E(N) = N^2 * AGP_ELEM_K_Q32 / 2^32 Hz */
#define AGP_SYM_PHI_Q32  2986240241u
#define AGP_ELEM_K_Q32   2076297713u
#define AGP_MAX_ELEMENTS 119                     /* Periodic table + neutron */
#define AGP_MAX_COMPOUND 32                      /* Max elements in a compound */

typedef struct agp_element {
    uint8_t atomic_number;    /* N = proton count */
    char symbol[3];           /* e.g. "H", "He", "Au" */
    char name[16];            /* e.g. "Hydrogen" */
    agp_hz_t frequency;       /* Computed elemental frequency, Hz Q16.16 */
} agp_element_t;

typedef struct agp_compound_component {
    uint8_t atomic_number;    /* Which element */
    uint8_t atom_count;       /* P = number of this type of atom */
    agp_hz_t element_freq;    /* E for this element, Hz Q16.16 */
} agp_compound_component_t;

typedef struct agp_compound {
    agp_compound_component_t components[AGP_MAX_COMPOUND];
    uint8_t num_components;   /* Number of distinct element types */
    uint16_t total_atoms;     /* W = total atoms in molecule */
    agp_hz_t frequency;       /* Computed compound frequency, Hz Q16.16 */
} agp_compound_t;

/* Sonic Chemistry API */
agp_hz_t agp_chemistry_element_frequency(uint8_t atomic_number);
void agp_chemistry_init_table(agp_element_t *table, uint32_t *count);
agp_hz_t agp_chemistry_compound_frequency(const agp_compound_component_t *components,
                                          uint8_t num_components, uint16_t total_atoms);
int agp_chemistry_build_compound(const uint8_t *atomic_numbers,
                                  const uint8_t *atom_counts,
                                  uint8_t num_types,
                                  agp_compound_t *out);
void agp_chemistry_generate_tone(agp_hz_t freq, uint32_t duration_ms, uint32_t sample_rate,
                                 agp_waveform_t wave, agp_sample_t *out, uint32_t *out_len,
                                 uint32_t max);
void agp_chemistry_compound_to_audio(const agp_compound_t *compound, uint32_t duration_ms,
                                     uint32_t sample_rate, agp_waveform_t wave,
                                     agp_audio_buffer_t *buf);

/* Relationship frequency: converts any numeric relationship to Hz.
 * Values are Q16.16 (> 0); weights are non-negative integers (only their
 * ratio matters). */
agp_hz_t agp_relationship_frequency(int64_t value_a_q16, int64_t value_b_q16, uint32_t weight_a,
                                    uint32_t weight_b);
void agp_relationship_to_audio(agp_hz_t freq, uint32_t duration_ms, uint32_t sample_rate,
                               agp_waveform_t wave, agp_audio_buffer_t *buf);

/* Utility */
void agp_normalize(agp_sample_t *signal, uint32_t len, agp_q16_t target_peak);
agp_q16_t agp_rms(const agp_sample_t *signal, uint32_t len);
void agp_apply_adsr(agp_sample_t *signal, uint32_t len, uint32_t sample_rate, uint32_t attack_ms,
                    uint32_t release_ms);

/* Pipeline */
int agp_run_pipeline(const agp_pipeline_config_t *cfg,
                     const char *input_data, uint32_t input_len,
                     agp_audio_buffer_t *output);

/* Reporting */
void agp_print_info(const agp_pipeline_config_t *cfg);
void agp_print_sequence_stats(const char *seq, uint32_t len);

/* Genetic code table access */
const agp_genetic_code_t *agp_get_genetic_code(void);
char agp_translate_codon(const char *codon);

/* Gematria / Hebrew */
uint32_t agp_hebrew_gematria(const char *hebrew_text);
char agp_gematria_to_base(uint32_t value);

#endif /* AUDIOGENOMICS_PRO_H */
