/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gesso_model.h — Gesso, image editing and painting, as a model (zxm.h):
 * one image of 8-bit channels (grey or colour), the edits made to it, its
 * stack of adjustment layers and where it is kept in the Hoard. The rules
 * are gesso_rules.h (GR1-GR7), the same ones the app's KRON, C++, Trebago,
 * Eve, Sutra and MYRA blocks run. No drawing, no allocation, integers only.
 *
 *   GM1 IMAGES ARE FILES. An image is a binary PGM ("P5", grey) or PPM
 *       ("P6", colour) of the Hoard with a maxval of 255, read with
 *       files.read (base64, part by part). Comments in its header are
 *       read past; anything else (another format, another maxval, a
 *       raster shorter or longer than width * height * channels) is
 *       refused (ZXM_E_ARG) and leaves the image as it was.
 *   GM2 LIMITS. A canvas is 1..GESSO_SIDE_MAX pixels a side (GR6), a brush
 *       inside GR5. Anything outside is refused, never cut.
 *   GM3 EDITS. Brightness (GR1), contrast (GR2) and a grey wash (GR3) go
 *       over every channel value; held_dark and held_light count the values
 *       the edit left at 0 and at 255 (witness.kind_count). A brush dab is
 *       round, size pixels across about (x, y), clipped to the canvas with
 *       hollow.shown; inside its hard core (hardness percent of its
 *       radius) it lays its colour at the brush's opacity, and from there
 *       to its rim the opacity falls with the squared distance, to 0 at
 *       the rim (GR3 for each channel).
 *   GM4 LAYERS. Up to GESSO_LAYERS_MAX adjustment layers (GR7) wait above
 *       the image, each shown or hidden; nothing changes the pixels until
 *       the owner lays them down (gesso_flatten): each shown layer, in
 *       order, makes its adjusted value and blends it over the pixel at its
 *       opacity (GR3). Hidden layers are dropped with the rest.
 *   GM5 SAVED AS NEW. An edit is written whole as a new file of the Hoard
 *       (files.write, exclusive on every part), so it never lands on a file
 *       that is there, the original least of all.
 *   GM6 OVER THE ORIGINAL ONLY ON THE OWNER'S WORD. gesso_save_over writes
 *       over the file the image was opened from only when the owner has
 *       confirmed it; without that it is refused (ZXM_E_STATE) and nothing
 *       is written. */
#ifndef GESSO_MODEL_H
#define GESSO_MODEL_H

#include "gesso_rules.h"
#include "zxm.h"

#define GESSO_CHANNELS_MAX 3
#define GESSO_BYTES_MAX    ((uint32_t) GESSO_SIDE_MAX * GESSO_SIDE_MAX * GESSO_CHANNELS_MAX)
#define GESSO_HEAD_MAX     1024u /* a header, comments and all */
#define GESSO_FILE_MAX     (GESSO_HEAD_MAX + GESSO_BYTES_MAX)
#define GESSO_PATH_MAX     1024u /* zxvd's path limit */
#define GESSO_LAYERS_MAX   8u    /* GR7: places 0..7 */
#define GESSO_BINS_MAX     256u  /* one bin a level */

typedef struct {
    int32_t kind;    /* GESSO_LAYER_* */
    int32_t amount;  /* GR1's delta, GR2's gain or a grey level */
    int32_t opacity; /* 0..255 */
    bool visible;
} gesso_layer_t;

typedef struct {
    zxm_link_t *link;
    char space[ZXM_SPACE_MAX + 1];  /* where path and saved are; "" = home */
    char path[GESSO_PATH_MAX + 1];  /* the original; "" = a new canvas */
    char saved[GESSO_PATH_MAX + 1]; /* where the edit was last saved as new */
    int32_t width, height;          /* 0 = no image */
    int32_t channels;               /* 1 grey, 3 colour */
    uint8_t px[GESSO_BYTES_MAX];    /* rows top to bottom, channels interleaved */
    gesso_layer_t layer[GESSO_LAYERS_MAX];
    uint32_t nlayers;
    bool dirty;                    /* changed since it was opened or saved */
    int64_t held_dark, held_light; /* GM3: values the last edit left at 0, at 255 */
} gesso_t;

void gesso_init(gesso_t *g, zxm_link_t *link);
/* A blank canvas width by height of channels (1 or 3), every value grey. */
int gesso_new(gesso_t *g, int64_t width, int64_t height, int64_t channels, int64_t grey);

/* GM3: edits over the whole image. ZXM_E_STATE with no image, ZXM_E_ARG
 * for an amount outside its rule's domain. */
int gesso_brightness(gesso_t *g, int64_t delta);
int gesso_contrast(gesso_t *g, int64_t gain);
int gesso_wash(gesso_t *g, int64_t grey, int64_t alpha);
/* One brush dab at (x, y); color holds one value a channel. */
int gesso_dab(gesso_t *g, int64_t x, int64_t y, int64_t size, const uint8_t *color, int64_t opacity,
              int64_t hardness);

/* GR4 over one channel: the values in lo..hi. */
int gesso_range(const gesso_t *g, int64_t channel, int64_t lo, int64_t hi, int64_t *count);
/* A histogram of one channel in bins of width levels (1..256): bin b counts
 * the values in b * width .. b * width + width - 1 (GR4); *nbins is
 * ceil(256 / width). */
int gesso_histogram(const gesso_t *g, int64_t channel, int64_t width, int64_t *bins,
                    uint32_t *nbins);

/* GM4 */
int gesso_layer_add(gesso_t *g, int64_t kind, int64_t amount, int64_t opacity);
int gesso_layer_show(gesso_t *g, uint32_t place, bool visible);
int gesso_layer_drop(gesso_t *g, uint32_t place);
int gesso_flatten(gesso_t *g);

/* GM1, GM5, GM6. space: a space id from spaces.list, "" for home. */
int gesso_open(gesso_t *g, const char *space, const char *path);
int gesso_save_as(gesso_t *g, const char *path);
int gesso_save_over(gesso_t *g, bool owner_confirmed);

#endif
