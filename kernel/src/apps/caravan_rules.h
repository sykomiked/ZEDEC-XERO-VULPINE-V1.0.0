/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* caravan_rules.h — Caravan's rules in C, the reference its Sutra, Trebago,
 * Eve, KRON and MYRA blocks are checked against (blocks/caravan). Pure
 * integer functions, no state: the model (caravan_model.c) and the
 * language test (tests/caravan_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 * Units: masses and charged weights in grams, sides in millimetres, the
 * volume divisor in cubic millimetres per gram (5000 is the usual 5000
 * cubic centimetres per kilogram).
 *
 *   CR1 CHARGED WEIGHT. max(mass, ceil(length * width * height / divisor)):
 *       mass 0..CARAVAN_GRAMS_MAX, each side 0..CARAVAN_SIDE_MAX, divisor
 *       1..CARAVAN_DIVISOR_MAX (Sutra's pictures), the answer at most
 *       CARAVAN_GRAMS_MAX; anything else is refused (CARAVAN_REFUSED).
 *   CR2 GIRTH. The way round a parcel across its two shorter sides,
 *       2 * (width + height), each side 0..CARAVAN_SIDE_MAX, else refused.
 *   CR3 WITHIN A CARRIER'S LIMITS. Every margin, the carrier's limit minus
 *       the parcel's measure, at least 0 and inside its range: mass
 *       0..CARAVAN_GRAMS_MAX, the longest side 0..CARAVAN_SIDE_MAX, length
 *       plus girth 0..CARAVAN_LG_MAX (the most five sides make). A margin
 *       below 0 is over the limit; one past its range is no measurement.
 *   CR4 WITHIN LIMITS AS A NUMBER: 1 when CR3 holds, else 0, to count with.
 *   CR5 A SHIPMENT RECORD. ship 1..CARAVAN_SHIP_MAX, carrier
 *       1..CARAVAN_CARRIER_MAX, mass and charged 0..CARAVAN_GRAMS_MAX, a
 *       known status (CARAVAN_DRAFTED..CARAVAN_RETURNED) and a stamp of
 *       0..CARAVAN_STAMP_MAX seconds since 1970. */
#ifndef CARAVAN_RULES_H
#define CARAVAN_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define CARAVAN_REFUSED     (-1)
#define CARAVAN_GRAMS_MAX   999999999 /* Sutra's 9(9) */
#define CARAVAN_SIDE_MAX    99999     /* 9(5) mm, nearly 100 m */
#define CARAVAN_DIVISOR_MAX 99999
#define CARAVAN_LG_MAX      499995 /* length + 2 * (width + height), each 99999 */
#define CARAVAN_SHIP_MAX    999999
#define CARAVAN_CARRIER_MAX 999
#define CARAVAN_STAMP_MAX   9999999999

enum {
    CARAVAN_DRAFTED = 0,   /* written down, not labelled */
    CARAVAN_LABELLED = 1,  /* a label made, waiting to be handed over */
    CARAVAN_HANDED = 2,    /* given to the carrier */
    CARAVAN_TRANSIT = 3,   /* on its way */
    CARAVAN_DELIVERED = 4, /* arrived */
    CARAVAN_RETURNED = 5,  /* came back */
};

/* CR1 */
static inline int64_t caravan_charged(int64_t mass, int64_t length, int64_t width, int64_t height,
                                      int64_t divisor)
{
    if (mass < 0 || mass > CARAVAN_GRAMS_MAX || divisor < 1 || divisor > CARAVAN_DIVISOR_MAX)
        return CARAVAN_REFUSED;
    if (length < 0 || length > CARAVAN_SIDE_MAX || width < 0 || width > CARAVAN_SIDE_MAX ||
        height < 0 || height > CARAVAN_SIDE_MAX)
        return CARAVAN_REFUSED;
    int64_t volume = length * width * height; /* at most 99999^3, inside 64 bits */
    int64_t weight = (volume + divisor - 1) / divisor;
    int64_t charged = weight > mass ? weight : mass;
    return charged > CARAVAN_GRAMS_MAX ? CARAVAN_REFUSED : charged;
}

/* CR2 */
static inline int64_t caravan_girth(int64_t width, int64_t height)
{
    if (width < 0 || width > CARAVAN_SIDE_MAX || height < 0 || height > CARAVAN_SIDE_MAX)
        return CARAVAN_REFUSED;
    return 2 * (width + height);
}

/* CR3 */
static inline bool caravan_fits(int64_t massleft, int64_t lengthleft, int64_t girthleft)
{
    return massleft >= 0 && massleft <= CARAVAN_GRAMS_MAX && lengthleft >= 0 &&
           lengthleft <= CARAVAN_SIDE_MAX && girthleft >= 0 && girthleft <= CARAVAN_LG_MAX;
}

/* CR4 */
static inline int64_t caravan_fits_n(int64_t massleft, int64_t lengthleft, int64_t girthleft)
{
    return caravan_fits(massleft, lengthleft, girthleft) ? 1 : 0;
}

/* CR5 */
static inline bool caravan_ship_ok(int64_t ship, int64_t carrier, int64_t mass, int64_t charged,
                                   int64_t status, int64_t stamp)
{
    return ship >= 1 && ship <= CARAVAN_SHIP_MAX && carrier >= 1 &&
           carrier <= CARAVAN_CARRIER_MAX && mass >= 0 && mass <= CARAVAN_GRAMS_MAX &&
           charged >= 0 && charged <= CARAVAN_GRAMS_MAX && status >= CARAVAN_DRAFTED &&
           status <= CARAVAN_RETURNED && stamp >= 0 && stamp <= CARAVAN_STAMP_MAX;
}

#endif
