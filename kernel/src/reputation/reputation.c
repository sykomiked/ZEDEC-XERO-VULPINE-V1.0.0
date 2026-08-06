/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* reputation.c — Pig Badge counterintelligence signal + earnable badges.
 *
 * They get the pig badge, and it goes to over nine thousand. They just never
 * see it themselves.
 */
#include "reputation.h"

/* ---- internal helpers ---- */

/* Find the row for `subject`, or NULL if it has none yet. */
static rep_entry_t *rep_find(rep_state_t *s, uint32_t subject)
{
    for (uint32_t i = 0; i < s->n_entries; i++)
        if (s->entry[i].used && s->entry[i].subject == subject)
            return &s->entry[i];
    return (rep_entry_t *)0;
}

static const rep_entry_t *rep_find_c(const rep_state_t *s, uint32_t subject)
{
    for (uint32_t i = 0; i < s->n_entries; i++)
        if (s->entry[i].used && s->entry[i].subject == subject)
            return &s->entry[i];
    return (const rep_entry_t *)0;
}

/* Find the row for `subject`, creating it if there is room. NULL if the table
 * is full — we fail closed rather than silently drop a confirmed occurrence. */
static rep_entry_t *rep_find_or_new(rep_state_t *s, uint32_t subject)
{
    rep_entry_t *e = rep_find(s, subject);
    if (e) return e;
    /* reuse any hole first (there are none today, but be honest about it) */
    for (uint32_t i = 0; i < s->n_entries; i++) {
        if (!s->entry[i].used) {
            e = &s->entry[i];
            break;
        }
    }
    if (!e) {
        if (s->n_entries >= REP_MAX_SUBJECTS) return (rep_entry_t *)0;
        e = &s->entry[s->n_entries++];
    }
    e->used     = true;
    e->subject  = subject;
    e->pig_level = 0u;
    e->n_badges = 0u;
    for (uint32_t b = 0; b < REP_MAX_BADGES_PER_SUB; b++) {
        e->badge[b].id = 0u;
        e->badge[b].level = 0u;
    }
    return e;
}

void rep_init(rep_state_t *s)
{
    s->n_entries = 0u;
    for (uint32_t i = 0; i < REP_MAX_SUBJECTS; i++) {
        s->entry[i].used     = false;
        s->entry[i].subject  = 0u;
        s->entry[i].pig_level = 0u;
        s->entry[i].n_badges = 0u;
        for (uint32_t b = 0; b < REP_MAX_BADGES_PER_SUB; b++) {
            s->entry[i].badge[b].id = 0u;
            s->entry[i].badge[b].level = 0u;
        }
    }
}

/* ---- the pig badge ---- */

int32_t pig_flag(rep_state_t *s, uint32_t subject)
{
    rep_entry_t *e = rep_find_or_new(s, subject);
    if (!e) return -1;                     /* table full: fail closed         */
    if (e->pig_level < REP_PIG_MAX)
        e->pig_level++;                    /* saturates at 9999               */
    return (int32_t)e->pig_level;
}

uint32_t pig_level_seen_by(const rep_state_t *s, uint32_t subject, uint32_t viewer)
{
    if (viewer == subject) return 0u;      /* you never see your OWN badge     */
    const rep_entry_t *e = rep_find_c(s, subject);
    return e ? e->pig_level : 0u;
}

void pig_badge_text(uint32_t level, char *out, uint32_t cap)
{
    static const char over[] = "over 9000";
    if (cap == 0u) return;

    if (level > REP_PIG_MAX) level = REP_PIG_MAX;   /* cap the stored level    */

    if (level > REP_PIG_OVER) {
        /* copy the literal, respecting cap (leave room for the terminator) */
        uint32_t i = 0;
        while (over[i] != '\0' && i + 1u < cap) { out[i] = over[i]; i++; }
        out[i] = '\0';
        return;
    }

    /* decimal render of `level`, no libc. Build digits back-to-front. */
    char tmp[12];
    uint32_t n = 0;
    uint32_t v = level;
    if (v == 0u) {
        tmp[n++] = '0';
    } else {
        while (v > 0u && n < sizeof tmp) { tmp[n++] = (char)('0' + (v % 10u)); v /= 10u; }
    }
    uint32_t o = 0;
    while (n > 0u && o + 1u < cap) { out[o++] = tmp[--n]; }
    out[o] = '\0';
}

/* ---- earnable badges ---- */

int32_t badge_award(rep_state_t *s, uint32_t subject, uint32_t badge_id, uint32_t level)
{
    rep_entry_t *e = rep_find_or_new(s, subject);
    if (!e) return -1;

    /* already hold this badge? raise it (monotonic — never demote). */
    for (uint32_t b = 0; b < e->n_badges; b++) {
        if (e->badge[b].id == badge_id) {
            if (level > e->badge[b].level) e->badge[b].level = level;
            return (int32_t)e->badge[b].level;
        }
    }
    /* new badge for this subject */
    if (e->n_badges >= REP_MAX_BADGES_PER_SUB) return -1;
    e->badge[e->n_badges].id = badge_id;
    e->badge[e->n_badges].level = level;
    e->n_badges++;
    return (int32_t)level;
}

bool badge_has(const rep_state_t *s, uint32_t subject, uint32_t badge_id, uint32_t min_level)
{
    const rep_entry_t *e = rep_find_c(s, subject);
    if (!e) return false;
    for (uint32_t b = 0; b < e->n_badges; b++)
        if (e->badge[b].id == badge_id)
            return e->badge[b].level >= min_level;
    return false;
}

uint32_t badge_score(const rep_state_t *s, uint32_t subject)
{
    const rep_entry_t *e = rep_find_c(s, subject);
    if (!e) return 0u;
    uint32_t score = 0u;
    for (uint32_t b = 0; b < e->n_badges; b++)
        score += e->badge[b].level;
    return score;
}
