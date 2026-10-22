/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hamlet_model.c — Hamlet, your social circle's feed (hamlet_model.h
 * HM1-HM6). */
#include "hamlet_model.h"

#include <string.h>

static const char *const KIND[] = {"post", "reply"};
static const char *const ROLE[] = {"none", "observer", "member", "admin", "owner"};
static const char *const STATUS[] = {"held", "signed", "denied"}; /* HAMLET_GIFT_* - 1 */

#define HAMLET_NOW_MAX INT64_C(253402300799) /* zxvd's now: 9999-12-31 */

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

/* CR2's length and something besides spaces (zxvd's social.post asks the same) */
static bool message_ok(const char *s)
{
    if (!s || !commons_message_text_ok((int64_t) strlen(s))) return false;
    for (const char *p = s; *p; p++)
        if (*p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') return true;
    return false;
}

/* a decimal of at most 18 digits, a leading '-' allowed when sign is true */
static int get_dec(const zxm_link_t *l, int32_t obj, const char *key, bool sign, int64_t *v)
{
    char s[24];
    int64_t x = 0;
    uint32_t n = 0, at = 0;
    bool neg = false;
    if (zxm_get_str(l, obj, key, s, sizeof s) != ZXM_OK) return ZXM_E_PROTOCOL;
    if (sign && s[0] == '-') neg = true, at = 1;
    for (; s[at + n]; n++) {
        char ch = s[at + n];
        if (ch < '0' || ch > '9' || n >= 18u) return ZXM_E_PROTOCOL;
        x = x * 10 + (ch - '0');
    }
    if (!n) return ZXM_E_PROTOCOL;
    *v = neg ? -x : x;
    return ZXM_OK;
}

/* an integer at least 0 */
static int get_count(const zxm_link_t *l, int32_t obj, const char *key, int64_t *v)
{
    if (zxm_get_i64(l, obj, key, v) != ZXM_OK || *v < 0) return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

/* a 64-digit hex id that may be null ("") */
static int get_opt_id(const zxm_link_t *l, int32_t obj, const char *key, char *out)
{
    out[0] = 0;
    if (zxm_get(l, obj, key) < 0 || is_null(l, obj, key)) return ZXM_OK;
    if (zxm_get_str(l, obj, key, out, HAMLET_HEX_ID + 1u) != ZXM_OK || !hex_ok(out, HAMLET_HEX_ID))
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

static int read_role(const zxm_link_t *l, int32_t o, uint8_t *role)
{
    char s[16];
    if (zxm_get_str(l, o, "my_role", s, sizeof s) != ZXM_OK) return ZXM_E_PROTOCOL;
    int32_t r = code_of(s, ROLE, 5);
    if (r < 0) return ZXM_E_PROTOCOL;
    *role = (uint8_t) r;
    return ZXM_OK;
}

/* ---- groups (HM1) ----------------------------------------------------------- */

void hamlet_init(hamlet_t *h, zxm_link_t *link)
{
    memset(h, 0, sizeof *h);
    h->link = link;
    h->ceiling = 0; /* HM5: no gift before the owner sets a ceiling */
}

int32_t hamlet_find_group(const hamlet_t *h, const char *id)
{
    if (!id) return -1;
    for (uint32_t i = 0; i < h->ngroup; i++)
        if (!strcmp(h->group[i].id, id)) return (int32_t) i;
    return -1;
}

static bool author_muted(const hamlet_t *h, const char *author)
{
    for (uint32_t i = 0; i < h->nmuted; i++)
        if (!strcmp(h->muted[i], author)) return true;
    return false;
}

/* HM3: HR2 for one item, from the seat's role in its group and its mutes */
static bool decide_shown(const hamlet_t *h, const hamlet_post_t *p)
{
    int64_t role = -1, mutes = author_muted(h, p->author) ? 1 : 0;
    if (p->group[0]) {
        int32_t g = hamlet_find_group(h, p->group);
        role = g < 0 ? HAMLET_ROLE_NONE : h->group[g].role;
        if (g >= 0 && h->group[g].muted) mutes++;
    }
    return hamlet_post_show_ok(hamlet_role_room(role), hamlet_mute_room(mutes));
}

static void decide_all(hamlet_t *h)
{
    h->nshown = 0;
    for (uint32_t i = 0; i < h->nfeed; i++) {
        h->feed[i].shown = decide_shown(h, &h->feed[i]);
        if (h->feed[i].shown) h->nshown++;
    }
}

int hamlet_groups(hamlet_t *h)
{
    static hamlet_group_t fresh[HAMLET_GROUPS];
    zxm_begin(h->link, "social.groups");
    int rc = zxm_call(h->link);
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(h->link, h->link->result, "groups");
    if (arr < 0 || h->link->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(h->link, arr);
    if (n > HAMLET_GROUPS) return ZXM_E_TOOBIG;
    for (uint32_t i = 0; i < n; i++) {
        int32_t g = zxm_at(h->link, arr, i);
        hamlet_group_t *r = &fresh[i];
        memset(r, 0, sizeof *r);
        if (zxm_get_str(h->link, g, "id", r->id, sizeof r->id) != ZXM_OK ||
            !hex_ok(r->id, HAMLET_HEX_ID) ||
            zxm_get_str(h->link, g, "name", r->name, sizeof r->name) != ZXM_OK ||
            zxm_get_bool(h->link, g, "open", &r->open) != ZXM_OK ||
            get_count(h->link, g, "members", &r->members) != ZXM_OK ||
            read_role(h->link, g, &r->role) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        for (uint32_t k = 0; k < i; k++)
            if (!strcmp(fresh[k].id, r->id)) return ZXM_E_PROTOCOL; /* one id, one group */
        /* HM4: a group known before keeps its marker, its counts and its mute */
        int32_t was = hamlet_find_group(h, r->id);
        if (was >= 0) {
            const hamlet_group_t *o = &h->group[was];
            r->muted = o->muted;
            r->newest = o->newest;
            r->seen = o->seen;
            r->mark = o->mark;
            r->top = o->top;
            r->stamp = o->stamp;
        }
        if (!hamlet_read_marker_ok((int64_t) i, r->mark, r->stamp)) return ZXM_E_PROTOCOL;
    }
    memcpy(h->group, fresh, n * sizeof fresh[0]);
    h->ngroup = n;
    decide_all(h);
    return ZXM_OK;
}

static int membership(hamlet_t *h, const char *method, const char *group)
{
    char g[HAMLET_HEX_ID + 1];
    uint8_t role = 0;
    int64_t members = 0;
    if (!hex_ok(group, HAMLET_HEX_ID)) return ZXM_E_ARG;
    int32_t i = hamlet_find_group(h, group);
    if (i < 0) return ZXM_E_STATE;
    bool leave = !strcmp(method, "social.leave");
    /* the owner never leaves, nor does a member join again (zxvd refuses both too) */
    if (leave ? h->group[i].role == HAMLET_ROLE_OWNER || h->group[i].role == HAMLET_ROLE_NONE
              : h->group[i].role >= HAMLET_ROLE_MEMBER)
        return ZXM_E_STATE;
    zxm_begin(h->link, method);
    zxm_p_str(h->link, "group", group);
    int rc = zxm_call(h->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = h->link->result;
    if (zxm_get_str(h->link, o, "group", g, sizeof g) != ZXM_OK || strcmp(g, group) ||
        read_role(h->link, o, &role) != ZXM_OK ||
        get_count(h->link, o, "members", &members) != ZXM_OK ||
        (leave ? role >= HAMLET_ROLE_MEMBER : role < HAMLET_ROLE_MEMBER))
        return ZXM_E_PROTOCOL;
    h->group[i].role = role;
    h->group[i].members = members;
    decide_all(h);
    return ZXM_OK;
}

int hamlet_join(hamlet_t *h, const char *group)
{
    return membership(h, "social.join", group);
}

int hamlet_leave(hamlet_t *h, const char *group)
{
    return membership(h, "social.leave", group);
}

/* ---- the feed (HM2-HM4) ----------------------------------------------------- */

/* one item at token o into p */
static int read_item(const zxm_link_t *l, int32_t o, hamlet_post_t *p)
{
    char kind[16];
    uint64_t replies = 0;
    memset(p, 0, sizeof *p);
    if (o < 0 || zxm_get_str(l, o, "id", p->id, sizeof p->id) != ZXM_OK ||
        !hex_ok(p->id, HAMLET_HEX_ID) ||
        zxm_get_str(l, o, "author", p->author, sizeof p->author) != ZXM_OK ||
        !hex_ok(p->author, HAMLET_HEX_ID) || zxm_get_bool(l, o, "mine", &p->mine) != ZXM_OK ||
        zxm_get_str(l, o, "kind", kind, sizeof kind) != ZXM_OK ||
        get_opt_id(l, o, "parent", p->parent) != ZXM_OK ||
        get_opt_id(l, o, "group", p->group) != ZXM_OK ||
        zxm_get_str(l, o, "text", p->text, sizeof p->text) != ZXM_OK ||
        get_dec(l, o, "lamport", false, &p->lamport) != ZXM_OK ||
        zxm_get_u64(l, o, "replies", &replies) != ZXM_OK || replies > UINT32_MAX)
        return ZXM_E_PROTOCOL;
    int32_t k = code_of(kind, KIND, 2);
    if (k < 0 || (k == HAMLET_REPLY) != (p->parent[0] != 0)) return ZXM_E_PROTOCOL;
    p->kind = (uint8_t) k;
    p->replies = (uint32_t) replies;
    int32_t arr = zxm_get(l, o, "reactions");
    if (arr >= 0 && l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr);
    for (uint32_t i = 0; i < n && p->nreact < HAMLET_REACTS; i++) {
        int32_t r = zxm_at(l, arr, i);
        hamlet_reaction_t *x = &p->react[p->nreact];
        int64_t count = 0;
        if (zxm_get_str(l, r, "reaction", x->reaction, sizeof x->reaction) != ZXM_OK ||
            !commons_reaction_text_ok((int64_t) strlen(x->reaction)) ||
            get_count(l, r, "count", &count) != ZXM_OK || count < 1 || count > UINT32_MAX ||
            zxm_get_bool(l, r, "mine", &x->mine) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        x->count = (uint32_t) count;
        p->nreact++;
    }
    return ZXM_OK;
}

int hamlet_feed(hamlet_t *h, uint32_t limit)
{
    static hamlet_post_t page[HAMLET_FEED_MAX];
    static int64_t fresh[HAMLET_GROUPS], top[HAMLET_GROUPS];
    char me[HAMLET_HEX_ID + 1];
    if (limit < 1u || limit > HAMLET_FEED_MAX) return ZXM_E_ARG;
    zxm_begin(h->link, "social.feed");
    zxm_p_u64(h->link, "limit", limit);
    int rc = zxm_call(h->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = h->link->result;
    if (zxm_get_str(h->link, o, "me", me, sizeof me) != ZXM_OK || !hex_ok(me, HAMLET_HEX_ID))
        return ZXM_E_PROTOCOL;
    int32_t arr = zxm_get(h->link, o, "items");
    if (arr < 0 || h->link->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(h->link, arr);
    if (n > limit) return ZXM_E_PROTOCOL;
    for (uint32_t g = 0; g < h->ngroup; g++) fresh[g] = 0, top[g] = h->group[g].top;
    for (uint32_t i = 0; i < n; i++) {
        rc = read_item(h->link, zxm_at(h->link, arr, i), &page[i]);
        if (rc != ZXM_OK) return rc;
        if (page[i].mine != !strcmp(page[i].author, me)) return ZXM_E_PROTOCOL;
        /* HM4: a post by another, shown, in a held group and past its marker */
        int32_t g = page[i].group[0] ? hamlet_find_group(h, page[i].group) : -1;
        if (g < 0 || page[i].mine || !decide_shown(h, &page[i])) continue;
        if (page[i].lamport > h->group[g].mark) fresh[g]++;
        if (page[i].lamport > top[g]) top[g] = page[i].lamport;
    }
    for (uint32_t g = 0; g < h->ngroup; g++) {
        const hamlet_group_t *r = &h->group[g];
        int64_t newest = r->seen + fresh[g];
        if (commons_room_unread(newest, r->seen) == COMMONS_REFUSED ||
            !hamlet_read_marker_ok((int64_t) g, top[g], r->stamp))
            return ZXM_E_PROTOCOL;
    }
    /* HM2: zxvd's order, as given; nothing is written before every check */
    memcpy(h->feed, page, n * sizeof page[0]);
    h->nfeed = n;
    memcpy(h->me, me, sizeof me);
    for (uint32_t g = 0; g < h->ngroup; g++) {
        h->group[g].newest = h->group[g].seen + fresh[g];
        h->group[g].top = top[g];
    }
    decide_all(h);
    return ZXM_OK;
}

bool hamlet_shown(const hamlet_t *h, uint32_t i)
{
    return i < h->nfeed && h->feed[i].shown;
}

/* ---- posting and reacting --------------------------------------------------- */

int hamlet_post(hamlet_t *h, int32_t group, const char *text, const char *reply_to)
{
    char id[HAMLET_HEX_ID + 1], kind[16];
    if (!message_ok(text) || (reply_to && !hex_ok(reply_to, HAMLET_HEX_ID))) return ZXM_E_ARG;
    if (group < -1 || group >= (int32_t) h->ngroup) return ZXM_E_ARG;
    /* a group is posted in by its members only (zxvd refuses the rest too) */
    if (group >= 0 && h->group[group].role < HAMLET_ROLE_MEMBER) return ZXM_E_STATE;
    zxm_begin(h->link, "social.post");
    zxm_p_str(h->link, "text", text);
    if (reply_to) zxm_p_str(h->link, "reply_to", reply_to);
    if (group >= 0) zxm_p_str(h->link, "group", h->group[group].id);
    int rc = zxm_call(h->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = h->link->result;
    int64_t lamport = 0;
    if (zxm_get_str(h->link, o, "id", id, sizeof id) != ZXM_OK || !hex_ok(id, HAMLET_HEX_ID) ||
        zxm_get_str(h->link, o, "kind", kind, sizeof kind) != ZXM_OK ||
        code_of(kind, KIND, 2) != (reply_to ? HAMLET_REPLY : HAMLET_POST) ||
        get_dec(h->link, o, "lamport", false, &lamport) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

int hamlet_react(hamlet_t *h, const char *post, const char *reaction)
{
    char id[HAMLET_HEX_ID + 1];
    if (!hex_ok(post, HAMLET_HEX_ID) || !reaction ||
        !commons_reaction_text_ok((int64_t) strlen(reaction)))
        return ZXM_E_ARG;
    zxm_begin(h->link, "social.react");
    zxm_p_str(h->link, "post", post);
    zxm_p_str(h->link, "reaction", reaction);
    int rc = zxm_call(h->link);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(h->link, h->link->result, "id", id, sizeof id) != ZXM_OK ||
        !hex_ok(id, HAMLET_HEX_ID))
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

/* ---- mutes, unread and markers (HM4) ---------------------------------------- */

int hamlet_mute_author(hamlet_t *h, const char *author, bool mute)
{
    if (!hex_ok(author, HAMLET_HEX_ID)) return ZXM_E_ARG;
    for (uint32_t i = 0; i < h->nmuted; i++) {
        if (strcmp(h->muted[i], author)) continue;
        if (!mute) {
            memmove(h->muted[i], h->muted[i + 1u], (h->nmuted - i - 1u) * sizeof h->muted[0]);
            h->nmuted--;
            decide_all(h);
        }
        return ZXM_OK;
    }
    if (!mute) return ZXM_OK;
    if (h->nmuted >= HAMLET_MUTES) return ZXM_E_STATE; /* full: bounded */
    memcpy(h->muted[h->nmuted++], author, HAMLET_HEX_ID + 1u);
    decide_all(h);
    return ZXM_OK;
}

int hamlet_mute_group(hamlet_t *h, uint32_t group, bool mute)
{
    if (group >= h->ngroup) return ZXM_E_ARG;
    h->group[group].muted = mute;
    decide_all(h);
    return ZXM_OK;
}

int64_t hamlet_unread(const hamlet_t *h, uint32_t group)
{
    if (group >= h->ngroup) return HAMLET_REFUSED;
    return commons_room_unread(h->group[group].newest, h->group[group].seen);
}

int hamlet_mark_read(hamlet_t *h, uint32_t group, int64_t now)
{
    if (group >= h->ngroup || now < 0 || now > HAMLET_NOW_MAX) return ZXM_E_ARG;
    hamlet_group_t *r = &h->group[group];
    if (!hamlet_read_marker_ok((int64_t) group, r->top, now)) return ZXM_E_STATE;
    r->seen = r->newest;
    r->mark = r->top;
    r->stamp = now;
    return ZXM_OK;
}

/* append s to the notice; false when it does not fit */
static bool put(char *out, uint32_t *at, const char *s)
{
    uint32_t n = (uint32_t) strlen(s);
    if (*at + n > HAMLET_NOTICE_MAX) return false;
    memcpy(out + *at, s, n);
    *at += n;
    out[*at] = 0;
    return true;
}

/* a count of at most 12 digits as decimal text */
static void dec(int64_t v, char out[16])
{
    char t[16];
    uint32_t n = 0, k = 0;
    do {
        t[n++] = (char) ('0' + v % 10);
        v /= 10;
    } while (v > 0 && n < 15u);
    while (n) out[k++] = t[--n];
    out[k] = 0;
}

int hamlet_notice(hamlet_t *h)
{
    char num[16];
    uint32_t at = 0, groups = 0;
    h->notice[0] = 0;
    for (uint32_t g = 0; g < h->ngroup; g++) {
        int64_t u = hamlet_unread(h, g);
        if (u == HAMLET_REFUSED) return ZXM_E_STATE;
        if (u == 0 || h->group[g].muted) continue;
        dec(u, num);
        if ((groups && !put(h->notice, &at, "; ")) || !put(h->notice, &at, num) ||
            !put(h->notice, &at, u == 1 ? " new post in " : " new posts in ") ||
            !put(h->notice, &at, h->group[g].name[0] ? h->group[g].name : "a group")) {
            /* what does not fit is summed up, never cut mid-word */
            h->notice[0] = 0;
            at = 0;
            put(h->notice, &at, "New posts in your groups");
            return ZXM_OK;
        }
        groups++;
    }
    return ZXM_OK;
}

/* ---- gifts (HM5) ------------------------------------------------------------ */

int hamlet_set_ceiling(hamlet_t *h, int64_t ceiling, bool owner_confirmed)
{
    if (ceiling < 0 || ceiling > HAMLET_MINOR_MAX) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE; /* the owner's word, never the model's */
    h->ceiling = ceiling;
    return ZXM_OK;
}

static bool now_ok(int64_t now)
{
    return now >= 0 && now <= HAMLET_NOW_MAX;
}

/* one feed entry's id, status (HAMLET_GIFT_*) and amount */
static int entry_head(const zxm_link_t *l, int32_t o, uint32_t *id, uint8_t *status,
                      int64_t *amount)
{
    char st[16];
    uint64_t v = 0;
    if (o < 0 || zxm_get_u64(l, o, "id", &v) != ZXM_OK || v == 0 || v > UINT32_MAX ||
        zxm_get_str(l, o, "status", st, sizeof st) != ZXM_OK ||
        get_dec(l, o, "amount_minor", true, amount) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    int32_t s = code_of(st, STATUS, 3);
    if (s < 0) return ZXM_E_PROTOCOL;
    *id = (uint32_t) v;
    *status = (uint8_t) (s + 1);
    return ZXM_OK;
}

int hamlet_gift(hamlet_t *h, const char *to, int64_t amount, const char *note, int64_t now,
                bool owner_confirmed)
{
    uint32_t id = 0;
    uint8_t status = 0;
    int64_t got = 0;
    char who[HAMLET_HEX_ID + 1];
    if (!hex_ok(to, HAMLET_HEX_ID) || !note || strlen(note) > HAMLET_NOTE_MAX ||
        (now != -1 && !now_ok(now)))
        return ZXM_E_ARG;
    /* HR1: 1 up to the owner's ceiling, never past it */
    if (hamlet_gift_room(amount, h->ceiling) == HAMLET_REFUSED) return ZXM_E_ARG;
    if (h->gift.status == HAMLET_GIFT_HELD) return ZXM_E_STATE; /* one held gift at a time */
    if (!owner_confirmed) return ZXM_E_STATE;                   /* never by itself */
    zxm_begin(h->link, "web4.post");
    zxm_p_str(h->link, "kind", "REQUEST");
    zxm_p_str(h->link, "to", to);
    zxm_p_str(h->link, "tool", "gift");
    zxm_p_str(h->link, "payload", note);
    zxm_p_i64(h->link, "amount_minor", amount);
    if (now >= 0) zxm_p_u64(h->link, "now", (uint64_t) now);
    int rc = zxm_call(h->link);
    if (rc != ZXM_OK) return rc;
    int32_t o = h->link->result;
    /* money is held, unsigned, until web4.consent answers it */
    if (entry_head(h->link, o, &id, &status, &got) != ZXM_OK || status != HAMLET_GIFT_HELD ||
        got != amount || zxm_get_str(h->link, o, "to", who, sizeof who) != ZXM_OK ||
        strcmp(who, to))
        return ZXM_E_PROTOCOL;
    h->gift.id = id;
    memcpy(h->gift.to, to, sizeof h->gift.to);
    h->gift.amount = amount;
    h->gift.status = HAMLET_GIFT_HELD;
    return ZXM_OK;
}

int hamlet_gift_answer(hamlet_t *h, bool grant, int64_t expires_in, int64_t now,
                       bool owner_confirmed)
{
    uint32_t id = 0;
    uint8_t status = 0;
    int64_t got = 0;
    if (!now_ok(now) || expires_in < 1 || expires_in > HAMLET_GRANT_MAX) return ZXM_E_ARG;
    if (h->gift.status != HAMLET_GIFT_HELD) return ZXM_E_STATE;
    if (!owner_confirmed) return ZXM_E_STATE; /* never by itself */
    if (grant) {
        /* RR1: the money scope, the ceiling's room after the amount, the expiry */
        int64_t gap = ramble_scope_gap(ramble_need(false, h->gift.amount), RAMBLE_SCOPE_MONEY);
        int64_t room = ramble_margin(h->ceiling, h->gift.amount);
        if (!ramble_consent_covers(gap, room, expires_in)) return ZXM_E_STATE;
    }
    zxm_begin(h->link, "web4.consent");
    zxm_p_u64(h->link, "id", h->gift.id);
    zxm_p_bool(h->link, "grant", grant);
    zxm_p_u64(h->link, "expires_in", (uint64_t) expires_in);
    zxm_p_u64(h->link, "now", (uint64_t) now);
    int rc = zxm_call(h->link);
    if (rc != ZXM_OK) return rc;
    int32_t e = zxm_get(h->link, h->link->result, "envelope");
    if (entry_head(h->link, e, &id, &status, &got) != ZXM_OK || id != h->gift.id ||
        status != (grant ? HAMLET_GIFT_SIGNED : HAMLET_GIFT_DENIED))
        return ZXM_E_PROTOCOL;
    h->gift.status = status;
    return ZXM_OK;
}
