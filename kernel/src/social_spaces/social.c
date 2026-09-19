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

int32_t soc_report(soc_world_t *w, uint32_t reporter, uint32_t subject,
                   uint32_t duration) {
    /* A FIRST-PARTY boundary report: `reporter` — the authenticated caller (the OS
     * session binds who is calling) — states that `subject` crossed a boundary
     * against THEM. This is the ONLY thing that slides standing toward quarantine;
     * the platform NEVER reports on anyone's behalf. Distinct reporters accumulate
     * strikes (concord de-dupes a repeat by the same reporter), so quarantine
     * takes real consensus, never one person. */
    if (!w) return SOC_ERR_NULL;
    con_commons_t *c = &w->commons;
    if (reporter == subject) return SOC_ERR_NOT_MEMBER;
    if (!con_get(c, reporter) || !con_get(c, subject)) return SOC_ERR_NOTFOUND;
    return con_report_boundary(c, reporter, subject, duration) ? SOC_OK
                                                               : SOC_ERR_NOTFOUND;
}

int32_t soc_sort(soc_world_t *w, uint32_t troublemaker) {
    if (!w) return SOC_ERR_NULL;
    con_commons_t *c = &w->commons;
    con_person_t *troll = con_get(c, troublemaker);
    if (!troll) return SOC_ERR_NOTFOUND;

    /* soc_sort does NOT impose quarantine and NEVER reports on anyone's behalf —
     * synthesising peer reports would be a covert strike, the platform
     * manufacturing consensus against a target who was never actually reported.
     * Quarantine is earned ONLY through real, first-party soc_report() calls by
     * peers who genuinely experienced the crossing (and, per concord, only
     * DISTINCT reporters accumulate strikes). soc_sort's sole job is to SEAT an
     * ALREADY-quarantined troll with fellow quarantined — the honest "hecklers
     * together" grouping. A target not yet quarantined by real reports is left
     * untouched (a covert, one-sided quiet-down is exactly what we refuse). */
    if (con_standing(troll) != CON_QUARANTINED)
        return SOC_ERR_NOT_QUARANTINED;

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

/* ---- DECLARATION -----------------------------------------------------------

 * ROOTING THE CALLER, NOT THE LEAF. social.o was measured at linked=0,
 * dropped=9, with 14 con_* call sites. concord is the leaf; social is the
 * caller. soc_init calls con_init, soc_join calls con_join, soc_report calls
 * con_report_boundary -- so this bring-up pulls the whole commons in behind
 * it, and rooting concord instead would have produced a matching engine that
 * nothing asks a question of.
 *
 * REQUIRES(concord_ready) is the entire measured boundary: social.o's `nm -u`
 * is six con_* symbols and nothing else.
 */
#include "zxv_decl.h"
static int zxvd_social_bringup(void) {
    static soc_world_t w;
    int32_t space;
    soc_init(&w);
    space = soc_open_space(&w, SOC_SOCIAL_CLUB, "zxv");
    if (space < 0) return -1;
    if (soc_join(&w, space, 1u) < 0) return -1;
    if (!soc_is_member(&w, space, 1u)) return -1;
    if (soc_is_member(&w, space, 2u))  return -1;   /* membership must bind */
    return 0;
}

ZXV_DECLARE(social,
    ZXV_PROVIDES(social_spaces_ready),
    ZXV_REQUIRES(concord_ready),
    ZXV_BRINGUP(zxvd_social_bringup));
