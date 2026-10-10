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
 * Each letter carries a gematric value. J, K and W are absent from the
 * canonical alphabet and fold as allographs: J->I, K->C, W->U. That fold
 * table is a kernel-side reconstruction, NOT deck provenance: J, K and W
 * appear zero times in the 52,095-card corpus, so nothing tests them.
 * Y is absent too but is NOT folded — it scores as itself, i.e. zero,
 * because the deck scores it so on all 15 Y-bearing cards (see eno_fold).
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
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
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

/* Fold an allograph to its canonical letter (J->I, K->C, W->U, and
 * lowercase -> uppercase). Y and V are NOT folded — the shipped deck
 * scores both as themselves, i.e. as zero. Characters outside the
 * alphabet pass through unchanged — they simply carry no gematria. */
char eno_fold(char c);

/* Sum of letter values over a text. Any language may be passed in: only
 * characters that fold into the twenty-one letters contribute. */
uint32_t eno_gematria(const char *text, uint32_t len);

/* ---- polarity: the second channel ------------------------------------
 * Gematria is a MAGNITUDE and cannot carry a sign. The doubled letters of
 * the alphabet are polarity pairs: ALPHA names the letter written U
 * "Vau", and V is its consonant face. U is light (+1), V is shadow (-1).
 * See PROVENANCE/ENOCHIAN_POLARITY.md. This is [SPEC] — the distinction is
 * this system's, not Dee's — and it is the letter-level form of the S+/S-/S0
 * ternary this module already carries at card level (eno_voice_t).
 *
 * V's zero MAGNITUDE and its negative CHARGE are one finding, not a
 * contradiction: 552 of the 52,095 shipped cards carry V and all 552 score
 * it 0. Light emanates and carries value; shadow carries direction but no
 * determinate magnitude — which is why VPAAH lifts to VEIL_AIN_SOPH
 * ("unbounded / unresolved potential") and UPAAH to VEIL_AIN_SOPH_AUR
 * ("the source of emanation") in dharma/upaah.h.
 *
 * W and Y take charge 0 BY OMISSION, which is a default and not a finding:
 * neither occurs anywhere in the 52,095-card corpus, so no evidence exists.
 * Gon's faces (I, J, Y) and Veh's (C, K) are deliberately UNCHARGED: see
 * ENOCHIAN_POLARITY.md §4.1 and §4.3. The charge is ORTHOGRAPHIC, not
 * moral — it measures which face of Vau a writer chose, nothing more
 * (measured: LOVE -1, CURSE +1, LIGHT 0). No UI may present it as a
 * judgement of the intent. */

/* +1 light (U), -1 shadow (V), 0 everything else. Operates on the RAW
 * character (uppercased only) — deliberately NOT folded, because folding
 * W->U would charge W as light. */
int32_t eno_letter_charge(char c);

/* Signed sum of letter charges over a text. Independent of eno_gematria:
 * magnitude and charge are two channels, not one number. */
int32_t eno_net_charge(const char *text, uint32_t len);

/* The tri-space role of a net charge. Ordering matches tri_role_t
 * (TRI_POSITIVE/NEGATIVE/NEUTRAL) and eno_voice_t (SOLAR/LUNAR/AEON):
 *   >0 -> 0  S+  UPAAH   <0 -> 1  S-  VPAAH   =0 -> 2  S0  PIR */
uint32_t eno_charge_role(int32_t net);

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
