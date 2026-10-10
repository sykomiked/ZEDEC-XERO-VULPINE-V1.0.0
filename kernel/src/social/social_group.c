/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* social_group.c — groups and syndicates as a replayed, signed log. See
 * social_group.h G1-G4. Freestanding: no libc, no allocation. */
#include "social_group.h"

/* ---------------- G1 record checks ---------------- */

static int32_t check_create(const uint8_t *b, uint32_t len)
{
    uint32_t o = 0;
    if (len < 6) return SP_ERR_RULE;
    if (b[o++] != 1) return SP_ERR_RULE;
    uint8_t kind = b[o++];
    if (kind > SG_KIND_SYNDICATE || b[o++] > SG_JOIN_OPEN) return SP_ERR_RULE;
    uint32_t nl = b[o++];
    if (nl == 0 || nl > SG_NAME_MAX || nl > len - o) return SP_ERR_RULE;
    o += nl;
    if (o >= len) return SP_ERR_RULE;
    uint32_t tl = b[o++];
    if (tl > SG_TREASURY_MAX || tl > len - o) return SP_ERR_RULE;
    o += tl;
    if (o >= len) return SP_ERR_RULE;
    uint32_t ol = b[o++];
    if (ol > SG_ORG_MAX || ol != len - o) return SP_ERR_RULE;
    if (kind == SG_KIND_SYNDICATE && ol == 0) return SP_ERR_RULE;
    return SP_OK;
}

int32_t sg_check_record(const sp_record_t *r)
{
    if (!r || r->n_media) return SP_ERR_RULE;
    const uint8_t *b = r->body;
    uint32_t len = r->body_len;
    if (r->kind == SP_KIND_GROUP_CREATE) {
        if (r->has_parent || r->has_audience || r->vis == SP_VIS_GROUP) return SP_ERR_RULE;
        return check_create(b, len);
    }
    if (!r->has_parent) return SP_ERR_RULE;
    switch (r->kind) {
    case SP_KIND_GROUP_INVITE:
        if (!r->has_audience || len != 1) return SP_ERR_RULE;
        return (b[0] >= SG_ROLE_OBSERVER && b[0] <= SG_ROLE_ADMIN) ? SP_OK : SP_ERR_RULE;
    case SP_KIND_GROUP_JOIN:
        if (r->has_audience || len < 1 || b[0] > SG_ORG_MAX || len != 1u + b[0]) return SP_ERR_RULE;
        return SP_OK;
    case SP_KIND_GROUP_LEAVE:
        return (r->has_audience && len == 0) ? SP_OK : SP_ERR_RULE;
    case SP_KIND_GROUP_ROLE:
        if (!r->has_audience || len != 1) return SP_ERR_RULE;
        return (b[0] >= SG_ROLE_OBSERVER && b[0] <= SG_ROLE_OWNER) ? SP_OK : SP_ERR_RULE;
    case SP_KIND_GROUP_CHARTER:
        if (r->has_audience || len != 1u + SG_HASH_LEN || b[0] > SG_CHARTER_SIGN)
            return SP_ERR_RULE;
        return SP_OK;
    default:
        return SP_ERR_RULE;
    }
}

/* ---------------- builders ---------------- */

int32_t sg_make_create(sp_record_t *r, sg_kind_t kind, sg_join_t join, const uint8_t *name,
                       uint32_t name_len, const uint8_t *treasury, uint32_t treasury_len,
                       const uint8_t *org, uint32_t org_len)
{
    if (!r || !name || (!treasury && treasury_len) || (!org && org_len)) return SP_ERR_ARG;
    if ((uint32_t) kind > SG_KIND_SYNDICATE || (uint32_t) join > SG_JOIN_OPEN || name_len == 0 ||
        name_len > SG_NAME_MAX || treasury_len > SG_TREASURY_MAX || org_len > SG_ORG_MAX ||
        (kind == SG_KIND_SYNDICATE && org_len == 0))
        return SP_ERR_RANGE;
    uint8_t *b = r->body;
    uint32_t o = 0;
    b[o++] = 1;
    b[o++] = (uint8_t) kind;
    b[o++] = (uint8_t) join;
    b[o++] = (uint8_t) name_len;
    sp_copy(b + o, name, name_len);
    o += name_len;
    b[o++] = (uint8_t) treasury_len;
    sp_copy(b + o, treasury, treasury_len);
    o += treasury_len;
    b[o++] = (uint8_t) org_len;
    sp_copy(b + o, org, org_len);
    o += org_len;
    r->body_len = (uint16_t) o;
    r->has_parent = r->has_audience = false;
    return SP_OK;
}

static int32_t one_byte(sp_record_t *r, const uint8_t *group, const uint8_t *subject, uint8_t v)
{
    if (!r || !group) return SP_ERR_ARG;
    sp_set_parent(r, group);
    if (subject) sp_set_audience(r, subject);
    r->body[0] = v;
    r->body_len = 1;
    return SP_OK;
}

int32_t sg_make_invite(sp_record_t *r, const uint8_t group[SP_ID_LEN],
                       const uint8_t invitee[SP_NODEID_LEN], sg_role_t role)
{
    if (!invitee) return SP_ERR_ARG;
    if (role < SG_ROLE_OBSERVER || role > SG_ROLE_ADMIN) return SP_ERR_RANGE;
    return one_byte(r, group, invitee, (uint8_t) role);
}

int32_t sg_make_join(sp_record_t *r, const uint8_t group[SP_ID_LEN], const uint8_t *org,
                     uint32_t org_len)
{
    if (!r || !group || (!org && org_len)) return SP_ERR_ARG;
    if (org_len > SG_ORG_MAX) return SP_ERR_RANGE;
    sp_set_parent(r, group);
    r->body[0] = (uint8_t) org_len;
    sp_copy(r->body + 1, org, org_len);
    r->body_len = (uint16_t) (1u + org_len);
    return SP_OK;
}

int32_t sg_make_leave(sp_record_t *r, const uint8_t group[SP_ID_LEN],
                      const uint8_t subject[SP_NODEID_LEN])
{
    if (!r || !group || !subject) return SP_ERR_ARG;
    sp_set_parent(r, group);
    sp_set_audience(r, subject);
    r->body_len = 0;
    return SP_OK;
}

int32_t sg_make_role(sp_record_t *r, const uint8_t group[SP_ID_LEN],
                     const uint8_t subject[SP_NODEID_LEN], sg_role_t role)
{
    if (!subject) return SP_ERR_ARG;
    if (role < SG_ROLE_OBSERVER || role > SG_ROLE_OWNER) return SP_ERR_RANGE;
    return one_byte(r, group, subject, (uint8_t) role);
}

int32_t sg_make_charter(sp_record_t *r, const uint8_t group[SP_ID_LEN], sg_charter_op_t op,
                        const uint8_t hash[SG_HASH_LEN])
{
    if (!r || !group || !hash) return SP_ERR_ARG;
    if ((uint32_t) op > SG_CHARTER_SIGN) return SP_ERR_RANGE;
    sp_set_parent(r, group);
    r->body[0] = (uint8_t) op;
    sp_copy(r->body + 1, hash, SG_HASH_LEN);
    r->body_len = 1u + SG_HASH_LEN;
    return SP_OK;
}

/* ---------------- G2 replay ---------------- */

static bool is_group_kind(uint8_t k)
{
    return k >= SP_KIND_GROUP_CREATE && k <= SP_KIND_GROUP_CHARTER;
}

static bool belongs(const sp_record_t *r, const uint8_t *group)
{
    if (!r || !is_group_kind(r->kind) || sg_check_record(r) != SP_OK) return false;
    if (r->kind == SP_KIND_GROUP_CREATE) return sp_eq(r->id, group, SP_ID_LEN);
    return sp_eq(r->parent, group, SP_ID_LEN);
}

static bool before(const sp_record_t *a, const sp_record_t *b)
{
    if (a->lamport != b->lamport) return a->lamport < b->lamport;
    return sp_cmp(a->id, b->id, SP_ID_LEN) < 0;
}

typedef struct {
    sg_member_t *m;
    uint32_t cap;
    sg_group_t *g;
} roster_t;

static sg_member_t *entry(roster_t *rs, const uint8_t *node, bool create)
{
    for (uint32_t i = 0; i < rs->g->n_entries; i++)
        if (sp_eq(rs->m[i].node, node, SP_NODEID_LEN)) return &rs->m[i];
    if (!create) return 0;
    if (rs->g->n_entries >= rs->cap) {
        rs->g->truncated++;
        return 0;
    }
    sg_member_t *e = &rs->m[rs->g->n_entries++];
    sp_copy(e->node, node, SP_NODEID_LEN);
    e->role = SG_ROLE_NONE;
    e->invited = SG_ROLE_NONE;
    e->charter_signed = false;
    e->org_len = 0;
    e->since = 0;
    return e;
}

static uint8_t role_of(roster_t *rs, const uint8_t *node)
{
    sg_member_t *e = entry(rs, node, false);
    return e ? e->role : SG_ROLE_NONE;
}

static void set_role(sg_member_t *e, uint8_t role, uint64_t t)
{
    e->role = role;
    e->since = t;
    if (role == SG_ROLE_NONE) e->charter_signed = false;
}

/* Apply one record at its place in the order. True if it took effect. */
static bool apply(roster_t *rs, const sp_record_t *r)
{
    sg_group_t *g = rs->g;
    const uint8_t *b = r->body;
    if (r->kind == SP_KIND_GROUP_CREATE) {
        if (g->exists) return false;
        g->exists = true;
        g->kind = b[1];
        g->join = b[2];
        uint32_t o = 3;
        g->name_len = b[o++];
        sp_copy(g->name, b + o, g->name_len);
        o += g->name_len;
        g->treasury_len = b[o++];
        sp_copy(g->treasury, b + o, g->treasury_len);
        o += g->treasury_len;
        sg_member_t *e = entry(rs, r->author, true);
        if (!e) return false;
        e->org_len = b[o++];
        sp_copy(e->org, b + o, e->org_len);
        set_role(e, SG_ROLE_OWNER, r->lamport);
        sp_copy(g->owner, r->author, SP_NODEID_LEN);
        return true;
    }
    if (!g->exists) return false; /* before the group was founded */
    uint8_t actor = role_of(rs, r->author);
    switch (r->kind) {
    case SP_KIND_GROUP_INVITE: {
        uint8_t want = b[0];
        if (actor < SG_ROLE_ADMIN || (want == SG_ROLE_ADMIN && actor != SG_ROLE_OWNER))
            return false;
        sg_member_t *e = entry(rs, r->audience, true);
        if (!e || e->role != SG_ROLE_NONE) return false;
        e->invited = want;
        return true;
    }
    case SP_KIND_GROUP_JOIN: {
        if (g->kind == SG_KIND_SYNDICATE && b[0] == 0) return false;
        sg_member_t *e = entry(rs, r->author, g->join == SG_JOIN_OPEN);
        if (!e || e->role != SG_ROLE_NONE) return false;
        uint8_t role;
        if (e->invited != SG_ROLE_NONE) {
            role = e->invited;
            e->invited = SG_ROLE_NONE;
        } else if (g->join == SG_JOIN_OPEN) {
            role = SG_ROLE_MEMBER;
        } else {
            return false;
        }
        e->org_len = b[0];
        sp_copy(e->org, b + 1, e->org_len);
        set_role(e, role, r->lamport);
        return true;
    }
    case SP_KIND_GROUP_LEAVE: {
        sg_member_t *e = entry(rs, r->audience, false);
        if (!e || e->role == SG_ROLE_NONE || e->role == SG_ROLE_OWNER) return false;
        bool self = sp_eq(r->author, r->audience, SP_NODEID_LEN);
        if (!self && (actor < SG_ROLE_ADMIN || e->role >= actor)) return false;
        set_role(e, SG_ROLE_NONE, r->lamport);
        e->invited = SG_ROLE_NONE;
        return true;
    }
    case SP_KIND_GROUP_ROLE: {
        uint8_t want = b[0];
        if (sp_eq(r->author, r->audience, SP_NODEID_LEN)) return false;
        sg_member_t *e = entry(rs, r->audience, false);
        if (!e || e->role == SG_ROLE_NONE) return false;
        if (actor == SG_ROLE_OWNER) {
            if (want == SG_ROLE_OWNER) { /* transfer: the old owner becomes an admin */
                sg_member_t *me = entry(rs, r->author, false);
                if (!me) return false;
                set_role(me, SG_ROLE_ADMIN, r->lamport);
                sp_copy(g->owner, r->audience, SP_NODEID_LEN);
            }
        } else if (actor == SG_ROLE_ADMIN) {
            if (want >= SG_ROLE_ADMIN || e->role >= SG_ROLE_ADMIN) return false;
        } else {
            return false;
        }
        set_role(e, want, r->lamport);
        return true;
    }
    case SP_KIND_GROUP_CHARTER: {
        if (b[0] == SG_CHARTER_SET) {
            if (actor != SG_ROLE_OWNER) return false;
            sp_copy(g->charter, b + 1, SG_HASH_LEN);
            g->has_charter = true;
            for (uint32_t i = 0; i < g->n_entries; i++) rs->m[i].charter_signed = false;
            return true;
        }
        if (actor == SG_ROLE_NONE || !g->has_charter || !sp_eq(b + 1, g->charter, SG_HASH_LEN))
            return false;
        entry(rs, r->author, false)->charter_signed = true;
        return true;
    }
    default:
        return false;
    }
}

static void group_reset(sg_group_t *g, const uint8_t *group)
{
    uint8_t *p = (uint8_t *) g;
    for (uint32_t i = 0; i < (uint32_t) sizeof *g; i++) p[i] = 0;
    sp_copy(g->id, group, SP_ID_LEN);
}

static uint32_t replay(const sp_record_t **sorted, uint32_t n, sg_member_t *members, uint32_t cap,
                       sg_group_t *out)
{
    roster_t rs = {members, cap, out};
    for (uint32_t i = 0; i < n; i++) {
        if (apply(&rs, sorted[i]))
            out->applied++;
        else
            out->ignored++;
    }
    for (uint32_t i = 0; i < out->n_entries; i++)
        if (members[i].role != SG_ROLE_NONE) out->n_members++;
    return out->n_entries;
}

static void insert_sorted(const sp_record_t **sorted, uint32_t n, const sp_record_t *r)
{
    uint32_t j = n;
    while (j > 0 && before(r, sorted[j - 1])) {
        sorted[j] = sorted[j - 1];
        j--;
    }
    sorted[j] = r;
}

uint32_t sg_derive(const sp_record_t *const *recs, uint32_t n, const uint8_t group[SP_ID_LEN],
                   const sp_record_t **sorted, sg_member_t *members, uint32_t cap, sg_group_t *out)
{
    if (!out || !group) return 0;
    group_reset(out, group);
    if (!recs || !sorted || !members) return 0;
    uint32_t m = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (!belongs(recs[i], group)) continue;
        /* the same record twice (same id) is replayed once */
        bool dup = false;
        for (uint32_t j = 0; j < m && !dup; j++) dup = sp_eq(sorted[j]->id, recs[i]->id, SP_ID_LEN);
        if (!dup) insert_sorted(sorted, m++, recs[i]);
    }
    return replay(sorted, m, members, cap, out);
}

uint32_t sg_derive_store(const ss_store_t *s, const uint8_t group[SP_ID_LEN],
                         const sp_record_t **sorted, uint32_t sorted_cap, sg_member_t *members,
                         uint32_t cap, sg_group_t *out)
{
    if (!out || !group) return 0;
    group_reset(out, group);
    if (!s || !sorted || !members) return 0;
    uint32_t m = 0, over = 0;
    for (uint32_t i = 0; i < ss_count(s); i++) {
        const sp_record_t *r = ss_at(s, i);
        if (!belongs(r, group)) continue;
        if (m < sorted_cap)
            insert_sorted(sorted, m++, r); /* the store already dedupes by id */
        else
            over++;
    }
    uint32_t e = replay(sorted, m, members, cap, out);
    out->truncated += over;
    return e;
}

const sg_member_t *sg_find(const sg_member_t *members, uint32_t n,
                           const uint8_t node[SP_NODEID_LEN])
{
    if (!members || !node) return 0;
    for (uint32_t i = 0; i < n; i++)
        if (sp_eq(members[i].node, node, SP_NODEID_LEN)) return &members[i];
    return 0;
}

bool sg_is_member(const sg_member_t *members, uint32_t n, const uint8_t node[SP_NODEID_LEN])
{
    const sg_member_t *e = sg_find(members, n, node);
    return e && e->role != SG_ROLE_NONE;
}

uint32_t sg_charter_signers(const sg_member_t *members, uint32_t n)
{
    uint32_t c = 0;
    for (uint32_t i = 0; members && i < n; i++)
        if (members[i].role != SG_ROLE_NONE && members[i].charter_signed) c++;
    return c;
}
