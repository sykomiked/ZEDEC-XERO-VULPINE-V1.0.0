/*
 * gematria.h — Gematria Syntactic Mapping System
 *
 * Maps natural language characters/phonemes to numerical values,
 * then projects into hyperdimensional Fock space via grammatical operators.
 *
 * Grammar as operators:
 *   Nouns      → State vectors |v⟩ (base values from gematria sums)
 *   Verbs      → Creation/Annihilation operators (a†, a)
 *   Adjectives → Scalar/phase modifiers (e^(iθ))
 *   Period (.) → State collapse (measurement)
 *   Comma (,)  → Tensor product ⊗ (composite state)
 *   Question (?)→ Superposition (G+ / speculative)
 *
 * Gematria systems: Standard (Hebrew), Ordinal (A=1..Z=26),
 *   Reduction (digital root), ASCII, Custom
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#ifndef ZEDEC_GEMATRIA_H
#define ZEDEC_GEMATRIA_H

#include <stdint.h>
#include <stdbool.h>
#include "hdcm.h"

/* ===== Constants ===== */

#define GEM_MAX_TEXT         4096
#define GEM_MAX_WORDS        256
#define GEM_MAX_WORD_LEN      64
#define GEM_MAX_LABEL         64
#define GEM_MAX_SYSTEMS        8
#define GEM_MAX_MAPPINGS     128

/* ===== Gematria Systems ===== */

typedef enum {
    GEM_SYSTEM_ORDINAL    = 0,  /* A=1, B=2, ... Z=26 */
    GEM_SYSTEM_STANDARD   = 1,  /* Hebrew traditional: A=1,B=2,...,I=10,J=10,... */
    GEM_SYSTEM_REDUCTION  = 2,  /* Digital root of ordinal */
    GEM_SYSTEM_ASCII      = 3,  /* Raw ASCII values */
    GEM_SYSTEM_PRIME      = 4,  /* A=2,B=3,C=5,... (primes) */
    GEM_SYSTEM_TORAH      = 5,  /* Torah-based extended */
    GEM_SYSTEM_GREEK      = 6,  /* Greek isopsophy */
    GEM_SYSTEM_CUSTOM     = 7,  /* User-defined mapping */
} gem_system_t;

/* ===== Grammatical Role ===== */

typedef enum {
    GEM_ROLE_NOUN       = 0,  /* State vector |v⟩ */
    GEM_ROLE_VERB       = 1,  /* Operator a†/a */
    GEM_ROLE_ADJECTIVE  = 2,  /* Scalar modifier e^(iθ) */
    GEM_ROLE_ADVERB     = 3,  /* Phase modifier */
    GEM_ROLE_ARTICLE    = 4,  /* Identity operator (no change) */
    GEM_ROLE_PREPOSITION = 5,  /* Tensor product ⊗ */
    GEM_ROLE_CONJUNCTION = 6,  /* Superposition + */
    GEM_ROLE_PRONOUN    = 7,  /* Reference vector */
    GEM_ROLE_INTERJECTION = 8, /* Delta function */
    GEM_ROLE_UNKNOWN    = 9,
} gem_role_t;

/* ===== Punctuation as Operators ===== */

typedef enum {
    GEM_PUNCT_NONE      = 0,
    GEM_PUNCT_PERIOD    = 1,  /* State collapse (measurement) */
    GEM_PUNCT_COMMA     = 2,  /* Tensor product ⊗ */
    GEM_PUNCT_QUESTION  = 3,  /* Superposition G+ */
    GEM_PUNCT_EXCLAIM   = 4,  /* Forced collapse (strong measurement) */
    GEM_PUNCT_SEMICOLON = 5,  /* Weak tensor product */
    GEM_PUNCT_COLON     = 6,  /* Definition operator */
    GEM_PUNCT_PAREN_OPEN  = 7,  /* Open subspace */
    GEM_PUNCT_PAREN_CLOSE = 8,  /* Close subspace */
} gem_punct_t;

/* ===== Word Analysis ===== */

typedef struct {
    char     text[GEM_MAX_WORD_LEN];
    uint32_t gematria_value;        /* Sum of character values */
    gem_role_t grammatical_role;    /* Noun, verb, etc. */
    gem_punct_t trailing_punct;     /* Punctuation after word */
    hdcm_vector_t fock_vector;      /* Hyperdim representation */
    uint32_t char_values[GEM_MAX_WORD_LEN]; /* Per-char values */
    uint32_t char_count;
    uint32_t phase_shift; /* Adjective/adverb modifier, permille of a turn */
} gem_word_t;

/* ===== Sentence Analysis ===== */

typedef struct {
    gem_word_t words[GEM_MAX_WORDS];
    uint32_t   num_words;
    uint32_t   total_gematria;       /* Sum of all word values */
    hdcm_vector_t sentence_vector;   /* Composite Fock state */
    uint32_t   noun_count;
    uint32_t   verb_count;
    uint32_t   modifier_count;
    bool       has_superposition;    /* Contains ? */
    bool       has_collapse;         /* Contains . */
    bool       measured;             /* Was state collapsed? */
} gem_sentence_t;

/* ===== Custom Mapping ===== */

typedef struct {
    uint32_t char_value[256];  /* ASCII → value */
    bool     active;
} gem_custom_mapping_t;

/* ===== Gematria Engine ===== */

typedef struct {
    gem_custom_mapping_t custom_mappings[GEM_MAX_SYSTEMS];
    bool                 initialized;
    uint32_t             total_sentences_processed;
    uint32_t             total_words_processed;
} gematria_t;

/* ===== API ===== */

void gematria_init(gematria_t *g);

/* Character value lookup */
uint32_t gematria_char_value(gematria_t *g, char c, gem_system_t system);
uint32_t gematria_word_value(gematria_t *g, const char *word, gem_system_t system);
uint32_t gematria_reduce(uint32_t value);  /* Digital root */

/* Custom mapping */
int gematria_set_custom(gematria_t *g, char c, uint32_t value);

/* Sentence analysis (grammatical operator mapping) */
int gematria_analyze_sentence(gematria_t *g, const char *text,
                               gem_system_t system,
                               gem_sentence_t *out);

/* Fock state projection: gematria → hyperdim vector */
int gematria_to_fock(gematria_t *g, const gem_word_t *word,
                     const hdcm_vector_t *language_base,
                     hdcm_vector_t *out_vector);
int gematria_sentence_to_fock(gematria_t *g, const gem_sentence_t *sentence,
                               hdcm_vector_t *out_vector);

/* Grammatical role detection */
gem_role_t gematria_detect_role(const char *word);
gem_punct_t gematria_detect_punct(char c);

/* Phase mapping to file archetypes */
const char *gematria_phase_mapping(gem_role_t role);

/* Utility */
const char *gematria_system_name(gem_system_t system);
const char *gematria_role_name(gem_role_t role);
const char *gematria_punct_name(gem_punct_t p);

#endif /* ZEDEC_GEMATRIA_H */
