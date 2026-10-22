/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* joinery_model.h — Joinery, the code editor, as a model (zxm.h): one
 * source file of the Hoard in an edit buffer, its lines and columns, its
 * undo history, finding in files, running a Playground template on it,
 * and the swarm's suggestions held until the person accepts them. No
 * drawing, no allocation, integers only. The rules it runs are
 * joinery_rules.h (JR1-JR7).
 *
 * zxvd methods used: files.read, files.write (replace, or begin, add...,
 * end), files.rename, search.query, search.refresh, play.run and
 * swarm.ask (zxm_ai_ask).
 *
 *   JM1 A FILE. joinery_open reads a file of the Hoard whole (files.read,
 *       UTF-8, part by part) into the buffer; it opens only when JR5 holds
 *       for its size and its longest line, and nothing of a failed open is
 *       kept: the buffer before it stays. joinery_new starts an empty
 *       buffer for a path not in the Hoard yet.
 *   JM2 EDITS. joinery_edit replaces `removed` bytes at `offset` by `ins`.
 *       In order: the arguments are checked (the range inside the buffer
 *       and on character boundaries, ins strict UTF-8 with no NUL), then
 *       JR5 for the buffer after it (its size and every line it touches),
 *       then JR6 for its undo record; only then is room made in the undo
 *       history (the oldest steps dropped past JOINERY_UNDO_MAX steps or
 *       JOINERY_UNDO_BYTES bytes), the text changed and the record kept.
 *       An edit larger than the whole history clears it and is not undone.
 *       joinery_undo takes back the newest step. An edit drops a held
 *       suggestion, whose range may have moved (zxm.h M3).
 *   JM3 PLACES. A place is a line and a column (characters, 0 the first),
 *       or a byte offset: joinery_place_offset and joinery_offset_place
 *       turn one into the other through JR1-JR4, the lines handed over
 *       JOINERY_LINE_BLOCK at a time, the widths read out of the line in C
 *       (reading source text stays C: its best-fit language, Racket, has
 *       no runtime yet).
 *   JM4 SAVING, RENAMING. Each changes the Hoard, so each takes the
 *       owner's word (owner_confirmed), checked after its arguments and
 *       before anything is sent: without it ZXM_E_STATE and nothing is
 *       sent. A save writes the buffer whole (files.write replace, or
 *       begin, add..., end when it does not fit one request line; nothing
 *       appears until end, zxv_files.h F4); a new file is written exclusive,
 *       never over another. A rename (files.rename) never replaces a file.
 *   JM5 FINDING. joinery_find asks search.query (the index brought up to
 *       date first) and keeps the hits in zxvd's rank; joinery_reindex
 *       asks search.refresh. Both only read.
 *   JM6 RUNNING. joinery_run runs a Playground template with the buffer as
 *       one of its files (play.run, on scratch state, never the books); its
 *       verdict is four-valued (LPRES_STATE_*), and BOTH or NEITHER is held
 *       for the person.
 *   JM7 SUGGESTIONS. joinery_suggest asks the swarm (swarm.ask) about the
 *       bytes [start, end); the answer is held, never applied, until
 *       joinery_suggest_accept applies it as an ordinary edit (JM2, so it is
 *       undone like any other); joinery_suggest_reject drops it. Advisory:
 *       a suggestion changes the buffer only, and the Hoard only on a save
 *       the owner confirms. */
#ifndef JOINERY_MODEL_H
#define JOINERY_MODEL_H

#include "../lpres/lpres.h"
#include "joinery_rules.h"
#include "zxm.h"

#define JOINERY_PATH_MAX     1024u
#define JOINERY_UNDO_BYTES   (1u << 20) /* removed and inserted bytes the history keeps */
#define JOINERY_LINE_BLOCK   1024u      /* lines handed to JR1/JR2 at once (JM3) */
#define JOINERY_HITS         20u
#define JOINERY_SNIPPET      120u
#define JOINERY_QUERY_MAX    256u /* search.query's q */
#define JOINERY_TEMPLATE_MAX 64u
#define JOINERY_FILE_NAME    255u /* a Playground file's name */
#define JOINERY_HEADLINE     240u
#define JOINERY_NOTICE       200u

typedef struct {
    int64_t step;     /* its place in the history, 1 the oldest */
    int64_t offset;   /* where the edit was made */
    int64_t removed;  /* bytes it took away, kept at `at` */
    int64_t inserted; /* bytes it put in, kept after them */
    uint32_t at;      /* in undo_bytes */
} joinery_undo_t;

typedef struct {
    char path[JOINERY_PATH_MAX + 1];
    int64_t size;
    int64_t score;
    bool in_name;
    char snippet[JOINERY_SNIPPET + 1];
} joinery_hit_t;

enum {
    JOINERY_AI_EXPLAIN,  /* nothing replaced: an explanation to read */
    JOINERY_AI_FIX,      /* replaces the range */
    JOINERY_AI_COMPLETE, /* inserted after the range */
    JOINERY_AI_COUNT
};
extern const zxm_ai_action_t JOINERY_AI[JOINERY_AI_COUNT];

typedef struct {
    zxm_link_t *link;
    /* JM1 */
    bool open;
    bool fresh; /* not in the Hoard yet: the first save is exclusive */
    bool dirty; /* changed since it was opened or saved */
    char path[JOINERY_PATH_MAX + 1];
    char text[JOINERY_FILE_MAX + 1];
    uint32_t len;
    int64_t longest; /* the longest line's bytes, its newline aside */
    /* JM2 */
    joinery_undo_t undo[JOINERY_UNDO_MAX];
    uint32_t nundo;
    char undo_bytes[JOINERY_UNDO_BYTES];
    uint32_t undo_len;
    uint32_t undo_dropped; /* steps given up for room */
    /* JM3 scratch */
    int64_t lines[JOINERY_LINE_BLOCK];
    int64_t width[JOINERY_LINE_MAX];
    /* JM5 */
    joinery_hit_t hit[JOINERY_HITS];
    uint32_t nhits;
    int64_t total_hits;
    int64_t indexed, text_indexed; /* search.refresh's files and text_files */
    /* JM6 */
    char template_id[JOINERY_TEMPLATE_MAX + 1];
    lpres_state_t verdict;
    bool approval_required;
    char headline[JOINERY_HEADLINE + 1];
    /* JM7 */
    zxm_held_t held;
    uint32_t sel_start, sel_end;
    char notice[JOINERY_NOTICE];
} joinery_t;

void joinery_init(joinery_t *j, zxm_link_t *link);
/* JM1 */
int joinery_open(joinery_t *j, const char *path);
int joinery_new(joinery_t *j, const char *path);
/* JM2 */
int joinery_edit(joinery_t *j, uint32_t offset, uint32_t removed, const char *ins, uint32_t n);
int joinery_undo(joinery_t *j);
/* JM3: line and col count from 0; col in characters */
int joinery_place_offset(joinery_t *j, int64_t line, int64_t col, int64_t *offset);
int joinery_offset_place(joinery_t *j, int64_t offset, int64_t *line, int64_t *col);
/* JR7 of the buffer */
int64_t joinery_fill(const joinery_t *j);
/* JM4 */
int joinery_save(joinery_t *j, bool owner_confirmed);
int joinery_rename(joinery_t *j, const char *to, bool owner_confirmed);
/* JM5 */
int joinery_find(joinery_t *j, const char *query);
int joinery_reindex(joinery_t *j);
/* JM6: the buffer goes in as the template's file `name` */
int joinery_run(joinery_t *j, const char *template_id, const char *name);
/* JM7 */
int joinery_suggest(joinery_t *j, int32_t action, uint32_t start, uint32_t end);
int joinery_suggest_accept(joinery_t *j);
int joinery_suggest_reject(joinery_t *j);

#endif
