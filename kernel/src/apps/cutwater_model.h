/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cutwater_model.h — Cutwater, the video editor, as a model (zxm.h): sources
 * from the Hoard (files.read), their cut points checked with zxvd's own cut
 * (stream.plan), an edit list of clips on tracks, and the list saved as
 * text (files.write) and opened again. The rules are cutwater_rules.h
 * (CR1-CR4).
 *
 *   CM1 A SOURCE is a video of the Hoard read by Kinetoscope's model
 *       (kinetoscope_open: an IVF file of VP8 or VP9, its size, frame rate
 *       and frame count), up to CUTWATER_SOURCES of them. The first source
 *       sets the edit list's rate; a source at another rate is refused,
 *       since nothing here converts one (ZXM_E_STATE, said in the notice).
 *       So is a rate whose timecode has no place (CR2) and a source of
 *       more than CUTWATER_FRAME_MAX frames.
 *   CM2 CUT POINTS. A clip may only start on a keyframe, where its frames
 *       can be taken as they are. The source's frames are indexed a window
 *       at a time (kinetoscope_index, files.read) and each window cut with
 *       stream.plan at a target of one microsecond (kinetoscope_plan), so
 *       zxvd's rule starts a segment at every keyframe: the segments that
 *       start on a keyframe are the cut points. The first
 *       CUTWATER_SCAN_FRAMES frames are scanned; a refusal on the way
 *       leaves the source's cut points as they were.
 *   CM3 THE EDIT LIST: up to CUTWATER_CLIPS clips, each (track 1..8,
 *       source, in, out, place), kept in order of track then place. A clip
 *       is added only when its source's cut points are scanned, its in is
 *       one of them, the clip is valid (CR1: in before out, inside the
 *       source, room up to the next clip on its track) and the clip before
 *       it on its track still has room up to it. A refused clip leaves the
 *       list as it was, the reason in the notice.
 *   CM4 TIMECODE: a frame of the list as "h:mm:ss:ff" (CR2), the list's
 *       length (its last frame on any track) and the frame a playhead in
 *       ms falls in (CR4, kinetoscope.frame_at_pos).
 *   CM5 SAVE: the list as text, "cutwater 1", "rate <fnum> <fden>", a line
 *       "source <path>" for each source and "clip <track> <source> <in>
 *       <out> <place>" for each clip, every clip's record checked (CR3)
 *       first, written whole with files.write. It changes the Hoard, so it
 *       takes the owner's confirmed word, checked after the arguments:
 *       without it nothing is sent (ZXM_E_STATE).
 *   CM6 OPEN: a saved list read with files.read (at most
 *       CUTWATER_TEXT_MAX bytes) and built again from its sources: each
 *       one read and scanned (CM1, CM2), each clip added (CM3), the rate
 *       line matching the sources'. Any refusal leaves the open list as it
 *       was.
 *
 * zxvd has no method that renders, encodes or joins video, so a finished
 * video is not made here: the edit list is the product, and its clips
 * start on keyframes so their frames can be taken as they are. Every
 * argument is checked before anything is sent. No allocation, integers
 * only. */
#ifndef CUTWATER_MODEL_H
#define CUTWATER_MODEL_H

#include "cutwater_rules.h"
#include "kinetoscope_model.h"
#include "zxm.h"

#define CUTWATER_SOURCES     8u
#define CUTWATER_TRACKS      8
#define CUTWATER_CLIPS       64u
#define CUTWATER_PATH_MAX    255u
#define CUTWATER_SCAN_FRAMES 2400u /* CM2: twenty windows of KINETOSCOPE_UNITS */
#define CUTWATER_TEXT_MAX    8192u /* CM5, CM6 */
#define CUTWATER_NOTICE_MAX  255u
#define CUTWATER_TC_TEXT     24u /* "277777:46:39:000" and its end */

typedef struct {
    char path[CUTWATER_PATH_MAX + 1u];
    int64_t frames; /* the header's frame count */
    int64_t width, height;
    bool scanned;           /* CM2 */
    int64_t scanned_frames; /* frames whose cut points are known */
    int64_t cut_points;
    uint8_t key[CUTWATER_SCAN_FRAMES / 8u]; /* bit f: frame f is a cut point */
} cutwater_source_t;

typedef struct {
    int64_t track;  /* 1..CUTWATER_TRACKS */
    int64_t source; /* 1..nsources */
    int64_t cutin, cutout, place;
} cutwater_clip_t;

typedef struct {
    zxm_link_t *link;
    kinetoscope_t kine; /* CM1, CM2: the source being read */
    int64_t fnum, fden; /* the list's rate, set by its first source */
    cutwater_source_t src[CUTWATER_SOURCES];
    uint32_t nsources;
    cutwater_clip_t clip[CUTWATER_CLIPS];
    uint32_t nclips;
    char notice[CUTWATER_NOTICE_MAX + 1u];
} cutwater_t;

void cutwater_init(cutwater_t *c, zxm_link_t *link);
/* CM1: a source by its path; *source is its number (1 the first) */
int cutwater_source_add(cutwater_t *c, const char *path, int64_t *source);
/* CM2 */
int cutwater_scan(cutwater_t *c, int64_t source);
bool cutwater_is_cut_point(const cutwater_t *c, int64_t source, int64_t frame);
/* CM3 */
int cutwater_clip_add(cutwater_t *c, int64_t track, int64_t source, int64_t cutin, int64_t cutout,
                      int64_t place);
int cutwater_clip_remove(cutwater_t *c, uint32_t clip);
/* CM4 */
int64_t cutwater_length(const cutwater_t *c);
int cutwater_timecode_text(const cutwater_t *c, int64_t frame, char *out, uint32_t cap);
int64_t cutwater_playhead_frame(const cutwater_t *c, int64_t ms);
/* CM5, CM6 */
int cutwater_save(cutwater_t *c, const char *path, bool owner_confirmed);
int cutwater_open(cutwater_t *c, const char *path);

#endif
