/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* watchfire_rules.h — Watchfire's rules in C, the reference its Eve, KRON,
 * Sutra and MYRA blocks are checked against (blocks/watchfire). Pure
 * integer functions, no state: the model (watchfire_model.c) and the
 * language test (tests/watchfire_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   WF1 SHARE WHERE THE MACHINE IS with one contact (place_share_ok) only
 *       when the person started the emergency, the owner opted in to
 *       sharing, the contact is on the emergency list and zxvd's place.ask
 *       allowed Watchfire: started = optin = listed = placeok = 1. Anything
 *       else, a flag that is no flag among it, is no.
 *   WF2 THE CANCEL WINDOW LEFT (cancel_left): seconds still to run before
 *       any ring, window - waited while waited < window, else 0. window
 *       0..99 and waited 0..WATCHFIRE_WAIT_MAX (Sutra's 9(2) and 9(9)),
 *       else WATCHFIRE_REFUSED.
 *   WF3 MAY WATCHFIRE RING NOW (sos_ring_ok): the owner's cancel
 *       window is WATCHFIRE_WINDOW_MIN..WATCHFIRE_WINDOW_MAX seconds (never
 *       skipped), none of it is left (left 0, WF2) and the person did not
 *       cancel (cancelled 0). Anything else is no.
 *   WF4 A MEDICAL ID RECORD of ^ZXWFMED holds integers of at most the
 *       digits its pieces declare, sign aside (blocks/watchfire/medical.myra):
 *       field 1, bytes 3, stamp 15 (ms since 1970).
 *   WF5 A CONTACT RECORD of ^ZXWFCON likewise (blocks/watchfire/contact.myra):
 *       seq 2, peer 10, bytes 2, stamp 15. */
#ifndef WATCHFIRE_RULES_H
#define WATCHFIRE_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define WATCHFIRE_REFUSED    (-1)
#define WATCHFIRE_WINDOW_MIN 10
#define WATCHFIRE_WINDOW_MAX 60
#define WATCHFIRE_PICTURE_2  99          /* Sutra's 9(2) */
#define WATCHFIRE_WAIT_MAX   999999999LL /* Sutra's 9(9) */

/* WF1 */
static inline bool watchfire_place_share_ok(int64_t started, int64_t optin, int64_t listed,
                                            int64_t placeok)
{
    return started == 1 && optin == 1 && listed == 1 && placeok == 1;
}

/* WF2 */
static inline int64_t watchfire_cancel_left(int64_t window, int64_t waited)
{
    if (window < 0 || window > WATCHFIRE_PICTURE_2 || waited < 0 || waited > WATCHFIRE_WAIT_MAX)
        return WATCHFIRE_REFUSED;
    return waited < window ? window - waited : 0;
}

/* WF3 */
static inline bool watchfire_window_ok(int64_t window)
{
    return window >= WATCHFIRE_WINDOW_MIN && window <= WATCHFIRE_WINDOW_MAX;
}

static inline bool watchfire_ring_ok(int64_t window, int64_t left, int64_t cancelled)
{
    return watchfire_window_ok(window) && left == 0 && cancelled == 0;
}

/* WF4, WF5: does x have at most `digits` digits, sign aside? */
static inline bool watchfire_digits_fit(int64_t x, uint32_t digits)
{
    int64_t lim = 1;
    for (uint32_t i = 0; i < digits && i < 18u; i++) lim *= 10;
    return x > -lim && x < lim;
}

static inline bool watchfire_medical_fits(int64_t field, int64_t bytes, int64_t stamp)
{
    return watchfire_digits_fit(field, 1) && watchfire_digits_fit(bytes, 3) &&
           watchfire_digits_fit(stamp, 15);
}

static inline bool watchfire_contact_fits(int64_t seq, int64_t peer, int64_t bytes, int64_t stamp)
{
    return watchfire_digits_fit(seq, 2) && watchfire_digits_fit(peer, 10) &&
           watchfire_digits_fit(bytes, 2) && watchfire_digits_fit(stamp, 15);
}

#endif
