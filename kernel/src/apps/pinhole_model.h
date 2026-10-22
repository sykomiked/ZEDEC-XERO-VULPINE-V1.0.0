/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pinhole_model.h — Pinhole, the camera, as a model (zxm.h): the camera's
 * state, the frame the view captured, its exposure, and the photos kept in
 * the Hoard with their index. No drawing, no allocation, integers only.
 * The rules it runs are pinhole_rules.h (PH1-PH7).
 *
 *   PM1 THE CAMERA IS SHOWN AS ON WHENEVER IT IS. pinhole_camera_on turns
 *       it on only when the camera is ready (PH1); from then until
 *       pinhole_camera_off, p->on is true and p->indicator says
 *       PINHOLE_ON_TEXT, which the view draws on every frame. No frame is
 *       taken while the camera is off.
 *   PM2 NO CAPTURE METHOD. zxvd has no method that captures a frame, lists
 *       a camera's sensors or modes, or sets an exposure, so the view
 *       captures the frame (a JPEG, PINHOLE_FRAME_MAX bytes at most) and
 *       hands it here with what the camera reported (pinhole_take). The
 *       buffer flag of PH1 is whether the frame fits PINHOLE_FRAME_MAX.
 *   PM3 EXPOSURE. An exposure outside PH3's limits is refused (ZXM_E_ARG)
 *       and the one kept stays as it was.
 *   PM4 ROOM FIRST. Before a photo is written, sys.disk is read and the
 *       photo must fit what the owner may still write with the owner's
 *       floor kept free (PH4); otherwise nothing is written
 *       (ZXM_E_TOOBIG) and the notice says how many bytes are short.
 *   PM5 THE PLACE ONLY WITH THE OWNER'S PERMIT. A place tag is asked for
 *       with place.ask; it is added only when the answer is allowed and
 *       holds a location (PH6). Pending, never and unknown add nothing,
 *       and the photo is still saved, untagged, with a notice saying why.
 *   PM6 FILES IN THE HOARD. A photo is PINHOLE_DIR/a<album>-p<photo>.jpg,
 *       written with files.write (base64, begin/add/end when it does not
 *       fit one request) as a new file (exclusive). Its record (PH7) goes
 *       into PINHOLE_INDEX, one line a photo, rewritten whole after each
 *       photo; pinhole_load reads it back. */
#ifndef PINHOLE_MODEL_H
#define PINHOLE_MODEL_H

#include "pinhole_rules.h"
#include "zxm.h"

#define PINHOLE_APP       "pinhole"
#define PINHOLE_DIR       "Pinhole"
#define PINHOLE_INDEX     "Pinhole/index.txt"
#define PINHOLE_ON_TEXT   "Camera on"
#define PINHOLE_FRAME_MAX 2097152u         /* bytes of one JPEG frame (2 MiB) */
#define PINHOLE_PHOTOS    PINHOLE_ROWS_MAX /* photos in the index (PH5) */
#define PINHOLE_ALBUM_MAX 9999             /* PH7: 4 digits */
#define PINHOLE_SIDE_MAX  99999            /* PH7: 5 digits of pixels */
#define PINHOLE_FLOOR     67108864         /* bytes kept free by default (64 MiB) */
#define PINHOLE_PATH_MAX  48u
#define PINHOLE_NOTICE    200u

typedef struct {
    int64_t photo;  /* 1.. in the order taken */
    int64_t album;  /* 0..PINHOLE_ALBUM_MAX */
    int64_t bytes;  /* the file's size */
    int64_t width;  /* pixels */
    int64_t height; /* pixels */
    int64_t placed; /* 1 when a place tag was added (PM5) */
    int32_t lat;    /* millionths of a degree, when placed */
    int32_t lon;
    int64_t stamp; /* ms since 1970, when it was taken */
    char path[PINHOLE_PATH_MAX];
} pinhole_photo_t;

typedef struct {
    zxm_link_t *link;
    /* PM1 */
    bool on;
    char indicator[16]; /* PINHOLE_ON_TEXT while on, else "" */
    int64_t sensor, mode;
    /* PM3 */
    int64_t iso, shutter, comp;
    /* PM2: the frame the view captured */
    uint8_t frame[PINHOLE_FRAME_MAX];
    uint32_t frame_len; /* 0 = none */
    int64_t width, height;
    /* PM4: the last sys.disk, and the owner's floor */
    int64_t total, free, avail, floor;
    /* PM5: place.ask's last answer */
    char answer[16];
    /* PM6 */
    bool folder;
    bool loaded;
    pinhole_photo_t photo[PINHOLE_PHOTOS];
    uint32_t n;
    int64_t last; /* the highest photo number kept */
    char notice[PINHOLE_NOTICE];
} pinhole_t;

void pinhole_init(pinhole_t *p, zxm_link_t *link);

/* PM1: on only when sensor and mode are 1 (PH1, with the buffer free). */
int pinhole_camera_on(pinhole_t *p, int64_t sensor, int64_t mode);
void pinhole_camera_off(pinhole_t *p);
/* PM3 */
int pinhole_set_exposure(pinhole_t *p, int64_t iso, int64_t shutter, int64_t comp);
/* PM2: the frame the view captured. ZXM_E_STATE while the camera is off,
 * ZXM_E_ARG for a frame that is no JPEG, no size or does not fit the buffer
 * (PH1's buffer flag 0), or a side outside 1..PINHOLE_SIDE_MAX. */
int pinhole_take(pinhole_t *p, const uint8_t *jpeg, uint32_t n, int64_t width, int64_t height);
/* PM4: the owner's floor, 0..LIFEBOAT_BYTES_MAX. */
int pinhole_set_floor(pinhole_t *p, int64_t floor);
/* PM4: sys.disk into p->total, p->free and p->avail. */
int pinhole_room(pinhole_t *p);
/* PM6: the index, read back (an index not written yet is an empty one). */
int pinhole_load(pinhole_t *p);
/* PM4-PM6: the frame taken, saved in album at stamp; with place set, a
 * place tag is asked for (PM5). ZXM_E_STATE when not loaded or no frame. */
int pinhole_save(pinhole_t *p, int64_t album, int64_t stamp, bool place);
/* PH5 over the index: 0 and the bytes in *total, or PINHOLE_REFUSED. */
int pinhole_album_bytes(const pinhole_t *p, int64_t album, int64_t *total);

#endif
