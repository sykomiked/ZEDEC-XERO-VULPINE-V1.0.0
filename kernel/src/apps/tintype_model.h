/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tintype_model.h — Tintype, the photo workflow, as a model (zxm.h):
 * photos and screen grabs captured, developed and catalogued in albums of
 * the Hoard, each with its rights and its record, and checked against that
 * record when read back. No drawing, no allocation, integers only; the
 * rules it runs are tintype_rules.h (TR1-TR6) and its parts' (Glint,
 * Gesso, Pinhole, Lifeboat, Glimpse).
 *
 * zxvd methods used: files.list, files.stat, files.read, files.write,
 * files.mkdir and sys.disk. zxvd decodes no image and captures neither the
 * camera nor the display, so the view captures and hands the bytes in
 * (TM2, TM3) and its decoder hands the develop step channel values (TM4);
 * the model reads only an image's header.
 *
 *   TM1 ALBUMS. tintype_album_add files an album 1..9999 under its parent
 *       (0 the root, or an album already filed, never itself) with the
 *       bytes the owner declares for it (TR4), when its node fits the tree
 *       (TR6). At most TINTYPE_ALBUMS.
 *   TM2 A PHOTO. tintype_take_photo takes the JPEG the view captured with
 *       what the camera reported: it is taken only when the camera is ready
 *       (pinhole.camera_ready: a sensor, the mode, and the frame fitting
 *       TINTYPE_SHOT_MAX) and the exposure is inside its limits
 *       (pinhole.exposure_ok, no compensation); its exposure value (TR1) is
 *       kept with it. Its width and height (SOF) and the time the camera
 *       wrote (EXIF, read on the camera's own clock) are decoded from its
 *       header and joined with what was reported (TR2): a frame that
 *       decodes to another size is kept BOTH, flagged possibly altered,
 *       with a notice; the owner decides.
 *   TM3 A SCREEN GRAB. tintype_take_screen takes the PNG the view captured
 *       of a region of the screen: the region's corner is
 *       glimpse.capture_origin and its extent the part on the screen
 *       (Glimpse's GR2), and the PNG's IHDR is joined with that size (TR2;
 *       a PNG holds no camera time, so the time is NEITHER, never a
 *       contradiction).
 *   TM4 DEVELOP. tintype_develop lays the tone curve (TR5) over channel
 *       values, then gesso.pixel_bright and gesso.pixel_contrast;
 *       tintype_histogram counts them in bins of a width of levels
 *       (gesso.pixel_range_count). Neither changes a kept file: the view
 *       hands the developed values to its encoder and the result is taken
 *       as a new capture.
 *   TM5 ROOM. tintype_room reads sys.disk. A photo is kept only when it
 *       fits what may still be written with the owner's floor kept free
 *       ("photo fits", lifeboat.restore_fits) and its album stays within
 *       its declared bytes (TR4); else nothing is written (ZXM_E_TOOBIG)
 *       and the notice says how many bytes are short. A screen recording's
 *       raw size is checked against the same room (glimpse.recording_bytes).
 *   TM6 KEEP. tintype_keep writes the capture held as "Tintype/Album
 *       <album>/Photo <N>.jpg" (.png for a grab), N the first number free
 *       in files.list's listing of the album (glimpse.first_free_n), with
 *       files.write as a new file (exclusive), and confirms its size with
 *       files.stat. Its rights row (TR3), its record (pinhole.photo_record_ok)
 *       and its catalogue item (glint.item_ok) must fit first. Keeping
 *       changes kept state, so it takes the owner's confirmation: without
 *       it nothing is sent (ZXM_E_STATE). Its content id is the SHA-256 of
 *       its bytes, in hex.
 *   TM7 POSSIBLY ALTERED. tintype_verify reads a kept photo back
 *       (files.stat, files.read), decodes its header again and hashes it,
 *       and joins that with its record (TR2): TRUE as recorded, BOTH
 *       flagged possibly altered. Advisory: nothing is changed or removed.
 *   TM8 THE CATALOGUE. TINTYPE_CATALOGUE holds the albums and every
 *       photo's record and rights row, one line each, written whole
 *       (files.write) only on the owner's word and read back with
 *       files.read; a catalogue that does not parse, or a line that does
 *       not fit its rules, leaves what is open as it was. */
#ifndef TINTYPE_MODEL_H
#define TINTYPE_MODEL_H

#include "tintype_rules.h"
#include "zxm.h"

#define TINTYPE_DIR       "Tintype"
#define TINTYPE_CATALOGUE "Tintype/catalogue.txt"
#define TINTYPE_ALBUMS    16u
#define TINTYPE_PHOTOS    64u
#define TINTYPE_SHOT_MAX  2097152u              /* bytes of one capture (2 MiB, Pinhole's frame) */
#define TINTYPE_SIDE_MAX  99999                 /* pixels a side (PH7: 5 digits) */
#define TINTYPE_CAT_MAX   32768u                /* bytes of the catalogue */
#define TINTYPE_FLOOR     67108864              /* bytes kept free by default (64 MiB) */
#define TINTYPE_STAMP_MAX INT64_C(999999999999) /* seconds since 1970 */
#define TINTYPE_NUM_MAX   99999                 /* photos in an album (TR6) */
#define TINTYPE_PATH_MAX  64u
#define TINTYPE_CID_HEX   64u
#define TINTYPE_NOTICE    200u
#define TINTYPE_BINS_MAX  256u

typedef struct {
    int64_t album, parent;
    int64_t declared, used; /* bytes */
    int64_t photos;
} tintype_album_t;

typedef struct {
    int64_t id;    /* 1.. in the order kept: the rights register's photo */
    int64_t num;   /* N of "Photo N" in its album */
    int64_t album; /* 1..TINTYPE_ALBUM_MAX */
    int64_t kind;  /* TINTYPE_KIND_* */
    int64_t bytes, width, height;
    int64_t stamp;                   /* seconds since 1970, as recorded */
    bool tknown;                     /* the file held a camera time when kept */
    int64_t ev;                      /* TR1, thirds; TINTYPE_EV_REFUSED for a grab */
    int64_t owner, licence, release; /* TR3 */
    char cid[TINTYPE_CID_HEX + 1];
    lpres_state_t said; /* TR2: TRUE as recorded, BOTH possibly altered */
} tintype_photo_t;

typedef struct {
    zxm_link_t *link;
    /* TM1 */
    tintype_album_t album[TINTYPE_ALBUMS];
    uint32_t nalbums;
    /* TM2, TM3: the capture held */
    uint8_t shot[TINTYPE_SHOT_MAX];
    uint32_t shot_len; /* 0 = none */
    int64_t shot_kind, shot_width, shot_height, shot_stamp, shot_ev;
    bool shot_tknown;
    lpres_state_t shot_said;
    /* TM5: the last sys.disk, and the owner's floor */
    int64_t total, free, avail, floor;
    /* TM6, TM8 */
    tintype_photo_t photo[TINTYPE_PHOTOS];
    uint32_t nphotos;
    int64_t last; /* the highest id kept */
    bool dirty;   /* changed since saved or loaded */
    char notice[TINTYPE_NOTICE];
} tintype_t;

void tintype_init(tintype_t *t, zxm_link_t *link);
/* TM1 */
int tintype_album_add(tintype_t *t, int64_t album, int64_t parent, int64_t declared);
tintype_album_t *tintype_album(tintype_t *t, int64_t album);
/* TM2: width and height as the camera reported them, stamp in seconds */
int tintype_take_photo(tintype_t *t, int64_t sensor, int64_t mode, const uint8_t *jpeg, uint32_t n,
                       int64_t width, int64_t height, int64_t aperture, int64_t shutter,
                       int64_t iso, int64_t stamp);
/* TM3: the screen's size, the region asked for, the PNG of it */
int tintype_take_screen(tintype_t *t, int64_t screen_w, int64_t screen_h, int64_t x, int64_t y,
                        int64_t w, int64_t h, const uint8_t *png, uint32_t n, int64_t stamp);
/* TM4: n channel values in, n out; curve holds TINTYPE_KNOTS knots */
int tintype_develop(const uint8_t *in, uint32_t n, const int64_t *curve, int64_t delta,
                    int64_t gain, uint8_t *out);
/* TM4: bins of width levels (1..256), *nbins = ceil(256 / width) */
int tintype_histogram(const uint8_t *px, uint32_t n, int64_t width, int64_t *bins, uint32_t *nbins);
/* TM5 */
int tintype_set_floor(tintype_t *t, int64_t floor);
int tintype_room(tintype_t *t);
/* TM5: a recording's exact raw size in *bytes, ZXM_E_TOOBIG past the room */
int tintype_recording_fits(tintype_t *t, int64_t w, int64_t h, int64_t bpp, int64_t secs,
                           int64_t fps, int64_t *bytes);
/* TM6: the new photo's id in *id (may be NULL) */
int tintype_keep(tintype_t *t, int64_t album, int64_t owner, int64_t licence, int64_t release,
                 bool owner_confirmed, int64_t *id);
tintype_photo_t *tintype_find(tintype_t *t, int64_t id);
/* TM6: "Tintype/Album <album>/Photo <num>.jpg|png" */
bool tintype_path(const tintype_photo_t *p, char *out, uint32_t cap);
/* TM7 */
int tintype_verify(tintype_t *t, int64_t id);
/* TM8 */
int tintype_save(tintype_t *t, bool owner_confirmed);
int tintype_load(tintype_t *t);

#endif
