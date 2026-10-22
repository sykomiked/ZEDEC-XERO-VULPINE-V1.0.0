/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fireglow_model.h — Fireglow, podcasts and audiobooks, as a model
 * (zxm.h): shows and books in the Hoard, where each episode is heard to,
 * saved, the time left at the playing speed, the chapter playing, and
 * each show's share of the listening. The rules are fireglow_rules.h
 * (FR1-FR6).
 *
 *   FM1 THE LIBRARY. Every folder of the Hoard's FIREGLOW_PODCASTS folder
 *       is a show, and every folder of FIREGLOW_BOOKS a book (files.list),
 *       podcasts first, at most FIREGLOW_SHOWS in all. Every file of a
 *       show whose name ends in a known audio extension (.mp3, .m4a,
 *       .m4b, .aac, .ogg, .opus, .flac, .wav; any case) is an episode, in
 *       the listing's order, at most FIREGLOW_EPISODES in all. A name
 *       longer than FIREGLOW_NAME_MAX, a show or episode past its table, a
 *       file beside the shows and a folder inside a show are counted in
 *       skipped; a show's other files (its saved places, chapter lists,
 *       notes) are its own and not counted. A root folder not made yet
 *       holds nothing.
 *   FM2 SAVED PLACES. A show's FIREGLOW_SAVED file (files.read, text):
 *       one line per episode, "<episode name>\t<pos>\t<length>\t<heard>",
 *       in ms. A line naming no episode of that show, with a number that
 *       is not one, whose record does not fit (FR5) or whose pos is past a
 *       known length is counted in unsaved and left out; a file past
 *       FIREGLOW_FILE_MAX is left out whole (one unsaved). fireglow_save
 *       writes the show's file again (files.write, replace) from the
 *       model, every episode with a place.
 *   FM3 CHAPTERS. An episode's "<episode name>FIREGLOW_CHAPTER_EXT" file
 *       (files.read, text): one chapter start in ms per line, ascending,
 *       the first 0, at most FIREGLOW_CHAPTERS. No such file is no
 *       chapters; a list that is not that (or longer than
 *       FIREGLOW_FILE_MAX) is refused (ZXM_E_STATE, said in the notice)
 *       and the chapters read before are kept. The chapter playing is
 *       chapter_at_pos (FR3) at the episode's place.
 *   FM4 PLAYING. zxvd has no method that plays or decodes audio, so the
 *       view plays an episode and reports where it is: pos and the length
 *       (0 not known yet), and the ms listened since its last report.
 *       Checked before anything changes: the record must fit (FR5) and
 *       pos be no later than a known length (ZXM_E_ARG otherwise).
 *   FM5 THE SPEED, a q16 multiple of the normal speed (FIREGLOW_RATE_UNIT
 *       to start), set only inside its limits (FR4; ZXM_E_ARG otherwise,
 *       never brought inside silently). The time left is FR1 at that
 *       speed, shown floored, and as h:mm:ss through halloo.hms (FR6).
 *   FM6 LISTENING SHARES. Each show's minutes listened (the floor of its
 *       episodes' heard ms over a minute) as its share of the minutes
 *       listened in all: banker.share (FR2), in the shows' order.
 *
 * Nothing is checked after it is sent: every argument is checked first,
 * and a refused call leaves the model as it was. No drawing, no
 * allocation, integers only. */
#ifndef FIREGLOW_MODEL_H
#define FIREGLOW_MODEL_H

#include "fireglow_rules.h"
#include "zxm.h"

#define FIREGLOW_PODCASTS    "Podcasts"
#define FIREGLOW_BOOKS       "Audiobooks"
#define FIREGLOW_SAVED       "fireglow.tsv"
#define FIREGLOW_CHAPTER_EXT ".chapters"
#define FIREGLOW_NAME_MAX    63u
#define FIREGLOW_PATH_MAX    (12u + 2u * FIREGLOW_NAME_MAX + 12u)
#define FIREGLOW_FILE_MAX    12000u /* FM2: a saved file, inside one files.write */
#define FIREGLOW_NOTICE_MAX  255u

enum { FIREGLOW_PODCAST = 0, FIREGLOW_BOOK = 1 };

typedef struct {
    char name[FIREGLOW_NAME_MAX + 1u];
    int32_t kind; /* FIREGLOW_PODCAST or FIREGLOW_BOOK */
} fireglow_show_t;

typedef struct {
    char name[FIREGLOW_NAME_MAX + 1u];
    uint32_t show;
    int64_t pos;    /* ms heard to */
    int64_t length; /* ms; 0 not known yet */
    int64_t heard;  /* ms listened in all */
} fireglow_episode_t;

typedef struct {
    zxm_link_t *link;
    bool loaded;
    fireglow_show_t show[FIREGLOW_SHOWS];
    uint32_t nshows;
    fireglow_episode_t ep[FIREGLOW_EPISODES];
    uint32_t n;
    uint32_t skipped; /* FM1 */
    uint32_t unsaved; /* FM2 */
    int64_t rate;     /* FM5 */
    int64_t starts[FIREGLOW_CHAPTERS];
    uint32_t nstarts;
    int32_t chapters_of; /* FM3: the episode the starts are of, -1 none */
    char notice[FIREGLOW_NOTICE_MAX + 1u];
} fireglow_t;

void fireglow_init(fireglow_t *f, zxm_link_t *link);
/* FM1, FM2 */
int fireglow_load(fireglow_t *f);
int32_t fireglow_find_show(const fireglow_t *f, const char *name);
int32_t fireglow_find(const fireglow_t *f, uint32_t show, const char *name);
/* FM2: the show's places written again */
int fireglow_save(fireglow_t *f, uint32_t show);
/* FM3: the episode's chapters read; then the chapter at its place, or -1
 * when its chapters are not the ones read. */
int fireglow_chapters(fireglow_t *f, uint32_t ep);
int64_t fireglow_chapter(const fireglow_t *f, uint32_t ep);
/* FM4 */
int fireglow_heard(fireglow_t *f, uint32_t ep, int64_t pos, int64_t length, int64_t listened);
/* FM5 */
int fireglow_set_speed(fireglow_t *f, int64_t rate);
int fireglow_left(const fireglow_t *f, uint32_t ep, int64_t *ms);
int fireglow_left_text(const fireglow_t *f, uint32_t ep, char *out, uint32_t cap);
/* FM6: one share per show into share (cap at least nshows) */
int fireglow_shares(const fireglow_t *f, int64_t *share, uint32_t cap);

#endif
