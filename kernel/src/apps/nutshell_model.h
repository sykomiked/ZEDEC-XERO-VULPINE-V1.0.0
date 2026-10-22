/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* nutshell_model.h — Nutshell, flashcards, as a model (zxm.h): decks of
 * cards kept in the Banker, one file per deck, read with files.read and
 * written with files.write; each card in a Leitner box (up one on a right
 * answer, back to 1 on a wrong one), shown again on its due day. No
 * drawing, no allocation, integers only. The rules are nutshell_rules.h
 * (NS1-NS5).
 *
 * Days are days since 1970-01-01 in UTC, 0 .. SOLSTICE_DAY_MAX
 * (9999-12-31): zxvd has no time-zone method, so the view gives today's
 * day in the owner's zone, as Bowline's does.
 *
 *   NM1 FILE. A deck is NUTSHELL_DIR/<deck>.deck, deck 1 ..
 *       NUTSHELL_DECK_MAX in plain decimal digits: "nutshell 1\n", then
 *       "<made> <reviewed>\n", then one line per card, "<box> <due> <right>
 *       <wrong>\t<front>\t<back>\n", numbers in plain decimal digits with no
 *       leading zero. A file that does not say this is not read as a deck,
 *       and it is never written over.
 *   NM2 VALID. A card is in box 1 .. NUTSHELL_BOX_MAX, due 0 ..
 *       SOLSTICE_DAY_MAX, with right and wrong answers 0 ..
 *       NUTSHELL_TALLY_MAX each; its front and back are 1 ..
 *       NUTSHELL_SIDE_MAX bytes, none a control byte (so no tab or line
 *       break). A deck holds at most NUTSHELL_CARDS cards; made and
 *       reviewed are days, reviewed 0 while it was never reviewed. The
 *       fields fit their ^ZXNUTSHELL and ^ZXNUTCARD records (NS4, NS5).
 *   NM3 REVIEW. A card answered on `today` moves to its next box (NS1) and
 *       is due on its due day (NS2); a due day past 9999-12-31 is refused,
 *       and the card stays as it was.
 *   NM4 OWNER. Every change to a deck (a card added, a card answered) takes
 *       owner_confirmed, checked after the arguments and before anything
 *       is asked of zxvd: without it the call is ZXM_E_STATE, nothing is
 *       sent and nothing changes.
 *   NM5 CHECKED FIRST. Every change is checked, then composed in full, and
 *       kept only after zxvd has written it: a refused write leaves the
 *       model as it was. Nothing is written before a deck is opened, and a
 *       new deck's file is written with exclusive set, so no deck is ever
 *       written over by another. */
#ifndef NUTSHELL_MODEL_H
#define NUTSHELL_MODEL_H

#include "nutshell_rules.h"
#include "zxm.h"

#define NUTSHELL_DIR       "Nutshell" /* in the Hoard's home */
#define NUTSHELL_DECK_MAX  999999     /* six digits (^ZXNUTSHELL) */
#define NUTSHELL_CARDS     64u        /* cards in one deck */
#define NUTSHELL_SIDE_MAX  48u        /* bytes on one side of a card */
#define NUTSHELL_TALLY_MAX 999999     /* six digits (^ZXNUTCARD) */
#define NUTSHELL_TEXT_MAX  8192u      /* one deck file; escaped, it fits ZXM_REQ_MAX */
#define NUTSHELL_E_FILES   (-32014)   /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */

typedef struct {
    int64_t box;   /* 1 .. NUTSHELL_BOX_MAX */
    int64_t due;   /* the day it is shown again */
    int64_t right; /* answers right so far */
    int64_t wrong; /* answers wrong so far */
    char front[NUTSHELL_SIDE_MAX + 1];
    char back[NUTSHELL_SIDE_MAX + 1];
} nutshell_card_t;

typedef struct {
    zxm_link_t *link;
    bool loaded;      /* a deck was opened: it may be written (NM5) */
    bool exists;      /* its file is in the Banker */
    int64_t deck;     /* 1 .. NUTSHELL_DECK_MAX */
    int64_t made;     /* the day the deck was made */
    int64_t reviewed; /* the day it was last reviewed, 0 never */
    nutshell_card_t c[NUTSHELL_CARDS];
    uint32_t n;
} nutshell_t;

/* NM2: may a card or a side say this? */
bool nutshell_card_valid(int64_t box, int64_t due, int64_t right, int64_t wrong);
bool nutshell_side_ok(const char *side);

void nutshell_init(nutshell_t *s, zxm_link_t *link);
/* Read deck `deck` (files.read); no file yet is a new, empty deck (ZXM_OK,
 * s->exists false). A file that is not a deck is ZXM_E_STATE: nothing is
 * kept and it is never written over (NM1). */
int nutshell_open(nutshell_t *s, int64_t deck);
/* A new card in box 1, due on `today` (NM2, NM4, NM5). */
int nutshell_add_card(nutshell_t *s, const char *front, const char *back, int64_t today,
                      bool owner_confirmed);
/* Card `index` answered on `today`, right 1 or wrong 0 (NM3, NM4, NM5). */
int nutshell_review(nutshell_t *s, uint32_t index, int64_t right, int64_t today,
                    bool owner_confirmed);

/* How many cards are due on `today` (NS3). */
int nutshell_due_total(const nutshell_t *s, int64_t today, uint32_t *count);
/* The card to show next on `today`: the one due longest, the lower box
 * first on the same day, then the first in the deck; ZXM_E_STATE when no
 * card is due. */
int nutshell_next_card(const nutshell_t *s, int64_t today, uint32_t *index);
/* How many cards each box holds, box b in counts[b - 1] (Witness's count
 * of a kind). */
int nutshell_box_counts(const nutshell_t *s, int64_t counts[NUTSHELL_BOX_MAX]);

#endif
