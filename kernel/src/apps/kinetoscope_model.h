/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* kinetoscope_model.h — Kinetoscope, the video player, as a model (zxm.h):
 * a video of the Hoard read with files.read, cut for streaming with
 * stream.plan and stream.segment, fitted to a window, and watched together
 * in a watch hall (hall.*). The rules are kinetoscope_rules.h (KR1-KR7).
 *
 *   KM1 A VIDEO is an IVF file in the Hoard (files.read, base64): its
 *       32-byte header ("DKIF", version 0, header size 32, the codec
 *       "VP80" or "VP90", width and height 1..65535, the time base
 *       rate / scale, each 1..999999, and the frame count). Its frame
 *       rate is rate / scale frames a second (fnum, fden for KR3), its
 *       length floor(frames * scale * 1000 / rate) ms. Any other file is
 *       refused (ZXM_E_STATE, said in the notice), and the video open
 *       before it is kept.
 *   KM2 THE INDEX reads the frames' 12-byte headers (size, then the
 *       timestamp in time-base ticks) and each frame's first byte, one
 *       files.read each, a window of up to KINETOSCOPE_UNITS frames at a
 *       time, from where the last window ended. A frame's time is
 *       floor(ts * scale * 1000000 / rate) us; it is a keyframe when its
 *       first byte says so (VP8: bit 0 clear; VP9: frame marker 2, not a
 *       shown-again frame, frame type 0). A frame of 0 bytes or past
 *       KINETOSCOPE_SEG_BYTES, a time that goes backwards, or a file cut
 *       short is refused, and the index is left as it was.
 *   KM3 THE PLAN cuts the window with zxvd's own rule (stream.plan): a
 *       segment closes before the first keyframe at least target_us after
 *       its start, or once max_us have passed, or before it would pass
 *       KINETOSCOPE_SEG_BYTES. The last frame lasts one frame's time
 *       (tail_us). The segments that start on a keyframe are the places a
 *       viewer can start: counted by witness.kind_count (KR6).
 *   KM4 A SEGMENT is described by stream.segment: its frames' bytes read
 *       back (files.read) and sent as hex, answered with its CID and the
 *       freights that carry it. KINETOSCOPE_SEG_BYTES is what fits
 *       zxvd's 16384-byte request line as hex.
 *   KM5 THE FIT: the picture in a window of winw by winh, its aspect kept
 *       (video_fit_w, video_fit_h: KR1, KR2), with the bars before it,
 *       floor((side - fit) / 2), the odd pixel after the picture.
 *   KM6 THE FRAME at a position (frame_at_pos, KR3), and the frame every
 *       viewer of the hall is on.
 *   KM7 THE WATCH HALL: a hall's space CID (pspace.create's), then
 *       hall.load of the media CID the owner publishes, hall.play (at a
 *       rate KR4 allows), hall.pause, hall.seek and hall.status, each
 *       answer kept in hall. Advisory and owner-confirmed: load, play,
 *       pause and seek change what every viewer sees, so each takes the
 *       owner's confirmed word (ZXM_E_STATE without it, nothing sent).
 *       zxvd's pspace switch and the hall's owner are zxvd's; their
 *       refusals are shown as they are.
 *   KM8 RESUME: where each video was watched to, kept in the model's
 *       table of KINETOSCOPE_RESUME videos by path (KR5's record, video
 *       its place in the table); the position must be inside the video's
 *       length. Nothing is written to the Hoard: Kinetoscope needs only
 *       fs.read.
 *
 * zxvd has no method that decodes or shows video on this device, and none
 * that gives a Hoard file's media CID past fab.put's 2 MiB block: the view
 * draws the frames, and the owner publishes the media CID the hall loads.
 * Every argument is checked before anything is sent, and a refused call
 * leaves the model as it was. No drawing, no allocation, integers only. */
#ifndef KINETOSCOPE_MODEL_H
#define KINETOSCOPE_MODEL_H

#include "kinetoscope_rules.h"
#include "zxm.h"

#define KINETOSCOPE_PATH_MAX   1024u /* files.read's path */
#define KINETOSCOPE_UNITS      120u  /* stream.plan's units in one request */
#define KINETOSCOPE_SEG_BYTES  8000u /* KM4: a segment's bytes as hex in one request */
#define KINETOSCOPE_RESUME     32u
#define KINETOSCOPE_CID_MAX    79u /* a space CID (hall.*'s cid) */
#define KINETOSCOPE_MEDIA_MAX  64u /* hall.load's media */
#define KINETOSCOPE_SEGCID_MAX 79u
#define KINETOSCOPE_NOTICE_MAX 255u
#define KINETOSCOPE_IVF_HDR    32u
#define KINETOSCOPE_FRAME_HDR  12u

enum { KINETOSCOPE_VP8 = 1, KINETOSCOPE_VP9 = 2 };
enum {
    KINETOSCOPE_HALL_NONE = 0,
    KINETOSCOPE_HALL_STOP,
    KINETOSCOPE_HALL_PAUSE,
    KINETOSCOPE_HALL_PLAY
};

typedef struct {
    int64_t file_off; /* where its bytes start in the file */
    int64_t len;
    int64_t pts_us;
    bool key;
} kinetoscope_unit_t;

typedef struct {
    uint32_t first_unit, nunits; /* in the window */
    int64_t offset, len;         /* in the window's bytes, as stream.plan says */
    int64_t t_start_us, dur_us;
    bool key_start;
    bool described; /* KM4 */
    char cid[KINETOSCOPE_SEGCID_MAX + 1u];
    int64_t nfreights;
} kinetoscope_seg_t;

typedef struct {
    bool set;
    int32_t state; /* KINETOSCOPE_HALL_* */
    char media[KINETOSCOPE_MEDIA_MAX + 1u];
    int64_t pos_ms;
    int64_t rate_q16;
    int64_t viewers;
    bool may_control;
} kinetoscope_hall_t;

typedef struct {
    bool used;
    char path[KINETOSCOPE_PATH_MAX + 1u];
    int64_t pos_ms, length_ms, stamp;
} kinetoscope_resume_t;

typedef struct {
    int64_t x, y, w, h; /* KM5: the picture's place in the window */
} kinetoscope_rect_t;

typedef struct {
    zxm_link_t *link;
    /* KM1 */
    bool open;
    char path[KINETOSCOPE_PATH_MAX + 1u];
    int32_t codec;
    int64_t width, height;
    int64_t fnum, fden; /* frames a second, fnum / fden */
    int64_t frames, length_ms, size;
    /* KM2 */
    int64_t next_off;    /* the next frame header's place in the file */
    int64_t first_frame; /* the window's first frame in the file */
    int64_t read_frames; /* frames indexed so far, every window */
    bool at_end;
    kinetoscope_unit_t unit[KINETOSCOPE_UNITS];
    uint32_t nunits;
    /* KM3, KM4 */
    kinetoscope_seg_t seg[KINETOSCOPE_UNITS];
    uint32_t nsegs;
    int64_t seek_points;
    /* KM7 */
    char hall_cid[KINETOSCOPE_CID_MAX + 1u];
    kinetoscope_hall_t hall;
    /* KM8 */
    kinetoscope_resume_t resume[KINETOSCOPE_RESUME];
    char notice[KINETOSCOPE_NOTICE_MAX + 1u];
} kinetoscope_t;

void kinetoscope_init(kinetoscope_t *k, zxm_link_t *link);
/* KM1: open a video by its path in the Hoard */
int kinetoscope_open(kinetoscope_t *k, const char *path);
/* KM2: the next window of frames; ZXM_E_STATE at the end of the video */
int kinetoscope_index(kinetoscope_t *k);
/* KM3 */
int kinetoscope_plan(kinetoscope_t *k, int64_t target_us, int64_t max_us);
/* KM4: segment seg of the plan, numbered seq in stream stream_id */
int kinetoscope_segment(kinetoscope_t *k, uint32_t seg, uint64_t stream_id, uint32_t seq);
/* KM5 */
int kinetoscope_fit(const kinetoscope_t *k, int64_t winw, int64_t winh, kinetoscope_rect_t *r);
/* KM6: the frame shown at pos_ms, or KINETOSCOPE_REFUSED */
int64_t kinetoscope_frame(const kinetoscope_t *k, int64_t pos_ms);
int64_t kinetoscope_hall_frame(const kinetoscope_t *k);
/* KR7: ms as "h:mm:ss" into out (cap bytes) */
int kinetoscope_time_text(int64_t ms, char *out, uint32_t cap);

/* KM7: pos_ms -1 is "where every viewer is"; rate_q16 0 is the hall's own */
int kinetoscope_hall_set(kinetoscope_t *k, const char *cid);
int kinetoscope_hall_load(kinetoscope_t *k, const char *media, int64_t pos_ms,
                          bool owner_confirmed);
int kinetoscope_hall_play(kinetoscope_t *k, int64_t pos_ms, int64_t rate_q16, bool owner_confirmed);
int kinetoscope_hall_pause(kinetoscope_t *k, int64_t pos_ms, bool owner_confirmed);
int kinetoscope_hall_seek(kinetoscope_t *k, int64_t pos_ms, bool owner_confirmed);
int kinetoscope_hall_status(kinetoscope_t *k);

/* KM8: the open video's place kept at pos_ms (stamp: seconds since 1970) */
int kinetoscope_resume_keep(kinetoscope_t *k, int64_t pos_ms, int64_t stamp);
/* where the open video was left: ZXM_E_STATE when it has no record */
int kinetoscope_resume_find(const kinetoscope_t *k, int64_t *pos_ms);
int kinetoscope_resume_forget(kinetoscope_t *k);

#endif
