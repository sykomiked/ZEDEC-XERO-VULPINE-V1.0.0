/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* colonnade_model.c — Colonnade, the language pillars (see colonnade_model.h).
 * Every run is checked against its fuel before anything is sent, and every
 * draft from the swarm is kept apart until the owner takes it. */
#include <stdio.h>
#include <string.h>

#include "colonnade_model.h"

/* CL5: the index, from LANGUAGE_FIT.tsv: each language whose runtime column
 * is a pillar or a Lingua language, by its catalogue name, its runtime and
 * its twin, with its place (line among the 108 languages) and pillar. */
static const struct {
    const char *name;
    int64_t place, pillar;
} LANGS[] = {
    {"FORTRAN", 6, COLONNADE_TREBAGO},
    {"trebago", 6, COLONNADE_TREBAGO},
    {"LISP", 8, COLONNADE_LINGUA},
    {"lisp", 8, COLONNADE_LINGUA},
    {"COBOL", 9, COLONNADE_SUTRA},
    {"sutra", 9, COLONNADE_SUTRA},
    {"treaty", 9, COLONNADE_SUTRA}, /* the Playground's treaty Sutra */
    {"ALGOL 60", 13, COLONNADE_LINGUA},
    {"algol60", 13, COLONNADE_LINGUA},
    {"APL", 14, COLONNADE_KRON},
    {"kron", 14, COLONNADE_KRON},
    {"BASIC", 19, COLONNADE_LINGUA},
    {"basic", 19, COLONNADE_LINGUA},
    {"Hatchling", 19, COLONNADE_LINGUA}, /* BASIC's twin */
    {"MUMPS", 24, COLONNADE_MYRA},
    {"myra", 24, COLONNADE_MYRA},
    {"Logo", 26, COLONNADE_LINGUA},
    {"logo", 26, COLONNADE_LINGUA},
    {"Forth", 30, COLONNADE_LINGUA},
    {"forth", 30, COLONNADE_LINGUA},
    {"Cairndrake", 30, COLONNADE_LINGUA}, /* Forth's twin */
    {"Pascal", 31, COLONNADE_LINGUA},
    {"pascal", 31, COLONNADE_LINGUA},
    {"Scheme", 37, COLONNADE_LINGUA},
    {"scheme", 37, COLONNADE_LINGUA},
    {"Ada", 48, COLONNADE_EVE},
    {"eve", 48, COLONNADE_EVE},
    {"J", 68, COLONNADE_KRON},
    {"K", 73, COLONNADE_KRON},
};

static const char *const PILLAR_NAME[COLONNADE_PILLARS] = {"Lingua", "Sutra", "Trebago",
                                                           "Eve",    "KRON",  "MYRA"};
static const char *const VERDICT_NAME[4] = {"NEITHER", "TRUE", "FALSE", "BOTH"};

/* the length of s, counting no further than cap + 1 */
static uint32_t bounded_len(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    while (n <= cap && s[n]) n++;
    return n;
}

/* a file name as play.run takes it: 1..63 bytes, no '/' */
static bool name_ok(const char *s)
{
    uint32_t n = bounded_len(s, COLONNADE_NAME);
    if (n == 0 || n > COLONNADE_NAME) return false;
    for (uint32_t i = 0; i < n; i++)
        if (s[i] == '/' || s[i] == '\\' || (unsigned char) s[i] < 0x20u) return false;
    return true;
}

static int64_t verdict_of(const char *w)
{
    for (int64_t v = 0; v < 4; v++)
        if (!strcmp(w, VERDICT_NAME[v])) return v;
    return COLONNADE_REFUSED;
}

int64_t colonnade_pillar_of(const char *lang, int64_t *place)
{
    if (!lang) return COLONNADE_REFUSED;
    for (uint32_t i = 0; i < sizeof LANGS / sizeof LANGS[0]; i++)
        if (!strcmp(lang, LANGS[i].name)) {
            if (!colonnade_pillar_index_fits(LANGS[i].place, LANGS[i].pillar))
                return COLONNADE_REFUSED;
            if (place) *place = LANGS[i].place;
            return LANGS[i].pillar;
        }
    return COLONNADE_REFUSED;
}

void colonnade_init(colonnade_t *c, zxm_link_t *link, colonnade_clock_fn clock, void *clock_ctx)
{
    memset(c, 0, sizeof *c);
    c->link = link;
    c->clock = clock;
    c->clock_ctx = clock_ctx;
    c->edit_tmpl = -1;
    c->fuel_max = COLONNADE_DEFAULT_FUEL;
}

/* ---- CM1 the catalogue -------------------------------------------------------- */

/* a string member cut on a character to cap bytes; false when it is missing */
static bool get_cut(const zxm_link_t *l, int32_t obj, const char *key, char *out, uint32_t cap)
{
    char tmp[512];
    int32_t t = zxm_get(l, obj, key);
    if (t < 0 || l->tok[t].type != W4_J_STR) return false;
    if (zxm_get_str(l, obj, key, tmp, sizeof tmp) != ZXM_OK) return false;
    uint32_t n = bounded_len(tmp, cap);
    if (n > cap) n = zxm_utf8_cut(tmp, cap);
    memcpy(out, tmp, n);
    out[n] = 0;
    return true;
}

int colonnade_catalogue(colonnade_t *c)
{
    static colonnade_template_t got[COLONNADE_TEMPLATES];
    zxm_link_t *l = c->link;
    zxm_begin(l, "play.catalogue");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (l->tok[l->result].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t all = zxm_len(l, l->result), n = all < COLONNADE_TEMPLATES ? all : COLONNADE_TEMPLATES;
    for (uint32_t i = 0; i < n; i++) {
        colonnade_template_t *t = &got[i];
        int32_t o = zxm_at(l, l->result, i);
        memset(t, 0, sizeof *t);
        if (o < 0 || l->tok[o].type != W4_J_OBJ ||
            zxm_get_str(l, o, "id", t->id, sizeof t->id) != ZXM_OK || !name_ok(t->id) ||
            zxm_get_str(l, o, "lang", t->lang, sizeof t->lang) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        if (!get_cut(l, o, "title", t->title, COLONNADE_TITLE)) t->title[0] = 0;
        if (!get_cut(l, o, "safety", t->safety, COLONNADE_WORD)) t->safety[0] = 0;
        int32_t s = zxm_get(l, o, "steps");
        if (s < 0 || l->tok[s].type != W4_J_ARR || zxm_len(l, s) > COLONNADE_STEPS)
            return ZXM_E_PROTOCOL;
        t->nsteps = zxm_len(l, s);
        int32_t d = zxm_get(l, o, "draft");
        if (d >= 0 && l->tok[d].type == W4_J_OBJ &&
            (zxm_get_str(l, d, "file", t->draft_file, sizeof t->draft_file) != ZXM_OK ||
             !name_ok(t->draft_file)))
            return ZXM_E_PROTOCOL;
        t->pillar = colonnade_pillar_of(t->lang, NULL);
    }
    memcpy(c->tmpl, got, sizeof got[0] * n);
    c->ntemplates = n;
    c->known = true;
    if (c->edit_tmpl >= (int64_t) n) c->edit_tmpl = -1;
    if (all > n)
        snprintf(c->notice, sizeof c->notice, "%u templates; the first %u are listed.", all, n);
    else
        snprintf(c->notice, sizeof c->notice, "%u templates in the pillars.", n);
    return ZXM_OK;
}

/* ---- CM2 the editor ----------------------------------------------------------- */

int colonnade_edit(colonnade_t *c, uint32_t tmpl, const char *file, const char *text)
{
    if (!c->known || tmpl >= c->ntemplates) return ZXM_E_STATE;
    if (!file || !text || !name_ok(file)) return ZXM_E_ARG;
    uint32_t n = bounded_len(text, COLONNADE_SOURCE_MAX);
    if (n > COLONNADE_SOURCE_MAX) return ZXM_E_TOOBIG;
    memcpy(c->text, text, n);
    c->text[n] = 0;
    c->len = n;
    memcpy(c->file, file, bounded_len(file, COLONNADE_NAME) + 1u);
    c->edit_tmpl = (int64_t) tmpl;
    snprintf(c->notice, sizeof c->notice, "%s for %s: %u bytes.", c->file, c->tmpl[tmpl].id, n);
    return ZXM_OK;
}

int colonnade_open(colonnade_t *c, uint32_t tmpl, const char *space, const char *path)
{
    static char got[COLONNADE_SOURCE_MAX + 1];
    zxm_link_t *l = c->link;
    if (!c->known || tmpl >= c->ntemplates) return ZXM_E_STATE;
    if (!path || !path[0]) return ZXM_E_ARG;
    const char *slash = strrchr(path, '/');
    const char *file = slash ? slash + 1 : path;
    if (!name_ok(file)) return ZXM_E_ARG;
    zxm_begin(l, "files.read");
    zxm_p_space(l, space);
    zxm_p_str(l, "path", path);
    zxm_p_u64(l, "max", COLONNADE_SOURCE_MAX + 1u);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    uint64_t size = 0;
    bool eof = false;
    if (zxm_get_u64(l, l->result, "size", &size) != ZXM_OK ||
        zxm_get_bool(l, l->result, "eof", &eof) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    if (size > COLONNADE_SOURCE_MAX || !eof) return ZXM_E_TOOBIG;
    if (zxm_get_str(l, l->result, "text", got, sizeof got) != ZXM_OK) return ZXM_E_PROTOCOL;
    return colonnade_edit(c, tmpl, file, got);
}

/* ---- CM3 a run ---------------------------------------------------------------- */

/* {"template":id[,"files":{file:text}]} into buf; its length, or -1 */
static int32_t run_params(const colonnade_t *c, uint32_t tmpl, bool with_file, char *buf,
                          uint32_t cap)
{
    uint32_t k = 0;
    int32_t w;
    const char *id = c->tmpl[tmpl].id;
#define PUT(s)                                                                                     \
    do {                                                                                           \
        uint32_t m_ = (uint32_t) strlen(s);                                                        \
        if (k + m_ >= cap) return -1;                                                              \
        memcpy(buf + k, s, m_);                                                                    \
        k += m_;                                                                                   \
    } while (0)
#define ESC(s, n)                                                                                  \
    do {                                                                                           \
        w = zxm_escape(s, n, buf + k, cap - k);                                                    \
        if (w < 0) return -1;                                                                      \
        k += (uint32_t) w;                                                                         \
    } while (0)
    PUT("{\"template\":\"");
    ESC(id, (uint32_t) strlen(id));
    PUT("\"");
    if (with_file) {
        PUT(",\"files\":{\"");
        ESC(c->file, (uint32_t) strlen(c->file));
        PUT("\":\"");
        ESC(c->text, c->len);
        PUT("\"}");
    }
    PUT("}");
#undef PUT
#undef ESC
    buf[k] = 0;
    return (int32_t) k;
}

int colonnade_run(colonnade_t *c, uint32_t tmpl)
{
    static char params[ZXM_REQ_MAX + 1];
    zxm_link_t *l = c->link;
    char word[COLONNADE_WORD + 1];
    if (!c->known || tmpl >= c->ntemplates) return ZXM_E_STATE;
    if (c->nruns >= COLONNADE_RUNS || !c->clock) return ZXM_E_STATE;
    int64_t left = colonnade_fuel_left(c->fuel_max, c->spent);
    if (left < (int64_t) COLONNADE_STEPS) {
        snprintf(c->notice, sizeof c->notice,
                 "%lld fuel left: a run needs %u (one for each step it may take).",
                 (long long) left, COLONNADE_STEPS);
        return ZXM_E_STATE;
    }
    bool edited = c->edit_tmpl == (int64_t) tmpl;
    int32_t n = run_params(c, tmpl, edited, params, sizeof params);
    if (n < 0) return ZXM_E_TOOBIG;
    int64_t start = c->clock(c->clock_ctx);
    zxm_begin(l, "play.run");
    zxm_p_raw(l, params, (uint32_t) n);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int64_t end = c->clock(c->clock_ctx);
    if (zxm_get_str(l, l->result, "verdict", word, sizeof word) != ZXM_OK) return ZXM_E_PROTOCOL;
    int64_t verdict = verdict_of(word);
    int32_t s = zxm_get(l, l->result, "steps");
    if (verdict < 0 || s < 0 || l->tok[s].type != W4_J_ARR || zxm_len(l, s) > COLONNADE_STEPS)
        return ZXM_E_PROTOCOL;
    int64_t ran_n = 0;
    for (uint32_t i = 0; i < zxm_len(l, s); i++) {
        int32_t o = zxm_at(l, s, i);
        bool ran = false;
        if (o < 0 || zxm_get_bool(l, o, "ran", &ran) != ZXM_OK ||
            zxm_get_str(l, o, "verdict", word, sizeof word) != ZXM_OK || verdict_of(word) < 0)
            return ZXM_E_PROTOCOL;
        ran_n += ran;
    }
    /* checked: the fuel was there for COLONNADE_STEPS, so this never goes below zero */
    if (colonnade_fuel_left(c->fuel_max, c->spent + ran_n) == COLONNADE_REFUSED)
        return ZXM_E_PROTOCOL;
    colonnade_run_t r;
    memset(&r, 0, sizeof r);
    r.tmpl = tmpl;
    r.pillar = c->tmpl[tmpl].pillar;
    r.steps_ran = ran_n;
    r.ms = end >= start ? end - start : 0; /* a clock that went back: 0 ms */
    if (r.ms > COLONNADE_MS_MAX) r.ms = COLONNADE_MS_MAX + 1;
    r.outbytes = (int64_t) l->resp_len;
    r.over = !colonnade_play_limits_ok(r.outbytes, r.ms);
    r.verdict = r.over ? LPRES_STATE_NEITHER : verdict;
    r.edited = edited;
    c->spent += ran_n;
    c->run[c->nruns++] = r;
    if (r.over)
        snprintf(c->notice, sizeof c->notice,
                 "%s ran past its limits (%lld bytes, %lld ms): its verdict is not used.",
                 c->tmpl[tmpl].id, (long long) r.outbytes, (long long) r.ms);
    else
        snprintf(c->notice, sizeof c->notice, "%s: %s in %lld ms, %lld steps ran, %lld fuel left.",
                 c->tmpl[tmpl].id, VERDICT_NAME[verdict], (long long) r.ms, (long long) ran_n,
                 (long long) colonnade_fuel(c));
    return ZXM_OK;
}

/* ---- CM4 the fuel ------------------------------------------------------------- */

int colonnade_set_fuel(colonnade_t *c, int64_t fuel_max, bool owner_confirmed)
{
    if (fuel_max < 1 || colonnade_fuel_left(fuel_max, c->spent) == COLONNADE_REFUSED)
        return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    c->fuel_max = fuel_max;
    snprintf(c->notice, sizeof c->notice, "Fuel for this session: %lld, %lld left.",
             (long long) fuel_max, (long long) colonnade_fuel(c));
    return ZXM_OK;
}

int64_t colonnade_fuel(const colonnade_t *c)
{
    return colonnade_fuel_left(c->fuel_max, c->spent);
}

/* ---- CM5 a draft -------------------------------------------------------------- */

int colonnade_draft(colonnade_t *c, uint32_t tmpl, const char *request)
{
    static char params[ZXM_REQ_MAX + 1];
    static char got[COLONNADE_DRAFT_MAX + 1];
    zxm_link_t *l = c->link;
    bool model = false;
    if (!c->known || tmpl >= c->ntemplates || !c->tmpl[tmpl].draft_file[0]) return ZXM_E_STATE;
    if (!request || bounded_len(request, COLONNADE_REQUEST) > COLONNADE_REQUEST) return ZXM_E_ARG;
    int32_t n = run_params(c, tmpl, c->edit_tmpl == (int64_t) tmpl, params, sizeof params);
    if (n < 0) return ZXM_E_TOOBIG;
    zxm_begin(l, "play.draft");
    zxm_p_raw(l, params, (uint32_t) n);
    if (request[0]) zxm_p_str(l, "request", request);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_bool(l, l->result, "model", &model) != ZXM_OK) return ZXM_E_PROTOCOL;
    rc = zxm_get_str(l, l->result, "draft", got, sizeof got);
    if (rc != ZXM_OK) return rc == ZXM_E_TOOBIG ? ZXM_E_TOOBIG : ZXM_E_PROTOCOL;
    uint32_t len = bounded_len(got, COLONNADE_DRAFT_MAX);
    memcpy(c->draft, got, len + 1u);
    c->draft_len = len;
    c->draft_model = model;
    c->draft_tmpl = tmpl;
    c->has_draft = true;
    snprintf(c->notice, sizeof c->notice,
             "A draft of %s (%u bytes, %s): untrusted, shown and not run. Take it into the "
             "editor to run it.",
             c->tmpl[tmpl].draft_file, len, model ? "from the model" : "a stand-in answered");
    return ZXM_OK;
}

int colonnade_take_draft(colonnade_t *c, bool owner_confirmed)
{
    if (!c->has_draft || c->draft_tmpl >= c->ntemplates) return ZXM_E_STATE;
    if (c->draft_len > COLONNADE_SOURCE_MAX) return ZXM_E_TOOBIG;
    if (!owner_confirmed) return ZXM_E_STATE;
    int rc = colonnade_edit(c, c->draft_tmpl, c->tmpl[c->draft_tmpl].draft_file, c->draft);
    if (rc != ZXM_OK) return rc;
    c->has_draft = false;
    snprintf(c->notice, sizeof c->notice,
             "The draft is in the editor as %s: it is checked like anything typed when it runs.",
             c->file);
    return ZXM_OK;
}

/* ---- CM6, CM7 ----------------------------------------------------------------- */

int colonnade_witness(colonnade_t *c, const uint32_t *runs, uint32_t n, colonnade_witness_t *out)
{
    int64_t v[COLONNADE_RUNS];
    lpres_state_t joined = LPRES_STATE_NEITHER;
    if (!out || !runs || n == 0 || n > COLONNADE_RUNS) return ZXM_E_ARG;
    memset(out, 0, sizeof *out);
    for (uint32_t i = 0; i < n; i++) {
        if (runs[i] >= c->nruns) return ZXM_E_ARG;
        v[i] = c->run[runs[i]].verdict;
        if (v[i] == LPRES_STATE_TRUE || v[i] == LPRES_STATE_BOTH)
            out->true_side[out->ntrue++] = runs[i];
        if (v[i] == LPRES_STATE_FALSE || v[i] == LPRES_STATE_BOTH)
            out->false_side[out->nfalse++] = runs[i];
    }
    if (!colonnade_verdict_join(v, n, &joined)) return ZXM_E_STATE;
    out->verdict = joined;
    out->held = colonnade_witness_split(out->ntrue > 0, out->nfalse > 0);
    if (out->held != (joined == LPRES_STATE_BOTH)) return ZXM_E_STATE; /* CL1's two forms agree */
    if (out->held)
        snprintf(c->notice, sizeof c->notice,
                 "The runs disagree: %u said TRUE and %u said FALSE. Held for you, both kept.",
                 out->ntrue, out->nfalse);
    else
        snprintf(c->notice, sizeof c->notice, "%u runs: %s.", n, VERDICT_NAME[joined]);
    return ZXM_OK;
}

int colonnade_tally(colonnade_t *c, int64_t pillar, int64_t *runs, int64_t *ms)
{
    int64_t p[COLONNADE_RUNS], t[COLONNADE_RUNS];
    if (!runs || !ms || !colonnade_pillar_ok(pillar)) return ZXM_E_ARG;
    uint32_t k = 0;
    for (uint32_t i = 0; i < c->nruns; i++) {
        if (!colonnade_pillar_ok(c->run[i].pillar)) continue; /* a language not in the index */
        p[k] = c->run[i].pillar;
        t[k] = c->run[i].ms > COLONNADE_MS_MAX ? COLONNADE_MS_MAX : c->run[i].ms;
        k++;
    }
    int64_t r = colonnade_pillar_runs(p, pillar, k), m = colonnade_pillar_time_ms(p, pillar, t, k);
    if (r == COLONNADE_REFUSED || m == COLONNADE_REFUSED) return ZXM_E_STATE;
    *runs = r;
    *ms = m;
    snprintf(c->notice, sizeof c->notice, "%s: %lld runs, %lld ms.", PILLAR_NAME[pillar],
             (long long) r, (long long) m);
    return ZXM_OK;
}

int64_t colonnade_output_x100(const colonnade_t *c, uint32_t run)
{
    if (run >= c->nruns) return COLONNADE_REFUSED;
    int64_t out = c->run[run].outbytes;
    if (out > COLONNADE_OUT_MAX) return COLONNADE_REFUSED; /* past the limit: no share */
    return colonnade_used_x100(COLONNADE_OUT_MAX, COLONNADE_OUT_MAX - out);
}
