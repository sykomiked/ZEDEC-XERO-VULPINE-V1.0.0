/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* warble_model.h — Warble, the music library and player, as a model
 * (zxm.h): the Hoard's Music folder as a library, a playlist with its
 * exact length, a shuffle, the listening volume under the owner's cap, and
 * playing together in a watch hall. The rules are warble_rules.h (WR1-WR5).
 *
 *   WM1 THE LIBRARY is the Hoard's WARBLE_DIR folder (files.list): every
 *       file whose name ends in a known audio extension (.wav, .flac,
 *       .mp3, .ogg, .opus, .m4a, .aac; any case), in the listing's order,
 *       at most WARBLE_TRACKS. A track is kept only when its index record
 *       fits (WR4); any other file, a folder, or a track past the table is
 *       counted in skipped.
 *   WM2 LENGTHS. A WAV track's length is read from its canonical 44-byte
 *       header (files.read, base64): "RIFF", "WAVE", "fmt ", the byte rate
 *       at byte 28 and "data" at 36 with its size at 40; ms is
 *       floor(size * 1000 / byte rate). Any other format, a header that is
 *       not canonical, or a length past a day is 0: not known (zxvd has no
 *       method that decodes audio). warble_restat looks at one track again
 *       (files.stat) and reads its length again.
 *   WM3 FINDING: search.query; the hits that are library tracks, in the
 *       order zxvd ranks them, at most WARBLE_HITS.
 *   WM4 THE PLAYLIST holds up to WARBLE_PLAYLIST_MAX library tracks in
 *       order (one track may be there more than once). Its length is
 *       playlist_ms (WR2) of the tracks' lengths; tracks of unknown length
 *       count 0 and are counted apart. Shown as h:mm:ss by halloo.hms.
 *   WM5 THE SHUFFLE: with a seed, the playlist plays in shuffle_at's order
 *       (WR1), place i playing the playlist's track shuffle_at(seed, n, i);
 *       without, in the playlist's own order. A change to the playlist
 *       leaves the shuffle (its order no longer holds).
 *   WM6 THE VOLUME and the owner's cap (WR3), 0..100 percent, kept for the
 *       view: zxvd has no method that plays sound or sets a volume.
 *       Advisory and owner-confirmed: the cap moves only with the owner's
 *       confirmed word (ZXM_E_STATE without it); lowering it below the
 *       volume brings the volume down to it, said in the notice; a volume
 *       past the cap is refused (ZXM_E_ARG), never turned down silently.
 *   WM7 THE WATCH HALL: a hall's space CID (pspace.create's), then
 *       hall.load of a track's media CID (the owner publishes it; zxvd's
 *       fab.put copies at most a 2 MiB block, so Warble does not make one),
 *       hall.play, hall.pause and hall.status, each answer kept in hall.
 *       zxvd's pspace switch is the owner's; its refusals are shown as is.
 *
 * Nothing is checked after it is sent: every argument is checked first,
 * and a refused call leaves the model as it was. No drawing, no
 * allocation, integers only. */
#ifndef WARBLE_MODEL_H
#define WARBLE_MODEL_H

#include "warble_rules.h"
#include "zxm.h"

#define WARBLE_DIR        "Music"
#define WARBLE_NAME_MAX   255u /* a file name in the Hoard (zxv_files.h) */
#define WARBLE_PATH_MAX   (6u + WARBLE_NAME_MAX)
#define WARBLE_HITS       64u
#define WARBLE_CID_MAX    79u /* a space CID (hall.*'s cid) */
#define WARBLE_MEDIA_MAX  64u /* hall.load's media */
#define WARBLE_CAP_START  85  /* WM6: the cap until the owner sets one */
#define WARBLE_VOL_START  50
#define WARBLE_RATE_UNIT  65536 /* hall rate_q16: normal speed */
#define WARBLE_RATE_MAX   262144
#define WARBLE_NOTICE_MAX 255u

enum { WARBLE_HALL_NONE = 0, WARBLE_HALL_STOP, WARBLE_HALL_PAUSE, WARBLE_HALL_PLAY };

typedef struct {
    char name[WARBLE_NAME_MAX + 1u];
    int64_t bytes;
    int64_t mtime; /* seconds since 1970 */
    int64_t ms;    /* WM2: 0 when not known */
} warble_track_t;

typedef struct {
    bool set;
    int32_t state; /* WARBLE_HALL_* */
    char media[WARBLE_MEDIA_MAX + 1u];
    int64_t pos_ms;
    int64_t rate_q16;
    int64_t viewers;
    bool may_control;
} warble_hall_t;

typedef struct {
    zxm_link_t *link;
    bool loaded;
    warble_track_t track[WARBLE_TRACKS];
    uint32_t n;
    uint32_t skipped;
    uint32_t hit[WARBLE_HITS]; /* WM3: library indexes */
    uint32_t nhits;
    uint32_t list[WARBLE_PLAYLIST_MAX]; /* WM4: library indexes, in order */
    uint32_t nlist;
    bool shuffled; /* WM5 */
    int64_t seed;
    uint32_t place; /* the place playing, 0..nlist - 1 */
    int64_t volume, cap;
    char hall_cid[WARBLE_CID_MAX + 1u];
    warble_hall_t hall;
    char notice[WARBLE_NOTICE_MAX + 1u];
} warble_t;

void warble_init(warble_t *w, zxm_link_t *link);
/* WM1, WM2: the library; a Music folder not made yet is an empty library. */
int warble_load(warble_t *w);
/* WM2: one track looked at again (files.stat) and its length read again. */
int warble_restat(warble_t *w, uint32_t track);
/* WM3 */
int warble_search(warble_t *w, const char *query);
int32_t warble_find(const warble_t *w, const char *name);

/* WM4 */
int warble_queue(warble_t *w, uint32_t track);
int warble_unqueue(warble_t *w, uint32_t place);
/* The playlist's exact length in ms and how many of its tracks have no
 * known length; ZXM_E_STATE when WR2 refuses it. */
int warble_length(const warble_t *w, int64_t *ms, uint32_t *unknown);
/* That length as "h:mm:ss" (halloo.hms) into out (cap bytes). */
int warble_length_text(const warble_t *w, char *out, uint32_t cap);

/* WM5: the library index of the track at place i of the play order, or -1. */
int32_t warble_at(const warble_t *w, uint32_t place);
int warble_shuffle(warble_t *w, int64_t seed);
void warble_unshuffle(warble_t *w);
/* The next track of the play order: its library index, -1 at the end. */
int32_t warble_next(warble_t *w);

/* WM6 */
int warble_set_cap(warble_t *w, int64_t cap, bool owner_confirmed);
int warble_set_volume(warble_t *w, int64_t volume);

/* WM7: pos_ms -1 is "where every listener is"; rate_q16 0 is normal speed. */
int warble_hall_set(warble_t *w, const char *cid);
int warble_hall_load(warble_t *w, const char *media, int64_t pos_ms);
int warble_hall_play(warble_t *w, int64_t pos_ms, int64_t rate_q16);
int warble_hall_pause(warble_t *w, int64_t pos_ms);
int warble_hall_status(warble_t *w);

#endif
