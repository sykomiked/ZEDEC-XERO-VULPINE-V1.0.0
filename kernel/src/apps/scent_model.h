/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* scent_model.h — Scent, search, as a model (zxm.h): finds files in a space
 * by name and by the words in them (zxvd's search.query, which keeps the
 * index up to date), says which app opens each hit, and can ask AI for a
 * sharper search. Read only: Scent never changes a file, and the AI's
 * suggestion is held until the owner runs it (M3). No drawing, no
 * allocation. */
#ifndef SCENT_MODEL_H
#define SCENT_MODEL_H

#include "burrow_model.h" /* burrow_kind: which app opens a hit */
#include "zxm.h"

#define SCENT_HITS    50u
#define SCENT_QUERY   256u
#define SCENT_PATH    1024u
#define SCENT_SNIPPET 120u

typedef struct {
    char path[SCENT_PATH + 1];
    uint64_t size;
    int64_t mtime;
    bool in_name; /* every word is in the file's name */
    uint32_t score;
    char snippet[SCENT_SNIPPET + 1]; /* the text around the first hit, or "" */
} scent_hit_t;

typedef struct {
    zxm_link_t *link;
    char space[49]; /* "" = home */
    char query[SCENT_QUERY + 1];
    scent_hit_t hit[SCENT_HITS];
    uint32_t n;     /* hits shown, best first */
    uint32_t total; /* hits found */
    int32_t sel;    /* the selected hit, -1 for none */
    zxm_held_t held;
} scent_t;

/* The app that opens a hit. */
enum { SCENT_OPEN_QUILLON, SCENT_OPEN_GLINT, SCENT_OPEN_BURROW };

enum { SCENT_AI_SHARPEN, SCENT_AI_COUNT };
extern const zxm_ai_action_t SCENT_AI[SCENT_AI_COUNT];

void scent_init(scent_t *s, zxm_link_t *link);
/* The space to search ("" or "home" = home); clears the hits. */
int scent_set_space(scent_t *s, const char *space);
/* Search as the owner types. An empty or blank query clears the hits
 * without asking zxvd. Nothing is kept from a failed search. */
int scent_search(scent_t *s, const char *query);
/* Rebuild the index now (search.refresh); *files = how many it holds. */
int scent_reindex(scent_t *s, uint32_t *files);

int scent_select(scent_t *s, int32_t i); /* -1 clears */
const scent_hit_t *scent_selected(const scent_t *s);
/* Which app opens it, and the folder holding it ("" for the top). */
int scent_opener(const scent_hit_t *h);
void scent_folder(const scent_hit_t *h, char *out, uint32_t cap);

/* AI: suggest a sharper search from what was found (held, M3; the owner
 * runs it with scent_ai_accept, which searches for the suggestion). */
int scent_ai(scent_t *s, int32_t action);
int scent_ai_accept(scent_t *s);
int scent_ai_reject(scent_t *s);

#endif
