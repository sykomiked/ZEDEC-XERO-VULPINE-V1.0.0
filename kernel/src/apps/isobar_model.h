/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* isobar_model.h — Isobar, a local weather station network, as a model
 * (zxm.h): sites in a tree, stations at the owner's places, each
 * station's sensors with their ranges and step limits, the observations
 * they make, a short outlook from the pressure tendency, and the
 * microclimates two near stations show.
 *
 * zxvd has no sensor method, and none is used: whatever reads a sensor
 * hands the reading to isobar_take. Where a station is comes from the
 * owner's places (place.here, place.list), whether its node is up from
 * the mesh roster (devmesh.devices) and how many peers are reachable from
 * net.peers. Everything is one UTF-8 text file in the Hoard, read with
 * files.read and written whole with files.write. zxvd has no notification
 * method and no clock method, so the model composes each notice's text
 * (isobar_t.notice) and the caller gives the time. No allocation, no
 * floating point; the rules are isobar_rules.h, and the parts' rules are
 * their own (weatherglass_model.h, homestead_rules.h, furrow_rules.h,
 * trellis_rules.h, wellspring_model.h).
 *
 *   IM1 The file's first line is "isobar 1".
 *   IM2 Then one line per site, "site <id> <parent> <kind> <elev>
 *       <name>": a node IR5 allows, id 1..ISOBAR_ID_MAX rising, kind 1 a
 *       network (parent 0) or 2 a site (its parent a node kept above),
 *       elev metres -ISOBAR_ELEV_MAX..ISOBAR_ELEV_MAX.
 *   IM3 Then one line per station, "station <id> <site> <node> <name>":
 *       id 1..ISOBAR_ID_MAX rising, at a kept site of kind 2; node "-" or
 *       the mesh device that hosts it (32 lower-case hex digits); the name
 *       is the owner's place it stands at (place.list).
 *   IM4 Then one line per sensor, "sensor <station> <kind> <lo> <hi>
 *       <step>": stations rising, kinds rising inside a station
 *       (ISOBAR_TEMP..ISOBAR_RAIN), lo at most hi, the range at most
 *       ISOBAR_MARGIN_MAX wide inside -ISOBAR_POINT_MAX..ISOBAR_POINT_MAX,
 *       the step 0..ISOBAR_STEP_MAX.
 *   IM5 Then one line per heater, "heater <station> <lo> <hi>
 *       <setpoint>": a Homestead heater record (homestead.home_device_ok,
 *       the station's number as the device's), the setpoint inside lo..hi.
 *       Isobar keeps it; Homestead commands it, with the owner's approval.
 *   IM6 Then one line per observation, "obs <station> <stamp> <kind>
 *       <reading> <state>", in the order taken: a record IR4 allows, of a
 *       kept sensor, stamps rising for each sensor; state 0 accepted, 1
 *       refused and kept for the owner to see. Every line ends with "\n";
 *       numbers are plain decimals, a minus sign only before one other
 *       than 0, no leading zero. Anything else is not Isobar's file: the
 *       load is refused (ZXM_E_PROTOCOL) and every change refused
 *       (ZXM_E_STATE) until a load succeeds, so a file the model cannot
 *       read is never overwritten. Each change is checked, written whole
 *       and only then kept.
 *   IM7 A READING is accepted when reading_accept_ok (IR1) holds for its
 *       margins against the sensor and its last accepted reading; else it
 *       is kept as refused, and the notice says where it fell against the
 *       range (wellspring.band) or that it moved past the step limit. The
 *       observations are a bounded table (ISOBAR_OBS): when it is full a
 *       reading is refused (ZXM_E_TOOBIG) until the owner prunes old ones;
 *       nothing is ever dropped silently.
 *   IM8 THE OUTLOOK of a station is pressure_outlook (IR2) of its 3-hour
 *       tendency: its last accepted pressure at or before now, less the
 *       last accepted at or before now - 3 hours (and no older than
 *       ISOBAR_TEND_SLACK before that). Advisory only, and the notice says
 *       so.
 *   IM9 A MICROCLIMATE: two stations of one site, at most
 *       ISOBAR_NEAR_M apart, whose last accepted air temperatures fall on
 *       either side of the frost-risk line (at or below it, above it).
 *       Each station is a source about one subject, "the site is at
 *       frost risk"; the sources are joined with lpres_kjoin, then
 *       lpres_conjoin with a second subject, both readings fresh (an hour
 *       old at most, else NEITHER), which keeps BOTH: both readings are
 *       kept as they are, never averaged.
 *   IM10 THE PARTS: a station's temperature shown in Fahrenheit, its
 *       warnings and its week are Weatherglass's rules; a range of hours'
 *       rain is furrow.range_total; the stations' nodes active and
 *       revoked are trellis.peers_in_state. */
#ifndef ISOBAR_MODEL_H
#define ISOBAR_MODEL_H

#include "isobar_rules.h"
#include "lpres.h"
#include "zxm.h"

#define ISOBAR_FILE       "Isobar.network" /* in the Hoard's home */
#define ISOBAR_SITES      32u
#define ISOBAR_STATIONS   32u
#define ISOBAR_SENSORS    160u /* five kinds at every station */
#define ISOBAR_OBS        512u
#define ISOBAR_ID_MAX     9999
#define ISOBAR_ELEV_MAX   99999
#define ISOBAR_STAMP_MAX  INT64_C(99999999999) /* IR4: 11 digits */
#define ISOBAR_RECORD_MAX INT64_C(9999999)     /* IR4: a reading kept, 7 digits */
#define ISOBAR_NAME_MAX   63u
#define ISOBAR_NODE_HEX   32u /* a mesh device id (devmesh.devices) */
#define ISOBAR_TEXT_MAX   65536u
#define ISOBAR_NOTICE_MAX 511u
#define ISOBAR_HOUR_S     INT64_C(3600)
#define ISOBAR_TEND_S     INT64_C(10800) /* IM8: three hours */
#define ISOBAR_TEND_SLACK INT64_C(1800)  /* IM8: half an hour */
#define ISOBAR_WEEK_S     INT64_C(604800)
#define ISOBAR_NEAR_M     INT64_C(1000) /* IM9: one site's stations */
#define ISOBAR_FROST_RISK INT64_C(30)   /* IM9: 3.0 C, ground frost possible */
#define ISOBAR_E_FILES    (-32014)      /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */

/* a sensor's kind (IR4) */
enum {
    ISOBAR_TEMP = 1,     /* tenths of a degree Celsius */
    ISOBAR_PRESSURE = 2, /* tenths of a hPa */
    ISOBAR_HUMIDITY = 3, /* tenths of a percent */
    ISOBAR_WIND = 4,     /* tenths of a metre a second */
    ISOBAR_RAIN = 5,     /* tenths of a millimetre */
};
#define ISOBAR_KINDS 5

/* What isobar_take came to, beside ZXM_OK (accepted) and ZXM_E_*. */
enum { ISOBAR_REFUSED_READING = 1 }; /* IM7: kept as refused */

typedef struct {
    int32_t id, parent, kind;
    int64_t elev;
    char name[ISOBAR_NAME_MAX + 1];
} isobar_site_t;

typedef struct {
    int32_t id, site;
    char node[ISOBAR_NODE_HEX + 1]; /* "" when no device hosts it */
    char name[ISOBAR_NAME_MAX + 1];
    bool placed; /* its place was in place.list at the last isobar_places */
    int64_t lat, lon;
    bool up; /* its device was active and up at the last isobar_nodes */
} isobar_station_t;

typedef struct {
    int32_t station, kind;
    int64_t lo, hi, step;
} isobar_sensor_t;

typedef struct {
    int32_t station;
    int64_t lo, hi, setpoint;
} isobar_heater_t;

typedef struct {
    int32_t station, kind, state;
    int64_t stamp, reading;
} isobar_obs_t;

/* IM9's answer */
typedef struct {
    lpres_state_t state;
    int64_t a_reading, b_reading; /* both kept, as read */
    int64_t apart_m;
} isobar_micro_t;

typedef struct {
    zxm_link_t *link;
    bool loaded; /* a load succeeded: changes may be written (IM6) */
    isobar_site_t site[ISOBAR_SITES];
    uint32_t nsite;
    isobar_station_t st[ISOBAR_STATIONS];
    uint32_t nst;
    isobar_sensor_t sensor[ISOBAR_SENSORS];
    uint32_t nsensor;
    isobar_heater_t heater[ISOBAR_STATIONS];
    uint32_t nheater;
    isobar_obs_t obs[ISOBAR_OBS];
    uint32_t nobs;
    bool here_known;
    int64_t here_lat, here_lon;
    uint32_t peers; /* net.peers at the last isobar_nodes */
    char notice[ISOBAR_NOTICE_MAX + 1];
} isobar_t;

void isobar_init(isobar_t *t, zxm_link_t *link);
/* Read ISOBAR_FILE; a file not made yet is an empty network (ZXM_OK). */
int isobar_load(isobar_t *t);

/* IM2-IM5: each ZXM_E_ARG when the record does not fit its rule, names
 * nothing kept, or is kept already; ZXM_E_TOOBIG when its table is full. */
int isobar_add_site(isobar_t *t, int64_t id, int64_t parent, int64_t kind, int64_t elev,
                    const char *name);
int isobar_add_station(isobar_t *t, int64_t id, int64_t site, const char *node, const char *name);
int isobar_set_sensor(isobar_t *t, int64_t station, int64_t kind, int64_t lo, int64_t hi,
                      int64_t step);
int isobar_set_heater(isobar_t *t, int64_t station, int64_t lo, int64_t hi, int64_t setpoint);

/* IM7: ZXM_OK accepted, ISOBAR_REFUSED_READING kept as refused. */
int isobar_take(isobar_t *t, int64_t station, int64_t kind, int64_t stamp, int64_t reading);
/* Drop every observation older than `before`; *dropped how many. */
int isobar_prune(isobar_t *t, int64_t before, uint32_t *dropped);
/* A sensor's last accepted reading at or before `at`: ZXM_E_STATE when none. */
int isobar_last(const isobar_t *t, int64_t station, int64_t kind, int64_t at, int64_t *reading,
                int64_t *stamp);

/* IM8: *code is IR2's answer; ZXM_E_STATE without the two pressures. */
int isobar_outlook(isobar_t *t, int64_t station, int64_t now, int64_t *code);
/* IR3 for one cell: the highest accepted reading of a sensor in hour
 * `hour` (hours since 1970); ZXM_E_STATE when the cell is empty. */
int isobar_hour_high(const isobar_t *t, int64_t station, int64_t kind, int64_t hour, int64_t *high);
/* IM10: the rain of hours from..to, both inside (furrow.range_total). */
int isobar_rain_total(const isobar_t *t, int64_t station, int64_t from, int64_t to, int64_t *total);
/* IM10: the lowest, highest and floored mean temperature of the week up
 * to now (weatherglass.week_temps); ZXM_E_STATE when there is none. */
int isobar_week(const isobar_t *t, int64_t station, int64_t now, int64_t out[3]);
/* IM10: the warnings of the last temperature and wind (weatherglass.warn_flags). */
int isobar_warnings(isobar_t *t, int64_t station, int64_t now, int32_t *flags);

/* Where each station stands: place.here and place.list. */
int isobar_places(isobar_t *t);
/* How far apart two placed stations are, in whole metres (an
 * equirectangular reckoning in integers, for stations less than a degree
 * apart); ZXM_E_STATE when one is not placed, ZXM_E_ARG when farther. */
int isobar_apart(const isobar_t *t, int64_t a, int64_t b, int64_t *metres);
/* IM9: the two stations' last accepted temperatures at `now`. */
int isobar_microclimate(isobar_t *t, int64_t a, int64_t b, int64_t now, isobar_micro_t *out);
/* The stations' nodes (devmesh.devices) and the peers reachable
 * (net.peers): *active and *revoked count the hosting devices by state
 * (trellis.peers_in_state); each station's `up` is set. */
int isobar_nodes(isobar_t *t, int64_t *active, int64_t *revoked);

#endif
