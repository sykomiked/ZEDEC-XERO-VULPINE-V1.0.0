/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* quarry_rules.h — Quarry's rules in C, the reference its Eve, KRON and
 * MYRA blocks are checked against (blocks/quarry), with the rule it uses
 * from nearer the core. Pure integer functions, no state: the model
 * (quarry_model.c) and the language test (tests/quarry_zxapp.inc) both
 * include this, so the languages are tested against the very C the model
 * runs.
 *
 *   QR1 A CLAIM HAS A SOURCE (claim_sourced): the citation number the
 *       claim names is 1..9999 and exactly one held citation carries it
 *       (held, counted by QR4 over the citations' numbers). A claim that
 *       names none (0), a number past four digits, a citation not held
 *       (held 0) or a number two citations carry (held 2: which one is
 *       meant?) has no source.
 *   QR2 SUMMARY LENGTH (summary_length_ok): a summary asked of the
 *       companion is 25..500 words long and draws on 1..32 sources, each
 *       held to its own range.
 *   QR3 CITATION RECORD (cite_record_ok) fits ^ZXQUARRY: cite (its
 *       number) at most 4 digits, topic 3, kind 1 (1 a Hoard file, 2 a
 *       Web4 envelope), stamp 12 (seconds since 1970) and quote (the
 *       bytes of its excerpt) 5, sign aside (witness_rules.h WR3).
 *   QR4 SOURCES PER TOPIC: how many held citations are of one topic, and
 *       how many carry one number, are witness.kind_count
 *       (witness_rules.h WR2) over the topic and number columns. */
#ifndef QUARRY_RULES_H
#define QUARRY_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "witness_rules.h"

#define QUARRY_CITE_MAX    INT64_C(9999) /* QR1, QR3: 4 digits */
#define QUARRY_TOPIC_MAX   INT64_C(999)  /* QR3: 3 digits */
#define QUARRY_WORDS_MIN   25            /* QR2 */
#define QUARRY_WORDS_MAX   500           /* QR2 */
#define QUARRY_SOURCES_MAX 32            /* QR2 */
#define QUARRY_KIND_FILE   1             /* QR3: a Hoard file */
#define QUARRY_KIND_WEB4   2             /* QR3: a Web4 envelope */

/* QR1 */
static inline bool quarry_sourced(int64_t cite, int64_t held)
{
    return cite >= 1 && cite <= QUARRY_CITE_MAX && held == 1;
}

/* QR2 */
static inline bool quarry_length_ok(int64_t words, int64_t sources)
{
    return words >= QUARRY_WORDS_MIN && words <= QUARRY_WORDS_MAX && sources >= 1 &&
           sources <= QUARRY_SOURCES_MAX;
}

/* QR3 */
static inline bool quarry_record_ok(int64_t cite, int64_t topic, int64_t kind, int64_t stamp,
                                    int64_t quote)
{
    return witness_digits_fit(cite, 4) && witness_digits_fit(topic, 3) &&
           witness_digits_fit(kind, 1) && witness_digits_fit(stamp, 12) &&
           witness_digits_fit(quote, 5);
}

/* QR4: the citations among n whose column holds `want` */
static inline int64_t quarry_count(const int64_t *column, uint32_t n, int64_t want)
{
    return witness_kind_count(column, n, want);
}

#endif
