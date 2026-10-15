/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxl_synth.h — the launch sounds, made live in integers.
 *
 * The owner (2026-10-12): each app's own begin / middle / end sound plays at
 * the speed of its real loading, not the speed it was recorded at. So nothing
 * is recorded: every cue is synthesised here, the moment zxl_launch says it
 * sounds, from the app's theme (key, octave, timbre, motif). It is the same
 * music as tools/launch/launch_sounds.py (the reference the mockups were made
 * with): the same notes, onsets, timbres, pans, echoes and fades, made with a
 * sine table, integer envelopes and an integer noise source instead of numpy.
 *
 *   BEGIN   the motif's short call over a low root (1.4 s)
 *   TICK    one note per lit tile, the k-th tile the k-th step up the app's
 *           major pentatonic, panned to where its tile sits (0.45 s)
 *   END     the motif's answer: an arpeggio up the chord, a held root and
 *           fifth, one bright note on top (1.9 s)
 * The six hand-made apps (Burrow, Flint, Commons, Cutwater, Murmuration,
 * Coffer) keep their own recipes: a knock on wood, a struck spark, a call and
 * answer, a projector's reel, a flock of wings, a key in a lock.
 *
 * Cues overlap freely (a tick ringing over the begin): the player mixes up to
 * ZXL_SYN_CUES at once and pulls frames as the audio device has room, so the
 * shell never blocks and no cue is ever stretched or squeezed: what changes
 * with the load's speed is WHEN each cue starts.
 *
 * Output: 48 kHz, stereo interleaved, signed 16-bit. Integer only,
 * freestanding, deterministic: the same theme and event give the same samples
 * on every machine. */
#ifndef ZXV_ZXL_SYNTH_H
#define ZXV_ZXL_SYNTH_H

#include <stdint.h>

#include "zxl_launch.h"

#define ZXL_SYN_RATE     48000u
#define ZXL_SYN_CUES     6   /* cues sounding at once                      */
#define ZXL_SYN_NOTES    96  /* voices in one cue (echoes included)        */
#define ZXL_SYN_PARTIALS 448 /* sine partials in one cue                   */
#define ZXL_SYN_BLOCK    128 /* frames mixed per pass                      */

typedef struct {
    uint32_t ph, inc; /* phase, phase step                               */
    int32_t amp;      /* Q14                                             */
    uint32_t att;     /* attack frames, 0 = none                         */
    uint32_t att_q30; /* attack ramp step per frame (Q30)                */
    uint32_t e, mul;  /* decay: level and per-frame multiplier (Q30)     */
    uint8_t dead;
} zxl_partial_t;

typedef struct {
    uint8_t kind; /* tone, noise or sweep                               */
    uint8_t hp;   /* noise: a click (high-passed)                       */
    uint8_t sustain;
    uint8_t dead;
    uint32_t start, dur; /* frames from the cue's start                  */
    uint32_t A, R;       /* sustained tone: attack and release frames      */
    int64_t gl, gr;      /* the voice's gain into left and right           */
    uint16_t p0, np;     /* tone: its partials in the cue's pool           */
    /* noise */
    uint32_t seed, alpha_q16, ea_q30;
    int32_t lp, prev, lp2;
    uint32_t e, mul, att;
    /* sweep: phase, step (Q16 beyond the phase's), glide, and the phase of
     * its half-sine envelope */
    uint32_t ph, eph, einc, glide_q30;
    uint64_t sinc;
} zxl_note_t;

typedef struct {
    uint8_t on;
    uint8_t kind; /* zxl_event_kind_t */
    uint8_t step, part;
    const zxl_theme_t *theme;
    uint32_t len, pos, fade; /* frames                                     */
    int64_t scale_q32;       /* normalises to the cue's peak, times gain    */
    uint32_t age;
    uint16_t nn, npart;
    zxl_note_t note[ZXL_SYN_NOTES];
    zxl_partial_t part_pool[ZXL_SYN_PARTIALS];
    /* while building: the echo being laid down */
    uint32_t b_delay;
    int32_t b_gain_q16;
    uint8_t b_mirror;
    uint16_t b_peak_q8; /* the recipe's peak, 0..256 of full scale     */
} zxl_cue_t;

#define ZXL_SYN_PEAKS 32 /* begin and end cues whose loudness is known */

typedef struct {
    const zxl_theme_t *theme;
    uint8_t kind;
    int64_t peak;
} zxl_peak_t;

typedef struct {
    zxl_cue_t cue[ZXL_SYN_CUES];
    uint32_t age;
    int64_t acc[ZXL_SYN_BLOCK * 2];
    /* Every cue is normalised to its own loudest moment, which takes one
     * silent pass through it. A tick's is quick; a begin's or an end's is
     * remembered here, and an end's is worked out in the background while
     * the app loads (zxl_player_prepare), so the end sounds the moment the
     * app is ready. */
    zxl_peak_t peaks[ZXL_SYN_PEAKS];
    uint32_t peak_next;
    zxl_cue_t prep;
    int64_t prep_peak;
} zxl_player_t;

void zxl_player_init(zxl_player_t *p);

/* Start one cue (BEGIN, TICK or END; anything else and silent events are
 * ignored). gain is ev->gain_q8. When all cues are busy the oldest stops.
 * Returns 0, or -1 when nothing starts. */
int zxl_player_play(zxl_player_t *p, const zxl_theme_t *t, const zxl_event_t *ev);

/* Work out the loudness of the theme's end (and begin) in the background,
 * a little with each zxl_player_render. zxl_player_play calls this itself
 * when a begin plays. */
void zxl_player_prepare(zxl_player_t *p, const zxl_theme_t *t);

/* Mix the next `frames` frames into lr (interleaved L, R). Always fills them
 * (silence when nothing sounds). Returns the cues still sounding after. */
uint32_t zxl_player_render(zxl_player_t *p, int16_t *lr, uint32_t frames);

/* Cues sounding now. */
uint32_t zxl_player_active(const zxl_player_t *p);

/* One cue's length in frames, for a theme and event kind (0 for others). */
uint32_t zxl_cue_frames(const zxl_theme_t *t, uint32_t kind);

#endif /* ZXV_ZXL_SYNTH_H */
