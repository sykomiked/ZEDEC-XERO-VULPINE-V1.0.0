/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* conch_rules.h — Conch's rules in C, the reference its Eve, KRON and MYRA
 * blocks are checked against (blocks/conch). Pure integer functions, no
 * state: the model (conch_model.c) and the language test
 * (tests/conch_zxapp.inc) both include this, so the languages are tested
 * against the very C the model runs. Advisory only: this is not a
 * clinical fitting, and the cap is a safety ceiling the owner sets.
 *
 *   CN1 OUTPUT LEVEL (hearing_level_ok). One band's gain in tenths of a
 *       dB, -300..300 (a cut or a boost of at most 30 dB), the level that
 *       band would play (total, tenths of a dB SPL) and the owner's cap
 *       (tenths of a dB SPL), each 0..1200 (120 dB SPL), and the total no
 *       louder than the cap. Eve holds each range alone; KRON also holds
 *       the total under the cap, a relation across fields Eve cannot say,
 *       so a total past the cap is held (BOTH), never played.
 *   CN2 BALANCE (hearing_balance_ok). balance -100 (all to the left ear)
 *       .. 100 (all to the right), 0 even; mono 0 (stereo) or 1 (both
 *       ears hear one mix).
 *   CN3 ALERT MAP (sound_alert_map_ok). How a sound kind is shown: sound
 *       1..CONCH_SOUNDS, flash 0..3 pulses of the screen and vibrate 0..3
 *       pulses. Pulses are CONCH_PULSE_MS apart, so a screen never flashes
 *       more than three times in any one second (the photosensitive
 *       limit). The ^ZXCONCH record holds one digit each, sign aside
 *       (MYRA); Eve's ranges are these, narrower, so a pulse count of 4..9
 *       or a sound the app does not have is held.
 *   CN4 EARS. What each ear plays of a band's level, in percent: the ear
 *       the balance leans away from plays 100 - |balance|, the other 100.
 *       Mono changes what is mixed, not how loud. */
#ifndef CONCH_RULES_H
#define CONCH_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define CONCH_GAIN_MIN    INT64_C(-300) /* CN1: tenths of a dB */
#define CONCH_GAIN_MAX    INT64_C(300)
#define CONCH_SPL_MAX     INT64_C(1200) /* CN1: tenths of a dB SPL, 120 dB SPL */
#define CONCH_BALANCE_MAX INT64_C(100)  /* CN2: percent either way */
#define CONCH_SOUNDS      INT64_C(5)    /* CN3: sound kinds, 1..5 */
#define CONCH_PULSES_MAX  INT64_C(3)    /* CN3: flash or vibrate pulses */
#define CONCH_PULSE_MS    INT64_C(334)  /* CN3: three pulses span 668 ms, under a second */

enum {
    CONCH_SOUND_CALL = 1,    /* call-incoming, call-missed */
    CONCH_SOUND_MESSAGE = 2, /* message, peer-request */
    CONCH_SOUND_ALARM = 3,   /* any urgent notice */
    CONCH_SOUND_TASK = 4,    /* ai-task-done, ai-task-failed */
    CONCH_SOUND_OTHER = 5    /* economy, update, any other kind */
};

enum { CONCH_LEFT = 0, CONCH_RIGHT = 1, CONCH_BOTH_EARS = 2, CONCH_SIDES = 3 };

/* CN1: one band's gain and level against the owner's cap. */
static inline bool conch_level_ok(int64_t gain, int64_t total, int64_t cap)
{
    return gain >= CONCH_GAIN_MIN && gain <= CONCH_GAIN_MAX && total >= 0 &&
           total <= CONCH_SPL_MAX && cap >= 0 && cap <= CONCH_SPL_MAX && total <= cap;
}

/* CN2 */
static inline bool conch_balance_ok(int64_t balance, int64_t mono)
{
    return balance >= -CONCH_BALANCE_MAX && balance <= CONCH_BALANCE_MAX && mono >= 0 && mono <= 1;
}

/* CN3: Eve's ranges. */
static inline bool conch_alert_map_ok(int64_t sound, int64_t flash, int64_t vibrate)
{
    return sound >= 1 && sound <= CONCH_SOUNDS && flash >= 0 && flash <= CONCH_PULSES_MAX &&
           vibrate >= 0 && vibrate <= CONCH_PULSES_MAX;
}

/* CN3: MYRA's digits, one each, sign aside. */
static inline bool conch_alert_digits_ok(int64_t sound, int64_t flash, int64_t vibrate)
{
    return sound > -10 && sound < 10 && flash > -10 && flash < 10 && vibrate > -10 && vibrate < 10;
}

/* CN4: percent of a band's level one ear plays; -1 for a balance or side
 * outside CN2. CONCH_BOTH_EARS answers the quieter of the two. */
static inline int64_t conch_ear_pct(int64_t balance, int64_t side)
{
    if (!conch_balance_ok(balance, 0) || side < CONCH_LEFT || side >= CONCH_SIDES) return -1;
    int64_t left = balance > 0 ? CONCH_BALANCE_MAX - balance : CONCH_BALANCE_MAX;
    int64_t right = balance < 0 ? CONCH_BALANCE_MAX + balance : CONCH_BALANCE_MAX;
    if (side == CONCH_LEFT) return left;
    if (side == CONCH_RIGHT) return right;
    return left < right ? left : right;
}

#endif
