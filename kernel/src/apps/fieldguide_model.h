/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fieldguide_model.h — Fieldguide, the offline encyclopedia and nature
 * guide, as a model (zxm.h): articles in the Hoard found by search.query,
 * read with files.read, ranked, and cautioned; an identification key
 * walked one question at a time. No drawing, no allocation, integers only.
 * The rules it runs are fieldguide_rules.h (FR1-FR4) and Assay's
 * score_rank (assay_rules.h AY3).
 *
 * zxvd methods used: search.refresh, search.query and files.read. Fieldguide
 * changes nothing in zxvd and in the Hoard.
 *
 *   FM1 ARTICLES. An article is a .txt file under FIELDGUIDE_DIR/ (but not
 *       under FIELDGUIDE_KEYS/). Its head, up to the first empty line, is
 *       header lines "field: value": name (the scientific name), common
 *       (the English name), taxon (GBIF key, 1..999999999), kind (plant,
 *       fungus, animal; anything else is other), toxic and dangerous (yes
 *       or no). A field missing or not understood is not known; nothing
 *       is guessed. The model reads no edibility from any article.
 *   FM2 THE MARK (FR1). An article marks its species 1 when its toxic or
 *       dangerous line says yes; 0 when one of them says no and neither
 *       says yes; -1 when neither line is there or says yes or no.
 *   FM3 SEARCH. fieldguide_search asks search.query (1..8 words,
 *       FIELDGUIDE_QUERY_MAX bytes of UTF-8 with no control byte; the
 *       index brought up to date first unless fieldguide_refresh already
 *       did) for up to FIELDGUIDE_HITS hits, keeps the articles (FM1),
 *       reads the head of each (at most FIELDGUIDE_HEAD bytes) and keeps
 *       it only when its record fits FR4. A refused search keeps the
 *       matches before it.
 *   FM4 RANKING. Each match's order key is FR3 of its search score and
 *       whether its taxon is the one the key reached; its rank is
 *       assay.score_rank of the keys, and the matches are kept best first
 *       (ties in the search's order).
 *   FM5 THE CAUTION. The level is FR1 over the matches' marks. caution
 *       is lpres_kjoin of what the articles say of their species (1 TRUE,
 *       0 FALSE, -1 nothing): BOTH when one article marks a species and
 *       another says it is not toxic or dangerous. The notice for every
 *       level ends with FIELDGUIDE_NEVER_SAFE, and no notice says
 *       anything is safe.
 *   FM6 THE KEY. fieldguide_key_load reads a key file of couplets, one a
 *       line: "<n>|<question>|<yes lead>|<no lead>", n counting 1, 2, ...
 *       in order (at most FIELDGUIDE_COUPLETS); a lead is "c<n>" (go to
 *       couplet n, which must exist), "t<taxon>" (the species reached) or
 *       "-" (the key ends). The walk starts at couplet 1;
 *       fieldguide_key_answer takes one answer (FR2) and moves on: to the
 *       next couplet, to a taxon (reached), or stops (not sure, or the
 *       key ends). A walk longer than the key has couplets is a loop and
 *       is refused (ZXM_E_STATE); fieldguide_key_back steps back one
 *       question. */
#ifndef FIELDGUIDE_MODEL_H
#define FIELDGUIDE_MODEL_H

#include "assay_rules.h"
#include "fieldguide_rules.h"
#include "lpres.h"
#include "zxm.h"

#define FIELDGUIDE_DIR       "Fieldguide"
#define FIELDGUIDE_KEYS      "Fieldguide/keys"
#define FIELDGUIDE_HITS      20u   /* hits asked of search.query */
#define FIELDGUIDE_QUERY_MAX 256u  /* search.query's q */
#define FIELDGUIDE_PATH_MAX  256u  /* one Hoard path, with its NUL */
#define FIELDGUIDE_HEAD      2048u /* bytes of an article's head read */
#define FIELDGUIDE_NAME_MAX  96u   /* a scientific or English name, with its NUL */
#define FIELDGUIDE_COUPLETS  64u   /* couplets in one key */
#define FIELDGUIDE_QUESTION  160u  /* one question, with its NUL */
#define FIELDGUIDE_KEY_READ  16384u
#define FIELDGUIDE_NOTICE    320u

#define FIELDGUIDE_NEVER_SAFE                                                                      \
    "Fieldguide never says a plant or fungus is safe to eat; do not eat anything on its word."

typedef struct {
    char path[FIELDGUIDE_PATH_MAX];
    char name[FIELDGUIDE_NAME_MAX];
    char common[FIELDGUIDE_NAME_MAX];
    int64_t taxon; /* GBIF key, 0 not known */
    int64_t kind;  /* FIELDGUIDE_KIND_* */
    int64_t mark;  /* FM2 */
    int64_t mtime;
    int64_t score; /* search.query's */
    int64_t key;   /* FR3 */
    int64_t rank;  /* AY3, 1 best */
} fieldguide_article_t;

typedef struct {
    char question[FIELDGUIDE_QUESTION];
    int64_t yes, no; /* FR2 leads */
} fieldguide_couplet_t;

typedef enum {
    FIELDGUIDE_WALK_NONE = 0, /* no key loaded */
    FIELDGUIDE_WALK_ASKING,   /* a question waits for its answer */
    FIELDGUIDE_WALK_REACHED,  /* a taxon is reached */
    FIELDGUIDE_WALK_STOPPED   /* not sure, or the key ends */
} fieldguide_walk_t;

typedef struct {
    zxm_link_t *link;
    bool refreshed;
    int64_t indexed; /* text files in the index, after a refresh */
    /* FM3, FM4 */
    fieldguide_article_t match[FIELDGUIDE_HITS];
    uint32_t n;
    /* FM5 */
    int64_t caution_level;
    lpres_state_t caution;
    char notice[FIELDGUIDE_NOTICE];
    /* FM6 */
    fieldguide_couplet_t couplet[FIELDGUIDE_COUPLETS];
    uint32_t ncouplets;
    fieldguide_walk_t walk;
    uint32_t at;                         /* the couplet asked, 1.. */
    uint32_t trail[FIELDGUIDE_COUPLETS]; /* couplets asked before it */
    uint32_t steps;
    int64_t reached; /* the taxon reached, 0 none */
} fieldguide_t;

void fieldguide_init(fieldguide_t *g, zxm_link_t *link);
/* FM3 */
int fieldguide_refresh(fieldguide_t *g);
int fieldguide_search(fieldguide_t *g, const char *query);
/* FM1, FM2: parse an article's head (n bytes) into a (name, common,
 * taxon, kind, mark); false when it has no header at all */
bool fieldguide_parse_head(const char *text, uint32_t n, fieldguide_article_t *a);
/* FM4: rank g's matches again (after the key reached a taxon) */
int fieldguide_rank(fieldguide_t *g);
/* FM5: the caution's notice for a level and state */
void fieldguide_caution_notice(int64_t level, lpres_state_t state, char *out, uint32_t cap);
/* FM6 */
int fieldguide_key_load(fieldguide_t *g, const char *path);
const char *fieldguide_key_question(const fieldguide_t *g);
int fieldguide_key_answer(fieldguide_t *g, int64_t answer);
int fieldguide_key_back(fieldguide_t *g);

#endif
