/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_nada.h — the sound view of the alphabet (docs/spec/ALPHABET.md
 * section 5): each consonant's place and manner by Śikṣā, and an earcon for
 * each sutra cell.
 *
 * The tone is a chosen mapping for accessibility, so a listener can tell
 * operations and evidence states apart. The pitches are just-intonation
 * ratios over 144 Hz because they are whole numbers that sound consonant;
 * 144 Hz and 432 Hz carry no special physical meaning. This file only
 * computes numbers. Whatever plays them (the voice and app-sound code)
 * plays only on tap. A tone for a checked block reports the CID and
 * signature check after it has run; it never replaces it.
 *
 * Freestanding C11: integers only, no allocation. */
#ifndef ZXV_ZL_NADA_H
#define ZXV_ZL_NADA_H

#include <stdint.h>

/* sthāna, where the sound is made */
enum {
    ZL_KANTHYA = 0,   /* throat: k kh g gh ṅ h */
    ZL_TALAVYA = 1,   /* palate: c ch j jh ñ y ś */
    ZL_MURDHANYA = 2, /* roof: ṭ ṭh ḍ ḍh ṇ r ṣ */
    ZL_DANTYA = 3,    /* teeth: t th d dh n l s */
    ZL_OSTHYA = 4,    /* lips: p ph b bh m */
    ZL_DANTOSTHYA = 5 /* teeth and lips: v */
};
/* prayatna, the effort */
enum {
    ZL_ALPAPRANA = 0,      /* unvoiced, unaspirated */
    ZL_MAHAPRANA = 1,      /* unvoiced, aspirated */
    ZL_GHOSA = 2,          /* voiced, unaspirated */
    ZL_GHOSA_MAHAPRANA = 3,/* voiced, aspirated */
    ZL_ANUNASIKA = 4,      /* nasal */
    ZL_ANTAHSTHA = 5,      /* semivowel: y r l v */
    ZL_USMAN = 6,          /* unvoiced sibilant: ś ṣ s */
    ZL_USMAN_GHOSA = 7     /* voiced breath: h */
};

typedef struct {
    const char *iast; /* the consonant, e.g. "kh" */
    uint32_t sthana, prayatna, digit; /* digit: its Kaṭapayādi digit */
} zl_varna_t;

#define ZL_NADA_CONSONANTS 33u
/* consonant i (0..32, in Varṇamālā order); 0 past the end */
const zl_varna_t *zl_nada_consonant(uint32_t i);
/* the consonant at the start of an IAST string (longest match), its byte
 * length in *len; 0 if the string does not start with one */
const zl_varna_t *zl_nada_match(const char *s, uint32_t *len);

typedef struct {
    uint32_t carrier_hz; /* 144 Hz times the sthāna's ratio of the root's
                            first consonant: 144, 162, 180, 192, 216, 240 */
    uint32_t formant_hz; /* twice 144 Hz times the ratio of its last one */
    uint32_t envelope;   /* the first consonant's prayatna */
    uint32_t phase_deg;  /* the evidence tag: N 0, T 90, F 180, B 270 */
} zl_tone_t;

/* The earcon of a sutra cell. Returns 0, or -1 for a cell that is not one
 * operation's (zl_sutra_op). */
int32_t zl_nada_tone(uint64_t cell, zl_tone_t *t);

#endif
