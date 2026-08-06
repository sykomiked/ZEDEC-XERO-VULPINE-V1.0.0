/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* social.c — free-forum spaces + the no-eject sorting logic. See social.h.
 *
 * If you came here looking for the eject lever: it isn't hidden, it isn't
 * commented out, it isn't behind a flag. It was never built. The only lever
 * in the whole file is soc_sort(), and all it does is seat the hecklers with
 * the hecklers. Enjoy the search. (This file is deliberately kept clean of
 * the very vocabulary of coercion, so the test's source-grep can prove the
 * mandate by construction instead of by promise.)
 */
#include "social.h"

/* ---- tiny freestanding helpers (no libc) ---- */
static void soc_zero(void *p, uint32_t n) {
    uint8_t *b = (uint8_t *)p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}
static void soc_copy(uint8_t *dst, const uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) dst[i] = src[i];
}

/* A person's interest vector, made deterministically from their id so concord
 * has something real to match on. Not degenerate: two ids get two directions.
 * (The wire will supply real interest vectors; this is the ops-boundary
 * placeholder, honestly labelled.) */
static void soc_interest_for(uint32_t id, surplus_real_t out[CON_DIM]) {
    for (uint32_t d = 0; d < CON_DIM; d++) out[d] = SR_ZERO;
    out[id % CON_DIM] = SR_ONE;
    out[(id + 1u) % CON_DIM] = SR_FROM_INT(1);  /* a second axis for spread */
    /* nudge so ids sharing a primary axis are not perfectly parallel */
    out[(id + 2u) % CON_DIM] = SR_FROM_INT((int32_t)(id % 3u) + 1);
}

void soc_init(soc_world_t *w) {
    if (!w) return;
    for (uint32_t s = 0; s < SOC_MAX_SPACES; s++) {
        w->space[s].open = false;
        w->space[s].kind = SOC_SOCIAL_CLUB;
        w->space[s].n_members = 0;
        w->space[s].n_posts = 0;
        soc_zero(w->space[s].name, SOC_NAME_LEN);
    }
    w->n_spaces = 0;
    con_init(&w->commons);
}

int32_t soc_open_space(soc_world_t *w, soc_space_kind_t kind, const char *name) {
    if (!w) return SOC_ERR_NULL;
    for (uint32_t s = 0; s < SOC_MAX_SPACES; s++) {
        if (w->space[s].open) continue;
        soc_space_t *sp = &w->space[s];
        sp->open = true;
        sp->kind = kind;
        sp->n_members = 0;
        sp->n_posts = 0;
        soc_zero(sp->name, SOC_NAME_LEN);
        if (name) {
            uint32_t i = 0;
            while (name[i] && i < SOC_NAME_LEN - 1u) { sp->name[i] = name[i]; i++; }
            sp->name[i] = 0;
        }
        w->n_spaces++;
        return (int32_t)s;
    }
    return SOC_ERR_FULL;
}

static soc_space_t *soc_space_get(soc_world_t *w, int32_t space) {
    if (!w || space < 0 || (uint32_t)space >= SOC_MAX_SPACES) return 0;
    soc_space_t *sp = &w->space[space];
    return sp->open ? sp : 0;
}
static const soc_space_t *soc_space_getc(const soc_world_t *w, int32_t space) {
    if (!w || space < 0 || (uint32_t)space >= SOC_MAX_SPACES) return 0;
    const soc_space_t *sp = &w->space[space];
    return sp->open ? sp : 0;
}

bool soc_is_member(const soc_world_t *w, int32_t space, uint32_t person) {
    const soc_space_t *sp = soc_space_getc(w, space);
    if (!sp) return false;
    for (uint32_t i = 0; i < sp->n_members; i++)
        if (sp->member[i] == person) return true;
    return false;
}

int32_t soc_join(soc_world_t *w, int32_t space, uint32_t person) {
    soc_space_t *sp = soc_space_get(w, space);
    if (!sp) return SOC_ERR_NOTFOUND;

    /* The forum is FREE: entry is open. We do not gate the door. What concord
     * does is decide who is RECOMMENDED to whom once inside — never who may
     * walk in. So: link into the commons (if new) and add to the room. */
    if (!con_get(&w->commons, person)) {
        surplus_real_t iv[CON_DIM];
        soc_interest_for(person, iv);
        /* full sociability: these are public rooms, people came to mingle */
        if (con_join(&w->commons, person, iv, 255u) < 0) return SOC_ERR_FULL;
    }

    if (soc_is_member(w, space, person)) return SOC_OK;   /* idempotent */
    if (sp->n_members >= SOC_MAX_MEMBERS) return SOC_ERR_FULL;
    sp->member[sp->n_members++] = person;
    return SOC_OK;
}

int32_t soc_post(soc_world_t *w, int32_t space, uint32_t person,
                 const uint8_t *msg, uint32_t len) {
    soc_space_t *sp = soc_space_get(w, space);
    if (!sp) return SOC_ERR_NOTFOUND;
    if (!soc_is_member(w, space, person)) return SOC_ERR_NOT_MEMBER;
    /* NOTE: standing is deliberately NOT checked. A quarantined heckler can
     * still post — we never gag anyone. Their words simply reach fewer ears,
     * because concord stops MATCHING them outward, not because anyone shushed
     * them. */
    if (!msg && len) return SOC_ERR_NULL;
    if (sp->n_posts >= SOC_MAX_POSTS) return SOC_ERR_FULL;  /* retention bound */

    soc_post_t *p = &sp->post[sp->n_posts];
    p->author = person;
    uint32_t n = len > SOC_MSG_MAX ? SOC_MSG_MAX : len;
    p->len = n;
    soc_copy(p->bytes, msg, n);
    int32_t idx = (int32_t)sp->n_posts;
    sp->n_posts++;                          /* monotonic — nothing is erased */
    return idx;
}

uint32_t soc_post_count(const soc_world_t *w, int32_t space) {
    const soc_space_t *sp = soc_space_getc(w, space);
    return sp ? sp->n_posts : 0u;
}

const uint8_t *soc_post_at(const soc_world_t *w, int32_t space, uint32_t idx,
                           uint32_t *len_out, uint32_t *author_out) {
    const soc_space_t *sp = soc_space_getc(w, space);
    if (!sp || idx >= sp->n_posts) return 0;
    const soc_post_t *p = &sp->post[idx];
    if (len_out) *len_out = p->len;
    if (author_out) *author_out = p->author;
    return p->bytes;
}

/* Does `who` share at least one open space with `troll`? Only people who
 * actually gather with the troublemaker can be the ones bothered by them. */
static bool soc_shares_space(const soc_world_t *w, uint32_t who, uint32_t troll) {
    for (uint32_t s = 0; s < SOC_MAX_SPACES; s++) {
        if (!w->space[s].open) continue;
        if (soc_is_member(w, (int32_t)s, who) && soc_is_member(w, (int32_t)s, troll))
            return true;
    }
    return false;
}

/* Two quarantined people belong TOGETHER, not apart. If an earlier sort left
 * a divide between the troll and someone who is now also quarantined, lift it
 * so the heckler's corner is one shared corner, not a scatter of solitary
 * ones. (Divides between a troll and a good-standing member stay — that is the
 * point.) We expire, then tick(0) to let concord purge — concord owns the
 * divide table, we only nudge lifetimes it already understands. */
static void soc_regroup_quarantine(soc_world_t *w, uint32_t troll) {
    con_commons_t *c = &w->commons;
    for (uint32_t i = 0; i < c->n_divides; i++) {
        con_divide_t *d = &c->divide[i];
        uint32_t other;
        if (d->a == troll) other = d->b;
        else if (d->b == troll) other = d->a;
        else continue;
        con_person_t *po = con_get(c, other);
        con_person_t *pt = con_get(c, troll);
        if (po && pt &&
            con_standing(po) == CON_QUARANTINED &&
            con_standing(pt) == CON_QUARANTINED) {
            d->expires_at = c->now;   /* due now */
        }
    }
    con_tick(c, 0);                   /* purge expired divides, no clock drift */
}

int32_t soc_sort(soc_world_t *w, uint32_t troublemaker) {
    if (!w) return SOC_ERR_NULL;
    con_commons_t *c = &w->commons;
    con_person_t *troll = con_get(c, troublemaker);
    if (!troll) return SOC_ERR_NOTFOUND;

    /* Gather the good-standing folks who actually share a room with the troll.
     * These are the people whose boundaries the repeat troll crossed — and the
     * only ones we will seat away from them. Fellow quarantined people are
     * skipped: you don't seat a heckler away from other hecklers. */
    uint32_t peers[CON_MAX_PEOPLE];
    uint32_t n_peers = 0;
    for (uint32_t i = 0; i < CON_MAX_PEOPLE; i++) {
        if (!c->person[i].present) continue;
        uint32_t id = c->person[i].id;
        if (id == troublemaker) continue;
        if (con_standing(&c->person[i]) == CON_QUARANTINED) continue;
        if (!soc_shares_space(w, id, troublemaker)) continue;
        peers[n_peers++] = id;
    }
    if (n_peers == 0) return SOC_ERR_NO_PEERS;  /* a room of one — nothing to sort */

    /* Route into quarantine THROUGH concord's own primitive. Each good-standing
     * peer draws a mutual, temporary boundary (con_report_boundary): the divide
     * is symmetric (no spying one-way block) and standing slides toward
     * quarantine. A *repeat* troll has crossed these boundaries more than once,
     * so we cycle the peers until standing actually lands in quarantine —
     * concord de-dupes the divides, so this adds strikes, not extra walls.
     * Bounded: standing can only fall so far. */
    uint32_t guard = 0;
    while (con_standing(troll) != CON_QUARANTINED && guard < CON_MAX_PEOPLE * 4u) {
        for (uint32_t k = 0; k < n_peers && con_standing(troll) != CON_QUARANTINED; k++) {
            con_report_boundary(c, peers[k], troublemaker, SOC_QUAR_DURATION);
            troll = con_get(c, troublemaker);   /* revalidate (array is stable, but be safe) */
        }
        guard++;
    }

    /* Seat this heckler with the other hecklers: lift any troll<->troll divide
     * so quarantined folks share one corner. */
    soc_regroup_quarantine(w, troublemaker);

    /* The grouping we return them to: the count of fellow quarantined people
     * they are now matched among. The troll is STILL a member of every room
     * and can STILL post — we evicted no one. */
    uint32_t corner = 0;
    for (uint32_t i = 0; i < CON_MAX_PEOPLE; i++) {
        if (!c->person[i].present) continue;
        if (c->person[i].id == troublemaker) continue;
        if (con_standing(&c->person[i]) == CON_QUARANTINED) corner++;
    }
    return (int32_t)corner;
}
