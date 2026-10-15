/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_place.h — location behind Waymark (place.*): where this machine is,
 * the places the owner keeps, and which apps may know where they are.
 *
 *   P1 WHERE. The location is set by a bridge to the system's location
 *      service or by the owner typing it, with its source, how sure it is
 *      (metres) and when it was set. Coordinates are whole millionths of a
 *      degree: no floating point anywhere here. A location older than
 *      ZPL_STALE_S is reported as stale.
 *   P2 PERMISSION. An app learns the location only when the owner allows
 *      it: "always", "once" (the next ask only) or "never". An app with no
 *      answer yet that asks is put on a list of pending asks (one entry per
 *      app) for the owner, and gets nothing until the owner answers.
 *   P3 PLACES. Up to ZPL_PLACES named places with a note; names are unique,
 *      ignoring ASCII case.
 *   P4 DISTANCE. Metres between two points from an equirectangular sum on
 *      a fixed-point cosine: within about 1% for places under 500 km apart,
 *      a rough figure farther away.
 *   P5 THE FILE. One text file, 0600, rewritten whole and swapped in place,
 *      ending in the SHA-256 of what is above it; a damaged file is moved
 *      aside as "<file>.damaged" and places start empty (`reset`). The
 *      location itself is never written to disk.
 *
 * zxvd uses one thread (zxvd_log.h L3); so does this. */
#ifndef ZXV_PLACE_H
#define ZXV_PLACE_H

#include <stdbool.h>
#include <stdint.h>

#define ZPL_NAME_MAX 63u  /* bytes of a place's name */
#define ZPL_NOTE_MAX 255u /* bytes of a place's note */
#define ZPL_APP_MAX  31u
#define ZPL_SRC_MAX  15u /* bytes of a source: "owner", "corelocation", ... */
#define ZPL_PLACES   200u
#define ZPL_APPS     64u
#define ZPL_PATH_MAX 900u
#define ZPL_STALE_S  600 /* ten minutes */
#define ZPL_LAT_MAX  90000000
#define ZPL_LON_MAX  180000000

enum {
    ZPL_E_ARG = -1,      /* a bad name, app or coordinate */
    ZPL_E_NOTFOUND = -2, /* no such place */
    ZPL_E_EXISTS = -3,   /* a place has that name */
    ZPL_E_FULL = -4,     /* no room for another place or app */
    ZPL_E_IO = -5,       /* the file could not be written */
    ZPL_E_NONE = -6,     /* the location is not known */
    ZPL_E_DENIED = -7,   /* the owner said never */
    ZPL_E_PENDING = -8   /* waiting for the owner's answer */
};

typedef enum { ZPL_ASK, ZPL_ONCE, ZPL_ALWAYS, ZPL_NEVER } zpl_mode_t;

typedef struct {
    int32_t lat, lon; /* millionths of a degree */
} zpl_point_t;

typedef struct {
    bool known;
    zpl_point_t at;
    uint32_t accuracy_m;
    char source[ZPL_SRC_MAX + 1];
    int64_t time; /* seconds since 1970 */
} zpl_here_t;

typedef struct {
    char name[ZPL_NAME_MAX + 1];
    zpl_point_t at;
    char note[ZPL_NOTE_MAX + 1];
} zpl_place_t;

typedef struct {
    char app[ZPL_APP_MAX + 1];
    uint8_t mode;  /* zpl_mode_t */
    bool pending;  /* asked, no answer yet (P2) */
    int64_t asked; /* when it last asked */
} zpl_app_t;

/* Load the places file; NULL or "" keeps them in memory. 0, or ZPL_E_ARG /
 * ZPL_E_IO; a damaged file is moved aside and 0 returned with
 * zpl_was_reset() true. */
int zpl_open(const char *path);
void zpl_close(void);
bool zpl_was_reset(void);
void zpl_set_clock(int64_t (*now)(void)); /* tests; NULL restores time() */

int zpl_set_here(zpl_point_t at, uint32_t accuracy_m, const char *source);
void zpl_forget_here(void);
const zpl_here_t *zpl_here(void);
bool zpl_stale(void);

/* An app asks for the location (P2): 0 and *out when allowed (a "once" is
 * used up), else ZPL_E_PENDING, ZPL_E_DENIED or ZPL_E_NONE. */
int zpl_ask(const char *app, zpl_here_t *out);
int zpl_permit(const char *app, zpl_mode_t mode);
uint32_t zpl_app_count(void);
const zpl_app_t *zpl_app(uint32_t i);
const char *zpl_mode_name(zpl_mode_t m);
int zpl_mode_parse(const char *s); /* -1 when unknown */

int zpl_add(const char *name, zpl_point_t at, const char *note);
int zpl_remove(const char *name);
uint32_t zpl_count(void);
const zpl_place_t *zpl_place(uint32_t i);
const zpl_place_t *zpl_find(const char *name);

bool zpl_valid(zpl_point_t p);
uint64_t zpl_distance_m(zpl_point_t a, zpl_point_t b);
const char *zpl_strerror(int code);

#endif
