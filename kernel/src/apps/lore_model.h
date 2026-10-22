/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* lore_model.h — Lore, your knowledge base, as a model (zxm.h): the
 * owner's notes and files in a space (files.list, files.read), found by
 * name and by the words in them (search.query, search.refresh), and read
 * by the companion (swarm.ask) only with the owner's leave. No drawing, no
 * allocation, every string bounded.
 *
 *   LO1 THE SPACE. Lore works in one space at a time; its kind (home,
 *       office, team, cloudtop, park, from spaces.get) orders it from the
 *       most private to the most open (lore_rules.h).
 *   LO2 LEAVES ARE THE OWNER'S. A leave names one folder ("" is the whole
 *       space), the most private kind of space it reaches, when it was
 *       given and when it expires. Lore never gives one by itself: only
 *       lore_leave_give, on the owner's word, adds one, and only
 *       lore_leave_revoke takes one back. A leave that does not fit its
 *       ^ZXLORE record (lore_rules.h LR3, LR4) is refused before it is
 *       kept. zxvd has no method to keep it without fs.write, so leaves
 *       live in the model for the session.
 *   LO3 MAY THE AI READ THIS FILE (lore_rules.h LR1), for a file of the
 *       space in use at `now`: some leave is live (not revoked, given at
 *       or before now, expires after now), grants the file's folder (the
 *       file is the leave's folder or under it) and reaches the space (the
 *       space's kind minus the leave's reach is 0..4). The first leave that
 *       says yes is the answer; otherwise the reason of the nearest miss.
 *   LO4 NOTHING IS SENT BEFORE IT IS CHECKED. lore_ask checks LO3 first
 *       and asks zxvd nothing when it is no; only then is the file read
 *       (files.read) and handed to the companion with the owner's
 *       question. The answer is held (zxm.h M3) until the owner accepts or
 *       rejects it; accepting changes no file.
 *   LO5 THE OWNER READS WITHOUT A LEAVE. Listing, opening and searching
 *       are the owner's own reads; each search hit is marked with whether
 *       the companion may read it (LO3), as advice for the view. */
#ifndef LORE_MODEL_H
#define LORE_MODEL_H

#include "lore_rules.h"
#include "zxm.h"

#define LORE_LEAVES   16u
#define LORE_PATH     1024u
#define LORE_NAME     255u
#define LORE_ENTRIES  64u
#define LORE_HITS     20u
#define LORE_QUERY    256u
#define LORE_SNIPPET  120u
#define LORE_TEXT_MAX 8192u /* bytes of a file the owner opens */
#define LORE_ASK_TEXT 3000u /* bytes of a file handed to the companion */

/* LO3: why the companion may or may not read a file */
enum {
    LORE_READ_OK,
    LORE_NO_LEAVE,    /* no leave at all, or every one revoked */
    LORE_LAPSED,      /* a leave grants it, but it has expired or not begun */
    LORE_NOT_GRANTED, /* no live leave grants the file's folder */
    LORE_TOO_PRIVATE, /* the space is more private than any live granting leave reaches */
};

typedef struct {
    bool used;
    bool revoked;
    char folder[LORE_PATH + 1]; /* "" = the whole space */
    int64_t reach;              /* the most private kind it reaches, 0..4 */
    int64_t given, expires;     /* seconds since 1970 */
} lore_leave_t;

typedef struct {
    char name[LORE_NAME + 1];
    bool dir;
    uint64_t size;
    int64_t mtime;
} lore_entry_t;

typedef struct {
    char path[LORE_PATH + 1];
    uint64_t size;
    int64_t mtime;
    uint32_t score;
    char snippet[LORE_SNIPPET + 1];
    bool ai_ok; /* LO5: the companion may read it now */
} lore_hit_t;

typedef struct {
    zxm_link_t *link;
    char space[ZXM_SPACE_MAX + 1]; /* "" = home */
    int64_t kind;                  /* LO1: 0 home .. 4 park */
    lore_leave_t leave[LORE_LEAVES];
    char folder[LORE_PATH + 1]; /* the folder listed */
    lore_entry_t entry[LORE_ENTRIES];
    uint32_t nentries;
    char query[LORE_QUERY + 1];
    lore_hit_t hit[LORE_HITS];
    uint32_t nhits, total;
    char open_path[LORE_PATH + 1];
    char text[LORE_TEXT_MAX + 1]; /* the file the owner opened */
    uint32_t text_len;
    bool text_cut; /* more of it than LORE_TEXT_MAX */
    char asked_path[LORE_PATH + 1];
    zxm_held_t held; /* LO4: the companion's answer, until the owner's word */
} lore_t;

enum { LORE_AI_ASK, LORE_AI_COUNT };
extern const zxm_ai_action_t LORE_AI[LORE_AI_COUNT];

void lore_init(lore_t *l, zxm_link_t *link);
/* LO1: work in this space ("" or "home" = home); its kind from spaces.get.
 * Clears the listing, the hits and the opened file. */
int lore_set_space(lore_t *l, const char *space);
/* The space kind's number for a name zxvd gives (home..park), or -1. */
int64_t lore_kind_of(const char *name);

/* LO5: the owner's own reads */
int lore_list(lore_t *l, const char *folder);
int lore_open(lore_t *l, const char *path);
int lore_search(lore_t *l, const char *query, int64_t now);
int lore_reindex(lore_t *l, uint32_t *files);

/* LO2: the owner's leaves. give: *idx = the leave's number; ZXM_E_ARG for a
 * leave that does not fit (LR4) or a folder that is no path; ZXM_E_STATE
 * when all LORE_LEAVES are in use. */
int lore_leave_give(lore_t *l, const char *folder, int64_t reach, int64_t given, int64_t expires,
                    uint32_t *idx);
int lore_leave_revoke(lore_t *l, uint32_t idx);
/* LR2: whole days left on a leave at now, never below 0; LORE_REFUSED for
 * no such leave or a time outside Lockbox's stamps. */
int64_t lore_leave_days(const lore_t *l, uint32_t idx, int64_t now);
/* How many segments deep a folder is ("" 0, "a" 1, "a/b" 2). */
int64_t lore_depth(const char *folder);
/* Whether path is folder or lies under it. */
bool lore_under(const char *path, const char *folder);

/* LO3: LORE_READ_OK or why not, for a file of the space in use */
int lore_may_read(const lore_t *l, const char *path, int64_t now);
const char *lore_why(int reason);

/* LO4: ask the companion about one file; ZXM_E_STATE, and nothing sent,
 * when it may not read it (*reason says why, if given). */
int lore_ask(lore_t *l, const char *path, const char *question, int64_t now, int *reason);
int lore_ai_accept(lore_t *l);
int lore_ai_reject(lore_t *l);

#endif
