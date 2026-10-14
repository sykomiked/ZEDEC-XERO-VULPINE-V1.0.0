/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_lock.h — the lock screen and idle auto-lock.
 *
 * The shell passes every input event through zui_lock_event before anything
 * else, ticks it with the platform's monotonic milliseconds, and draws it
 * over everything when it is up. Who may unlock is not decided here: an
 * auth callback (the seats service, on its own branch) gets the account and
 * the secret and answers yes or no.
 *
 *   L1 STATES. UNLOCKED -> DIMMING (idle; the screen fades) -> LOCKED ->
 *      AUTHENTICATING (the auth callback is still deciding) -> UNLOCKED, or
 *      -> FAILED -> LOCKED. Input while dimming wakes the screen and is
 *      passed on. While the lock is up every event is the lock's: nothing
 *      reaches an app.
 *   L2 IDLE. Any input event (keys, text, pointer, scroll, touch) restarts
 *      the idle timer; ticks, focus and resize do not. After idle_ms the
 *      screen locks, dimming for the last dim_ms. idle_ms 0 never locks.
 *   L3 LOCK NOW. The lock shortcut (lock_key with at least lock_mods; by
 *      default Cmd-Shift-L, Ctrl-Shift-L off a Mac) or zui_lock_now locks at
 *      once, with no grace period.
 *   L4 GRACE. When the screen locked because of idling, input within
 *      grace_ms of the lock unlocks with no secret (the person was right
 *      there). Never after lock now, never after a failure.
 *   L5 BACKOFF. The first free_tries failures only say so. After that each
 *      failure waits: backoff_ms, doubling each time, at most backoff_max_ms.
 *      While waiting, typing is ignored (FAILED); then the screen is LOCKED
 *      again. A success clears the count.
 *   L6 THE SECRET IS NEVER KEPT. Typed bytes sit in one fixed buffer until
 *      they are submitted, and are wiped (with writes the compiler may not
 *      drop) right after the auth callback returns, on Escape, on lock now,
 *      and after ZUI_LOCK_TYPED_MS without typing. The auth callback must not
 *      keep the pointer: the bytes are valid only during the call. Without an
 *      auth callback nothing unlocks.
 *   L7 NOTIFICATIONS. Notices that arrive while locked are listed by app;
 *      their text shows only when previews are allowed. They are dropped on
 *      unlock (the apps keep their own).
 *   L8 EMERGENCY. The Emergency button on the lock screen (or
 *      zui_lock_emergency) calls the emergency hook in any locked state,
 *      during a backoff too, without unlocking anything.
 *   L9 DRAWING. The #002911 ground; the fox mark (a zui_part_fn, as
 *      zui_launch_t's mark) at the top centre; the time large and the date
 *      under it, engraved, from integer epoch seconds and a timezone offset
 *      the caller passes; the account name; a carved password field that
 *      shows one dot per character and a gold caret (the one gold element);
 *      a failure in the alert colour with the wait left; the ZXV bar while
 *      authenticating. Integer only, deterministic, no allocation.
 */
#ifndef ZXV_ZUI_LOCK_H
#define ZXV_ZUI_LOCK_H

#include <stdbool.h>
#include <stdint.h>

#include "zui_canvas.h"
#include "zui_input.h"
#include "zui_progress.h"

#define ZUI_LOCK_SECRET_MAX 128u   /* bytes of a typed secret */
#define ZUI_LOCK_NOTICES    4u     /* notices listed on the lock screen */
#define ZUI_LOCK_TYPED_MS   30000u /* typed but not submitted: wiped after this */
#define ZUI_LOCK_NAME_MAX   48u

typedef enum {
    ZUI_LOCK_UNLOCKED = 0,
    ZUI_LOCK_DIMMING,
    ZUI_LOCK_LOCKED,
    ZUI_LOCK_AUTHENTICATING,
    ZUI_LOCK_FAILED
} zui_lock_state_t;

/* L6: the auth callback's answers. */
enum { ZUI_AUTH_OK = 0, ZUI_AUTH_FAIL = 1, ZUI_AUTH_PENDING = 2 };

/* Is `secret` (n bytes, valid only during the call) right for `account`? */
typedef int (*zui_lock_auth_fn)(void *ctx, uint32_t account, const uint8_t *secret, uint32_t n);

typedef struct {
    uint32_t idle_ms;        /* L2: 0 never locks by itself */
    uint32_t dim_ms;         /* L2: the last part of idle_ms, dimming */
    uint32_t grace_ms;       /* L4 */
    uint32_t free_tries;     /* L5 */
    uint32_t backoff_ms;     /* L5: the first wait */
    uint32_t backoff_max_ms; /* L5: the longest */
    uint16_t lock_key;       /* L3 */
    uint8_t lock_mods;
    bool previews; /* L7: notification text shown while locked */
    bool h24;      /* 24-hour time */
} zui_lock_config_t;

typedef struct {
    char app[32];
    char text[96];
} zui_lock_notice_t;

/* Where things are on the lock screen (device pixels). */
typedef struct {
    zui_rect_t mark, time, date, account, field, message, emergency;
    zui_rect_t notice[ZUI_LOCK_NOTICES];
} zui_lock_layout_t;

typedef struct {
    int32_t year, month, day; /* month 1..12, day 1..31 */
    int32_t wday;             /* 0 Sunday .. 6 Saturday */
    int32_t hour, min, sec;
} zui_civil_t;

typedef struct {
    zui_lock_config_t cfg;
    zui_lock_state_t state;
    uint32_t now, last_input, last_typed, locked_at;
    bool by_idle; /* L4: this lock came from idling */
    uint32_t fails, retry_at;
    uint32_t account;
    char account_name[ZUI_LOCK_NAME_MAX];
    uint8_t secret[ZUI_LOCK_SECRET_MAX];
    uint32_t nsecret;
    zui_lock_auth_fn auth;
    void *auth_ctx;
    void (*emergency)(void *ctx); /* L8 */
    void *emergency_ctx;
    void (*on_state)(void *ctx, zui_lock_state_t from, zui_lock_state_t to);
    void *on_state_ctx;
    zui_part_fn mark; /* L9: the fox */
    void *mark_ctx;
    const zui_text_t *big, *text; /* the time; everything else (NULL: no text) */
    zui_lock_notice_t notice[ZUI_LOCK_NOTICES];
    uint32_t nnotice;
    zui_rect_t area; /* where it was last drawn, for pointer hits */
    uint32_t emergencies;
} zui_lock_t;

void zui_lock_defaults(zui_lock_config_t *cfg);
/* cfg NULL: the defaults. Starts unlocked with the idle timer at now_ms. */
void zui_lock_init(zui_lock_t *l, const zui_lock_config_t *cfg, uint32_t now_ms);
void zui_lock_account(zui_lock_t *l, uint32_t account, const char *name);
void zui_lock_set_area(zui_lock_t *l, zui_rect_t area);

/* L1..L8. Returns true when the event was the lock's (always, while up). */
bool zui_lock_event(zui_lock_t *l, const zui_event_t *e);
void zui_lock_tick(zui_lock_t *l, uint32_t now_ms);
void zui_lock_now(zui_lock_t *l);                   /* L3 */
bool zui_lock_submit(zui_lock_t *l);                /* the typed secret to the auth callback */
void zui_lock_auth_result(zui_lock_t *l, bool ok);  /* a PENDING answer arrives */
void zui_lock_emergency(zui_lock_t *l);             /* L8 */
bool zui_lock_is_up(const zui_lock_t *l);           /* LOCKED, AUTHENTICATING or FAILED */
uint32_t zui_lock_wait_ms(const zui_lock_t *l);     /* L5: backoff left, 0 none */
uint32_t zui_lock_dim(const zui_lock_t *l);         /* 0..256 while dimming */
uint32_t zui_lock_typed_chars(const zui_lock_t *l); /* characters typed (dots) */
const char *zui_lock_state_name(zui_lock_state_t s);

/* L7 */
void zui_lock_notify(zui_lock_t *l, const char *app, const char *text);
/* What the lock screen shows as notice i's text ("" past the end). */
const char *zui_lock_notice_text(const zui_lock_t *l, uint32_t i);

/* L9 */
void zui_civil(int64_t epoch_s, int32_t tz_min, zui_civil_t *out);
uint32_t zui_lock_time_text(char *out, uint32_t cap, int64_t epoch_s, int32_t tz_min, bool h24);
uint32_t zui_lock_date_text(char *out, uint32_t cap, int64_t epoch_s, int32_t tz_min);
void zui_lock_layout(const zui_lock_t *l, zui_rect_t area, zui_lock_layout_t *out);
void zui_lock_draw(zui_lock_t *l, zui_surface_t *s, zui_rect_t area, int64_t epoch_s,
                   int32_t tz_min, uint32_t tick);

#endif /* ZXV_ZUI_LOCK_H */
