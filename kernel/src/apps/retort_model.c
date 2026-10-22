/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* retort_model.c — Retort, run and compare models (see retort_model.h).
 * Every run is checked against the owner's limits before anything is
 * asked, and every record made to fit before it is kept. */
#include <stdio.h>
#include <string.h>

#include "retort_model.h"

static const char *const KIND_NAME[3] = {"model", "template", "function"};

/* the length of s, counting no further than cap + 1 */
static uint32_t bounded_len(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    while (n <= cap && s[n]) n++;
    return n;
}

/* a CID as text: 'z' + base58btc (aipi.execute's pattern), at most 127 bytes */
static bool cid_ok(const char *s)
{
    static const char B58[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    uint32_t n = bounded_len(s, RETORT_CID);
    if (n < 2 || n > RETORT_CID || s[0] != 'z') return false;
    for (uint32_t i = 1; i < n; i++)
        if (!strchr(B58, s[i])) return false;
    return true;
}

/* "-?[0-9]+" into *v, false when it is not one or passes 64 bits */
static bool parse_i64(const char *s, int64_t *v)
{
    bool neg = s[0] == '-';
    uint32_t i = neg ? 1u : 0u;
    uint64_t x = 0, lim = neg ? (uint64_t) INT64_MAX + 1u : (uint64_t) INT64_MAX;
    if (!s[i]) return false;
    for (; s[i]; i++) {
        if (s[i] < '0' || s[i] > '9') return false;
        uint64_t d = (uint64_t) (s[i] - '0');
        if (x > (lim - d) / 10u) return false;
        x = x * 10u + d;
    }
    *v = neg ? (x == (uint64_t) INT64_MAX + 1u ? INT64_MIN : -(int64_t) x) : (int64_t) x;
    return true;
}

/* ---- RM1-RM3 ------------------------------------------------------------------- */

void retort_init(retort_t *t, zxm_link_t *link, retort_clock_fn clock, void *clock_ctx)
{
    memset(t, 0, sizeof *t);
    t->link = link;
    t->clock = clock;
    t->clock_ctx = clock_ctx;
    t->lim_prompt = RETORT_DEFAULT_PROMPT;
    t->lim_fuel = RETORT_DEFAULT_FUEL;
    t->lim_seconds = RETORT_DEFAULT_SECONDS;
}

int retort_read_slot(retort_t *t)
{
    zxm_link_t *l = t->link;
    bool loaded = false, gen = false;
    char name[RETORT_TEXT + 1], arch[RETORT_TEXT + 1];
    int64_t layers = 0, vocab = 0;
    zxm_begin(l, "model.info");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_bool(l, l->result, "loaded", &loaded) != ZXM_OK ||
        zxm_get_str(l, l->result, "name", name, sizeof name) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    /* the others are read when given: a slot with no model has no arch */
    if (zxm_get(l, l->result, "can_generate") >= 0 &&
        zxm_get_bool(l, l->result, "can_generate", &gen) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    arch[0] = 0;
    if (zxm_get(l, l->result, "arch") >= 0 &&
        zxm_get_str(l, l->result, "arch", arch, sizeof arch) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    if ((zxm_get(l, l->result, "n_layers") >= 0 &&
         zxm_get_i64(l, l->result, "n_layers", &layers) != ZXM_OK) ||
        (zxm_get(l, l->result, "n_vocab") >= 0 &&
         zxm_get_i64(l, l->result, "n_vocab", &vocab) != ZXM_OK) ||
        layers < 0 || vocab < 0)
        return ZXM_E_PROTOCOL;
    t->known = true;
    t->loaded = loaded;
    t->can_generate = loaded && gen;
    memcpy(t->name, name, sizeof name);
    memcpy(t->arch, arch, sizeof arch);
    t->layers = layers;
    t->vocab = vocab;
    if (t->can_generate)
        snprintf(t->notice, sizeof t->notice, "The slot holds %s (%s, %lld layers, %lld words).",
                 name[0] ? name : "a model", arch[0] ? arch : "unknown arch", (long long) layers,
                 (long long) vocab);
    else
        snprintf(t->notice, sizeof t->notice,
                 "No model can answer in the slot: swarm.ask reads shorthand back.");
    return ZXM_OK;
}

int retort_set_limits(retort_t *t, int64_t prompt, int64_t fuel, int64_t seconds)
{
    if (!retort_run_limits_ok(prompt, fuel, seconds)) return ZXM_E_ARG;
    t->lim_prompt = prompt;
    t->lim_fuel = fuel;
    t->lim_seconds = seconds;
    snprintf(t->notice, sizeof t->notice,
             "Each run: at most %lld prompt bytes, %lld fuel and %lld seconds.", (long long) prompt,
             (long long) fuel, (long long) seconds);
    return ZXM_OK;
}

int32_t retort_add(retort_t *t, int64_t kind, const char *name, int64_t fuel)
{
    retort_contestant_t c;
    memset(&c, 0, sizeof c);
    if (t->ncontestants >= RETORT_CONTESTANTS) return ZXM_E_STATE;
    if (kind == RETORT_KIND_ASK) {
        if (!t->known || fuel != 0) return t->known ? ZXM_E_ARG : ZXM_E_STATE;
        for (uint32_t i = 0; i < t->ncontestants; i++)
            if (t->contestant[i].kind == RETORT_KIND_ASK) return ZXM_E_STATE;
        snprintf(c.name, sizeof c.name, "%s", t->name[0] ? t->name : "the slot");
    } else if (kind == RETORT_KIND_PLAY) {
        if (!name || fuel != 0) return ZXM_E_ARG;
        uint32_t n = bounded_len(name, RETORT_TEXT);
        if (n == 0 || n > RETORT_TEXT) return ZXM_E_ARG;
        memcpy(c.name, name, n + 1u);
    } else if (kind == RETORT_KIND_AIPI) {
        if (!name || !cid_ok(name) || fuel < 1 || fuel > RETORT_FUEL_MAX) return ZXM_E_ARG;
        memcpy(c.name, name, bounded_len(name, RETORT_CID) + 1u);
        c.fuel = fuel;
    } else {
        return ZXM_E_ARG;
    }
    c.kind = kind;
    uint32_t i = t->ncontestants;
    t->contestant[i] = c;
    t->ncontestants = i + 1u;
    snprintf(t->notice, sizeof t->notice, "Contestant %u: the %s %s.", i, KIND_NAME[kind], c.name);
    return (int32_t) i;
}

/* ---- RM4, RM5: runs ---------------------------------------------------------- */

/* RM2, RM4: whether contestant `who` of `kind` may run `bytes` now */
static int run_allowed(const retort_t *t, uint32_t who, int64_t kind, int64_t bytes)
{
    if (who >= t->ncontestants || t->contestant[who].kind != kind) return ZXM_E_ARG;
    if (t->nruns >= RETORT_RUNS || !t->clock) return ZXM_E_STATE;
    if (bytes < 1 || bytes > t->lim_prompt) return ZXM_E_ARG;
    if (t->contestant[who].fuel > t->lim_fuel) return ZXM_E_ARG;
    return ZXM_OK;
}

/* RM4: time it, read its energy, make its record fit, keep it */
static int keep_run(retort_t *t, uint32_t who, int64_t bytes, int64_t start, int64_t score)
{
    zxm_link_t *l = t->link;
    int64_t end = t->clock(t->clock_ctx), uj = 0;
    int64_t ms = end >= start ? end - start : 0; /* a clock that went back: 0 ms */
    bool over = ms > t->lim_seconds * 1000;
    zxm_begin(l, "power.status");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_i64(l, l->result, "total_uj", &uj) != ZXM_OK || uj < 0 || uj > RETORT_UJ_MAX)
        return ZXM_E_PROTOCOL;
    retort_run_t r;
    r.model = (int64_t) who;
    r.kind = t->contestant[who].kind;
    r.prompt = bytes;
    r.fuel = t->contestant[who].fuel;
    r.ms = ms > RETORT_MS_KEPT ? RETORT_MS_KEPT : ms;
    r.score = over ? 0 : score;
    r.uj = uj;
    r.over = over;
    if (!retort_run_record_fits((int64_t) t->nruns, r.model, r.kind, r.prompt, r.fuel, r.ms,
                                r.score, r.uj))
        return ZXM_E_PROTOCOL;
    t->run[t->nruns++] = r;
    return ZXM_OK;
}

static void say_run(retort_t *t, const char *what)
{
    const retort_run_t *r = &t->run[t->nruns - 1u];
    const char *name = t->contestant[r->model].name;
    if (r->over)
        snprintf(t->notice, sizeof t->notice, "%s ran past %lld seconds: graded 0 (%s).", name,
                 (long long) t->lim_seconds, what);
    else if (r->score == RETORT_UNGRADED)
        snprintf(t->notice, sizeof t->notice, "%s: %s in %lld ms, %lld uJ; it waits for a grade.",
                 name, what, (long long) r->ms, (long long) r->uj);
    else
        snprintf(t->notice, sizeof t->notice, "%s: %s in %lld ms, %lld uJ, %lld points.", name,
                 what, (long long) r->ms, (long long) r->uj, (long long) r->score);
}

int retort_ask(retort_t *t, uint32_t who, const char *prompt)
{
    zxm_link_t *l = t->link;
    char got[ZXM_ANSWER_MAX + 1];
    if (!prompt) return ZXM_E_ARG;
    int64_t bytes = (int64_t) bounded_len(prompt, (uint32_t) RETORT_PROMPT_MAX);
    int rc = run_allowed(t, who, RETORT_KIND_ASK, bytes);
    if (rc != ZXM_OK) return rc;
    int64_t start = t->clock(t->clock_ctx);
    zxm_begin(l, "swarm.ask");
    zxm_p_str(l, "prompt", prompt);
    rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(l, l->result, "answer", got, sizeof got) != ZXM_OK) return ZXM_E_PROTOCOL;
    rc = keep_run(t, who, bytes, start, RETORT_UNGRADED);
    if (rc != ZXM_OK) return rc;
    uint32_t n = bounded_len(got, RETORT_ANSWER);
    if (n > RETORT_ANSWER) n = zxm_utf8_cut(got, RETORT_ANSWER);
    memcpy(t->answer, got, n);
    t->answer[n] = 0;
    say_run(t, "answered");
    return ZXM_OK;
}

int retort_play(retort_t *t, uint32_t who)
{
    zxm_link_t *l = t->link;
    char verdict[RETORT_TEXT + 1];
    if (who >= t->ncontestants) return ZXM_E_ARG;
    const char *id = t->contestant[who].name;
    int64_t bytes = (int64_t) bounded_len(id, RETORT_CID);
    int rc = run_allowed(t, who, RETORT_KIND_PLAY, bytes);
    if (rc != ZXM_OK) return rc;
    int64_t start = t->clock(t->clock_ctx), score;
    zxm_begin(l, "play.run");
    zxm_p_str(l, "template", id);
    rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(l, l->result, "verdict", verdict, sizeof verdict) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    if (!strcmp(verdict, "TRUE"))
        score = RETORT_SCORE_MAX;
    else if (!strcmp(verdict, "FALSE"))
        score = 0;
    else if (!strcmp(verdict, "BOTH") || !strcmp(verdict, "NEITHER"))
        score = RETORT_UNGRADED; /* held for a person (RM5) */
    else
        return ZXM_E_PROTOCOL;
    rc = keep_run(t, who, bytes, start, score);
    if (rc != ZXM_OK) return rc;
    t->answer[0] = 0;
    say_run(t, verdict);
    return ZXM_OK;
}

int retort_execute(retort_t *t, uint32_t who, const int64_t *args, uint32_t nargs, int64_t expect)
{
    zxm_link_t *l = t->link;
    char list[RETORT_ARGS * 21u + 3u], params[sizeof list + RETORT_CID + 32u];
    char res[32];
    uint32_t k = 0;
    if (who >= t->ncontestants || nargs > RETORT_ARGS || (nargs && !args)) return ZXM_E_ARG;
    list[k++] = '[';
    for (uint32_t i = 0; i < nargs; i++)
        k += (uint32_t) snprintf(list + k, sizeof list - k, "%s%lld", i ? "," : "",
                                 (long long) args[i]);
    list[k++] = ']';
    list[k] = 0;
    int rc = run_allowed(t, who, RETORT_KIND_AIPI, (int64_t) k);
    if (rc != ZXM_OK) return rc;
    int n = snprintf(params, sizeof params, "{\"function\":\"%s\",\"args\":%s}",
                     t->contestant[who].name, list);
    int64_t start = t->clock(t->clock_ctx), got = 0, score = 0;
    zxm_begin(l, "aipi.execute");
    zxm_p_raw(l, params, (uint32_t) n);
    rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t r = zxm_get(l, l->result, "result");
    if (r < 0) return ZXM_E_PROTOCOL;
    if (l->tok[r].type == W4_J_STR) {
        if (zxm_get_str(l, l->result, "result", res, sizeof res) != ZXM_OK || !parse_i64(res, &got))
            return ZXM_E_PROTOCOL;
        score = got == expect ? RETORT_SCORE_MAX : 0;
    } else if (l->tok[r].type != W4_J_NULL) {
        return ZXM_E_PROTOCOL;
    }
    rc = keep_run(t, who, (int64_t) k, start, score);
    if (rc != ZXM_OK) return rc;
    t->answer[0] = 0;
    say_run(t, l->tok[r].type == W4_J_NULL ? "no result" : "a result");
    return ZXM_OK;
}

int retort_grade(retort_t *t, uint32_t run, int64_t score)
{
    if (run >= t->nruns || t->run[run].score != RETORT_UNGRADED) return ZXM_E_STATE;
    if (!retort_score_ok(score)) return ZXM_E_ARG;
    t->run[run].score = score;
    snprintf(t->notice, sizeof t->notice, "Run %u of %s: %lld points.", run,
             t->contestant[t->run[run].model].name, (long long) score);
    return ZXM_OK;
}

/* ---- RM6: the comparison ---------------------------------------------------- */

int retort_compare(retort_t *t, retort_standing_t *out, uint32_t cap)
{
    int64_t model[RETORT_RUNS], score[RETORT_RUNS], whole = 0;
    uint32_t g = 0;
    if (!out || cap < t->ncontestants) return ZXM_E_ARG;
    for (uint32_t i = 0; i < t->nruns; i++)
        if (t->run[i].score != RETORT_UNGRADED) {
            model[g] = t->run[i].model;
            score[g] = t->run[i].score;
            g++;
        }
    for (uint32_t c = 0; c < t->ncontestants; c++) {
        retort_standing_t *s = &out[c];
        int64_t fast = RETORT_LATENCY_MAX, slow = 0, uj = 0;
        bool timed = false, uj_ok = true;
        memset(s, 0, sizeof *s);
        s->total = retort_model_score_total(model, (int64_t) c, score, g);
        if (s->total == RETORT_REFUSED) return ZXM_E_STATE; /* every kept grade is 0..100 */
        whole += s->total;
        for (uint32_t i = 0; i < t->nruns; i++) {
            const retort_run_t *r = &t->run[i];
            if (r->model != (int64_t) c) continue;
            s->runs++;
            s->graded += r->score != RETORT_UNGRADED;
            if (uj_ok && uj > RETORT_UJ_MAX - r->uj) uj_ok = false;
            if (uj_ok) uj += r->uj;
            if (r->over) continue;
            fast = retort_latency_fastest(fast, r->ms);
            slow = retort_latency_slowest(slow, r->ms);
            timed = true;
        }
        s->fastest = timed ? fast : -1;
        s->slowest = timed ? slow : -1;
        if (s->runs == 0 || !uj_ok ||
            !retort_energy_per_answer(uj, s->runs, &s->uj_num, &s->uj_den))
            s->uj_num = s->uj_den = 0;
    }
    int64_t best = -1, at = -1;
    for (uint32_t c = 0; c < t->ncontestants; c++) {
        out[c].share_x100 = retort_share_x100(out[c].total, whole);
        if (out[c].total > best) best = out[c].total, at = (int64_t) c;
    }
    if (at < 0)
        snprintf(t->notice, sizeof t->notice, "No contestants yet.");
    else if (whole == 0)
        snprintf(t->notice, sizeof t->notice, "No points given yet: grade a run to compare.");
    else
        snprintf(t->notice, sizeof t->notice, "%s leads with %lld of %lld points (%lld.%02lld%%).",
                 t->contestant[at].name, (long long) best, (long long) whole,
                 (long long) (out[at].share_x100 / 100), (long long) (out[at].share_x100 % 100));
    return ZXM_OK;
}

bool retort_record_ok(const retort_t *t, uint32_t i)
{
    if (!t || i >= t->nruns) return false;
    const retort_run_t *r = &t->run[i];
    return retort_run_record_fits((int64_t) i, r->model, r->kind, r->prompt, r->fuel, r->ms,
                                  r->score, r->uj);
}
