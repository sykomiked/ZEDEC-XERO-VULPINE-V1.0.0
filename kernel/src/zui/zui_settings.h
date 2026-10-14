/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_settings.h — the settings store: typed keys, layered values, a cache,
 * change subscriptions and a text format, for every seat and person at once.
 *
 * Every setting is a key in a schema ("theme.scheme", "panel.size") with a
 * type and a default. Values come from layers; the highest layer that has a
 * key wins. A view (seat, user, app) says which seat, user and app layers
 * apply, so several seats and several people each read their own settings
 * from one store.
 *
 *   S1 LAYERS, lowest to highest: the schema default < SYSTEM (what the image
 *      ships) < DEVICE (this machine) < SEAT (one seat: a screen, keyboard
 *      and pointer) < USER (one person) < APP (one app, for one person).
 *      A value stored in a layer has a scope: none for SYSTEM and DEVICE,
 *      the seat for SEAT, the user for USER, the user and the app for APP.
 *   S2 TYPED KEYS. A key is an int (with min, max and step: a value is
 *      min + k * step and at most max), a bool, an enum (one of a list of
 *      names), a short string (at most ZUI_SET_STR_MAX - 1 bytes, no control
 *      characters, so it stays on one line) or a colour role (a
 *      zui_color_role_t of zui_palette.h, by its name). Key names are
 *      [a-z0-9._-], at most ZUI_SET_KEY_MAX - 1 bytes. A value the schema
 *      does not allow, or a key it does not have, is refused and nothing
 *      changes; a default outside its own rules is refused at definition.
 *   S3 THE CACHE. Lookups go through a direct-mapped cache of
 *      ZUI_SET_CACHE resolved values, hashed by key and view. A change to a
 *      key in any layer drops that key's cached values (for every view) and
 *      no others. hits and misses are counted.
 *   S4 LOCKS. A value in SYSTEM, DEVICE or SEAT may be locked: the layers
 *      above it are not consulted for that key. Setting a key above a
 *      SYSTEM or DEVICE lock is refused (ZUI_SET_E_LOCKED); a SEAT lock
 *      masks the user and app layers of views on that seat (a user value is
 *      kept for the person's other seats). USER and APP cannot lock. Text
 *      loaded into a layer above a lock is kept as written, and stays
 *      masked while the lock holds.
 *   S5 SUBSCRIPTIONS. zui_set_watch(view, prefix) calls back once for each
 *      effective change of a key that starts with prefix, as seen from that
 *      view: setting a value equal to the one in effect, or changing a lower
 *      layer under a higher one that overrides it, calls nothing. A callback
 *      may read settings; a write from inside one is refused (ZUI_SET_E_BUSY).
 *   S6 TEXT. One layer and scope is saved as text: the header line
 *      "zxcfg 1", then one "key = value" per line in schema order, a locked
 *      value as "lock key = value". Lines whose first non-blank character
 *      is '#' are comments (whole lines only: a '#' after a value is part
 *      of it); blank lines are skipped; CR LF is accepted. Saving writes
 *      one comment after the header naming the layer and scope.
 *      Values: ints in decimal, bools true/false, enums and colour roles by
 *      name, strings in double quotes with \" and \\ escapes. Saving, loading
 *      and saving again gives the same bytes.
 *   S7 LOADING IS ALL OR NOTHING. zui_set_load replaces the whole layer and
 *      scope with the text. Any error (bad header, unknown version, unknown
 *      key, bad value, a key twice, a lock in USER or APP, no room) changes
 *      nothing and gives the 1-based line number. Watchers then see each
 *      effective change once (S5).
 *   S8 RESET. zui_set_reset drops one key from a layer and scope (the layer
 *      below shows through); zui_set_reset_layer drops all of them.
 *   S9 FIXED SIZE. No allocation: ZUI_SET_KEYS keys, ZUI_SET_ENTRIES stored
 *      values across all layers and scopes, ZUI_SET_SEATS seats,
 *      ZUI_SET_USERS users, ZUI_SET_APPS apps, ZUI_SET_WATCH subscriptions.
 *      Running out is an error (ZUI_SET_E_FULL), never a silent drop.
 *   S10 Integer only, deterministic, no libc. The caller owns the store
 *      (it is large: keep it static). */
#ifndef ZXV_ZUI_SETTINGS_H
#define ZXV_ZUI_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_palette.h"

#define ZUI_SET_KEY_MAX  48u   /* a key name, with its NUL           */
#define ZUI_SET_STR_MAX  48u   /* a string value, with its NUL       */
#define ZUI_SET_NAME_MAX 24u   /* a seat, user or app name, with NUL */
#define ZUI_SET_KEYS     320u  /* keys in the schema                 */
#define ZUI_SET_ENTRIES  1024u /* stored values, all layers          */
#define ZUI_SET_SEATS    8u
#define ZUI_SET_USERS    16u
#define ZUI_SET_APPS     32u
#define ZUI_SET_WATCH    16u
#define ZUI_SET_CACHE    128u /* resolved values cached (power of 2) */
#define ZUI_SET_ENUM_MAX 16u  /* names in one enum                   */
#define ZUI_SET_LINE_MAX 192u /* one line of text                    */

typedef enum {
    ZUI_SET_OK = 0,
    ZUI_SET_E_NOKEY = -1,   /* not in the schema                  */
    ZUI_SET_E_TYPE = -2,    /* not a value of the key's type       */
    ZUI_SET_E_RANGE = -3,   /* the type, but outside the key's rules */
    ZUI_SET_E_LOCKED = -4,  /* S4                                  */
    ZUI_SET_E_FULL = -5,    /* S9                                  */
    ZUI_SET_E_SCOPE = -6,   /* a scope the layer does not take     */
    ZUI_SET_E_PARSE = -7,   /* a line that is not "key = value"    */
    ZUI_SET_E_VERSION = -8, /* not "zxcfg 1"                       */
    ZUI_SET_E_NAME = -9,    /* a bad key, seat, user or app name   */
    ZUI_SET_E_DUP = -10,    /* defined, or in the text, twice      */
    ZUI_SET_E_SPACE = -11,  /* the output buffer is too small      */
    ZUI_SET_E_BUSY = -12    /* a write from inside a callback (S5) */
} zui_set_err_t;

typedef enum {
    ZUI_SET_INT = 1,
    ZUI_SET_BOOL,
    ZUI_SET_ENUM,
    ZUI_SET_STR,
    ZUI_SET_COLOR /* a zui_color_role_t */
} zui_set_type_t;

typedef enum { /* S1 */
               ZUI_LAYER_SYSTEM = 0,
               ZUI_LAYER_DEVICE,
               ZUI_LAYER_SEAT,
               ZUI_LAYER_USER,
               ZUI_LAYER_APP,
               ZUI_LAYER_COUNT
} zui_set_layer_t;

/* Who is looking (a view) or where a value is stored (a scope): ids from
 * zui_set_seat / zui_set_user / zui_set_app, 0 = none. */
typedef struct {
    uint8_t seat, user, app;
} zui_set_view_t;

/* A value: i for int, bool (0/1), enum (the index) and colour role; s for a
 * string (and all zero otherwise). */
typedef struct {
    int32_t i;
    char s[ZUI_SET_STR_MAX];
} zui_set_value_t;

typedef struct {
    char key[ZUI_SET_KEY_MAX];
    uint8_t type, nnames;
    int32_t min, max, step;
    const char *const *names; /* ZUI_SET_ENUM: the caller's list, kept */
    zui_set_value_t def;
} zui_set_def_t;

typedef struct {
    uint8_t used, layer, locked;
    zui_set_view_t scope;
    uint16_t key;
    zui_set_value_t v;
} zui_set_entry_t;

typedef struct {
    uint8_t valid;
    zui_set_view_t view;
    uint16_t key;
    zui_set_value_t v;
} zui_set_cached_t;

typedef struct zui_settings zui_settings_t;
/* S5: key is the key's index (zui_set_key), v the value now in effect. */
typedef void (*zui_set_watch_fn)(zui_settings_t *st, uint32_t key, const zui_set_value_t *v,
                                 void *ctx);

typedef struct {
    uint8_t used;
    zui_set_view_t view;
    char prefix[ZUI_SET_KEY_MAX];
    zui_set_watch_fn fn;
    void *ctx;
} zui_set_watcher_t;

struct zui_settings {
    zui_set_def_t def[ZUI_SET_KEYS];
    uint32_t nkeys;
    uint16_t index[ZUI_SET_KEYS * 2u]; /* name hash -> key + 1 */
    zui_set_entry_t e[ZUI_SET_ENTRIES];
    char seat[ZUI_SET_SEATS][ZUI_SET_NAME_MAX];
    char user[ZUI_SET_USERS][ZUI_SET_NAME_MAX];
    char app[ZUI_SET_APPS][ZUI_SET_NAME_MAX];
    zui_set_cached_t cache[ZUI_SET_CACHE];
    uint32_t hits, misses; /* S3 */
    zui_set_watcher_t watch[ZUI_SET_WATCH];
    uint32_t busy; /* inside a callback (S5) */
};

void zui_set_init(zui_settings_t *st);

/* ---- the schema (S2): each returns the key's index, or a negative
 * zui_set_err_t ---------------------------------------------------------- */
int zui_set_def_int(zui_settings_t *st, const char *key, int32_t def, int32_t min, int32_t max,
                    int32_t step);
int zui_set_def_bool(zui_settings_t *st, const char *key, bool def);
int zui_set_def_enum(zui_settings_t *st, const char *key, const char *const *names, uint32_t n,
                     uint32_t def);
int zui_set_def_str(zui_settings_t *st, const char *key, const char *def);
int zui_set_def_color(zui_settings_t *st, const char *key, zui_color_role_t def);
int zui_set_key(const zui_settings_t *st, const char *key); /* index, or E_NOKEY */
const zui_set_def_t *zui_set_schema(const zui_settings_t *st, uint32_t key);

/* ---- seats, users, apps: an id for a name (the same name gives the same
 * id), 0 when the table is full or the name is bad ([A-Za-z0-9._-]) ---- */
uint8_t zui_set_seat(zui_settings_t *st, const char *name);
uint8_t zui_set_user(zui_settings_t *st, const char *name);
uint8_t zui_set_app(zui_settings_t *st, const char *name);

/* ---- reading (through the cache, S3) ----------------------------------- */
int zui_set_get(zui_settings_t *st, zui_set_view_t view, const char *key, zui_set_value_t *out);
int zui_set_get_k(zui_settings_t *st, zui_set_view_t view, uint32_t key, zui_set_value_t *out);
/* int, bool, enum index or colour role; 0 for an unknown key */
int32_t zui_set_int(zui_settings_t *st, zui_set_view_t view, const char *key);
bool zui_set_bool(zui_settings_t *st, zui_set_view_t view, const char *key);
/* a string, copied into buf (at most cap - 1 bytes); "" for an unknown key */
void zui_set_str(zui_settings_t *st, zui_set_view_t view, const char *key, char *buf, uint32_t cap);
/* What one layer and scope holds for key: ZUI_SET_OK and *out, or
 * ZUI_SET_E_NOKEY when that layer has nothing for it. */
int zui_set_peek(const zui_settings_t *st, zui_set_layer_t layer, zui_set_view_t scope,
                 const char *key, zui_set_value_t *out, bool *locked);

/* ---- writing ----------------------------------------------------------- */
int zui_set_set(zui_settings_t *st, zui_set_layer_t layer, zui_set_view_t scope, const char *key,
                const zui_set_value_t *v);
int zui_set_set_int(zui_settings_t *st, zui_set_layer_t layer, zui_set_view_t scope,
                    const char *key, int32_t v); /* int, bool, enum, colour */
int zui_set_set_str(zui_settings_t *st, zui_set_layer_t layer, zui_set_view_t scope,
                    const char *key, const char *v);
/* the value as it is written in the text format (S6) */
int zui_set_set_text(zui_settings_t *st, zui_set_layer_t layer, zui_set_view_t scope,
                     const char *key, const char *text);
/* S4: lock (or unlock) key in layer; a lock with no value there keeps the
 * value in effect below it. */
int zui_set_lock(zui_settings_t *st, zui_set_layer_t layer, zui_set_view_t scope, const char *key,
                 bool on);
int zui_set_reset(zui_settings_t *st, zui_set_layer_t layer, zui_set_view_t scope,
                  const char *key);                                                       /* S8 */
int zui_set_reset_layer(zui_settings_t *st, zui_set_layer_t layer, zui_set_view_t scope); /* S8 */

/* ---- S5 ---------------------------------------------------------------- */
/* Returns a subscription id (1..ZUI_SET_WATCH), or a negative error. */
int zui_set_watch(zui_settings_t *st, zui_set_view_t view, const char *prefix, zui_set_watch_fn fn,
                  void *ctx);
void zui_set_unwatch(zui_settings_t *st, int id);

/* ---- S6, S7 ------------------------------------------------------------ */
/* The text of one layer and scope, NUL-terminated; returns its length, or
 * ZUI_SET_E_SPACE when cap is too small (buf then holds nothing useful). */
int zui_set_save(const zui_settings_t *st, zui_set_layer_t layer, zui_set_view_t scope, char *buf,
                 uint32_t cap);
/* Replace one layer and scope with text (len bytes). On error *line (when
 * not NULL) is the 1-based line at fault, 0 when no line is (no room). */
int zui_set_load(zui_settings_t *st, zui_set_layer_t layer, zui_set_view_t scope, const char *text,
                 uint32_t len, uint32_t *line);
/* One value as text (S6); returns its length or ZUI_SET_E_SPACE. */
int zui_set_format(const zui_settings_t *st, uint32_t key, const zui_set_value_t *v, char *buf,
                   uint32_t cap);
/* Text to a value of key's type, checked against its rules (S2). */
int zui_set_parse(const zui_settings_t *st, uint32_t key, const char *text, uint32_t len,
                  zui_set_value_t *out);

const char *zui_set_layer_name(zui_set_layer_t layer); /* "system", ..., "app" */

#endif /* ZXV_ZUI_SETTINGS_H */
