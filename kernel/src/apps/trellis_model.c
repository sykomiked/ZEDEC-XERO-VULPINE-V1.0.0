/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* trellis_model.c — Trellis, peers and mesh links (see trellis_model.h).
 * Every answer is checked whole before anything of it is kept, and a
 * revocation is sent only after the owner's second yes. */
#include <stdio.h>
#include <string.h>

#include "gatehouse_rules.h"
#include "trellis_model.h"

#define TRELLIS_LINK_ROOM 512u /* the least room for a phone's pairing link */

/* ---- forms ------------------------------------------------------------------------ */

static bool hex_ok(const char *s, uint32_t len)
{
    uint32_t i = 0;
    for (; s[i]; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return i == len;
}

/* a count zxvd sends as a decimal string: 1..18 digits, no leading zero */
static bool digits_val(const char *s, int64_t *v)
{
    int64_t x = 0;
    uint32_t i = 0;
    for (; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9' || i >= 18) return false;
        if (i == 1 && s[0] == '0') return false;
        x = x * 10 + (s[i] - '0');
    }
    if (!i) return false;
    *v = x;
    return true;
}

/* XXXXX-XXXXX of 0-9 and A-Z (devmesh.invite's code) */
static bool code_ok(const char *s)
{
    if (!s) return false;
    for (uint32_t i = 0; i < 11; i++) {
        char c = s[i];
        if (i == 5) {
            if (c != '-') return false;
        } else if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')))
            return false;
    }
    return s[11] == 0;
}

/* 1..32 printable ASCII characters (phone.pair's name) */
static bool name_ok(const char *s)
{
    uint32_t i = 0;
    if (!s) return false;
    for (; s[i]; i++)
        if (s[i] < 0x20 || s[i] > 0x7e || i >= TRELLIS_NAME) return false;
    return i > 0;
}

static int64_t role_of(const char *s)
{
    if (!strcmp(s, "home")) return TRELLIS_ROLE_HOME;
    if (!strcmp(s, "thin")) return TRELLIS_ROLE_THIN;
    if (!strcmp(s, "standalone")) return TRELLIS_ROLE_STANDALONE;
    return 0;
}

static const char *role_text(int64_t r)
{
    return r == TRELLIS_ROLE_HOME ? "home" : r == TRELLIS_ROLE_THIN ? "thin" : "standalone";
}

/* a boolean member, read whole */
static bool get_flag(const zxm_link_t *l, int32_t o, const char *k, bool *v)
{
    return zxm_get_bool(l, o, k, v) == ZXM_OK;
}

/* ---- TM1: the mesh ------------------------------------------------------------------ */

void trellis_init(trellis_t *t, zxm_link_t *link)
{
    memset(t, 0, sizeof *t);
    t->link = link;
    t->net_mode = -1;
}

int trellis_read_status(trellis_t *t)
{
    zxm_link_t *l = t->link;
    bool joined, admin, pairing = false;
    char self[TRELLIS_ID + 1], ver[24], dev[TRELLIS_ID + 1] = "", sas[7] = "";
    int64_t version;
    zxm_begin(l, "devmesh.status");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t r = l->result;
    if (r < 0 || l->tok[r].type != W4_J_OBJ || !get_flag(l, r, "joined", &joined) ||
        !get_flag(l, r, "admin", &admin) ||
        zxm_get_str(l, r, "self", self, sizeof self) != ZXM_OK || !hex_ok(self, TRELLIS_ID) ||
        zxm_get_str(l, r, "roster_version", ver, sizeof ver) != ZXM_OK ||
        !digits_val(ver, &version))
        return ZXM_E_PROTOCOL;
    int32_t p = zxm_get(l, r, "pairing");
    if (p < 0) return ZXM_E_PROTOCOL;
    if (l->tok[p].type == W4_J_OBJ) {
        if (zxm_get_str(l, p, "device", dev, sizeof dev) != ZXM_OK || !hex_ok(dev, TRELLIS_ID) ||
            zxm_get_str(l, p, "sas", sas, sizeof sas) != ZXM_OK || strlen(sas) != 6)
            return ZXM_E_PROTOCOL;
        for (uint32_t i = 0; i < 6; i++) /* six digits, leading zeros kept */
            if (sas[i] < '0' || sas[i] > '9') return ZXM_E_PROTOCOL;
        pairing = true;
    } else if (l->tok[p].type != W4_J_NULL)
        return ZXM_E_PROTOCOL;
    t->known = true;
    t->joined = joined;
    t->admin = admin;
    memcpy(t->self, self, sizeof self);
    t->version = version;
    t->pairing = pairing;
    memcpy(t->pair_device, dev, sizeof dev);
    memcpy(t->sas, sas, sizeof sas);
    if (pairing)
        snprintf(t->notice, sizeof t->notice,
                 "A device waits to pair: check that both screens show %s.", sas);
    else
        snprintf(t->notice, sizeof t->notice, "%s, %s; roster version %lld.",
                 joined ? "In your device mesh" : "Not in a mesh yet",
                 admin ? "this device is an admin" : "this device is not an admin",
                 (long long) version);
    return ZXM_OK;
}

int trellis_read_devices(trellis_t *t)
{
    zxm_link_t *l = t->link;
    char ver[24];
    int64_t version;
    trellis_device_t got[TRELLIS_DEVICES];
    zxm_begin(l, "devmesh.devices");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t r = l->result;
    if (r < 0 || l->tok[r].type != W4_J_OBJ ||
        zxm_get_str(l, r, "roster_version", ver, sizeof ver) != ZXM_OK ||
        !digits_val(ver, &version))
        return ZXM_E_PROTOCOL;
    int32_t arr = zxm_get(l, r, "devices");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr);
    if (n > TRELLIS_DEVICES) return ZXM_E_PROTOCOL; /* more than devmesh holds */
    uint32_t selves = 0;
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, arr, i);
        trellis_device_t *d = &got[i];
        char role[16], status[16], since[24];
        bool admin, held;
        memset(d, 0, sizeof *d);
        if (o < 0 || l->tok[o].type != W4_J_OBJ ||
            zxm_get_str(l, o, "id", d->id, sizeof d->id) != ZXM_OK || !hex_ok(d->id, TRELLIS_ID) ||
            zxm_get_str(l, o, "name", d->name, sizeof d->name) != ZXM_OK ||
            zxm_get_str(l, o, "role", role, sizeof role) != ZXM_OK || !role_of(role) ||
            !get_flag(l, o, "admin", &admin) || !get_flag(l, o, "held", &held) ||
            zxm_get_str(l, o, "status", status, sizeof status) != ZXM_OK ||
            !get_flag(l, o, "self", &d->self) || !get_flag(l, o, "up", &d->up) ||
            zxm_get_str(l, o, "since", since, sizeof since) != ZXM_OK ||
            !digits_val(since, &d->since))
            return ZXM_E_PROTOCOL;
        if (!strcmp(status, "active"))
            d->state = TRELLIS_ACTIVE;
        else if (!strcmp(status, "revoked"))
            d->state = TRELLIS_REVOKED;
        else
            return ZXM_E_PROTOCOL;
        d->role = role_of(role);
        d->admin = admin ? 1 : 0;
        d->held = held ? 1 : 0;
        selves += d->self;
        /* TM7: the row fits ^ZXTRELLIS before the roster is kept */
        if (!trellis_peer_record_fits(i, d->state, d->role, d->admin, d->held, d->since))
            return ZXM_E_PROTOCOL;
    }
    if (selves > 1) return ZXM_E_PROTOCOL; /* one device is this one, at most */
    memcpy(t->device, got, n * sizeof got[0]);
    t->devices = n;
    t->version = version;
    t->roster_known = true;
    uint32_t active = 0, admins = 0;
    for (uint32_t i = 0; i < n; i++) {
        active += got[i].state == TRELLIS_ACTIVE;
        admins += got[i].state == TRELLIS_ACTIVE && got[i].admin;
    }
    snprintf(t->notice, sizeof t->notice,
             "%u devices in your mesh: %u active, %u revoked; %u admin%s.", n, active, n - active,
             admins, admins == 1 ? "" : "s");
    return ZXM_OK;
}

int32_t trellis_find_device(const trellis_t *t, const char *id)
{
    if (!t || !id) return -1;
    for (uint32_t i = 0; i < t->devices; i++)
        if (!strcmp(t->device[i].id, id)) return (int32_t) i;
    return -1;
}

/* ---- TM2: pairing ------------------------------------------------------------------- */

int trellis_invite(trellis_t *t, int64_t role, bool held, bool admin, int64_t now_ms)
{
    zxm_link_t *l = t->link;
    char code[TRELLIS_TEXT + 1];
    int64_t ttl;
    if (role < TRELLIS_ROLE_HOME || role > TRELLIS_ROLE_STANDALONE || now_ms < 0 ||
        now_ms > TRELLIS_MS_MAX)
        return ZXM_E_ARG;
    zxm_begin(l, "devmesh.invite");
    zxm_p_str(l, "role", role_text(role));
    zxm_p_bool(l, "held", held);
    zxm_p_bool(l, "admin", admin);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(l, l->result, "code", code, sizeof code) != ZXM_OK || !code_ok(code) ||
        zxm_get_i64(l, l->result, "expires_in_ms", &ttl) != ZXM_OK || ttl < 1 ||
        ttl > TRELLIS_INVITE_TTL_MS)
        return ZXM_E_PROTOCOL;
    memcpy(t->code, code, sizeof code);
    t->issued_ms = now_ms;
    t->ttl_ms = ttl;
    t->invited = true;
    snprintf(t->notice, sizeof t->notice,
             "Invitation %s for a %s device%s: valid %lld minutes. Type it on the new device.",
             code, role_text(role), admin ? " that may add and revoke devices" : "",
             (long long) trellis_invite_minutes_left(now_ms, ttl, now_ms));
    return ZXM_OK;
}

int64_t trellis_invite_left(const trellis_t *t, int64_t now_ms)
{
    if (!t || !t->invited) return TRELLIS_REFUSED;
    return trellis_invite_minutes_left(t->issued_ms, t->ttl_ms, now_ms);
}

int trellis_join(trellis_t *t, const char *code)
{
    zxm_link_t *l = t->link;
    bool pairing;
    if (!code_ok(code)) return ZXM_E_ARG;
    zxm_begin(l, "devmesh.join");
    zxm_p_str(l, "code", code);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (!get_flag(l, l->result, "pairing", &pairing)) return ZXM_E_PROTOCOL;
    snprintf(t->notice, sizeof t->notice, "%s",
             pairing ? "Pairing: compare the six digits on both screens."
                     : "The invitation was not taken; nothing is paired.");
    return ZXM_OK;
}

int trellis_confirm_sas(trellis_t *t, bool match)
{
    zxm_link_t *l = t->link;
    bool confirmed, in_roster;
    int64_t devices;
    if (!t->known || !t->pairing) return ZXM_E_STATE;
    zxm_begin(l, "devmesh.confirm_sas");
    zxm_p_str(l, "device", t->pair_device);
    zxm_p_bool(l, "match", match);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (!get_flag(l, l->result, "confirmed", &confirmed) ||
        !get_flag(l, l->result, "in_roster", &in_roster) ||
        zxm_get_i64(l, l->result, "devices", &devices) != ZXM_OK || devices < 0 ||
        devices > (int64_t) TRELLIS_DEVICES)
        return ZXM_E_PROTOCOL;
    t->pairing = false;
    t->pair_device[0] = 0;
    t->sas[0] = 0;
    if (!match)
        snprintf(t->notice, sizeof t->notice,
                 "The digits did not match: pairing stopped. Someone may be in the middle.");
    else
        snprintf(t->notice, sizeof t->notice, "%s %lld devices in your mesh.",
                 in_roster ? "Paired." : "Confirmed; the inviting device adds it.",
                 (long long) devices);
    return ZXM_OK;
}

/* ---- TM3: phones ------------------------------------------------------------------- */

int trellis_read_phones(trellis_t *t)
{
    zxm_link_t *l = t->link;
    bool on;
    trellis_phone_t got[TRELLIS_PHONES];
    zxm_begin(l, "phone.list");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t r = l->result;
    if (r < 0 || l->tok[r].type != W4_J_OBJ || !get_flag(l, r, "on", &on)) return ZXM_E_PROTOCOL;
    int32_t arr = zxm_get(l, r, "phones");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr), kept = 0;
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, arr, i);
        trellis_phone_t p;
        memset(&p, 0, sizeof p);
        if (o < 0 || l->tok[o].type != W4_J_OBJ ||
            zxm_get_str(l, o, "phone_id", p.id, sizeof p.id) != ZXM_OK ||
            !hex_ok(p.id, TRELLIS_ID) ||
            zxm_get_str(l, o, "name", p.name, sizeof p.name) != ZXM_OK || !name_ok(p.name) ||
            !get_flag(l, o, "allow_post", &p.allow_post) ||
            zxm_get_i64(l, o, "connected", &p.connected) != ZXM_OK || p.connected < 0)
            return ZXM_E_PROTOCOL;
        if (kept < TRELLIS_PHONES) got[kept++] = p;
    }
    memcpy(t->phone, got, kept * sizeof got[0]);
    t->phones = kept;
    t->phones_known = true;
    t->phone_link_on = on;
    snprintf(t->notice, sizeof t->notice, "%u paired phone%s%s; the phone link is %s.", n,
             n == 1 ? "" : "s", n > kept ? ", the first 16 kept" : "", on ? "on" : "off");
    return ZXM_OK;
}

int trellis_pair_phone(trellis_t *t, const char *name, bool allow_post, char *link_out,
                       uint32_t cap)
{
    zxm_link_t *l = t->link;
    trellis_phone_t p;
    if (!name_ok(name) || !link_out || cap < TRELLIS_LINK_ROOM) return ZXM_E_ARG;
    link_out[0] = 0;
    zxm_begin(l, "phone.pair");
    zxm_p_str(l, "name", name);
    zxm_p_bool(l, "allow_post", allow_post);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    memset(&p, 0, sizeof p);
    int32_t r = l->result;
    rc = ZXM_E_PROTOCOL;
    if (r >= 0 && zxm_get_str(l, r, "phone_id", p.id, sizeof p.id) == ZXM_OK &&
        hex_ok(p.id, TRELLIS_ID) && zxm_get_str(l, r, "name", p.name, sizeof p.name) == ZXM_OK &&
        get_flag(l, r, "allow_post", &p.allow_post) && p.allow_post == allow_post &&
        zxm_get_str(l, r, "link", link_out, cap) == ZXM_OK &&
        !strncmp(link_out, "zxv-phone:1?", 12))
        rc = ZXM_OK;
    /* the link holds the phone's secret key: handed out once, never kept here */
    memset(l->resp, 0, sizeof l->resp);
    if (rc != ZXM_OK) {
        memset(link_out, 0, cap);
        return rc;
    }
    if (t->phones < TRELLIS_PHONES) t->phone[t->phones++] = p;
    snprintf(t->notice, sizeof t->notice, "Paired %s. Show its code once, on this screen only.%s",
             p.name,
             allow_post ? " It may move money and change the network; revoke it if it is lost."
                        : "");
    return ZXM_OK;
}

bool trellis_phone_may_post(const trellis_t *t, uint32_t i, int64_t asked)
{
    if (!t || i >= t->phones) return false;
    /* TL5: gatehouse.use_ok, used as it is */
    return gatehouse_use_ok(asked, t->phone[i].allow_post ? 1 : 0, t->phone[i].revoked ? 1 : 0);
}

/* ---- TM4: revoking ------------------------------------------------------------------- */

static int64_t admins_left(const trellis_t *t, uint32_t i)
{
    int64_t admins = 0;
    for (uint32_t k = 0; k < t->devices; k++)
        admins += t->device[k].state == TRELLIS_ACTIVE && t->device[k].admin == 1;
    if (t->device[i].state == TRELLIS_ACTIVE && t->device[i].admin == 1) admins--;
    return admins;
}

bool trellis_device_revocable(const trellis_t *t, uint32_t i)
{
    if (!t || !t->known || !t->roster_known || i >= t->devices) return false;
    const trellis_device_t *d = &t->device[i];
    return trellis_revoke_allowed(t->admin ? 1 : 0, d->self ? 1 : 0,
                                  d->state == TRELLIS_ACTIVE ? 1 : 0, admins_left(t, i));
}

int trellis_revoke_device(trellis_t *t, uint32_t i)
{
    if (!t->known || !t->roster_known || t->revoke_kind != TRELLIS_NOTHING) return ZXM_E_STATE;
    if (i >= t->devices) return ZXM_E_ARG;
    const trellis_device_t *d = &t->device[i];
    if (!trellis_device_revocable(t, i)) {
        const char *why = !t->admin                    ? "this device is not an admin"
                          : d->self                    ? "it is this device"
                          : d->state != TRELLIS_ACTIVE ? "it is already revoked"
                                                       : "it is the last admin";
        snprintf(t->notice, sizeof t->notice, "%s cannot be revoked here: %s.", d->name, why);
        return ZXM_OK;
    }
    t->revoke_kind = TRELLIS_DEVICE;
    t->revoke_at = i;
    t->revoke_yes = 0;
    snprintf(t->notice, sizeof t->notice,
             "Revoke %s? It can never come back with the same key. Confirm twice.", d->name);
    return ZXM_OK;
}

int trellis_revoke_phone(trellis_t *t, uint32_t i)
{
    if (!t->phones_known || t->revoke_kind != TRELLIS_NOTHING) return ZXM_E_STATE;
    if (i >= t->phones) return ZXM_E_ARG;
    if (t->phone[i].revoked) {
        snprintf(t->notice, sizeof t->notice, "%s is already revoked.", t->phone[i].name);
        return ZXM_OK;
    }
    t->revoke_kind = TRELLIS_PHONE;
    t->revoke_at = i;
    t->revoke_yes = 0;
    snprintf(t->notice, sizeof t->notice,
             "Revoke the phone %s? Its pairing ends for good. Confirm twice.", t->phone[i].name);
    return ZXM_OK;
}

static int send_device_revoke(trellis_t *t, trellis_device_t *d)
{
    zxm_link_t *l = t->link;
    bool revoked;
    char ver[24];
    int64_t version;
    zxm_begin(l, "devmesh.revoke");
    zxm_p_str(l, "device", d->id);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (!get_flag(l, l->result, "revoked", &revoked) || !revoked ||
        zxm_get_str(l, l->result, "roster_version", ver, sizeof ver) != ZXM_OK ||
        !digits_val(ver, &version) || version <= t->version)
        return ZXM_E_PROTOCOL; /* a roster version only goes up */
    d->state = TRELLIS_REVOKED;
    d->since = version;
    t->version = version;
    snprintf(t->notice, sizeof t->notice, "%s is revoked for good (roster version %lld).", d->name,
             (long long) version);
    return ZXM_OK;
}

static int send_phone_revoke(trellis_t *t, trellis_phone_t *p)
{
    zxm_link_t *l = t->link;
    bool revoked;
    int64_t closed;
    zxm_begin(l, "phone.revoke");
    zxm_p_str(l, "phone_id", p->id);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (!get_flag(l, l->result, "revoked", &revoked) || !revoked ||
        zxm_get_i64(l, l->result, "closed", &closed) != ZXM_OK || closed < 0)
        return ZXM_E_PROTOCOL;
    p->revoked = true;
    p->allow_post = false;
    p->connected = 0;
    snprintf(t->notice, sizeof t->notice, "The phone %s is revoked; %lld connection%s closed.",
             p->name, (long long) closed, closed == 1 ? "" : "s");
    return ZXM_OK;
}

int trellis_revoke_confirm(trellis_t *t, bool yes)
{
    int32_t kind = t->revoke_kind;
    uint32_t i = t->revoke_at;
    const char *name = kind == TRELLIS_DEVICE  ? t->device[i].name
                       : kind == TRELLIS_PHONE ? t->phone[i].name
                                               : "";
    if (kind == TRELLIS_NOTHING) return ZXM_E_STATE;
    if (!yes) {
        t->revoke_kind = TRELLIS_NOTHING;
        t->revoke_yes = 0;
        snprintf(t->notice, sizeof t->notice, "%s is kept; nothing was revoked.", name);
        return ZXM_OK;
    }
    if (t->revoke_yes == 0) {
        t->revoke_yes = 1;
        snprintf(t->notice, sizeof t->notice,
                 "Are you sure? %s is revoked for good and can never be undone.", name);
        return ZXM_OK;
    }
    /* the second yes: the word is spent whatever zxvd answers */
    t->revoke_kind = TRELLIS_NOTHING;
    t->revoke_yes = 0;
    return kind == TRELLIS_DEVICE ? send_device_revoke(t, &t->device[i])
                                  : send_phone_revoke(t, &t->phone[i]);
}

/* ---- TM5: peers ------------------------------------------------------------------------ */

int trellis_read_peers(trellis_t *t)
{
    zxm_link_t *l = t->link;
    trellis_peer_t got[TRELLIS_PEERS];
    zxm_begin(l, "net.peers");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t r = l->result;
    if (r < 0 || l->tok[r].type != W4_J_OBJ) return ZXM_E_PROTOCOL;
    int32_t arr = zxm_get(l, r, "peers");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr), kept = 0, members = 0;
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, arr, i);
        trellis_peer_t p;
        char seen[24];
        memset(&p, 0, sizeof p);
        if (o < 0 || l->tok[o].type != W4_J_OBJ ||
            zxm_get_str(l, o, "id", p.id, sizeof p.id) != ZXM_OK || !hex_ok(p.id, TRELLIS_NODE) ||
            zxm_get_str(l, o, "addr", p.addr, sizeof p.addr) != ZXM_OK ||
            zxm_get_str(l, o, "last_seen_ms", seen, sizeof seen) != ZXM_OK ||
            !digits_val(seen, &p.last_seen_ms) || !get_flag(l, o, "member", &p.member) ||
            !get_flag(l, o, "in_book", &p.in_book))
            return ZXM_E_PROTOCOL;
        members += p.member;
        if (kept < TRELLIS_PEERS) got[kept++] = p;
    }
    memcpy(t->peer, got, kept * sizeof got[0]);
    t->peers = n;
    t->peers_kept = kept;
    snprintf(t->notice, sizeof t->notice, "%u peer%s, %u admitted to the commons%s.", n,
             n == 1 ? "" : "s", members, n ? "" : " (none while the network is off)");
    return ZXM_OK;
}

int trellis_net(trellis_t *t, int64_t mode)
{
    zxm_link_t *l = t->link;
    char answer[TRELLIS_TEXT + 1];
    static const char *const MODE[3] = {"off", "lan", "online"};
    if (mode < TRELLIS_NET_OFF || mode > TRELLIS_NET_ONLINE) return ZXM_E_ARG;
    zxm_begin(l, "net.command");
    zxm_p_str(l, "command", MODE[mode]);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    rc = zxm_get_str(l, l->result, "result", answer, sizeof answer);
    if (rc == ZXM_E_TOOBIG) rc = ZXM_OK; /* a long answer is cut, the mode still taken */
    if (rc != ZXM_OK) return ZXM_E_PROTOCOL;
    memcpy(t->net_answer, answer, sizeof answer);
    t->net_mode = mode;
    snprintf(t->notice, sizeof t->notice, "The peer network is %s%s.", MODE[mode],
             mode == TRELLIS_NET_LAN ? ", this local network only" : "");
    return ZXM_OK;
}

/* ---- TM6, TM7 ---------------------------------------------------------------------------- */

int64_t trellis_count(const trellis_t *t, int64_t state)
{
    int64_t s[TRELLIS_DEVICES + TRELLIS_PHONES + 1], w[TRELLIS_DEVICES + TRELLIS_PHONES + 1];
    uint32_t n = 0;
    if (!t) return TRELLIS_REFUSED;
    for (uint32_t i = 0; i < t->devices; i++) s[n++] = t->device[i].state;
    for (uint32_t i = 0; i < t->phones; i++)
        s[n++] = t->phone[i].revoked ? TRELLIS_REVOKED : TRELLIS_ACTIVE;
    if (t->pairing) s[n++] = TRELLIS_WAITING;
    for (uint32_t i = 0; i < n; i++) w[i] = state;
    return trellis_peers_in_state(s, w, n);
}

bool trellis_record_ok(const trellis_t *t, uint32_t i)
{
    if (!t || i >= t->devices) return false;
    const trellis_device_t *d = &t->device[i];
    return trellis_peer_record_fits(i, d->state, d->role, d->admin, d->held, d->since);
}
