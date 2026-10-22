/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* meadow_model.c — Meadow, the park (meadow_model.h MM1-MM8). */
#include "meadow_model.h"

#include <stdio.h>
#include <string.h>

#include "seedbank_rules.h"

const char MEADOW_NO_DELETE[] = "Posts in a public space cannot be deleted: every member keeps "
                                "what you post, and nobody can take it back. Confirm to send.";

static const char *const KIND[MEADOW_KINDS] = {"home", "office", "team", "cloudtop", "park"};
static const char *const ROLE[MEADOW_ROLES] = {"owner", "member", "guest", "public"};
static const char *const PHASE[] = {"?", "searching", "joining", "in", "not_found"};
static const char *const SKIND[] = {"?", "room", "hall"};

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

/* a string that may be null or missing ("") */
static int get_opt_str(const zxm_link_t *l, int32_t obj, const char *key, char *out, uint32_t cap)
{
    out[0] = 0;
    if (zxm_get(l, obj, key) < 0 || is_null(l, obj, key)) return ZXM_OK;
    return zxm_get_str(l, obj, key, out, cap) == ZXM_OK ? ZXM_OK : ZXM_E_PROTOCOL;
}

static int get_count(const zxm_link_t *l, int32_t obj, const char *key, int64_t *v)
{
    if (zxm_get_i64(l, obj, key, v) != ZXM_OK || *v < 0) return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

static bool hex_ok(const char *s, uint32_t len)
{
    if (!s || strlen(s) != len) return false;
    for (uint32_t i = 0; i < len; i++)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    return true;
}

/* MM2: a space CID's text, Seedbank's SR2 */
static bool cid_ok(const char *s)
{
    return s && seedbank_cid_ok(s, (uint32_t) strlen(s));
}

/* MM2: 1..24 of [a-z0-9-], pspace.find's topic */
static bool topic_ok(const char *s)
{
    uint32_t n = s ? (uint32_t) strlen(s) : 0u;
    if (n < 1u || n > MEADOW_TOPIC_MAX) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '-'))
            return false;
    return true;
}

/* MM7: a call; an answer of -32012 marks the feature off, any answer
 * from zxvd tells whether it is */
static int call(meadow_t *m)
{
    int rc = zxm_call(m->link);
    if (rc == ZXM_OK) m->off = false;
    if (rc == ZXM_E_REMOTE) m->off = m->link->err_code == MEADOW_E_OFF;
    return rc;
}

void meadow_init(meadow_t *m, zxm_link_t *link)
{
    memset(m, 0, sizeof *m);
    m->link = link;
}

/* MM1: the seat this node holds in a public space */
static void seat_of(const meadow_t *m, meadow_space_t *s)
{
    s->seat_kind = MEADOW_PARK;
    s->seat_role = MEADOW_PUBLIC;
    for (uint32_t j = 0; j < m->nplace; j++)
        if (m->place[j].cid[0] && !strcmp(m->place[j].cid, s->cid)) {
            s->seat_kind = m->place[j].kind;
            s->seat_role = m->place[j].role;
            return;
        }
}

/* ---- the rights record (MM1) -------------------------------------------------- */

int meadow_places(meadow_t *m)
{
    static meadow_place_t fresh[MEADOW_PLACES];
    char kind[16], role[16];
    zxm_begin(m->link, "spaces.list");
    int rc = call(m);
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(m->link, m->link->result, "spaces");
    if (arr < 0 || m->link->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(m->link, arr);
    if (n > MEADOW_PLACES) return ZXM_E_TOOBIG;
    for (uint32_t i = 0; i < n; i++) {
        int32_t s = zxm_at(m->link, arr, i);
        meadow_place_t *p = &fresh[i];
        memset(p, 0, sizeof *p);
        if (zxm_get_str(m->link, s, "id", p->id, sizeof p->id) != ZXM_OK ||
            zxm_get_str(m->link, s, "kind", kind, sizeof kind) != ZXM_OK ||
            zxm_get_str(m->link, s, "role", role, sizeof role) != ZXM_OK ||
            zxm_get_bool(m->link, s, "write", &p->write) != ZXM_OK ||
            get_opt_str(m->link, s, "name", p->name, sizeof p->name) != ZXM_OK ||
            get_opt_str(m->link, s, "cid", p->cid, sizeof p->cid) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        int32_t k = code_of(kind, KIND, MEADOW_KINDS), r = code_of(role, ROLE, MEADOW_ROLES);
        if (k < 0 || r < 0 || (p->cid[0] && !cid_ok(p->cid))) return ZXM_E_PROTOCOL;
        p->kind = (uint8_t) k;
        p->role = (uint8_t) r;
    }
    memcpy(m->place, fresh, n * sizeof fresh[0]);
    m->nplace = n;
    /* the seats of the spaces already held follow the record */
    for (uint32_t i = 0; i < m->nspace; i++) seat_of(m, &m->space[i]);
    return ZXM_OK;
}

/* ---- finding (MM2) ------------------------------------------------------------ */

int meadow_find(meadow_t *m, const char *topic)
{
    static meadow_found_t fresh[MEADOW_FOUND];
    char got[MEADOW_TOPIC_MAX + 1], kind[8];
    bool done = false;
    if (!topic_ok(topic)) return ZXM_E_ARG;
    zxm_begin(m->link, "pspace.find");
    zxm_p_str(m->link, "topic", topic);
    int rc = call(m);
    if (rc != ZXM_OK) return rc;
    int32_t o = m->link->result;
    int32_t arr = zxm_get(m->link, o, "found");
    if (zxm_get_str(m->link, o, "topic", got, sizeof got) != ZXM_OK || strcmp(got, topic) ||
        zxm_get_bool(m->link, o, "done", &done) != ZXM_OK || arr < 0 ||
        m->link->tok[arr].type != W4_J_ARR)
        return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(m->link, arr);
    if (n > MEADOW_FOUND) return ZXM_E_TOOBIG;
    for (uint32_t i = 0; i < n; i++) {
        int32_t f = zxm_at(m->link, arr, i);
        meadow_found_t *x = &fresh[i];
        memset(x, 0, sizeof *x);
        if (zxm_get_str(m->link, f, "cid", x->cid, sizeof x->cid) != ZXM_OK || !cid_ok(x->cid) ||
            get_count(m->link, f, "members", &x->members) != ZXM_OK ||
            x->members > MEADOW_SPACE_PEOPLE ||
            zxm_get_str(m->link, f, "host", x->host, sizeof x->host) != ZXM_OK ||
            !hex_ok(x->host, MEADOW_HEX_ID) ||
            get_opt_str(m->link, f, "name", x->name, sizeof x->name) != ZXM_OK ||
            get_opt_str(m->link, f, "kind", kind, sizeof kind) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        int32_t k = kind[0] ? code_of(kind, SKIND, 3) : 0;
        if (k < 0) return ZXM_E_PROTOCOL;
        x->kind = (uint8_t) k;
    }
    memcpy(m->topic, topic, strlen(topic) + 1u);
    memcpy(m->found, fresh, n * sizeof fresh[0]);
    m->nfound = n;
    m->find_done = done;
    return ZXM_OK;
}

/* ---- joined spaces (MM3) ------------------------------------------------------ */

int32_t meadow_space_of(const meadow_t *m, const char *cid)
{
    for (uint32_t i = 0; cid && i < m->nspace; i++)
        if (!strcmp(m->space[i].cid, cid)) return (int32_t) i;
    return -1;
}

static bool space_fits(const meadow_space_t *s, uint32_t place)
{
    return meadow_joined_space_ok(place, s->phase, s->members, s->last, s->stamp);
}

/* MR4 over the spaces held */
static int64_t people(const meadow_t *m)
{
    int64_t c[MEADOW_SPACES];
    for (uint32_t i = 0; i < m->nspace; i++) c[i] = m->space[i].members;
    return meadow_space_members_total(c, m->nspace);
}

static int read_phase(const zxm_link_t *l, int32_t o, uint8_t *phase)
{
    char s[16];
    if (zxm_get_str(l, o, "phase", s, sizeof s) != ZXM_OK) return ZXM_E_PROTOCOL;
    int32_t p = code_of(s, PHASE, 5);
    if (p < MEADOW_SEARCHING) return ZXM_E_PROTOCOL;
    *phase = (uint8_t) p;
    return ZXM_OK;
}

int meadow_spaces(meadow_t *m)
{
    static meadow_space_t fresh[MEADOW_SPACES];
    char kind[8];
    bool online = false;
    zxm_begin(m->link, "pspace.list");
    int rc = call(m);
    if (rc != ZXM_OK) return rc;
    int32_t o = m->link->result;
    int32_t arr = zxm_get(m->link, o, "spaces");
    if (zxm_get_bool(m->link, o, "online", &online) != ZXM_OK || arr < 0 ||
        m->link->tok[arr].type != W4_J_ARR)
        return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(m->link, arr);
    if (n > MEADOW_SPACES) return ZXM_E_TOOBIG;
    for (uint32_t i = 0; i < n; i++) {
        int32_t s = zxm_at(m->link, arr, i);
        meadow_space_t *x = &fresh[i];
        memset(x, 0, sizeof *x);
        if (zxm_get_str(m->link, s, "cid", x->cid, sizeof x->cid) != ZXM_OK || !cid_ok(x->cid) ||
            read_phase(m->link, s, &x->phase) != ZXM_OK ||
            get_count(m->link, s, "members", &x->members) != ZXM_OK ||
            get_opt_str(m->link, s, "name", x->name, sizeof x->name) != ZXM_OK ||
            get_opt_str(m->link, s, "topic", x->topic, sizeof x->topic) != ZXM_OK ||
            get_opt_str(m->link, s, "kind", kind, sizeof kind) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        int32_t k = kind[0] ? code_of(kind, SKIND, 3) : 0;
        if (k < 0) return ZXM_E_PROTOCOL;
        x->kind = (uint8_t) k;
        int32_t was = meadow_space_of(m, x->cid);
        if (was >= 0) { /* the last index read stays */
            x->last = m->space[was].last;
            x->stamp = m->space[was].stamp;
            x->warned = m->space[was].warned;
        }
        seat_of(m, x);
        if (!space_fits(x, i)) return ZXM_E_PROTOCOL;
    }
    int64_t c[MEADOW_SPACES];
    for (uint32_t i = 0; i < n; i++) c[i] = fresh[i].members;
    int64_t total = meadow_space_members_total(c, n);
    if (total == MEADOW_REFUSED) return ZXM_E_PROTOCOL;
    memcpy(m->space, fresh, n * sizeof fresh[0]);
    m->nspace = n;
    m->online = online;
    m->people = total;
    return ZXM_OK;
}

int meadow_join(meadow_t *m, const char *cid)
{
    meadow_space_t x;
    char got[MEADOW_CID_MAX + 1];
    if (!cid_ok(cid)) return ZXM_E_ARG;
    int32_t i = meadow_space_of(m, cid);
    if (i < 0 && m->nspace >= MEADOW_SPACES) return ZXM_E_STATE;
    zxm_begin(m->link, "pspace.join");
    zxm_p_str(m->link, "cid", cid);
    int rc = call(m);
    if (rc != ZXM_OK) return rc;
    int32_t o = m->link->result;
    memset(&x, 0, sizeof x);
    if (i >= 0) x = m->space[i];
    if (zxm_get_str(m->link, o, "cid", got, sizeof got) != ZXM_OK || strcmp(got, cid) ||
        read_phase(m->link, o, &x.phase) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    memcpy(x.cid, cid, strlen(cid) + 1u);
    seat_of(m, &x);
    uint32_t at = i >= 0 ? (uint32_t) i : m->nspace;
    if (!space_fits(&x, at)) return ZXM_E_PROTOCOL;
    m->space[at] = x;
    if (i < 0) m->nspace++;
    return ZXM_OK;
}

int meadow_leave(meadow_t *m, uint32_t space)
{
    char got[MEADOW_CID_MAX + 1];
    bool left = false;
    if (space >= m->nspace) return ZXM_E_ARG;
    zxm_begin(m->link, "pspace.leave");
    zxm_p_str(m->link, "cid", m->space[space].cid);
    int rc = call(m);
    if (rc != ZXM_OK) return rc;
    int32_t o = m->link->result;
    if (zxm_get_str(m->link, o, "cid", got, sizeof got) != ZXM_OK ||
        strcmp(got, m->space[space].cid) || zxm_get_bool(m->link, o, "left", &left) != ZXM_OK ||
        !left)
        return ZXM_E_PROTOCOL;
    for (uint32_t i = space + 1u; i < m->nspace; i++) m->space[i - 1u] = m->space[i];
    m->nspace--;
    m->people = people(m);
    return ZXM_OK;
}

int meadow_members(meadow_t *m, uint32_t space)
{
    static meadow_member_t fresh[MEADOW_MEMBERS];
    int64_t count = 0;
    if (space >= m->nspace) return ZXM_E_ARG;
    zxm_begin(m->link, "pspace.members");
    zxm_p_str(m->link, "cid", m->space[space].cid);
    int rc = call(m);
    if (rc != ZXM_OK) return rc;
    int32_t o = m->link->result;
    int32_t arr = zxm_get(m->link, o, "members");
    if (arr < 0 || m->link->tok[arr].type != W4_J_ARR ||
        get_count(m->link, o, "count", &count) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(m->link, arr);
    if (n > MEADOW_MEMBERS || count < (int64_t) n || count > (int64_t) n + 1) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < n; i++) {
        int32_t e = zxm_at(m->link, arr, i);
        meadow_member_t *x = &fresh[i];
        memset(x, 0, sizeof *x);
        if (zxm_get_str(m->link, e, "id", x->id, sizeof x->id) != ZXM_OK ||
            !hex_ok(x->id, MEADOW_HEX_ID) ||
            get_opt_str(m->link, e, "addr", x->addr, sizeof x->addr) != ZXM_OK ||
            get_count(m->link, e, "last_seen_ms", &x->last_seen_ms) != ZXM_OK)
            return ZXM_E_PROTOCOL;
    }
    meadow_space_t s = m->space[space];
    s.members = count;
    if (!space_fits(&s, space)) return ZXM_E_PROTOCOL;
    int64_t keep = m->space[space].members;
    m->space[space].members = count;
    int64_t total = people(m);
    if (total == MEADOW_REFUSED) {
        m->space[space].members = keep;
        return ZXM_E_PROTOCOL;
    }
    m->people = total;
    memcpy(m->member, fresh, n * sizeof fresh[0]);
    m->nmember = n;
    return ZXM_OK;
}

/* ---- events (MM4) ------------------------------------------------------------- */

int meadow_events(meadow_t *m, uint32_t space)
{
    static meadow_event_t ev[MEADOW_EVENTS];
    char kind[8];
    int64_t next = 0;
    if (space >= m->nspace) return ZXM_E_ARG;
    meadow_space_t *s = &m->space[space];
    zxm_begin(m->link, "pspace.events");
    zxm_p_str(m->link, "cid", s->cid);
    zxm_p_u64(m->link, "since", (uint64_t) s->last);
    int rc = call(m);
    if (rc != ZXM_OK) return rc;
    int32_t o = m->link->result;
    int32_t arr = zxm_get(m->link, o, "events");
    if (arr < 0 || m->link->tok[arr].type != W4_J_ARR ||
        get_count(m->link, o, "next", &next) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    int64_t fresh = meadow_space_new_events(s->last, next); /* MR3 */
    uint32_t n = zxm_len(m->link, arr);
    if (fresh == MEADOW_REFUSED || n > MEADOW_EVENTS || (int64_t) n > fresh) return ZXM_E_PROTOCOL;
    int64_t stamp = s->stamp;
    for (uint32_t i = 0; i < n; i++) {
        int32_t e = zxm_at(m->link, arr, i);
        meadow_event_t *x = &ev[i];
        memset(x, 0, sizeof *x);
        /* indexes after since, rising, the last one next */
        if (get_count(m->link, e, "index", &x->index) != ZXM_OK || x->index <= s->last ||
            x->index > next || (i && x->index <= ev[i - 1u].index) ||
            (i + 1u == n && x->index != next) ||
            zxm_get_str(m->link, e, "kind", kind, sizeof kind) != ZXM_OK ||
            get_count(m->link, e, "lamport", &x->lamport) != ZXM_OK ||
            get_count(m->link, e, "created_ms", &x->created_ms) != ZXM_OK ||
            zxm_get_str(m->link, e, "author", x->author, sizeof x->author) != ZXM_OK ||
            !hex_ok(x->author, MEADOW_HEX_ID) ||
            get_opt_str(m->link, e, "text", x->text, sizeof x->text) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        if (!strcmp(kind, "post"))
            x->post = true;
        else if (strcmp(kind, "hall"))
            return ZXM_E_PROTOCOL;
        stamp = x->created_ms;
    }
    meadow_space_t t = *s;
    t.last = next;
    t.stamp = stamp;
    if (!space_fits(&t, space)) return ZXM_E_PROTOCOL;
    memcpy(m->event, ev, n * sizeof ev[0]);
    m->nevent = n;
    m->fresh = fresh;
    m->missed = fresh - (int64_t) n;
    s->last = next;
    s->stamp = stamp;
    return ZXM_OK;
}

/* ---- posting (MM5, MM6) ------------------------------------------------------- */

bool meadow_may_post(const meadow_t *m, uint32_t space, int64_t bytes)
{
    if (space >= m->nspace || !m->online || m->off) return false;
    const meadow_space_t *s = &m->space[space];
    return s->phase == MEADOW_IN && meadow_seat_may_post(s->seat_kind, s->seat_role, bytes);
}

static void say(meadow_t *m, const char *text)
{
    size_t n = strlen(text);
    if (n > MEADOW_NOTICE_MAX) n = MEADOW_NOTICE_MAX;
    memcpy(m->notice, text, n);
    m->notice[n] = 0;
}

int meadow_post(meadow_t *m, uint32_t space, const char *text, bool owner_confirmed)
{
    char got[MEADOW_CID_MAX + 1];
    bool posted = false;
    if (space >= m->nspace || !text) return ZXM_E_ARG;
    uint32_t n = 0;
    while (n <= MEADOW_POST_MAX && text[n]) n++;
    if (n > MEADOW_POST_MAX || zxm_utf8_cut(text, n) != n) return ZXM_E_ARG;
    if (!meadow_may_post(m, space, (int64_t) n)) return ZXM_E_STATE;
    meadow_space_t *s = &m->space[space];
    if (!s->warned || !owner_confirmed) { /* MM6: say it first, send nothing */
        say(m, MEADOW_NO_DELETE);
        s->warned = true;
        return ZXM_E_STATE;
    }
    s->warned = false; /* the next post is warned again */
    m->notice[0] = 0;
    zxm_begin(m->link, "pspace.post");
    zxm_p_str(m->link, "cid", s->cid);
    zxm_p_str(m->link, "text", text);
    int rc = call(m);
    if (rc != ZXM_OK) return rc;
    int32_t o = m->link->result;
    return zxm_get_str(m->link, o, "cid", got, sizeof got) == ZXM_OK && !strcmp(got, s->cid) &&
                   zxm_get_bool(m->link, o, "posted", &posted) == ZXM_OK && posted
               ? ZXM_OK
               : ZXM_E_PROTOCOL;
}

/* ---- notices (MM8) ------------------------------------------------------------ */

int meadow_notice(meadow_t *m, uint32_t space)
{
    if (space >= m->nspace) return ZXM_E_ARG;
    const meadow_space_t *s = &m->space[space];
    const char *name = s->name[0] ? s->name : "a public space";
    m->notice[0] = 0;
    if (m->fresh <= 0) return ZXM_OK;
    int w = m->missed > 0 ? snprintf(m->notice, sizeof m->notice, "%lld new in %s (%lld missed).",
                                     (long long) m->fresh, name, (long long) m->missed)
                          : snprintf(m->notice, sizeof m->notice, "%lld new in %s.",
                                     (long long) m->fresh, name);
    return w < 0 ? ZXM_E_TOOBIG : ZXM_OK;
}
