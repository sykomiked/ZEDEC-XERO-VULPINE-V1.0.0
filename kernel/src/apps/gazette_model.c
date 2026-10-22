/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gazette_model.c — Gazette, news and feeds (gazette_model.h GM1-GM7). */
#include "gazette_model.h"

#include <stdio.h>
#include <string.h>

/* one item of a page, with the source it belongs to, before it is kept */
typedef struct {
    uint8_t kind;
    char key[GAZETTE_KEY_MAX + 1];
    gazette_item_t it;
} gz_new_t;

static gz_new_t PAGE[GAZETTE_PAGE];

/* ---- text and answers ------------------------------------------------------ */

static bool is_null(const zxm_link_t *l, int32_t obj, const char *key)
{
    int32_t t = zxm_get(l, obj, key);
    return t >= 0 && l->tok[t].type == W4_J_NULL;
}

/* exactly len hex digits; lower case only unless any_case */
static bool hex_ok(const char *s, uint32_t len, bool any_case)
{
    if (!s || strlen(s) != len) return false;
    for (uint32_t i = 0; i < len; i++) {
        char c = s[i];
        bool up = any_case && c >= 'A' && c <= 'F';
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || up)) return false;
    }
    return true;
}

static void lower(char *s)
{
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z') *s = (char) (*s - 'A' + 'a');
}

/* an IRC channel: "#" and 1..49 bytes with no space, comma or control */
static bool chan_ok(const char *s)
{
    uint32_t n = s ? (uint32_t) strlen(s) : 0u;
    if (n < 2u || n > GAZETTE_CHAN_MAX || s[0] != '#') return false;
    for (uint32_t i = 0; i < n; i++)
        if ((unsigned char) s[i] < 0x20u || s[i] == 0x7f || s[i] == ' ' || s[i] == ',')
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

/* GM2: the text, or "" and cut when it is longer than GAZETTE_TEXT_MAX */
static int get_text(const zxm_link_t *l, int32_t obj, const char *key, gazette_item_t *it)
{
    int rc = zxm_get_str(l, obj, key, it->text, sizeof it->text);
    it->cut = rc == ZXM_E_TOOBIG;
    if (it->cut) it->text[0] = 0;
    return rc == ZXM_OK || it->cut ? ZXM_OK : ZXM_E_PROTOCOL;
}

static void put_u64(char *out, uint32_t cap, uint64_t v)
{
    snprintf(out, cap, "%llu", (unsigned long long) v);
}

/* ---- sources (GM1) ---------------------------------------------------------- */

void gazette_init(gazette_t *g, zxm_link_t *link)
{
    memset(g, 0, sizeof *g);
    g->link = link;
}

int32_t gazette_find(const gazette_t *g, uint8_t kind, const char *key)
{
    if (!key) return -1;
    for (uint32_t i = 0; i < g->nsrc; i++)
        if (g->src[i].kind == kind && !strcmp(g->src[i].key, key)) return (int32_t) i;
    return -1;
}

/* a new source at the end, not followed; the caller checked there is room */
static uint32_t new_source(gazette_t *g, uint8_t kind, const char *key)
{
    gazette_source_t *s = &g->src[g->nsrc];
    memset(s, 0, sizeof *s);
    s->kind = kind;
    snprintf(s->key, sizeof s->key, "%s", key);
    s->mark = -1;
    return g->nsrc++;
}

/* the source of kind and key, made followed when it is new or kept as it is */
static int add_followed(gazette_t *g, uint8_t kind, const char *key, uint32_t *at)
{
    int32_t i = gazette_find(g, kind, key);
    if (i < 0 && g->nsrc >= GAZETTE_SOURCES) return ZXM_E_TOOBIG;
    uint32_t s = i >= 0 ? (uint32_t) i : new_source(g, kind, key);
    g->src[s].followed = true;
    if (at) *at = s;
    return ZXM_OK;
}

int gazette_me(gazette_t *g)
{
    static char fol[GAZETTE_FOLLOW_MAX][GAZETTE_HEX_ID + 1];
    char id[GAZETTE_HEX_ID + 1];
    zxm_begin(g->link, "chat.me");
    int rc = zxm_call(g->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = g->link->result;
    if (zxm_get_str(g->link, o, "node_id", id, sizeof id) != ZXM_OK ||
        !hex_ok(id, GAZETTE_HEX_ID, false))
        return ZXM_E_PROTOCOL;
    int32_t arr = zxm_get(g->link, o, "following");
    if (arr < 0 || g->link->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(g->link, arr), fresh = 0;
    if (n > GAZETTE_FOLLOW_MAX) return ZXM_E_TOOBIG;
    for (uint32_t i = 0; i < n; i++) {
        int32_t t = zxm_at(g->link, arr, i);
        if (t < 0 || g->link->tok[t].type != W4_J_STR ||
            w4_json_str(g->link->resp, &g->link->tok[t], fol[i], sizeof fol[i]) < 0 ||
            !hex_ok(fol[i], GAZETTE_HEX_ID, false))
            return ZXM_E_PROTOCOL;
        if (gazette_find(g, GAZETTE_SRC_NODE, fol[i]) < 0) fresh++;
    }
    if (g->nsrc + fresh > GAZETTE_SOURCES) return ZXM_E_TOOBIG;
    memcpy(g->me, id, sizeof id);
    for (uint32_t s = 0; s < g->nsrc; s++)
        if (g->src[s].kind == GAZETTE_SRC_NODE) g->src[s].followed = false;
    for (uint32_t i = 0; i < n; i++) add_followed(g, GAZETTE_SRC_NODE, fol[i], NULL);
    return ZXM_OK;
}

int gazette_follow(gazette_t *g, const char *node_id, bool follow)
{
    char id[GAZETTE_HEX_ID + 1];
    bool now = !follow;
    if (!hex_ok(node_id, GAZETTE_HEX_ID, false) || (g->me[0] && !strcmp(node_id, g->me)))
        return ZXM_E_ARG;
    int32_t was = gazette_find(g, GAZETTE_SRC_NODE, node_id);
    if (follow && was < 0 && g->nsrc >= GAZETTE_SOURCES) return ZXM_E_TOOBIG;
    zxm_begin(g->link, "chat.follow");
    zxm_p_str(g->link, "node_id", node_id);
    zxm_p_bool(g->link, "follow", follow);
    int rc = zxm_call(g->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = g->link->result;
    if (zxm_get_str(g->link, o, "node_id", id, sizeof id) != ZXM_OK || strcmp(id, node_id) ||
        zxm_get_bool(g->link, o, "following", &now) != ZXM_OK || now != follow)
        return ZXM_E_PROTOCOL;
    if (follow) return add_followed(g, GAZETTE_SRC_NODE, id, NULL);
    if (was >= 0) g->src[was].followed = false;
    return ZXM_OK;
}

int gazette_add_space(gazette_t *g, const char *cid, uint32_t *at)
{
    if (!cid || !seedbank_cid_ok(cid, (uint32_t) strlen(cid))) return ZXM_E_ARG;
    return add_followed(g, GAZETTE_SRC_SPACE, cid, at);
}

int gazette_add_channel(gazette_t *g, const char *chan, uint32_t *at)
{
    if (!chan_ok(chan)) return ZXM_E_ARG;
    return add_followed(g, GAZETTE_SRC_IRC, chan, at);
}

int gazette_keep(gazette_t *g, uint32_t src, bool follow)
{
    if (src >= g->nsrc) return ZXM_E_ARG;
    if (g->src[src].kind == GAZETTE_SRC_NODE) return ZXM_E_STATE; /* zxvd's FOLLOW decides */
    g->src[src].followed = follow;
    return ZXM_OK;
}

int gazette_mute(gazette_t *g, uint32_t src, bool muted)
{
    if (src >= g->nsrc) return ZXM_E_ARG;
    g->src[src].muted = muted;
    return ZXM_OK;
}

/* ---- items (GM2, GM3) ------------------------------------------------------- */

static bool held(const gazette_t *g, uint32_t src, uint8_t feed, const char *id)
{
    for (uint32_t i = 0; i < g->nitem; i++)
        if (g->item[i].source == src && g->item[i].feed == feed && !strcmp(g->item[i].id, id))
            return true;
    return false;
}

static uint32_t read_count(const gazette_t *g)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < g->nitem; i++) n += g->item[i].read ? 1u : 0u;
    return n;
}

uint32_t gazette_drop_read(gazette_t *g)
{
    uint32_t m = 0, gone = 0;
    for (uint32_t i = 0; i < g->nitem; i++) {
        gazette_item_t *it = &g->item[i];
        if (it->read) {
            gazette_source_t *s = &g->src[it->source];
            if (it->order > s->mark) s->mark = it->order;
            gone++;
            continue;
        }
        if (m != i) g->item[m] = *it;
        m++;
    }
    g->nitem = m;
    return gone;
}

/* Keep PAGE[0..n) in zxvd's order: every check first (sources, room, GR3,
 * CR3), then the list changes. An item held already, one let go, or a
 * repeat within the page is passed over. */
static int add_page(gazette_t *g, uint32_t n)
{
    static int32_t at[GAZETTE_PAGE];
    static bool fresh[GAZETTE_PAGE];
    static int64_t more[GAZETTE_SOURCES];
    uint32_t nsrc = g->nsrc, nfresh = 0;
    memset(more, 0, sizeof more);
    for (uint32_t k = 0; k < n; k++) {
        gz_new_t *p = &PAGE[k];
        int32_t s = gazette_find(g, p->kind, p->key);
        for (uint32_t j = 0; s < 0 && j < k; j++)
            if (at[j] >= (int32_t) g->nsrc && PAGE[j].kind == p->kind &&
                !strcmp(PAGE[j].key, p->key))
                s = at[j];
        if (s < 0) {
            if (nsrc >= GAZETTE_SOURCES) return ZXM_E_TOOBIG;
            s = (int32_t) nsrc++;
        }
        at[k] = s;
        fresh[k] = !(s < (int32_t) g->nsrc && (held(g, (uint32_t) s, p->it.feed, p->it.id) ||
                                               p->it.order <= g->src[s].mark));
        for (uint32_t j = 0; fresh[k] && j < k; j++)
            if (fresh[j] && at[j] == s && PAGE[j].it.feed == p->it.feed &&
                !strcmp(PAGE[j].it.id, p->it.id))
                fresh[k] = false;
        if (fresh[k]) nfresh++, more[s]++;
    }
    uint32_t room = GAZETTE_ITEMS - g->nitem, drop = 0;
    if (nfresh > room) {
        drop = read_count(g);
        if (nfresh > room + drop) return ZXM_E_TOOBIG;
    }
    uint32_t base = g->nitem - (nfresh > room ? drop : 0u), pos = base;
    for (uint32_t k = 0; k < n; k++) {
        if (!fresh[k]) continue;
        if (!gazette_read_record_fits(at[k], pos++, PAGE[k].it.order, PAGE[k].it.stamp, 0))
            return ZXM_E_PROTOCOL;
    }
    for (uint32_t s = 0; s < nsrc; s++) {
        int64_t newest = s < g->nsrc ? g->src[s].newest : 0;
        int64_t seen = s < g->nsrc ? g->src[s].seen : 0;
        if (more[s] && (newest > COMMONS_COUNT_MAX - more[s] ||
                        commons_room_unread(newest + more[s], seen) == COMMONS_REFUSED))
            return ZXM_E_PROTOCOL;
    }
    /* every check passed: the list changes */
    if (nfresh > room) gazette_drop_read(g);
    for (uint32_t k = 0; k < n; k++) {
        if ((uint32_t) at[k] >= g->nsrc) new_source(g, PAGE[k].kind, PAGE[k].key);
        if (!fresh[k]) continue;
        gazette_item_t *it = &g->item[g->nitem++];
        *it = PAGE[k].it;
        it->source = (uint32_t) at[k];
        it->read = false;
        g->src[at[k]].newest++;
    }
    return ZXM_OK;
}

/* the array at key of the result, at most cap long */
static int page_array(const zxm_link_t *l, const char *key, uint32_t cap, int32_t *arr, uint32_t *n)
{
    *arr = zxm_get(l, l->result, key);
    if (*arr < 0 || l->tok[*arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    *n = zxm_len(l, *arr);
    return *n > cap ? ZXM_E_TOOBIG : ZXM_OK;
}

/* one post of chat.feed or social.feed into PAGE[*k]; mine and reactions passed over */
static int read_post(gazette_t *g, int32_t o, uint8_t feed, uint32_t *k)
{
    const zxm_link_t *l = g->link;
    gz_new_t *p = &PAGE[*k];
    char kind[16];
    bool mine = false;
    memset(p, 0, sizeof *p);
    p->kind = GAZETTE_SRC_NODE;
    p->it.feed = feed;
    if (o < 0 || zxm_get_str(l, o, "id", p->it.id, sizeof p->it.id) != ZXM_OK ||
        !hex_ok(p->it.id, GAZETTE_HEX_ID, false) ||
        zxm_get_str(l, o, "author", p->key, sizeof p->key) != ZXM_OK ||
        !hex_ok(p->key, GAZETTE_HEX_ID, false) || zxm_get_bool(l, o, "mine", &mine) != ZXM_OK ||
        zxm_get_str(l, o, "kind", kind, sizeof kind) != ZXM_OK ||
        get_dec(l, o, "lamport", &p->it.order) != ZXM_OK ||
        get_text(l, o, "text", &p->it) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    if (strcmp(kind, "post") && strcmp(kind, "reply") && strcmp(kind, "reaction"))
        return ZXM_E_PROTOCOL;
    if (feed == GAZETTE_SOCIAL && !strcmp(kind, "reaction")) return ZXM_E_PROTOCOL;
    if (zxm_get(l, o, "wall_ms") >= 0 && !is_null(l, o, "wall_ms") &&
        get_dec(l, o, "wall_ms", &p->it.stamp) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    if (feed == GAZETTE_CHAT && zxm_get(l, o, "wall_ms") < 0) return ZXM_E_PROTOCOL;
    memcpy(p->it.by, p->key, sizeof p->it.by);
    if (mine || !strcmp(kind, "reaction") || (g->me[0] && !strcmp(p->key, g->me)))
        return ZXM_OK; /* GM2: not news */
    (*k)++;
    return ZXM_OK;
}

int gazette_chat(gazette_t *g)
{
    int32_t arr;
    uint32_t n = 0, k = 0;
    zxm_begin(g->link, "chat.feed");
    int rc = zxm_call(g->link);
    if (rc != ZXM_OK) return rc;
    rc = page_array(g->link, "posts", GAZETTE_PAGE, &arr, &n);
    for (uint32_t i = 0; rc == ZXM_OK && i < n; i++)
        rc = read_post(g, zxm_at(g->link, arr, i), GAZETTE_CHAT, &k);
    return rc == ZXM_OK ? add_page(g, k) : rc;
}

int gazette_social(gazette_t *g, uint32_t limit)
{
    int32_t arr;
    uint32_t n = 0, k = 0;
    if (limit < 1u || limit > GAZETTE_PAGE) return ZXM_E_ARG;
    zxm_begin(g->link, "social.feed");
    zxm_p_u64(g->link, "limit", limit);
    int rc = zxm_call(g->link);
    if (rc != ZXM_OK) return rc;
    rc = page_array(g->link, "items", limit, &arr, &n);
    for (uint32_t i = 0; rc == ZXM_OK && i < n; i++)
        rc = read_post(g, zxm_at(g->link, arr, i), GAZETTE_SOCIAL, &k);
    return rc == ZXM_OK ? add_page(g, k) : rc;
}

/* GM6: one envelope into PAGE[*k]; only another companion's signed ones */
static int read_envelope(gazette_t *g, int32_t o, const char *self, uint32_t *k)
{
    const zxm_link_t *l = g->link;
    gz_new_t *p = &PAGE[*k];
    char kind[16], status[16];
    uint64_t id = 0, ts = 0;
    memset(p, 0, sizeof *p);
    p->kind = GAZETTE_SRC_AGENT;
    p->it.feed = GAZETTE_WEB4;
    if (o < 0 || zxm_get_u64(l, o, "id", &id) != ZXM_OK || id < 1u || id > UINT32_MAX ||
        zxm_get_str(l, o, "kind", kind, sizeof kind) != ZXM_OK ||
        zxm_get_str(l, o, "status", status, sizeof status) != ZXM_OK ||
        zxm_get_str(l, o, "from", p->key, sizeof p->key) != ZXM_OK ||
        !hex_ok(p->key, GAZETTE_HEX_ID, true) || get_text(l, o, "payload", &p->it) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    if (zxm_get(l, o, "ts") >= 0 && !is_null(l, o, "ts")) {
        if (zxm_get_u64(l, o, "ts", &ts) != ZXM_OK || ts > (uint64_t) GAZETTE_STAMP_FIELD / 1000u)
            return ZXM_E_PROTOCOL;
    }
    lower(p->key);
    put_u64(p->it.id, sizeof p->it.id, id);
    memcpy(p->it.by, p->key, sizeof p->it.by);
    p->it.order = (int64_t) id;
    p->it.stamp = (int64_t) ts * 1000;
    if (strcmp(kind, "REQUEST") && strcmp(kind, "RESPONSE") && strcmp(kind, "STREAM") &&
        strcmp(kind, "CONSENT"))
        return ZXM_E_PROTOCOL;
    if (strcmp(status, "signed") || !strcmp(kind, "CONSENT") || (self[0] && !strcmp(p->key, self)))
        return ZXM_OK; /* GM6: not news */
    (*k)++;
    return ZXM_OK;
}

int gazette_web4(gazette_t *g, uint32_t limit)
{
    char self[GAZETTE_HEX_ID + 1];
    bool open = false;
    int32_t arr;
    uint32_t n = 0, k = 0;
    if (limit < 1u || limit > GAZETTE_PAGE) return ZXM_E_ARG;
    zxm_begin(g->link, "web4.feed");
    zxm_p_u64(g->link, "limit", limit);
    zxm_p_str(g->link, "status", "signed");
    int rc = zxm_call(g->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = g->link->result;
    self[0] = 0;
    if (zxm_get_bool(g->link, o, "open", &open) != ZXM_OK) return ZXM_E_PROTOCOL;
    if (!is_null(g->link, o, "agent_id") &&
        (zxm_get_str(g->link, o, "agent_id", self, sizeof self) != ZXM_OK ||
         !hex_ok(self, GAZETTE_HEX_ID, true)))
        return ZXM_E_PROTOCOL;
    lower(self);
    rc = page_array(g->link, "envelopes", limit, &arr, &n);
    if (rc == ZXM_OK && !open && n) return ZXM_E_PROTOCOL; /* closed: no envelopes */
    for (uint32_t i = 0; rc == ZXM_OK && i < n; i++)
        rc = read_envelope(g, zxm_at(g->link, arr, i), self, &k);
    return rc == ZXM_OK ? add_page(g, k) : rc;
}

int gazette_space(gazette_t *g, uint32_t src)
{
    int32_t arr;
    uint32_t n = 0, k = 0;
    int64_t next = 0;
    if (src >= g->nsrc || g->src[src].kind != GAZETTE_SRC_SPACE) return ZXM_E_ARG;
    const gazette_source_t *s = &g->src[src];
    zxm_begin(g->link, "pspace.events");
    zxm_p_str(g->link, "cid", s->key);
    zxm_p_i64(g->link, "since", s->next);
    int rc = zxm_call(g->link);
    if (rc != ZXM_OK) return rc;
    const zxm_link_t *l = g->link;
    if (zxm_get_i64(l, l->result, "next", &next) != ZXM_OK || next < s->next ||
        next > GAZETTE_LAMPORT_FIELD)
        return ZXM_E_PROTOCOL;
    rc = page_array(l, "events", GAZETTE_EVENTS, &arr, &n);
    for (uint32_t i = 0; rc == ZXM_OK && i < n; i++) {
        int32_t o = zxm_at(l, arr, i);
        gz_new_t *p = &PAGE[k];
        char kind[8];
        int64_t lamport = 0;
        memset(p, 0, sizeof *p);
        p->kind = GAZETTE_SRC_SPACE;
        memcpy(p->key, s->key, sizeof p->key);
        p->it.feed = GAZETTE_SPACE;
        if (o < 0 || zxm_get_i64(l, o, "index", &p->it.order) != ZXM_OK || p->it.order < s->next ||
            p->it.order >= next || zxm_get_str(l, o, "kind", kind, sizeof kind) != ZXM_OK ||
            zxm_get_i64(l, o, "lamport", &lamport) != ZXM_OK || lamport < 0 ||
            zxm_get_i64(l, o, "created_ms", &p->it.stamp) != ZXM_OK || p->it.stamp < 0 ||
            zxm_get_str(l, o, "author", p->it.by, sizeof p->it.by) != ZXM_OK ||
            !hex_ok(p->it.by, GAZETTE_HEX_ID, false))
            return ZXM_E_PROTOCOL;
        if (strcmp(kind, "post") && strcmp(kind, "hall")) return ZXM_E_PROTOCOL;
        if (strcmp(kind, "post") || (g->me[0] && !strcmp(p->it.by, g->me)))
            continue; /* a watch-hall change, or the owner's own: not news */
        if (get_text(l, o, "text", &p->it) != ZXM_OK) return ZXM_E_PROTOCOL;
        put_u64(p->it.id, sizeof p->it.id, (uint64_t) p->it.order);
        k++;
    }
    if (rc != ZXM_OK) return rc;
    rc = add_page(g, k);
    if (rc == ZXM_OK) g->src[src].next = next;
    return rc;
}

int gazette_irc(gazette_t *g, const char *msgid)
{
    char id[GAZETTE_HEX_ID + 1], truth[4];
    if (!hex_ok(msgid, 32u, false)) return ZXM_E_ARG;
    zxm_begin(g->link, "irc.record");
    zxm_p_str(g->link, "msgid", msgid);
    int rc = zxm_call(g->link);
    if (rc != ZXM_OK) return rc;
    const zxm_link_t *l = g->link;
    int32_t o = l->result;
    gz_new_t *p = &PAGE[0];
    memset(p, 0, sizeof *p);
    p->kind = GAZETTE_SRC_IRC;
    p->it.feed = GAZETTE_IRC;
    if (zxm_get_str(l, o, "msgid", id, sizeof id) != ZXM_OK || strcmp(id, msgid) ||
        zxm_get_str(l, o, "target", p->key, sizeof p->key) != ZXM_OK || !chan_ok(p->key) ||
        zxm_get_str(l, o, "nick", p->it.by, sizeof p->it.by) != ZXM_OK || !p->it.by[0] ||
        zxm_get_str(l, o, "truth", truth, sizeof truth) != ZXM_OK || strlen(truth) != 1u ||
        !strchr("TFBN", truth[0]) || get_dec(l, o, "ms", &p->it.order) != ZXM_OK ||
        get_text(l, o, "text", &p->it) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    memcpy(p->it.id, id, sizeof id);
    p->it.stamp = p->it.order;
    return add_page(g, 1);
}

/* ---- shown, unread, read (GM4, GM5) ----------------------------------------- */

bool gazette_shown(const gazette_t *g, uint32_t item)
{
    if (item >= g->nitem) return false;
    const gazette_source_t *s = &g->src[g->item[item].source];
    return gazette_feed_item_shown(s->followed ? 1 : 0, s->muted ? 1 : 0);
}

uint32_t gazette_view(const gazette_t *g, uint32_t *out, uint32_t cap)
{
    uint32_t n = 0;
    for (uint32_t i = 0; out && i < g->nitem && n < cap; i++)
        if (gazette_shown(g, i)) out[n++] = i;
    return n;
}

int64_t gazette_unread(const gazette_t *g, uint32_t src)
{
    if (src >= g->nsrc) return GAZETTE_REFUSED;
    return commons_room_unread(g->src[src].newest, g->src[src].seen);
}

int64_t gazette_unread_total(const gazette_t *g)
{
    static int64_t newest[GAZETTE_SOURCES], seen[GAZETTE_SOURCES], shown[GAZETTE_SOURCES];
    for (uint32_t i = 0; i < g->nsrc; i++) {
        const gazette_source_t *s = &g->src[i];
        newest[i] = s->newest;
        seen[i] = s->seen;
        shown[i] = gazette_feed_item_shown(s->followed ? 1 : 0, s->muted ? 1 : 0) ? 1 : 0;
    }
    return gazette_feed_unread_total(newest, seen, shown, g->nsrc);
}

int gazette_mark_read(gazette_t *g, uint32_t item)
{
    if (item >= g->nitem) return ZXM_E_ARG;
    gazette_item_t *it = &g->item[item];
    gazette_source_t *s = &g->src[it->source];
    if (it->read) return ZXM_OK;
    if (!gazette_read_record_fits(it->source, item, it->order, it->stamp, 1) ||
        s->seen >= s->newest)
        return ZXM_E_STATE;
    it->read = true;
    s->seen++;
    return ZXM_OK;
}

int gazette_mark_source_read(gazette_t *g, uint32_t src)
{
    int64_t n = 0;
    if (src >= g->nsrc) return ZXM_E_ARG;
    gazette_source_t *s = &g->src[src];
    for (uint32_t i = 0; i < g->nitem; i++) {
        const gazette_item_t *it = &g->item[i];
        if (it->source != src || it->read) continue;
        if (!gazette_read_record_fits(src, i, it->order, it->stamp, 1)) return ZXM_E_STATE;
        n++;
    }
    if (s->seen + n > s->newest) return ZXM_E_STATE;
    for (uint32_t i = 0; i < g->nitem; i++)
        if (g->item[i].source == src) g->item[i].read = true;
    s->seen += n;
    return ZXM_OK;
}

/* ---- notices (GM7) ---------------------------------------------------------- */

void gazette_notices(gazette_t *g, bool on)
{
    g->notices = on;
    if (!on) g->notice[0] = 0;
}

int gazette_notice(gazette_t *g)
{
    g->notice[0] = 0;
    if (!g->notices) return ZXM_OK;
    int64_t total = gazette_unread_total(g);
    if (total == GAZETTE_REFUSED) return ZXM_E_PROTOCOL;
    if (total > 0)
        snprintf(g->notice, sizeof g->notice, "%lld new from the sources you follow.",
                 (long long) total);
    return ZXM_OK;
}
