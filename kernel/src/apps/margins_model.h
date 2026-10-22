/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* margins_model.h — Margins, the notes, as a model (zxm.h): each note a
 * plain text file in the Hoard's Notes folder, listed in order (pinned
 * first, then the most recent), indexed by its #tags, found by its words,
 * renamed, put in the Trash and put back. No drawing, no allocation,
 * integers only. The rules it runs are margins_rules.h (MR1-MR7).
 *
 * zxvd methods used: files.list, files.read, files.write, files.mkdir,
 * files.rename, files.trash and search.query.
 *
 *   MM1 A NOTE IS A FILE. Notes/<title>.txt, its text UTF-8 with no
 *       control byte but newline and tab. Margins' own small files are
 *       Margins/pins.txt (one pinned title a line) and Margins/trash.txt
 *       (one line a note it put in the Trash: stamp, title and where the
 *       Trash keeps it, tab-separated). A missing folder or file is an
 *       empty one.
 *   MM2 LOADING. margins_load lists Notes (files.list): every file named
 *       <title>.txt whose title holds MR1 is a note, at most
 *       MARGINS_NOTES of them; anything else is passed over and counted
 *       (skipped). Each note within MR1's bytes is read (files.read) and
 *       its #tags indexed; a note that is not UTF-8, or past MR1, is
 *       listed but not opened (readable false). Nothing is kept from a
 *       load that fails: the notes before it stay.
 *   MM3 TAGS are read out of a note's text, in C (text work): a '#' at
 *       the start or after a space, tab or newline, then 1..MARGINS_TAG_LEN
 *       letters, digits, '_' or '-', ending at any other byte; folded to
 *       lower case, each counted once a note. Every (tag, note, mtime)
 *       holds MR4; the notes of a tag are counted by MR5.
 *   MM4 ORDER. Each note's key is MR2 of its pin and its mtime, and its
 *       place MR3; order[] lists the notes by place, a tie in file order.
 *   MM5 WRITING. A note is checked before anything is sent: its title a
 *       Hoard file name (zxv_files.h F2) inside MR1, its text MM1's and
 *       its body and tags inside MR1. A new note is written exclusive
 *       (never over another); an edit replaces the note whole (F4). A
 *       rename refuses a title already used. After each change the folder
 *       is loaded again, so what is shown is what zxvd keeps.
 *   MM6 THE TRASH. margins_trash moves a note to the Trash (files.trash)
 *       and logs when; margins_trash_left gives its days left (MR6), and
 *       a note is offered for removal when none are left. It is put back
 *       (files.rename out of the Trash) or removed for good (files.trash
 *       of its place in the Trash) only on the owner's confirmed word.
 *   MM7 FINDING. margins_search asks search.query and keeps the hits that
 *       are notes, in zxvd's rank; margins_tagged lists the notes of one
 *       tag in the list's order. */
#ifndef MARGINS_MODEL_H
#define MARGINS_MODEL_H

#include "margins_rules.h"
#include "zxm.h"

#define MARGINS_DIR       "Notes"
#define MARGINS_OWN       "Margins"
#define MARGINS_PINS      "Margins/pins.txt"
#define MARGINS_TRASHLOG  "Margins/trash.txt"
#define MARGINS_NOTES     64u  /* notes kept from one listing */
#define MARGINS_TAG_LEN   32u  /* bytes of one tag, '#' aside */
#define MARGINS_TAG_TABLE 128u /* distinct tags indexed */
#define MARGINS_TRASHED   32u  /* notes the Trash log keeps, the oldest dropped */
#define MARGINS_HITS      20u
#define MARGINS_QUERY_MAX 256u /* search.query's q */
#define MARGINS_PATH_MAX  192u
#define MARGINS_NOTICE    200u

typedef struct {
    char title[MARGINS_TITLE_MAX + 1];
    int64_t bytes;  /* files.list's size */
    int64_t mtime;  /* seconds since 1970 */
    int64_t pinned; /* 0 or 1 */
    int64_t key;    /* MR2 */
    int64_t place;  /* MR3, 1 first */
    bool readable;  /* read, UTF-8 and inside MR1 (MM2) */
    uint32_t ntags;
    uint16_t tag[MARGINS_TAGS_MAX]; /* numbers in the tag table, 1.. */
} margins_note_t;

typedef struct {
    int64_t stamp; /* seconds since 1970, when it was put there */
    char title[MARGINS_TITLE_MAX + 1];
    char trashed[MARGINS_PATH_MAX]; /* its path in the Trash */
} margins_trashed_t;

typedef struct {
    zxm_link_t *link;
    bool loaded;
    margins_note_t note[MARGINS_NOTES];
    uint32_t n;
    uint32_t skipped;              /* listed entries that are no note (MM2) */
    uint32_t order[MARGINS_NOTES]; /* note indexes by place (MM4) */
    int64_t npinned;               /* MR5 */
    char tag[MARGINS_TAG_TABLE][MARGINS_TAG_LEN + 1];
    uint32_t ntags;
    uint32_t tags_dropped;                /* tags past the table, not indexed */
    int64_t tag_notes[MARGINS_TAG_TABLE]; /* MR5 */
    margins_trashed_t trash[MARGINS_TRASHED];
    uint32_t ntrash;
    /* the open note */
    int32_t open; /* its index, -1 none */
    char text[MARGINS_BODY_MAX + 1];
    uint32_t len;
    /* MM7 */
    uint32_t hit[MARGINS_HITS]; /* note indexes */
    uint32_t nhits;
    char notice[MARGINS_NOTICE];
} margins_t;

void margins_init(margins_t *m, zxm_link_t *link);
/* MM2-MM4 */
int margins_load(margins_t *m);
/* the note titled `title`, read into text (MM2); ZXM_E_STATE when it
 * cannot be opened here */
int margins_open(margins_t *m, const char *title);
/* MM5: a new note (fresh) or the whole text of one that exists */
int margins_save(margins_t *m, const char *title, const char *text, uint32_t len, bool fresh);
int margins_rename(margins_t *m, const char *from, const char *to);
/* MM4: pinned 1 or 0 */
int margins_pin(margins_t *m, const char *title, int64_t pinned);
/* MM6: now is seconds since 1970 */
int margins_trash(margins_t *m, const char *title, int64_t now);
int64_t margins_trash_left(const margins_t *m, uint32_t i, int64_t now);
uint32_t margins_trash_due(const margins_t *m, int64_t now);
int margins_restore(margins_t *m, uint32_t i, bool confirmed);
int margins_forget(margins_t *m, uint32_t i, bool confirmed);
/* MM7 */
int margins_search(margins_t *m, const char *query);
int margins_tagged(margins_t *m, const char *tag);
/* the index of the note titled `title`, or -1 */
int32_t margins_find(const margins_t *m, const char *title);
/* MR7 of the open note, MARGINS_REFUSED when none is open */
int64_t margins_fill(const margins_t *m);

#endif
