/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_emote.h — live emoji faces that morph: the swarm's status bar.
 *
 * Every model in the swarm already carries a feeling (swarm/swarm_emotion.h):
 * one of eight emoji and a Fibonacci intensity 0..5 whose charge (0 1 2 3 5 8)
 * decides how many imaginary-axis tokens it gets and how they are spent. The
 * status bar shows exactly that feeling, live, as a face.
 *
 * WHY WE DRAW THE FACES OURSELVES
 * A colour-emoji font holds finished pictures; 😌 cannot slide into 🤔, it can
 * only be swapped or cross-faded. So zui draws each emoji as a face built from
 * a handful of numbers (mouth curve and opening, eye opening, smile squint,
 * brow tilt, one raised brow, blush, tear, colour). Every emotion is a point
 * in that space, so a change of feeling is a straight walk between two points
 * and every frame on the way is a real face. The emoji code point stays the
 * name of the feeling: it is what text, logs, exports and screen readers get.
 *
 * INTENSITY IS THE FIBONACCI CHARGE
 * The face is the neutral face moved toward the emotion's face by charge/8:
 * intensity 1 is a hint (1/8), 5 is the whole expression. What the person sees
 * is the same number that sets the agent's token share.
 *
 * HEALTH IS SEPARATE FROM FEELING
 * A ring around the face shows whether the agent is working at all: nothing
 * for OK, a slow dashed gold ring for SLOW (missed its cycle deadline), an
 * alert ring for FAILING (errors, failed self-checks), a faded face for
 * STOPPED. A frustrated agent that is running fine and a calm agent that has
 * stopped must never look alike.
 *
 * Deterministic and integer only: same faces, t and size -> same pixels.
 */
#ifndef ZXV_ZUI_EMOTE_H
#define ZXV_ZUI_EMOTE_H

#include <stdint.h>
#include "zui_canvas.h"
#include "swarm_emotion.h"
#include "lpres_hex.h"

/* A face. All shape fields are Q8 (256 = full); colour is XRGB. */
typedef struct {
    int16_t  mouth_curve;   /* -256 frown .. +256 smile                    */
    int16_t  mouth_open;    /*    0 closed .. 256 open                     */
    int16_t  eye_open;      /*    0 lids shut .. 256 wide                  */
    int16_t  squint;        /*    0 round eyes .. 256 smiling arcs  ^ ^    */
    int16_t  brow_tilt;     /* -256 inner ends down (anger) .. +256 up (worry) */
    int16_t  brow_raise;    /*    0 level .. 256 one brow raised (wondering) */
    int16_t  blush;         /*    0 .. 256                                 */
    int16_t  tear;          /*    0 .. 256                                 */
    uint32_t color;
} zui_face_t;

typedef enum {
    ZUI_HEALTH_OK = 0,
    ZUI_HEALTH_SLOW,
    ZUI_HEALTH_FAILING,
    ZUI_HEALTH_STOPPED
} zui_health_t;

/* A morphing face: where it was, where it is going, and when it set off. */
typedef struct {
    zui_face_t from, to;
    uint32_t   start_ms;
} zui_emote_t;

/* A live morph between two feelings takes this long. Status, not decoration:
 * like the live shimmer it is exempt from the 180 ms transition ceiling, and
 * with reduce-motion on it jumps straight to the new face. */
#define ZUI_EMOTE_MORPH_MS 420u

/* The full face of an emotion (intensity 5). Out of range -> neutral. */
zui_face_t zui_face_of(swarm_emotion_t e);

/* The face of a feeling: neutral moved toward the emotion by charge/8. */
zui_face_t zui_face_feeling(swarm_feeling_t f);

/* Straight walk between two faces, t in 0..256 (exact at both ends). */
zui_face_t zui_face_lerp(zui_face_t a, zui_face_t b, uint32_t t256);

/* Ease-in-out on 0..256 (smoothstep), exact at both ends. */
uint32_t zui_emote_ease(uint32_t t256);

/* Begin a morph toward a new feeling from whatever is on screen now. */
void zui_emote_set(zui_emote_t *m, swarm_feeling_t f, uint32_t now_ms);
void zui_emote_init(zui_emote_t *m, swarm_feeling_t f, uint32_t now_ms);
/* The face to draw at now_ms (reduce_motion: the destination at once). */
zui_face_t zui_emote_now(const zui_emote_t *m, uint32_t now_ms, int reduce_motion);

/* Draw a face centred at (cx, cy) with radius r pixels. */
void zui_face_draw(zui_surface_t *s, const zui_face_t *f, zui_health_t h,
                   int32_t cx, int32_t cy, int32_t r, uint32_t tick);

/* The logic tag a feeling shows with: positive valence TRUE, negative FALSE,
 * neutral NEITHER; intensity sets the phase (0 potential .. 5 manifest);
 * FAILING health is entropic, STOPPED is dissolving. Feeds zui_look(). */
lpx_t zui_emote_tag(swarm_feeling_t f, zui_health_t h);

/* Short word for the feeling ("calm", "curious"...). */
const char *zui_emote_word(swarm_emotion_t e);

#endif /* ZXV_ZUI_EMOTE_H */
