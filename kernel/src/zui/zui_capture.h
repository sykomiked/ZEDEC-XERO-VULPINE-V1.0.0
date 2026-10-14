/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_capture.h — screenshots and screen recording.
 *
 * Every ZXV screen is pixels zui drew itself (zui_canvas.h), so a picture of
 * the screen is a copy of those pixels: the whole surface, a rectangle of it,
 * or one window of the window manager (zui_wm.h), its frame or only its
 * content. Pictures are written as PNG by our own encoder; a recording is a
 * run of such frames in a small container of our own, described below.
 *
 *   CP1 OUR OWN PNG. 8-bit RGB, no interlace, filter 0 on every row. The
 *       zlib stream is either stored blocks (one per row, no compression) or
 *       one fixed-Huffman deflate block whose matches repeat the pixel to the
 *       left (distance 3) or the pixel above (distance one row). CRC-32 on
 *       every chunk, Adler-32 at the end of the zlib stream. No library, no
 *       allocation: the encoder keeps two rows and one IDAT buffer in the
 *       caller's zui_capture_t and streams through a write callback.
 *   CP2 PRIVATE IS BLACK. A window marked private (zui_capture_set_private:
 *       a lock screen, a window holding a password field) and every private
 *       rectangle (zui_capture_private_rect) come out black in every
 *       picture and every recorded frame: the parts of them that are
 *       visible on screen, so a window raised above a private one still
 *       shows. A private rectangle can belong to a window (it is then
 *       relative to that window's content and moves with it) or to the
 *       screen (it covers whatever is there).
 *   CP3 CAPTURE IS NEVER SILENT. A screenshot and the start and the stop of
 *       a recording each tell zui_capture_t.in_use, which is what drives the
 *       "screen capture in use" indicator (zui_privacy). A screenshot reports
 *       on then off; a recording reports on at start and off at stop.
 *   CP4 RECORDING. A session takes frames from the surfaces the shell
 *       presents (zui_rec_present after every present), at an integer rate
 *       the caller asks for (1..ZUI_REC_FPS_MAX per second) on a schedule
 *       that does not drift: frame k is due at start + k * 1000 / fps ms.
 *       Late calls skip the slots they missed. A frame whose pixels did not
 *       change since the last written one is skipped (no damage in the
 *       target, or the same pixels). Times are integer ms from the start.
 *   CP5 THE CONTAINER ("ZXVR", all numbers little-endian):
 *         header  32 bytes: "ZXVR", u16 version (1), u16 header size (32),
 *                 u32 width, u32 height, u32 fps, u32 encoding
 *                 (0 = every frame PNG, 1 = PNG key frames and run-length
 *                 deltas), u32 key_every, u32 reserved (0)
 *         record  16 bytes: u8 kind (1 = PNG key frame, 2 = delta,
 *                 0xFF = end), 3 zero bytes, u32 frame index (end: frames
 *                 written), u32 t_ms (end: duration), u32 payload bytes;
 *                 then the payload
 *         delta   runs until width x height pixels are covered, row-major:
 *                 u32 unchanged pixels to skip, u32 changed pixels n, then
 *                 n x 3 bytes R G B
 *       A payload's length is known before it is written: the frame is
 *       encoded once into a counter and once for real (both deterministic).
 *   CP6 Integer only, deterministic, no allocation: the same surface gives
 *       the same bytes.
 */
#ifndef ZXV_ZUI_CAPTURE_H
#define ZXV_ZUI_CAPTURE_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_canvas.h"
#include "zui_present.h"
#include "zui_wm.h"

#ifndef ZUI_CAP_MAX_W
#    define ZUI_CAP_MAX_W 7680 /* widest picture (8K); a row is 1 + 3w bytes */
#endif
#ifndef ZUI_CAP_IDAT
#    define ZUI_CAP_IDAT 32768u /* bytes per IDAT chunk */
#endif
#define ZUI_CAP_PRIVATE_RECTS 16u
#define ZUI_CAP_REGIONS       (ZUI_WM_MAX + ZUI_CAP_PRIVATE_RECTS)
#define ZUI_REC_FPS_MAX       120u

enum {
    ZUI_CAP_OK = 0,
    ZUI_CAP_E_ARG = -1,   /* a bad rectangle, size or rate             */
    ZUI_CAP_E_WRITE = -2, /* the write callback refused                */
    ZUI_CAP_E_SIZE = -3,  /* wider than ZUI_CAP_MAX_W                  */
    ZUI_CAP_E_STATE = -4, /* not recording, or already recording       */
    ZUI_CAP_E_GONE = -5   /* no such window, or it is minimised        */
};

typedef enum { ZUI_PNG_STORED = 0, ZUI_PNG_FIXED } zui_png_mode_t;
typedef enum { ZUI_CAP_SCREENSHOT = 0, ZUI_CAP_RECORDING } zui_cap_use_t;

/* Takes the next n bytes of output; false stops the capture (E_WRITE).
 * A NULL write function only counts bytes. */
typedef bool (*zui_cap_write_fn)(void *ctx, const uint8_t *p, uint32_t n);

typedef struct {
    zui_rect_t r;
    zui_rect_t cover[ZUI_WM_MAX]; /* windows above it: not blacked */
    uint32_t ncover;
} zui_cap_region_t;

typedef struct {
    uint32_t win; /* 0: in screen coordinates; else relative to its content */
    zui_rect_t r;
    bool used;
} zui_cap_private_t;

/* The encoder's state (CP1). */
typedef struct {
    zui_cap_write_fn write;
    void *ctx;
    uint64_t bytes; /* written (or counted) so far */
    bool failed;
    uint8_t idat[ZUI_CAP_IDAT];
    uint32_t nidat;
    uint32_t bits, nbits; /* deflate bit buffer, LSB first */
    uint32_t a, b;        /* Adler-32 halves */
    uint8_t row[2][1 + 3 * ZUI_CAP_MAX_W];
} zui_png_t;

typedef struct {
    zui_wm_t *wm;                     /* windows to capture and to black out; may be NULL */
    uint32_t priv[ZUI_WM_MAX], npriv; /* private window ids (CP2) */
    zui_cap_private_t rect[ZUI_CAP_PRIVATE_RECTS];
    /* CP3: the "screen capture in use" report (zui_privacy draws it) */
    void (*in_use)(void *ctx, zui_cap_use_t use, bool on);
    void *in_use_ctx;
    zui_png_mode_t mode; /* how pictures are compressed (default FIXED) */
    bool recording;
    uint32_t shots; /* screenshots taken */
    /* scratch */
    zui_cap_region_t reg[ZUI_CAP_REGIONS];
    uint32_t nreg;
    zui_png_t png;
} zui_capture_t;

void zui_capture_init(zui_capture_t *c, zui_wm_t *wm);
/* CP2: mark a window private (or not). False when the list is full. */
bool zui_capture_set_private(zui_capture_t *c, uint32_t win, bool on);
bool zui_capture_is_private(const zui_capture_t *c, uint32_t win);
/* CP2: a private rectangle; win 0 = screen coordinates, else relative to
 * that window's content. Returns a handle (>= 0), or -1 when full. */
int32_t zui_capture_private_rect(zui_capture_t *c, uint32_t win, zui_rect_t r);
void zui_capture_private_clear(zui_capture_t *c, int32_t handle);

/* Screenshots (CP1..CP3). Each writes one PNG through write; pixels outside
 * the surface come out black. */
int zui_capture_surface(zui_capture_t *c, const zui_surface_t *s, zui_cap_write_fn write,
                        void *ctx);
int zui_capture_rect(zui_capture_t *c, const zui_surface_t *s, zui_rect_t r, zui_cap_write_fn write,
                     void *ctx);
/* One window: its frame, or only its content. With `scratch` (at least the
 * content's size) and content_only, the window's client paints itself there
 * first, so a window partly hidden behind others still comes out whole. */
int zui_capture_window(zui_capture_t *c, const zui_surface_t *s, uint32_t win, bool content_only,
                       zui_surface_t *scratch, uint32_t tick, zui_cap_write_fn write, void *ctx);
/* The rectangle a window capture takes (screen coordinates), w == 0 when
 * the window is gone or minimised. */
zui_rect_t zui_capture_window_rect(const zui_capture_t *c, uint32_t win, bool content_only);

/* The PNG encoder on its own: `r` of `s` (screen coordinates), private
 * regions blacked out, with no in_use report (the callers above make it). */
int zui_png_write(zui_capture_t *c, const zui_surface_t *s, zui_rect_t r, zui_cap_write_fn write,
                  void *ctx);

/* Checksums PNG uses (exposed for tests and the recording container). */
uint32_t zui_crc32(uint32_t crc, const uint8_t *p, uint32_t n);     /* start with 0 */
uint32_t zui_adler32(uint32_t adler, const uint8_t *p, uint32_t n); /* start with 1 */

/* ---- recording (CP4, CP5) ---------------------------------------------- */
typedef enum { ZUI_REC_PNG = 0, ZUI_REC_DELTA } zui_rec_encoding_t;
typedef enum { ZUI_REC_SURFACE = 0, ZUI_REC_RECT, ZUI_REC_WINDOW } zui_rec_target_t;

enum { ZUI_REC_KIND_KEY = 1, ZUI_REC_KIND_DELTA = 2, ZUI_REC_KIND_END = 0xFF };
#define ZUI_REC_HEADER 32u
#define ZUI_REC_RECORD 16u

typedef struct {
    zui_rec_target_t target;
    zui_rect_t rect;   /* ZUI_REC_RECT */
    uint32_t win;      /* ZUI_REC_WINDOW */
    bool content_only; /* ZUI_REC_WINDOW */
    uint32_t fps;      /* 1..ZUI_REC_FPS_MAX */
    zui_rec_encoding_t encoding;
    uint32_t key_every; /* DELTA: a PNG key frame every this many frames (0: only the first) */
    /* The last written frame, width x height pixels, caller-owned. Needed for
     * DELTA; with PNG it makes the unchanged test exact instead of a CRC. */
    uint32_t *prev;
    uint32_t prev_cap; /* in pixels */
    zui_cap_write_fn write;
    void *ctx;
} zui_rec_config_t;

typedef struct {
    zui_capture_t *cap;
    zui_rec_config_t cfg;
    int32_t w, h; /* frame size, fixed at start */
    uint32_t start_ms, next_slot;
    uint32_t frames, skipped, last_ms;
    uint32_t last_crc;
    zui_rect_t last_rect; /* where the last written frame came from */
    bool have_last;
    bool on;
    int err; /* the first error, ZUI_CAP_OK while fine */
} zui_rec_t;

/* Start: writes the header and reports in_use(RECORDING, true). The frame
 * size is the target's size now; a window target follows the window's
 * position, keeping that size. */
int zui_rec_start(zui_rec_t *r, zui_capture_t *c, const zui_surface_t *s,
                  const zui_rec_config_t *cfg, uint32_t now_ms);
/* After each present. damage NULL: unknown, the pixels decide. Returns 1
 * when a frame was written, 0 when none was due or nothing changed, <0 on an
 * error (the recording then stops). */
int zui_rec_present(zui_rec_t *r, const zui_surface_t *s, const zui_damage_t *damage,
                    uint32_t now_ms);
/* Writes the end record and reports in_use(RECORDING, false). */
int zui_rec_stop(zui_rec_t *r, uint32_t now_ms);

#endif /* ZXV_ZUI_CAPTURE_H */
