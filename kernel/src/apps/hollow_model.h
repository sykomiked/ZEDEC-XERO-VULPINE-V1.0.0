/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hollow_model.h — Hollow, the desktop, as a model (zxm.h): the wallpaper
 * (an image from the Hoard, files.read), the dock, window places and the
 * grid windows tile into, the spaces and the one you are in (spaces.list,
 * spaces.get), the app in front reported once a minute (screen.tick), the
 * notices (notes.list, notes.mark_read) and simple mode (ui.get). The rules
 * are hollow_rules.h (HR1-HR6), the same ones the app's KRON, C++, Sutra,
 * Eve and MYRA blocks run. No drawing, no allocation, integers only.
 *
 * The desk is one UTF-8 text file in the Hoard, HOLLOW_DESK, read with
 * files.read and written whole with files.write:
 *
 *   HD1 The first line is "hollow 1".
 *   HD2 Then at most one "wallpaper <path>" line: a path in the Hoard,
 *       1..HOLLOW_PATH_MAX bytes, none a control byte.
 *   HD3 Then the dock in its order, one "pin <app> <added>" line per place:
 *       app is the app's matrix slot (1..HOLLOW_APP_MAX), each app at most
 *       once, added the Unix second it was pinned; at most HOLLOW_DOCK_MAX.
 *   HD4 Then one "win <app> <x> <y> <width> <height>" line per window
 *       place, each app at most once, at most HOLLOW_WIN_MAX, each inside
 *       the window limits (HR5) on a screen as large as HOLLOW_PX_MAX.
 *   HD5 Every line ends with "\n"; numbers are plain decimal with no
 *       leading zero, x and y may be negative. Anything else is not a
 *       desk: the load is refused (ZXM_E_PROTOCOL) and every change is
 *       refused (ZXM_E_STATE) until a load succeeds, so a file the model
 *       cannot read is never overwritten.
 *   HD6 A change is checked, written whole, and only then kept: a write
 *       zxvd refuses leaves the model as it was.
 *
 * What zxvd tells it is checked before anything shown changes (refresh,
 * enter, tick): a reply out of bounds leaves the model as it was. ui.get
 * is served on zxvd's control socket only; where it is not served
 * (-32601), simple mode stays off and simple_known says so. */
#ifndef HOLLOW_MODEL_H
#define HOLLOW_MODEL_H

#include "hollow_rules.h"
#include "zxm.h"

#define HOLLOW_DESK      "Hollow.desk" /* in the Hoard's home */
#define HOLLOW_APP_MAX   144           /* the matrix's last layer */
#define HOLLOW_DOCK_MAX  24u
#define HOLLOW_WIN_MAX   64u
#define HOLLOW_PATH_MAX  255u
#define HOLLOW_TEXT_MAX  8192u /* the whole desk, every line full */
#define HOLLOW_IMAGE_MAX (4u * 1024u * 1024u)
#define HOLLOW_SPACES    32u
#define HOLLOW_NAME_MAX  80u      /* zxv_spaces.h ZXS_NAME_MAX */
#define HOLLOW_NOTES     20u      /* notes.list answers the newest 20 */
#define HOLLOW_TITLE_MAX 63u      /* zx_notify.h ZXN_TITLE_MAX - 1 */
#define HOLLOW_APP_NAME  31u      /* screen.tick's app */
#define HOLLOW_E_FILES   (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */
#define HOLLOW_E_METHOD  (-32601) /* a method zxvd does not serve here */

/* What kind of image the wallpaper is, from its first bytes. */
enum {
    HOLLOW_IMG_NONE = 0,
    HOLLOW_IMG_PNG = 1,
    HOLLOW_IMG_JPEG = 2,
    HOLLOW_IMG_GIF = 3,
    HOLLOW_IMG_BMP = 4,
    HOLLOW_IMG_WEBP = 5,
};

typedef struct {
    int32_t app;   /* matrix slot */
    int64_t added; /* Unix seconds */
} hollow_pin_t;

typedef struct {
    int32_t app;
    int32_t x, y;          /* top left, pixels; may be off the screen in part */
    int32_t width, height; /* pixels */
} hollow_win_t;

typedef struct {
    int64_t cols, rows;     /* HR1, HR2 */
    int64_t cell_w, cell_h; /* HR3 */
} hollow_grid_t;

typedef struct {
    char id[ZXM_SPACE_MAX + 1];
    char kind[12]; /* home, office, team, cloudtop, park */
    char name[HOLLOW_NAME_MAX + 1];
    bool write; /* this node may change it */
} hollow_space_t;

typedef struct {
    uint32_t seq;
    uint32_t pri; /* 0 low .. 3 urgent (zx_notify.h) */
    bool read;
    char title[HOLLOW_TITLE_MAX + 1];
} hollow_note_t;

typedef struct {
    char app[HOLLOW_APP_NAME + 1];
    bool allowed;
    bool counted; /* this tick counted a minute */
    uint32_t used;
    int32_t left; /* -1: no limit */
    char why[96];
} hollow_front_t;

typedef struct {
    zxm_link_t *link;
    bool loaded; /* a load succeeded: changes may be written (HD5) */
    char wallpaper[HOLLOW_PATH_MAX + 1];
    int32_t wallpaper_kind;
    hollow_pin_t dock[HOLLOW_DOCK_MAX];
    uint32_t ndock;
    hollow_win_t win[HOLLOW_WIN_MAX];
    uint32_t nwin;
    hollow_space_t space[HOLLOW_SPACES];
    uint32_t nspaces;
    bool more_spaces;    /* zxvd listed more than HOLLOW_SPACES */
    hollow_space_t here; /* the space you are in ("home" until entered) */
    hollow_note_t note[HOLLOW_NOTES];
    uint32_t nnotes;
    uint32_t unread;
    bool simple;       /* simple mode */
    bool simple_known; /* ui.get answered */
    hollow_front_t front;
} hollow_t;

/* The rules, as the app's logic says them (hollow.zxapp). */
/* A grid for n windows on a screen sw by sh: ZXM_E_ARG when a rule refuses
 * (n outside 0..HOLLOW_TILE_MAX, a side outside 1..HOLLOW_PX_MAX). */
int hollow_grid(int64_t n, int64_t sw, int64_t sh, hollow_grid_t *g);
/* A window at x, y of width by height on a screen sw by sh is inside its
 * limits (HR4, HR5). */
bool hollow_window_ok(int64_t x, int64_t y, int64_t width, int64_t height, int64_t sw, int64_t sh);
/* The image kind of n first bytes (HOLLOW_IMG_NONE when none known). */
int32_t hollow_image_kind(const uint8_t *b, uint32_t n);

/* The desk. */
void hollow_init(hollow_t *h, zxm_link_t *link);
/* Read HOLLOW_DESK; a desk not made yet is an empty desk (ZXM_OK). */
int hollow_load(hollow_t *h);
/* Pin app at the dock's end; now is the Unix second (HR6). */
int hollow_pin(hollow_t *h, int64_t app, int64_t now);
int hollow_unpin(hollow_t *h, uint32_t place);
/* Move the pin at place `from` to place `to`, the others keeping their order. */
int hollow_move(hollow_t *h, uint32_t from, uint32_t to);
/* Keep where app's window is (HD4): refused unless inside the limits. */
int hollow_place(hollow_t *h, int64_t app, int64_t x, int64_t y, int64_t width, int64_t height,
                 int64_t sw, int64_t sh);
/* Tile n apps' windows in the grid for n on a screen sw by sh, row by
 * row, and keep the places: refused when a cell is under the limits. */
int hollow_tile(hollow_t *h, const int32_t *apps, uint32_t n, int64_t sw, int64_t sh);
/* Make the image at path the wallpaper ("" clears it): its first bytes
 * must be an image's (files.read). */
int hollow_set_wallpaper(hollow_t *h, const char *path);
/* Read the wallpaper's bytes into buf (files.read, base64 parts). */
int hollow_wallpaper(hollow_t *h, uint8_t *buf, uint32_t cap, uint32_t *len);

/* zxvd. */
/* spaces.list, notes.list and ui.get, all checked before any is kept. */
int hollow_refresh(hollow_t *h);
/* spaces.get: the space you are in. */
int hollow_enter(hollow_t *h, const char *space);
/* screen.tick for the app in front (once a minute). */
int hollow_tick(hollow_t *h, const char *app);
/* notes.mark_read: every notice read; marked is how many zxvd marked. */
int hollow_mark_read(hollow_t *h, uint32_t *marked);

#endif
