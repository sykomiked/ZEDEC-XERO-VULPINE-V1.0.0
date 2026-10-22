/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* warding_model.c — Warding's model (warding_model.h WM1-WM8). */
#include "warding_model.h"

#include <stdio.h>
#include <string.h>

#define WARDING_E_OFF (-32012) /* zxvd: the feature is switched off */

static bool hex_ok(const char *s, uint32_t chars)
{
    if (!s || strlen(s) != chars) return false;
    for (uint32_t i = 0; i < chars; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

static void copy_id(char *dst, const char *src)
{
    size_t n = src ? strlen(src) : 0u;
    if (n > WARDING_ID) n = WARDING_ID;
    if (n) memcpy(dst, src, n);
    dst[n] = 0;
}

void warding_init(warding_t *w, zxm_link_t *link)
{
    memset(w, 0, sizeof *w);
    w->link = link;
    threshold_init(&w->seats, link);
}

int32_t warding_find(const warding_t *w, int64_t serial)
{
    for (uint32_t i = 0; w && i < w->nkeys; i++)
        if (w->key[i].serial == serial) return (int32_t) i;
    return -1;
}

static int32_t device_place(const warding_t *w, const char *id)
{
    for (uint32_t i = 0; id && i < w->ndev; i++)
        if (!strcmp(w->dev[i].id, id)) return (int32_t) i;
    return -1;
}

static const threshold_key_t *passkey(const warding_t *w, const char *cred)
{
    for (uint32_t i = 0; cred && i < w->seats.nkeys; i++)
        if (!strcmp(w->seats.key[i].cred_id, cred)) return &w->seats.key[i];
    return NULL;
}

/* WM2: devmesh.devices, all checked before anything changes */
static int read_roster(warding_t *w, warding_device_t *got, uint32_t *n_out, uint32_t *self_out)
{
    zxm_link_t *l = w->link;
    zxm_begin(l, "devmesh.devices");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t r = l->result;
    if (r < 0 || l->tok[r].type != W4_J_OBJ) return ZXM_E_PROTOCOL;
    int32_t arr = zxm_get(l, r, "devices");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr), selves = 0, self = 0;
    if (n > WARDING_DEVICES) return ZXM_E_PROTOCOL; /* more than devmesh holds */
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, arr, i);
        warding_device_t *d = &got[i];
        char status[16];
        memset(d, 0, sizeof *d);
        if (o < 0 || l->tok[o].type != W4_J_OBJ ||
            zxm_get_str(l, o, "id", d->id, sizeof d->id) != ZXM_OK || !hex_ok(d->id, WARDING_ID) ||
            zxm_get_str(l, o, "name", d->name, sizeof d->name) != ZXM_OK ||
            zxm_get_str(l, o, "status", status, sizeof status) != ZXM_OK ||
            zxm_get_bool(l, o, "self", &d->self) != ZXM_OK ||
            zxm_get_bool(l, o, "admin", &d->admin) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        if (!strcmp(status, "revoked"))
            d->revoked = true;
        else if (strcmp(status, "active"))
            return ZXM_E_PROTOCOL;
        for (uint32_t k = 0; k < i; k++)
            if (!strcmp(got[k].id, d->id)) return ZXM_E_PROTOCOL; /* one row a device */
        if (d->self) selves++, self = i;
    }
    if (selves > 1) return ZXM_E_PROTOCOL; /* one device is this one, at most */
    *n_out = n;
    *self_out = self;
    return ZXM_OK;
}

/* WM2: this device's word on one key, from what zxvd listed */
static void own_word(warding_t *w, warding_key_t *k)
{
    if (k->kind == WARDING_ACCOUNT) {
        const threshold_key_t *p = passkey(w, k->ref);
        if (p) k->word[w->self] = warding_device_word(true, p->revoked);
    } else if (k->kind == WARDING_DEVICE) {
        int32_t d = device_place(w, k->ref);
        if (d >= 0) k->word[w->self] = warding_device_word(true, w->dev[d].revoked);
    }
}

int warding_load(warding_t *w)
{
    warding_device_t got[WARDING_DEVICES];
    uint32_t n = 0, self = 0;
    if (!w) return ZXM_E_ARG;
    int rc = threshold_refresh(&w->seats);
    if (rc != ZXM_OK) return rc;
    rc = threshold_passkeys(&w->seats, NULL);
    if (rc == ZXM_E_REMOTE && w->link->err_code == WARDING_E_OFF)
        w->seats.nkeys = 0; /* passkeys switched off: none listed */
    else if (rc != ZXM_OK)
        return rc;
    rc = read_roster(w, got, &n, &self);
    if (rc != ZXM_OK) return rc;
    /* everything checked: commit */
    memcpy(w->dev, got, n * sizeof got[0]);
    memset(w->dev + n, 0, (WARDING_DEVICES - n) * sizeof got[0]);
    w->ndev = n;
    w->self = self;
    for (uint32_t i = 0; i < w->nkeys; i++) own_word(w, &w->key[i]);
    return ZXM_OK;
}

/* threshold.rights_ok of the acting seat over the holder's: an owner any
 * seat, a person their own, a guest none; *role the actor's */
static bool rights(const warding_t *w, const char *actor, const char *holder, int64_t *role)
{
    const threshold_seat_t *a = threshold_seat(&w->seats, actor);
    const threshold_seat_t *h = threshold_seat(&w->seats, holder);
    if (!a || !h) return false;
    if (role) *role = a->role;
    return threshold_rights(a->role, !strcmp(a->id, h->id) ? 1 : 0, h->role);
}

/* the holder's place on seats.list, from 1; 0 when not listed */
static int64_t seat_place(const warding_t *w, const char *id)
{
    for (uint32_t i = 0; id && i < w->seats.nseats; i++)
        if (!strcmp(w->seats.seat[i].id, id)) return (int64_t) i + 1;
    return 0;
}

static void say(warding_t *w, const char *a, int64_t serial, const char *b)
{
    snprintf(w->notice, sizeof w->notice, "%s%lld%s", a, (long long) serial, b);
}

int warding_issue(warding_t *w, const char *actor, int64_t serial, const char *holder, uint8_t kind,
                  int64_t item, const char *ref, int64_t days, int64_t now, int64_t chars,
                  int64_t alphabet, bool owner_confirmed)
{
    if (!w) return ZXM_E_ARG;
    /* check: the register, the limits, the holder, the thing it opens */
    if (warding_serial_ok(serial, w->last_serial) != 1 || warding_find(w, serial) >= 0)
        return ZXM_E_ARG;
    if (!warding_expiry_ok(days) || now < 0 || now > WARDING_STAMP_MAX - days * WARDING_DAY)
        return ZXM_E_ARG;
    if (kind < WARDING_DOOR || kind > WARDING_ACCOUNT || item < 1 || item > WARDING_ITEM_MAX)
        return ZXM_E_ARG;
    if (kind == WARDING_DOOR ? ref != NULL : !hex_ok(ref, WARDING_ID)) return ZXM_E_ARG;
    if (kind == WARDING_DEVICE) {
        int32_t d = device_place(w, ref);
        if (d < 0 || w->dev[d].revoked) return ZXM_E_ARG;
    }
    if (kind == WARDING_ACCOUNT) {
        const threshold_key_t *p = passkey(w, ref);
        if (!p || p->revoked) return ZXM_E_ARG;
    }
    int64_t bits = warding_entropy_bits(chars, alphabet);
    if (bits == WARDING_REFUSED) return ZXM_E_ARG;
    const threshold_seat_t *h = threshold_seat(&w->seats, holder);
    int64_t place = seat_place(w, holder), expires = now + days * WARDING_DAY;
    if (!h || !threshold_seat_fits(place - 1, h->role, h->lock, h->color, h->wait_ms) ||
        !warding_node_ok(place, kind, item, serial, expires))
        return ZXM_E_ARG;
    if (w->nkeys >= WARDING_KEYS) return ZXM_E_STATE;
    if (!rights(w, actor, holder, NULL)) return ZXM_E_STATE;
    if (!owner_confirmed) return ZXM_E_STATE;
    /* commit */
    warding_key_t *k = &w->key[w->nkeys];
    memset(k, 0, sizeof *k);
    k->serial = serial;
    k->kind = kind;
    k->item = item;
    copy_id(k->ref, ref);
    copy_id(k->holder, holder);
    k->place = place;
    k->expires = expires;
    k->bits = bits;
    k->weak = bits < WARDING_BITS_MIN;
    k->granted = true;
    k->word[w->self] = LPRES_STATE_TRUE; /* issued here: active here */
    own_word(w, k);
    w->nkeys++;
    w->last_serial = serial;
    if (k->weak)
        say(w, "Key ", serial,
            " is issued; its entropy is below 128 bits. A longer key, or a larger alphabet, is "
            "advised.");
    else
        say(w, "Key ", serial, " is issued.");
    return ZXM_OK;
}

int warding_device_says(warding_t *w, int64_t serial, uint32_t dev, bool revoked)
{
    int32_t k = warding_find(w, serial);
    if (k < 0 || dev >= WARDING_DEVICES || (w->ndev && dev >= w->ndev)) return ZXM_E_ARG;
    w->key[k].word[dev] = warding_device_word(true, revoked);
    if (warding_state(w, serial) == LPRES_STATE_BOTH)
        say(w, "Key ", serial,
            " is revoked on one device and still active on another: it is locked out everywhere "
            "until the revocation reaches every device.");
    return ZXM_OK;
}

lpres_state_t warding_state(const warding_t *w, int64_t serial)
{
    int32_t k = warding_find(w, serial);
    if (k < 0) return LPRES_STATE_NEITHER;
    return warding_key_state(w->key[k].word, WARDING_DEVICES);
}

/* the devices that say the key is revoked */
static int64_t revoked_on(const warding_key_t *k)
{
    int64_t n = 0;
    for (uint32_t i = 0; i < WARDING_DEVICES; i++) n += k->word[i] == LPRES_STATE_FALSE;
    return n;
}

bool warding_active(const warding_t *w, int64_t serial, int64_t now)
{
    int32_t k = warding_find(w, serial);
    if (k < 0) return false;
    const warding_key_t *key = &w->key[k];
    int64_t revoked = revoked_on(key), left = lockbox_days_left(key->expires, now);
    return warding_state(w, serial) == LPRES_STATE_TRUE && left != LOCKBOX_REFUSED &&
           warding_key_active(1, revoked, left) &&
           gatehouse_use_ok(1, key->granted ? 1 : 0, revoked ? 1 : 0);
}

/* devmesh.revoke: the answer says revoked */
static int revoke_device(warding_t *w, const char *id)
{
    zxm_link_t *l = w->link;
    bool done = false;
    zxm_begin(l, "devmesh.revoke");
    zxm_p_str(l, "device", id);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_bool(l, l->result, "revoked", &done) != ZXM_OK || !done) return ZXM_E_PROTOCOL;
    int32_t d = device_place(w, id);
    if (d >= 0) w->dev[d].revoked = true;
    return ZXM_OK;
}

int warding_revoke(warding_t *w, const char *actor, int64_t serial, bool owner_confirmed)
{
    int32_t k = w ? warding_find(w, serial) : -1;
    if (k < 0) return ZXM_E_ARG;
    warding_key_t *key = &w->key[k];
    if (!rights(w, actor, key->holder, NULL)) return ZXM_E_STATE;
    if (!owner_confirmed) return ZXM_E_STATE;
    int rc = ZXM_OK;
    if (key->kind == WARDING_ACCOUNT)
        rc = threshold_passkey_revoke(&w->seats, key->ref);
    else if (key->kind == WARDING_DEVICE)
        rc = revoke_device(w, key->ref);
    if (rc != ZXM_OK) return rc;
    key->word[w->self] = LPRES_STATE_FALSE;
    if (warding_state(w, serial) == LPRES_STATE_BOTH)
        say(w, "Key ", serial,
            " is revoked here; another device still holds it, so it is locked out everywhere until "
            "the revocation reaches every device.");
    else
        say(w, "Key ", serial, " is revoked.");
    return ZXM_OK;
}

int warding_door(warding_t *w, const char *actor, int64_t serial, int64_t setpoint, bool ai,
                 int64_t now, bool owner_confirmed)
{
    int32_t k = w ? warding_find(w, serial) : -1;
    int64_t role = 0;
    if (k < 0 || w->key[k].kind != WARDING_DOOR || setpoint < 0 || setpoint > 1) return ZXM_E_ARG;
    if (!rights(w, actor, w->key[k].holder, &role) || !warding_active(w, serial, now))
        return ZXM_E_STATE;
    /* homestead.device_cmd_ok for a lock (0..1): the owner's approval owed
     * unless an owner's seat confirmed it, an AI's suggestion never run */
    int64_t owed =
        homestead_owed(HOMESTEAD_LOCK, role == THRESHOLD_OWNER ? 1 : 0, owner_confirmed ? 1 : 0);
    if (!homestead_command_ok(setpoint, 1 - setpoint, owed, ai ? 1 : 0)) {
        if (ai)
            say(w, "A suggestion for the door of key ", serial,
                " is held for the owner; it is never run by itself.");
        return ZXM_E_STATE;
    }
    if (!owner_confirmed) return ZXM_E_STATE;
    w->door_held = true;
    w->door_serial = serial;
    w->door_item = w->key[k].item;
    w->door_setpoint = setpoint;
    say(w, "The owner's command for the door of key ", serial,
        " is kept for the door's bridge; zxvd has no door method yet.");
    return ZXM_OK;
}

uint32_t warding_grid(const warding_t *w, int64_t now, int64_t *cell)
{
    int64_t holder[WARDING_KEYS], item[WARDING_KEYS], live[WARDING_KEYS];
    uint32_t n = w ? w->nkeys : 0u;
    if (!cell) return 0;
    for (uint32_t i = 0; i < n; i++) {
        holder[i] = w->key[i].place;
        item[i] = (int64_t) w->key[i].kind * 10000 + w->key[i].item;
        live[i] = warding_active(w, w->key[i].serial, now) ? 1 : 0;
    }
    for (uint32_t i = 0; i < n; i++) cell[i] = warding_grid_cell(holder, item, live, n, i);
    return n;
}
