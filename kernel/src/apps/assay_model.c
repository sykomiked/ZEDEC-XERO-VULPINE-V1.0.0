/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* assay_model.c — Assay, agent testing and scores (see assay_model.h).
 * Every run is counted aside and kept only when it is whole; a score is
 * advice and changes nothing in the swarm. */
#include <stdio.h>
#include <string.h>

#include "assay_model.h"
#include "../pce/pce_entry.h"

/* swarm.state's witness trit on an agent (swarm_ledger.h W3) */
enum { WIT_FALSE = 0, WIT_TRUE = 1, WIT_GLUT = 2 };

/* the index of each LPRES_STATE_* in a run's four counts (pce_prob's order) */
static uint32_t slot_of(int32_t lp)
{
    return lp == LPRES_STATE_TRUE    ? 0u
           : lp == LPRES_STATE_FALSE ? 1u
           : lp == LPRES_STATE_BOTH  ? 2u
                                     : 3u;
}

/* the length of s, counting no further than cap + 1 */
static uint32_t bounded_len(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    while (n <= cap && s[n]) n++;
    return n;
}

/* copy s into out (cap + 1 bytes); false when s is longer than cap */
static bool copy_text(char *out, const char *s, uint32_t cap)
{
    uint32_t n = bounded_len(s, cap);
    if (n > cap) return false;
    memcpy(out, s, n);
    out[n] = 0;
    return true;
}

void assay_init(assay_t *a, zxm_link_t *link)
{
    memset(a, 0, sizeof *a);
    a->link = link;
    a->bar_x100 = ASSAY_DEFAULT_BAR;
    a->max_refusals = ASSAY_DEFAULT_MAX_REFUSALS;
    a->min_runs = ASSAY_DEFAULT_MIN_RUNS;
    a->decide_num = 1, a->decide_den = 2;
    a->conflict_num = 1, a->conflict_den = 4;
    a->subject[ASSAY_COMPANION].id = -1;
    snprintf(a->subject[ASSAY_COMPANION].name, sizeof a->subject[0].name, "the companion");
    a->nsubjects = 1;
}

/* ---- AM2, AM3: the suite, the bar and the thresholds ---------------------------- */

int assay_set_bar(assay_t *a, int64_t bar_x100, int64_t max_refusals, int64_t min_runs)
{
    if (bar_x100 < 0 || bar_x100 > ASSAY_SHARE_MAX || max_refusals < 0 ||
        max_refusals > ASSAY_COUNT_MAX || min_runs < 1 || min_runs > ASSAY_COUNT_MAX)
        return ZXM_E_ARG;
    a->bar_x100 = bar_x100;
    a->max_refusals = max_refusals;
    a->min_runs = min_runs;
    snprintf(a->notice, sizeof a->notice,
             "The bar: %lld.%02lld%% passed, at most %lld refusals, at least %lld results.",
             (long long) (bar_x100 / 100), (long long) (bar_x100 % 100), (long long) max_refusals,
             (long long) min_runs);
    return ZXM_OK;
}

int assay_set_thresholds(assay_t *a, int64_t decide_num, int64_t decide_den, int64_t conflict_num,
                         int64_t conflict_den)
{
    if (!assay_fraction_ok(decide_num, decide_den) ||
        !assay_fraction_ok(conflict_num, conflict_den))
        return ZXM_E_ARG;
    a->decide_num = decide_num, a->decide_den = decide_den;
    a->conflict_num = conflict_num, a->conflict_den = conflict_den;
    snprintf(a->notice, sizeof a->notice, "A run passes at %lld/%lld; conflict at %lld/%lld.",
             (long long) decide_num, (long long) decide_den, (long long) conflict_num,
             (long long) conflict_den);
    return ZXM_OK;
}

int32_t assay_add_ask(assay_t *a, const char *prompt, const char *expect, const char *forbid)
{
    assay_test_t t;
    memset(&t, 0, sizeof t);
    if (a->ntests >= ASSAY_TESTS) return ZXM_E_STATE;
    if (!prompt || !expect || !forbid || !prompt[0] || (!expect[0] && !forbid[0])) return ZXM_E_ARG;
    if (!copy_text(t.text, prompt, ASSAY_PROMPT) || !copy_text(t.expect, expect, ASSAY_TEXT) ||
        !copy_text(t.forbid, forbid, ASSAY_TEXT))
        return ZXM_E_ARG;
    t.kind = ASSAY_TEST_ASK;
    uint32_t i = a->ntests;
    a->test[i] = t;
    a->ntests = i + 1u;
    snprintf(a->notice, sizeof a->notice, "Test %u: a prompt of %u bytes.", i,
             bounded_len(t.text, ASSAY_PROMPT));
    return (int32_t) i;
}

int32_t assay_add_play(assay_t *a, const char *template_id)
{
    assay_test_t t;
    memset(&t, 0, sizeof t);
    if (a->ntests >= ASSAY_TESTS) return ZXM_E_STATE;
    if (!template_id || !template_id[0] || !copy_text(t.text, template_id, ASSAY_TEXT))
        return ZXM_E_ARG;
    t.kind = ASSAY_TEST_PLAY;
    uint32_t i = a->ntests;
    a->test[i] = t;
    a->ntests = i + 1u;
    snprintf(a->notice, sizeof a->notice, "Test %u: the template %.63s.", i, t.text);
    return (int32_t) i;
}

/* ---- AM4: grading and runs -------------------------------------------------------- */

int32_t assay_grade(const char *answer, const char *expect, const char *forbid)
{
    if (!answer || !answer[0]) return LPRES_STATE_NEITHER;
    bool want = expect && expect[0], shun = forbid && forbid[0];
    bool has = want && strstr(answer, expect) != NULL;
    bool bad = shun && strstr(answer, forbid) != NULL;
    if (want && shun) {
        if (has && bad) return LPRES_STATE_BOTH;
        if (has) return LPRES_STATE_TRUE;
        if (bad) return LPRES_STATE_FALSE;
        return LPRES_STATE_NEITHER;
    }
    if (want) return has ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    if (shun) return bad ? LPRES_STATE_FALSE : LPRES_STATE_TRUE;
    return LPRES_STATE_NEITHER; /* a test with no text to look for decides nothing */
}

/* one run counted aside before it is kept */
typedef struct {
    assay_counts_t n[ASSAY_SUBJECTS];
    int64_t refused[ASSAY_SUBJECTS];
    bool ran[ASSAY_SUBJECTS];
    assay_entry_t e[ASSAY_SUBJECTS > ASSAY_TESTS ? ASSAY_SUBJECTS : ASSAY_TESTS];
    uint32_t ne;
} pending_t;

static void note(pending_t *p, uint32_t subject, int32_t from, int32_t test, int32_t lp,
                 bool refused)
{
    assay_entry_t *e = &p->e[p->ne++];
    memset(e, 0, sizeof *e);
    e->subject = subject;
    e->from = from;
    e->test = test;
    e->result = lp;
    e->refused = refused;
    p->n[subject].c[slot_of(lp)]++;
    p->refused[subject] += refused;
    p->ran[subject] = true;
}

/* AM4: every total stays inside ASSAY_COUNT_MAX, checked before anything is kept */
static bool fits(const assay_t *a, const pending_t *p)
{
    for (uint32_t s = 0; s < a->nsubjects; s++) {
        if (!p->ran[s]) continue;
        const assay_subject_t *x = &a->subject[s];
        int64_t total = 0, add = 0;
        for (uint32_t k = 0; k < 4u; k++) total += x->all.c[k], add += p->n[s].c[k];
        if (total > ASSAY_COUNT_MAX - add || x->refusals > ASSAY_COUNT_MAX - p->refused[s] ||
            x->runs >= ASSAY_COUNT_MAX)
            return false;
    }
    return true;
}

/* AM5: one entry into the ring, the oldest dropped when it is full */
static void log_push(assay_t *a, const assay_entry_t *e)
{
    if (a->head - a->tail >= ASSAY_LOG) {
        a->tail++;
        a->dropped++;
    }
    assay_entry_t *slot = &a->log[a->head % ASSAY_LOG];
    *slot = *e;
    slot->seq = a->head++;
}

static void commit(assay_t *a, const pending_t *p)
{
    for (uint32_t s = 0; s < a->nsubjects; s++) {
        if (!p->ran[s]) continue;
        assay_subject_t *x = &a->subject[s];
        x->prev = x->cur;
        x->cur = p->n[s];
        for (uint32_t k = 0; k < 4u; k++) x->all.c[k] += p->n[s].c[k];
        x->refusals += p->refused[s];
        x->runs++;
    }
    for (uint32_t i = 0; i < p->ne; i++) log_push(a, &p->e[i]);
}

/* one test put to the companion: the result, or a negative ZXM_E_*; an
 * asked answer goes to `said`, kept only when the whole run is */
static int32_t put_test(assay_t *a, const assay_test_t *t, bool *refused, char *said)
{
    zxm_link_t *l = a->link;
    char got[ZXM_ANSWER_MAX + 1];
    *refused = false;
    zxm_begin(l, t->kind == ASSAY_TEST_ASK ? "swarm.ask" : "play.run");
    zxm_p_str(l, t->kind == ASSAY_TEST_ASK ? "prompt" : "template", t->text);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE) { /* zxvd refused it: the budget gate, or the template */
        *refused = true;
        return LPRES_STATE_NEITHER;
    }
    if (rc != ZXM_OK) return rc;
    if (t->kind == ASSAY_TEST_ASK) {
        if (zxm_get_str(l, l->result, "answer", got, sizeof got) != ZXM_OK) return ZXM_E_PROTOCOL;
        uint32_t n = bounded_len(got, ASSAY_ANSWER);
        if (n > ASSAY_ANSWER) n = zxm_utf8_cut(got, ASSAY_ANSWER);
        memcpy(said, got, n);
        said[n] = 0;
        return assay_grade(got, t->expect, t->forbid);
    }
    if (zxm_get_str(l, l->result, "verdict", got, sizeof got) != ZXM_OK) return ZXM_E_PROTOCOL;
    if (!strcmp(got, "TRUE")) return LPRES_STATE_TRUE;
    if (!strcmp(got, "FALSE")) return LPRES_STATE_FALSE;
    if (!strcmp(got, "BOTH")) return LPRES_STATE_BOTH;
    if (!strcmp(got, "NEITHER")) return LPRES_STATE_NEITHER;
    return ZXM_E_PROTOCOL;
}

int assay_run_suite(assay_t *a)
{
    static pending_t p;
    char said[ASSAY_ANSWER + 1];
    if (a->ntests == 0) return ZXM_E_STATE;
    memset(&p, 0, sizeof p);
    memcpy(said, a->answer, sizeof said);
    for (uint32_t i = 0; i < a->ntests; i++) {
        bool refused = false;
        int32_t lp = put_test(a, &a->test[i], &refused, said);
        if (lp < 0) return lp; /* nothing kept */
        note(&p, ASSAY_COMPANION,
             a->test[i].kind == ASSAY_TEST_ASK ? ASSAY_FROM_ASK : ASSAY_FROM_PLAY, (int32_t) i, lp,
             refused);
    }
    if (!fits(a, &p)) return ZXM_E_STATE;
    commit(a, &p);
    memcpy(a->answer, said, sizeof said);
    const assay_counts_t *c = &a->subject[ASSAY_COMPANION].cur;
    snprintf(a->notice, sizeof a->notice,
             "The companion's run: %lld passed, %lld failed, %lld contested, %lld undecided, "
             "%lld refused.",
             (long long) c->c[0], (long long) c->c[1], (long long) c->c[2], (long long) c->c[3],
             (long long) p.refused[ASSAY_COMPANION]);
    return ZXM_OK;
}

/* one agent object of swarm.state: its id, name and witness trit */
static int read_agent(const zxm_link_t *l, int32_t o, int64_t *id, char *name, int64_t *wit)
{
    static char full[ZXM_ANSWER_MAX + 1];
    if (o < 0 || l->tok[o].type != W4_J_OBJ) return ZXM_E_PROTOCOL;
    if (zxm_get_i64(l, o, "id", id) != ZXM_OK || *id < 0 ||
        zxm_get_str(l, o, "name", full, sizeof full) != ZXM_OK ||
        zxm_get_i64(l, o, "witness", wit) != ZXM_OK || *wit < WIT_FALSE || *wit > WIT_GLUT)
        return ZXM_E_PROTOCOL;
    uint32_t n = bounded_len(full, ASSAY_TEXT); /* a long name is cut on a character */
    if (n > ASSAY_TEXT) n = zxm_utf8_cut(full, ASSAY_TEXT);
    memcpy(name, full, n);
    name[n] = 0;
    return ZXM_OK;
}

int assay_round(assay_t *a)
{
    static pending_t p;
    zxm_link_t *l = a->link;
    char audit[ASSAY_TEXT + 1];
    int64_t id[ASSAY_SUBJECTS], wit[ASSAY_SUBJECTS];
    char name[ASSAY_SUBJECTS][ASSAY_TEXT + 1];
    uint32_t n = 0, total;
    zxm_begin(l, "swarm.state");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(l, l->result, "audit", audit, sizeof audit) != ZXM_OK) return ZXM_E_PROTOCOL;
    int32_t arr = zxm_get(l, l->result, "agents");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    total = zxm_len(l, arr);
    /* check everything before anything is kept */
    for (uint32_t i = 0; i < total; i++) {
        int64_t ai, aw;
        char an[ASSAY_TEXT + 1];
        rc = read_agent(l, zxm_at(l, arr, i), &ai, an, &aw);
        if (rc != ZXM_OK) return rc;
        if (n < ASSAY_SUBJECTS - 1u) {
            id[n] = ai, wit[n] = aw;
            memcpy(name[n], an, sizeof an);
            n++;
        }
    }
    /* the table: agents already known keep their row; new ones take free rows */
    uint32_t row[ASSAY_SUBJECTS], nsub = a->nsubjects;
    uint64_t unscored = 0;
    for (uint32_t i = 0; i < n; i++) {
        row[i] = 0;
        for (uint32_t s = 1; s < nsub; s++)
            if (a->subject[s].id == id[i]) row[i] = s;
        if (row[i] == 0 && nsub < ASSAY_SUBJECTS) row[i] = nsub++;
        if (row[i] == 0) unscored++;
    }
    unscored += total - n;
    memset(&p, 0, sizeof p);
    bool audit_on = strcmp(audit, "off") != 0;
    for (uint32_t s = 1; s < nsub; s++) {
        int32_t lp = LPRES_STATE_NEITHER;
        for (uint32_t i = 0; i < n; i++)
            if (row[i] == s && audit_on)
                lp = wit[i] == WIT_TRUE    ? LPRES_STATE_TRUE
                     : wit[i] == WIT_FALSE ? LPRES_STATE_FALSE
                                           : LPRES_STATE_BOTH;
        note(&p, s, ASSAY_FROM_AUDIT, -1, lp, false);
    }
    uint32_t was = a->nsubjects;
    a->nsubjects = nsub; /* fits() and commit() read the new rows, still zero */
    for (uint32_t i = 0; i < n; i++)
        if (row[i] >= was) {
            a->subject[row[i]].id = id[i];
            memcpy(a->subject[row[i]].name, name[i], sizeof name[i]);
        }
    if (!fits(a, &p)) {
        for (uint32_t s = was; s < nsub; s++) memset(&a->subject[s], 0, sizeof a->subject[s]);
        a->nsubjects = was;
        return ZXM_E_STATE;
    }
    for (uint32_t s = 1; s < nsub; s++) {
        a->subject[s].present = false;
        for (uint32_t i = 0; i < n; i++)
            if (row[i] == s) a->subject[s].present = true;
    }
    commit(a, &p);
    memcpy(a->audit, audit, sizeof audit);
    a->unscored = unscored;
    snprintf(a->notice, sizeof a->notice,
             "A round of %u agents (audit %s)%s; %llu not scored: the table holds %u.", total,
             audit, audit_on ? "" : ": no evidence", (unsigned long long) unscored,
             ASSAY_SUBJECTS - 1u);
    return ZXM_OK;
}

/* ---- AM5: the log ------------------------------------------------------------------- */

uint32_t assay_log_count(const assay_t *a)
{
    return (uint32_t) (a->head - a->tail);
}

const assay_entry_t *assay_log_at(const assay_t *a, uint32_t i)
{
    if (!a || i >= assay_log_count(a)) return NULL;
    return &a->log[(a->tail + i) % ASSAY_LOG];
}

/* ---- AM6: scores -------------------------------------------------------------------- */

/* AY4: pce_prob.verdict on four counts, an LPRES_STATE_* or a negative ZXM_E_* */
static int32_t decide(const assay_t *a, const int64_t m[4])
{
    const int64_t th[4] = {a->decide_num, a->decide_den, a->conflict_num, a->conflict_den};
    const pce_arg_t in[2] = {{"m", m, 4u}, {"th", th, 4u}};
    pce_num_t out;
    uint32_t n = 0;
    if (pce_entry_call("prob.verdict", in, 2u, &out, 1u, &n) != PCE_OK || n != 1u || out.den != 0 ||
        out.num < LPRES_STATE_NEITHER || out.num > LPRES_STATE_BOTH)
        return ZXM_E_ARG;
    return (int32_t) out.num;
}

/* AY5: pce_prob.join of two runs, back to whole counts over the product of
 * their sizes (every joined mass is a sum of products over that product,
 * so its reduced denominator divides it), then decided as AY4 */
static int32_t repeated(const assay_t *a, const assay_subject_t *x)
{
    int64_t sp = 0, sc = 0, m[4];
    for (uint32_t k = 0; k < 4u; k++) sp += x->prev.c[k], sc += x->cur.c[k];
    if (x->runs < 2 || sp == 0 || sc == 0) return LPRES_STATE_NEITHER;
    if (sp > INT64_MAX / sc) return ZXM_E_ARG;
    const pce_arg_t in[2] = {{"a", x->prev.c, 4u}, {"b", x->cur.c, 4u}};
    pce_num_t out[4];
    uint32_t n = 0;
    if (pce_entry_call("prob.join", in, 2u, out, 4u, &n) != PCE_OK || n != 4u) return ZXM_E_ARG;
    int64_t whole = sp * sc;
    for (uint32_t k = 0; k < 4u; k++) {
        if (out[k].den <= 0 || out[k].num < 0 || whole % out[k].den != 0) return ZXM_E_ARG;
        int64_t f = whole / out[k].den; /* den <= whole, so f >= 1 */
        if (out[k].num > INT64_MAX / f) return ZXM_E_ARG;
        m[k] = out[k].num * f;
    }
    return decide(a, m);
}

int assay_score(assay_t *a, assay_score_t *out, uint32_t cap)
{
    int64_t share[ASSAY_SUBJECTS];
    uint32_t who[ASSAY_SUBJECTS], ns = 0;
    if (!out || cap < a->nsubjects) return ZXM_E_ARG;
    for (uint32_t s = 0; s < a->nsubjects; s++) {
        const assay_subject_t *x = &a->subject[s];
        assay_score_t *o = &out[s];
        memset(o, 0, sizeof *o);
        for (uint32_t k = 0; k < 4u; k++) o->results += x->all.c[k];
        o->refusals = x->refusals;
        o->share_x100 = -1;
        o->verdict = o->repeat = LPRES_STATE_NEITHER;
        if (o->results > 0) {
            o->share_x100 = assay_pass_share_x100(x->all.c[0], o->results);
            if (o->share_x100 == ASSAY_REFUSED) return ZXM_E_STATE; /* kept totals fit AY1 */
            o->meets = assay_meets_bar(o->share_x100 - a->bar_x100, a->max_refusals - o->refusals,
                                       o->results - a->min_runs);
            share[ns] = o->share_x100;
            who[ns++] = s;
        }
        int32_t v = decide(a, x->cur.c), r = repeated(a, x);
        if (v < 0 || r < 0) return ZXM_E_STATE;
        o->verdict = v;
        o->repeat = r;
    }
    int64_t best = -1;
    uint32_t at = 0, met = 0;
    for (uint32_t i = 0; i < ns; i++) {
        out[who[i]].rank = assay_score_rank(share, ns, i);
        met += out[who[i]].meets;
        if (share[i] > best) best = share[i], at = who[i];
    }
    if (ns == 0)
        snprintf(a->notice, sizeof a->notice, "No results yet: run the suite or a round.");
    else
        snprintf(a->notice, sizeof a->notice,
                 "%u of %u scored meet the bar; %s leads at %lld.%02lld%%. Advice only: no "
                 "budget, standing or share changes.",
                 met, ns, a->subject[at].name, (long long) (best / 100), (long long) (best % 100));
    return ZXM_OK;
}
