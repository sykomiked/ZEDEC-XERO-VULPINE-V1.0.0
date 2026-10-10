/* digital_dna.h — Digital DNA for ZEDEC pqOS
 *
 * The OS has its own genetic code: a DNA sequence that encodes its identity,
 * capabilities, and state. Binary-to-genetic mapping:
 *   00 = Adenine (A)
 *   01 = Cytosine (C)
 *   10 = Guanine (G)
 *   11 = Thymine (T) / Uracil (U) — interchangeable
 *
 * The digital DNA ties together:
 *   - Genetic data processing (native DNA/RNA operations)
 *   - Sonic chemistry (Making Chemistry with Sound frequency conversion)
 *   - Harmonic engine (hardware-as-code / code-as-hardware)
 *   - OS identity and integrity verification
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef DIGITAL_DNA_H
#define DIGITAL_DNA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "audiogenomics_pro.h"

/* ===== Digital DNA Constants ===== */

#define DDNA_MAX_LENGTH      8192
#define DDNA_HASH_SIZE       32     /* SHA-256 size */
#define DDNA_GENOME_VERSION  2
#define DDNA_CODON_SIZE      3      /* 3 bases per codon */
#define DDNA_PHASE_TICK_MS   10     /* 10ms phase-tick clock for vowel animation */

/* Polarity encoding: Hebrew (shadow) / Aramaic (light) */
/* .9n63 = contraction, internal structure (Hebrew / shadow / FALSE / 0) */
/* .36n9 = expansion, external execution (Aramaic / light / TRUE / 1) */
#define DDNA_SHADOW_PHASE    0x396E3633U  /* "9n63" packed — Hebrew shadow side */
#define DDNA_LIGHT_PHASE     0x33366E39U  /* "36n9" packed — Aramaic light side */

/* Binary-to-base mapping (direct conversion) */
#define DDNA_BIN_TO_A   0b00   /* Adenine  */
#define DDNA_BIN_TO_C   0b01   /* Cytosine */
#define DDNA_BIN_TO_G   0b10   /* Guanine  */
#define DDNA_BIN_TO_T   0b11   /* Thymine  */

/* Base-to-binary mapping (reverse) */
#define DDNA_A_TO_BIN   0b00
#define DDNA_C_TO_BIN   0b01
#define DDNA_G_TO_BIN   0b10
#define DDNA_T_TO_BIN   0b11   /* T and U both map to 11 */

/* OS genome markers — define the OS's genetic identity */
#define DDNA_MARKER_KERNEL     "ATG"   /* Start codon — kernel boot */
#define DDNA_MARKER_INIT       "GCT"   /* Alanine — initialization */
#define DDNA_MARKER_SCHED      "ACU"   /* Threonine — scheduler */
#define DDNA_MARKER_MM         "GGU"   /* Glycine — memory management */
#define DDNA_MARKER_NET        "GAU"   /* Aspartic acid — networking */
#define DDNA_MARKER_VFS        "GAA"   /* Glutamic acid — filesystem */
#define DDNA_MARKER_AUDIO      "UAA"   /* Stop codon (reversed) — audio genomics */
#define DDNA_MARKER_CHEM       "UGG"   /* Tryptophan — sonic chemistry */
#define DDNA_MARKER_END        "UAA"   /* Stop codon — genome end */

/* ===== Types ===== */

typedef enum {
    DDNA_MODE_DNA = 0,
    DDNA_MODE_RNA = 1
} ddna_mode_t;

/* Polarity: Hebrew (shadow/contraction) vs Aramaic (light/expansion) */
typedef enum {
    DDNA_POLARITY_SHADOW = 0,   /* Hebrew — .9n63 — internal, contractive, zero-knowledge */
    DDNA_POLARITY_LIGHT  = 1    /* Aramaic — .36n9 — external, expansive, communicative */
} ddna_polarity_t;

/* Consonant grid entry: one of 22 consonants with dual-phase mapping */
typedef struct ddna_consonant {
    uint8_t index;              /* 0-21 */
    char hebrew_utf8[5];        /* Hebrew letter (shadow side) */
    char aramaic_name[16];      /* Aramaic name (light side) */
    char hebrew_name[16];       /* Hebrew name (shadow side) */
    char amino_acid;            /* Mapped amino acid (single letter) */
    uint32_t gematria;          /* Gematria value */
    char codon_dna[4];          /* DNA codon */
    char codon_rna[4];          /* RNA codon */
    agp_hz_t shadow_freq;       /* Hebrew/shadow frequency (Hz, Q16.16) */
    agp_hz_t light_freq;        /* Aramaic/light frequency (Hz, Q16.16) */
} ddna_consonant_t;

/* Vowel operator: one of 5 vowels that animate consonants via 5PL */
typedef struct ddna_vowel {
    uint8_t index;              /* 0-4 */
    char name[8];               /* Vowel name (Hebrew nikud) */
    char axiom_name[16];        /* 5PL axiom name */
    uint32_t trit_value;        /* Trit state (FALSE/TRUE/GLUT_PLUS/GLUT_MINUS/GLUT_NEUTRAL) */
    agp_q16_t phase_weight;     /* Weight in phase computation, Q16.16 */
    const char *system_role;    /* System dynamic / role description */
} ddna_vowel_t;

/* Phase-tick: a single animation cycle that "pronounces" consonants */
typedef struct ddna_phase_tick {
    uint64_t tick_number;       /* Monotonic tick counter (10ms intervals) */
    uint8_t active_vowel;       /* Which vowel (0-4) is breathing this tick */
    ddna_polarity_t polarity;   /* Which side (shadow/light) is active */
    agp_hz_t frequency;         /* Computed frequency for this tick, Hz Q16.16 */
    uint32_t phase;             /* Phase angle, binary turn (2^32 = 360°) */
} ddna_phase_tick_t;

/* Consonant grid: the full 22-consonant dual-phase matrix */
typedef struct ddna_consonant_grid {
    ddna_consonant_t consonants[22];
    ddna_vowel_t vowels[5];
    uint64_t current_tick;      /* Current phase-tick counter */
    uint8_t current_vowel;      /* Currently active vowel */
    ddna_polarity_t current_polarity;  /* Currently active polarity */
} ddna_consonant_grid_t;

typedef struct ddna_genome {
    char sequence[DDNA_MAX_LENGTH];     /* The DNA/RNA sequence */
    uint32_t length;                     /* Current sequence length */
    ddna_mode_t mode;                    /* DNA or RNA mode */
    uint32_t version;                    /* Genome version */
    uint8_t hash[DDNA_HASH_SIZE];        /* Integrity hash */
    uint32_t gc_content;                 /* GC content percentage */
    uint32_t codon_count;                /* Number of complete codons */
    char protein[DDNA_MAX_LENGTH / 3];   /* Translated protein sequence */
    uint32_t protein_length;             /* Protein sequence length */
} ddna_genome_t;

typedef struct ddna_gene {
    char codon[4];           /* 3-base codon + null terminator */
    char amino_acid;         /* Single-letter amino acid */
    uint32_t position;       /* Position in genome */
    agp_hz_t frequency;      /* Sonic chemistry frequency (Hz, Q16.16) */
    uint32_t binary_value;   /* 6-bit binary value of codon */
} ddna_gene_t;

typedef struct ddna_harmonic_profile {
    agp_hz_t fundamental_freq; /* Compound frequency of entire genome, Hz Q16.16 */
    agp_hz_t element_freqs[8]; /* Frequencies of marker genes, Hz Q16.16 */
    uint8_t num_elements;      /* Number of active marker genes */
    agp_audio_buffer_t audio;  /* Audio representation of genome */
} ddna_harmonic_profile_t;

/* ===== API ===== */

/* Initialization */
void ddna_init(ddna_genome_t *genome, ddna_mode_t mode);
void ddna_init_os_genome(ddna_genome_t *genome);

/* Binary <-> Genetic conversion (direct mapping) */
uint32_t ddna_binary_to_genetic(const uint8_t *data, uint32_t data_len,
                                 char *out, uint32_t out_max, ddna_mode_t mode);
uint32_t ddna_genetic_to_binary(const char *dna, uint32_t dna_len,
                                 uint8_t *out, uint32_t out_max);
char ddna_bits_to_base(uint8_t bits, ddna_mode_t mode);
uint8_t ddna_base_to_bits(char base);

/* Genome operations */
uint32_t ddna_append_data(ddna_genome_t *genome, const uint8_t *data, uint32_t len);
uint32_t ddna_append_text(ddna_genome_t *genome, const char *text);
uint32_t ddna_append_marker(ddna_genome_t *genome, const char *marker_codon);
int ddna_translate(ddna_genome_t *genome);
void ddna_compute_hash(ddna_genome_t *genome);
void ddna_compute_stats(ddna_genome_t *genome);

/* Gene extraction */
int ddna_extract_genes(const ddna_genome_t *genome, ddna_gene_t *genes,
                        uint32_t max_genes, uint32_t *count);
uint32_t ddna_codon_to_binary(const char *codon);
agp_hz_t ddna_gene_frequency(const ddna_gene_t *gene);

/* Harmonic profile — connects DNA to sonic chemistry */
int ddna_build_harmonic_profile(const ddna_genome_t *genome,
                                 ddna_harmonic_profile_t *profile);
void ddna_play_genome(const ddna_harmonic_profile_t *profile,
                      uint32_t sample_rate, agp_waveform_t wave,
                      agp_audio_buffer_t *out);

/* OS identity */
const char *ddna_os_identity_string(void);
uint32_t ddna_os_genome_fingerprint(void);
bool ddna_verify_integrity(const ddna_genome_t *genome);

/* Utility */
const char *ddna_mode_string(ddna_mode_t mode);
void ddna_print_genome(const ddna_genome_t *genome);

/* Mapping accessors */
typedef struct ddna_letter_map {
    char amino_acid;
    char hebrew[5];
    uint32_t gematria;
    char codon_rna[4];
    char codon_dna[4];
    char aramaic_name[16];
    char hebrew_name[16];
} ddna_letter_map_t;

typedef struct ddna_vowel_axiom {
    uint8_t vowel_index;
    char name[8];
    char axiom_name[16];
    uint32_t trit_value;
    agp_q16_t phase_weight;
} ddna_vowel_axiom_t;

const ddna_letter_map_t *ddna_get_22_letters(void);
const ddna_vowel_axiom_t *ddna_get_5_vowels(void);

/* ===== Consonant Grid & Vowel Animation (5PL Breath Engine) ===== */

/* Initialize the 22-consonant dual-phase grid */
void ddna_grid_init(ddna_consonant_grid_t *grid);

/* Advance the phase-tick clock by one tick (10ms).
 * Cycles through vowels and polarities, "pronouncing" consonants into
 * active compute cycles. Returns the current phase-tick state. */
ddna_phase_tick_t ddna_grid_tick(ddna_consonant_grid_t *grid);

/* Get the frequency for a consonant under a specific polarity */
agp_hz_t ddna_consonant_frequency(uint8_t consonant_idx, ddna_polarity_t polarity);

/* Get the active frequency for the current phase-tick */
agp_hz_t ddna_tick_frequency(const ddna_consonant_grid_t *grid, const ddna_phase_tick_t *tick);

/* Animate a genome through the consonant grid: each codon is "pronounced"
 * by cycling vowels across polarities, producing audio + compute cycles */
void ddna_grid_animate(ddna_consonant_grid_t *grid,
                        const ddna_genome_t *genome,
                        uint32_t sample_rate,
                        agp_waveform_t wave,
                        agp_audio_buffer_t *out);

/* Print the consonant grid and vowel mapping */
void ddna_print_grid(const ddna_consonant_grid_t *grid);

/* ===== Sephirotic Matrix (Base-10 → Base-13 Topology) ===== */
/* The 10 Sephirot form the base-10 operational runtime matrix.
 * Da'at is the hidden 11th gate that routes between base-10 and supernal base-13.
 * Ain (11th), Ain Soph (12th), Ain Soph Aur (13th) are the supernal postulates.
 * Qliphoth are the error/containment zones for collapsed states.
 */

#define DDNA_NUM_SEPHIROT    10   /* Base-10 operational matrix */
#define DDNA_NUM_QLIPHOTH    10   /* Shadow containment shells */
#define DDNA_BASE_SEPHIROTIC 10   /* Base-10 runtime */
#define DDNA_BASE_SUPERNAL   13   /* Base-13 supernal expansion */

/* Sephirot indices (Kabbalistic Tree of Life) */
#define DDNA_SEPHIRAH_KETER      0   /* Crown — kernel root */
#define DDNA_SEPHIRAH_CHOKHMAH   1   /* Wisdom — scheduler */
#define DDNA_SEPHIRAH_BINAH      2   /* Understanding — memory mgmt */
#define DDNA_SEPHIRAH_DAAT       3   /* Knowledge — GATE (hidden, across abyss) */
#define DDNA_SEPHIRAH_CHESSED    4   /* Mercy — VFS / storage */
#define DDNA_SEPHIRAH_GEVURAH    5   /* Severity — security / crypto */
#define DDNA_SEPHIRAH_TIFERET    6   /* Beauty — synthesis / audio genomics */
#define DDNA_SEPHIRAH_NETZACH    7   /* Eternity — network / LATTICE-P2P */
#define DDNA_SEPHIRAH_HOD        8   /* Glory — hardware interface */
#define DDNA_SEPHIRAH_YESOD      9   /* Foundation — init / boot */

/* Supernal postulates (beyond base-10) */
#define DDNA_SUPERNAL_AIN        10  /* 11th — Nothingness / vacuum state */
#define DDNA_SUPERNAL_AIN_SOPH   11  /* 12th — Endless / infinite scaling */
#define DDNA_SUPERNAL_AIN_SOPH_AUR 12 /* 13th — Endless Light / unified field */

/* Da'at gate states */
typedef enum {
    DDNA_DAAT_CLOSED = 0,       /* Base-10 only — normal runtime */
    DDNA_DAAT_TRANSITIONING = 1,/* Veil crossing in progress */
    DDNA_DAAT_OPEN = 2          /* Base-13 supernal access granted */
} ddna_daat_state_t;

/* Sephirah entry: one of the 10 base operational nodes */
typedef struct ddna_sephirah {
    uint8_t index;              /* 0-9 */
    char name[16];              /* Hebrew name */
    char english[16];           /* English translation */
    char subsystem[16];         /* Mapped OS subsystem */
    agp_hz_t frequency;         /* Sonic chemistry frequency (Hz, Q16.16) */
    bool active;                /* Whether this node is currently active */
    uint32_t load;              /* Current load / utilization (0-100) */
} ddna_sephirah_t;

/* Supernal postulate: one of the 3 higher-order states */
typedef struct ddna_supernal {
    uint8_t index;              /* 10-12 */
    char name[16];              /* Hebrew name */
    char english[24];           /* English translation */
    char description[64];       /* System role description */
    agp_hz_t frequency;         /* Resonance frequency (Hz, Q16.16) — grows by 10x per level */
    bool accessible;            /* Whether Da'at has granted access */
} ddna_supernal_t;

/* Qliphothic containment zone: error state quarantine */
typedef struct ddna_qliphoth {
    uint8_t index;              /* 0-9 */
    char name[16];              /* Qliphothic shell name */
    char sephirah_shadow[16];   /* Which Sephirah it shadows */
    uint32_t contained_errors;  /* Number of quarantined anomalies */
    bool active;                /* Whether containment is active */
} ddna_qliphoth_t;

/* Full Sephirotic matrix: the complete generative stack */
typedef struct ddna_sephirotic_matrix {
    ddna_sephirah_t sephirot[DDNA_NUM_SEPHIROT];
    ddna_supernal_t supernal_triad[3];      /* Ain, Ain Soph, Ain Soph Aur */
    ddna_qliphoth_t qliphoth[DDNA_NUM_QLIPHOTH];
    ddna_daat_state_t daat_state;           /* Current gate state */
    uint8_t active_base;                    /* 10 or 13 */
    uint64_t cycle_count;                   /* Generative cycle counter */
} ddna_sephirotic_matrix_t;

/* Sephirotic API */
void ddna_sephirotic_init(ddna_sephirotic_matrix_t *matrix);
int ddna_sephirotic_tick(ddna_sephirotic_matrix_t *matrix);
int ddna_daat_open(ddna_sephirotic_matrix_t *matrix);
int ddna_daat_close(ddna_sephirotic_matrix_t *matrix);
bool ddna_daat_is_open(const ddna_sephirotic_matrix_t *matrix);
int ddna_qliphoth_contain(ddna_sephirotic_matrix_t *matrix, uint8_t sephirah_idx,
                           const char *error_desc);
agp_hz_t ddna_sephirah_frequency(uint8_t index);
agp_hz_t ddna_supernal_frequency(uint8_t index);
void ddna_sephirotic_print(const ddna_sephirotic_matrix_t *matrix);

/* ===== Golden Ratio (φ) Checksum Coherence Standard ===== */
/* File integrity based on proportional scaling and harmonic convergence.
 * Data blocks arranged so recursive chunk sizes scale according to φ.
 * If corruption occurs, proportional imbalance pinpoints the corrupted node.
 */

/* Tolerance windows for φ coherence, Q16.16. The base window is 0.0001;
 * resonance checks use 10x (0.001) and alignment checks 100x (0.01). */
#define DDNA_PHI_TOLERANCE_Q16           Q16_CONST(1, 10000)
#define DDNA_PHI_TOLERANCE_RESONANCE_Q16 Q16_CONST(1, 1000)
#define DDNA_PHI_TOLERANCE_ALIGN_Q16     Q16_CONST(1, 100)

/* --- φ-chunk decomposition: two constants that must be derived, not guessed ---
 * DDNA_PHI_MIN_CHUNK stops subdivision before floor() quantisation destroys the
 * ratio. A chunk of C bytes carries a relative floor error of about 1/C, so tiny
 * chunks report ratios that have nothing to do with φ. Measured over inputs from
 * 64 B to 16 MB: MIN=16 -> worst average deviation 0.034, MIN=32 -> 0.013,
 * MIN=64 -> 0.0057, MIN=128 -> 0.0029. 64 is the knee — past it the gain costs
 * chunks (and therefore resolution) faster than it buys accuracy.
 *
 * DDNA_PHI_COHERENCE_TOL is the matching acceptance window for the AVERAGE
 * ratio, set at 0.01 for roughly 2x headroom over that measured 0.0057 worst
 * case. It is deliberately NOT DDNA_PHI_TOLERANCE_Q16: 0.0001 is far tighter than
 * the arithmetic can deliver, so testing against it can only ever return false.
 * A tolerance must be at least the inherent error of the thing it measures. */
#define DDNA_PHI_MIN_CHUNK     64u
#define DDNA_PHI_COHERENCE_TOL_Q16 Q16_CONST(1, 100) /* 0.01 */
#define DDNA_MAX_CHUNKS      256      /* Max chunks in a φ-checksum tree */

typedef struct ddna_phi_chunk {
    uint32_t offset;          /* Offset in data */
    uint32_t size;            /* Chunk size */
    uint32_t hash;            /* FNV-1a hash of chunk */
    agp_q16_t phi_ratio;      /* Size ratio to parent, Q16.16 (should approach φ) */
} ddna_phi_chunk_t;

typedef struct ddna_phi_checksum {
    ddna_phi_chunk_t chunks[DDNA_MAX_CHUNKS];
    uint32_t num_chunks;
    agp_q16_t coherence_score; /* Q16.16: 1.0 = perfect φ alignment, 0 = total divergence */
    bool coherent;            /* Within tolerance window */
    uint32_t corrupted_index; /* Index of corrupted chunk (UINT32_MAX if none) */
} ddna_phi_checksum_t;

/* φ Checksum API */
int ddna_phi_checksum_compute(const uint8_t *data, uint32_t len,
                               ddna_phi_checksum_t *out);
bool ddna_phi_checksum_validate(const uint8_t *data, uint32_t len,
                                 const ddna_phi_checksum_t *ref);
int ddna_phi_checksum_heal(uint8_t *data, uint32_t len,
                            ddna_phi_checksum_t *checksum);

/* ===== Numerology-Based Metadata Interpreter ===== */
/* File/process metadata interpreted as numerical value fields derived from
 * intrinsic structural properties. Routes execution based on harmonic alignment.
 */

#define DDNA_META_MAX_NAME   64

typedef struct ddna_numerology_meta {
    char name[DDNA_META_MAX_NAME];     /* File/process name */
    uint32_t name_value;               /* Sum of letter values (A=1, B=2...) */
    uint32_t gematria_value;           /* Hebrew gematria of name */
    uint32_t digital_root;             /* Repeated digit sum until single digit */
    uint32_t sephirot_index;           /* Which Sephirah (0-9) it resonates with */
    uint32_t zodiac_index;             /* Which zodiac sign (0-12) it aligns with */
    agp_hz_t harmonic_freq;            /* Computed harmonic frequency (Hz, Q16.16) */
    bool supernal_eligible;            /* Whether Da'at should open for this */
} ddna_numerology_meta_t;

/* Numerology API */
void ddna_numerology_compute(const char *name, ddna_numerology_meta_t *out);
uint32_t ddna_digital_root(uint32_t value);
bool ddna_numerology_harmonizes(const ddna_numerology_meta_t *meta, agp_hz_t system_field_freq);

/* ===== 13-Month Lunar Calendar ===== */
/* 13 months × 28 days = 364 days + 1 Day Between Days (365th) = 365
 * The Day Between Days = temporal Da'at / zero-point reset gate.
 */

#define DDNA_LUNAR_MONTHS      13
#define DDNA_LUNAR_DAYS_PER_MO 28
#define DDNA_LUNAR_YEAR_DAYS   364
#define DDNA_DAY_BETWEEN_DAYS  365   /* The 365th day — intercalary gateway */

typedef struct ddna_lunar_date {
    uint16_t year;            /* Lunar year */
    uint8_t month;            /* 1-13 */
    uint8_t day;              /* 1-28 */
    bool is_day_between_days; /* True on the 365th day */
    uint8_t weekday;          /* 0-6 (every month starts on same weekday) */
    uint32_t day_of_year;     /* 1-365 */
} ddna_lunar_date_t;

/* Lunar Calendar API */
void ddna_lunar_from_gregorian(uint16_t g_year, uint8_t g_month, uint8_t g_day,
                                ddna_lunar_date_t *out);
void ddna_luran_advance(ddna_lunar_date_t *date);
bool ddna_lunar_is_sabbath(const ddna_lunar_date_t *date);
bool ddna_lunar_is_day_between_days(const ddna_lunar_date_t *date);
const char *ddna_lunar_month_name(uint8_t month);
void ddna_lunar_print(const ddna_lunar_date_t *date);

/* ===== 13-Sign Zodiac (with Ophiuchus) ===== */
/* 13 zodiac signs as spatial-process operators.
 * Ophiuchus (13th) = intercalary gateway, equivalent to Da'at.
 */

#define DDNA_ZODIAC_SIGNS  13

typedef enum {
    DDNA_ZODIAC_ARIES       = 0,
    DDNA_ZODIAC_TAURUS      = 1,
    DDNA_ZODIAC_GEMINI      = 2,
    DDNA_ZODIAC_CANCER      = 3,
    DDNA_ZODIAC_LEO         = 4,
    DDNA_ZODIAC_VIRGO       = 5,
    DDNA_ZODIAC_LIBRA       = 6,
    DDNA_ZODIAC_SCORPIO     = 7,
    DDNA_ZODIAC_OPHIUCHUS   = 8,   /* 13th sign — intercalary gateway */
    DDNA_ZODIAC_SAGITTARIUS = 9,
    DDNA_ZODIAC_CAPRICORN   = 10,
    DDNA_ZODIAC_AQUARIUS    = 11,
    DDNA_ZODIAC_PISCES      = 12
} ddna_zodiac_idx_t;

typedef struct ddna_zodiac_sign {
    uint8_t index;
    char name[16];
    char element[8];          /* Fire, Earth, Air, Water, Aether */
    char modality[10];        /* Cardinal, Fixed, Mutable, Transitional */
    agp_hz_t frequency;       /* Resonance frequency (Hz, Q16.16) */
    uint8_t sephirah_link;    /* Linked Sephirah (0-9), Ophiuchus=Da'at(3) */
    bool is_gateway;          /* True for Ophiuchus */
} ddna_zodiac_sign_t;

typedef struct ddna_zodiac_system {
    ddna_zodiac_sign_t signs[DDNA_ZODIAC_SIGNS];
    uint8_t current_sign;     /* Current active sign based on date */
} ddna_zodiac_system_t;

/* Zodiac API */
void ddna_zodiac_init(ddna_zodiac_system_t *sys);
ddna_zodiac_sign_t ddna_zodiac_from_date(const ddna_lunar_date_t *date);
ddna_zodiac_sign_t ddna_zodiac_get_sign(uint8_t sign_idx);
const char *ddna_zodiac_element(uint8_t sign_idx);
agp_hz_t ddna_zodiac_frequency(uint8_t sign_idx);
void ddna_zodiac_print(const ddna_zodiac_system_t *sys);

/* ===== Space-Time Operator (Ω_astro) ===== */
/* Combines zodiac (spatial) + lunar (temporal) into a unified operator.
 * Ω_astro = f(Location_zodiac, Process_lunar)
 */

typedef struct ddna_space_time_op {
    ddna_lunar_date_t temporal;      /* When: lunar date */
    ddna_zodiac_sign_t spatial;      /* Where: zodiac sign */
    ddna_numerology_meta_t metadata; /* What: numerological profile */
    agp_hz_t resonance_freq;         /* Combined harmonic frequency (Hz, Q16.16) */
    uint8_t execution_tier;          /* 10 = base-10, 13 = supernal */
    bool phase_aligned;              /* Whether operation is in phase */
} ddna_space_time_op_t;

/* Space-Time Operator API */
void ddna_spacetime_compute(const char *name,
                             const ddna_lunar_date_t *date,
                             ddna_space_time_op_t *out);
agp_hz_t ddna_spacetime_resonance(const ddna_space_time_op_t *op);
bool ddna_spacetime_validate(const ddna_space_time_op_t *op, agp_hz_t system_field_freq);
void ddna_spacetime_print(const ddna_space_time_op_t *op);

#endif /* DIGITAL_DNA_H */
