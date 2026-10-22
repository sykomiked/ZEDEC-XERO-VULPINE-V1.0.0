/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* nightjar_model.h — Nightjar, the screen reader, as a model (zxm.h): what
 * is on the screen and where focus is, put into words and queued for the
 * voice, with alerts and focus changes cutting in on page reading. The
 * rules are nightjar_rules.h (NR1-NR4), the same ones the app's Eve, KRON
 * and MYRA blocks run. No drawing, no allocation, integers only.
 *
 *   NJ1 THE SCREEN. zxvd has no accessibility-tree method, so the view
 *       hands in the screen's elements (zui_a11y_text's widgets): a role
 *       0..9 (zui_widget.h zui_kind_t), a name of 1..NIGHTJAR_NAME_MAX
 *       bytes with no control characters, a row and column
 *       0..NIGHTJAR_GRID_MAX and a state 0..7 (NIGHTJAR_SELECTED,
 *       _DIMMED, _CHECKED). Each must make a ^ZXNIGHTJAR record (NR3).
 *       At most NIGHTJAR_ELEMENTS; full is refused (ZXM_E_STATE).
 *   NJ2 READING ORDER is by row, then column (NR4); elements at one spot
 *       are read in the order they were handed in.
 *   NJ3 WORDS. An element is said as its name, its role and its state
 *       words, then "k of n" in reading order. In simple mode (ui.get's
 *       simple_mode, read by nightjar_refresh) the role and the place are
 *       left out; the state is always said.
 *   NJ4 THE QUEUE is a bounded ring of NIGHTJAR_QUEUE utterances (head:
 *       how many were ever put in, tail: how many were taken; full when
 *       head - tail >= NIGHTJAR_QUEUE, refused, never overwritten). An
 *       utterance that cuts in (NR1: an alert or a change of focus) drops
 *       the page reading still waiting and sets `cut`, so the view stops
 *       the voice at once; one that does not waits behind the rest. A call
 *       that queues several utterances queues all of them or none.
 *   NJ5 LANGUAGE. Each utterance carries the language xlate.detect gives
 *       its text, so the voice speaks it in that language; "und" when zxvd
 *       cannot tell (refused or undetermined).
 *   NJ6 ALERTS. nightjar_refresh reads notes.list: each unread notice
 *       newer than the last seen becomes an utterance, an alert that cuts
 *       in when its priority is high or urgent (zx_notify.h ZXN_PRI_HIGH
 *       and above), otherwise one that waits. Nightjar changes nothing in
 *       zxvd: notices are left unread.
 *   NJ7 WHERE AM I. nightjar_where says the focused element and its place
 *       and the machine's name (sys.info host); it cuts in as a focus
 *       change does.
 *   NJ8 VOICE. The rate and pitch are set only inside NR2 and only on the
 *       owner's word (ZXM_E_STATE without it, nothing changed). zxvd has
 *       no speech method (kernel/src/speech is not on RPC), so the voice
 *       is the view's: the model gives it each utterance, its language,
 *       the rate and pitch, and how long the utterance takes at that rate. */
#ifndef NIGHTJAR_MODEL_H
#define NIGHTJAR_MODEL_H

#include "nightjar_rules.h"
#include "zxm.h"

#define NIGHTJAR_ELEMENTS    64u
#define NIGHTJAR_NAME_MAX    95u /* NR3: namelen fits 2 digits */
#define NIGHTJAR_ROLES       10  /* zui_kind_t: ZUI_W_BOX .. ZUI_W_SEPARATOR */
#define NIGHTJAR_STATE_MAX   7
#define NIGHTJAR_GRID_MAX    INT64_C(9999) /* NR3: a row or column fits 4 digits */
#define NIGHTJAR_QUEUE       32u
#define NIGHTJAR_SAY_MAX     255u
#define NIGHTJAR_TAG_MAX     15u /* a BCP 47 tag from xlate.detect */
#define NIGHTJAR_HOST_MAX    63u
#define NIGHTJAR_SOURCE      23u /* zx_notify.h ZXN_SOURCE_MAX - 1 */
#define NIGHTJAR_TITLE       63u /* ZXN_TITLE_MAX - 1 */
#define NIGHTJAR_NOTES       20u /* notes.list answers the newest 20 */
#define NIGHTJAR_PRI_HIGH    2u  /* zx_notify.h ZXN_PRI_HIGH */
#define NIGHTJAR_PRI_MAX     3u  /* ZXN_PRI_URGENT */
#define NIGHTJAR_WPM_START   180 /* defaults taken: an everyday rate */
#define NIGHTJAR_PITCH_START 100

enum { NIGHTJAR_SELECTED = 1, NIGHTJAR_DIMMED = 2, NIGHTJAR_CHECKED = 4 };

typedef struct {
    int64_t role, row, column, state;
    char name[NIGHTJAR_NAME_MAX + 1];
} nightjar_element_t;

typedef struct {
    int64_t kind;    /* NR1: NIGHTJAR_ALERT, _FOCUS or _PAGE */
    int32_t element; /* the element it speaks of; -1 none */
    char text[NIGHTJAR_SAY_MAX + 1];
    char lang[NIGHTJAR_TAG_MAX + 1]; /* NJ5 */
} nightjar_say_t;

typedef struct {
    zxm_link_t *link;
    nightjar_element_t el[NIGHTJAR_ELEMENTS];
    uint32_t nel;
    int32_t focus; /* -1: nothing has focus */
    int64_t wpm, pitch;
    bool simple; /* NJ3 */
    nightjar_say_t queue[NIGHTJAR_QUEUE];
    uint32_t head, tail; /* NJ4 */
    bool cut;            /* a cut-in since the last nightjar_next */
    uint32_t dropped;    /* page utterances dropped by cut-ins */
    uint32_t last_seq;   /* NJ6: the newest notice seen */
    char host[NIGHTJAR_HOST_MAX + 1];
} nightjar_t;

void nightjar_init(nightjar_t *n, zxm_link_t *link);

/* NJ1: *index is the element's number (0..). */
int nightjar_add_element(nightjar_t *n, int64_t role, const char *name, int64_t row, int64_t column,
                         int64_t state, uint32_t *index);
/* The screen changed: its elements, focus and waiting page reading go. */
void nightjar_clear(nightjar_t *n);
/* NJ2: the elements in reading order into order (cap), *count of them. */
int nightjar_order(const nightjar_t *n, uint32_t *order, uint32_t cap, uint32_t *count);
/* NJ3: element i in words into out (cap); its length, or -1. */
int32_t nightjar_describe(const nightjar_t *n, uint32_t i, char *out, uint32_t cap);

/* NJ3, NJ6: ui.get, then notes.list. *queued: utterances added. */
int nightjar_refresh(nightjar_t *n, uint32_t *queued);
/* NJ2, NJ4: the whole screen, in reading order, waiting its turn. */
int nightjar_read_page(nightjar_t *n, uint32_t *queued);
/* NJ4: focus moves to element i, and says so, cutting in. */
int nightjar_focus(nightjar_t *n, uint32_t i);
/* NJ7 */
int nightjar_where(nightjar_t *n);
/* NJ4: the next utterance to speak; ZXM_E_STATE when none waits. */
int nightjar_next(nightjar_t *n, nightjar_say_t *out);
uint32_t nightjar_waiting(const nightjar_t *n);

/* NJ8 */
int nightjar_set_voice(nightjar_t *n, int64_t wpm, int64_t pitch, bool owner_confirmed);
/* Milliseconds `text` takes at wpm words a minute, rounded up; -1 when
 * wpm is outside NR2 or text is NULL. */
int64_t nightjar_speak_ms(const char *text, int64_t wpm);

#endif
