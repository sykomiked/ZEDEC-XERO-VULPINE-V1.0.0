/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* chg_name.h — naming the chiglet: the first thing a person does with it.
 *
 * The person names their chiglet once. From then on it answers to that
 * name, the name can never change, and its personality is grown from the
 * name: the same name and the same person always give the same chiglet.
 *
 *   N1 NAME. 1..CHGN_NAME_MAX bytes of well-formed UTF-8 (no overlong
 *      forms, no surrogates, nothing past U+10FFFF). It starts with a
 *      letter: an ASCII letter or any multi-byte character, so every
 *      script can name its chiglet. After that: letters, ASCII digits, and
 *      a single space, hyphen or apostrophe between two of them. No other
 *      ASCII punctuation, no control characters, no leading, trailing or
 *      doubled separator. The name is kept as spelled.
 *   N2 RESERVED. A name that matches a reserved word is refused: the
 *      system's own words (chiglet, system, root, admin, kernel, owner,
 *      guest, tiamat, zxv) and other companies' assistant or character
 *      names (siri, alexa, cortana, bixby, copilot, gemini, chatgpt,
 *      claude, netnavi, megaman, pet). The match ignores ASCII case and
 *      separators, so "Mega-Man" is "megaman".
 *   N3 SEED. seed = the first 8 bytes (little-endian) of SHA-256 of
 *      "zxv/chiglet/name/v1" || salt || name. The salt is the person's own
 *      32 random bytes, kept with their chiglet, so two people who choose
 *      the same name still get two different chiglets.
 *   N4 PERSONALITY. The DNA is swarm_dna_from_seed(seed), a founder
 *      (generation 0, no parents). Then the name's sound leans three genes
 *      (sound symbolism: soft sounds read warm, hard sounds lively, short
 *      names quick): warmth from the share of vowels and soft consonants
 *      (l m n r w y), temperament from the share of hard consonants (b d g
 *      k p q t x z), pace from the length. Each becomes the average of the
 *      seed's gene and the name's, so the name shapes the personality
 *      without fixing it. Only ASCII letters are scored; a name without any
 *      keeps the seed's genes (no guessing about scripts not scored).
 *   N5 PREVIEW, CONFIRM, LOCK. chgn_preview shows the personality a name
 *      would give and a confirm digest, SHA-256 of "zxv/chiglet/name/
 *      confirm/v1" || salt || name; nothing is kept. chgn_confirm locks the
 *      name only with that exact digest, so the person confirms what they
 *      saw. Once locked, confirm refuses (CHGN_E_LOCKED) and there is no
 *      rename: a new name means a new chiglet, with a new salt.
 *   N6 ADDRESSED. Text is addressed to the chiglet when it starts with the
 *      name (ASCII letters compared without case, other bytes exactly) and
 *      the name is followed by the end or by one of " ,:;!?."; the rest of
 *      the text starts after those separators.
 *   N7 RECORD. A named chiglet is stored as a fixed CHGN_RECORD-byte
 *      record: "ZXCN", version 1, locked flag, length, the name padded with
 *      zeros, the salt, then 8 bytes of SHA-256 over everything before it.
 *      A record that fails any check is refused, never half read.
 *
 * Freestanding C11: no libc, no allocation, no float.
 */
#ifndef ZXV_CHG_NAME_H
#define ZXV_CHG_NAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../swarm/swarm_dna.h"

#define CHGN_NAME_MAX 32u
#define CHGN_SALT     32u
#define CHGN_DIGEST   32u
#define CHGN_RECORD   (4u + 1u + 1u + 1u + CHGN_NAME_MAX + CHGN_SALT + 8u)

enum {
    CHGN_OK = 0,
    CHGN_E_ARG = -1,
    CHGN_E_EMPTY = -2,    /* N1: no name, or longer than CHGN_NAME_MAX */
    CHGN_E_UTF8 = -3,     /* N1: not well-formed UTF-8 */
    CHGN_E_START = -4,    /* N1: does not start with a letter */
    CHGN_E_CHAR = -5,     /* N1: a character a name may not hold */
    CHGN_E_SEP = -6,      /* N1: a separator at an end or doubled */
    CHGN_E_RESERVED = -7, /* N2 */
    CHGN_E_LOCKED = -8,   /* N5: already named; the name is permanent */
    CHGN_E_DIGEST = -9,   /* N5: not the name the person was shown */
    CHGN_E_RECORD = -10   /* N7 */
};

typedef struct {
    uint8_t salt[CHGN_SALT];
    bool locked;
    uint8_t len;
    char name[CHGN_NAME_MAX + 1]; /* NUL-terminated */
    uint64_t seed;                /* N3, set when locked */
} chgn_t;

typedef struct {
    uint64_t seed;
    swarm_dna_t dna;              /* N4 */
    uint8_t confirm[CHGN_DIGEST]; /* N5 */
} chgn_preview_t;

/* a chiglet not named yet, with the person's salt */
void chgn_init(chgn_t *n, const uint8_t salt[CHGN_SALT]);
/* N1, N2: CHGN_OK or the reason the name is refused */
int chgn_check(const char *name, size_t len);
/* a short reason for a code */
const char *chgn_reason(int code);
/* N3, N4, N5: what this name would give; nothing is kept */
int chgn_preview(const chgn_t *n, const char *name, size_t len, chgn_preview_t *out);
/* N5: lock the name with the digest the preview gave */
int chgn_confirm(chgn_t *n, const char *name, size_t len, const uint8_t confirm[CHGN_DIGEST]);
/* N4: the personality of a named chiglet; false when not named */
bool chgn_dna(const chgn_t *n, swarm_dna_t *out);
/* N6: true when text is addressed to the chiglet; *rest = offset of the rest */
bool chgn_addressed(const chgn_t *n, const char *text, size_t len, size_t *rest);
/* N7 */
void chgn_encode(const chgn_t *n, uint8_t out[CHGN_RECORD]);
int chgn_decode(chgn_t *n, const uint8_t in[CHGN_RECORD]);

#endif /* ZXV_CHG_NAME_H */
