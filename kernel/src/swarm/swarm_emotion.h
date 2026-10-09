/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_emotion.h — the emotional economy of the ZXV AI swarm.
 *
 * TWO AXES
 * --------
 * Every cycle's token budget is treated as a complex quantity. The REAL axis
 * is the logic of the swarm: the Fibonacci level rule of swarm_budget.h,
 * unchanged. The IMAGINARY axis, at a right angle to it, is the emotional
 * economy. Each model's allotment is a pair (re, im): re tokens it earns from
 * its place in the swarm, im tokens it earns from its emotional charge.
 * It may spend re + im tokens in the cycle.
 *
 * EMOTIONS ARE EMOJI
 * ------------------
 * An emotion is named by its emoji (a Unicode code point) from a fixed
 * palette. Each one carries a valence, the sign it takes on the imaginary
 * axis, matching the trit GLUT states (+ GLUT_PLUS, - GLUT_MINUS,
 * 0 GLUT_NEUTRAL), and a behaviour profile that sets HOW its tokens are
 * spent (sampling temperature, exploration, self-checking, priority).
 *
 * INTENSITY IS FIBONACCI
 * ----------------------
 * Intensity k runs 0 .. 5. Its charge is F(k+1) for k >= 1, else 0:
 *   k      0  1  2  3  4  5
 *   charge 0  1  2  3  5  8
 * The neutral emoji always has charge 0.
 *
 * THE RULES
 * ---------
 *   E1  MOOD SETS THE SIZE.  The swarm's cycle mood (an emotion + intensity)
 *       moves charge(k)/21 of the cycle total T onto the imaginary axis:
 *       at most 8/21, about 38%, close to 1/phi^2. The rest stays real.
 *   E2  LOGIC STAYS FIBONACCI.  The real part is split exactly by R1-R6
 *       of swarm_budget.h.
 *   E3  FEELING SETS THE SHARE.  The imaginary part is split across active
 *       models in proportion to each model's own charge, with the same
 *       exact largest-remainder rule (R5).
 *   E4  NOTHING IS STRANDED.  If no active model has a charge, the
 *       imaginary part folds back onto the real axis.
 *   E5  FEELING SETS THE BEHAVIOUR.  A model's emoji picks the behaviour
 *       profile its tokens run under (swarm_emotion_profile).
 *
 * Where emotions come from (user tone, each model's own task state) is the
 * caller's business; this module only turns them into tokens.
 * Freestanding: no libc, no allocation, no floating point.
 */
#ifndef SWARM_EMOTION_H
#define SWARM_EMOTION_H

#include <stdint.h>
#include <stdbool.h>
#include "swarm_budget.h"

#define SWARM_EMO_MAX_INTENSITY 5u
#define SWARM_EMO_CHARGE_DEN    21u /* F(8); max charge F(6) = 8 */

typedef enum {
    SWARM_EMO_NEUTRAL = 0, /* U+1F610 😐 */
    SWARM_EMO_CALM,        /* U+1F60C 😌 */
    SWARM_EMO_JOY,         /* U+1F604 😄 */
    SWARM_EMO_CURIOUS,     /* U+1F914 🤔 */
    SWARM_EMO_LOVE,        /* U+1F970 🥰 */
    SWARM_EMO_WORRY,       /* U+1F61F 😟 */
    SWARM_EMO_FRUSTRATION, /* U+1F620 😠 */
    SWARM_EMO_SADNESS,     /* U+1F622 😢 */
    SWARM_EMO_COUNT
} swarm_emotion_t;

/* How the tokens of an emotion behave (E5). All integers. */
typedef struct {
    uint32_t codepoint;         /* the emoji */
    int8_t valence;             /* +1, -1 or 0: GLUT_PLUS / GLUT_MINUS / GLUT_NEUTRAL */
    uint16_t temperature_milli; /* sampling temperature x 1000 */
    uint16_t explore_permille;  /* weight on novelty (Interaction Surplus) */
    uint8_t verify_passes;      /* self-check passes per answer */
    uint8_t priority;           /* scheduling priority, higher runs first */
} swarm_emotion_profile_t;

typedef struct {
    swarm_emotion_t emotion;
    uint8_t intensity; /* 0 .. SWARM_EMO_MAX_INTENSITY */
} swarm_feeling_t;

typedef struct {
    swarm_feeling_t mood;                /* E1, the whole swarm */
    uint32_t model_id[SWARM_MAX_MODELS]; /* E3, per model */
    swarm_feeling_t feeling[SWARM_MAX_MODELS];
    uint32_t num;
    uint64_t last_imag_pool; /* imaginary tokens last cycle */
} swarm_emotion_state_t;

/* Charge of intensity k: 0, 1, 2, 3, 5, 8. 0 if k is out of range. */
uint64_t swarm_emotion_charge(uint8_t intensity);

/* Charge of a feeling: 0 for neutral, else swarm_emotion_charge. */
uint64_t swarm_feeling_charge(swarm_feeling_t f);

/* Behaviour profile of an emotion; NULL if out of range. */
const swarm_emotion_profile_t *swarm_emotion_profile(swarm_emotion_t e);

/* Emotion for an emoji code point. SWARM_ERR_ARG if it is not in the palette. */
swarm_status_t swarm_emotion_from_codepoint(uint32_t codepoint, swarm_emotion_t *out);

void swarm_emotion_init(swarm_emotion_state_t *s);
swarm_status_t swarm_emotion_set_mood(swarm_emotion_state_t *s, swarm_feeling_t mood);
swarm_status_t swarm_emotion_set_feeling(swarm_emotion_state_t *s, uint32_t model_id,
                                         swarm_feeling_t f);

/* Imaginary pool for a cycle total under a mood (E1). */
uint64_t swarm_emotion_imag_pool(uint64_t total, swarm_feeling_t mood);

/* E1 + E4: the imaginary pool this cycle would get (0 if no active model
 * has a charge). */
uint64_t swarm_emotion_pool(const swarm_budget_t *b, const swarm_emotion_state_t *s);

/* E3: add `imag` imaginary tokens to the open cycle by each model's charge. */
void swarm_emotion_apply(swarm_budget_t *b, swarm_emotion_state_t *s, uint64_t imag);

/* Open a cycle on both axes (E1-E4). Each slot's allotted becomes re + im,
 * with the imaginary part in allotted_im. */
swarm_status_t swarm_emotion_begin_cycle(swarm_budget_t *b, swarm_emotion_state_t *s);

#endif /* SWARM_EMOTION_H */
