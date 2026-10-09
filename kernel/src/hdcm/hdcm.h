/*
 * hdcm.h — Hyperdimensional Conversion Matrix
 *
 * Universal language interoperability engine. Maps any programming or
 * human language into a common hyperdimensional vector space, enabling
 * bidirectional translation between any pair of languages.
 *
 * Architecture:
 *   Source Language → HDCM Vector (1024-dim) → Target Language
 *
 * The HDCM uses second quantization (CREATE → ENTANGLE → MEASURE) to:
 *   CREATE:   Encode source language constructs into hyperdimensional vectors
 *   ENTANGLE: Bind vectors across language pairs via tensor product
 *   MEASURE:  Collapse entangled state into target language output
 *
 * Programming Languages (32+):
 *   C, C++, Rust, Go, Python, JavaScript, TypeScript, Java, Kotlin,
 *   Swift, Ruby, Lua, Haskell, OCaml, Erlang, Elixir, Clojure, Scala,
 *   Zig, Nim, Crystal, D, Julia, R, Perl, PHP, Shell, Assembly,
 *   SystemVerilog, VHDL, SQL, WASM
 *
 * Human Languages (32+):
 *   English, Spanish, French, German, Chinese, Japanese, Korean,
 *   Arabic, Hindi, Russian, Portuguese, Italian, Turkish, Dutch,
 *   Swedish, Polish, Hebrew, Persian, Thai, Vietnamese, Indonesian,
 *   Swahili, Navajo, Klingon, Morse, M5 Axiomatic, Latin, Greek,
 *   Sanskrit, Esperanto, Lojban, Ithkuil
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#ifndef ZEDEC_HDCM_H
#define ZEDEC_HDCM_H

#include <stdint.h>
#include <stdbool.h>

/* ===== Constants ===== */

#define HDCM_VECTOR_DIM       1024   /* Hyperdimensional vector dimensionality */
#define HDCM_MAX_LANGUAGES     128   /* Maximum registered languages */
#define HDCM_MAX_CONSTRUCTS    256   /* Max language constructs per language */
#define HDCM_MAX_LABEL         64
#define HDCM_MAX_TOKENS       1024
#define HDCM_MAX_OUTPUT      8192
#define HDCM_VECTOR_BYTES    (HDCM_VECTOR_DIM / 8)  /* 128 bytes per vector */
#define HDCM_MAX_MATRICES     128   /* Max conversion matrices */
#define HDCM_MAX_PAIRS       4096   /* Max language pairs */

/* ===== Language Categories ===== */

typedef enum {
    HDCM_LANG_PROGRAMMING = 0,
    HDCM_LANG_HUMAN       = 1,
    HDCM_LANG_AXIOMATIC   = 2,  /* M5 / mathematical */
    HDCM_LANG_MACHINE     = 3,  /* Assembly / bytecode */
} hdcm_lang_category_t;

/* ===== Construct Types ===== */

typedef enum {
    HDCM_CONSTRUCT_VARIABLE    = 0,
    HDCM_CONSTRUCT_FUNCTION    = 1,
    HDCM_CONSTRUCT_TYPE        = 2,
    HDCM_CONSTRUCT_CONTROL     = 3,  /* if/else/loop */
    HDCM_CONSTRUCT_OPERATOR    = 4,
    HDCM_CONSTRUCT_LITERAL     = 5,
    HDCM_CONSTRUCT_MODULE      = 6,
    HDCM_CONSTRUCT_COMMENT     = 7,
    HDCM_CONSTRUCT_IMPORT      = 8,
    HDCM_CONSTRUCT_CLASS       = 9,
    HDCM_CONSTRUCT_INTERFACE   = 10,
    HDCM_CONSTRUCT_NAMESPACE   = 11,
    HDCM_CONSTRUCT_MACRO       = 12,
    HDCM_CONSTRUCT_GENERIC     = 13,  /* Template/parametric */
    HDCM_CONSTRUCT_PATTERN     = 14,  /* Pattern matching */
    HDCM_CONSTRUCT_CONCURRENCY = 15,  /* Async/parallel */
    HDCM_CONSTRUCT_ERROR       = 16,  /* Exception handling */
    HDCM_CONSTRUCT_MEMORY      = 17,  /* Memory management */
    HDCM_CONSTRUCT_ANNOTATION  = 18,  /* Decorators/attributes */
    HDCM_CONSTRUCT_SENTENCE    = 19,  /* Human language: sentence */
    HDCM_CONSTRUCT_WORD        = 20,  /* Human language: word */
    HDCM_CONSTRUCT_PHONEME     = 21,  /* Human language: phoneme */
    HDCM_CONSTRUCT_GRAMMAR     = 22,  /* Human language: grammar rule */
    HDCM_CONSTRUCT_SEMANTIC    = 23,  /* Semantic unit */
    HDCM_CONSTRUCT_PRAGMA      = 24,
    HDCM_CONSTRUCT_ASSERTION   = 25,
    HDCM_CONSTRUCT_AXIOM       = 26,  /* M5 axiomatic */
    HDCM_CONSTRUCT_PHASE       = 27,  /* Phase logic construct */
} hdcm_construct_t;

/* ===== Second Quantization Phases ===== */

typedef enum {
    HDCM_PHASE_CREATE    = 0,  /* Encode source → hyperdim vector */
    HDCM_PHASE_ENTANGLE  = 1,  /* Bind vectors across language pairs */
    HDCM_PHASE_MEASURE   = 2,  /* Collapse → target language output */
} hdcm_phase_t;

/* ===== Hyperdimensional Vector ===== */

typedef struct {
    uint8_t bits[HDCM_VECTOR_BYTES];  /* 1024-bit binary vector */
    uint32_t hamming_weight;           /* Popcount for similarity */
} hdcm_vector_t;

/* ===== Language Construct Mapping ===== */

typedef struct {
    hdcm_construct_t type;
    char     source_token[HDCM_MAX_LABEL];   /* Token in source language */
    char     target_token[HDCM_MAX_LABEL];   /* Token in target language */
    hdcm_vector_t vector;                     /* Hyperdim representation */
    uint32_t semantic_hash;                   /* Semantic identity hash */
} hdcm_construct_map_t;

/* ===== Language Descriptor ===== */

typedef struct {
    uint32_t id;
    char     name[HDCM_MAX_LABEL];
    char     file_extension[8];
    hdcm_lang_category_t category;
    uint32_t construct_count;
    hdcm_construct_map_t constructs[HDCM_MAX_CONSTRUCTS];
    hdcm_vector_t language_vector;  /* Base vector for this language */
    bool     active;
} hdcm_language_t;

/* ===== Conversion Matrix (Language Pair) ===== */

typedef struct {
    uint32_t source_lang_id;
    uint32_t target_lang_id;
    hdcm_vector_t transform[HDCM_MAX_CONSTRUCTS];  /* Per-construct transform */
    uint32_t num_mappings;
    float    compatibility_score;  /* 0.0-1.0, how well languages align */
    bool     bidirectional;        /* Can reverse the transform */
} hdcm_matrix_t;

/* ===== Translation Result ===== */

typedef struct {
    char     output[HDCM_MAX_OUTPUT];
    uint32_t output_len;
    uint32_t constructs_translated;
    uint32_t constructs_unmapped;   /* No equivalent found */
    float    fidelity_score;        /* 0.0-1.0 */
    hdcm_phase_t final_phase;
    bool     success;
} hdcm_result_t;

/* ===== HDCM Engine ===== */

typedef struct {
    hdcm_language_t languages[HDCM_MAX_LANGUAGES];
    uint32_t        num_languages;
    hdcm_matrix_t   matrices[HDCM_MAX_MATRICES];
    uint32_t        num_matrices;
    uint32_t        total_translations;
    uint32_t        create_count;
    uint32_t        entangle_count;
    uint32_t        measure_count;
    bool            initialized;
} hdcm_t;

/* ===== API ===== */

void hdcm_init(hdcm_t *h);

/* Language registration */
int32_t hdcm_language_register(hdcm_t *h, const char *name,
                                const char *extension,
                                hdcm_lang_category_t category);
int hdcm_language_add_construct(hdcm_t *h, uint32_t lang_idx,
                                 hdcm_construct_t type,
                                 const char *source_token,
                                 const char *target_token);
const hdcm_language_t *hdcm_language_find(hdcm_t *h, const char *name);
const hdcm_language_t *hdcm_language_by_ext(hdcm_t *h, const char *ext);

/* Conversion matrix operations */
int32_t hdcm_matrix_create(hdcm_t *h, uint32_t src_lang, uint32_t dst_lang);
int hdcm_matrix_build(hdcm_t *h, uint32_t matrix_idx);
const hdcm_matrix_t *hdcm_matrix_find(hdcm_t *h, uint32_t src, uint32_t dst);
float hdcm_matrix_compatibility(hdcm_t *h, uint32_t src, uint32_t dst);

/* Second Quantization Translation Pipeline */
int hdcm_translate(hdcm_t *h, uint32_t src_lang, uint32_t dst_lang,
                    const char *source_code, hdcm_result_t *result);

/* Phase operations (second quantization) */
int hdcm_phase_create(hdcm_t *h, uint32_t lang_idx,
                       const char *source_code,
                       hdcm_vector_t *out_vector);
int hdcm_phase_entangle(hdcm_t *h, uint32_t src_lang, uint32_t dst_lang,
                         const hdcm_vector_t *src_vector,
                         hdcm_vector_t *out_vector);
int hdcm_phase_measure(hdcm_t *h, uint32_t dst_lang,
                        const hdcm_vector_t *entangled,
                        char *output, uint32_t max_len);

/* Vector operations */
void hdcm_vector_zero(hdcm_vector_t *v);
void hdcm_vector_random(hdcm_vector_t *v, uint32_t seed);
void hdcm_vector_bind(const hdcm_vector_t *a, const hdcm_vector_t *b,
                       hdcm_vector_t *out);  /* XOR bind */
void hdcm_vector_superpose(const hdcm_vector_t *a, const hdcm_vector_t *b,
                            hdcm_vector_t *out);  /* Majority vote */
uint32_t hdcm_vector_hamming(const hdcm_vector_t *a, const hdcm_vector_t *b);
float hdcm_vector_similarity(const hdcm_vector_t *a, const hdcm_vector_t *b);
void hdcm_vector_permute(const hdcm_vector_t *in, uint32_t shift,
                          hdcm_vector_t *out);

/* Utility */
const char *hdcm_construct_name(hdcm_construct_t type);
const char *hdcm_category_name(hdcm_lang_category_t cat);
const char *hdcm_phase_name(hdcm_phase_t phase);

/* Omni-compatibility check */
bool hdcm_omni_compatible(hdcm_t *h, uint32_t lang_idx);
uint32_t hdcm_compatible_count(hdcm_t *h, uint32_t lang_idx);

#endif /* ZEDEC_HDCM_H */
