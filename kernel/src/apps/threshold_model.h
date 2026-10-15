/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* threshold_model.h — Threshold, login and accounts, as a model (zxm.h):
 * the people who use this device and their locks (zxvd's seats.*,
 * kernel/arch/hosted/zxvd_seats.c) and their passkeys (passkeys.*). The
 * rules are threshold_rules.h (TH1-TH4), the same ones the app's Sutra,
 * KRON, Eve and MYRA blocks run. No drawing, no allocation, integers only.
 *
 *   TM1 THE ROSTER is what seats.list and seats.auto answered last. A
 *       refresh checks every seat (TH4, and each value inside its range)
 *       before it changes anything: a reply out of bounds leaves what was
 *       shown as it was. The model has no seat limit of its own: it shows
 *       the first THRESHOLD_SHOWN seats and counts them all; zxvd refuses a
 *       seat past its own limit (SEAT_ERR_FULL), and the model shows that.
 *   TM2 THE ACTING SEAT is the one unlocked last (seats.unlock). Its
 *       session token is the only secret the model keeps, and seats.lock
 *       forgets it. A passphrase is never kept: it goes into one request,
 *       and that request's buffer is wiped once it is answered.
 *   TM3 A WRONG ENTRY shows zxvd's back-off (wait_ms) and its status; the
 *       model never tries again on its own.
 *   TM4 BEFORE ASKING, the model holds a change to TH1 (rights over a lock)
 *       and a new passphrase to TH3; zxvd decides the rest (who may add or
 *       remove a seat) and the model shows its refusal as it came.
 *   TM5 PASSKEYS: a challenge from register_begin or auth_begin is kept
 *       until its finish uses it (single use, as zxvd's); authenticator
 *       data and signatures go through as they came and are never kept. */
#ifndef THRESHOLD_MODEL_H
#define THRESHOLD_MODEL_H

#include "threshold_rules.h"
#include "zxm.h"

#define THRESHOLD_SHOWN     64u /* seats shown at once; zxvd may hold more */
#define THRESHOLD_ID        32u /* 16 bytes as hex */
#define THRESHOLD_TOKEN     64u /* 32 bytes as hex: a session or a challenge */
#define THRESHOLD_NAME      47u /* seats.h SEAT_NAME_MAX - 1 */
#define THRESHOLD_KEYS      32u /* passkeys shown at once */
#define THRESHOLD_RP        64u
#define THRESHOLD_NOTICE    160u
#define THRESHOLD_SEATS_MAX 4096    /* zxvd_seats.c SEATS_MAX, for TH4's index only */
#define THRESHOLD_WAIT_MAX  3600000 /* seats.h SEAT_WAIT_MAX_MS */

enum { THRESHOLD_LOGIN_NONE, THRESHOLD_LOGIN_OK, THRESHOLD_LOGIN_WRONG, THRESHOLD_LOGIN_WAIT };
enum { THRESHOLD_CH_NONE, THRESHOLD_CH_REGISTER, THRESHOLD_CH_AUTH };

typedef struct {
    char id[THRESHOLD_ID + 1];
    char name[THRESHOLD_NAME + 1];
    uint8_t role; /* THRESHOLD_OWNER .. THRESHOLD_GUEST */
    uint8_t lock; /* THRESHOLD_LOCK_* */
    uint32_t color;
    uint32_t wait_ms; /* before a login try is heard again; 0: now */
} threshold_seat_t;

typedef struct {
    char cred_id[THRESHOLD_ID + 1];
    char user[THRESHOLD_ID + 1];
    uint64_t sign_count;
    int64_t registered;
    int64_t last_used; /* -1: never */
    bool revoked;
} threshold_key_t;

typedef struct {
    zxm_link_t *link;
    threshold_seat_t seat[THRESHOLD_SHOWN];
    uint32_t nseats, total;           /* shown, and all zxvd holds */
    bool auto_enter;                  /* the owner's setting */
    char auto_seat[THRESHOLD_ID + 1]; /* seats.auto: the seat entered without a login; "" none */
    /* TM2: the acting seat */
    char me[THRESHOLD_ID + 1];
    char session[THRESHOLD_TOKEN + 1];
    uint8_t my_role;
    /* TM3: the last login */
    uint8_t login;
    uint32_t wait_ms;
    bool full; /* the last seats.add answered SEAT_ERR_FULL */
    char notice[THRESHOLD_NOTICE + 1];
    /* TM5 */
    uint8_t ch_kind;
    char challenge[THRESHOLD_TOKEN + 1];
    int64_t ch_expires;
    char ch_user[THRESHOLD_ID + 1];
    bool passkey_ok; /* the last passkeys.auth_finish verified; seats.unlock takes it */
    threshold_key_t key[THRESHOLD_KEYS];
    uint32_t nkeys;
} threshold_t;

void threshold_init(threshold_t *t, zxm_link_t *link);
/* TM1: seats.list and seats.auto. */
int threshold_refresh(threshold_t *t);
const threshold_seat_t *threshold_seat(const threshold_t *t, const char *id);
/* TH2 over the roster as listed. */
bool threshold_skips_login(const threshold_t *t);

/* seats.add: role THRESHOLD_OWNER..GUEST, lock THRESHOLD_LOCK_*; pass only
 * with THRESHOLD_LOCK_PASS (else NULL). The first seat needs no session. */
int threshold_add(threshold_t *t, const char *name, uint8_t role, uint8_t lock, const char *pass,
                  uint32_t color);
int threshold_remove(threshold_t *t, const char *id);
/* seats.unlock: one try (TM3). pass NULL for a seat with no lock or a
 * passkey; passkey_ok is the host's verdict on the seat's passkey. */
int threshold_unlock(threshold_t *t, const char *id, const char *pass, bool passkey_ok);
/* seats.lock: end the acting seat's session. */
int threshold_lock(threshold_t *t);
/* seats.set_lock (TM4): old_pass when the seat had a passphrase. */
int threshold_set_lock(threshold_t *t, const char *id, uint8_t lock, const char *old_pass,
                       const char *pass);
/* seats.set_auto: the owner's word on skipping the login. */
int threshold_set_auto(threshold_t *t, bool on);

/* passkeys.* (TM5): user and cred_id are 16 bytes as hex. */
int threshold_passkey_begin(threshold_t *t, const char *user);
int threshold_passkey_finish(threshold_t *t, const char *auth_hex);
int threshold_signin_begin(threshold_t *t, const char *user);
int threshold_signin_finish(threshold_t *t, const char *cred_id, const char *auth_hex,
                            const char *sig_hex);
int threshold_passkeys(threshold_t *t, const char *user); /* NULL: every user's */
int threshold_passkey_revoke(threshold_t *t, const char *cred_id);

#endif
