/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* warding_model.h — Warding, keys for doors, devices and accounts, as a
 * model (zxm.h): one register of the owner's keys, who holds each (a seat
 * on seats.list), what it opens (a door, a device of the owner's mesh, an
 * account's passkey), when it expires and what every device of the mesh
 * says of it. Composed of its parts: the people and the passkeys are
 * Threshold's model (seats.list, passkeys.list, passkeys.revoke), the
 * rules Threshold's, Gatehouse's, Lockbox's and Homestead's.
 *
 *   WM1 NO KEY BYTES. A key is its serial in the register (WR3) and the
 *       public ids zxvd lists (a passkey's cred_id, a device's id); the
 *       model has no field for a key's bytes and logs none.
 *   WM2 LOADED from zxvd: the seats and passkeys through Threshold
 *       (threshold_refresh, threshold_passkeys; passkeys switched off is
 *       read as no passkeys), the signed roster from devmesh.devices
 *       (WARDING_DEVICES at most, devmesh's DM_MAX_DEVICES). Everything is
 *       checked before anything changes. This device's word on each key
 *       (WR7) is then what zxvd lists: a passkey revoked or a device
 *       revoked is FALSE, a listed live one TRUE.
 *   WM3 ISSUED (warding_issue) on the owner's word: the serial enters the
 *       register only above the last (WR3), the expiry inside the limits
 *       (WR2), the holder a listed seat whose record fits
 *       (threshold.seat_ok) and over whom the acting seat has the rights
 *       (threshold.rights_ok: an owner any seat, a person their own, a
 *       guest none), the node fits (WR6); its entropy (WR4) below
 *       WARDING_BITS_MIN is advice in the notice, never a refusal. zxvd
 *       issues passkeys through Threshold's ceremony and devices through
 *       devmesh.invite; Warding registers what was issued, and a device
 *       or account key names a device on the roster or a passkey listed.
 *   WM4 EVERY DEVICE'S WORD. warding_device_says records what one device
 *       of the mesh says of a key (zxvd has no method that reads another
 *       device's keys: the view carries it). The key's state is WR7: a
 *       key revoked on one device and active on another is BOTH, locked
 *       out everywhere until the revocation reaches every device.
 *   WM5 ACTIVE (warding_active) only when its state is TRUE, WR1 says so
 *       (issued, revoked on no device, its days left lockbox.days_left
 *       1..3650) and the use is allowed (gatehouse.use_ok: asked, the
 *       owner's grant, not revoked).
 *   WM6 REVOKED (warding_revoke) on the owner's word, with the acting
 *       seat's rights: a passkey by passkeys.revoke (through Threshold),
 *       a device by devmesh.revoke, a door key in the register only (zxvd
 *       has no door method). This device's word becomes FALSE; the notice
 *       says whether other devices still hold the key (BOTH).
 *   WM7 DOORS NEED THE OWNER. warding_door never unlocks by itself: a
 *       command needs an active door key, the owner's confirmed word from
 *       an owner's seat, and homestead.device_cmd_ok for a lock (a
 *       guarded kind: homestead_owed through Warren's approval); an AI's
 *       suggestion is held, never run. zxvd has no door method, so an
 *       allowed command is kept for a bridge (door_held), never sent.
 *   WM8 THE ACCESS GRID (warding_grid): WR5 over the register, a row per
 *       key (its holder's place, its item as kind * 10000 + item, live
 *       when WM5 says active).
 *
 * Every call that changes kept state takes owner_confirmed, checked after
 * its arguments and before any request: without it ZXM_E_STATE, nothing
 * sent and nothing changed. No drawing, no allocation, integers only. */
#ifndef WARDING_MODEL_H
#define WARDING_MODEL_H

#include "gatehouse_rules.h"
#include "homestead_rules.h"
#include "lockbox_rules.h"
#include "threshold_model.h"
#include "warding_rules.h"
#include "zxm.h"

#define WARDING_KEYS       64u
#define WARDING_DEVICES    8u /* devmesh.h DM_MAX_DEVICES */
#define WARDING_ID         32u
#define WARDING_DEV_NAME   47u
#define WARDING_NOTICE_MAX 255u
#define WARDING_ITEM_MAX   INT64_C(9999) /* WR6 */
#define WARDING_DAY        INT64_C(86400)

typedef struct {
    char id[WARDING_ID + 1];
    char name[WARDING_DEV_NAME + 1];
    bool admin, self, revoked;
} warding_device_t;

typedef struct {
    int64_t serial;                      /* WR3 */
    uint8_t kind;                        /* WARDING_DOOR .. WARDING_ACCOUNT */
    int64_t item;                        /* 1..WARDING_ITEM_MAX */
    char ref[WARDING_ID + 1];            /* a device id or a passkey's cred_id; "" for a door */
    char holder[WARDING_ID + 1];         /* the holder's seat id */
    int64_t place;                       /* the holder's place on seats.list, from 1 */
    int64_t expires;                     /* seconds since 1970 */
    int64_t bits;                        /* WR4 */
    bool weak;                           /* bits below WARDING_BITS_MIN: advice */
    bool granted;                        /* the owner's grant (gatehouse.use_ok) */
    lpres_state_t word[WARDING_DEVICES]; /* WR7, by the device's place on the roster */
} warding_key_t;

typedef struct {
    zxm_link_t *link;
    threshold_t seats; /* the part: seats and passkeys */
    warding_device_t dev[WARDING_DEVICES];
    uint32_t ndev;
    uint32_t self; /* this device's place (0 when the roster is empty) */
    warding_key_t key[WARDING_KEYS];
    uint32_t nkeys;
    int64_t last_serial; /* the register's last serial, 0 before the first */
    /* WM7: a door command allowed, kept for a bridge */
    bool door_held;
    int64_t door_serial, door_item, door_setpoint;
    char notice[WARDING_NOTICE_MAX + 1];
} warding_t;

void warding_init(warding_t *w, zxm_link_t *link);
/* WM2 */
int warding_load(warding_t *w);
/* The key with serial, or -1. */
int32_t warding_find(const warding_t *w, int64_t serial);

/* WM3: actor and holder are seat ids; ref a device id (kind 2) or a
 * cred_id (kind 3), NULL for a door; days its life (WR2), now the time in
 * seconds since 1970; chars and alphabet its two lengths (WR4). */
int warding_issue(warding_t *w, const char *actor, int64_t serial, const char *holder, uint8_t kind,
                  int64_t item, const char *ref, int64_t days, int64_t now, int64_t chars,
                  int64_t alphabet, bool owner_confirmed);
/* WM4: what the device at place dev says of the key (revoked or not). */
int warding_device_says(warding_t *w, int64_t serial, uint32_t dev, bool revoked);
/* WM4: the key's state over every device's word (WR7). */
lpres_state_t warding_state(const warding_t *w, int64_t serial);
/* WM5 */
bool warding_active(const warding_t *w, int64_t serial, int64_t now);
/* WM6 */
int warding_revoke(warding_t *w, const char *actor, int64_t serial, bool owner_confirmed);
/* WM7: setpoint 0 unlocks, 1 locks; ai true for an AI's suggestion. */
int warding_door(warding_t *w, const char *actor, int64_t serial, int64_t setpoint, bool ai,
                 int64_t now, bool owner_confirmed);
/* WM8: each key's cell into cell (WARDING_KEYS at most); the rows. */
uint32_t warding_grid(const warding_t *w, int64_t now, int64_t *cell);

#endif
