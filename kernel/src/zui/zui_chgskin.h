/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_chgskin.h — the Chiglet's base schematic, and skins drawn on it.
 *
 * Every Chiglet has the same base schematic: a body of radius r at a centre,
 * its halo of time together and its pace motes (zui_chiglet.h), and a pose that says
 * what it is doing right now: its feeling as a face (zui_emote.h), how far its
 * mouth is open while it talks, where it is looking, and whether it is
 * blinking. A skin is the look drawn over that schematic. The person picks
 * one; their Chiglet keeps its genes, halo and behaviour whatever it wears.
 *
 *   SK1 A SKIN IS DATA, NEVER CODE. A skin is a .zxcs file: filled vector
 *       shapes (quadratic outlines in one box, +-2048 = +-2 r) with a layer
 *       order, a paint and one reaction each. Nothing in it runs, so a skin
 *       can come from anyone, including a visiting Chiglet over Web 4.
 *   SK2 FAIL CLOSED. zui_chg_skin_parse checks every byte: magic, version,
 *       counts and limits (96 shapes, 512 points a shape, 2048 in all), every
 *       enum, every coordinate, outline rules and printable text. Anything
 *       off and the skin is refused whole, never half drawn.
 *   SK3 GENES STILL SHOW. Paints can name the Chiglet's own colours (accent,
 *       body tint, hair, eyes, skin tone) instead of fixed ones, and shapes
 *       can be gated by its curiosity or specialty gene, so two people with
 *       the same skin still have Chiglets that look like themselves.
 *   SK4 IT REACTS. Shapes react to the pose: eyes open, blink and squint with
 *       the face, pupils follow the gaze, the mouth opens with the speech
 *       level and smiles or frowns with the feeling, brows tilt and lift,
 *       blush and tears fade in, the head turns a little toward what it looks
 *       at and breathes. With reduce motion there is no breathing and no
 *       blinking; it still looks and speaks.
 *   SK5 CREDITS TRAVEL WITH IT. A skin names itself, its author and its
 *       SPDX licence; the Chiglet's settings show them.
 *   SK6 DETERMINISTIC AND INTEGER. Same skin, genes, pose, place and size ->
 *       same pixels. Edges are anti-aliased with 4 x 16 sub-samples.
 *
 * NULL as a skin means "classic": zui_chiglet_draw, the look from its genes.
 * The built-in "Anime" skin (zui_chg_skin_anime) is itself a .zxcs file,
 * built by tools/chgskin/anime_skin.py, so it goes through the same parser.
 *
 * Not reentrant: one static work area (about 90 KB), like the rest of zui's
 * drawing, which runs on one thread.
 */
#ifndef ZXV_ZUI_CHGSKIN_H
#define ZXV_ZUI_CHGSKIN_H

#include <stdbool.h>
#include <stdint.h>
#include "zui_canvas.h"
#include "zui_chiglet.h"
#include "zui_emote.h"

#define ZUI_SKIN_MAGIC      "ZXCS"
#define ZUI_SKIN_VERSION    1u
#define ZUI_SKIN_MAX_SHAPES 96u
#define ZUI_SKIN_MAX_SHAPE  512u  /* points in one shape */
#define ZUI_SKIN_MAX_POINTS 2048u /* points in the skin  */
#define ZUI_SKIN_COORD      2048  /* |x|, |y| at most this */
#define ZUI_SKIN_SHAPE_B    16u   /* bytes per shape record */
#define ZUI_SKIN_POINT_B    5u    /* bytes per point        */

/* flags */
#define ZUI_SKIN_EXPRESSIVE 1u /* its face shows the feeling: no separate face badge */

/* Which part of the schematic a shape rides on. */
typedef enum {
    ZUI_SKG_BODY = 0, /* still                                     */
    ZUI_SKG_HEAD,     /* breathes, and turns a little with the gaze */
    ZUI_SKG_COUNT
} zui_skin_group_t;

/* What a shape does with the pose (amt and pivot are per shape). */
typedef enum {
    ZUI_SKR_NONE = 0,
    ZUI_SKR_EYE,   /* height x eye opening x (1 - blink), about the pivot; hidden while squinting */
    ZUI_SKR_PUPIL, /* as EYE, and moves with the gaze by amt skin units    */
    ZUI_SKR_SQUINT,      /* shown only while the face squints (happy arcs)        */
    ZUI_SKR_MOUTH_OPEN,  /* shown while the mouth is open; height by the opening   */
    ZUI_SKR_MOUTH_SMILE, /* shown while closed and smiling; height by the curve    */
    ZUI_SKR_MOUTH_FROWN, /* shown while closed and frowning; height by the curve   */
    ZUI_SKR_BROW,        /* tilts by brow_tilt (amt: + left brow, - right), lifts  */
    ZUI_SKR_BLUSH,       /* alpha x blush                                           */
    ZUI_SKR_TEAR,        /* alpha x tear                                            */
    ZUI_SKR_CURIOUS,     /* shown when the curiosity gene % 4 > amt                */
    ZUI_SKR_EMBLEM,      /* shown when the specialty gene % 4 == amt               */
    ZUI_SKR_COUNT
} zui_skin_react_t;

/* Where a shape's colour comes from. For every paint but FIXED, the rgb bytes
 * are a recipe: r darkens toward black by r/255, then g lightens toward white
 * by g/255, so a skin can shade the Chiglet's own colours. */
typedef enum {
    ZUI_SKP_FIXED = 0, /* the rgb as given                         */
    ZUI_SKP_ACCENT,    /* zui_chiglet_accent: its home emotion      */
    ZUI_SKP_BODY,      /* its body tint (warmth gene)               */
    ZUI_SKP_HAIR,      /* one of 8 hair colours from its genes      */
    ZUI_SKP_IRIS,      /* one of 6 eye colours from its genes       */
    ZUI_SKP_TONE,      /* one of 6 skin tones from its genes        */
    ZUI_SKP_INK,       /* ZUI_TEXT                                  */
    ZUI_SKP_GROUND,    /* ZUI_VOID                                  */
    ZUI_SKP_COUNT
} zui_skin_paint_t;

typedef enum {
    ZUI_SKIN_OK = 0,
    ZUI_SKIN_E_ARG = -1,
    ZUI_SKIN_E_MAGIC = -2,
    ZUI_SKIN_E_VERSION = -3,
    ZUI_SKIN_E_TRUNCATED = -4,
    ZUI_SKIN_E_TEXT = -5,
    ZUI_SKIN_E_LIMIT = -6,
    ZUI_SKIN_E_ENUM = -7,
    ZUI_SKIN_E_COORD = -8,
    ZUI_SKIN_E_OUTLINE = -9,
    ZUI_SKIN_E_TRAILING = -10,
    ZUI_SKIN_E_FLAGS = -11
} zui_skin_status_t;

/* A parsed skin. shapes/points point into the caller's bytes, which must
 * outlive it. */
typedef struct {
    char name[32];
    char author[64];
    char licence[32]; /* SPDX id */
    uint8_t flags;
    uint16_t nshapes, npoints;
    const uint8_t *shapes, *points;
} zui_chg_skin_t;

/* What the Chiglet is doing this frame. */
typedef struct {
    zui_face_t face; /* its feeling (zui_emote_now)                     */
    uint8_t mouth;   /* 0: the face's mouth; 1..255: the speech level    */
    int16_t gaze_x;  /* -256 left .. 256 right                           */
    int16_t gaze_y;  /* -256 up .. 256 down                              */
    uint16_t blink;  /* 0 open .. 256 shut                               */
    uint32_t tick;   /* frame clock (125 ms); 0 with reduce motion       */
} zui_chg_pose_t;

/* SK2: check and read a .zxcs file. On any error *out is zeroed. */
zui_skin_status_t zui_chg_skin_parse(zui_chg_skin_t *out, const uint8_t *b, uint32_t n);

/* The built-in Anime skin (parsed once; NULL only if its bytes were broken,
 * which the tests rule out). */
const zui_chg_skin_t *zui_chg_skin_anime(void);
/* Its .zxcs bytes, to share or save. */
const uint8_t *zui_chg_skin_anime_bytes(uint32_t *len);

/* A still pose from the Chiglet's own state: neutral face, an UNCERTAIN
 * mood raises a brow, SPEAKING opens the mouth, c->mouth is the level. */
void zui_chg_pose_from(zui_chg_pose_t *p, const zui_chiglet_t *c);

/* SK4: the blink at now_ms. A short blink every few seconds, each Chiglet on
 * its own rhythm from its genes; 0 with reduce motion. */
uint16_t zui_chg_blink(const zui_chiglet_t *c, uint32_t now_ms, bool reduce_motion);

/* SK4: the gaze toward a point (a pointer) from a body at (cx, cy), radius r.
 * Within r it is proportional; beyond, it is the full 256 that way. */
void zui_chg_gaze(int32_t cx, int32_t cy, int32_t r, int32_t px, int32_t py, int16_t *gx,
                  int16_t *gy);

/* Draw the Chiglet: halo, motes, then the skin (NULL: classic). */
void zui_chg_skin_draw(zui_surface_t *s, const zui_chg_skin_t *skin, const zui_chiglet_t *c,
                       const zui_chg_pose_t *p, int32_t cx, int32_t cy, int32_t r);

/* True when the skin draws its own expressive face (SK flag). */
bool zui_chg_skin_expressive(const zui_chg_skin_t *skin);

#endif /* ZXV_ZUI_CHGSKIN_H */
