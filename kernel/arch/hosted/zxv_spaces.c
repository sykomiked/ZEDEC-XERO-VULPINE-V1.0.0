/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_spaces.c — spaces (see zxv_spaces.h S1-S4). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_spaces.h"
#include "web4_web2.h"

#define ZXS_JSON_MAX (96u * 1024u) /* a manifest with every member and proposal */

/* ===================== S1 ===================== */

#define V ZXS_VIEW
#define W ZXS_WRITE
static const unsigned char RIGHTS[ZXS_KINDS][ZXS_ROLES] = {
    /*            owner  member guest  public */
    [ZXS_HOME] = {V | W, 0, 0, 0},
    [ZXS_OFFICE] = {V | W, V | W, V, 0},
    [ZXS_TEAM] = {V | W, V | W, V, 0},
    [ZXS_CLOUDTOP] = {V | W, V, V, V},
    [ZXS_PARK] = {V | W, V | W, V | W, V | W},
};
#undef V
#undef W

unsigned zxs_rights(zxs_kind_t kind, zxs_role_t role)
{
    if ((unsigned) kind >= ZXS_KINDS || (unsigned) role >= ZXS_ROLES) return 0;
    return RIGHTS[kind][role];
}

bool zxs_can(const zxs_space_t *s, unsigned want)
{
    return want && (zxs_rights((zxs_kind_t) s->kind, (zxs_role_t) s->role) & want) == want;
}

static const char *const KIND[ZXS_KINDS] = {"home", "office", "team", "cloudtop", "park"};
static const char *const ROLE[ZXS_ROLES] = {"owner", "member", "guest", "public"};

const char *zxs_kind_name(zxs_kind_t k)
{
    return (unsigned) k < ZXS_KINDS ? KIND[k] : "?";
}

const char *zxs_role_name(zxs_role_t r)
{
    return (unsigned) r < ZXS_ROLES ? ROLE[r] : "?";
}

int zxs_kind_parse(const char *s)
{
    for (int i = 0; i < ZXS_KINDS; i++)
        if (!strcmp(s, KIND[i])) return i;
    return -1;
}

int zxs_role_parse(const char *s)
{
    for (int i = 0; i < ZXS_ROLES; i++)
        if (!strcmp(s, ROLE[i])) return i;
    return -1;
}

bool zxs_who_ok(const char *who)
{
    size_t n = strlen(who);
    if (n == 0 || n > ZXS_WHO_MAX) return false;
    for (size_t i = 0; i < n; i++)
        if (who[i] < 0x21 || who[i] > 0x7e || who[i] == '"' || who[i] == '\\') return false;
    return true;
}

/* a display name: 1..80 bytes of UTF-8 with no control characters */
static bool name_ok(const char *p)
{
    const unsigned char *s = (const unsigned char *) p;
    size_t n = strlen(p);
    if (n == 0 || n > ZXS_NAME_MAX) return false;
    for (size_t i = 0; i < n;) {
        unsigned c = s[i];
        if (c < 0x80) {
            if (c < 0x20 || c == 0x7f) return false;
            i++;
            continue;
        }
        size_t k = c >= 0xc2 && c <= 0xdf   ? 1
                   : c >= 0xe0 && c <= 0xef ? 2
                   : c >= 0xf0 && c <= 0xf4 ? 3
                                            : 0;
        if (!k) return false;
        uint32_t cp = c & (k == 1 ? 0x1f : k == 2 ? 0x0f : 0x07);
        for (size_t j = 1; j <= k; j++) {
            if (i + j >= n || (s[i + j] & 0xc0) != 0x80) return false;
            cp = cp << 6 | (s[i + j] & 0x3f);
        }
        if ((k == 2 && cp < 0x800) || (k == 3 && (cp < 0x10000 || cp > 0x10ffff)) ||
            (cp >= 0xd800 && cp <= 0xdfff))
            return false;
        i += k + 1;
    }
    return true;
}

/* ===================== S3 ===================== */

static bool is_public(const zxs_space_t *s)
{
    return s->kind == ZXS_CLOUDTOP || s->kind == ZXS_PARK;
}

/* Is `who` in s's audience? This node always is (it holds the copy). */
static bool sees(const zxs_space_t *s, const char *who)
{
    if (is_public(s) || !strcmp(who, ZXS_ME) || !strcmp(who, s->owner)) return true;
    if (s->kind == ZXS_HOME) return false;
    for (uint32_t i = 0; i < s->n; i++)
        if (!strcmp(s->m[i].who, who)) return true;
    return false;
}

bool zxs_widens(const zxs_space_t *from, const zxs_space_t *to)
{
    if (to->kind == ZXS_HOME) return false;
    if (to->kind > from->kind) return true; /* kinds run from private to open */
    if (is_public(from)) return false;
    if (is_public(to)) return true;
    if (!sees(from, to->owner)) return true;
    for (uint32_t i = 0; i < to->n; i++)
        if (!sees(from, to->m[i].who)) return true;
    return false;
}

/* ===================== JSON ===================== */

typedef struct {
    char *p;
    uint32_t n, cap;
    bool over;
} out_t;

static void put(out_t *o, const char *s, size_t n)
{
    if (o->over || o->n + n >= o->cap) {
        o->over = true;
        return;
    }
    memcpy(o->p + o->n, s, n);
    o->n += (uint32_t) n;
}

static void puts_(out_t *o, const char *s)
{
    put(o, s, strlen(s));
}

static void str(out_t *o, const char *s)
{
    put(o, "\"", 1);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') put(o, "\\", 1);
        put(o, s, 1);
    }
    put(o, "\"", 1);
}

int32_t zxs_json(const zxs_space_t *s, char *out, uint32_t cap)
{
    out_t o = {out, 0, cap, false};
    puts_(&o, "{\"id\":");
    str(&o, s->id);
    puts_(&o, ",\"kind\":");
    str(&o, zxs_kind_name((zxs_kind_t) s->kind));
    puts_(&o, ",\"name\":");
    str(&o, s->name);
    puts_(&o, ",\"owner\":");
    str(&o, s->owner);
    puts_(&o, ",\"role\":");
    str(&o, zxs_role_name((zxs_role_t) s->role));
    unsigned r = zxs_rights((zxs_kind_t) s->kind, (zxs_role_t) s->role);
    puts_(&o, r & ZXS_VIEW ? ",\"view\":true" : ",\"view\":false");
    puts_(&o, r & ZXS_WRITE ? ",\"write\":true" : ",\"write\":false");
    puts_(&o, ",\"public\":");
    puts_(&o, is_public(s) ? "true" : "false");
    puts_(&o, ",\"members\":[");
    for (uint32_t i = 0; i < s->n; i++) {
        puts_(&o, i ? ",{\"who\":" : "{\"who\":");
        str(&o, s->m[i].who);
        puts_(&o, ",\"role\":");
        str(&o, zxs_role_name((zxs_role_t) s->m[i].role));
        puts_(&o, "}");
    }
    puts_(&o, "],\"governance\":");
    str(&o, s->gov == ZXS_GOV_HORIZONTAL ? "horizontal" : "vertical");
    char num[16];
    snprintf(num, sizeof num, "%u", (unsigned) s->quorum_pct);
    puts_(&o, ",\"quorum_pct\":");
    puts_(&o, num);
    puts_(&o, ",\"self\":");
    str(&o, s->self);
    puts_(&o, ",\"proposals\":[");
    for (uint32_t i = 0; i < s->np; i++) {
        const zxs_proposal_t *p = &s->p[i];
        uint32_t yes, no, voters;
        zxs_decision_t d = zxs_tally(s, p, &yes, &no, &voters);
        snprintf(num, sizeof num, "%u", (unsigned) p->id);
        puts_(&o, i ? ",{\"id\":" : "{\"id\":");
        puts_(&o, num);
        puts_(&o, ",\"by\":");
        str(&o, p->by);
        puts_(&o, ",\"members\":[");
        for (uint32_t j = 0; j < p->n; j++) {
            puts_(&o, j ? ",{\"who\":" : "{\"who\":");
            str(&o, p->m[j].who);
            puts_(&o, ",\"role\":");
            str(&o, zxs_role_name((zxs_role_t) p->m[j].role));
            puts_(&o, "}");
        }
        puts_(&o, "],\"votes\":[");
        for (uint32_t j = 0; j < p->nv; j++) {
            puts_(&o, j ? ",{\"who\":" : "{\"who\":");
            str(&o, p->v[j].who);
            puts_(&o, p->v[j].yes ? ",\"yes\":true}" : ",\"yes\":false}");
        }
        char tail[96];
        snprintf(tail, sizeof tail, "],\"yes\":%u,\"no\":%u,\"voters\":%u,\"state\":\"%s\"}",
                 (unsigned) yes, (unsigned) no, (unsigned) voters, zxs_decision_name(d));
        puts_(&o, tail);
    }
    snprintf(num, sizeof num, "%u", (unsigned) s->next_proposal);
    puts_(&o, "],\"next_proposal\":");
    puts_(&o, num);
    puts_(&o, "}");
    if (o.over) return -1;
    out[o.n] = 0;
    return (int32_t) o.n;
}

static bool parse_members(const char *js, const w4_jtok_t *t, int32_t k, int32_t arr,
                          zxs_member_t *m, uint32_t *n)
{
    char role[16];
    *n = 0;
    if (arr < 0 || t[arr].type != W4_J_ARR || t[arr].size > ZXS_MEMBERS_MAX) return false;
    for (uint32_t i = 0; i < t[arr].size; i++) {
        int32_t e = w4_json_at(t, k, arr, i);
        zxs_member_t *x = &m[*n];
        if (e < 0 || t[e].type != W4_J_OBJ ||
            w4_json_get_str(js, t, k, e, "who", x->who, sizeof x->who) < 0 ||
            w4_json_get_str(js, t, k, e, "role", role, sizeof role) < 0)
            return false;
        int r = zxs_role_parse(role);
        if (!zxs_who_ok(x->who) || (r != ZXS_MEMBER && r != ZXS_GUEST)) return false;
        x->role = (uint8_t) r;
        (*n)++;
    }
    return true;
}

/* S4 fields; absent ones take their defaults (vertical, 51, no proposals) */
static int parse_gov(const char *js, const w4_jtok_t *t, int32_t k, zxs_space_t *s)
{
    char gov[16] = "vertical";
    uint64_t v;
    int32_t i = w4_json_get(js, t, k, 0, "governance");
    if (i >= 0 && w4_json_get_str(js, t, k, 0, "governance", gov, sizeof gov) < 0) return ZXF_E_IO;
    if (!strcmp(gov, "horizontal"))
        s->gov = ZXS_GOV_HORIZONTAL;
    else if (strcmp(gov, "vertical"))
        return ZXF_E_IO;
    s->quorum_pct = 51;
    if ((i = w4_json_get(js, t, k, 0, "quorum_pct")) >= 0) {
        if (w4_json_u64(js, &t[i], &v) != W4_OK || v < 1 || v > 100) return ZXF_E_IO;
        s->quorum_pct = (uint8_t) v;
    }
    if (s->role == ZXS_OWNER) snprintf(s->self, sizeof s->self, ZXS_ME);
    if ((i = w4_json_get(js, t, k, 0, "self")) >= 0) {
        if (w4_json_get_str(js, t, k, 0, "self", s->self, sizeof s->self) < 0) return ZXF_E_IO;
        if (s->role == ZXS_OWNER ? strcmp(s->self, ZXS_ME) != 0
                                 : s->self[0] && (!zxs_who_ok(s->self) || !strcmp(s->self, ZXS_ME)))
            return ZXF_E_IO;
    }
    if ((i = w4_json_get(js, t, k, 0, "next_proposal")) >= 0) {
        if (w4_json_u64(js, &t[i], &v) != W4_OK || v > UINT32_MAX) return ZXF_E_IO;
        s->next_proposal = (uint32_t) v;
    }
    int32_t arr = w4_json_get(js, t, k, 0, "proposals");
    if (arr < 0) return ZXF_OK;
    if (t[arr].type != W4_J_ARR || t[arr].size > ZXS_PROPOSALS_MAX) return ZXF_E_IO;
    for (uint32_t j = 0; j < t[arr].size; j++) {
        int32_t o = w4_json_at(t, k, arr, j), x;
        zxs_proposal_t *p = &s->p[s->np];
        if (o < 0 || t[o].type != W4_J_OBJ || (x = w4_json_get(js, t, k, o, "id")) < 0 ||
            w4_json_u64(js, &t[x], &v) != W4_OK || v == 0 || v > s->next_proposal ||
            w4_json_get_str(js, t, k, o, "by", p->by, sizeof p->by) < 0 || !zxs_who_ok(p->by) ||
            !parse_members(js, t, k, w4_json_get(js, t, k, o, "members"), p->m, &p->n))
            return ZXF_E_IO;
        p->id = (uint32_t) v;
        for (uint32_t q = 0; q < s->np; q++)
            if (s->p[q].id == p->id) return ZXF_E_IO;
        int32_t va = w4_json_get(js, t, k, o, "votes");
        if (va < 0 || t[va].type != W4_J_ARR || t[va].size > ZXS_VOTERS_MAX) return ZXF_E_IO;
        for (uint32_t q = 0; q < t[va].size; q++) {
            int32_t e = w4_json_at(t, k, va, q), y;
            zxs_vote_t *w = &p->v[p->nv];
            if (e < 0 || t[e].type != W4_J_OBJ ||
                w4_json_get_str(js, t, k, e, "who", w->who, sizeof w->who) < 0 ||
                !zxs_who_ok(w->who) || (y = w4_json_get(js, t, k, e, "yes")) < 0 ||
                w4_json_bool(&t[y], &w->yes) != W4_OK)
                return ZXF_E_IO;
            for (uint32_t z = 0; z < p->nv; z++)
                if (!strcmp(p->v[z].who, w->who)) return ZXF_E_IO; /* one vote each */
            p->nv++;
        }
        s->np++;
    }
    return ZXF_OK;
}

int zxs_parse(const char *js, uint32_t n, zxs_space_t *s)
{
    static w4_jtok_t t[64 + 6 * ZXS_MEMBERS_MAX + ZXS_PROPOSALS_MAX * 12 * ZXS_VOTERS_MAX];
    const uint32_t max = sizeof t / sizeof *t;
    char kind[16], role[16];
    memset(s, 0, sizeof *s);
    int32_t k = w4_json_parse(js, n, t, max, 6);
    if (k <= 0 || t[0].type != W4_J_OBJ) return ZXF_E_IO;
    if (w4_json_get_str(js, t, k, 0, "id", s->id, sizeof s->id) < 0 ||
        w4_json_get_str(js, t, k, 0, "kind", kind, sizeof kind) < 0 ||
        w4_json_get_str(js, t, k, 0, "name", s->name, sizeof s->name) < 0 ||
        w4_json_get_str(js, t, k, 0, "owner", s->owner, sizeof s->owner) < 0 ||
        w4_json_get_str(js, t, k, 0, "role", role, sizeof role) < 0)
        return ZXF_E_IO;
    int kk = zxs_kind_parse(kind), rr = zxs_role_parse(role);
    if (kk <= ZXS_HOME || rr < 0 || rr == ZXS_PUBLIC || !zxf_space_id_ok(s->id) ||
        !name_ok(s->name) || !zxs_who_ok(s->owner) ||
        (rr == ZXS_OWNER) != !strcmp(s->owner, ZXS_ME))
        return ZXF_E_IO;
    s->kind = (uint8_t) kk;
    s->role = (uint8_t) rr;
    if (!parse_members(js, t, k, w4_json_get(js, t, k, 0, "members"), s->m, &s->n)) return ZXF_E_IO;
    return parse_gov(js, t, k, s);
}

/* ===================== S4 governance ===================== */

zxs_decision_t zxs_decide(zxs_gov_t gov, uint8_t quorum_pct, zxs_role_t proposer, uint32_t voters,
                          uint32_t yes, uint32_t no)
{
    if (proposer != ZXS_OWNER && proposer != ZXS_MEMBER) return ZXS_D_FALSE;
    if (gov == ZXS_GOV_VERTICAL) return proposer == ZXS_OWNER ? ZXS_D_TRUE : ZXS_D_NEITHER;
    if (voters == 0) return ZXS_D_FALSE;
    if (yes > voters) yes = voters;
    if (no > voters - yes) no = voters - yes;
    uint64_t q = quorum_pct ? (quorum_pct > 100 ? 100 : quorum_pct) : 51;
    bool reached = (uint64_t) yes * 100u >= q * voters;
    if (reached && no == 0) return ZXS_D_TRUE;
    if (reached) return ZXS_D_BOTH;
    if ((uint64_t) (voters - no) * 100u < q * voters) return ZXS_D_FALSE; /* cannot pass */
    return ZXS_D_NEITHER;
}

const char *zxs_decision_name(zxs_decision_t d)
{
    static const char *const N[] = {"false", "true", "neither", "both"};
    return (unsigned) d < 4 ? N[d] : "?";
}

/* the role `who` holds in s: owner, member, guest or public */
static zxs_role_t role_of(const zxs_space_t *s, const char *who)
{
    if (!strcmp(who, s->owner)) return ZXS_OWNER;
    if (s->self[0] && !strcmp(who, s->self)) return (zxs_role_t) s->role;
    for (uint32_t i = 0; i < s->n; i++)
        if (!strcmp(s->m[i].who, who)) return (zxs_role_t) s->m[i].role;
    return ZXS_PUBLIC;
}

static bool votes_ok(const zxs_space_t *s, const char *who)
{
    zxs_role_t r = role_of(s, who);
    return r == ZXS_OWNER || r == ZXS_MEMBER;
}

zxs_decision_t zxs_tally(const zxs_space_t *s, const zxs_proposal_t *p, uint32_t *yes, uint32_t *no,
                         uint32_t *voters)
{
    uint32_t v = 1; /* the owner */
    for (uint32_t i = 0; i < s->n; i++)
        if (s->m[i].role == ZXS_MEMBER) v++;
    if (s->self[0] && s->role == ZXS_MEMBER && strcmp(s->self, s->owner)) {
        bool listed = false;
        for (uint32_t i = 0; i < s->n; i++) listed |= !strcmp(s->m[i].who, s->self);
        if (!listed) v++; /* this node, when its own list leaves it out */
    }
    *yes = *no = 0;
    bool owner_yes = false, owner_no = false;
    for (uint32_t i = 0; i < p->nv; i++) {
        if (!votes_ok(s, p->v[i].who)) continue; /* a voter no longer in the space */
        if (p->v[i].yes)
            (*yes)++;
        else
            (*no)++;
        if (!strcmp(p->v[i].who, s->owner)) {
            if (p->v[i].yes)
                owner_yes = true;
            else
                owner_no = true;
        }
    }
    *voters = v;
    if (s->gov == ZXS_GOV_VERTICAL) /* the owner's vote is the decision */
        return owner_yes ? ZXS_D_TRUE : owner_no ? ZXS_D_FALSE : ZXS_D_NEITHER;
    return zxs_decide(ZXS_GOV_HORIZONTAL, s->quorum_pct, role_of(s, p->by), v, *yes, *no);
}

static void drop(zxs_space_t *s, uint32_t i)
{
    memmove(&s->p[i], &s->p[i + 1], (s->np - i - 1) * sizeof *s->p);
    s->np--;
}

/* apply or drop proposal i by its tally; true when it was decided */
static bool decide_one(zxs_space_t *s, uint32_t i, zxs_decision_t *d)
{
    uint32_t yes, no, voters;
    *d = zxs_tally(s, &s->p[i], &yes, &no, &voters);
    if (*d == ZXS_D_TRUE) {
        memcpy(s->m, s->p[i].m, sizeof s->m);
        s->n = s->p[i].n;
    }
    if (*d != ZXS_D_TRUE && *d != ZXS_D_FALSE) return false;
    drop(s, i);
    return true;
}

/* apply or drop proposal i after a change; *d its outcome. A new member
 * list changes who votes on the others, so they are counted again. */
static void settle(zxs_space_t *s, uint32_t i, zxs_decision_t *d)
{
    if (!decide_one(s, i, d) || *d != ZXS_D_TRUE) return;
    for (bool again = true; again;) {
        zxs_decision_t e;
        again = false;
        for (uint32_t j = 0; j < s->np && !again; j++) again = decide_one(s, j, &e);
    }
}

/* an id into a field of the same space (the source may be inside it) */
static void put_who(char dst[ZXS_WHO_MAX + 1], const char *src)
{
    size_t n = 0;
    while (n < ZXS_WHO_MAX && src[n]) n++;
    memmove(dst, src, n);
    dst[n] = 0;
}

static bool list_ok(const zxs_space_t *s, const zxs_member_t *m, uint32_t n)
{
    if (n > ZXS_MEMBERS_MAX) return false;
    for (uint32_t i = 0; i < n; i++) {
        if (!zxs_who_ok(m[i].who) || !strcmp(m[i].who, ZXS_ME) || !strcmp(m[i].who, s->owner) ||
            (m[i].role != ZXS_MEMBER && m[i].role != ZXS_GUEST))
            return false;
        for (uint32_t j = 0; j < i; j++)
            if (!strcmp(m[i].who, m[j].who)) return false;
    }
    return true;
}

int zxs_propose(zxs_space_t *s, const zxs_member_t *m, uint32_t n, uint32_t *id, zxs_decision_t *d)
{
    *id = 0;
    *d = ZXS_D_FALSE;
    if (s->kind == ZXS_HOME || (s->role != ZXS_OWNER && s->role != ZXS_MEMBER)) return ZXS_E_ROLE;
    if (!s->self[0]) return ZXS_E_SELF;
    if (!list_ok(s, m, n)) return ZXF_E_PATH;
    if (s->np == ZXS_PROPOSALS_MAX) return ZXS_E_FULL;
    zxs_proposal_t *p = &s->p[s->np];
    memset(p, 0, sizeof *p);
    p->id = ++s->next_proposal;
    put_who(p->by, s->role == ZXS_OWNER ? s->owner : s->self);
    memcpy(p->m, m, n * sizeof *m);
    p->n = n;
    put_who(p->v[0].who, p->by);
    p->v[0].yes = true;
    p->nv = 1;
    s->np++;
    *id = p->id;
    settle(s, s->np - 1, d);
    if (*d == ZXS_D_TRUE || *d == ZXS_D_FALSE) *id = 0;
    return ZXF_OK;
}

int zxs_vote_from(zxs_space_t *s, uint32_t id, const char *who, bool yes, zxs_decision_t *d)
{
    *d = ZXS_D_NEITHER;
    uint32_t i = 0;
    while (i < s->np && s->p[i].id != id) i++;
    if (i == s->np) return ZXS_E_NOPROP;
    if (!votes_ok(s, who)) return ZXS_E_ROLE;
    zxs_proposal_t *p = &s->p[i];
    uint32_t k = 0;
    while (k < p->nv && strcmp(p->v[k].who, who)) k++;
    if (k == p->nv) {
        if (p->nv == ZXS_VOTERS_MAX) return ZXS_E_FULL;
        put_who(p->v[k].who, who);
        p->nv++;
    }
    p->v[k].yes = yes;
    settle(s, i, d);
    return ZXF_OK;
}

int zxs_vote(zxs_space_t *s, uint32_t id, bool yes, zxs_decision_t *d)
{
    *d = ZXS_D_NEITHER;
    if (s->kind == ZXS_HOME || (s->role != ZXS_OWNER && s->role != ZXS_MEMBER)) return ZXS_E_ROLE;
    if (!s->self[0]) return ZXS_E_SELF;
    return zxs_vote_from(s, id, s->role == ZXS_OWNER ? s->owner : s->self, yes, d);
}

const char *zxs_strerror(int code)
{
    switch (code) {
    case ZXS_E_ROLE:
        return "your role in this space may not do that";
    case ZXS_E_SELF:
        return "this node has no member id in this space";
    case ZXS_E_FULL:
        return "too many open proposals or votes";
    case ZXS_E_NOPROP:
        return "no open proposal with that id";
    case ZXS_E_GOV:
        return "this space decides its members by vote";
    default:
        return zxf_strerror(code);
    }
}

/* ===================== the store ===================== */

void zxs_home(zxs_space_t *s)
{
    memset(s, 0, sizeof *s);
    snprintf(s->id, sizeof s->id, "home");
    s->kind = ZXS_HOME;
    snprintf(s->name, sizeof s->name, "Home");
    snprintf(s->owner, sizeof s->owner, ZXS_ME);
    s->role = ZXS_OWNER;
    snprintf(s->self, sizeof s->self, ZXS_ME);
    s->quorum_pct = 51;
}

int zxs_load(const char *id, zxs_space_t *s)
{
    static char buf[ZXS_JSON_MAX];
    uint32_t n;
    if (!strcmp(id, "home")) {
        zxs_home(s);
        return ZXF_OK;
    }
    int rc = zxf_meta_read(id, buf, sizeof buf, &n);
    if (rc) return rc;
    if ((rc = zxs_parse(buf, n, s)) != 0) return rc;
    return strcmp(s->id, id) ? ZXF_E_IO : ZXF_OK; /* a manifest under another name */
}

int zxs_save(const zxs_space_t *s, bool exclusive)
{
    static char buf[ZXS_JSON_MAX];
    int32_t n = zxs_json(s, buf, sizeof buf);
    if (n < 0) return ZXF_E_TOOBIG;
    return zxf_meta_write(s->id, buf, (uint32_t) n, exclusive);
}

static int check_new(zxs_kind_t kind, const char *name)
{
    if (kind <= ZXS_HOME || kind >= ZXS_KINDS) return ZXF_E_PATH;
    return name_ok(name) ? ZXF_OK : ZXF_E_PATH;
}

int zxs_create(zxs_kind_t kind, const char *name, zxs_gov_t gov, uint8_t quorum_pct,
               zxs_space_t *out)
{
    int rc = check_new(kind, name);
    if (rc || gov > ZXS_GOV_HORIZONTAL || quorum_pct < 1 || quorum_pct > 100)
        return rc ? rc : ZXF_E_PATH;
    if (rc) return rc;
    char slug[ZXF_SPACE_ID_MAX + 1];
    size_t n = 0;
    bool dash = false;
    for (const char *p = name; *p && n < ZXF_SPACE_ID_MAX - 4; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            if (dash && n) slug[n++] = '-';
            slug[n++] = c;
            dash = false;
        } else {
            dash = true;
        }
    }
    slug[n] = 0;
    if (!n || !strcmp(slug, "home")) snprintf(slug, sizeof slug, "%s", zxs_kind_name(kind));
    memset(out, 0, sizeof *out);
    out->kind = (uint8_t) kind;
    snprintf(out->name, sizeof out->name, "%s", name);
    snprintf(out->owner, sizeof out->owner, ZXS_ME);
    out->role = ZXS_OWNER;
    snprintf(out->self, sizeof out->self, ZXS_ME);
    out->gov = (uint8_t) gov;
    out->quorum_pct = quorum_pct;
    for (unsigned i = 1; i <= 999; i++) {
        /* slug is at most ZXF_SPACE_ID_MAX - 4 bytes: room for "-999" */
        size_t k = strlen(slug);
        memcpy(out->id, slug, k);
        if (i > 1) {
            out->id[k++] = '-';
            if (i >= 100) out->id[k++] = (char) ('0' + i / 100);
            if (i >= 10) out->id[k++] = (char) ('0' + i / 10 % 10);
            out->id[k++] = (char) ('0' + i % 10);
        }
        out->id[k] = 0;
        rc = zxs_save(out, true);
        if (rc != ZXF_E_EXISTS) return rc;
    }
    return ZXF_E_EXISTS;
}

int zxs_join(const char *id, zxs_kind_t kind, const char *name, const char *owner, zxs_role_t role,
             const char *self, zxs_gov_t gov, uint8_t quorum_pct, zxs_space_t *out)
{
    int rc = check_new(kind, name);
    if (rc) return rc;
    if (!zxf_space_id_ok(id) || !zxs_who_ok(owner) || !strcmp(owner, ZXS_ME) ||
        (role != ZXS_MEMBER && role != ZXS_GUEST) || gov > ZXS_GOV_HORIZONTAL || quorum_pct < 1 ||
        quorum_pct > 100 ||
        (self[0] && (!zxs_who_ok(self) || !strcmp(self, ZXS_ME) || !strcmp(self, owner))))
        return ZXF_E_PATH;
    memset(out, 0, sizeof *out);
    snprintf(out->id, sizeof out->id, "%s", id);
    out->kind = (uint8_t) kind;
    snprintf(out->name, sizeof out->name, "%s", name);
    snprintf(out->owner, sizeof out->owner, "%s", owner);
    out->role = (uint8_t) role;
    snprintf(out->self, sizeof out->self, "%s", self);
    out->gov = (uint8_t) gov;
    out->quorum_pct = quorum_pct;
    return zxs_save(out, true);
}

typedef struct {
    char (*ids)[ZXF_SPACE_ID_MAX + 1];
    uint32_t n, cap;
} ids_t;

static int collect(void *ctx, const char *id)
{
    ids_t *c = ctx;
    if (c->n == c->cap) {
        uint32_t nc = c->cap ? c->cap * 2 : 16;
        void *nv = realloc(c->ids, nc * sizeof *c->ids);
        if (!nv) return ZXF_E_IO;
        c->ids = nv;
        c->cap = nc;
    }
    snprintf(c->ids[c->n++], sizeof *c->ids, "%s", id);
    return 0;
}

static int by_id(const void *a, const void *b)
{
    return strcmp(a, b);
}

int zxs_each(zxs_each_fn fn, void *ctx)
{
    static zxs_space_t s;
    zxs_home(&s);
    int rc = fn(ctx, &s);
    if (rc) return rc;
    ids_t c = {NULL, 0, 0};
    rc = zxf_meta_each(collect, &c);
    if (!rc && c.n) qsort(c.ids, c.n, sizeof *c.ids, by_id);
    for (uint32_t i = 0; !rc && i < c.n; i++)
        if (zxs_load(c.ids[i], &s) == ZXF_OK) rc = fn(ctx, &s); /* a broken manifest is skipped */
    free(c.ids);
    return rc;
}
