/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_enochian.h — Enochian as the swarm's core internal language.
 *
 * Ported from the owner's Enochian linguistics scripts (derive_definitions.py,
 * universal_translator.py, phonetic.py and the 233,126-entry dictionary).
 * The values below are exactly those scripts' values.
 *
 *   E1  THE LETTERS.  23 canonical letters with their gematria
 *       (A=6 B=5 C=3 D=4 E=9 F=6 G=1 H=5 I=3 L=8 M=9 N=8 O=6 P=5 Q=10 R=3
 *       S=3 T=9 U=4 V=0 X=1 Y=0 Z=7). V and Y are glides worth 0
 *       (VORS = 12, GNAY = 15). J, K and W are allographs of I, C and U.
 *   E2  ROOT AND DOMAIN.  A value's digital root (1..9, with 0 -> 9) picks one
 *       of the nine domains, MONADIC .. ENNEADIC.
 *   E3  SANSKRIT PHONETICS.  Every letter has a Devanagari form and an IAST
 *       spelling (X = ksa, Q = ka; the glides are silent), as in the dictionary.
 *   E4  NUMEROLOGY.  Any number, from text or from mathematics, reads out as
 *       the dictionary's annotation does: digit meaning, master number,
 *       vortex class, mod 7 planet, mod 3 alchemical element, prime, Fibonacci.
 *   E5  VORTEX MATHEMATICS.  Doubling mod 9 runs the loop 1 2 4 8 7 5; 3 6 9
 *       are the axis. Every root has a vortex class and a loop position.
 *   E6  UNBOUNDED DIMENSIONS.  Your scripts climb from letters (1D) through
 *       pairs (2D), the 49^3 cube (3D, the "cube3d" words), the inflection
 *       axis (4D, the "cube4d" words) and on to 5D-13D compositions. Here every
 *       dimension is the same move: a dimension-(d+1) unit is a sequence of
 *       dimension-d units. So there is no top dimension. A unit's summary is
 *       built from its children's summaries alone, because the digital root
 *       and every residue (mod 9, 7, 3, 49) respect addition. Summarising any
 *       composition costs one step per child, never a re-read of the text.
 *       The caller supplies the storage for the per-dimension counts, so the
 *       only bound on dimension is the memory the compute budget grants.
 *   E7  THE 168-BIT CODEC.  36 symbols (23 letters, a word break and an end
 *       mark) pack into one 21-byte UBH-168 frame, as base-25 digits:
 *       25^36 < 2^168. That is 4.67 bits a letter instead of 8, losslessly.
 *   E8  OPERATOR LANGUAGES.  The person can install any language as the one
 *       they are answered in. That choice never changes the Enochian core:
 *       the swarm processes and stores in Enochian and translates at the edge.
 * Freestanding: no libc, no allocation, no floating point, no 64-bit division.
 */
#ifndef SWARM_ENOCHIAN_H
#define SWARM_ENOCHIAN_H

#include <stdint.h>
#include <stdbool.h>

#define SWARM_EN_LETTERS       23u
#define SWARM_EN_FRAME_BYTES   21u /* one UBH-168 frame */
#define SWARM_EN_FRAME_SYMBOLS 36u /* E7 */
#define SWARM_EN_LATTICE       49u /* 7 x 7, the cube's side */

/* E1 */
int swarm_en_index(char c);           /* 0..22 after allographs, -1 if not a letter */
char swarm_en_letter(uint32_t index); /* the canonical letter, or 0 */
uint32_t swarm_en_value(char c);
uint32_t swarm_en_gematria(const char *text, uint32_t len);

/* E2 */
uint32_t swarm_en_root(uint64_t n);
const char *swarm_en_domain(uint32_t root);

/* E3: write the word's Devanagari (or IAST) spelling, NUL-terminated.
 * Returns its length in bytes, or -1 if buf is too small. */
int32_t swarm_en_sanskrit(const char *word, uint32_t len, char *buf, uint32_t cap);
int32_t swarm_en_iast(const char *word, uint32_t len, char *buf, uint32_t cap);

/* E4, E5 */
typedef enum { SWARM_VORTEX_LOOP = 0, SWARM_VORTEX_AXIS = 1 } swarm_vortex_t;

typedef struct {
    uint32_t n;
    uint8_t root, mod9, mod7, mod3;
    bool master, prime, fibonacci;
    swarm_vortex_t vortex;
    int8_t loop_step; /* place in 1 2 4 8 7 5 (0..5), -1 on the axis */
    const char *digit_meaning;
    const char *master_meaning; /* "" unless master */
    const char *planet;         /* mod 7 */
    const char *element;        /* mod 3 */
} swarm_numen_t;

swarm_numen_t swarm_numen(uint32_t n);
uint32_t swarm_vortex_next(uint32_t root); /* E5: root(2 * root) */

/* E6: the summary of one unit at any dimension. `units[k]` counts the
 * dimension-(k+1) units inside it (units[0] letters, units[1] words, ...);
 * the array and its capacity come from the caller. */
typedef struct {
    uint64_t gematria;
    uint32_t dim; /* 1 = letter, 2 = word, 3 = phrase, ... unbounded */
    uint64_t *units;
    uint32_t cap; /* length of units[] */
    uint8_t root, mod7, mod3, mod49;
} swarm_en_unit_t;

void swarm_en_unit_init(swarm_en_unit_t *u, uint32_t dim, uint64_t *units, uint32_t cap);
bool swarm_en_unit_letter(swarm_en_unit_t *u, char c);                    /* a 1D unit */
bool swarm_en_unit_word(swarm_en_unit_t *u, const char *w, uint32_t len); /* a 2D unit */
/* Append child to parent; parent->dim must be child->dim + 1. */
bool swarm_en_unit_add(swarm_en_unit_t *parent, const swarm_en_unit_t *child);
/* The unit's place in the 49^dim lattice: axis 0 is gematria mod 49, axis k
 * is the count of dimension-k units mod 49. Writes min(dim, cap) axes. */
uint32_t swarm_en_unit_coords(const swarm_en_unit_t *u, uint8_t *out, uint32_t cap);

/* E7: pack canonical Enochian text (letters and single spaces only) into
 * 21-byte frames. Returns bytes written, or -1 for a character outside the
 * alphabet or too little room. Unpack returns the text length, or -1. */
int32_t swarm_en_pack(const char *text, uint32_t len, uint8_t *out, uint32_t cap);
int32_t swarm_en_unpack(const uint8_t *in, uint32_t len, char *out, uint32_t cap);

/* E8 */
#define SWARM_LANG_MAX 21u
typedef struct {
    char code[16]; /* BCP 47 tag: "en", "es", "ja", "sa", ... */
    char name[32];
    uint32_t translator; /* the agent that translates to and from it */
} swarm_lang_t;

typedef struct {
    swarm_lang_t lang[SWARM_LANG_MAX];
    uint32_t count;
    uint32_t operator_lang; /* index of the language the person is answered in */
} swarm_lang_table_t;

void swarm_lang_init(swarm_lang_table_t *t); /* installs English as the operator */
int32_t swarm_lang_install(swarm_lang_table_t *t, const char *code, const char *name,
                           uint32_t translator); /* index, or -1 */
bool swarm_lang_set_operator(swarm_lang_table_t *t, const char *code);
const char *swarm_lang_core(void); /* always "enochian" */

#endif /* SWARM_ENOCHIAN_H */
