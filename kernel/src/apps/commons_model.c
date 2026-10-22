/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* commons_model.c — Commons, chat (commons_model.h CM1-CM7). */
#include "commons_model.h"

#include <stdio.h>
#include <string.h>

#include "witness_rules.h"

static const char *const KIND[] = {"post", "reply", "reaction"};
static const char *const ROLE[] = {"none", "observer", "member", "admin", "owner"};
static const char *const PHASE[] = {"searching", "joining", "in", "not_found"};

/* ---- text and answers ------------------------------------------------------ */

static int32_t code_of(const char *s, const char *const *names, int32_t n)
{
    for (int32_t i = 0; i < n; i++)
        if (!strcmp(s, names[i])) return i;
    return -1;
}

static bool is_null(const zxm_link_t *l, int32_t obj, const char *key)
{
    int32_t t = zxm_get(l, obj, key);
    return t >= 0 && l->tok[t].type == W4_J_NULL;
}

/* exactly len lower-case hex digits */
static bool hex_ok(const char *s, uint32_t len)
{
    if (!s || strlen(s) != len) return false;
    for (uint32_t i = 0; i < len; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return true;
}

/* a space CID: "b" and base32 lower case, at most 79 bytes */
static bool cid_ok(const char *s)
{
    uint32_t n = s ? (uint32_t) strlen(s) : 0u;
    if (n < 2u || n > COMMONS_CID_MAX || s[0] != 'b') return false;
    for (uint32_t i = 1; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '2' && s[i] <= '7'))) return false;
    return true;
}

/* 1..max bytes with no control byte */
static bool line_ok(const char *s, uint32_t max)
{
    uint32_t n = s ? (uint32_t) strlen(s) : 0u;
    if (n < 1u || n > max) return false;
    for (uint32_t i = 0; i < n; i++)
        if ((unsigned char) s[i] < 0x20u || s[i] == 0x7f) return false;
    return true;
}

/* CR2 and something besides spaces (zxvd's chat.post asks the same) */
static bool message_ok(const char *s)
{
    if (!s || !commons_message_text_ok((int64_t) strlen(s))) return false;
    for (const char *p = s; *p; p++)
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') return true;
    return false;
}

/* an IRC channel: "#" and 1..49 bytes with no space, comma or control */
static bool chan_ok(const char *s)
{
    if (!line_ok(s, COMMONS_CHAN_MAX) || s[0] != '#' || !s[1]) return false;
    for (const char *p = s; *p; p++)
        if (*p == ' ' || *p == ',') return false;
    return true;
}

/* a directory topic: 0..24 of a-z 0-9 - */
static bool topic_ok(const char *s)
{
    uint32_t n = (uint32_t) strlen(s);
    if (n > COMMONS_TOPIC_MAX) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '-'))
            return false;
    return true;
}

/* a decimal of at most 18 digits (zxvd's Lamport times and ms as text) */
static int get_dec(const zxm_link_t *l, int32_t obj, const char *key, int64_t *v)
{
    char s[24];
    int64_t x = 0;
    uint32_t n = 0;
    if (zxm_get_str(l, obj, key, s, sizeof s) != ZXM_OK) return ZXM_E_PROTOCOL;
    for (; s[n]; n++) {
        if (s[n] < '0' || s[n] > '9' || n >= 18u) return ZXM_E_PROTOCOL;
        x = x * 10 + (s[n] - '0');
    }
    if (!n) return ZXM_E_PROTOCOL;
    *v = x;
    return ZXM_OK;
}

/* an integer at least 0 */
static int get_count(const zxm_link_t *l, int32_t obj, const char *key, int64_t *v)
{
    if (zxm_get_i64(l, obj, key, v) != ZXM_OK || *v < 0) return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

/* a string that may be null or missing ("") */
static int get_opt_str(const zxm_link_t *l, int32_t obj, const char *key, char *out, uint32_t cap)
{
    out[0] = 0;
    if (zxm_get(l, obj, key) < 0 || is_null(l, obj, key)) return ZXM_OK;
    return zxm_get_str(l, obj, key, out, cap) == ZXM_OK ? ZXM_OK : ZXM_E_PROTOCOL;
}

/* ---- rooms (CM1) ----------------------------------------------------------- */

void commons_init(commons_t *c, zxm_link_t *link)
{
    memset(c, 0, sizeof *c);
    c->link = link;
    c->room[0].kind = COMMONS_ROOM_HERE;
    memcpy(c->room[0].name, "this node", sizeof "this node");
    c->room[0].members = 1;
    c->nroom = 1;
}

int32_t commons_find(const commons_t *c, const char *key)
{
    if (!key) return -1;
    for (uint32_t i = 0; i < c->nroom; i++)
        if (!strcmp(c->room[i].key, key)) return (int32_t) i;
    return -1;
}

static bool room_fits(const commons_room_t *r, uint32_t i)
{
    return commons_room_record_fits((int64_t) i, r->kind, r->newest, r->seen, r->members);
}

/* Replace every room of kind with fresh[0..n), keeping the others and each
 * kept key's counts; CR4 for every room before any is kept. */
static int replace_rooms(commons_t *c, uint8_t kind, commons_room_t *fresh, uint32_t n)
{
    static commons_room_t next[COMMONS_ROOMS];
    uint32_t m = 0;
    for (uint32_t i = 0; i < c->nroom; i++)
        if (c->room[i].kind != kind) next[m++] = c->room[i];
    if (m + n > COMMONS_ROOMS) return ZXM_E_TOOBIG;
    for (uint32_t i = 0; i < n; i++) {
        int32_t was = commons_find(c, fresh[i].key);
        if (was >= 0 && c->room[was].kind == kind) {
            fresh[i].newest = c->room[was].newest;
            fresh[i].seen = c->room[was].seen;
            fresh[i].next = c->room[was].next;
        }
        next[m++] = fresh[i];
    }
    for (uint32_t i = 0; i < m; i++)
        if (!room_fits(&next[i], i)) return ZXM_E_PROTOCOL;
    memcpy(c->room, next, m * sizeof next[0]);
    c->nroom = m;
    return ZXM_OK;
}

/* add one room, or update the one with its key */
static int put_room(commons_t *c, const commons_room_t *r)
{
    int32_t i = commons_find(c, r->key);
    commons_room_t keep = *r;
    if (i >= 0) {
        keep.newest = c->room[i].newest;
        keep.seen = c->room[i].seen;
        keep.next = c->room[i].next;
    } else if (c->nroom >= COMMONS_ROOMS) {
        return ZXM_E_TOOBIG;
    }
    uint32_t at = i >= 0 ? (uint32_t) i : c->nroom;
    if (!room_fits(&keep, at)) return ZXM_E_PROTOCOL;
    c->room[at] = keep;
    if (i < 0) c->nroom++;
    return ZXM_OK;
}

/* ---- this node: who, follows, feed, thread (CM2) --------------------------- */

int commons_me(commons_t *c)
{
    char id[COMMONS_HEX_ID + 1];
    static char fol[COMMONS_FOLLOW_MAX][COMMONS_HEX_ID + 1];
    bool saved = false;
    int64_t records = 0;
    zxm_begin(c->link, "chat.me");
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = c->link->result;
    if (zxm_get_str(c->link, o, "node_id", id, sizeof id) != ZXM_OK ||
        !hex_ok(id, COMMONS_HEX_ID) || zxm_get_bool(c->link, o, "saved", &saved) != ZXM_OK ||
        get_count(c->link, o, "records", &records) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    int32_t arr = zxm_get(c->link, o, "following");
    uint32_t n = zxm_len(c->link, arr);
    if (arr < 0 || c->link->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    if (n > COMMONS_FOLLOW_MAX) return ZXM_E_TOOBIG;
    for (uint32_t i = 0; i < n; i++) {
        int32_t t = zxm_at(c->link, arr, i);
        if (t < 0 || c->link->tok[t].type != W4_J_STR ||
            w4_json_str(c->link->resp, &c->link->tok[t], fol[i], sizeof fol[i]) < 0 ||
            !hex_ok(fol[i], COMMONS_HEX_ID))
            return ZXM_E_PROTOCOL;
    }
    memcpy(c->me, id, sizeof id);
    c->saved = saved;
    c->records = records;
    memcpy(c->following, fol, n * sizeof fol[0]);
    c->nfollowing = n;
    return ZXM_OK;
}

int commons_follow(commons_t *c, const char *node_id, bool follow)
{
    char id[COMMONS_HEX_ID + 1];
    bool now = false;
    if (!hex_ok(node_id, COMMONS_HEX_ID) || (c->me[0] && !strcmp(node_id, c->me))) return ZXM_E_ARG;
    zxm_begin(c->link, "chat.follow");
    zxm_p_str(c->link, "node_id", node_id);
    zxm_p_bool(c->link, "follow", follow);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = c->link->result;
    if (zxm_get_str(c->link, o, "node_id", id, sizeof id) != ZXM_OK || strcmp(id, node_id) ||
        zxm_get_bool(c->link, o, "following", &now) != ZXM_OK || now != follow)
        return ZXM_E_PROTOCOL;
    uint32_t at = c->nfollowing;
    for (uint32_t i = 0; i < c->nfollowing; i++)
        if (!strcmp(c->following[i], id)) at = i;
    if (follow && at == c->nfollowing) {
        if (c->nfollowing >= COMMONS_FOLLOW_MAX) return ZXM_E_TOOBIG;
        memcpy(c->following[c->nfollowing++], id, sizeof id);
    } else if (!follow && at < c->nfollowing) {
        memmove(c->following[at], c->following[at + 1u],
                (c->nfollowing - at - 1u) * sizeof c->following[0]);
        c->nfollowing--;
    }
    return ZXM_OK;
}

/* one record at token o into p, as room `room` place `pos` (CR5) */
static int read_post(const zxm_link_t *l, int32_t o, commons_post_t *p, uint32_t room, uint32_t pos)
{
    char kind[16];
    uint64_t replies = 0;
    memset(p, 0, sizeof *p);
    if (o < 0 || zxm_get_str(l, o, "id", p->id, sizeof p->id) != ZXM_OK ||
        !hex_ok(p->id, COMMONS_HEX_ID) ||
        zxm_get_str(l, o, "author", p->author, sizeof p->author) != ZXM_OK ||
        !hex_ok(p->author, COMMONS_HEX_ID) || zxm_get_bool(l, o, "mine", &p->mine) != ZXM_OK ||
        zxm_get_str(l, o, "kind", kind, sizeof kind) != ZXM_OK ||
        get_dec(l, o, "lamport", &p->lamport) != ZXM_OK ||
        get_dec(l, o, "wall_ms", &p->wall_ms) != ZXM_OK ||
        zxm_get_str(l, o, "text", p->text, sizeof p->text) != ZXM_OK ||
        get_opt_str(l, o, "reply_to", p->reply_to, sizeof p->reply_to) != ZXM_OK ||
        (p->reply_to[0] && !hex_ok(p->reply_to, COMMONS_HEX_ID)) ||
        zxm_get_u64(l, o, "replies", &replies) != ZXM_OK || replies > UINT32_MAX)
        return ZXM_E_PROTOCOL;
    int32_t k = code_of(kind, KIND, 3);
    if (k < 0) return ZXM_E_PROTOCOL;
    p->kind = (uint8_t) k;
    p->replies = (uint32_t) replies;
    int32_t arr = zxm_get(l, o, "reactions");
    uint32_t n = zxm_len(l, arr);
    for (uint32_t i = 0; i < n && p->nreact < COMMONS_REACTS; i++) {
        int32_t r = zxm_at(l, arr, i);
        commons_reaction_t *x = &p->react[p->nreact];
        uint64_t count = 0;
        if (zxm_get_str(l, r, "emoji", x->emoji, sizeof x->emoji) != ZXM_OK ||
            !commons_reaction_text_ok((int64_t) strlen(x->emoji)) ||
            zxm_get_u64(l, r, "count", &count) != ZXM_OK || count < 1u || count > UINT32_MAX ||
            zxm_get_bool(l, r, "mine", &x->mine) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        x->count = (uint32_t) count;
        p->nreact++;
    }
    if (!commons_message_record_fits(room, pos, p->lamport, p->wall_ms, (int64_t) strlen(p->text)))
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

int commons_feed(commons_t *c)
{
    static commons_post_t page[COMMONS_FEED_MAX];
    zxm_begin(c->link, "chat.feed");
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(c->link, c->link->result, "posts");
    uint32_t n = zxm_len(c->link, arr);
    if (arr < 0 || c->link->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    if (n > COMMONS_FEED_MAX) return ZXM_E_TOOBIG;
    commons_room_t *here = &c->room[0];
    int64_t fresh = 0, top = here->next;
    for (uint32_t i = 0; i < n; i++) {
        rc = read_post(c->link, zxm_at(c->link, arr, i), &page[i], 0, i);
        if (rc != ZXM_OK) return rc;
        /* CM4: a post by another past the last one read */
        if (!page[i].mine && page[i].kind != COMMONS_REACTION && page[i].lamport > here->mark) {
            fresh++;
            if (page[i].lamport > top) top = page[i].lamport;
        }
    }
    int64_t newest = here->seen + fresh;
    if (commons_room_unread(newest, here->seen) == COMMONS_REFUSED ||
        !commons_room_record_fits(0, here->kind, newest, here->seen, here->members))
        return ZXM_E_PROTOCOL;
    memcpy(c->feed, page, n * sizeof page[0]); /* CM2: zxvd's order, as given */
    c->nfeed = n;
    here->newest = newest;
    here->next = top;
    return ZXM_OK;
}

int commons_thread(commons_t *c, const char *id)
{
    static commons_post_t rep[COMMONS_THREAD_MAX];
    static commons_post_t root;
    if (!hex_ok(id, COMMONS_HEX_ID)) return ZXM_E_ARG;
    zxm_begin(c->link, "chat.thread");
    zxm_p_str(c->link, "id", id);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = c->link->result;
    rc = read_post(c->link, zxm_get(c->link, o, "root"), &root, 0, 0);
    if (rc != ZXM_OK) return rc;
    if (strcmp(root.id, id)) return ZXM_E_PROTOCOL;
    int32_t arr = zxm_get(c->link, o, "replies");
    uint32_t n = zxm_len(c->link, arr);
    if (arr < 0 || c->link->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t keep = n < COMMONS_THREAD_MAX ? n : COMMONS_THREAD_MAX;
    for (uint32_t i = 0; i < keep; i++) {
        rc = read_post(c->link, zxm_at(c->link, arr, i), &rep[i], 0, i + 1u);
        if (rc != ZXM_OK) return rc;
        if (i && rep[i].lamport < rep[i - 1u].lamport) return ZXM_E_PROTOCOL; /* (Lamport, id) */
    }
    c->root = root;
    memcpy(c->thread, rep, keep * sizeof rep[0]);
    c->nthread = keep;
    c->thread_total = n;
    return ZXM_OK;
}

/* ---- groups ----------------------------------------------------------------- */

static int read_role(const zxm_link_t *l, int32_t o, uint8_t *role)
{
    char s[16];
    if (zxm_get_str(l, o, "my_role", s, sizeof s) != ZXM_OK) return ZXM_E_PROTOCOL;
    int32_t r = code_of(s, ROLE, 5);
    if (r < 0) return ZXM_E_PROTOCOL;
    *role = (uint8_t) r;
    return ZXM_OK;
}

int commons_groups(commons_t *c)
{
    static commons_room_t fresh[COMMONS_GROUPS];
    zxm_begin(c->link, "social.groups");
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(c->link, c->link->result, "groups");
    uint32_t n = zxm_len(c->link, arr);
    if (arr < 0 || c->link->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    if (n > COMMONS_GROUPS) return ZXM_E_TOOBIG;
    for (uint32_t i = 0; i < n; i++) {
        int32_t g = zxm_at(c->link, arr, i);
        commons_room_t *r = &fresh[i];
        memset(r, 0, sizeof *r);
        r->kind = COMMONS_ROOM_GROUP;
        if (zxm_get_str(c->link, g, "id", r->key, sizeof r->key) != ZXM_OK ||
            !hex_ok(r->key, COMMONS_HEX_ID) ||
            zxm_get_str(c->link, g, "name", r->name, sizeof r->name) != ZXM_OK ||
            zxm_get_bool(c->link, g, "open", &r->open) != ZXM_OK ||
            get_count(c->link, g, "members", &r->members) != ZXM_OK ||
            read_role(c->link, g, &r->role) != ZXM_OK)
            return ZXM_E_PROTOCOL;
    }
    return replace_rooms(c, COMMONS_ROOM_GROUP, fresh, n);
}

int commons_group_create(commons_t *c, const char *name, bool open, char group[COMMONS_HEX_ID + 1])
{
    commons_room_t r;
    bool was_open = false;
    if (!line_ok(name, COMMONS_NAME_MAX) || !group) return ZXM_E_ARG;
    if (c->nroom >= COMMONS_ROOMS) return ZXM_E_STATE;
    zxm_begin(c->link, "social.group_create");
    zxm_p_str(c->link, "name", name);
    zxm_p_bool(c->link, "open", open);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = c->link->result;
    memset(&r, 0, sizeof r);
    r.kind = COMMONS_ROOM_GROUP;
    if (zxm_get_str(c->link, o, "group", r.key, sizeof r.key) != ZXM_OK ||
        !hex_ok(r.key, COMMONS_HEX_ID) || zxm_get_bool(c->link, o, "open", &was_open) != ZXM_OK ||
        was_open != open || read_role(c->link, o, &r.role) != ZXM_OK ||
        r.role != COMMONS_ROLE_OWNER)
        return ZXM_E_PROTOCOL;
    memcpy(r.name, name, strlen(name) + 1u);
    r.open = open;
    r.members = 1;
    rc = put_room(c, &r);
    if (rc != ZXM_OK) return rc;
    memcpy(group, r.key, COMMONS_HEX_ID + 1u); /* 64 hex digits and the NUL (hex_ok) */
    return ZXM_OK;
}

static int membership(commons_t *c, const char *method, const char *group)
{
    char g[COMMONS_HEX_ID + 1];
    uint8_t role = 0;
    int64_t members = 0;
    if (!hex_ok(group, COMMONS_HEX_ID)) return ZXM_E_ARG;
    int32_t i = commons_find(c, group);
    if (i < 0 || c->room[i].kind != COMMONS_ROOM_GROUP) return ZXM_E_STATE;
    bool leave = !strcmp(method, "social.leave");
    /* the owner cannot leave, nor a member join again (zxvd refuses both too) */
    if (leave ? c->room[i].role == COMMONS_ROLE_OWNER || c->room[i].role == COMMONS_ROLE_NONE
              : c->room[i].role >= COMMONS_ROLE_MEMBER)
        return ZXM_E_STATE;
    zxm_begin(c->link, method);
    zxm_p_str(c->link, "group", group);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = c->link->result;
    if (zxm_get_str(c->link, o, "group", g, sizeof g) != ZXM_OK || strcmp(g, group) ||
        read_role(c->link, o, &role) != ZXM_OK ||
        get_count(c->link, o, "members", &members) != ZXM_OK ||
        (leave ? role >= COMMONS_ROLE_MEMBER : role < COMMONS_ROLE_MEMBER))
        return ZXM_E_PROTOCOL;
    commons_room_t r = c->room[i];
    r.role = role;
    r.members = members;
    return put_room(c, &r);
}

int commons_join(commons_t *c, const char *group)
{
    return membership(c, "social.join", group);
}

int commons_leave(commons_t *c, const char *group)
{
    return membership(c, "social.leave", group);
}

/* ---- spaces (CM5) ----------------------------------------------------------- */

static int read_phase(const zxm_link_t *l, int32_t o, uint8_t *phase)
{
    char s[16];
    if (zxm_get_str(l, o, "phase", s, sizeof s) != ZXM_OK) return ZXM_E_PROTOCOL;
    int32_t p = code_of(s, PHASE, 4);
    if (p < 0) return ZXM_E_PROTOCOL;
    *phase = (uint8_t) p;
    return ZXM_OK;
}

int commons_space_create(commons_t *c, const char *name, const char *topic,
                         char cid[COMMONS_CID_MAX + 1])
{
    commons_room_t r;
    if (!line_ok(name, COMMONS_SPACE_NAME) || (topic && !topic_ok(topic)) || !cid) return ZXM_E_ARG;
    if (c->nroom >= COMMONS_ROOMS) return ZXM_E_STATE;
    zxm_begin(c->link, "pspace.create");
    zxm_p_str(c->link, "kind", "room");
    zxm_p_str(c->link, "gov", "owner");
    zxm_p_str(c->link, "name", name);
    if (topic) zxm_p_str(c->link, "topic", topic);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    memset(&r, 0, sizeof r);
    r.kind = COMMONS_ROOM_SPACE;
    if (zxm_get_str(c->link, c->link->result, "cid", r.key, sizeof r.key) != ZXM_OK ||
        !cid_ok(r.key))
        return ZXM_E_PROTOCOL;
    memcpy(r.name, name, strlen(name) + 1u);
    r.phase = COMMONS_IN; /* this node hosts it */
    r.members = 1;
    rc = put_room(c, &r);
    if (rc != ZXM_OK) return rc;
    memcpy(cid, r.key, sizeof r.key);
    return ZXM_OK;
}

int commons_space_join(commons_t *c, const char *cid)
{
    commons_room_t r;
    char got[COMMONS_CID_MAX + 1];
    if (!cid_ok(cid)) return ZXM_E_ARG;
    int32_t i = commons_find(c, cid);
    if (i < 0 && c->nroom >= COMMONS_ROOMS) return ZXM_E_STATE;
    zxm_begin(c->link, "pspace.join");
    zxm_p_str(c->link, "cid", cid);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = c->link->result;
    memset(&r, 0, sizeof r);
    if (i >= 0) r = c->room[i];
    r.kind = COMMONS_ROOM_SPACE;
    if (zxm_get_str(c->link, o, "cid", got, sizeof got) != ZXM_OK || strcmp(got, cid) ||
        read_phase(c->link, o, &r.phase) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    memcpy(r.key, cid, strlen(cid) + 1u);
    return put_room(c, &r);
}

int commons_spaces(commons_t *c)
{
    static commons_room_t fresh[COMMONS_SPACES];
    bool online = false;
    zxm_begin(c->link, "pspace.list");
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = c->link->result;
    int32_t arr = zxm_get(c->link, o, "spaces");
    uint32_t n = zxm_len(c->link, arr);
    if (zxm_get_bool(c->link, o, "online", &online) != ZXM_OK || arr < 0 ||
        c->link->tok[arr].type != W4_J_ARR)
        return ZXM_E_PROTOCOL;
    if (n > COMMONS_SPACES) return ZXM_E_TOOBIG;
    for (uint32_t i = 0; i < n; i++) {
        int32_t s = zxm_at(c->link, arr, i);
        commons_room_t *r = &fresh[i];
        memset(r, 0, sizeof *r);
        r->kind = COMMONS_ROOM_SPACE;
        if (zxm_get_str(c->link, s, "cid", r->key, sizeof r->key) != ZXM_OK || !cid_ok(r->key) ||
            read_phase(c->link, s, &r->phase) != ZXM_OK ||
            get_count(c->link, s, "members", &r->members) != ZXM_OK ||
            get_opt_str(c->link, s, "name", r->name, COMMONS_SPACE_NAME + 1u) != ZXM_OK)
            return ZXM_E_PROTOCOL;
    }
    rc = replace_rooms(c, COMMONS_ROOM_SPACE, fresh, n);
    if (rc == ZXM_OK) c->online = online;
    return rc;
}

static int space_room(const commons_t *c, uint32_t room)
{
    if (room >= c->nroom || c->room[room].kind != COMMONS_ROOM_SPACE) return ZXM_E_ARG;
    return ZXM_OK;
}

int commons_space_events(commons_t *c, uint32_t room)
{
    static commons_event_t ev[COMMONS_EVENTS];
    int64_t code[COMMONS_EVENTS], next = 0;
    char kind[8];
    if (space_room(c, room) != ZXM_OK) return ZXM_E_ARG;
    if (!c->me[0]) return ZXM_E_STATE; /* which posts are mine: commons_me first */
    commons_room_t *r = &c->room[room];
    zxm_begin(c->link, "pspace.events");
    zxm_p_str(c->link, "cid", r->key);
    zxm_p_u64(c->link, "since", (uint64_t) r->next);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = c->link->result;
    int32_t arr = zxm_get(c->link, o, "events");
    uint32_t n = zxm_len(c->link, arr);
    if (arr < 0 || c->link->tok[arr].type != W4_J_ARR ||
        get_count(c->link, o, "next", &next) != ZXM_OK || next < r->next)
        return ZXM_E_PROTOCOL;
    if (n > COMMONS_EVENTS) return ZXM_E_TOOBIG;
    for (uint32_t i = 0; i < n; i++) {
        int32_t e = zxm_at(c->link, arr, i);
        commons_event_t *x = &ev[i];
        memset(x, 0, sizeof *x);
        /* zxvd: since is exclusive, next is the newest index returned */
        if (get_count(c->link, e, "index", &x->index) != ZXM_OK || x->index <= r->next ||
            x->index > next || (i && x->index <= ev[i - 1u].index) ||
            zxm_get_str(c->link, e, "kind", kind, sizeof kind) != ZXM_OK ||
            get_count(c->link, e, "lamport", &x->lamport) != ZXM_OK ||
            get_count(c->link, e, "created_ms", &x->created_ms) != ZXM_OK ||
            zxm_get_str(c->link, e, "author", x->author, sizeof x->author) != ZXM_OK ||
            !hex_ok(x->author, COMMONS_HEX_ID) ||
            get_opt_str(c->link, e, "text", x->text, sizeof x->text) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        if (!strcmp(kind, "post"))
            x->code = strcmp(x->author, c->me) ? COMMONS_EV_POST : COMMONS_EV_MINE;
        else if (!strcmp(kind, "hall"))
            x->code = COMMONS_EV_HALL;
        else
            return ZXM_E_PROTOCOL;
        if (!commons_message_record_fits(room, x->index, x->lamport, x->created_ms,
                                         (int64_t) strlen(x->text)))
            return ZXM_E_PROTOCOL;
        code[i] = x->code;
    }
    /* CM4: posts by others, Witness's count (WR2) */
    int64_t posts = witness_kind_count(code, n, COMMONS_EV_POST);
    if (r->newest > COMMONS_COUNT_MAX - posts ||
        !commons_room_record_fits(room, r->kind, r->newest + posts, r->seen, r->members))
        return ZXM_E_PROTOCOL;
    memcpy(c->events, ev, n * sizeof ev[0]);
    c->nevents = n;
    r->newest += posts;
    r->next = next;
    return ZXM_OK;
}

int commons_space_members(commons_t *c, uint32_t room)
{
    static char ids[COMMONS_MEMBERS][COMMONS_HEX_ID + 1];
    int64_t count = 0;
    if (space_room(c, room) != ZXM_OK) return ZXM_E_ARG;
    zxm_begin(c->link, "pspace.members");
    zxm_p_str(c->link, "cid", c->room[room].key);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = c->link->result;
    int32_t arr = zxm_get(c->link, o, "members");
    uint32_t n = zxm_len(c->link, arr);
    if (arr < 0 || c->link->tok[arr].type != W4_J_ARR ||
        get_count(c->link, o, "count", &count) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    if (n > COMMONS_MEMBERS) return ZXM_E_TOOBIG;
    for (uint32_t i = 0; i < n; i++)
        if (zxm_get_str(c->link, zxm_at(c->link, arr, i), "id", ids[i], sizeof ids[i]) != ZXM_OK ||
            !hex_ok(ids[i], COMMONS_HEX_ID))
            return ZXM_E_PROTOCOL;
    commons_room_t r = c->room[room];
    r.members = count;
    if (!room_fits(&r, room)) return ZXM_E_PROTOCOL;
    c->room[room].members = count;
    memcpy(c->member, ids, n * sizeof ids[0]);
    c->nmember = n;
    return ZXM_OK;
}

/* ---- posting (CM3) ---------------------------------------------------------- */

/* CR1's three flags for a room */
static void flags(const commons_t *c, uint32_t room, int64_t f[3])
{
    const commons_room_t *r = &c->room[room];
    f[0] = f[1] = f[2] = 0;
    if (r->kind == COMMONS_ROOM_HERE) {
        f[0] = f[1] = f[2] = 1;
    } else if (r->kind == COMMONS_ROOM_GROUP) {
        f[0] = r->role >= COMMONS_ROLE_MEMBER;
        f[1] = r->role != COMMONS_ROLE_NONE;
        f[2] = 1;
    } else {
        f[0] = 1; /* pspace.list names it: this node is in it or joining */
        f[1] = r->phase == COMMONS_IN;
        f[2] = c->online;
    }
}

bool commons_may_post(const commons_t *c, uint32_t room)
{
    int64_t f[3];
    if (room >= c->nroom) return false;
    flags(c, room, f);
    return commons_room_post_ok(f[0], f[1], f[2]);
}

int commons_post(commons_t *c, uint32_t room, const char *text, const char *reply_to)
{
    char got[COMMONS_CID_MAX + 1];
    bool posted = false;
    if (room >= c->nroom || !message_ok(text) || (reply_to && !hex_ok(reply_to, COMMONS_HEX_ID)))
        return ZXM_E_ARG;
    const commons_room_t *r = &c->room[room];
    if (r->kind == COMMONS_ROOM_SPACE && reply_to) return ZXM_E_ARG; /* a space has no replies */
    if (!commons_may_post(c, room)) return ZXM_E_STATE;
    const char *method = r->kind == COMMONS_ROOM_HERE    ? "chat.post"
                         : r->kind == COMMONS_ROOM_GROUP ? "social.post"
                                                         : "pspace.post";
    zxm_begin(c->link, method);
    zxm_p_str(c->link, "text", text);
    if (reply_to) zxm_p_str(c->link, "reply_to", reply_to);
    if (r->kind == COMMONS_ROOM_GROUP) zxm_p_str(c->link, "group", r->key);
    if (r->kind == COMMONS_ROOM_SPACE) zxm_p_str(c->link, "cid", r->key);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = c->link->result;
    if (r->kind == COMMONS_ROOM_SPACE)
        return zxm_get_str(c->link, o, "cid", got, sizeof got) == ZXM_OK && !strcmp(got, r->key) &&
                       zxm_get_bool(c->link, o, "posted", &posted) == ZXM_OK && posted
                   ? ZXM_OK
                   : ZXM_E_PROTOCOL;
    return zxm_get_str(c->link, o, "id", got, sizeof got) == ZXM_OK && hex_ok(got, COMMONS_HEX_ID)
               ? ZXM_OK
               : ZXM_E_PROTOCOL;
}

int commons_react(commons_t *c, const char *to, const char *emoji)
{
    char id[COMMONS_HEX_ID + 1], kind[16];
    if (!hex_ok(to, COMMONS_HEX_ID) || !emoji ||
        !commons_reaction_text_ok((int64_t) strlen(emoji)) || !line_ok(emoji, COMMONS_REACT_MAX))
        return ZXM_E_ARG;
    zxm_begin(c->link, "chat.react");
    zxm_p_str(c->link, "to", to);
    zxm_p_str(c->link, "emoji", emoji);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = c->link->result;
    if (zxm_get_str(c->link, o, "id", id, sizeof id) != ZXM_OK || !hex_ok(id, COMMONS_HEX_ID) ||
        zxm_get_str(c->link, o, "kind", kind, sizeof kind) != ZXM_OK || strcmp(kind, "reaction"))
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

/* ---- unread and notices (CM4, CM7) ---------------------------------------- */

int64_t commons_unread(const commons_t *c, uint32_t room)
{
    if (room >= c->nroom) return COMMONS_REFUSED;
    return commons_room_unread(c->room[room].newest, c->room[room].seen);
}

int commons_mark_read(commons_t *c, uint32_t room)
{
    if (room >= c->nroom) return ZXM_E_ARG;
    commons_room_t *r = &c->room[room];
    r->seen = r->newest;
    if (r->kind == COMMONS_ROOM_HERE) r->mark = r->next;
    return ZXM_OK;
}

int commons_notice(commons_t *c)
{
    char out[COMMONS_NOTICE_MAX + 1];
    uint32_t len = 0;
    bool more = false;
    out[0] = 0;
    for (uint32_t i = 0; i < c->nroom; i++) {
        int64_t u = commons_unread(c, i);
        if (u == COMMONS_REFUSED) return ZXM_E_STATE;
        if (u == 0) continue;
        const char *name = c->room[i].name[0] ? c->room[i].name : "a room";
        int w = snprintf(out + len, sizeof out - len, "%s%lld new in %s", len ? "; " : "",
                         (long long) u, name);
        if (w < 0 || (uint32_t) w >= sizeof out - len - 8u) {
            out[len] = 0;
            more = true;
            break;
        }
        len += (uint32_t) w;
    }
    if (more) {
        int w = snprintf(out + len, sizeof out - len, "%s(more)", len ? "; " : "");
        if (w > 0) len += (uint32_t) w;
    }
    if (len && len + 1u < sizeof out) out[len++] = '.', out[len] = 0;
    memcpy(c->notice, out, len + 1u);
    return ZXM_OK;
}

/* ---- the IRC bridge (CM6) --------------------------------------------------- */

static int read_bridge(const zxm_link_t *l, int32_t b, commons_irc_t *irc)
{
    irc->bridged = false;
    irc->state[0] = irc->host[0] = irc->remote[0] = irc->local[0] = irc->nick[0] = 0;
    irc->error[0] = 0;
    irc->sent = irc->received = irc->dropped = irc->queued = 0;
    if (b < 0 || l->tok[b].type == W4_J_NULL) return ZXM_OK;
    if (l->tok[b].type != W4_J_OBJ ||
        zxm_get_str(l, b, "state", irc->state, sizeof irc->state) != ZXM_OK ||
        zxm_get_str(l, b, "host", irc->host, sizeof irc->host) != ZXM_OK ||
        zxm_get_str(l, b, "remote", irc->remote, sizeof irc->remote) != ZXM_OK ||
        zxm_get_str(l, b, "local", irc->local, sizeof irc->local) != ZXM_OK ||
        zxm_get_str(l, b, "nick", irc->nick, sizeof irc->nick) != ZXM_OK ||
        get_count(l, b, "sent", &irc->sent) != ZXM_OK ||
        get_count(l, b, "received", &irc->received) != ZXM_OK ||
        get_count(l, b, "dropped", &irc->dropped) != ZXM_OK ||
        get_count(l, b, "queued", &irc->queued) != ZXM_OK ||
        get_opt_str(l, b, "error", irc->error, sizeof irc->error) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    irc->bridged = true;
    return ZXM_OK;
}

int commons_irc_status(commons_t *c)
{
    commons_irc_t irc;
    memset(&irc, 0, sizeof irc);
    zxm_begin(c->link, "irc.status");
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = c->link->result;
    irc.available = true;
    if (zxm_get(c->link, o, "available") >= 0 &&
        zxm_get_bool(c->link, o, "available", &irc.available) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    if (irc.available && (zxm_get_bool(c->link, o, "on", &irc.on) != ZXM_OK ||
                          zxm_get_bool(c->link, o, "listening", &irc.listening) != ZXM_OK ||
                          get_count(c->link, o, "port", &irc.port) != ZXM_OK || irc.port > 65535 ||
                          get_count(c->link, o, "clients", &irc.clients) != ZXM_OK ||
                          get_count(c->link, o, "delivered", &irc.delivered) != ZXM_OK ||
                          get_count(c->link, o, "refused", &irc.refused) != ZXM_OK ||
                          read_bridge(c->link, zxm_get(c->link, o, "bridge"), &irc) != ZXM_OK))
        return ZXM_E_PROTOCOL;
    c->irc = irc; /* the gateway's password is never kept */
    return ZXM_OK;
}

int commons_irc_bridge(commons_t *c, bool on, const char *host, int64_t port, const char *remote,
                       const char *local, const char *nick, const char *password,
                       bool owner_confirmed)
{
    commons_irc_t irc = c->irc;
    if (on && (!line_ok(host, COMMONS_HOST_MAX) || strchr(host, ' ') || port < 1 || port > 65535 ||
               !chan_ok(remote) || !chan_ok(local) || !strcmp(local, "#swarm") ||
               !strcmp(remote, "#swarm") || !line_ok(nick, COMMONS_NICK_MAX) || strchr(nick, ' ') ||
               (password && strlen(password) > COMMONS_PASS_MAX)))
        return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE; /* CM6: the owner's act */
    zxm_begin(c->link, "irc.bridge");
    zxm_p_bool(c->link, "on", on);
    if (on) {
        zxm_p_str(c->link, "host", host);
        zxm_p_u64(c->link, "port", (uint64_t) port);
        zxm_p_str(c->link, "remote", remote);
        zxm_p_str(c->link, "local", local);
        zxm_p_str(c->link, "nick", nick);
        if (password && password[0]) zxm_p_str(c->link, "password", password);
    }
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    rc = read_bridge(c->link, c->link->result, &irc);
    if (rc != ZXM_OK) return rc;
    if (on != irc.bridged) return ZXM_E_PROTOCOL;
    c->irc = irc;
    return ZXM_OK;
}

int commons_irc_record(commons_t *c, const char *msgid)
{
    commons_irc_record_t r;
    char truth[4];
    if (!hex_ok(msgid, 32u)) return ZXM_E_ARG;
    memset(&r, 0, sizeof r);
    zxm_begin(c->link, "irc.record");
    zxm_p_str(c->link, "msgid", msgid);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = c->link->result;
    if (zxm_get_str(c->link, o, "msgid", r.msgid, sizeof r.msgid) != ZXM_OK ||
        strcmp(r.msgid, msgid) ||
        zxm_get_str(c->link, o, "target", r.target, sizeof r.target) != ZXM_OK ||
        zxm_get_str(c->link, o, "nick", r.nick, sizeof r.nick) != ZXM_OK ||
        zxm_get_str(c->link, o, "truth", truth, sizeof truth) != ZXM_OK || strlen(truth) != 1u ||
        !strchr("TFBN", truth[0]) || get_dec(c->link, o, "ms", &r.ms) != ZXM_OK ||
        r.ms > COMMONS_STAMP_MAX ||
        zxm_get_str(c->link, o, "text", r.text, sizeof r.text) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    r.truth = truth[0];
    c->irc_record = r;
    return ZXM_OK;
}
