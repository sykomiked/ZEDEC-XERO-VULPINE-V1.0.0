/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* whetstone_model.c — Whetstone, the build and test runner (see
 * whetstone_model.h). Every run is checked against its limits and its
 * record's room before anything is sent, and a record is checked before it
 * is kept. */
#include <stdio.h>
#include <string.h>

#include "whetstone_model.h"

static const char *const VERDICT_NAME[4] = {"NEITHER", "TRUE", "FALSE", "BOTH"};

/* the length of s, counting no further than cap + 1 */
static uint32_t bounded_len(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    while (n <= cap && s[n]) n++;
    return n;
}

/* a template id as play.run takes it: 1..63 bytes, no '/', no control byte */
static bool id_ok(const char *s, uint32_t n)
{
    if (n == 0 || n > WHETSTONE_NAME) return false;
    for (uint32_t i = 0; i < n; i++)
        if (s[i] == '/' || s[i] == '\\' || (unsigned char) s[i] <= 0x20u) return false;
    return true;
}

static int64_t verdict_of(const char *word)
{
    for (int64_t v = 0; v < 4; v++)
        if (!strcmp(word, VERDICT_NAME[v])) return v;
    return WHETSTONE_REFUSED;
}

void whetstone_init(whetstone_t *w, zxm_link_t *link)
{
    memset(w, 0, sizeof *w);
    w->link = link;
}

/* ---- WM1 the plan ------------------------------------------------------------- */

/* a count of at most 9 digits from s[0..n); WHETSTONE_REFUSED when it is none */
static int64_t count_of(const char *s, uint32_t n)
{
    int64_t v = 0;
    if (n == 0 || n > 9) return WHETSTONE_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return WHETSTONE_REFUSED;
        v = v * 10 + (s[i] - '0');
    }
    return v;
}

/* one line s[0..n) into t: 1 a target, 0 nothing (blank or '#'), -1 not a target */
static int plan_line(const char *s, uint32_t n, whetstone_target_t *t)
{
    uint32_t start[4], len[4], k = 0, i = 0;
    while (i < n) {
        while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')) i++;
        if (i >= n) break;
        if (k == 4) return -1;
        start[k] = i;
        while (i < n && s[i] != ' ' && s[i] != '\t' && s[i] != '\r') i++;
        len[k] = i - start[k];
        k++;
    }
    if (k == 0 || s[start[0]] == '#') return 0;
    if (k == 2 || k > 3 || !id_ok(s + start[0], len[0])) return -1;
    memset(t, 0, sizeof *t);
    memcpy(t->id, s + start[0], len[0]);
    t->retries = WHETSTONE_DEFAULT_RETRIES;
    t->timeout_s = WHETSTONE_DEFAULT_TIMEOUT;
    if (k == 3) {
        t->retries = count_of(s + start[1], len[1]);
        t->timeout_s = count_of(s + start[2], len[2]);
    }
    return whetstone_retry_ok(t->retries, t->timeout_s) ? 1 : -1;
}

int whetstone_load_plan(whetstone_t *w, const char *space, const char *path)
{
    static char text[WHETSTONE_PLAN_MAX + 1];
    static whetstone_target_t got[WHETSTONE_TARGETS];
    zxm_link_t *l = w->link;
    uint64_t size = 0;
    bool eof = false;
    if (!path || !path[0]) return ZXM_E_ARG;
    zxm_begin(l, "files.read");
    zxm_p_space(l, space);
    zxm_p_str(l, "path", path);
    zxm_p_u64(l, "max", WHETSTONE_PLAN_MAX + 1u);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_u64(l, l->result, "size", &size) != ZXM_OK ||
        zxm_get_bool(l, l->result, "eof", &eof) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    if (size > WHETSTONE_PLAN_MAX || !eof) return ZXM_E_TOOBIG;
    if (zxm_get_str(l, l->result, "text", text, sizeof text) != ZXM_OK) return ZXM_E_PROTOCOL;
    uint32_t len = bounded_len(text, WHETSTONE_PLAN_MAX), n = 0, line = 1;
    for (uint32_t i = 0; i < len; line++) {
        uint32_t e = i;
        while (e < len && text[e] != '\n') e++;
        whetstone_target_t t;
        int r = plan_line(text + i, e - i, &t);
        if (r < 0) {
            snprintf(w->notice, sizeof w->notice,
                     "%s line %u is no target (an id, then retries 0..3 and a timeout of "
                     "1..3600 s): the plan is not taken.",
                     path, line);
            return ZXM_E_ARG;
        }
        if (r > 0) {
            if (n >= WHETSTONE_TARGETS) {
                snprintf(w->notice, sizeof w->notice,
                         "%s has more than %u targets: the plan is not taken.", path,
                         WHETSTONE_TARGETS);
                return ZXM_E_TOOBIG;
            }
            got[n++] = t;
        }
        i = e + 1u;
    }
    memcpy(w->target, got, sizeof got[0] * n);
    w->ntargets = n;
    w->planned = true;
    snprintf(w->notice, sizeof w->notice, "%s: %u targets.", path, n);
    return ZXM_OK;
}

/* ---- WM2 limits --------------------------------------------------------------- */

int whetstone_set_limits(whetstone_t *w, uint32_t target, int64_t retries, int64_t timeout_s,
                         bool owner_confirmed)
{
    if (!w->planned || target >= w->ntargets || !whetstone_retry_ok(retries, timeout_s))
        return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    w->target[target].retries = retries;
    w->target[target].timeout_s = timeout_s;
    snprintf(w->notice, sizeof w->notice, "%s: %lld retries, %lld s a try.", w->target[target].id,
             (long long) retries, (long long) timeout_s);
    return ZXM_OK;
}

/* ---- WM3 a run ---------------------------------------------------------------- */

/* zxvd's clock, ms since 1970 (sys.info time_ms) */
static int clock_ms(zxm_link_t *l, int64_t *now)
{
    zxm_begin(l, "sys.info");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_i64(l, l->result, "time_ms", now) != ZXM_OK || *now < 0) return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

/* one try: its verdict and step counts into r (counts only when it answered in time) */
static int one_try(whetstone_t *w, const whetstone_target_t *t, whetstone_record_t *r, int64_t *ms)
{
    zxm_link_t *l = w->link;
    char word[16];
    int64_t start = 0, end = 0, limit = t->timeout_s * 1000;
    int rc = clock_ms(l, &start);
    if (rc != ZXM_OK) return rc;
    zxm_begin(l, "play.run");
    zxm_p_str(l, "template", t->id);
    rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(l, l->result, "verdict", word, sizeof word) != ZXM_OK) return ZXM_E_PROTOCOL;
    int64_t verdict = verdict_of(word);
    int32_t s = zxm_get(l, l->result, "steps");
    if (verdict < 0 || s < 0 || l->tok[s].type != W4_J_ARR || zxm_len(l, s) > WHETSTONE_STEPS)
        return ZXM_E_PROTOCOL;
    int64_t count[4] = {0, 0, 0, 0};
    for (uint32_t i = 0; i < zxm_len(l, s); i++) {
        int32_t o = zxm_at(l, s, i);
        int64_t v = WHETSTONE_REFUSED;
        if (o >= 0 && zxm_get_str(l, o, "verdict", word, sizeof word) == ZXM_OK)
            v = verdict_of(word);
        if (v < 0) return ZXM_E_PROTOCOL;
        count[v]++;
    }
    /* the play.run answer is read; now zxvd's clock again */
    rc = clock_ms(l, &end);
    if (rc != ZXM_OK) return rc;
    *ms = end >= start ? end - start : 0; /* a clock that went back: 0 ms */
    r->timed_out = *ms > limit;
    if (r->timed_out) {
        *ms = limit;
        r->verdict = LPRES_STATE_NEITHER;
        r->passed = r->failed = r->held = 0;
        r->unknown = 1; /* the run itself, undecided */
        return ZXM_OK;
    }
    r->verdict = verdict;
    r->passed = count[LPRES_STATE_TRUE];
    r->failed = count[LPRES_STATE_FALSE];
    r->held = count[LPRES_STATE_BOTH];
    r->unknown = count[LPRES_STATE_NEITHER];
    return ZXM_OK;
}

int whetstone_run(whetstone_t *w, uint32_t target)
{
    int64_t ms[WHETSTONE_RETRIES_MAX + 1];
    whetstone_record_t r;
    if (!w->planned || target >= w->ntargets) return ZXM_E_ARG;
    const whetstone_target_t *t = &w->target[target];
    if (w->nrecords >= WHETSTONE_RECORDS || !whetstone_retry_ok(t->retries, t->timeout_s))
        return ZXM_E_STATE;
    memset(&r, 0, sizeof r);
    memcpy(r.target, t->id, bounded_len(t->id, WHETSTONE_NAME) + 1u);
    r.place = (int64_t) target;
    uint32_t tries = 0;
    do {
        int rc = one_try(w, t, &r, &ms[tries]);
        if (rc != ZXM_OK) return rc; /* nothing kept */
        tries++;
    } while (r.verdict == LPRES_STATE_NEITHER && (int64_t) tries <= t->retries);
    r.tries = (int64_t) tries;
    r.ms = whetstone_ms_total(ms, tries);
    int64_t results = whetstone_results_total(r.passed, r.failed, r.held);
    if (r.ms == WHETSTONE_REFUSED || results == WHETSTONE_REFUSED ||
        !whetstone_record_fits((int64_t) w->nrecords, r.place, r.verdict, r.passed, r.failed,
                               r.held, r.ms))
        return ZXM_E_STATE;
    w->record[w->nrecords++] = r;
    if (r.timed_out)
        snprintf(w->notice, sizeof w->notice,
                 "%s passed its timeout of %lld s after %lld tries: NEITHER, %lld ms.", r.target,
                 (long long) t->timeout_s, (long long) r.tries, (long long) r.ms);
    else
        snprintf(w->notice, sizeof w->notice,
                 "%s: %s in %lld ms (%lld tries), %lld passed, %lld failed, %lld held of %lld.",
                 r.target, VERDICT_NAME[r.verdict], (long long) r.ms, (long long) r.tries,
                 (long long) r.passed, (long long) r.failed, (long long) r.held,
                 (long long) results);
    return ZXM_OK;
}

/* ---- WM4 the verdict, WM5 clearing -------------------------------------------- */

int whetstone_summary(whetstone_t *w, whetstone_summary_t *out)
{
    int64_t v[WHETSTONE_RECORDS], ms[WHETSTONE_RECORDS];
    if (!out) return ZXM_E_ARG;
    whetstone_summary_t s;
    memset(&s, 0, sizeof s);
    for (uint32_t i = 0; i < w->nrecords; i++) {
        const whetstone_record_t *r = &w->record[i];
        v[i] = r->verdict;
        ms[i] = r->ms;
        s.passed_n += r->passed; /* at most 64 runs of 12 steps: no overflow */
        s.failed_n += r->failed;
        s.held_n += r->held;
        s.unknown_n += r->unknown;
    }
    s.runs = (int64_t) w->nrecords;
    s.verdict = whetstone_suite_verdict(v, w->nrecords);
    s.runs_true = whetstone_kind_count(v, w->nrecords, LPRES_STATE_TRUE);
    s.runs_false = whetstone_kind_count(v, w->nrecords, LPRES_STATE_FALSE);
    s.runs_both = whetstone_kind_count(v, w->nrecords, LPRES_STATE_BOTH);
    s.runs_neither = whetstone_kind_count(v, w->nrecords, LPRES_STATE_NEITHER);
    s.passed = whetstone_suite_passed(s.runs_true, s.runs_false, s.runs_both, s.runs_neither);
    s.ms = whetstone_ms_total(ms, w->nrecords);
    int64_t results = s.passed_n + s.failed_n + s.held_n + s.unknown_n;
    s.pass_x100 = results ? whetstone_pass_x100(s.passed_n, results) : WHETSTONE_REFUSED;
    if (s.verdict == WHETSTONE_REFUSED || s.ms == WHETSTONE_REFUSED ||
        s.passed != (s.verdict == LPRES_STATE_TRUE)) /* WR2 agrees with WR1 */
        return ZXM_E_STATE;
    *out = s;
    if (s.pass_x100 == WHETSTONE_REFUSED)
        snprintf(w->notice, sizeof w->notice, "%lld runs: %s, no results counted.",
                 (long long) s.runs, VERDICT_NAME[s.verdict]);
    else
        snprintf(w->notice, sizeof w->notice,
                 "%lld runs: %s (%lld TRUE, %lld FALSE, %lld held, %lld undecided); %lld of "
                 "%lld results passed (%lld.%02lld%%), %lld ms.",
                 (long long) s.runs, VERDICT_NAME[s.verdict], (long long) s.runs_true,
                 (long long) s.runs_false, (long long) s.runs_both, (long long) s.runs_neither,
                 (long long) s.passed_n, (long long) results, (long long) (s.pass_x100 / 100),
                 (long long) (s.pass_x100 % 100), (long long) s.ms);
    return ZXM_OK;
}

int whetstone_clear(whetstone_t *w, bool owner_confirmed)
{
    if (!owner_confirmed) return ZXM_E_STATE;
    uint32_t n = w->nrecords;
    memset(w->record, 0, sizeof w->record);
    w->nrecords = 0;
    snprintf(w->notice, sizeof w->notice, "%u runs cleared.", n);
    return ZXM_OK;
}
