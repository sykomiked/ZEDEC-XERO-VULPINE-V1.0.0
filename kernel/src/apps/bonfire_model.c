/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bonfire_model.c — Bonfire, events and gatherings (bonfire_model.h BM1-BM8). */
#include "bonfire_model.h"

#include <stdio.h>
#include <string.h>

#include "meadow_rules.h"
#include "seedbank_rules.h"
#include "witness_rules.h"

const char BONFIRE_NO_DELETE[] = "An announcement in a public space cannot be deleted: every "
                                 "member keeps it. Confirm to post.";

static bool is_null(const zxm_link_t *l, int32_t obj, const char *key)
{
    int32_t t = zxm_get(l, obj, key);
    return t >= 0 && l->tok[t].type == W4_J_NULL;
}

/* a string that may be null or missing ("") */
static int get_opt_str(const zxm_link_t *l, int32_t obj, const char *key, char *out, uint32_t cap)
{
    out[0] = 0;
    if (zxm_get(l, obj, key) < 0 || is_null(l, obj, key)) return ZXM_OK;
    return zxm_get_str(l, obj, key, out, cap) == ZXM_OK ? ZXM_OK : ZXM_E_PROTOCOL;
}

/* 1..max bytes of whole UTF-8, no control bytes */
static bool text_ok(const char *s, uint32_t max)
{
    uint32_t n = 0;
    if (!s) return false;
    while (n <= max && s[n]) {
        if ((unsigned char) s[n] < 0x20u || s[n] == 0x7f) return false;
        n++;
    }
    return n >= 1u && n <= max && zxm_utf8_cut(s, n) == n;
}

/* BM6: 0..24 of [a-z0-9-], pspace.create's topic */
static bool topic_ok(const char *s)
{
    uint32_t n = s ? (uint32_t) strlen(s) : 0u;
    if (n > BONFIRE_TOPIC_MAX) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '-'))
            return false;
    return true;
}

static bool hex_ok(const char *s, uint32_t len)
{
    if (!s || strlen(s) != len) return false;
    for (uint32_t i = 0; i < len; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return true;
}

/* BM6: an answer of -32012 marks the feature off, any answer from zxvd tells whether it is */
static int call(bonfire_t *b)
{
    int rc = zxm_call(b->link);
    if (rc == ZXM_OK) b->off = false;
    if (rc == ZXM_E_REMOTE) b->off = b->link->err_code == BONFIRE_E_OFF;
    return rc;
}

static void say(bonfire_t *b, const char *text)
{
    size_t n = strlen(text);
    if (n > BONFIRE_NOTICE_MAX) n = BONFIRE_NOTICE_MAX;
    memcpy(b->notice, text, n);
    b->notice[n] = 0;
}

void bonfire_init(bonfire_t *b, zxm_link_t *link)
{
    memset(b, 0, sizeof *b);
    b->link = link;
}

/* ---- venues (BM1) ------------------------------------------------------------- */

int bonfire_places(bonfire_t *b)
{
    static bonfire_place_t fresh[BONFIRE_PLACES];
    char note[BONFIRE_NOTE_MAX + 1];
    bool reset = false;
    zxm_begin(b->link, "place.list");
    int rc = call(b);
    if (rc != ZXM_OK) return rc;
    int32_t o = b->link->result;
    int32_t arr = zxm_get(b->link, o, "places");
    if (zxm_get_bool(b->link, o, "reset", &reset) != ZXM_OK || arr < 0 ||
        b->link->tok[arr].type != W4_J_ARR)
        return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(b->link, arr);
    if (n > BONFIRE_PLACES) return ZXM_E_TOOBIG;
    for (uint32_t i = 0; i < n; i++) {
        int32_t p = zxm_at(b->link, arr, i);
        bonfire_place_t *x = &fresh[i];
        memset(x, 0, sizeof *x);
        x->distance_m = -1;
        if (zxm_get_str(b->link, p, "name", x->name, sizeof x->name) != ZXM_OK || !x->name[0] ||
            zxm_get_i64(b->link, p, "lat", &x->lat) != ZXM_OK ||
            zxm_get_i64(b->link, p, "lon", &x->lon) != ZXM_OK || x->lat < -BONFIRE_LAT_MAX ||
            x->lat > BONFIRE_LAT_MAX || x->lon < -BONFIRE_LON_MAX || x->lon > BONFIRE_LON_MAX ||
            get_opt_str(b->link, p, "note", note, sizeof note) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        if (zxm_get(b->link, p, "distance_m") >= 0 && !is_null(b->link, p, "distance_m") &&
            (zxm_get_i64(b->link, p, "distance_m", &x->distance_m) != ZXM_OK || x->distance_m < 0))
            return ZXM_E_PROTOCOL;
    }
    /* a venue past the new list is no venue any more */
    for (uint32_t g = 0; g < b->ngathering; g++)
        if (b->gathering[g].venue >= (int64_t) n) b->gathering[g].venue = -1;
    memcpy(b->place, fresh, n * sizeof fresh[0]);
    b->nplace = n;
    b->reset = reset;
    return ZXM_OK;
}

/* ---- gatherings and their lists (BM2, BM3) ------------------------------------ */

int bonfire_plan(bonfire_t *b, const char *name, int64_t start, int64_t seats, int64_t venue,
                 uint32_t *at)
{
    if (!at || !text_ok(name, BONFIRE_NAME_MAX) || start < 0 || start > BONFIRE_START_MAX ||
        seats < 1 || seats > BONFIRE_SEATS_MAX || venue < -1 || venue >= (int64_t) b->nplace)
        return ZXM_E_ARG;
    if (b->ngathering >= BONFIRE_GATHERINGS) return ZXM_E_STATE;
    uint32_t g = b->ngathering;
    if (!bonfire_event_record_ok(g, start, seats, 0, venue)) return ZXM_E_ARG; /* BR4 */
    bonfire_gathering_t *x = &b->gathering[g];
    memset(x, 0, sizeof *x);
    memcpy(x->name, name, strlen(name) + 1u);
    x->start = start;
    x->seats = seats;
    x->venue = venue;
    x->present = -1;
    b->ngathering++;
    *at = g;
    return ZXM_OK;
}

int32_t bonfire_guest_of(const bonfire_t *b, uint32_t g, const char *guest)
{
    if (g >= b->ngathering || !guest) return -1;
    const bonfire_gathering_t *x = &b->gathering[g];
    for (uint32_t i = 0; i < x->nguest; i++)
        if (!strcmp(x->guest[i].name, guest)) return (int32_t) i;
    return -1;
}

int bonfire_invite(bonfire_t *b, uint32_t g, const char *guest, uint32_t *at)
{
    if (!at || g >= b->ngathering || !text_ok(guest, BONFIRE_GUEST_NAME)) return ZXM_E_ARG;
    bonfire_gathering_t *x = &b->gathering[g];
    if (bonfire_guest_of(b, g, guest) >= 0 || x->nguest >= BONFIRE_GUESTS) return ZXM_E_STATE;
    uint32_t i = x->nguest;
    if (!bonfire_invite_record_ok(g, i, BONFIRE_NONE, 1, 0)) return ZXM_E_STATE; /* BR5 */
    bonfire_guest_t *y = &x->guest[i];
    memset(y, 0, sizeof *y);
    memcpy(y->name, guest, strlen(guest) + 1u);
    y->answer = BONFIRE_NONE;
    y->party = 1;
    x->nguest++;
    *at = i;
    return ZXM_OK;
}

/* ---- answers (BM4) ------------------------------------------------------------ */

/* the yes parties' sum (BR3) over the list, with guest `skip`'s row left out (-1: none) */
static int64_t going_without(const bonfire_gathering_t *x, int32_t skip)
{
    int64_t party[BONFIRE_GUESTS], answer[BONFIRE_GUESTS];
    uint32_t n = 0;
    for (uint32_t i = 0; i < x->nguest; i++) {
        if ((int32_t) i == skip) continue;
        party[n] = x->guest[i].party;
        answer[n] = x->guest[i].answer;
        n++;
    }
    return bonfire_rsvp_headcount(party, answer, n);
}

bool bonfire_may_join(const bonfire_t *b, uint32_t g, int32_t guest, int64_t party)
{
    if (g >= b->ngathering) return false;
    const bonfire_gathering_t *x = &b->gathering[g];
    int64_t listpos = guest >= 0 && (uint32_t) guest < x->nguest ? guest : -1;
    int64_t going = going_without(x, (int32_t) listpos);
    if (going == BONFIRE_REFUSED) return false;
    return bonfire_guest_may_join(listpos, x->seats, going, party);
}

int bonfire_answer(bonfire_t *b, uint32_t g, uint32_t guest, int64_t answer, int64_t party,
                   int64_t stamp)
{
    if (g >= b->ngathering || guest >= b->gathering[g].nguest || answer < BONFIRE_NONE ||
        answer >= BONFIRE_ANSWERS || party < 1 || party > BONFIRE_PARTY_MAX || stamp < 0 ||
        !bonfire_invite_record_ok(g, guest, answer, party, stamp)) /* BR5 */
        return ZXM_E_ARG;
    bonfire_gathering_t *x = &b->gathering[g];
    if (answer == BONFIRE_YES && !bonfire_may_join(b, g, (int32_t) guest, party))
        return ZXM_E_STATE; /* BR1: no seat for this party */
    bonfire_guest_t keep = x->guest[guest];
    x->guest[guest].answer = answer;
    x->guest[guest].party = party;
    x->guest[guest].stamp = stamp;
    int64_t going = going_without(x, -1);
    if (going == BONFIRE_REFUSED || bonfire_seats_left(x->seats, going) == BONFIRE_REFUSED ||
        !bonfire_event_record_ok(g, x->start, x->seats, going, x->venue)) {
        x->guest[guest] = keep; /* nothing changes */
        return ZXM_E_STATE;
    }
    x->going = going;
    return ZXM_OK;
}

int64_t bonfire_left(const bonfire_t *b, uint32_t g)
{
    if (g >= b->ngathering) return BONFIRE_REFUSED;
    return bonfire_seats_left(b->gathering[g].seats, b->gathering[g].going);
}

int64_t bonfire_full_x100(const bonfire_t *b, uint32_t g)
{
    int64_t left = bonfire_left(b, g);
    if (left == BONFIRE_REFUSED) return BONFIRE_REFUSED;
    return bonfire_fill_x100(b->gathering[g].seats, left);
}

int64_t bonfire_answered(const bonfire_t *b, uint32_t g, int64_t answer)
{
    int64_t kind[BONFIRE_GUESTS];
    if (g >= b->ngathering || answer < BONFIRE_NONE || answer >= BONFIRE_ANSWERS)
        return BONFIRE_REFUSED;
    const bonfire_gathering_t *x = &b->gathering[g];
    for (uint32_t i = 0; i < x->nguest; i++) kind[i] = x->guest[i].answer;
    return witness_kind_count(kind, x->nguest, answer);
}

/* ---- the notice (BM5) --------------------------------------------------------- */

int bonfire_notice(bonfire_t *b, uint32_t g)
{
    int64_t left = bonfire_left(b, g);
    if (left == BONFIRE_REFUSED) return ZXM_E_ARG;
    const bonfire_gathering_t *x = &b->gathering[g];
    int w = left == 0 ? snprintf(b->notice, sizeof b->notice, "%s is full (%lld coming).", x->name,
                                 (long long) x->going)
                      : snprintf(b->notice, sizeof b->notice, "%s: %lld of %lld seats left.",
                                 x->name, (long long) left, (long long) x->seats);
    return w < 0 || (uint32_t) w > BONFIRE_NOTICE_MAX ? ZXM_E_TOOBIG : ZXM_OK;
}

/* ---- opening (BM6) ------------------------------------------------------------ */

int bonfire_open_space(bonfire_t *b, uint32_t g, const char *topic, bool owner_confirmed)
{
    char cid[BONFIRE_CID_MAX + 1];
    if (g >= b->ngathering || !topic_ok(topic)) return ZXM_E_ARG;
    bonfire_gathering_t *x = &b->gathering[g];
    if (x->cid[0]) return ZXM_E_STATE; /* already open */
    if (!owner_confirmed) return ZXM_E_STATE;
    zxm_begin(b->link, "pspace.create");
    zxm_p_str(b->link, "kind", "room");
    zxm_p_str(b->link, "gov", "owner");
    zxm_p_str(b->link, "name", x->name);
    if (topic && topic[0]) zxm_p_str(b->link, "topic", topic);
    int rc = call(b);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(b->link, b->link->result, "cid", cid, sizeof cid) != ZXM_OK ||
        !seedbank_cid_ok(cid, (uint32_t) strlen(cid)))
        return ZXM_E_PROTOCOL;
    memcpy(x->cid, cid, strlen(cid) + 1u);
    return ZXM_OK;
}

int bonfire_open_group(bonfire_t *b, uint32_t g, bool owner_confirmed)
{
    char group[BONFIRE_GROUP_HEX + 1], role[16];
    bool open = true;
    if (g >= b->ngathering) return ZXM_E_ARG;
    bonfire_gathering_t *x = &b->gathering[g];
    if (x->group[0]) return ZXM_E_STATE; /* already open */
    if (!owner_confirmed) return ZXM_E_STATE;
    zxm_begin(b->link, "social.group_create");
    zxm_p_str(b->link, "name", x->name);
    zxm_p_bool(b->link, "open", false);
    int rc = call(b);
    if (rc != ZXM_OK) return rc;
    int32_t o = b->link->result;
    if (zxm_get_str(b->link, o, "group", group, sizeof group) != ZXM_OK ||
        !hex_ok(group, BONFIRE_GROUP_HEX) || zxm_get_bool(b->link, o, "open", &open) != ZXM_OK ||
        open || zxm_get_str(b->link, o, "my_role", role, sizeof role) != ZXM_OK ||
        strcmp(role, "owner"))
        return ZXM_E_PROTOCOL;
    memcpy(x->group, group, sizeof group);
    return ZXM_OK;
}

/* ---- announcing (BM7) --------------------------------------------------------- */

int bonfire_announcement(const bonfire_t *b, uint32_t g, char *out, uint32_t cap)
{
    int64_t left = bonfire_left(b, g);
    if (!out || !cap || left == BONFIRE_REFUSED) return ZXM_E_ARG;
    const bonfire_gathering_t *x = &b->gathering[g];
    int w = x->venue >= 0 && (uint32_t) x->venue < b->nplace
                ? snprintf(out, cap,
                           "Gathering: %s. Starts %lld (seconds since 1970). %lld of %lld "
                           "seats left. At %s.",
                           x->name, (long long) x->start, (long long) left, (long long) x->seats,
                           b->place[x->venue].name)
                : snprintf(out, cap,
                           "Gathering: %s. Starts %lld (seconds since 1970). %lld of %lld "
                           "seats left.",
                           x->name, (long long) x->start, (long long) left, (long long) x->seats);
    return w < 0 || (uint32_t) w >= cap ? ZXM_E_TOOBIG : ZXM_OK;
}

int bonfire_announce(bonfire_t *b, uint32_t g, bool owner_confirmed)
{
    static char text[BONFIRE_ANNOUNCE + 1];
    char got[BONFIRE_CID_MAX + 1];
    bool posted = false;
    if (g >= b->ngathering) return ZXM_E_ARG;
    bonfire_gathering_t *x = &b->gathering[g];
    int rc = bonfire_announcement(b, g, text, sizeof text);
    if (rc != ZXM_OK) return rc;
    /* Meadow's MR2: this node made the room, so its seat is the public's in a park (MM1) */
    if (!x->cid[0] || b->off ||
        !meadow_seat_may_post(MEADOW_PARK, MEADOW_PUBLIC, (int64_t) strlen(text)))
        return ZXM_E_STATE;
    if (!owner_confirmed) { /* a post stays: say so, send nothing */
        say(b, BONFIRE_NO_DELETE);
        return ZXM_E_STATE;
    }
    b->notice[0] = 0;
    zxm_begin(b->link, "pspace.post");
    zxm_p_str(b->link, "cid", x->cid);
    zxm_p_str(b->link, "text", text);
    if ((rc = call(b)) != ZXM_OK) return rc;
    int32_t o = b->link->result;
    return zxm_get_str(b->link, o, "cid", got, sizeof got) == ZXM_OK && !strcmp(got, x->cid) &&
                   zxm_get_bool(b->link, o, "posted", &posted) == ZXM_OK && posted
               ? ZXM_OK
               : ZXM_E_PROTOCOL;
}

/* ---- who is there (BM8) ------------------------------------------------------- */

int bonfire_present(bonfire_t *b, uint32_t g)
{
    int64_t count = 0;
    if (g >= b->ngathering) return ZXM_E_ARG;
    bonfire_gathering_t *x = &b->gathering[g];
    if (!x->cid[0]) return ZXM_E_STATE;
    zxm_begin(b->link, "pspace.members");
    zxm_p_str(b->link, "cid", x->cid);
    int rc = call(b);
    if (rc != ZXM_OK) return rc;
    int32_t o = b->link->result;
    int32_t arr = zxm_get(b->link, o, "members");
    if (arr < 0 || b->link->tok[arr].type != W4_J_ARR ||
        zxm_get_i64(b->link, o, "count", &count) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(b->link, arr);
    /* the count is the list and, once in, this node (zxvd_pspace.c s_members) */
    if (count < (int64_t) n || count > (int64_t) n + 1 || count > BONFIRE_SPACE_MAX)
        return ZXM_E_PROTOCOL;
    x->present = count;
    return ZXM_OK;
}
