/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* quillon_model.h — Quillon, the text editor, as a model (zxm.h): one
 * UTF-8 document, where it is saved in the ZXV folder, whether it changed,
 * its counts, and AI help that is held until the owner accepts it. No
 * drawing, no allocation: the view owns the cursor and the screen. */
#ifndef QUILLON_MODEL_H
#define QUILLON_MODEL_H

#include "zxm.h"

#define QUILLON_TEXT_MAX (1u << 20) /* bytes in one document */
#define QUILLON_PATH_MAX 1024u

typedef struct {
    uint32_t words, chars, lines; /* chars = Unicode code points */
} quillon_counts_t;

typedef struct {
    zxm_link_t *link;
    char path[QUILLON_PATH_MAX + 1]; /* "" = not saved yet */
    char text[QUILLON_TEXT_MAX + 1];
    uint32_t len;
    uint64_t saved_hash; /* of the text as last opened or saved */
    uint32_t saved_len;
    quillon_counts_t counts;
    zxm_held_t held;             /* AI text waiting for accept or reject (M3) */
    uint32_t sel_start, sel_end; /* the range an AI answer replaces */
} quillon_t;

enum {
    QUILLON_AI_SUMMARIZE, /* inserted after the selection */
    QUILLON_AI_REWRITE,   /* replaces the selection */
    QUILLON_AI_CONTINUE,  /* inserted after the selection */
    QUILLON_AI_PROOFREAD, /* replaces the selection */
    QUILLON_AI_COUNT
};
extern const zxm_ai_action_t QUILLON_AI[QUILLON_AI_COUNT];

void quillon_new(quillon_t *q, zxm_link_t *link);
int quillon_open(quillon_t *q, const char *path); /* refuses a file that is not UTF-8 text */
int quillon_save(quillon_t *q);                   /* to q->path; ZXM_E_STATE when there is none */
int quillon_save_as(quillon_t *q, const char *path);
bool quillon_dirty(const quillon_t *q);

/* Edits from the view. Byte offsets, on character boundaries; the text
 * stays valid UTF-8 or the edit is refused (ZXM_E_ARG). */
int quillon_set_text(quillon_t *q, const char *s, uint32_t n);
int quillon_replace(quillon_t *q, uint32_t start, uint32_t end, const char *s, uint32_t n);

/* AI on the bytes [start, end) (the whole text when start == end). The
 * answer is held; accept applies it (replace or insert, by action), reject
 * drops it. An edit made while an answer is held drops the answer, since
 * the range it was for may have moved. */
int quillon_ai(quillon_t *q, int32_t action, uint32_t start, uint32_t end);
int quillon_ai_accept(quillon_t *q);
int quillon_ai_reject(quillon_t *q);

#endif
