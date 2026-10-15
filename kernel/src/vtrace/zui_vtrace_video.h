/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_vtrace_video.h — a film to a vector film (ZXVM), and playing one back.
 *
 * Every frame the zui_video iterator gives (Y4M, Motion-JPEG AVI/MP4, raw
 * RGB AVI, animated GIF, or whatever a platform adapter decodes) is traced
 * by zvt_trace (zui_vtrace.h) into a ZXVG frame. A film is mostly the same
 * picture again, so the frames are stored as:
 *   keyframe  the frame's palette and all its shapes;
 *   delta     only the shapes not already in this group of pictures (GOP):
 *             a shape whose bytes (its outline relative to its own start
 *             point, and its paint) are already in the GOP's table is a
 *             reference to it plus where it now starts, so an unchanged
 *             shape costs a few bytes and a shape that only moved does too.
 * Within a GOP the palette is the keyframe's (zvt_extra_t.pal), so the same
 * region gets the same paint index and the same bytes from frame to frame.
 * A keyframe starts a GOP every `gop` frames, at a scene change (more than
 * scene_pct % of the pixels differ from the previous frame by more than
 * scene_thr in some channel, counted by the backend's diff stage), and when
 * the GOP's shape table would overflow. Gradients are not used in films (a
 * gradient lives in the ZXVG header, which is per GOP here).
 *
 * The player rebuilds each frame's ZXVG byte for byte (it is the very ZXVG
 * zvt_trace wrote for that frame) and draws it with zui_vec at any size.
 *
 * ZXVM v1 (all integers little endian; uv / zz as in ZXVG):
 *   header (96 bytes)
 *     0  "ZXVM"  4 u8 version (1)  5 u8 flags (bit 0: the CID is of decoded
 *        pixels, not of a file)  6 u8 preset  7 u8 0
 *     8  u32 width   12 u32 height   16 u32 frames   20 u32 keyframes
 *     24 u32 rate_num  28 u32 rate_den   (nominal, 0/0 when variable)
 *     32 u64 duration_us                 (last pts + its duration)
 *     40 u8 cid[36]  CIDv1 raw sha2-256 of the original video file
 *     76 u32 table_off   80 u32 total_len (the whole file, trailer included)
 *     84 u32 gop_shapes  (the most shapes any GOP's table holds)
 *     88 u32 frame_max   (the longest frame ZXVG)   92 u32 0
 *   records, from offset 96, one per frame in order:
 *     keyframe: u8 0  uv npal  npal x (r g b)  uv nshapes  op*
 *     delta:    u8 1  uv nshapes  op*
 *     op := uv tag  zz x0  zz y0  [body]
 *           tag odd:  a reference to shape tag >> 1 of this GOP's table;
 *           tag even: a new shape, appended to the table:
 *           body := zz dex zz dey, then tag >> 1 (>= 1) bytes of ZXVG shape
 *           (everything after its first MOVE's operands through its FILL)
 *           and (dex, dey) where it ends relative to where it starts.
 *           (x0, y0): where the shape starts in this frame, absolute,
 *           1/16 px; |x0|, |y0|, |dex|, |dey| <= 2^24.
 *   frame table at table_off: frames x 32 bytes
 *     u64 pts_us  u32 dur_us  u32 record_off  u32 record_len
 *     i32 psnr_cdb  i32 ssim_x10000  u8 kind (0 key, 1 delta)  u8[3] 0
 *   trailer: SHA-256 of every byte before it (32 bytes).
 * A frame's ZXVG is then: "ZXVG" 1 0, viewBox 0 0 16w 16h, the GOP palette
 * (alpha 255), no gradients, no ids; per shape NEWPATH, MOVE to (x0, y0)
 * relative to the previous shape's end point (its last subpath's start;
 * (0, 0) for the first), the body; END. The player checks every frame of
 * every record with zui_zxvg_check when it opens the file.
 *
 * WHAT IS AND IS NOT IMPLEMENTED (do not overstate this)
 *   Done: tracing every decodable frame; keyframes on a fixed interval,
 *   scene changes and table overflow; whole-shape reuse by hash and byte
 *   comparison (translation included); per-frame PSNR and SSIM of the
 *   frame's own re-render against the decoded frame; the original file's
 *   CID; a reader that fails closed (bounds, ordering, references, every
 *   rebuilt frame checked, the SHA-256 trailer) and is fuzzed in
 *   test_zui_vtrace.c; playing in order, rewinding and seeking (from the
 *   nearest keyframe).
 *   Not done: reuse of a part of a shape (a shape whose outline changed
 *   anywhere is stored whole again), motion search beyond exact shape
 *   matches, temporal smoothing of the trace (a still region of a noisy
 *   film can retrace slightly differently each frame and then costs its
 *   bytes again), audio, partial alpha. Every frame is APPROXIMATE unless
 *   its record says otherwise; the original film stays addressable by its
 *   CID.
 */
#ifndef ZXV_ZUI_VTRACE_VIDEO_H
#define ZXV_ZUI_VTRACE_VIDEO_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_canvas.h"
#include "zui_vtrace.h"

#define ZVM_HEAD_LEN  96u
#define ZVM_ENTRY_LEN 32u
#define ZVM_GOP_CAP   65536u /* shapes one GOP's table can hold */

typedef struct {
    uint32_t gop;        /* a keyframe at least every gop frames (1..100000)     */
    uint32_t scene_thr;  /* a channel difference counted as a change             */
    uint32_t scene_pct;  /* percent of changed pixels that makes a keyframe      */
    uint32_t max_frames; /* trace at most this many (0: as many as the file has) */
} zvm_opts_t;

void zvm_opts_default(zvm_opts_t *o); /* gop 60, thr 40, pct 30, all frames */

typedef struct {
    uint32_t w, h, frames, keyframes;
    uint64_t duration_us;
    uint32_t bytes;      /* the ZXVM                                        */
    uint64_t zxvg_bytes; /* the frames as separate ZXVG files would take    */
    uint32_t shapes_new, shapes_ref;
    int32_t psnr_min, psnr_avg, ssim_min, ssim_avg;
    uint32_t fallbacks; /* backend calls that fell back to the CPU        */
    uint8_t cid[ZVT_CID_LEN];
    int video_rc; /* < 0: the film stopped with this zui_video error after the
                     frames that are in the ZXVM                         */
} zvm_result_t;

/* ---- writing, one frame at a time --------------------------------------- */
typedef struct {
    uint32_t off, len; /* the body's bytes in the ZXVM                       */
    int32_t dex, dey;  /* its end point relative to its start, 1/16 px       */
    uint64_t hash;
} zvm_tab_t;

typedef struct {
    uint32_t w, h, n, max_frames;
    zvt_params_t p;
    zvm_opts_t o;
    const zvt_backend_t *be;
    uint8_t *out;
    uint32_t cap, len;
    bool full, cid_of_pixels;
    /* work */
    uint8_t *tw;
    uint64_t twlen;
    uint8_t *zxvg; /* the last frame's ZXVG (zvm_writer_frame's output)      */
    uint32_t zxvg_cap, zxvg_len;
    zvt_shape_t *shapes;
    uint32_t shape_cap;
    uint32_t *prev;
    uint8_t *mask;
    uint32_t *rows;
    zvm_tab_t *tab;
    uint32_t ntab, *index;
    uint8_t *table; /* frame entries */
    /* GOP */
    uint32_t pal[256], npal, since_key;
    bool have_prev;
    zvt_result_t last; /* the last frame's trace result                       */
    bool last_key;
    zvm_result_t r;
    int64_t psnr_sum, ssim_sum;
    uint32_t gop_max;
    uint32_t rate_num, rate_den;
} zvm_writer_t;

/* Work bytes for frames of w x h, up to max_frames of them. */
uint64_t zvm_writer_work_size(uint32_t w, uint32_t h, uint32_t max_frames);
/* cid: of the original file (NULL: all zero, and the flags say "pixels").
 * p: the trace parameters (gradients are turned off); o NULL: defaults. */
int zvm_writer_begin(zvm_writer_t *wr, uint32_t w, uint32_t h, uint32_t max_frames,
                     uint32_t rate_num, uint32_t rate_den, const uint8_t *cid,
                     const zvt_params_t *p, const zvm_opts_t *o, const zvt_backend_t *be,
                     void *work, uint64_t worklen, uint8_t *out, uint32_t outcap);
/* Trace one frame (w x h, stride w) and append it. After it returns 0,
 * wr->zxvg / wr->zxvg_len hold the frame's ZXVG and wr->last its result. */
int zvm_writer_frame(zvm_writer_t *wr, const uint32_t *px, uint64_t pts_us, uint64_t dur_us);
/* Finish: the frame table and the trailer. Returns the ZXVM length. */
int32_t zvm_writer_end(zvm_writer_t *wr, zvm_result_t *res);

/* ---- the whole film in one call ------------------------------------------ */
/* Work for zvm_trace_video on this file (0 when it cannot be opened). */
uint64_t zvm_trace_video_work(const uint8_t *file, uint32_t n, const zvm_opts_t *o);
/* An output size that is enough for any film of this file's size and shape
 * is not knowable in advance; zvm_trace_video returns ZVT_E_SPACE when out
 * is too small and the caller may retry with more. */
int32_t zvm_trace_video(const uint8_t *file, uint32_t n, const zvt_params_t *p, const zvm_opts_t *o,
                        const zvt_backend_t *be, void *work, uint64_t worklen, uint8_t *out,
                        uint32_t outcap, zvm_result_t *res);

/* ---- reading and playing ---------------------------------------------------- */
typedef struct {
    uint32_t w, h, frames, keyframes, rate_num, rate_den;
    uint64_t duration_us;
    zvt_preset_t preset;
    bool cid_of_pixels;
    uint8_t cid[ZVT_CID_LEN];
    uint32_t gop_shapes, frame_max;
    uint64_t work; /* bytes zvm_open needs */
} zvm_info_t;

typedef struct {
    uint32_t index;
    uint64_t pts_us, dur_us;
    int32_t psnr_cdb, ssim;
    bool key;
    const uint8_t *zxvg; /* the rebuilt frame, inside the player's work */
    uint32_t zxvg_len;
} zvm_frame_t;

typedef struct {
    const uint8_t *d;
    uint32_t n;
    zvm_info_t info;
    zvm_tab_t *tab;
    uint32_t ntab;
    uint8_t *frame;
    void *vw; /* zui_vec_work_t */
    uint32_t next;
    uint32_t pal_off, npal; /* the GOP palette in d */
} zvm_player_t;

/* Check everything that can be checked without rebuilding frames (header,
 * trailer, table, records, references). ZVT_E_DATA on any defect. */
int zvm_probe(const uint8_t *d, uint32_t n, zvm_info_t *info);
/* Probe, then rebuild every frame once and zui_zxvg_check it. */
int zvm_open(zvm_player_t *pl, const uint8_t *d, uint32_t n, void *work, uint64_t worklen);
/* 1: the next frame is in *f and, when s is not NULL, drawn fitted into box
 * (aspect kept, centred; the caller clears s first); 0: the end; < 0 error. */
int zvm_next_frame(zvm_player_t *pl, zui_surface_t *s, zui_rect_t box, zvm_frame_t *f);
void zvm_rewind(zvm_player_t *pl);
/* Make frame i the next one (rebuilds from its keyframe). */
int zvm_seek(zvm_player_t *pl, uint32_t i);

#endif /* ZXV_ZUI_VTRACE_VIDEO_H */
