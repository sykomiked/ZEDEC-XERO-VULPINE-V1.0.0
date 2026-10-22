/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sieve_model.c — Sieve, AI suggestions held until accepted
 * (sieve_model.h SM1-SM7). */
#include "sieve_model.h"

#include <stdio.h>
#include <string.h>

#include "ramble_rules.h"
#include "witness_rules.h"

#define SIEVE_JOURNAL_MAX 500u /* journal.query's largest page */

static const char *const SOURCE_NAME[] = {"an answer", "a draft", "an envelope"};

/* ---- small helpers ----------------------------------------------------------- */

static uint32_t bounded_len(const char *s, uint32_t most)
{
    uint32_t n = 0;
    while (n <= most && s[n]) n++;
    return n;
}

static bool now_ok(int64_t now)
{
    return now >= 0 && now <= SIEVE_NOW_MAX;
}

static bool version_ok(int64_t v)
{
    return v >= 0 && v <= SIEVE_VERSION_MAX;
}

static bool app_ok(int64_t app)
{
    return app >= 1 && app <= SIEVE_APP_FIELD;
}

/* SR6 for a kept item; 0 when it has lapsed */
static int64_t left_of(const sieve_item_t *it, int64_t now)
{
    int64_t left = sieve_seconds_left(it->made, it->ttl, now);
    return left < 0 ? 0 : left;
}

/* SM1: the checks every new hold passes before anything is asked */
static int hold_check(const sieve_t *s, int64_t app, int64_t source, int64_t now, int64_t ttl,
                      int64_t version, uint32_t room)
{
    if (!app_ok(app) || !now_ok(now) || !version_ok(version) || !sieve_hold_ok(source, ttl, now))
        return ZXM_E_ARG;
    if (s->n + room > SIEVE_MAX) return ZXM_E_TOOBIG; /* backpressure: nothing sent */
    if ((uint64_t) s->next_id + room > (uint64_t) SIEVE_ID_FIELD) return ZXM_E_TOOBIG;
    /* the record it will make (SR5): its expiry fits 12 digits */
    if (!sieve_record_ok(s->next_id + 1u, app, source, SIEVE_PENDING, now + ttl, version))
        return ZXM_E_ARG;
    return ZXM_OK;
}

/* SM1: keep one new PENDING item (room was checked) */
static sieve_item_t *keep(sieve_t *s, int64_t app, uint8_t source, int64_t now, int64_t ttl,
                          int64_t version)
{
    sieve_item_t *it = &s->item[s->n++];
    memset(it, 0, sizeof *it);
    it->id = ++s->next_id;
    it->source = source;
    it->state = SIEVE_PENDING;
    it->app = app;
    it->made = now;
    it->ttl = ttl;
    it->version = version;
    it->decided = -1;
    return it;
}

/* SM4: room for one more log line */
static bool log_room(const sieve_t *s)
{
    return s->head - s->tail < SIEVE_LOG;
}

/* SM4: the line, written after the change it records (room was checked) */
static void log_line(sieve_t *s, const sieve_item_t *it, int64_t at)
{
    sieve_log_t *l = &s->log[s->head % SIEVE_LOG];
    l->id = it->id;
    l->source = it->source;
    l->state = it->state;
    l->app = it->app;
    l->at = at;
    l->version = it->version;
    s->head++;
}

void sieve_init(sieve_t *s, zxm_link_t *link)
{
    memset(s, 0, sizeof *s);
    s->link = link;
}

int32_t sieve_find(const sieve_t *s, uint32_t id)
{
    for (uint32_t i = 0; i < s->n; i++)
        if (s->item[i].id == id) return (int32_t) i;
    return -1;
}

/* ---- SM1: holding -------------------------------------------------------------- */

int sieve_ask(sieve_t *s, int64_t app, const char *prompt, int64_t now, int64_t ttl,
              int64_t version)
{
    static char got[SIEVE_TEXT_MAX + 1];
    zxm_link_t *l = s->link;
    if (!prompt) return ZXM_E_ARG;
    uint32_t n = bounded_len(prompt, ZXM_ASK_MAX);
    if (n == 0 || n > ZXM_ASK_MAX) return ZXM_E_ARG;
    int rc = hold_check(s, app, SIEVE_SRC_ANSWER, now, ttl, version, 1u);
    if (rc != ZXM_OK) return rc;
    zxm_begin(l, "swarm.ask");
    zxm_p_str(l, "prompt", prompt);
    rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    rc = zxm_get_str(l, l->result, "answer", got, sizeof got);
    if (rc != ZXM_OK) return rc == ZXM_E_TOOBIG ? ZXM_E_TOOBIG : ZXM_E_PROTOCOL;
    uint32_t len = bounded_len(got, SIEVE_TEXT_MAX);
    if (len == 0) return ZXM_E_PROTOCOL; /* nothing to hold */
    sieve_item_t *it = keep(s, app, SIEVE_SRC_ANSWER, now, ttl, version);
    memcpy(it->text, got, len + 1u);
    it->len = len;
    it->model = true;
    return ZXM_OK;
}

int sieve_draft(sieve_t *s, int64_t app, const char *template_id, const char *request, int64_t now,
                int64_t ttl, int64_t version)
{
    static char got[SIEVE_TEXT_MAX + 1];
    zxm_link_t *l = s->link;
    bool model = false;
    if (!template_id || !request) return ZXM_E_ARG;
    uint32_t tn = bounded_len(template_id, SIEVE_NAME_MAX);
    uint32_t rn = bounded_len(request, SIEVE_DRAFT_REQ);
    if (tn == 0 || tn > SIEVE_NAME_MAX || rn == 0 || rn > SIEVE_DRAFT_REQ) return ZXM_E_ARG;
    int rc = hold_check(s, app, SIEVE_SRC_DRAFT, now, ttl, version, 1u);
    if (rc != ZXM_OK) return rc;
    zxm_begin(l, "play.draft");
    zxm_p_str(l, "template", template_id);
    zxm_p_str(l, "request", request);
    rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_bool(l, l->result, "model", &model) != ZXM_OK) return ZXM_E_PROTOCOL;
    rc = zxm_get_str(l, l->result, "draft", got, sizeof got);
    if (rc != ZXM_OK) return rc == ZXM_E_TOOBIG ? ZXM_E_TOOBIG : ZXM_E_PROTOCOL;
    uint32_t len = bounded_len(got, SIEVE_TEXT_MAX);
    if (len == 0) return ZXM_E_PROTOCOL;
    sieve_item_t *it = keep(s, app, SIEVE_SRC_DRAFT, now, ttl, version);
    memcpy(it->text, got, len + 1u);
    it->len = len;
    it->model = model;
    memcpy(it->what, template_id, tn + 1u);
    return ZXM_OK;
}

/* the envelope already held, by its feed id */
static bool envelope_held(const sieve_t *s, uint32_t ref)
{
    for (uint32_t i = 0; i < s->n; i++)
        if (s->item[i].source == SIEVE_SRC_ENVELOPE && s->item[i].ref == ref) return true;
    return false;
}

/* a decimal string of at most 19 digits that fits int64 */
static bool dec_i64(const char *t, int64_t *v)
{
    bool neg = *t == '-';
    uint64_t u = 0;
    uint32_t n = 0;
    if (neg) t++;
    for (; *t; t++, n++) {
        if (*t < '0' || *t > '9' || n >= 19u) return false;
        u = u * 10u + (uint64_t) (*t - '0');
    }
    if (!n || u > (uint64_t) INT64_MAX) return false;
    *v = neg ? -(int64_t) u : (int64_t) u;
    return true;
}

typedef struct {
    uint32_t id;
    bool pii;
    int64_t amount;
    char tool[SIEVE_NAME_MAX + 1];
    char payload[SIEVE_TEXT_MAX + 1];
} sieve_env_t;

int sieve_gather(sieve_t *s, int64_t app, int64_t now, int64_t ttl)
{
    static sieve_env_t page[SIEVE_FEED_MAX];
    zxm_link_t *l = s->link;
    char status[16], amount[24];
    bool open = false;
    int rc = hold_check(s, app, SIEVE_SRC_ENVELOPE, now, ttl, 0, 0u);
    if (rc != ZXM_OK) return rc;
    zxm_begin(l, "web4.feed");
    zxm_p_u64(l, "limit", SIEVE_FEED_MAX);
    zxm_p_str(l, "status", "held");
    rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t o = l->result;
    if (zxm_get_bool(l, o, "open", &open) != ZXM_OK) return ZXM_E_PROTOCOL;
    int32_t arr = zxm_get(l, o, "envelopes");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr), fresh = 0;
    if (n > SIEVE_FEED_MAX || (!open && n)) return ZXM_E_PROTOCOL;
    /* the whole page is read and checked before any of it is kept */
    for (uint32_t i = 0; i < n; i++) {
        int32_t e = zxm_at(l, arr, i);
        uint64_t id = 0;
        sieve_env_t *p = &page[i];
        if (e < 0 || zxm_get_u64(l, e, "id", &id) != ZXM_OK || id == 0 || id > UINT32_MAX ||
            zxm_get_str(l, e, "status", status, sizeof status) != ZXM_OK ||
            strcmp(status, "held") || zxm_get_bool(l, e, "pii", &p->pii) != ZXM_OK ||
            zxm_get_str(l, e, "amount_minor", amount, sizeof amount) != ZXM_OK ||
            !dec_i64(amount, &p->amount) || p->amount < 0 || p->amount > RAMBLE_MINOR_MAX ||
            zxm_get_str(l, e, "tool", p->tool, sizeof p->tool) != ZXM_OK ||
            zxm_get_str(l, e, "payload", p->payload, sizeof p->payload) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        p->id = (uint32_t) id;
        if (i && p->id >= page[i - 1u].id) return ZXM_E_PROTOCOL; /* newest first */
        if (!envelope_held(s, p->id)) fresh++;
    }
    rc = hold_check(s, app, SIEVE_SRC_ENVELOPE, now, ttl, 0, fresh);
    if (rc != ZXM_OK) return rc; /* not room for all: none kept */
    for (uint32_t i = 0; i < n; i++) {
        const sieve_env_t *p = &page[i];
        if (envelope_held(s, p->id)) continue;
        sieve_item_t *it = keep(s, app, SIEVE_SRC_ENVELOPE, now, ttl, 0);
        it->ref = p->id;
        it->pii = p->pii;
        it->amount = p->amount;
        it->model = false;
        memcpy(it->what, p->tool, sizeof it->what);
        it->len = bounded_len(p->payload, SIEVE_TEXT_MAX);
        memcpy(it->text, p->payload, it->len + 1u);
    }
    s->added = fresh;
    return ZXM_OK;
}

/* ---- SM3: the owner's word ------------------------------------------------------ */

/* web4.consent for an envelope: grant for left seconds, or deny */
static int consent(sieve_t *s, const sieve_item_t *it, bool grant, int64_t left, int64_t now)
{
    zxm_link_t *l = s->link;
    char status[16];
    uint64_t got = 0, answers = 0;
    zxm_begin(l, "web4.consent");
    zxm_p_u64(l, "id", it->ref);
    zxm_p_bool(l, "grant", grant);
    if (grant) zxm_p_i64(l, "expires_in", left);
    zxm_p_u64(l, "now", (uint64_t) now);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t env = zxm_get(l, l->result, "envelope"), con = zxm_get(l, l->result, "consent");
    if (env < 0 || con < 0 || zxm_get_u64(l, env, "id", &got) != ZXM_OK || got != it->ref ||
        zxm_get_str(l, env, "status", status, sizeof status) != ZXM_OK ||
        strcmp(status, grant ? "signed" : "denied") ||
        zxm_get_u64(l, con, "answers", &answers) != ZXM_OK || answers != it->ref)
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

int sieve_accept(sieve_t *s, uint32_t id, int64_t now, int64_t version, bool owner_confirmed)
{
    int32_t at = sieve_find(s, id);
    if (at < 0 || !now_ok(now) || !version_ok(version)) return ZXM_E_ARG;
    sieve_item_t *it = &s->item[at];
    int64_t left = left_of(it, now);
    if (it->state != SIEVE_PENDING || left < 1) return ZXM_E_STATE;
    if (sieve_drift(version, it->version) != 0) return ZXM_E_STATE; /* the target moved */
    if (!owner_confirmed) return ZXM_E_STATE;                       /* never by itself */
    if (!log_room(s)) return ZXM_E_TOOBIG;                          /* SM4: logged, or not done */
    if (it->source == SIEVE_SRC_ENVELOPE) {
        /* the grant is the envelope's own scope and amount: it covers it (RR1) */
        int64_t need = ramble_need(it->pii, it->amount);
        if (!ramble_consent_covers(ramble_scope_gap(need, need), 0, left)) return ZXM_E_ARG;
        int rc = consent(s, it, true, left, now);
        if (rc != ZXM_OK) return rc;
    }
    it->state = SIEVE_ACCEPTED;
    it->decided = now;
    log_line(s, it, now);
    return ZXM_OK;
}

int sieve_reject(sieve_t *s, uint32_t id, int64_t now, bool owner_confirmed)
{
    int32_t at = sieve_find(s, id);
    if (at < 0 || !now_ok(now)) return ZXM_E_ARG;
    sieve_item_t *it = &s->item[at];
    if (it->state != SIEVE_PENDING) return ZXM_E_STATE;
    if (!owner_confirmed) return ZXM_E_STATE;
    if (!log_room(s)) return ZXM_E_TOOBIG;
    if (it->source == SIEVE_SRC_ENVELOPE) {
        int rc = consent(s, it, false, 0, now);
        if (rc != ZXM_OK) return rc;
    }
    it->state = SIEVE_REJECTED;
    it->decided = now;
    it->text[0] = 0; /* dropped, as zxm_held_reject does */
    it->len = 0;
    log_line(s, it, now);
    return ZXM_OK;
}

/* ---- SM5: applying -------------------------------------------------------------- */

bool sieve_may_apply(const sieve_t *s, uint32_t id, int64_t now, int64_t version)
{
    int32_t at = sieve_find(s, id);
    if (at < 0 || !now_ok(now)) return false;
    const sieve_item_t *it = &s->item[at];
    int64_t accepted = it->state == SIEVE_ACCEPTED ? 1 : 0;
    return sieve_apply_ok(accepted, left_of(it, now), sieve_drift(version, it->version));
}

int sieve_apply(sieve_t *s, uint32_t id, int64_t now, int64_t version, bool owner_confirmed,
                const char **text, uint32_t *len)
{
    int32_t at = sieve_find(s, id);
    if (at < 0 || !text || !len || !now_ok(now) || !version_ok(version)) return ZXM_E_ARG;
    if (!sieve_may_apply(s, id, now, version)) return ZXM_E_STATE;
    if (!owner_confirmed) return ZXM_E_STATE;
    if (!log_room(s)) return ZXM_E_TOOBIG;
    sieve_item_t *it = &s->item[at];
    it->state = SIEVE_APPLIED;
    log_line(s, it, now);
    *text = it->text;
    *len = it->len;
    return ZXM_OK;
}

int sieve_sweep(sieve_t *s, int64_t now, bool owner_confirmed)
{
    if (!now_ok(now)) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    uint32_t k = 0;
    for (uint32_t i = 0; i < s->n; i++) {
        const sieve_item_t *it = &s->item[i];
        bool live =
            (it->state == SIEVE_PENDING || it->state == SIEVE_ACCEPTED) && left_of(it, now) > 0;
        if (live) {
            if (k != i) s->item[k] = *it;
            k++;
        }
    }
    s->n = k;
    return ZXM_OK;
}

/* ---- SM6: counts ---------------------------------------------------------------- */

int64_t sieve_held_for(const sieve_t *s, int64_t app, int64_t now)
{
    int64_t apps[SIEVE_MAX], pending[SIEVE_MAX];
    if (!now_ok(now)) return SIEVE_REFUSED;
    for (uint32_t i = 0; i < s->n; i++) {
        apps[i] = s->item[i].app;
        pending[i] = s->item[i].state == SIEVE_PENDING && left_of(&s->item[i], now) > 0;
    }
    return sieve_held_count(apps, pending, s->n, app);
}

int64_t sieve_held_by_source(const sieve_t *s, int64_t source)
{
    int64_t kind[SIEVE_MAX];
    uint32_t n = 0;
    if (source < 0 || source > SIEVE_SRC_MAX) return SIEVE_REFUSED;
    for (uint32_t i = 0; i < s->n; i++)
        if (s->item[i].state == SIEVE_PENDING) kind[n++] = s->item[i].source;
    return witness_kind_count(kind, n, source);
}

/* ---- SM4: the log ---------------------------------------------------------------- */

int sieve_log_take(sieve_t *s, sieve_log_t *out)
{
    if (!out) return ZXM_E_ARG;
    if (s->head == s->tail) return ZXM_E_STATE;
    *out = s->log[s->tail % SIEVE_LOG];
    s->tail++;
    return ZXM_OK;
}

int sieve_journal(sieve_t *s, uint64_t since_ms)
{
    zxm_link_t *l = s->link;
    char text[64], source[16];
    bool on = false, more = false;
    uint32_t count = 0;
    uint64_t newest = 0, prev = 0;
    zxm_begin(l, "journal.query");
    if (since_ms) zxm_p_u64(l, "since_ms", since_ms);
    zxm_p_u64(l, "max", SIEVE_JOURNAL_MAX);
    zxm_p_str(l, "source", "rpc");
    zxm_p_str(l, "text", "web4.consent");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t o = l->result;
    if (zxm_get_bool(l, o, "on", &on) != ZXM_OK || zxm_get_bool(l, o, "more", &more) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    int32_t arr = zxm_get(l, o, "entries");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr);
    if (n > SIEVE_JOURNAL_MAX || (!on && n)) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < n; i++) {
        int32_t e = zxm_at(l, arr, i);
        uint64_t seq = 0;
        if (e < 0 || zxm_get_u64(l, e, "seq", &seq) != ZXM_OK || seq == 0 ||
            zxm_get_str(l, e, "source", source, sizeof source) != ZXM_OK || strcmp(source, "rpc") ||
            (i && seq >= prev))
            return ZXM_E_PROTOCOL; /* newest first */
        if (i == 0) newest = seq;
        prev = seq;
        /* zxvd's line for an answered consent: "web4.consent done in N ms";
         * a text too long for this buffer is no such line */
        if (zxm_get_str(l, e, "text", text, sizeof text) == ZXM_OK &&
            !strncmp(text, "web4.consent done in ", 21))
            count++;
    }
    s->journal_on = on;
    s->journal_more = more;
    s->journal_consents = count;
    s->journal_newest = newest;
    return ZXM_OK;
}

/* ---- SM7: notices ---------------------------------------------------------------- */

int sieve_notice(sieve_t *s, uint32_t id, int64_t now)
{
    int32_t at = sieve_find(s, id);
    if (at < 0 || !now_ok(now)) return ZXM_E_ARG;
    const sieve_item_t *it = &s->item[at];
    int64_t left = left_of(it, now);
    if (it->state != SIEVE_PENDING || left < 1) return ZXM_E_STATE;
    int n = snprintf(s->notice, sizeof s->notice,
                     "Suggestion %u, %s for app %lld, waits for your accept: nothing is applied "
                     "until you accept it. It lapses in %lld minutes.",
                     (unsigned) it->id, SOURCE_NAME[it->source], (long long) it->app,
                     (long long) ((left + 59) / 60));
    return n > 0 && (uint32_t) n < sizeof s->notice ? ZXM_OK : ZXM_E_TOOBIG;
}
