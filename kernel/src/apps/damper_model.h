/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* damper_model.h — Damper, the fire and power watch, as a model (zxm.h):
 * heat and smoke readings room by room, the alarm and the way out of the
 * building, the machine's own power and battery heat, zxvd's warnings,
 * and the evacuation contacts rung on the person's request. Composed of
 * its parts: Wake follows and counts zxvd's warnings (log.tail) and reads
 * the journal (journal.query); Brazier reads sys.info and power.status and
 * judges the battery's heat; Homestead's rules file a heat source and
 * total its energy; Watchfire keeps the contacts, the cancel window and
 * the calls (phone.list, call.start, place.ask, place.here); Conch's rules
 * hold the alarm's flash pattern and its tone under the owner's cap.
 *
 * Life-safety: advisory and human-approved. Damper never replaces a
 * certified smoke alarm, never calls emergency services or anyone by
 * itself, and never skips Watchfire's cancel window.
 *
 *   DM1 THE BUILDING. Rooms are a tree (DR5): a room's toward is 0 (it
 *       opens outside, so it has an exit) or a room kept before it, so
 *       the way out from any room ends outside in at most DAMPER_ROOMS
 *       steps. Added on the owner's word (owner_confirmed): without it
 *       ZXM_E_STATE and nothing changes.
 *   DM2 SENSORS. A sensor 1..DAMPER_SENSOR_MAX sits in a kept room with
 *       limits that fit DR3; the owner's word, as DM1.
 *   DM3 READINGS. zxvd has no method for home heat and smoke sensors, so
 *       readings are handed in (by the person, or a sensor bridge): a
 *       stamp in seconds never before the sensor's last, a temperature
 *       DAMPER_TEMP_MIN..MAX and smoke 1, 0 or DAMPER_SMOKE_NONE. Each
 *       sensor keeps its last DAMPER_WINDOW readings, a window that slides
 *       (the oldest leaves when a new one comes), never a queue to drain.
 *   DM4 THE ALARM (damper_assess). Of each sensor's latest second: DR6's
 *       joins, its margin, smoke and split, and DR1 quiet or not; the
 *       building's fire state, DR6. Raised when any sensor with readings
 *       is not quiet. Each sensor's rise against its rate (DR2) over its
 *       window is advice. The sensor grid (DR4) counts each room's raised
 *       sensors. The notice names them, the way out and the heat sources
 *       in their rooms; the alarm's pattern is Conch's.
 *   DM5 THE WAY OUT. From a room along toward to outside; a step through
 *       a room whose sensors are raised is counted, so the view can say
 *       to choose another way.
 *   DM6 HEAT SOURCES. A plug, a heater or an oven of Homestead's kinds,
 *       filed by homestead.home_device_ok in a kept room (the owner's
 *       word). Its energy readings (watt-hours) sit in a bounded list of
 *       DAMPER_ENERGY: full is refused (ZXM_E_STATE), never overwritten;
 *       a source's total is homestead.device_energy. Damper sends no
 *       command to a device: turning one off is the person's.
 *   DM7 POWER AND WARNINGS. brazier_refresh (sys.info, power.status): the
 *       battery's heat (brazier.heat_ok, heat_safe) and its minutes on
 *       battery (brazier.minutes_left). wake_follow (log.tail) and Wake's
 *       filter count the warnings and errors (wake.count); wake_history
 *       reads the journal (journal.query).
 *   DM8 CONTACTS. Watchfire's: added and its window and sharing set on
 *       the owner's word; on the person's own request (warren's owner and
 *       yes) the countdown starts, and only when the window has run out
 *       are they rung (call.start); where the machine is is shared only
 *       as WF1 allows. phone.list and place.here are read only.
 *   DM9 STANDING DOWN. On the owner's word, each line Watchfire rang is
 *       hung up (call.hangup) and the emergency ends; without it
 *       ZXM_E_STATE and nothing is sent.
 *
 * zxvd has no notification method: the notice is held in `notice` for the
 * view. No drawing, no allocation, integers only; the rules are
 * damper_rules.h. Settings are kept in the model only: Damper's needs
 * carry no files capability, so nothing is written to the Hoard. */
#ifndef DAMPER_MODEL_H
#define DAMPER_MODEL_H

#include "brazier_model.h"
#include "conch_rules.h"
#include "damper_rules.h"
#include "homestead_rules.h"
#include "wake_model.h"
#include "watchfire_model.h"
#include "zxm.h"

#define DAMPER_NOT_A_SMOKE_ALARM                                                                   \
    "Damper is not a certified smoke alarm and calls nobody by itself; call emergency services "   \
    "from a phone network."
#define DAMPER_ROOMS      32u
#define DAMPER_SENSORS    32u
#define DAMPER_SOURCES    16u
#define DAMPER_WINDOW     8u /* DM3: readings a sensor keeps */
#define DAMPER_ENERGY     64u
#define DAMPER_NOTICE_MAX 767u
#define DAMPER_ROOM_MAX   INT64_C(999) /* DR5: 3 digits */
#define DAMPER_FLOOR_MIN  INT64_C(-9)
#define DAMPER_FLOOR_MAX  INT64_C(99)
#define DAMPER_EXITS_MAX  INT64_C(9)
#define DAMPER_SENSOR_MAX INT64_C(999)
#define DAMPER_WH_MAX     INT64_C(999999999) /* one energy reading */

typedef struct {
    int64_t floor, room, toward, exits;
} damper_room_t;

typedef struct {
    int64_t id, room, limit, rate;
    int64_t temp[DAMPER_WINDOW], stamp[DAMPER_WINDOW], smoke[DAMPER_WINDOW];
    uint32_t n; /* readings kept, oldest first */
    /* DM4, the last assess */
    lpres_state_t fire; /* DR6: NEITHER when it has no reading */
    int64_t margin, has_smoke, split;
    bool quiet;          /* DR1 */
    bool rising;         /* DR2: climbing faster than its rate */
    int64_t room_raised; /* DR4: raised sensors in its room */
} damper_sensor_t;

typedef struct {
    int64_t device, kind, room;
} damper_source_t;

typedef struct {
    zxm_link_t *link;
    damper_room_t room[DAMPER_ROOMS];
    uint32_t nroom;
    damper_sensor_t sensor[DAMPER_SENSORS];
    uint32_t nsensor;
    damper_source_t source[DAMPER_SOURCES];
    uint32_t nsource;
    int64_t en_device[DAMPER_ENERGY], en_wh[DAMPER_ENERGY];
    uint32_t nen;
    int64_t flash, vibrate;   /* the alarm's pattern (conch.sound_alert_map_ok) */
    bool has_tone;            /* a tone for a hearing device is set */
    int64_t gain, total, cap; /* conch.hearing_level_ok */
    lpres_state_t fire;       /* DM4: the building's */
    bool raised;              /* DM4 */
    uint32_t raised_n, rising_n;
    uint32_t warnings;    /* DM7: wake.count of warnings and errors */
    brazier_t power;      /* DM7 */
    wake_t wake;          /* DM7 */
    watchfire_t contacts; /* DM8 */
    char notice[DAMPER_NOTICE_MAX + 1];
} damper_t;

void damper_init(damper_t *d, zxm_link_t *link);

/* DM1, DM2, DM6: the owner's word on every one */
int damper_add_room(damper_t *d, int64_t floor, int64_t room, int64_t toward, int64_t exits,
                    bool owner_confirmed);
int damper_add_sensor(damper_t *d, int64_t sensor, int64_t room, int64_t limit, int64_t rate,
                      bool owner_confirmed);
int damper_add_source(damper_t *d, int64_t device, int64_t kind, int64_t lo, int64_t hi,
                      int64_t setpoint, int64_t room, bool owner_confirmed);
/* DM4: the alarm's flash and vibrate pulses, and its tone (Conch's rules) */
int damper_set_alert(damper_t *d, int64_t flash, int64_t vibrate, bool owner_confirmed);
int damper_set_tone(damper_t *d, int64_t gain, int64_t total, int64_t cap, bool owner_confirmed);

/* DM3 */
int damper_reading(damper_t *d, int64_t sensor, int64_t stamp, int64_t temp, int64_t smoke);
/* DM6 */
int damper_energy(damper_t *d, int64_t device, int64_t wh);
int damper_source_energy(const damper_t *d, int64_t device, int64_t *wh);

/* DM4 */
int damper_assess(damper_t *d);
/* The place of sensor id, or -1. */
int32_t damper_find_sensor(const damper_t *d, int64_t id);
/* DM5: the rooms from `room` to the last before outside into path (cap),
 * *n of them; *through how many of them have raised sensors. */
int damper_way_out(const damper_t *d, int64_t room, int64_t *path, uint32_t cap, uint32_t *n,
                   uint32_t *through);

/* DM7 */
int damper_refresh_power(damper_t *d);
int damper_follow(damper_t *d, uint32_t *added);
int damper_history(damper_t *d, uint64_t since_ms);

/* DM8 */
int damper_add_contact(damper_t *d, const char *name, int64_t peer, const char *phone_id,
                       int64_t stamp, bool owner_confirmed);
int damper_set_window(damper_t *d, int64_t secs, bool owner_confirmed);
int damper_set_share(damper_t *d, bool on, bool owner_confirmed);
int damper_refresh_contacts(damper_t *d);
int damper_call_contacts(damper_t *d, int64_t now_s, int64_t owner, int64_t approved);
int64_t damper_call_left(const damper_t *d, int64_t now_s);
int damper_cancel_call(damper_t *d);
int damper_ring(damper_t *d, int64_t now_s);
int damper_share_place(damper_t *d, uint32_t *n);
/* DM9 */
int damper_stand_down(damper_t *d, bool owner_confirmed);

#endif
