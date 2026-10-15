/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* witness_model.c — Witness (see witness_model.h). */
#include <stdio.h>
#include <string.h>

#include "witness_model.h"

static const char *const LEVEL_NAME[] = {"info", "warn", "error"};

static int level_of(const char *s)
{
    for (int i = WITNESS_INFO; i <= WITNESS_ERROR; i++)
        if (!strcmp(s, LEVEL_NAME[i])) return i;
    return -1;
}

uint8_t witness_classify(uint8_t level, const char *source, const char *text)
{
    if (level == WITNESS_ERROR) return WITNESS_FAULT;
    if (level == WITNESS_WARN) return WITNESS_REFUSED;
    if (strcmp(source, "rpc")) return WITNESS_OTHER;
    if (!strncmp(text, "swarm.", 6)) return WITNESS_AI;
    const char *sp = strchr(text, ' ');
    if (sp && sp > text && !strncmp(sp, " done", 5)) return WITNESS_CHANGE;
    return WITNESS_OTHER;
}

void witness_init(witness_t *w, zxm_link_t *link)
{
    memset(w, 0, sizeof *w);
    w->link = link;
}

/* one entry of log.tail or journal.query (WM2) */
static int read_event(zxm_link_t *l, int32_t o, witness_entry_t *e)
{
    char lv[8];
    memset(e, 0, sizeof *e);
    if (o < 0 || zxm_get_u64(l, o, "seq", &e->index) || zxm_get_u64(l, o, "time_ms", &e->time_ms) ||
        zxm_get_str(l, o, "level", lv, sizeof lv) || level_of(lv) < 0 ||
        zxm_get_str(l, o, "source", e->source, sizeof e->source) ||
        zxm_get_str(l, o, "text", e->text, sizeof e->text) ||
        e->index > (uint64_t) WITNESS_SEQ_MAX || e->time_ms > (uint64_t) WITNESS_STAMP_MAX)
        return ZXM_E_PROTOCOL;
    e->level = (uint8_t) level_of(lv);
    e->kind = witness_classify(e->level, e->source, e->text);
    return ZXM_OK;
}

static int stamp(witness_t *g)
{
    zxm_link_t *l = g->link;
    zxm_begin(l, "sys.info");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_u64(l, l->result, "time_ms", &g->as_of_ms) ||
        zxm_get_str(l, l->result, "host", g->host, sizeof g->host))
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

/* the journal, newest page first, into g->e newest first */
static int from_journal(witness_t *g, bool *on)
{
    zxm_link_t *l = g->link;
    uint64_t before = 0;
    *on = false;
    while (g->n < WITNESS_KEEP) {
        zxm_begin(l, "journal.query");
        if (g->since_ms) zxm_p_u64(l, "since_ms", g->since_ms);
        if (g->until_ms) zxm_p_u64(l, "until_ms", g->until_ms);
        if (before) zxm_p_u64(l, "before", before);
        zxm_p_u64(l, "max", WITNESS_PAGE);
        zxm_p_str(l, "level", LEVEL_NAME[WITNESS_INFO]);
        int rc = zxm_call(l);
        if (rc) return rc;
        bool more;
        int32_t arr = zxm_get(l, l->result, "entries");
        if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_get_bool(l, l->result, "on", on) ||
            zxm_get_bool(l, l->result, "more", &more) || zxm_len(l, arr) > WITNESS_PAGE)
            return ZXM_E_PROTOCOL;
        uint32_t n = zxm_len(l, arr);
        if (!*on) return ZXM_OK;
        for (uint32_t i = 0; i < n; i++) {
            if (g->n == WITNESS_KEEP) {
                g->more = true;
                return ZXM_OK;
            }
            if (read_event(l, zxm_at(l, arr, i), &g->e[g->n])) return ZXM_E_PROTOCOL;
            g->n++;
        }
        /* the next page is older than the oldest shown; a page that does
         * not move back ends the walk (the chain shows what came) */
        uint64_t oldest = n ? g->e[g->n - 1].index : 0;
        if (!more || !n || (before && oldest >= before) || oldest == 0) return ZXM_OK;
        before = oldest;
    }
    g->more = true;
    return ZXM_OK;
}

/* zxvd's recent events, oldest first, the newest WITNESS_KEEP kept */
static int from_tail(witness_t *g)
{
    zxm_link_t *l = g->link;
    uint64_t since = 0;
    for (uint32_t round = 0; round < 8u; round++) { /* 8 x 256 covers zxvd's ring */
        zxm_begin(l, "log.tail");
        zxm_p_u64(l, "since", since);
        zxm_p_u64(l, "max", WITNESS_TAIL);
        int rc = zxm_call(l);
        if (rc) return rc;
        uint64_t next, dropped;
        int32_t arr = zxm_get(l, l->result, "entries");
        if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_get_u64(l, l->result, "next", &next) ||
            zxm_get_u64(l, l->result, "dropped", &dropped) || next < since ||
            zxm_len(l, arr) > WITNESS_TAIL)
            return ZXM_E_PROTOCOL;
        uint32_t n = zxm_len(l, arr);
        for (uint32_t i = 0; i < n; i++) {
            static witness_entry_t e;
            if (read_event(l, zxm_at(l, arr, i), &e)) return ZXM_E_PROTOCOL;
            if ((g->since_ms && e.time_ms < g->since_ms) ||
                (g->until_ms && e.time_ms > g->until_ms))
                continue;
            if (g->n == WITNESS_KEEP) { /* full: the oldest goes */
                memmove(&g->e[0], &g->e[1], (WITNESS_KEEP - 1u) * sizeof g->e[0]);
                g->n--;
                g->more = true;
            }
            g->e[g->n++] = e;
        }
        g->dropped += dropped;
        if (dropped) g->more = true; /* older events zxvd no longer holds */
        bool more = n == WITNESS_TAIL && next > since;
        since = next;
        if (!more) break;
    }
    return ZXM_OK;
}

int witness_gather(witness_t *w, uint64_t since_ms, uint64_t until_ms)
{
    if (until_ms && since_ms > until_ms) return ZXM_E_ARG;
    static witness_t g;
    witness_init(&g, w->link);
    g.since_ms = since_ms;
    g.until_ms = until_ms;
    int rc = stamp(&g);
    bool on = false;
    if (!rc) rc = from_journal(&g, &on);
    if (!rc && on) { /* newest first: turn it to oldest first (WM1) */
        g.kept = true;
        for (uint32_t i = 0, j = g.n; i + 1u < j; i++, j--) {
            witness_entry_t t = g.e[i];
            g.e[i] = g.e[j - 1u];
            g.e[j - 1u] = t;
        }
    } else if (!rc) {
        g.n = 0;
        g.more = false;
        rc = from_tail(&g);
    }
    if (rc) return rc; /* WM4: the record shown stays */
    for (uint32_t i = 0; i < g.n; i++) {
        g.e[i].prev = i ? g.e[i - 1u].index : 0;
        g.count[g.e[i].kind]++;
    }
    g.broken = witness_chain(&g);
    g.gathered = true;
    g.verdict = LPRES_STATE_NEITHER;
    *w = g;
    return ZXM_OK;
}

uint32_t witness_chain(const witness_t *w)
{
    uint64_t last = 0;
    for (uint32_t i = 0; i < w->n; i++) {
        const witness_entry_t *e = &w->e[i];
        if (!witness_linked((int64_t) e->index, (int64_t) e->prev, (int64_t) last)) return i;
        last = e->index;
    }
    return w->n;
}

int64_t witness_count(const witness_t *w, uint8_t kind)
{
    static int64_t k[WITNESS_KEEP];
    for (uint32_t i = 0; i < w->n; i++) k[i] = w->e[i].kind;
    return witness_kind_count(k, w->n, kind);
}

int witness_seal(witness_t *w)
{
    if (!w->gathered) return ZXM_E_STATE;
    zxm_link_t *l = w->link;
    zxm_begin(l, "journal.verify");
    int rc = zxm_call(l);
    if (rc) return rc;
    witness_seal_t s;
    memset(&s, 0, sizeof s);
    if (zxm_get_bool(l, l->result, "on", &s.on) || zxm_get_bool(l, l->result, "ok", &s.ok) ||
        zxm_get_u64(l, l->result, "entries", &s.entries) ||
        zxm_get_u64(l, l->result, "first_seq", &s.first_seq) ||
        zxm_get_u64(l, l->result, "last_seq", &s.last_seq) ||
        zxm_get_u64(l, l->result, "broken_at", &s.broken_at) ||
        zxm_get_str(l, l->result, "why", s.why, sizeof s.why) ||
        (s.ok && (s.broken_at || s.why[0])) || (!s.ok && s.on && !s.why[0]))
        return ZXM_E_PROTOCOL;
    s.done = true;
    w->seal = s;
    /* two witnesses of one subject, the record intact: a journal not kept
     * and an empty record say nothing */
    lpres_state_t hash = !s.on ? LPRES_STATE_NEITHER : s.ok ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    lpres_state_t links = !w->n               ? LPRES_STATE_NEITHER
                          : w->broken == w->n ? LPRES_STATE_TRUE
                                              : LPRES_STATE_FALSE;
    w->verdict = lpres_kjoin(hash, links);
    return ZXM_OK;
}

const char *witness_summarize(witness_t *w)
{
    char chain[64], seal[160];
    if (!w->n)
        snprintf(chain, sizeof chain, "nothing to link");
    else if (w->broken == w->n)
        snprintf(chain, sizeof chain, "chain intact");
    else
        snprintf(chain, sizeof chain, "chain broken at seq %llu",
                 (unsigned long long) w->e[w->broken].index);
    if (!w->seal.done)
        snprintf(seal, sizeof seal, "journal not checked");
    else if (!w->seal.on)
        snprintf(seal, sizeof seal, "no journal kept");
    else if (w->seal.ok)
        snprintf(seal, sizeof seal, "journal sealed through seq %llu",
                 (unsigned long long) w->seal.last_seq);
    else
        snprintf(seal, sizeof seal, "journal broken at seq %llu: %s",
                 (unsigned long long) w->seal.broken_at, w->seal.why);
    static const char *const VERDICT[] = {"not witnessed", "intact", "broken",
                                          "held: the witnesses disagree"};
    snprintf(w->summary, sizeof w->summary,
             "%u %s events: %u changes, %u refused, %u AI actions, %u faults, %u other; %s; "
             "%s; %s",
             w->n, w->kept ? "kept" : "recent", w->count[WITNESS_CHANGE], w->count[WITNESS_REFUSED],
             w->count[WITNESS_AI], w->count[WITNESS_FAULT], w->count[WITNESS_OTHER], chain, seal,
             VERDICT[(uint32_t) w->verdict & 3u]);
    return w->summary;
}
