/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fieldguide_rules.h — Fieldguide's rules in C, the reference its KRON,
 * Trebago, BASIC (Hatchling) and MYRA blocks are checked against
 * (blocks/fieldguide). Pure integer functions, no state: the model
 * (fieldguide_model.c) and the language test (tests/fieldguide_zxapp.inc)
 * both include this, so the languages are tested against the very C the
 * model runs.
 *
 *   FR1 THE CAUTION (species_caution) over the articles a search matched,
 *       one mark each: 1 the article marks the species toxic or dangerous,
 *       0 the article carries toxicity information and marks neither, -1
 *       the article carries no toxicity information. The level is 2
 *       (caution: a matched species is marked toxic or dangerous) when any
 *       mark is 1; else 1 (toxicity unknown) when any mark is -1; else 0
 *       (none marked). 0 never means safe: the guide never says a plant or
 *       fungus is safe to eat. No article at all is 1: nothing is known. A
 *       mark outside -1..1 refuses the whole set (Trebago divides by zero;
 *       KRON reads a mark above 1 as 1 and below -1 as -1: held).
 *   FR2 THE NEXT LEAD OF AN IDENTIFICATION KEY (key_next_lead), one
 *       question at a time: a couplet asks one question and has two leads,
 *       yes and no; a lead is a couplet number (above 0), a taxon reached
 *       (its GBIF key, negated) or 0 (the key ends there). The answer is 1
 *       yes, 0 no or -1 not sure; not sure is 0, since the key never
 *       guesses. An answer outside -1..1 or a lead past 999999999 either
 *       way is refused (BASIC refuses, KRON reads the answer as clamped
 *       and any lead as it is: held).
 *   FR3 THE MATCH ORDER KEY (match_order_key) of each article a search
 *       found: keyed * 4294967296 + score, where score is search.query's
 *       score (0..4294967295, zxv_search.h's uint32) and keyed is 1 when
 *       the article is the species the identification key reached, else
 *       0, so the keyed species ranks first and the search's order holds
 *       among the rest. A row outside refuses the set (Trebago divides by
 *       zero; KRON reads keyed above 0 as 1 and any score as it is: held).
 *       The ranks are assay.score_rank of the keys (best first, ties
 *       sharing the higher place).
 *   FR4 AN ARTICLE'S INDEX RECORD (article_row_ok) fits
 *       ^ZXFIELDGUIDE(article) = taxon ^ kind ^ mark ^ mtime: article (its
 *       place in the index) at most 4 digits, taxon (GBIF key, 0 not
 *       known) 9, kind 1 (FIELDGUIDE_KIND_*), mark 1 (FR1) and mtime
 *       (seconds since 1970) 12, sign aside (MYRA's i-fields).
 *
 * Anything else is refused (FIELDGUIDE_REFUSED), never rounded or wrapped. */
#ifndef FIELDGUIDE_RULES_H
#define FIELDGUIDE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define FIELDGUIDE_REFUSED   INT64_MIN
#define FIELDGUIDE_LEAD_MAX  INT64_C(999999999)  /* FR2 */
#define FIELDGUIDE_SCORE_MAX INT64_C(4294967295) /* FR3: search.query's uint32 */
#define FIELDGUIDE_KEYED     INT64_C(4294967296) /* FR3: above every score */

enum { /* FR1: an article's mark */
       FIELDGUIDE_MARK_UNKNOWN = -1,
       FIELDGUIDE_MARK_NONE = 0,
       FIELDGUIDE_MARK_TOXIC = 1
};

enum { /* FR1: the caution's level */
       FIELDGUIDE_CAUTION_NONE = 0,
       FIELDGUIDE_CAUTION_UNKNOWN = 1,
       FIELDGUIDE_CAUTION_MARKED = 2
};

enum { /* FR2: an answer to a couplet's question */
       FIELDGUIDE_ANSWER_UNSURE = -1,
       FIELDGUIDE_ANSWER_NO = 0,
       FIELDGUIDE_ANSWER_YES = 1
};

enum { /* FR4: what an article is about */
       FIELDGUIDE_KIND_OTHER = 0,
       FIELDGUIDE_KIND_PLANT = 1,
       FIELDGUIDE_KIND_FUNGUS = 2,
       FIELDGUIDE_KIND_ANIMAL = 3
};

/* FR1 */
static inline int64_t fieldguide_caution(const int64_t *mark, uint32_t n)
{
    int64_t level = FIELDGUIDE_CAUTION_NONE;
    if (!n) return FIELDGUIDE_CAUTION_UNKNOWN;
    if (!mark) return FIELDGUIDE_REFUSED;
    for (uint32_t i = 0; i < n; i++)
        if (mark[i] < FIELDGUIDE_MARK_UNKNOWN || mark[i] > FIELDGUIDE_MARK_TOXIC)
            return FIELDGUIDE_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (mark[i] == FIELDGUIDE_MARK_TOXIC) return FIELDGUIDE_CAUTION_MARKED;
        if (mark[i] == FIELDGUIDE_MARK_UNKNOWN) level = FIELDGUIDE_CAUTION_UNKNOWN;
    }
    return level;
}

/* FR2 */
static inline int64_t fieldguide_key_next(int64_t answer, int64_t yes, int64_t no)
{
    if (answer < FIELDGUIDE_ANSWER_UNSURE || answer > FIELDGUIDE_ANSWER_YES)
        return FIELDGUIDE_REFUSED;
    if (yes < -FIELDGUIDE_LEAD_MAX || yes > FIELDGUIDE_LEAD_MAX) return FIELDGUIDE_REFUSED;
    if (no < -FIELDGUIDE_LEAD_MAX || no > FIELDGUIDE_LEAD_MAX) return FIELDGUIDE_REFUSED;
    if (answer == FIELDGUIDE_ANSWER_YES) return yes;
    if (answer == FIELDGUIDE_ANSWER_NO) return no;
    return 0;
}

/* FR3 */
static inline int64_t fieldguide_match_key(int64_t score, int64_t keyed)
{
    if (score < 0 || score > FIELDGUIDE_SCORE_MAX || keyed < 0 || keyed > 1)
        return FIELDGUIDE_REFUSED;
    return keyed * FIELDGUIDE_KEYED + score;
}

/* FR4: v fits a field of max (all nines), sign aside */
static inline bool fieldguide_digits_ok(int64_t v, int64_t max)
{
    return v >= -max && v <= max;
}

static inline bool fieldguide_article_row_ok(int64_t article, int64_t taxon, int64_t kind,
                                             int64_t mark, int64_t mtime)
{
    return fieldguide_digits_ok(article, INT64_C(9999)) &&
           fieldguide_digits_ok(taxon, INT64_C(999999999)) && fieldguide_digits_ok(kind, 9) &&
           fieldguide_digits_ok(mark, 9) && fieldguide_digits_ok(mtime, INT64_C(999999999999));
}

#endif
