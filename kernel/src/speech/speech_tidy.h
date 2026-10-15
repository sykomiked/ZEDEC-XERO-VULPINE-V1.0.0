/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* speech_tidy.h — turn text on a screen into what a person would say aloud.
 *
 * A careful human reading a page to someone does not spell out a commit
 * hash, a session id or a base64 blob character by character, read out
 * markdown stars, or recite a code listing. This module rewrites text before
 * it goes to a voice, so read-aloud sounds like a person, not a printer.
 *
 *   TD1 NAME IT, DON'T SPELL IT. A hex hash ("4bea0160fe2f...", 7 or more hex
 *       digits with letters and digits both, or 0x and 9 or more), a UUID, an
 *       id with a random-looking part ("cmsg_01SiwTx..."), a random code or
 *       token (base64, keys), and a run of 13 or more digits are each said as
 *       what they are: "a hash", "an ID", "a code", "a long number".
 *   TD2 SAY HOW MANY. Several of the same kind in a row (separated by spaces
 *       or commas) are said once with a count: "3 hashes".
 *   TD3 LINKS, FILES AND ADDRESSES. A link is "a link to github.com" (the
 *       site, never the path or query). A path is "the file zui_hear.c" or
 *       "the folder models" (its last part). An email address is read the
 *       way people say it ("bob at example dot com"), or "an email address"
 *       when its name is a random code.
 *   TD4 CODE IS DESCRIBED, NOT RECITED. A fenced code block (``` ... ```)
 *       becomes one sentence, "There is some code here."; inline `code`
 *       loses its backticks and the rules above apply to what is inside.
 *   TD5 MARKUP IS SILENT. Markdown heading marks, quote marks, bullets,
 *       emphasis stars and underscores, table bars and rules (---, ===) are
 *       not said. A heading, a bullet or a line before a blank line ends in a
 *       pause (a full stop is added when it has none); a sentence wrapped
 *       over two lines is not broken. snake_case words are read as words
 *       ("zxv policy"), and "#153" is "number 153".
 *   TD6 EXACT ON REQUEST. speech_spell says the same text with every hash,
 *       id, code and long number spelled out, grouped in fours ("4 b e a,
 *       0 1 6 0"), with "capital" before capitals in mixed-case codes and
 *       symbols by name ("dash"), for when the person asks to hear it exactly.
 *   TD7 WORDS ARE A TABLE. Every word this module says comes from a
 *       speech_tidy_words_t, so each spoken language brings its own.
 *
 * Freestanding: no libc, no allocation, no float. Output is NUL-terminated,
 * never longer than cap - 1 bytes, and never ends inside a UTF-8 character.
 */
#ifndef ZXV_SPEECH_TIDY_H
#define ZXV_SPEECH_TIDY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SPEECH_TK_WORD = 0, /* say it as it is                         */
    SPEECH_TK_HASH,     /* TD1 */
    SPEECH_TK_ID,       /* TD1 */
    SPEECH_TK_CODE,     /* TD1 */
    SPEECH_TK_NUMBER,   /* TD1: 13 or more digits                  */
    SPEECH_TK_LINK,     /* TD3 */
    SPEECH_TK_PATH,     /* TD3 */
    SPEECH_TK_EMAIL,    /* TD3 */
    SPEECH_TK_JUNK      /* TD5: symbols only, not said             */
} speech_tk_t;

typedef struct {
    const char *tag;              /* "en" */
    const char *hash, *hashes;    /* "a hash", "%u hashes"         */
    const char *id, *ids;         /* "an ID", "%u IDs"             */
    const char *code, *codes;     /* "a code", "%u codes"          */
    const char *number, *numbers; /* "a long number", "%u long numbers" */
    const char *link, *link_to;   /* "a link", "a link to %s"      */
    const char *file, *folder;    /* "the file %s", "the folder %s" */
    const char *email;            /* "an email address"            */
    const char *at, *dot;         /* "at", "dot" (in an address)   */
    const char *number_sign;      /* "number" ("#153")             */
    const char *code_block;       /* "There is some code here."    */
    const char *capital;          /* TD6: "capital"                */
    const char *dash, *underscore, *slash, *colon, *plus, *equals, *period; /* TD6 */
} speech_tidy_words_t;

extern const speech_tidy_words_t speech_tidy_en;

/* What one token is (no spaces in it; surrounding punctuation already off). */
speech_tk_t speech_tidy_kind(const char *t, uint32_t n);

/* TD1-TD5: n bytes of in to what a person would say. w NULL: English.
 * Returns the bytes written to out (without the NUL). */
uint32_t speech_tidy(const char *in, uint32_t n, char *out, uint32_t cap,
                     const speech_tidy_words_t *w);
/* TD6: the same text, with hashes, ids, codes and long numbers spelled. */
uint32_t speech_spell(const char *in, uint32_t n, char *out, uint32_t cap,
                      const speech_tidy_words_t *w);

/* speech_tidy with the words as a const void *, for hooks that do not
 * depend on speech/ (zui_reader_voice_t.tidy). */
uint32_t speech_tidy_fn(const char *in, uint32_t n, char *out, uint32_t cap, const void *words);

#endif /* ZXV_SPEECH_TIDY_H */
