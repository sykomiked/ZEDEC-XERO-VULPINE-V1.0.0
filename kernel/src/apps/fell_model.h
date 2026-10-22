/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fell_model.h — Fell, search and rescue coordination, as a model (zxm.h):
 * the search grid and its sectors, the priority of each sector, the
 * sightings and the leads they give, the crews' shifts and check-ins,
 * callout claims and the casualty records, composed of its parts: Waymark
 * keeps the places (place.here, place.list, place.add), Aerie the radio
 * messages (ehop.bands, ehop.seal, ehop.open, harmonic.encode,
 * harmonic.decode, freight.pack), Commons the team room (chat.post,
 * chat.feed, pspace.post), and Watchfire's rules say when where the
 * command post is may be shared and whether a casualty's medical field
 * fits its record. The casualty records are kept in the Hoard
 * (files.read, files.write).
 *
 * Life-safety: advisory and human-approved. Every priority, lead and
 * overdue is a suggestion for the incident commander, who decides. Fell
 * never calls emergency services or anyone by itself, transmits nothing
 * (zxvd has no radio method: a sealed frame or a line frame is handed to
 * the view) and infers nothing clinical.
 *
 *   FM1 THE INCIDENT. Opened on the commander's word (owner_confirmed)
 *       with a number 1..FELL_INCIDENT_MAX; everything below waits for it.
 *   FM2 THE GRID. Its south-west corner is a place Waymark keeps (so
 *       waymark's own check has passed on it), with square cells of
 *       FELL_CELL_MIN..FELL_CELL_MAX millionths of a degree and
 *       1..FELL_COLS_MAX columns; set on the commander's word. A point's
 *       sector is FR3 (fell_rules.h).
 *   FM3 SECTORS. Up to FELL_SECTOR_MAX, each 1..FELL_SECTOR_ID_MAX with a
 *       probability of area and a searched coverage 0..10000; added and
 *       changed on the commander's word. fell_priorities runs FR1 over
 *       them, in the order they were added; a refusal (nothing likely
 *       left) leaves the last priorities as they were.
 *   FM4 SIGHTINGS. Up to FELL_SIGHTINGS, each in a kept sector with a
 *       stamp in seconds; a report, as the teams hand it in. Full is
 *       refused (ZXM_E_STATE), never overwritten. fell_leads joins the
 *       ones of the last FELL_SIGHT_WINDOW seconds (FR5 counts them) by
 *       FR8: every sector TRUE or BOTH is a lead, none dropped.
 *   FM5 CREWS. Up to FELL_CREWS, added on the commander's word with the
 *       minute their shift began; a check-in is the crew's own report,
 *       never before its last. fell_crew_watch raises every crew FR2 does
 *       not hold safe as overdue, for the commander.
 *   FM6 CLAIMS. Callout claims (FR6) are records for the owner to check
 *       and settle; Fell pays nothing. Kept on the owner's word, up to
 *       FELL_CLAIMS, their total refused past FELL_TOTAL_MAX.
 *   FM7 CASUALTIES. Up to FELL_CASUALTIES, each a record FR7 fits, in a
 *       kept sector, triage 0..4 as the team recorded it, and an optional
 *       medical field (Watchfire's fields 1..6, as typed, WF4 fits); kept
 *       on the commander's word. fell_save rewrites FELL_FILE in the Hoard
 *       whole on the owner's word; fell_load reads it back and changes
 *       the model only when the whole file is good.
 *   FM8 THE TEAM ROOM. Commons's: fell_room_feed reads chat.feed,
 *       fell_room_post posts (chat.post here, pspace.post in a space)
 *       only on the owner's word and when commons.room_post_ok holds.
 *       fell_share_post posts where the command post is to the room only
 *       when watchfire.place_share_ok holds: the incident open, the owner
 *       opted in, the crew on the roster, and a location zxvd holds fresh.
 *   FM9 RADIO. Aerie's sealed path for private bearers only (never
 *       amateur radio, AR5), on the bands the owner chose; line frames
 *       whose tag is aerie.frame_tag and split back by its inverse; long
 *       messages as freight. Composed, never transmitted.
 *
 * Every call that changes kept state takes owner_confirmed after its
 * argument checks: without it ZXM_E_STATE, nothing sent and nothing
 * changed. No drawing, no allocation, integers only. */
#ifndef FELL_MODEL_H
#define FELL_MODEL_H

#include "aerie_model.h"
#include "commons_model.h"
#include "fell_rules.h"
#include "watchfire_model.h"
#include "waymark_model.h"
#include "zxm.h"

#define FELL_NO_SERVICES                                                                           \
    "Fell's priorities and alerts are advice for the incident commander, who decides. Fell calls " \
    "nobody by itself and transmits nothing; call emergency services from a phone network."
#define FELL_FILE          "fell.txt"
#define FELL_INCIDENT_MAX  INT64_C(999999)        /* FR7: 6 digits */
#define FELL_SECTOR_ID_MAX INT64_C(9999)          /* FR7: 4 digits */
#define FELL_CASUALTY_MAX  INT64_C(999)           /* FR7: 3 digits */
#define FELL_STAMP_MAX     INT64_C(9999999999999) /* FR7: 13 digits of ms */
#define FELL_CREWS         16u
#define FELL_CREW_MAX      INT64_C(99)
#define FELL_SIGHTINGS     64u
#define FELL_CLAIMS        64u
#define FELL_TOTAL_MAX     INT64_C(999999999999) /* FM6: the claims' total */
#define FELL_CASUALTIES    32u
#define FELL_TRIAGE_MAX    INT64_C(4)
#define FELL_NOTE_MAX      WATCHFIRE_TEXT_MAX
#define FELL_NOTICE_MAX    1023u
#define FELL_FILE_MAX      16383u

typedef struct {
    int64_t id, poa, cov;
    int64_t priority;   /* FR1, the last fell_priorities */
    lpres_state_t seen; /* FR8, the last fell_leads */
} fell_sector_t;

typedef struct {
    int64_t id, start, last; /* minutes */
    bool overdue;            /* FR2, the last fell_crew_watch */
} fell_crew_t;

typedef struct {
    int64_t sector, stamp; /* seconds */
} fell_sighting_t;

typedef struct {
    int64_t crew, hours, rate, miles, permile, claim;
} fell_claim_t;

typedef struct {
    int64_t casualty, sector, triage, stamp, field; /* field 0: no medical note */
    char note[FELL_NOTE_MAX + 1];                   /* as typed */
} fell_casualty_t;

typedef struct {
    zxm_link_t *link;
    bool open; /* FM1 */
    int64_t incident;
    bool grid_set; /* FM2 */
    int32_t lat0, lon0;
    int64_t cell, cols;
    fell_sector_t sector[FELL_SECTOR_MAX]; /* FM3 */
    uint32_t nsector;
    bool ranked;
    fell_sighting_t sight[FELL_SIGHTINGS]; /* FM4 */
    uint32_t nsight;
    lpres_state_t lead;
    uint32_t nlead, recent;
    fell_crew_t crew[FELL_CREWS]; /* FM5 */
    uint32_t ncrew, overdue;
    fell_claim_t claim[FELL_CLAIMS]; /* FM6 */
    uint32_t nclaim;
    int64_t claims_total;
    fell_casualty_t cas[FELL_CASUALTIES]; /* FM7 */
    uint32_t ncas;
    bool share_optin; /* FM8 */
    waymark_t map;    /* the parts */
    aerie_t radio;
    commons_t room;
    char notice[FELL_NOTICE_MAX + 1];
    char file[FELL_FILE_MAX + 1];
} fell_t;

void fell_init(fell_t *f, zxm_link_t *link);

/* FM1, FM2, FM3 */
int fell_open_incident(fell_t *f, int64_t incident, bool owner_confirmed);
int fell_set_grid(fell_t *f, const char *corner, int64_t cell, int64_t cols, bool owner_confirmed);
int fell_sector_at(const fell_t *f, int32_t lat, int32_t lon, int64_t *sector);
int fell_add_sector(fell_t *f, int64_t sector, int64_t poa, bool owner_confirmed);
int fell_set_coverage(fell_t *f, int64_t sector, int64_t cov, bool owner_confirmed);
int fell_priorities(fell_t *f);
int32_t fell_find_sector(const fell_t *f, int64_t sector);
/* FR4: the kept sectors whose coverage is under bar */
uint32_t fell_unsearched_count(const fell_t *f, int64_t bar);

/* FM4 */
int fell_sighting(fell_t *f, int64_t sector, int64_t stamp);
int fell_sighting_at(fell_t *f, const char *place, int64_t stamp);
int fell_leads(fell_t *f, int64_t now);

/* FM5 */
int fell_add_crew(fell_t *f, int64_t crew, int64_t start, bool owner_confirmed);
int fell_check_in(fell_t *f, int64_t crew, int64_t now);
int fell_crew_watch(fell_t *f, int64_t now);

/* FM6 */
int fell_add_claim(fell_t *f, int64_t crew, int64_t hours, int64_t rate, int64_t miles,
                   int64_t permile, bool owner_confirmed);

/* FM7 */
int fell_add_casualty(fell_t *f, int64_t casualty, int64_t sector, int64_t triage, int64_t stamp,
                      int64_t field, const char *note, bool owner_confirmed);
int fell_save(fell_t *f, bool owner_confirmed);
int fell_load(fell_t *f);

/* Waymark's places: place.here and place.list, and place.add */
int fell_refresh_map(fell_t *f);
int fell_add_place(fell_t *f, const char *name, int32_t lat, int32_t lon, const char *note,
                   bool owner_confirmed);

/* FM8 */
int fell_room_feed(fell_t *f);
int fell_room_post(fell_t *f, uint32_t room, const char *text, bool owner_confirmed);
int fell_set_share(fell_t *f, bool on, bool owner_confirmed);
int fell_share_post(fell_t *f, int64_t crew, bool owner_confirmed);

/* FM9 */
int fell_radio_bands(fell_t *f, uint32_t mask, bool owner_confirmed);
int fell_radio_seal(fell_t *f, const aerie_chan_t *ch, const char *text, int64_t now_ms);
int fell_radio_open(fell_t *f, const aerie_chan_t *ch, const char *frame_hex, int64_t now_ms);
int fell_line_send(fell_t *f, int64_t sync, int64_t shell, int64_t trunk);
int fell_line_read(fell_t *f, const char *frame_hex, int64_t *sync, int64_t *shell, int64_t *trunk);
int fell_freight(fell_t *f, const char *text);

#endif
