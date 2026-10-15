/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* speech_g2p_en.h — English text to the phonemes Kokoro speaks (misaki's
 * American English symbols), on the device, with no GPL code.
 *
 * Kokoro (tensor/zt_kokoro.h) reads phonemes, not letters. Its own pipeline
 * uses misaki's dictionaries and falls back to espeak-ng, which is GPL-3 and
 * never bundled here. This module does the same job in our own code:
 *
 *   G1 A DICTIONARY FILE, CHECKED. The lexicon is a .zxlx file built by
 *      tools/kokoro/build_lexicon.py from misaki's us_gold and us_silver
 *      (Apache-2.0; about 184,000 words, 5 MB), shipped beside the voice and
 *      never in git. speech_lex_parse checks every byte (magic, version,
 *      offsets, strictly sorted words, phonemes only from Kokoro's symbols)
 *      and refuses a broken file whole.
 *   G2 SENTENCE CONTEXT. "the" is ði before a vowel and ðə before a
 *      consonant; "to" is tʊ / tə / tu (before a vowel, a consonant, a
 *      pause); "a" is the reduced ɐ unless it stands alone; "an" ɐn; "I" ˌI;
 *      as misaki does.
 *   G3 WORKS WITHOUT THE FILE. A built-in table (speech_g2p_words.inc,
 *      generated from misaki's gold list) holds the commonest words, number
 *      words, letter names and our own words (Chiglet, ZXV, GitHub ...), so
 *      the Chiglet can talk before the dictionary is installed.
 *   G4 WORDS IT HAS NOT SEEN. In order: our words; the dictionary as written,
 *      then lower case; a short all-capital word is spelt out (ZXV ->
 *      zˌiˌɛksvˈi); -s, -'s, -ed and -ing are taken off a known stem and put
 *      back by sound (misaki's rules); two known words joined (github ->
 *      git + hub, the second stress made secondary); then letter-to-sound
 *      rules in the manner of the US Naval Research Laboratory's (Elovitz et
 *      al., 1976), written here, with the stress on the first full vowel.
 *   G5 NUMBERS. Cardinals to the trillions, decimals ("point" and digits),
 *      ordinals (1st, 22nd), years (1999, 2026), a leading minus, "%" as
 *      percent, "$" as dollars, long digit runs one digit at a time. "&",
 *      "+", "@" are said as words.
 *   G6 PUNCTUATION IS PROSODY. , . ! ? ; : — … ( ) and curly quotes pass
 *      through (Kokoro pauses and lifts on them); other symbols are dropped.
 *   G7 BOUNDED. The caller owns the output; when it would not fit, the call
 *      returns -1 and nothing is to be used. Words over 63 bytes are spelt
 *      by rules in pieces.
 *
 * Freestanding: no libc, no allocation, no float. Reentrant: no hidden state.
 */
#ifndef ZXV_SPEECH_G2P_EN_H
#define ZXV_SPEECH_G2P_EN_H

#include <stdbool.h>
#include <stdint.h>

#define SPEECH_LEX_MAGIC   "ZXLX"
#define SPEECH_LEX_VERSION 1u
#define SPEECH_G2P_WORD    64u /* bytes of a word looked up whole */

typedef enum {
    SPEECH_LEX_OK = 0,
    SPEECH_LEX_EARG = -1,
    SPEECH_LEX_EMAGIC = -2,
    SPEECH_LEX_EVERSION = -3,
    SPEECH_LEX_ESIZE = -4,
    SPEECH_LEX_EOFFSET = -5,
    SPEECH_LEX_ERECORD = -6,
    SPEECH_LEX_EORDER = -7,
    SPEECH_LEX_EPHONE = -8
} speech_lex_status_t;

/* A parsed lexicon: points into the caller's bytes, which must outlive it. */
typedef struct {
    uint32_t count, blob_len;
    const uint8_t *offs; /* count little-endian u32 */
    const uint8_t *blob;
} speech_lex_t;

/* G1: check and read a .zxlx file. On error *l is zeroed. */
speech_lex_status_t speech_lex_parse(speech_lex_t *l, const uint8_t *b, uint64_t n);
/* The phonemes of word w (len bytes, exact), NUL-terminated, or 0. */
const char *speech_lex_find(const speech_lex_t *l, const char *w, uint32_t len);

typedef struct {
    uint32_t words;    /* words said                                   */
    uint32_t builtin;  /* found in the built-in table                  */
    uint32_t lexicon;  /* found in the dictionary file                 */
    uint32_t stemmed;  /* a known stem with -s, -ed or -ing            */
    uint32_t compound; /* two known words                              */
    uint32_t spelled;  /* spelt out letter by letter                   */
    uint32_t rules;    /* letter-to-sound rules                        */
    uint32_t numbers;  /* numbers said                                 */
} speech_g2p_stats_t;

/* Text (UTF-8, len bytes) to phonemes in out (cap bytes, NUL-terminated).
 * lex may be NULL (G3). Returns the length, or -1 when it does not fit. */
int32_t speech_g2p_en(const speech_lex_t *lex, const char *text, uint32_t len, char *out,
                      uint32_t cap, speech_g2p_stats_t *st);

/* The phonemes of one word (no context rules), for tests and tools; returns
 * the length or -1. */
int32_t speech_g2p_word(const speech_lex_t *lex, const char *w, uint32_t len, char *out,
                        uint32_t cap);

/* Credits to show with the voice. */
extern const char speech_g2p_en_attribution[];

#endif /* ZXV_SPEECH_G2P_EN_H */
