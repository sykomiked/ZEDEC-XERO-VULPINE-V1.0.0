/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zab_logic.c — ZAB-Logic over the public libzxv ABI. See zab_logic.h.
 *
 * Every number of the economy (allotments, the emotional pool, the level
 * split) comes from libzxv; this file only gates proposals with the
 * library's hexagram action table, spends admitted tokens with
 * zxv_swarm_consume, quarantines forbidden work in a zxv_hexlog and counts
 * what happened for the cycle report. No floats.
 */
#include "zab_logic.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEUTRAL_FACE 0x1F610u

typedef struct {
    uint32_t id;
    uint32_t level;
    int32_t active;       /* as set now (takes effect at the next cycle) */
    int32_t cycle_active; /* as it was when the open cycle began */
    uint32_t feel_cp, feel_k;
    uint32_t cycle_feel_cp, cycle_feel_k;
    uint32_t outcomes[ZAB_OUTCOMES];
} agent_t;

typedef struct {
    int32_t used;
    zab_held_t h;
} held_slot_t;

struct zab_swarm {
    zxv_ctx *ctx;
    const zxv_cap *cap;
    zxv_swarm *swarm;
    zxv_hexlog *log;
    uint32_t levels;
    uint64_t rate;       /* T for the next cycle */
    uint64_t cycle_rate; /* T of the open cycle */
    uint32_t mood_cp, mood_k;
    uint32_t cycle_mood_cp, cycle_mood_k;
    int32_t open;
    int32_t failed_closed;
    uint64_t cycle;
    uint32_t num_agents;
    agent_t agents[ZAB_MAX_AGENTS];
    uint32_t outcomes[ZAB_OUTCOMES];
    held_slot_t held[ZAB_MAX_HELD];
    uint32_t next_held_id;
};

/* ------------------------------------------------------------ errors */

static _Thread_local char g_err[256];

static int32_t fail(int32_t rc, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
    return rc;
}

/* A libzxv failure: keep its detail. */
static int32_t lib_fail(int32_t rc, const char *what)
{
    char detail[200];
    size_t n = 0;
    if (zxv_last_error(detail, sizeof detail, &n) != ZXV_OK) detail[0] = '\0';
    return fail(rc, "%s: %s (%s)", what, zxv_status_name(rc), detail);
}

static int32_t ok(void)
{
    g_err[0] = '\0';
    return ZXV_OK;
}

int32_t zab_last_error(char *buf, size_t cap, size_t *out_len)
{
    size_t n = strlen(g_err);
    if (out_len) *out_len = n;
    if (!buf || cap <= n) return ZXV_E_BUFFER;
    memcpy(buf, g_err, n + 1);
    return ZXV_OK;
}

const char *zab_outcome_name(int32_t outcome)
{
    switch (outcome) {
    case ZAB_ADMITTED: return "admitted";
    case ZAB_QUARANTINED: return "quarantined";
    case ZAB_HELD: return "held";
    case ZAB_REFUSED: return "refused";
    case ZAB_DEFERRED: return "deferred";
    case ZAB_OVER_BUDGET: return "over-budget";
    default: return "INVALID";
    }
}

/* ------------------------------------------------------------ lifecycle */

static agent_t *find_agent(zab_swarm_t *sw, uint32_t id)
{
    for (uint32_t i = 0; i < sw->num_agents; i++)
        if (sw->agents[i].id == id) return &sw->agents[i];
    return NULL;
}

int32_t zab_swarm_create(zxv_ctx *ctx, const zxv_cap *cap, uint32_t levels,
                         uint64_t tokens_per_cycle, uint32_t quarantine_capacity,
                         zab_swarm_t **out_swarm)
{
    if (!ctx || !cap || !out_swarm) return fail(ZXV_E_ARG, "ctx, cap or out_swarm is NULL");
    if (levels == 0 || levels > ZAB_MAX_LEVELS)
        return fail(ZXV_E_ARG, "levels must be 1..%u", ZAB_MAX_LEVELS);
    int32_t rc = zxv_cap_check(cap, ZAB_CAPS_NEEDED);
    if (rc) return lib_fail(rc, "zab_swarm_create: the cap needs SWARM and ATTEST");

    /* The cap must belong to ctx: a swarm made from ctx accepts only a cap
     * of the same context (R10, ABI 1.2), so a probe registration tells. */
    zxv_swarm *probe = NULL;
    rc = zxv_swarm_create(ctx, 1, 0, &probe);
    if (rc) return lib_fail(rc, "zab_swarm_create");
    rc = zxv_swarm_register(cap, probe, 0, 0);
    zxv_swarm_destroy(probe);
    if (rc) return lib_fail(rc, "zab_swarm_create: the cap does not belong to ctx");

    zab_swarm_t *sw = calloc(1, sizeof *sw);
    if (!sw) return fail(ZXV_E_NOMEM, "zab_swarm_create: out of memory");
    rc = zxv_swarm_create_cap(cap, levels, tokens_per_cycle, &sw->swarm);
    if (rc) {
        free(sw);
        return lib_fail(rc, "zab_swarm_create: zxv_swarm_create_cap");
    }
    rc = zxv_hexlog_create(cap, quarantine_capacity, &sw->log);
    if (rc) {
        zxv_swarm_destroy(sw->swarm);
        free(sw);
        return lib_fail(rc, "zab_swarm_create: zxv_hexlog_create");
    }
    sw->ctx = ctx;
    sw->cap = cap;
    sw->levels = levels;
    sw->rate = tokens_per_cycle;
    sw->mood_cp = NEUTRAL_FACE;
    sw->next_held_id = 1;
    *out_swarm = sw;
    return ok();
}

void zab_swarm_destroy(zab_swarm_t *sw)
{
    if (!sw) return;
    zxv_hexlog_destroy(sw->log);
    zxv_swarm_destroy(sw->swarm);
    free(sw);
}

int32_t zab_agent_add(zab_swarm_t *sw, const zab_agent_t *a)
{
    if (!sw || !a) return fail(ZXV_E_ARG, "zab_agent_add: NULL argument");
    if (sw->open) return fail(ZXV_E_STATE, "zab_agent_add: a cycle is open");
    if (find_agent(sw, a->id)) return fail(ZXV_E_DUPLICATE, "agent %u already added", a->id);
    uint32_t cp = a->feeling_codepoint ? a->feeling_codepoint : NEUTRAL_FACE;
    uint32_t k = a->feeling_codepoint ? a->feeling_intensity : 0;
    /* Check the feeling before registering, so a bad one adds nothing. */
    int32_t rc = zxv_swarm_emotion_profile(cp, NULL, NULL, NULL, NULL, NULL);
    if (rc) return lib_fail(rc, "zab_agent_add: feeling");
    if (k > 5) return fail(ZXV_E_ARG, "zab_agent_add: intensity %u is not 0..5", k);
    rc = zxv_swarm_register(sw->cap, sw->swarm, a->id, a->level);
    if (rc) return lib_fail(rc, "zab_agent_add");
    rc = zxv_swarm_set_feeling(sw->cap, sw->swarm, a->id, cp, k);
    if (rc) return lib_fail(rc, "zab_agent_add: set_feeling"); /* registered, neutral */
    agent_t *g = &sw->agents[sw->num_agents++];
    memset(g, 0, sizeof *g);
    g->id = a->id;
    g->level = a->level;
    g->active = 1;
    g->feel_cp = cp;
    g->feel_k = k;
    return ok();
}

int32_t zab_agent_set_active(zab_swarm_t *sw, uint32_t agent_id, int32_t active)
{
    if (!sw) return fail(ZXV_E_ARG, "swarm is NULL");
    agent_t *g = find_agent(sw, agent_id);
    if (!g) return fail(ZXV_E_NOT_FOUND, "agent %u is not in the swarm", agent_id);
    int32_t rc = zxv_swarm_set_active(sw->cap, sw->swarm, agent_id, active != 0);
    if (rc) return lib_fail(rc, "zab_agent_set_active");
    g->active = active != 0;
    return ok();
}

int32_t zab_agent_set_feeling(zab_swarm_t *sw, uint32_t agent_id, uint32_t codepoint,
                              uint32_t intensity)
{
    if (!sw) return fail(ZXV_E_ARG, "swarm is NULL");
    agent_t *g = find_agent(sw, agent_id);
    if (!g) return fail(ZXV_E_NOT_FOUND, "agent %u is not in the swarm", agent_id);
    int32_t rc = zxv_swarm_set_feeling(sw->cap, sw->swarm, agent_id, codepoint, intensity);
    if (rc) return lib_fail(rc, "zab_agent_set_feeling");
    g->feel_cp = codepoint;
    g->feel_k = intensity;
    return ok();
}

int32_t zab_set_mood(zab_swarm_t *sw, uint32_t codepoint, uint32_t intensity)
{
    if (!sw) return fail(ZXV_E_ARG, "swarm is NULL");
    int32_t rc = zxv_swarm_set_mood(sw->cap, sw->swarm, codepoint, intensity);
    if (rc) return lib_fail(rc, "zab_set_mood");
    sw->mood_cp = codepoint;
    sw->mood_k = intensity;
    return ok();
}

int32_t zab_set_rate(zab_swarm_t *sw, uint64_t tokens_per_cycle)
{
    if (!sw) return fail(ZXV_E_ARG, "swarm is NULL");
    int32_t rc = zxv_swarm_set_rate(sw->cap, sw->swarm, tokens_per_cycle);
    if (rc) return lib_fail(rc, "zab_set_rate");
    sw->rate = tokens_per_cycle;
    return ok();
}

/* ------------------------------------------------------------ cycles */

int32_t zab_cycle_begin(zab_swarm_t *sw, uint64_t *out_cycle)
{
    if (!sw) return fail(ZXV_E_ARG, "swarm is NULL");
    if (sw->failed_closed) return fail(ZXV_E_STATE, "the swarm failed closed");
    if (sw->open) return fail(ZXV_E_STATE, "a cycle is already open");
    uint64_t cycle = 0;
    int32_t rc = zxv_swarm_begin_cycle(sw->cap, sw->swarm, &cycle);
    if (rc) return lib_fail(rc, "zab_cycle_begin");
    sw->open = 1;
    sw->cycle = cycle;
    sw->cycle_rate = sw->rate;
    sw->cycle_mood_cp = sw->mood_cp;
    sw->cycle_mood_k = sw->mood_k;
    memset(sw->outcomes, 0, sizeof sw->outcomes);
    for (uint32_t i = 0; i < sw->num_agents; i++) {
        agent_t *g = &sw->agents[i];
        g->cycle_active = g->active;
        g->cycle_feel_cp = g->feel_cp;
        g->cycle_feel_k = g->feel_k;
        memset(g->outcomes, 0, sizeof g->outcomes);
    }
    if (out_cycle) *out_cycle = cycle;
    return ok();
}

int32_t zab_agent_allotment(zab_swarm_t *sw, uint32_t agent_id, uint64_t *out_allotted,
                            uint64_t *out_imaginary, uint64_t *out_used)
{
    if (!sw) return fail(ZXV_E_ARG, "swarm is NULL");
    if (!find_agent(sw, agent_id)) return fail(ZXV_E_NOT_FOUND, "agent %u is not in the swarm", agent_id);
    int32_t rc = zxv_swarm_allotment(sw->swarm, agent_id, out_allotted, out_imaginary, out_used);
    return rc ? lib_fail(rc, "zab_agent_allotment") : ok();
}

static void count(zab_swarm_t *sw, agent_t *g, int32_t outcome)
{
    sw->outcomes[outcome]++;
    g->outcomes[outcome]++;
}

/* The gate itself, for a tag already checked to be 0..63. */
static int32_t gate(zab_swarm_t *sw, agent_t *g, uint8_t tag, uint64_t tokens,
                    const uint8_t subject[32], zab_decision_t *d)
{
    uint8_t base = 0, pol = 0, phase = 0;
    int32_t action = 0;
    int32_t rc = zxv_hex_unpack(tag, &base, &pol, &phase);
    if (!rc) rc = zxv_hex_action(tag, &action);
    if (rc) return lib_fail(rc, "zab_propose: tag");

    memset(d, 0, sizeof *d);
    d->tag = tag;
    d->action = action;
    memcpy(d->subject, subject, 32);

    uint64_t allotted = 0, used = 0;
    rc = zxv_swarm_allotment(sw->swarm, g->id, &allotted, NULL, &used);
    if (rc) return lib_fail(rc, "zab_propose: allotment");
    uint64_t left = allotted - used;

    if (action == ZXV_HEX_ACT_ROLLBACK) {
        /* Forbidden: refuse and record. A full log means forbidden work we
         * cannot record, which must not continue: fail closed. */
        uint64_t seq = 0;
        rc = zxv_hexlog_append(sw->cap, sw->log, tag, (uint8_t) ZAB_REASON_FORBIDDEN, g->id,
                               subject, &seq);
        if (rc == ZXV_E_CAPACITY) {
            sw->failed_closed = 1;
            zxv_ctx_halt(sw->ctx, "zab-logic: quarantine log full");
            return fail(ZXV_E_CAPACITY, "zab_propose: quarantine log full; the swarm failed "
                                        "closed and the context is halted");
        }
        if (rc) return lib_fail(rc, "zab_propose: quarantine");
        d->outcome = ZAB_QUARANTINED;
        d->quarantine_seq = seq;
    } else if (base == ZXV_HEX_BASE_BOTH) {
        uint32_t slot = ZAB_MAX_HELD;
        for (uint32_t i = 0; i < ZAB_MAX_HELD && slot == ZAB_MAX_HELD; i++)
            if (!sw->held[i].used) slot = i;
        if (slot == ZAB_MAX_HELD)
            return fail(ZXV_E_CAPACITY, "zab_propose: %u proposals already held", ZAB_MAX_HELD);
        held_slot_t *h = &sw->held[slot];
        h->used = 1;
        h->h.held_id = sw->next_held_id++;
        h->h.agent_id = g->id;
        h->h.tag = tag;
        h->h.tokens = tokens;
        h->h.cycle = sw->cycle;
        memcpy(h->h.subject, subject, 32);
        d->outcome = ZAB_HELD;
        d->held_id = h->h.held_id;
    } else if (action == ZXV_HEX_ACT_REFUSE) {
        d->outcome = ZAB_REFUSED;
    } else if (action != ZXV_HEX_ACT_PROCEED || phase != ZXV_HEX_PHASE_MANIFEST) {
        d->outcome = ZAB_DEFERRED;
    } else if (tokens > left) {
        d->outcome = ZAB_OVER_BUDGET;
    } else {
        uint64_t granted = 0;
        rc = zxv_swarm_consume(sw->cap, sw->swarm, g->id, tokens, &granted);
        if (rc) return lib_fail(rc, "zab_propose: consume"); /* not reached: it fit */
        d->outcome = ZAB_ADMITTED;
        d->granted = granted;
        left -= granted;
    }
    d->remaining = left;
    count(sw, g, d->outcome);
    return ok();
}

static int32_t precheck(zab_swarm_t *sw, uint32_t agent_id, agent_t **g)
{
    if (sw->failed_closed) return fail(ZXV_E_STATE, "the swarm failed closed");
    if (!sw->open) return fail(ZXV_E_STATE, "no cycle is open");
    *g = find_agent(sw, agent_id);
    if (!*g) return fail(ZXV_E_NOT_FOUND, "agent %u is not in the swarm", agent_id);
    return ZXV_OK;
}

int32_t zab_propose(zab_swarm_t *sw, const zab_proposal_t *p, zab_decision_t *out)
{
    if (!sw || !p || !out) return fail(ZXV_E_ARG, "zab_propose: NULL argument");
    if (p->tag >= ZXV_HEX_STATES) return fail(ZXV_E_ARG, "zab_propose: tag %u is not 0..63", p->tag);
    if (p->tokens == 0) return fail(ZXV_E_ARG, "zab_propose: a proposal costs at least 1 token");
    if (!p->work && p->work_len) return fail(ZXV_E_ARG, "zab_propose: work is NULL");
    agent_t *g = NULL;
    int32_t rc = precheck(sw, p->agent_id, &g);
    if (rc) return rc;
    uint8_t subject[32];
    static const uint8_t empty[1] = {0};
    rc = zxv_sha3_256(p->work ? p->work : empty, p->work_len, subject);
    if (rc) return lib_fail(rc, "zab_propose: sha3");
    zab_decision_t d;
    rc = gate(sw, g, p->tag, p->tokens, subject, &d);
    if (rc == ZXV_OK) *out = d;
    return rc;
}

int32_t zab_review(zab_swarm_t *sw, uint32_t held_id, uint8_t base, uint8_t polarity,
                   zab_decision_t *out)
{
    if (!sw || !out) return fail(ZXV_E_ARG, "zab_review: NULL argument");
    held_slot_t *h = NULL;
    for (uint32_t i = 0; i < ZAB_MAX_HELD && !h; i++)
        if (sw->held[i].used && sw->held[i].h.held_id == held_id) h = &sw->held[i];
    if (!h) return fail(ZXV_E_NOT_FOUND, "zab_review: no held proposal %u", held_id);
    agent_t *g = NULL;
    int32_t rc = precheck(sw, h->h.agent_id, &g);
    if (rc) return rc;
    uint8_t tag = 0;
    rc = zxv_hex_set(h->h.tag, base, polarity, &tag);
    if (rc) return lib_fail(rc, "zab_review");
    zab_held_t copy = h->h;
    h->used = 0; /* free the slot first, so a BOTH again can be held again */
    zab_decision_t d;
    rc = gate(sw, g, tag, copy.tokens, copy.subject, &d);
    if (rc) {
        h->used = 1; /* nothing happened: keep it held */
        h->h = copy;
        return rc;
    }
    *out = d;
    return ok();
}

uint32_t zab_held_count(const zab_swarm_t *sw)
{
    uint32_t n = 0;
    if (!sw) return 0;
    for (uint32_t i = 0; i < ZAB_MAX_HELD; i++) n += sw->held[i].used ? 1u : 0u;
    return n;
}

int32_t zab_held_at(const zab_swarm_t *sw, uint32_t index, zab_held_t *out)
{
    if (!sw || !out) return fail(ZXV_E_ARG, "zab_held_at: NULL argument");
    /* Oldest first: held ids grow, so order by id. */
    uint32_t last = 0, k = 0;
    for (;;) {
        const held_slot_t *best = NULL;
        for (uint32_t i = 0; i < ZAB_MAX_HELD; i++) {
            const held_slot_t *h = &sw->held[i];
            if (h->used && h->h.held_id > last && (!best || h->h.held_id < best->h.held_id))
                best = h;
        }
        if (!best) return fail(ZXV_E_NOT_FOUND, "zab_held_at: index %u out of range", index);
        if (k == index) {
            *out = best->h;
            return ok();
        }
        last = best->h.held_id;
        k++;
    }
}

int32_t zab_cycle_end(zab_swarm_t *sw, zab_cycle_report_t *r)
{
    if (!sw) return fail(ZXV_E_ARG, "swarm is NULL");
    if (!sw->open) return fail(ZXV_E_STATE, "no cycle is open");
    zab_cycle_report_t tmp;
    zab_cycle_report_t *rep = r ? r : &tmp;
    memset(rep, 0, sizeof *rep);
    rep->cycle = sw->cycle;
    rep->tokens_per_cycle = sw->cycle_rate;
    rep->mood_codepoint = sw->cycle_mood_cp;
    rep->mood_intensity = sw->cycle_mood_k;
    /* Read the allotments before closing: the library zeroes them then. */
    for (uint32_t i = 0; i < sw->num_agents; i++) {
        const agent_t *g = &sw->agents[i];
        zab_agent_report_t *a = &rep->agents[i];
        a->id = g->id;
        a->level = g->level;
        a->active = g->cycle_active;
        int32_t rc = zxv_swarm_allotment(sw->swarm, g->id, &a->allotted, &a->imaginary, &a->used);
        if (rc) return lib_fail(rc, "zab_cycle_end: allotment");
        memcpy(a->outcomes, g->outcomes, sizeof a->outcomes);
        a->feeling_codepoint = g->cycle_feel_cp;
        a->feeling_intensity = g->cycle_feel_k;
        rc = zxv_swarm_emotion_profile(g->cycle_feel_cp, &a->valence, &a->temperature_milli,
                                       &a->explore_permille, &a->verify_passes, &a->priority);
        if (rc) return lib_fail(rc, "zab_cycle_end: profile");
        rep->allotted_total += a->allotted;
        rep->imaginary_total += a->imaginary;
        rep->used_total += a->used;
    }
    rep->num_agents = sw->num_agents;
    uint64_t expired = 0;
    int32_t rc = zxv_swarm_end_cycle(sw->cap, sw->swarm, &expired);
    if (rc) return lib_fail(rc, "zab_cycle_end");
    sw->open = 0;
    rep->expired = expired;
    memcpy(rep->outcomes, sw->outcomes, sizeof rep->outcomes);
    rep->held_pending = zab_held_count(sw);
    rc = zxv_hexlog_count(sw->log, &rep->quarantine_count);
    if (!rc) rc = zxv_hexlog_head(sw->log, rep->quarantine_head);
    if (rc) return lib_fail(rc, "zab_cycle_end: quarantine log");
    return ok();
}

zxv_hexlog *zab_quarantine_log(zab_swarm_t *sw)
{
    return sw ? sw->log : NULL;
}

int32_t zab_failed_closed(const zab_swarm_t *sw)
{
    return sw ? sw->failed_closed : 0;
}

/* ------------------------------------------------------------ report text */

typedef struct {
    char *buf;
    size_t cap, len;
} sbuf;

static void put(sbuf *s, const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    size_t m = (size_t) n < sizeof line ? (size_t) n : sizeof line - 1;
    if (s->buf && s->len + m < s->cap) memcpy(s->buf + s->len, line, m);
    s->len += m;
}

int32_t zab_report_format(const zab_cycle_report_t *r, char *buf, size_t cap, size_t *out_len)
{
    if (!r || !out_len) return fail(ZXV_E_ARG, "zab_report_format: NULL argument");
    if (r->num_agents > ZAB_MAX_AGENTS) return fail(ZXV_E_ARG, "zab_report_format: bad report");
    sbuf s = {buf, cap, 0};
    put(&s, "cycle %llu  T=%llu  mood U+%04X x%u\n", (unsigned long long) r->cycle,
        (unsigned long long) r->tokens_per_cycle, r->mood_codepoint, r->mood_intensity);
    put(&s, "allotted %llu (emotional %llu)  used %llu  expired %llu\n",
        (unsigned long long) r->allotted_total, (unsigned long long) r->imaginary_total,
        (unsigned long long) r->used_total, (unsigned long long) r->expired);
    put(&s, "gate: admitted %u  quarantined %u  held %u  refused %u  deferred %u  over-budget %u\n",
        r->outcomes[0], r->outcomes[1], r->outcomes[2], r->outcomes[3], r->outcomes[4],
        r->outcomes[5]);
    put(&s, "held pending %u  quarantine records %u  head %02x%02x%02x%02x...\n", r->held_pending,
        r->quarantine_count, r->quarantine_head[0], r->quarantine_head[1], r->quarantine_head[2],
        r->quarantine_head[3]);
    put(&s, "agent       lvl act  allotted  emotional      used  adm qua hld ref def ovr  "
            "feeling   temp explore verify prio\n");
    for (uint32_t i = 0; i < r->num_agents; i++) {
        const zab_agent_report_t *a = &r->agents[i];
        put(&s,
            "%-10u  %3u %3s %9llu %10llu %9llu  %3u %3u %3u %3u %3u %3u  U+%04X x%u %4u.%03u "
            "%3u.%01u%% %6u %4u\n",
            a->id, a->level, a->active ? "yes" : "no", (unsigned long long) a->allotted,
            (unsigned long long) a->imaginary, (unsigned long long) a->used, a->outcomes[0],
            a->outcomes[1], a->outcomes[2], a->outcomes[3], a->outcomes[4], a->outcomes[5],
            a->feeling_codepoint, a->feeling_intensity, a->temperature_milli / 1000u,
            a->temperature_milli % 1000u, a->explore_permille / 10u, a->explore_permille % 10u,
            a->verify_passes, a->priority);
    }
    *out_len = s.len;
    if (!buf || cap <= s.len) return fail(ZXV_E_BUFFER, "zab_report_format: need %zu bytes", s.len + 1);
    buf[s.len] = '\0';
    return ok();
}
