/* audiogenomics_pro.h — Audio Genomics Pro Engine
 * DNA/RNA-to-Audio synthesis, FM/AM modulation, electromagnetic genomics,
 * Hebrew/Gematria encoding, frequency mapping, and data-to-audio pipeline.
 *
 * Port of Audio Genomics Pro (Python) to freestanding C for ZEDEC pqOS.
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef AUDIOGENOMICS_PRO_H
#define AUDIOGENOMICS_PRO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ===== Constants ===== */

#define AGP_MAX_SEQUENCE    4096
#define AGP_MAX_AUDIO       65536
#define AGP_MAX_LAYERS      8
#define AGP_SAMPLE_RATE     44100
#define AGP_SAMPLE_RATE_HQ  192000

/* DNA base frequency mappings (scientific pitch) */
#define AGP_FREQ_A   146.832383958704  /* D3 — Adenine */
#define AGP_FREQ_T   174.614115716502  /* F3 — Thymine */
#define AGP_FREQ_U   174.614115716502  /* F3 — Uracil (RNA) */
#define AGP_FREQ_C   261.625565300599  /* C4 — Cytosine */
#define AGP_FREQ_G   391.995435981749  /* G4 — Guanine */
#define AGP_FREQ_SEP 528.0             /* Separator (Love frequency) */
#define AGP_FREQ_CODON_SEP 639.0       /* Codon separator */

/* Electromagnetic frequencies */
#define AGP_EM_FREQ_A  545.6   /* Electric component */
#define AGP_EM_FREQ_T  543.4   /* Magnetic component */
#define AGP_EM_FREQ_G  550.0   /* Electric component */
#define AGP_EM_FREQ_C  537.8   /* Magnetic component */
#define AGP_EM_FREQ_N  544.2   /* Equilibrium */

/* Physical constants */
#define AGP_C_LIGHT    299792458.0
#define AGP_MU_0       1.2566370614e-6
#define AGP_EPSILON_0  8.854187817e-12
#define AGP_Z0         376.730313668

/* Modulation defaults */
#define AGP_FM_CARRIER_DEFAULT   528.0
#define AGP_FM_INDEX_DEFAULT     0.1
#define AGP_AM_DEPTH_DEFAULT     0.05
#define AGP_TARGET_DB_SUBAUDIBLE -40.0

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
    double base_freq[5];      /* A, T/U, C, G, N */
    double separator_freq;
    double codon_sep_freq;
    bool retune_432;
} agp_freq_map_t;

typedef struct agp_modulation {
    double fm_carrier_freq;
    double fm_modulation_index;
    double am_modulation_depth;
    bool subaudible;
    double target_db;
} agp_modulation_t;

typedef struct agp_em_params {
    double electric_amplitude;
    double magnetic_amplitude;
    double electric_phase;
    double magnetic_phase;
} agp_em_params_t;

typedef struct agp_audio_buffer {
    float samples[AGP_MAX_AUDIO];
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
double agp_get_base_frequency(const agp_freq_map_t *fm, char base, int harmonic);
void agp_get_sequence_frequencies(const agp_freq_map_t *fm,
                                   const char *seq, uint32_t len,
                                   double *freqs, uint32_t *count, uint32_t max);
double agp_get_codon_frequency(const agp_freq_map_t *fm, const char *codon);
double agp_calculate_beat_frequency(const agp_freq_map_t *fm, char b1, char b2);

/* Tone generation */
void agp_generate_tone(double freq, double duration, uint32_t sample_rate,
                       agp_waveform_t wave, float *out, uint32_t *out_len,
                       uint32_t max_samples);
void agp_generate_sequence_audio(const agp_freq_map_t *fm,
                                  const char *seq, uint32_t len,
                                  uint32_t sample_rate, uint32_t base_cycles,
                                  agp_waveform_t wave,
                                  agp_audio_buffer_t *buf);

/* Modulation */
void agp_fm_modulate(const float *modulator, uint32_t len,
                     double carrier_freq, double mod_index,
                     uint32_t sample_rate, float *out);
void agp_am_modulate(const float *carrier, const float *modulator,
                     uint32_t len, double depth, float *out);
void agp_subaudible_embed(float *signal, uint32_t len, double target_db);
void agp_nested_modulation(agp_audio_buffer_t *layers, uint32_t num_layers,
                            const agp_modulation_t *mod,
                            agp_audio_buffer_t *out);

/* Electromagnetic genomics */
void agp_em_generate_tone(double freq, double duration, uint32_t sample_rate,
                           bool is_electric, const agp_em_params_t *em,
                           float *out, uint32_t *out_len, uint32_t max_samples);
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

#define AGP_PHI          1.61803398874989484820  /* Golden ratio */
#define AGP_PHI_INV      0.61803398874989484820  /* 1/Phi */
#define AGP_SYMMETRY     1.125                   /* 360° turn + 45° symmetry breaking */
#define AGP_MAX_ELEMENTS 119                     /* Periodic table + neutron */
#define AGP_MAX_COMPOUND 32                      /* Max elements in a compound */

typedef struct agp_element {
    uint8_t atomic_number;    /* N = proton count */
    char symbol[3];           /* e.g. "H", "He", "Au" */
    char name[16];            /* e.g. "Hydrogen" */
    double frequency;         /* Computed elemental frequency in Hz */
} agp_element_t;

typedef struct agp_compound_component {
    uint8_t atomic_number;    /* Which element */
    uint8_t atom_count;       /* P = number of this type of atom */
    double element_freq;      /* E for this element */
} agp_compound_component_t;

typedef struct agp_compound {
    agp_compound_component_t components[AGP_MAX_COMPOUND];
    uint8_t num_components;   /* Number of distinct element types */
    uint16_t total_atoms;     /* W = total atoms in molecule */
    double frequency;         /* Computed compound frequency in Hz */
} agp_compound_t;

/* Sonic Chemistry API */
double agp_chemistry_element_frequency(uint8_t atomic_number);
void agp_chemistry_init_table(agp_element_t *table, uint32_t *count);
double agp_chemistry_compound_frequency(const agp_compound_component_t *components,
                                         uint8_t num_components, uint16_t total_atoms);
int agp_chemistry_build_compound(const uint8_t *atomic_numbers,
                                  const uint8_t *atom_counts,
                                  uint8_t num_types,
                                  agp_compound_t *out);
void agp_chemistry_generate_tone(double freq, double duration,
                                  uint32_t sample_rate, agp_waveform_t wave,
                                  float *out, uint32_t *out_len, uint32_t max);
void agp_chemistry_compound_to_audio(const agp_compound_t *compound,
                                      double duration, uint32_t sample_rate,
                                      agp_waveform_t wave,
                                      agp_audio_buffer_t *buf);

/* Relationship frequency: converts any numeric relationship to Hz */
double agp_relationship_frequency(double value_a, double value_b,
                                   double weight_a, double weight_b);
void agp_relationship_to_audio(double freq, double duration, uint32_t sample_rate,
                                agp_waveform_t wave, agp_audio_buffer_t *buf);

/* Utility */
void agp_normalize(float *signal, uint32_t len, double target_peak);
double agp_rms(const float *signal, uint32_t len);
void agp_apply_adsr(float *signal, uint32_t len, uint32_t sample_rate,
                    double attack_ms, double release_ms);

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
