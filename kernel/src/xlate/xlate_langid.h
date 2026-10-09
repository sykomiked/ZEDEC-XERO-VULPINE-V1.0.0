/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* xlate_langid.h — compact on-device language identification.
 *
 * Which language is this text in? Answered locally, in integers, from a
 * table trained offline on public-domain text (gen_langid_model.py):
 *
 *   1. UTF-8 decode, drop combining marks, fold case, collapse punctuation
 *      and digits to word breaks (first XL_MAX_CPS code points only).
 *   2. Route by Unicode script. A language alone in its script (Greek, Thai,
 *      Korean, Georgian, Tamil, ...) is answered from the script alone.
 *   3. Otherwise score hashed character 1-3-grams against each candidate's
 *      naive-Bayes cost table; the lowest total wins.
 *
 * Accuracy on held-out sentences is in docs/SPEECH_AND_TRANSLATION.md and is
 * re-checked by test_xlate.c. Short texts (a few words) are often ambiguous
 * between close languages (Danish / Norwegian, Serbian / Macedonian, ...):
 * use `reliable` before acting on a guess, and let the person override it.
 *
 * Freestanding: no libc, no allocation, no float, no 64-bit division.
 */
#ifndef ZXV_XLATE_LANGID_H
#define ZXV_XLATE_LANGID_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int32_t lang;        /* model index, or -1 when undetermined          */
    const char *tag;     /* BCP 47 tag; "und" when undetermined           */
    uint32_t confidence; /* 0..100: score margin per n-gram, or 100 when  */
                         /* the script alone decides                     */
    uint32_t letters;    /* letters (non-space code points) examined     */
    bool reliable;       /* confidence >= 30 and letters >= 12           */
} xlate_langid_result_t;

/* Identify the language of len bytes of UTF-8 (stops early at a NUL). */
void xlate_langid(const char *utf8, uint32_t len, xlate_langid_result_t *out);

uint32_t xlate_langid_count(void);
const char *xlate_langid_tag(uint32_t i);  /* "" when out of range */
const char *xlate_langid_name(uint32_t i); /* English name         */
/* Model index of a BCP 47 tag (exact, case-insensitive), or -1. */
int32_t xlate_langid_find(const char *tag);

/* Decode one UTF-8 code point at s[*i] (bounded by len), advancing *i.
 * Malformed input decodes to U+FFFD one byte at a time. */
uint32_t xlate_utf8_next(const char *s, uint32_t len, uint32_t *i);

#endif /* ZXV_XLATE_LANGID_H */
