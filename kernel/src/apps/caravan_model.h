/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* caravan_model.h — Caravan, freight and shipments, as a model (zxm.h):
 * the carriers the owner keeps with their limits, parcels with their mass
 * and sides, and shipments addressed to the places zxvd keeps.
 *
 * zxvd has no shipment, carrier, label or tracking method, and none is
 * used. Addresses are zxvd's places (place.list); the carriers and the
 * shipments are one UTF-8 text file in the Banker, CARAVAN_FILE, read with
 * files.read and written whole with files.write. zxvd has no notification
 * method either, so the model only composes each notice's text
 * (caravan_t.notice) for the view to show. No drawing, no allocation,
 * integers only; the rules are caravan_rules.h.
 *
 *   CM1 The file's first line is "caravan 1".
 *   CM2 Then one line per carrier, "carrier <id> <mass max g> <length max
 *       mm> <length plus girth max mm> <divisor> <quickest h> <slowest h>",
 *       ids rising.
 *   CM3 Then one line per shipment, "ship <id> <carrier> <mass g> <side
 *       mm> <side mm> <side mm> <status> <stamp> <place>", ids rising; the
 *       sides in the order given, the place a name zxvd kept (1 to 63
 *       bytes, no tab or line break) to the end of the line.
 *   CM4 Every line ends with "\n", numbers are plain decimal digits with
 *       no leading zero. Anything else is not Caravan's file: the load is
 *       refused (ZXM_E_PROTOCOL) and every change is refused (ZXM_E_STATE)
 *       until a load succeeds, so a file the model cannot read is never
 *       overwritten.
 *   CM5 Changes are checked, written whole, and only then kept: a write
 *       zxvd refuses leaves the model as it was. A shipment is added only
 *       to a place zxvd keeps (caravan_places first), with a carrier the
 *       owner keeps, and inside that carrier's limits (CR3); over a limit
 *       it is refused and the notice says which limit and by how much.
 *   CM6 Which carrier to take is advice, never a choice made for the
 *       owner. caravan_route puts the question in pce_strat's contract
 *       (pce_entry.c strat_of): one option per carrier whose limits the
 *       parcel fits, its row of payoffs minus its quickest and minus its
 *       slowest transit in hours, both scenarios possible (lo 0, hi 1, den
 *       1). caravan_choose changes a drafted shipment's carrier only with
 *       the owner's confirmation.
 *   CM7 A shipment moves forward only: drafted, labelled, handed over, in
 *       transit, delivered; returned from handed over or in transit; its
 *       stamp never goes back. Only a drafted shipment is removed. */
#ifndef CARAVAN_MODEL_H
#define CARAVAN_MODEL_H

#include "caravan_rules.h"
#include "zxm.h"

#define CARAVAN_FILE       "Caravan.ships" /* in the Banker's home */
#define CARAVAN_CARRIERS   16u
#define CARAVAN_SHIPS      256u
#define CARAVAN_PLACES     64u
#define CARAVAN_PLACE_MAX  63u    /* bytes of a place's name (place.add) */
#define CARAVAN_TEXT_MAX   32768u /* the whole file, every line full */
#define CARAVAN_NOTICE_MAX 511u
#define CARAVAN_HOURS_MAX  9999     /* a transit of more than a year is no estimate */
#define CARAVAN_DIVISOR    5000     /* cubic mm per gram: 5000 cubic cm per kg */
#define CARAVAN_E_FILES    (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */

typedef struct {
    int32_t id;         /* 1..CARAVAN_CARRIER_MAX */
    int64_t mass_max;   /* g, 1..CARAVAN_GRAMS_MAX */
    int64_t length_max; /* the longest side, mm, 1..CARAVAN_SIDE_MAX */
    int64_t lg_max;     /* length plus girth, mm, 1..CARAVAN_LG_MAX */
    int64_t divisor;    /* cubic mm per gram, 1..CARAVAN_DIVISOR_MAX */
    int32_t quick;      /* hours, 1..slow */
    int32_t slow;       /* hours, quick..CARAVAN_HOURS_MAX */
} caravan_carrier_t;

/* A parcel as a carrier measures it (caravan_parcel). */
typedef struct {
    int64_t mass;                  /* g */
    int64_t length, width, height; /* mm, longest first */
    int64_t girth;                 /* CR2 of width and height */
    int64_t lg;                    /* length plus girth */
    int64_t charged;               /* CR1 at the carrier's divisor */
} caravan_parcel_t;

/* How far a parcel is inside a carrier's limits: each limit minus the
 * parcel's measure (CR3's margins; below 0 is over). */
typedef struct {
    int64_t mass, length, lg;
} caravan_margin_t;

typedef struct {
    int32_t id;      /* 1..CARAVAN_SHIP_MAX */
    int32_t carrier; /* a kept carrier's id */
    int32_t status;  /* CARAVAN_DRAFTED..CARAVAN_RETURNED */
    int64_t mass;    /* g */
    int64_t side[3]; /* mm, as given */
    int64_t stamp;   /* seconds since 1970, of the last change */
    char to[CARAVAN_PLACE_MAX + 1];
} caravan_ship_t;

typedef struct {
    char name[CARAVAN_PLACE_MAX + 1];
    int64_t distance_m; /* -1 when zxvd does not know where it is */
} caravan_place_t;

/* CM6: pce_strat's arguments, payoff a row per option. */
typedef struct {
    int64_t payoff[CARAVAN_CARRIERS * 2u];
    int64_t nopt;
    int64_t lo[2], hi[2], den;
    int32_t carrier[CARAVAN_CARRIERS]; /* option k is carrier[k] */
} caravan_route_t;

typedef struct {
    zxm_link_t *link;
    bool loaded; /* a load succeeded: changes may be written (CM4) */
    caravan_carrier_t carrier[CARAVAN_CARRIERS];
    uint32_t ncarriers;
    caravan_ship_t ship[CARAVAN_SHIPS];
    uint32_t nships;
    caravan_place_t place[CARAVAN_PLACES];
    uint32_t nplaces;
    char notice[CARAVAN_NOTICE_MAX + 1];
} caravan_t;

/* A parcel of mass g and three sides in mm, in any order, measured at a
 * divisor: ZXM_E_ARG when a value is outside CR1's domain or the charged
 * weight passes CARAVAN_GRAMS_MAX. */
int caravan_parcel(int64_t mass, int64_t a, int64_t b, int64_t c, int64_t divisor,
                   caravan_parcel_t *p);
/* CR3's margins of a parcel against a carrier, and whether it fits. */
caravan_margin_t caravan_margins(const caravan_carrier_t *k, const caravan_parcel_t *p);
bool caravan_within(const caravan_carrier_t *k, const caravan_parcel_t *p);
/* A carrier the owner may keep (CM2's ranges). */
bool caravan_carrier_ok(const caravan_carrier_t *k);

void caravan_init(caravan_t *c, zxm_link_t *link);
/* Read CARAVAN_FILE; a file not made yet is an empty one (ZXM_OK). */
int caravan_load(caravan_t *c);
/* The places zxvd keeps (place.list), nearest first when it knows. */
int caravan_places(caravan_t *c);
const caravan_carrier_t *caravan_find_carrier(const caravan_t *c, int64_t id);
const caravan_ship_t *caravan_find_ship(const caravan_t *c, int64_t id);

/* Keep a carrier, or change the one with its id. */
int caravan_set_carrier(caravan_t *c, const caravan_carrier_t *k);
/* A new drafted shipment (CM5); its id is the last one plus one. */
int caravan_add(caravan_t *c, int64_t carrier, int64_t mass, int64_t a, int64_t b, int64_t s,
                const char *to, int64_t stamp, int64_t *id);
/* Move a shipment on (CM7). */
int caravan_advance(caravan_t *c, int64_t id, int64_t status, int64_t stamp);
/* Remove a drafted shipment (CM7). */
int caravan_remove(caravan_t *c, int64_t id);
/* The route question for a shipment (CM6); ZXM_E_STATE when no carrier fits. */
int caravan_route(const caravan_t *c, int64_t id, caravan_route_t *r);
/* The owner's choice of carrier for a drafted shipment (CM6). */
int caravan_choose(caravan_t *c, int64_t id, int64_t carrier, bool confirmed);
/* A shipment's notice text, in c->notice. */
int caravan_notice(caravan_t *c, int64_t id);

#endif
