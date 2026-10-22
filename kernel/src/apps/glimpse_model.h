/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* glimpse_model.h — Glimpse, screenshots and screen recording, as a model
 * (zxm.h): the screen's size, the capture region, the screenshot the view
 * captured, the recording and its plan. No drawing, no allocation,
 * integers only. The rules it runs are glimpse_rules.h (GR1-GR5).
 *
 *   GM1 NO CAPTURE METHOD. zxvd has no method that captures the screen, a
 *       window or a region, or records the screen, so the view captures a
 *       frame and hands it here as a PNG (glimpse_take), with the screen's
 *       size (glimpse_set_screen) and the rectangle asked for: the whole
 *       screen, a window's or a region drawn by the owner (glimpse_select).
 *   GM2 THE REGION IS CLAMPED TO THE SCREEN. Its x and y become
 *       capture_origin (GR1) and its w and h the pixels of it on the screen
 *       (hollow.shown, GR2), so x, y, w and h lie inside the width and
 *       height and none is negative. A region with nothing on the screen is
 *       refused (ZXM_E_ARG) and the one kept stays as it was. A PNG whose
 *       size (its IHDR) is not the region's is refused.
 *   GM3 SCREENSHOTS IN THE HOARD. glimpse_save asks sys.disk for the room
 *       first (GR5: nothing is written past it, ZXM_E_TOOBIG, and the
 *       notice says how many bytes are short), then lists GLIMPSE_DIR with
 *       files.list and saves the PNG as GLIMPSE_DIR "/Screenshot N.png", N
 *       the first number free there (GR4), written with files.write (base64,
 *       begin/add/end when it does not fit one request) as a new file
 *       (exclusive). Saving changes kept state, so it takes the owner's
 *       confirmation: without it nothing is sent (ZXM_E_STATE).
 *   GM4 A RECORDING ALWAYS SHOWS THAT IT IS RECORDING. glimpse_record_start
 *       starts one only with the owner's confirmation and only when its
 *       exact raw size (GR3) fits the room sys.disk reports less the
 *       owner's floor; from then until glimpse_record_stop, g->recording is
 *       true and g->indicator says GLIMPSE_REC_TEXT, which the view draws
 *       on every frame and in its notice. A recording stops by itself at
 *       its last frame (secs * fps).
 *   GM5 CUT WITH stream.plan. A finished recording's encoded units (bytes,
 *       time and keyframe of each, at most GLIMPSE_UNITS) are cut into
 *       segments by zxvd's stream.plan, and every segment it answers is
 *       checked to follow the one before and to cover the units. */
#ifndef GLIMPSE_MODEL_H
#define GLIMPSE_MODEL_H

#include "glimpse_rules.h"
#include "zxm.h"

#define GLIMPSE_APP       "glimpse"
#define GLIMPSE_DIR       "Screenshots"
#define GLIMPSE_REC_TEXT  "Recording"
#define GLIMPSE_SHOT_MAX  8388608u /* bytes of one PNG (8 MiB) */
#define GLIMPSE_UNITS     120u     /* stream.plan's units at once */
#define GLIMPSE_SEG_BYTES 215040u  /* stream.plan's default segment, 64 freights */
#define GLIMPSE_US_MAX    4294967295LL
#define GLIMPSE_NUM_MAX   999999999LL /* screenshot numbers, 9 digits */
#define GLIMPSE_PATH_MAX  48u
#define GLIMPSE_NOTICE    200u

typedef struct {
    int64_t len;    /* bytes, 0..GLIMPSE_SEG_BYTES */
    int64_t pts_us; /* 0..GLIMPSE_SECS_MAX seconds in microseconds, never going backwards */
    bool key;       /* a keyframe */
} glimpse_unit_t;

typedef struct {
    uint32_t first_unit, nunits;
    int64_t offset, len, t_start_us, dur_us;
    bool key_start;
} glimpse_seg_t;

typedef struct {
    zxm_link_t *link;
    /* GM1, GM2 */
    int64_t screen_w, screen_h; /* 0 until set */
    bool region;
    int64_t x, y, w, h;
    uint8_t shot[GLIMPSE_SHOT_MAX];
    uint32_t shot_len; /* 0 = none */
    /* GM3: the last sys.disk, the owner's floor and the last listing */
    int64_t total, free, avail, floor;
    int64_t used[GLIMPSE_USED_MAX];
    uint32_t nused;
    bool folder;
    int64_t last_n; /* the number of the last screenshot saved, 0 none */
    char last_path[GLIMPSE_PATH_MAX];
    /* GM4 */
    bool recording;
    char indicator[16]; /* GLIMPSE_REC_TEXT while recording, else "" */
    int64_t bpp, fps, secs, rec_bytes, frames, frames_max;
    /* GM5 */
    glimpse_seg_t seg[GLIMPSE_UNITS];
    uint32_t nsegs;
    char notice[GLIMPSE_NOTICE];
} glimpse_t;

void glimpse_init(glimpse_t *g, zxm_link_t *link);

/* GM1: the screen's size, 1..GLIMPSE_SIDE_MAX each; the region is cleared. */
int glimpse_set_screen(glimpse_t *g, int64_t width, int64_t height);
/* GM2: a region (x, y from -GLIMPSE_POS_MAX..GLIMPSE_POS_MAX, w and h
 * 0..GLIMPSE_POS_MAX), clamped to the screen. ZXM_E_STATE before the
 * screen is set or while recording. */
int glimpse_select(glimpse_t *g, int64_t x, int64_t y, int64_t w, int64_t h);
int glimpse_select_screen(glimpse_t *g);
/* GM1, GM2: the PNG the view captured of the region. */
int glimpse_take(glimpse_t *g, const uint8_t *png, uint32_t n);
/* GM3, GM4: the owner's floor, 0..GLIMPSE_ROOM_MAX. */
int glimpse_set_floor(glimpse_t *g, int64_t floor);
/* GM3: sys.disk into g->total, g->free and g->avail. */
int glimpse_room(glimpse_t *g);
/* GM3: files.list of GLIMPSE_DIR into g->used, and the first free N. */
int glimpse_next_number(glimpse_t *g, int64_t *n);
/* GM3: the screenshot taken, saved as the first free number. */
int glimpse_save(glimpse_t *g, bool owner_confirmed);
/* GM4 */
int glimpse_record_start(glimpse_t *g, int64_t bpp, int64_t fps, int64_t secs,
                         bool owner_confirmed);
/* GM4: one frame recorded; ZXM_E_STATE when not recording. The last frame
 * stops the recording. */
int glimpse_record_frame(glimpse_t *g);
void glimpse_record_stop(glimpse_t *g);
/* GM5: n units (1..GLIMPSE_UNITS) cut with stream.plan; ZXM_E_STATE while
 * recording. */
int glimpse_cut(glimpse_t *g, const glimpse_unit_t *u, uint32_t n, int64_t target_us,
                int64_t max_us);

#endif
