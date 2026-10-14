/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_privacy.h — who is using the camera, the microphone, the screen or
 * the location right now, shown where the person can always see it.
 *
 * The drivers and the permission layer call begin / end around every use;
 * the status bar draws the indicator, and the privacy pane reads the log.
 *
 *   PV1 COUNTED USE. zui_privacy_begin / zui_privacy_end bracket each use
 *       of a sensor by an app, and nest: an app that begins twice is live
 *       until it ends twice. An end with no begin is refused, never
 *       counted below zero. zui_privacy_end_app ends everything an app
 *       holds (it closed or crashed), so a sensor is never left lit by an
 *       app that is gone.
 *   PV2 LIVE AND LIT. A sensor is LIVE while any app uses it. It stays LIT
 *       for `linger_ms` after the last use ends (ZUI_PRIV_LINGER_MS unless
 *       set), so a use too short to notice still shows. The indicator
 *       draws a live sensor's dot full and a lingering one faded.
 *   PV3 THE LOG keeps who used what and when: app, sensor, start and end
 *       (while `live`, no end yet). A log entry is opened at an app's first
 *       begin and closed at its last end. The last ZUI_PRIV_LOG entries
 *       are kept, oldest dropped first; the count of dropped ones is kept
 *       so the pane can say the log is not complete.
 *   PV4 NOTHING HIDES. There is no call that turns the indicator off. If
 *       the table of live users is full, a begin is refused (the caller
 *       must refuse the sensor too) rather than going untracked.
 *   PV5 TIME is integer milliseconds passed in by the caller, never read
 *       here; a time earlier than the last one seen is treated as the last
 *       one (a clock that steps back cannot shorten a light). Integer only,
 *       no allocation, deterministic.
 *
 * Colours are zui_tokens.h status colours, the same on every screen:
 * camera ZUI_OK, microphone ZUI_INFO, screen capture ZUI_ALERT, location
 * ZUI_TEXT_2. None is gold: the indicator is a warning light, not an
 * action.
 */
#ifndef ZXV_ZUI_PRIVACY_H
#define ZXV_ZUI_PRIVACY_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_canvas.h"

#define ZUI_PRIV_USERS     32u   /* app x sensor pairs live at once */
#define ZUI_PRIV_LOG       64u   /* log entries kept                */
#define ZUI_PRIV_APP_MAX   32u   /* bytes of an app id, with the NUL */
#define ZUI_PRIV_LINGER_MS 3000u /* PV2 default                     */

typedef enum {
    ZUI_SENSOR_CAMERA = 0,
    ZUI_SENSOR_MIC,
    ZUI_SENSOR_SCREEN, /* screen capture or sharing */
    ZUI_SENSOR_LOCATION,
    ZUI_SENSOR_COUNT
} zui_sensor_t;

#define ZUI_SENSOR_BIT(s) (1u << (uint32_t) (s))

typedef struct {
    char app[ZUI_PRIV_APP_MAX];
    uint8_t sensor;
    uint32_t refs;  /* 0 = a free slot */
    uint64_t since; /* first begin */
    uint32_t log;   /* its log entry's seq (the entry may have been dropped) */
} zui_priv_user_t;

typedef struct {
    uint32_t seq; /* 1, 2, ... in the order uses began */
    char app[ZUI_PRIV_APP_MAX];
    uint8_t sensor;
    bool live;           /* still in use: end is not set yet */
    uint64_t start, end; /* ms */
    uint32_t uses;       /* begins nested in this entry */
} zui_priv_log_t;

/* What the status bar shows now (PV2). */
typedef struct {
    uint8_t live;                    /* ZUI_SENSOR_BIT mask in use now           */
    uint8_t lit;                     /* live, or ended less than linger_ms ago   */
    uint32_t apps[ZUI_SENSOR_COUNT]; /* how many apps use each now */
} zui_priv_state_t;

typedef struct {
    zui_priv_user_t user[ZUI_PRIV_USERS];
    zui_priv_log_t log[ZUI_PRIV_LOG]; /* a ring */
    uint32_t log_head, log_n, next_seq;
    uint32_t dropped; /* PV3: log entries lost to the ring */
    uint32_t refused; /* PV4: begins refused, the table full */
    uint64_t last_end[ZUI_SENSOR_COUNT];
    bool ended[ZUI_SENSOR_COUNT]; /* last_end is meaningful */
    uint64_t now;                 /* PV5: the latest time seen */
    uint32_t linger_ms;
} zui_privacy_t;

void zui_privacy_init(zui_privacy_t *p);
/* PV1: returns the app's count for that sensor after the call, 0 when
 * refused (bad arguments, or PV4 full). */
uint32_t zui_privacy_begin(zui_privacy_t *p, const char *app, zui_sensor_t s, uint64_t now_ms);
/* PV1: false when the app was not using it. */
bool zui_privacy_end(zui_privacy_t *p, const char *app, zui_sensor_t s, uint64_t now_ms);
/* PV1: ends every use the app holds; returns how many sensors it held. */
uint32_t zui_privacy_end_app(zui_privacy_t *p, const char *app, uint64_t now_ms);
/* The app's count for a sensor now (0 = not using it). */
uint32_t zui_privacy_refs(const zui_privacy_t *p, const char *app, zui_sensor_t s);

/* PV2 */
zui_priv_state_t zui_privacy_state(const zui_privacy_t *p, uint64_t now_ms);
/* The apps using sensor s now, in the order they began; returns how many
 * there are (only the first cap written). */
uint32_t zui_privacy_users(const zui_privacy_t *p, zui_sensor_t s, const char **out, uint32_t cap);

/* PV3: entry i of the log, 0 = the newest; NULL past the end. */
const zui_priv_log_t *zui_privacy_log(const zui_privacy_t *p, uint32_t i);
uint32_t zui_privacy_log_count(const zui_privacy_t *p);

const char *zui_sensor_name(zui_sensor_t s); /* "camera", "microphone", ... */
uint32_t zui_sensor_rgb(zui_sensor_t s);

/* The indicator: a carved pill holding one dot per lit sensor, in sensor
 * order, drawn right-aligned in r (the status bar's corner). Returns the
 * width used in pixels (0 when nothing is lit: nothing is drawn). dot is
 * the dot's diameter in pixels. */
int32_t zui_privacy_draw(const zui_privacy_t *p, zui_surface_t *s, zui_rect_t r, int32_t dot,
                         uint64_t now_ms);
/* What a screen reader says: "camera in use by notes, chat; microphone
 * used recently" or "no sensors in use". Always NUL-terminates; returns
 * the length written. */
uint32_t zui_privacy_describe(const zui_privacy_t *p, uint64_t now_ms, char *out, uint32_t cap);

#endif /* ZXV_ZUI_PRIVACY_H */
