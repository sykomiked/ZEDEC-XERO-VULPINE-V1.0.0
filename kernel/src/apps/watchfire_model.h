/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* watchfire_model.h — Watchfire, emergency contacts and a medical ID, as a
 * model (zxm.h): the medical ID and the contacts shown on the lock screen,
 * and, on the person's own request, a cancel window, then a ring to each
 * contact (call.start, signalling only) and where the machine is
 * (place.ask, place.here), for the contacts the owner chose to tell.
 *
 * Life-safety: advisory and human-approved. Watchfire never calls
 * emergency services, never calls anyone by itself, is not a medical
 * device and infers nothing clinical. zxvd has no lock-screen method, no
 * method that delivers a message to a contact's device and no notification
 * method, so the model composes the lock-screen card, the location lines
 * and each notice for the view, and sends none of them. No drawing, no
 * allocation, integers only; the rules are watchfire_rules.h.
 *
 *   WM1 AS TYPED. Each medical ID field and each contact's name is kept
 *       byte for byte as the person typed it (no control characters, so
 *       it stays one line of the file), never computed, checked for
 *       meaning or read for anything clinical. Each is filed only as a
 *       record that fits WF4 or WF5.
 *   WM2 THE PERSON STARTS IT. An emergency starts only with
 *       warren.approval_ok (the owner's own session and the person's yes,
 *       both exactly 1) and at least one contact. Nothing rings yet.
 *   WM3 THE CANCEL WINDOW. The window is WATCHFIRE_WINDOW_MIN..MAX seconds
 *       (default WATCHFIRE_WINDOW_DEFAULT), set only while no emergency
 *       runs. watchfire_ring places calls only when WF3 holds: the whole
 *       window has passed by the view's clock (WF2 left 0) and the person
 *       did not cancel. A cancel during the window ends the emergency;
 *       nobody was called.
 *   WM4 RINGING. Each contact in list order, up to WATCHFIRE_LINES (zxvd's
 *       four call lines), by call.start with audio: signalling only, as
 *       zxvd's call group is (media is the host's, not wired).
 *   WM5 WHERE THE MACHINE IS. Shared only once ringing began, and with a
 *       contact only when WF1 holds: the owner opted in (confirmed), the
 *       contact is listed and place.ask answered allowed. place.here only
 *       tells the person whether a location is set and fresh; it is never
 *       shared by itself.
 *   WM6 THE FILE. WATCHFIRE_FILE in home, rewritten whole by
 *       watchfire_save; watchfire_load reads it back, checks every line
 *       and changes the model only when the whole file is good. */
#ifndef WATCHFIRE_MODEL_H
#define WATCHFIRE_MODEL_H

#include "warren_rules.h"
#include "watchfire_rules.h"
#include "zxm.h"

#define WATCHFIRE_NO_SERVICES                                                                      \
    "Watchfire does not call emergency services and is not a medical device. Call them from a "    \
    "phone network."
#define WATCHFIRE_APP            "watchfire" /* the name place.ask is given */
#define WATCHFIRE_DIR            "Watchfire"
#define WATCHFIRE_FILE           "Watchfire/watchfire.txt"
#define WATCHFIRE_CONTACTS       9u  /* seq 1..9 */
#define WATCHFIRE_LINES          4u  /* zxvd's call lines, the loopback line aside */
#define WATCHFIRE_NAME_MAX       47u /* bytes of a contact's name */
#define WATCHFIRE_TEXT_MAX       255u
#define WATCHFIRE_PHONE_ID_LEN   32u /* phone.list's phone_id, hex */
#define WATCHFIRE_NOTICE_MAX     255u
#define WATCHFIRE_SHARE_MAX      191u
#define WATCHFIRE_LOCK_MAX       2047u
#define WATCHFIRE_FILE_MAX       4095u
#define WATCHFIRE_PEER_MAX       4294967295LL
#define WATCHFIRE_STAMP_MAX      999999999999999LL /* 15 digits of ms */
#define WATCHFIRE_WINDOW_DEFAULT 30

/* the medical ID's fields (WF4 field) */
enum {
    WATCHFIRE_MED_NAME = 1,
    WATCHFIRE_MED_BORN = 2,
    WATCHFIRE_MED_ALLERGIES = 3,
    WATCHFIRE_MED_MEDICATIONS = 4,
    WATCHFIRE_MED_CONDITIONS = 5,
    WATCHFIRE_MED_NOTES = 6,
    WATCHFIRE_MED_FIELDS = 6,
};

enum {
    WATCHFIRE_IDLE = 0,
    WATCHFIRE_COUNTDOWN, /* started, inside the cancel window */
    WATCHFIRE_CANCELLED, /* the person cancelled: nobody was called */
    WATCHFIRE_RINGING,   /* the window ran out: contacts were rung */
};

typedef struct {
    char text[WATCHFIRE_TEXT_MAX + 1]; /* as typed; "" not set */
    int64_t stamp;                     /* ms since 1970 */
} watchfire_med_t;

typedef struct {
    char name[WATCHFIRE_NAME_MAX + 1];         /* as typed */
    int64_t peer;                              /* member id call.start rings */
    char phone_id[WATCHFIRE_PHONE_ID_LEN + 1]; /* a paired phone (phone.list), "" none */
    int64_t stamp;
    bool paired;    /* phone_id is on phone.list */
    int64_t phones; /* its open connections there */
    int32_t line;   /* the line call.start gave, -1 none */
    bool shared;    /* WF1 held when the location was shared */
    char share[WATCHFIRE_SHARE_MAX + 1];
} watchfire_contact_t;

typedef struct {
    zxm_link_t *link;
    watchfire_med_t med[WATCHFIRE_MED_FIELDS + 1]; /* [1..6] */
    watchfire_contact_t contact[WATCHFIRE_CONTACTS];
    uint32_t ncontacts;
    bool optin;     /* the owner opted in to sharing where the machine is */
    int64_t window; /* WM3, seconds */
    int32_t state;
    int64_t started_s; /* the view's clock when the person started it */
    uint32_t rang;     /* contacts call.start reached */
    bool place_known, place_stale;
    char notice[WATCHFIRE_NOTICE_MAX + 1];
    char file[WATCHFIRE_FILE_MAX + 1];
} watchfire_t;

void watchfire_init(watchfire_t *w, zxm_link_t *link);

/* WM1: the person's own words; stamp in ms since 1970. */
int watchfire_set_medical(watchfire_t *w, int32_t field, const char *text, int64_t stamp);
int watchfire_add_contact(watchfire_t *w, const char *name, int64_t peer, const char *phone_id,
                          int64_t stamp);
int watchfire_remove_contact(watchfire_t *w, int64_t peer);
/* WM5: only with the owner's confirmation; ZXM_E_STATE without it. */
int watchfire_set_optin(watchfire_t *w, bool on, bool confirmed);
/* WM3: 10..60 s, only while no emergency runs. */
int watchfire_set_window(watchfire_t *w, int64_t secs);

/* The lock-screen card: the medical ID as typed and the contacts' names,
 * with WATCHFIRE_NO_SERVICES; its length, or ZXM_E_TOOBIG. */
int32_t watchfire_lock_text(const watchfire_t *w, char *out, uint32_t cap);

/* phone.list: which contacts' phones are paired, and connected. */
int watchfire_refresh_devices(watchfire_t *w);
/* place.here: whether a location is set and fresh (WM5); read only. */
int watchfire_refresh_place(watchfire_t *w);

/* WM2: start an emergency at the view's clock now_s (seconds). */
int watchfire_start(watchfire_t *w, int64_t now_s, int64_t owner, int64_t approved);
/* WM3: seconds of the cancel window left at now_s, or WATCHFIRE_REFUSED. */
int64_t watchfire_left(const watchfire_t *w, int64_t now_s);
int watchfire_cancel(watchfire_t *w);
/* WM3, WM4: ring the contacts once the window has run out. */
int watchfire_ring(watchfire_t *w, int64_t now_s);
/* WM5: place.ask, and a location line for each contact WF1 allows; how
 * many in *n. */
int watchfire_share_place(watchfire_t *w, uint32_t *n);
/* Back to idle after an emergency; the calls stay on zxvd's lines. */
int watchfire_finish(watchfire_t *w);

/* WM6 */
int watchfire_save(watchfire_t *w);
int watchfire_load(watchfire_t *w);

#endif
