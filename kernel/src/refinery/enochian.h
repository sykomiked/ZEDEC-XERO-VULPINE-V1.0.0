/* enochian.h — the Enochian language core of the Magitech Refinery
 *
 * WHY THE OS CARRIES A LANGUAGE
 * -----------------------------
 * The Glyph & Grid sigils were not drawn — they were COMPILED, and the
 * source language is Enochian. The pipeline that made all 52,095 cards is:
 *
 *     word  ->  gematria  ->  digital root  ->  star polygon {root+3 / k}
 *     word  ->  SHA-256   ->  kamea path    ->  the red circuit trace
 *
 * so the language layer is not flavor text. It is the front half of the
 * compiler. This module is that layer, native and freestanding, taken
 * from the reconstruction grammar (GRAMMAR.md, 48 seed lexemes + 233,078
 * cube-generated derivations) and byte-checked against the shipped deck.
 *
 * THE ALPHABET OF TWENTY-ONE LETTERS
 * ----------------------------------
 * Each letter carries a gematric value. J, K, W, Y are absent from the
 * canonical alphabet and fold as allographs: J->I, K->C, W->U, Y->I.
 * Verified against seal_24525: OLPIRT HPOU = 54, digital root 9, and the
 * card's printed section mark reads §54.
 *
 * THE NINE ROOTS
 * --------------
 * The digital root (1..9) of a word's gematria selects its semantic
 * domain and, downstream, its star fabric {root+3/k}. Digital-root
 * arithmetic wraps on mod 9, so every root has a mirror partner
 * (1<->8, 2<->7, 3<->6, 4<->5, 9 self) — the grammar's "reflective
 * semantics", and a free consistency check on any claimed root.
 *
 * THE THREE VOICES
 * ----------------
 * The deck ships three Card-Between-Cards — the engine's own law written
 * three ways: the Wizard's Compendium (command), the Witch's Grimoire
 * (receptive), the Alchemist's Tome (balanced). They are the same law in
 * three registers, and they map exactly onto ZXV Tri-Space:
 *
 *     ENO_VOICE_SOLAR  = S+  command    "Invoke... Speak with conviction"
 *     ENO_VOICE_LUNAR  = S-  receptive  "Awaken... Whisper with devotion"
 *     ENO_VOICE_AEON   = S0  balanced   "Activate... Vibrate with presence"
 *
 * Everything the Refinery says to a human (operating instructions,
 * activation lines) is rendered in one of the three voices.
 *
 * Freestanding: integer only, no libc, no allocation.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV Magitech Refinery slice)
 * License: SEL-3.3
 */
#ifndef ZXV_ENOCHIAN_H
#define ZXV_ENOCHIAN_H

#include <stdint.h>
#include <stdbool.h>

#define ENO_LETTERS 21u

typedef enum {
    ENO_VOICE_SOLAR = 0,   /* S+  masculine  — the Wizard's Compendium  */
    ENO_VOICE_LUNAR = 1,   /* S-  feminine   — the Witch's Grimoire     */
    ENO_VOICE_AEON  = 2    /* S0  androgynous — the Alchemist's Tome    */
} eno_voice_t;

/* Gematric value of one character after allograph folding; 0 for
 * characters outside the alphabet (digits, punctuation, other scripts). */
uint32_t eno_letter_value(char c);

/* Fold an allograph to its canonical letter (J->I, K->C, W->U, Y->I,
 * lowercase -> uppercase). Characters outside the alphabet pass through
 * unchanged — they simply carry no gematria. */
char eno_fold(char c);

/* Sum of letter values over a text. Any language may be passed in: only
 * characters that fold into the twenty-one letters contribute. */
uint32_t eno_gematria(const char *text, uint32_t len);

/* Digital root 1..9. Zero (no alphabet letters at all) yields 9, matching
 * the generation pipeline exactly. */
uint32_t eno_root(uint32_t gematria);

/* The mirror partner of a root: 1<->8, 2<->7, 3<->6, 4<->5, 9<->9. */
uint32_t eno_root_mirror(uint32_t root);

/* Domain name of a root ("Monadic - Unity", ...). */
const char *eno_root_domain(uint32_t root);

/* Angelic name of a canonical letter ("Un" for A, "Pa" for B, ...),
 * or "" if the character is not one of the twenty-one. */
const char *eno_letter_name(char c);

/* The law of the engine in the requested voice — the Card-Between-Cards
 * text, embedded verbatim. The OS carries its own source. */
const char *eno_law(eno_voice_t v);
const char *eno_law_title(eno_voice_t v);

/* Activation verbs of the three voices, used when the Refinery composes
 * operating instructions: invoke/speak, awaken/whisper, activate/vibrate. */
const char *eno_voice_verb(eno_voice_t v);
const char *eno_voice_manner(eno_voice_t v);

#endif /* ZXV_ENOCHIAN_H */
