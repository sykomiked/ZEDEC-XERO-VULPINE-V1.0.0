/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pawprint_model.c — Pawprint (see pawprint_model.h). */
#include <stdio.h>
#include <string.h>

#include "pawprint_model.h"

static uint32_t bounded_len(const char *s, uint32_t most)
{
    uint32_t n = 0;
    while (n <= most && s[n]) n++;
    return n;
}

static void say(pawprint_t *p, const char *what)
{
    snprintf(p->notice, sizeof p->notice, "%s", what);
}

/* PM3/PM4: the replay starts again from the first step */
static void rewind_replay(pawprint_t *p)
{
    p->at_step = 0;
    p->at_line = 0;
    p->ended = false;
    p->stopped = false;
    p->stopped_at = -1;
    p->nmoves = 0;
    p->instructions = 0;
    for (uint32_t i = 0; i < p->nbreaks; i++) p->brk[i].hits = 0;
}

static void end_run(pawprint_t *p)
{
    p->have_run = false;
    p->nsteps = 0;
    p->verdict = LPRES_STATE_NEITHER;
    p->headline[0] = 0;
    rewind_replay(p);
}

void pawprint_init(pawprint_t *p, zxm_link_t *link)
{
    memset(p, 0, sizeof *p);
    p->link = link;
    end_run(p);
}

/* ---- PM1: the program ------------------------------------------------- */

int pawprint_open(pawprint_t *p, const char *template_id)
{
    if (!template_id) return ZXM_E_ARG;
    uint32_t n = bounded_len(template_id, PAWPRINT_TEMPLATE);
    if (n == 0 || n > PAWPRINT_TEMPLATE) return ZXM_E_ARG;
    memcpy(p->template_id, template_id, n);
    p->template_id[n] = 0;
    p->nfiles = 0;
    p->nbreaks = 0;
    pawprint_detach(p);
    end_run(p);
    say(p, "Template opened. Ask to attach before running it.");
    return ZXM_OK;
}

static int64_t count_lines(const char *text, uint32_t len)
{
    int64_t lines = 0;
    for (uint32_t i = 0; i < len; i++)
        if (text[i] == '\n') lines++;
    if (len && text[len - 1] != '\n') lines++;
    return lines;
}

static int32_t opened_index(const pawprint_t *p, const char *name)
{
    for (uint32_t i = 0; i < p->nfiles; i++)
        if (!strcmp(p->file[i].name, name)) return (int32_t) i;
    return -1;
}

int pawprint_file(pawprint_t *p, const char *name, const char *text)
{
    if (!p->template_id[0]) return ZXM_E_STATE;
    if (!name || !text) return ZXM_E_ARG;
    uint32_t nl = bounded_len(name, PAWPRINT_NAME), tl = bounded_len(text, PAWPRINT_TEXT);
    if (nl == 0 || nl > PAWPRINT_NAME || strchr(name, '/') || strchr(name, '\\')) return ZXM_E_ARG;
    if (tl > PAWPRINT_TEXT) return ZXM_E_TOOBIG;
    int32_t at = opened_index(p, name);
    if (at < 0) {
        if (p->nfiles == PAWPRINT_FILES) return ZXM_E_TOOBIG;
        at = (int32_t) p->nfiles++;
    }
    pawprint_file_t *f = &p->file[at];
    memcpy(f->name, name, nl);
    f->name[nl] = 0;
    memcpy(f->text, text, tl);
    f->text[tl] = 0;
    f->len = tl;
    f->lines = count_lines(text, tl);
    /* breakpoints past the file's new end go */
    uint32_t k = 0;
    for (uint32_t i = 0; i < p->nbreaks; i++)
        if (p->brk[i].file != at || p->brk[i].line <= f->lines) p->brk[k++] = p->brk[i];
    p->nbreaks = k;
    pawprint_detach(p);
    end_run(p);
    say(p, "File changed. The program must be approved again.");
    return ZXM_OK;
}

/* ---- PM2: attach, owner-confirmed --------------------------------------- */

int pawprint_attach(pawprint_t *p, bool own, bool system)
{
    if (!p->template_id[0]) return ZXM_E_STATE;
    p->attached = false;
    p->asked = false;
    /* even the owner's approval could not let this through: refused now */
    if (!pawprint_may_attach(own ? 1 : 0, 1, system ? 1 : 0)) {
        say(p, system ? "A system process: Pawprint does not attach to it."
                      : "Not your program: Pawprint does not attach to it.");
        return ZXM_E_STATE;
    }
    p->own = own;
    p->system = system;
    p->asked = true;
    say(p, "Attach to this program? Waiting for your approval.");
    return ZXM_OK;
}

int pawprint_approve(pawprint_t *p, bool approved)
{
    if (!p->asked) return ZXM_E_STATE;
    p->asked = false;
    p->attached = pawprint_may_attach(p->own ? 1 : 0, approved ? 1 : 0, p->system ? 1 : 0);
    say(p, p->attached ? "Attached." : "Not attached: you did not approve.");
    return p->attached ? ZXM_OK : ZXM_E_STATE;
}

void pawprint_detach(pawprint_t *p)
{
    p->attached = false;
    p->asked = false;
}

/* ---- PM3: run --------------------------------------------------------- */

static int32_t verdict_of(const char *s)
{
    return !strcmp(s, "TRUE")      ? LPRES_STATE_TRUE
           : !strcmp(s, "FALSE")   ? LPRES_STATE_FALSE
           : !strcmp(s, "BOTH")    ? LPRES_STATE_BOTH
           : !strcmp(s, "NEITHER") ? LPRES_STATE_NEITHER
                                   : -1;
}

static const char *verdict_word(int32_t v)
{
    return v == LPRES_STATE_TRUE    ? "passed"
           : v == LPRES_STATE_FALSE ? "refused"
           : v == LPRES_STATE_BOTH  ? "held for a person"
                                    : "undecided";
}

/* a string field of up to 63 bytes, cut to cap - 1 on a whole character */
static int get_cut(const zxm_link_t *l, int32_t o, const char *key, char *out, uint32_t cap)
{
    char buf[64];
    if (zxm_get_str(l, o, key, buf, sizeof buf) != ZXM_OK) return ZXM_E_PROTOCOL;
    uint32_t n = (uint32_t) strlen(buf);
    if (n >= cap) n = zxm_utf8_cut(buf, cap - 1u);
    memcpy(out, buf, n);
    out[n] = 0;
    return ZXM_OK;
}

static int read_step(const pawprint_t *p, const zxm_link_t *l, int32_t o, pawprint_step_t *s)
{
    char v[16];
    memset(s, 0, sizeof *s);
    if (o < 0 || get_cut(l, o, "name", s->name, sizeof s->name) ||
        get_cut(l, o, "lang", s->lang, sizeof s->lang) ||
        get_cut(l, o, "file", s->file, sizeof s->file) || zxm_get_bool(l, o, "ran", &s->ran) ||
        zxm_get_str(l, o, "verdict", v, sizeof v) || (s->verdict = verdict_of(v)) < 0)
        return ZXM_E_PROTOCOL;
    int32_t e = zxm_get(l, o, "error");
    if (e >= 0) {
        uint64_t line = 0, col = 0;
        if (zxm_get_u64(l, e, "line", &line) || zxm_get_u64(l, e, "col", &col) ||
            line > (uint64_t) PAW_LINE_MAX || col > (uint64_t) PAW_LINE_MAX)
            return ZXM_E_PROTOCOL;
        s->line = (int64_t) line;
        s->col = (int64_t) col;
    }
    s->opened = opened_index(p, s->file);
    /* PM3: the lines this step runs */
    if (!s->ran)
        s->reach = 0;
    else if (s->verdict == LPRES_STATE_FALSE)
        s->reach = s->line;
    else
        s->reach = s->opened >= 0 ? p->file[s->opened].lines : 0;
    if (s->opened >= 0 && s->reach > p->file[s->opened].lines) return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

static int params_of(const pawprint_t *p, char *out, uint32_t cap, uint32_t *len)
{
    uint32_t k = 0;
    int32_t w;
#define PUT(s)                                                                                     \
    do {                                                                                           \
        uint32_t m_ = (uint32_t) strlen(s);                                                        \
        if (k + m_ >= cap) return ZXM_E_TOOBIG;                                                    \
        memcpy(out + k, s, m_);                                                                    \
        k += m_;                                                                                   \
    } while (0)
#define ESC(s, n)                                                                                  \
    do {                                                                                           \
        w = zxm_escape(s, n, out + k, cap - k);                                                    \
        if (w < 0 || k + (uint32_t) w >= cap) return ZXM_E_TOOBIG;                                 \
        k += (uint32_t) w;                                                                         \
    } while (0)
    PUT("{\"template\":\"");
    ESC(p->template_id, (uint32_t) strlen(p->template_id));
    PUT("\"");
    if (p->nfiles) {
        PUT(",\"files\":{");
        for (uint32_t i = 0; i < p->nfiles; i++) {
            PUT(i ? ",\"" : "\"");
            ESC(p->file[i].name, (uint32_t) strlen(p->file[i].name));
            PUT("\":\"");
            ESC(p->file[i].text, p->file[i].len);
            PUT("\"");
        }
        PUT("}");
    }
    PUT("}");
#undef PUT
#undef ESC
    out[k] = 0;
    *len = k;
    return ZXM_OK;
}

int pawprint_run(pawprint_t *p)
{
    static char params[ZXM_REQ_MAX];
    static pawprint_step_t steps[PAWPRINT_STEPS];
    char verdict[16], headline[641];
    zxm_link_t *l = p->link;
    uint32_t len = 0;
    if (!l || !p->template_id[0]) return ZXM_E_STATE;
    if (!p->attached) {
        say(p, "Not attached: ask to attach, and approve it, first.");
        return ZXM_E_STATE;
    }
    int rc = params_of(p, params, sizeof params, &len);
    if (rc) return rc;
    zxm_begin(l, "play.run");
    zxm_p_raw(l, params, len);
    rc = zxm_call(l);
    if (rc) return rc;
    int32_t v, arr = zxm_get(l, l->result, "steps");
    if (zxm_get_str(l, l->result, "verdict", verdict, sizeof verdict) ||
        (v = verdict_of(verdict)) < 0 ||
        zxm_get_str(l, l->result, "headline", headline, sizeof headline) || arr < 0 ||
        l->tok[arr].type != W4_J_ARR || zxm_len(l, arr) > PAWPRINT_STEPS)
        return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr);
    for (uint32_t i = 0; i < n; i++)
        if (read_step(p, l, zxm_at(l, arr, i), &steps[i])) return ZXM_E_PROTOCOL;
    /* checked: now the run is kept */
    uint32_t hl = (uint32_t) strlen(headline);
    if (hl > PAWPRINT_HEADLINE) hl = zxm_utf8_cut(headline, PAWPRINT_HEADLINE);
    memcpy(p->headline, headline, hl);
    p->headline[hl] = 0;
    memcpy(p->step, steps, n * sizeof steps[0]);
    p->nsteps = n;
    p->verdict = v;
    p->have_run = true;
    rewind_replay(p);
    snprintf(p->notice, sizeof p->notice, "Run %s in %u steps. Step or continue to replay it.",
             verdict_word(v), n);
    return ZXM_OK;
}

/* ---- PM4: step and continue -------------------------------------------- */

/* the next line of the replay: false at the end */
static bool next_line(pawprint_t *p)
{
    while (p->at_step < p->nsteps) {
        if (p->at_line < p->step[p->at_step].reach) {
            p->at_line++;
            return true;
        }
        p->at_step++;
        p->at_line = 0;
    }
    p->ended = true;
    return false;
}

/* PR2 over the breakpoints of the current line's file: true when one stops */
static bool check_break(pawprint_t *p)
{
    static int64_t stops[PAW_BREAK_MAX];
    const pawprint_step_t *s = &p->step[p->at_step];
    int64_t hit = 0;
    uint32_t a = 0, n = 0;
    if (s->opened < 0) return false;
    while (a < p->nbreaks && p->brk[a].file < s->opened) a++;
    while (a + n < p->nbreaks && p->brk[a + n].file == s->opened) {
        stops[n] = p->brk[a + n].line;
        n++;
    }
    if (!pawprint_breakpoint_hit(stops, n, p->at_line, &hit) || !hit) return false;
    for (uint32_t i = a; i < a + n; i++) {
        pawprint_break_t *b = &p->brk[i];
        if (b->line != p->at_line) continue;
        if (b->hits < PAW_COUNT_MAX) b->hits++; /* held at the record's most */
        if (b->hits > b->condition) {
            p->stopped = true;
            p->stopped_at = (int32_t) i;
            return true;
        }
    }
    return false;
}

static void say_where(pawprint_t *p)
{
    if (p->ended) {
        snprintf(p->notice, sizeof p->notice, "Replay ended: the run %s. %s",
                 verdict_word(p->verdict), p->headline);
        return;
    }
    const pawprint_step_t *s = &p->step[p->at_step];
    if (s->verdict == LPRES_STATE_FALSE && p->at_line == s->reach)
        snprintf(p->notice, sizeof p->notice, "Step %s refused at %s line %lld column %lld.",
                 s->name, s->file, (long long) s->line, (long long) s->col);
    else
        snprintf(p->notice, sizeof p->notice, "%s at step %s, %s line %lld.",
                 p->stopped ? "Breakpoint" : "Paused", s->name, s->file, (long long) p->at_line);
}

static int move(pawprint_t *p, bool to_break)
{
    int64_t lines = 0, total = 0;
    if (!p->have_run || p->ended) return ZXM_E_STATE;
    if (p->nmoves == PAWPRINT_MOVES) return ZXM_E_TOOBIG;
    p->stopped = false;
    p->stopped_at = -1;
    while (next_line(p)) {
        lines++;
        bool stop = check_break(p);
        if (!to_break || stop) break;
    }
    p->moves[p->nmoves] = lines;
    if (!pawprint_stepped_insns(p->moves, p->nmoves + 1u, &total)) return ZXM_E_TOOBIG;
    p->nmoves++;
    p->instructions = total;
    say_where(p);
    return ZXM_OK;
}

int pawprint_step(pawprint_t *p)
{
    return move(p, false);
}

int pawprint_continue(pawprint_t *p)
{
    return move(p, true);
}

/* ---- PM5: breakpoints and the stack view --------------------------------- */

static int32_t find_break(const pawprint_t *p, int64_t file, int64_t line, uint32_t *at)
{
    uint32_t i = 0;
    while (i < p->nbreaks &&
           (p->brk[i].file < file || (p->brk[i].file == file && p->brk[i].line < line)))
        i++;
    *at = i;
    return i < p->nbreaks && p->brk[i].file == file && p->brk[i].line == line ? (int32_t) i : -1;
}

int pawprint_break(pawprint_t *p, uint32_t file, int64_t line, int64_t condition)
{
    uint32_t at = 0;
    if (file >= p->nfiles || line < 1 || line > p->file[file].lines ||
        !pawprint_in(condition, 0, PAW_COUNT_MAX) ||
        !pawprint_record_ok((int64_t) file, line, condition, 0))
        return ZXM_E_ARG;
    if (find_break(p, file, line, &at) >= 0) return ZXM_E_STATE;
    if (!pawprint_limits_ok((int64_t) p->nbreaks + 1, 0)) return ZXM_E_TOOBIG;
    memmove(p->brk + at + 1, p->brk + at, (p->nbreaks - at) * sizeof p->brk[0]);
    p->brk[at] = (pawprint_break_t){(int64_t) file, line, condition, 0};
    p->nbreaks++;
    snprintf(p->notice, sizeof p->notice, "Breakpoint at %s line %lld.", p->file[file].name,
             (long long) line);
    return ZXM_OK;
}

int pawprint_unbreak(pawprint_t *p, uint32_t file, int64_t line)
{
    uint32_t at = 0;
    if (find_break(p, file, line, &at) < 0) return ZXM_E_ARG;
    memmove(p->brk + at, p->brk + at + 1, (p->nbreaks - at - 1u) * sizeof p->brk[0]);
    p->nbreaks--;
    if (p->stopped_at >= (int32_t) at) p->stopped_at = -1;
    return ZXM_OK;
}

int pawprint_stack(const pawprint_t *p, pawprint_frame_t *out, uint32_t cap, uint32_t *n)
{
    if (!out || !n) return ZXM_E_ARG;
    if (!p->have_run) return ZXM_E_STATE;
    /* entered: every step passed, and the current one once its first line ran */
    uint32_t entered = p->ended ? p->nsteps : p->at_step + (p->at_line > 0 ? 1u : 0u);
    uint32_t frames = entered > (uint32_t) PAW_FRAMES_MAX ? (uint32_t) PAW_FRAMES_MAX : entered;
    if (!pawprint_limits_ok((int64_t) p->nbreaks, (int64_t) frames)) return ZXM_E_TOOBIG;
    if (cap < frames) return ZXM_E_TOOBIG;
    for (uint32_t k = 0; k < frames; k++) {
        uint32_t i = entered - 1u - k;
        const pawprint_step_t *s = &p->step[i];
        memcpy(out[k].step, s->name, sizeof out[k].step);
        memcpy(out[k].file, s->file, sizeof out[k].file);
        out[k].line = !p->ended && i == p->at_step ? p->at_line : s->reach;
    }
    *n = frames;
    return ZXM_OK;
}

/* ---- PM6: events ------------------------------------------------------- */

static int level_of(const char *s)
{
    return !strcmp(s, "error") ? 2 : !strcmp(s, "warn") ? 1 : !strcmp(s, "info") ? 0 : -1;
}

static int read_event(const zxm_link_t *l, int32_t o, pawprint_event_t *e)
{
    char lv[8];
    memset(e, 0, sizeof *e);
    if (o < 0 || zxm_get_u64(l, o, "seq", &e->seq) || zxm_get_u64(l, o, "time_ms", &e->time_ms) ||
        zxm_get_str(l, o, "level", lv, sizeof lv) || level_of(lv) < 0 ||
        zxm_get_str(l, o, "source", e->source, sizeof e->source) ||
        zxm_get_str(l, o, "text", e->text, sizeof e->text))
        return ZXM_E_PROTOCOL;
    e->level = (uint8_t) level_of(lv);
    return ZXM_OK;
}

static void keep_event(pawprint_t *p, const pawprint_event_t *e)
{
    if (p->nev == PAWPRINT_EVENTS) { /* full: the oldest goes */
        p->first = (p->first + 1u) % PAWPRINT_EVENTS;
        p->nev--;
    }
    p->ev[(p->first + p->nev) % PAWPRINT_EVENTS] = *e;
    p->nev++;
}

int pawprint_follow(pawprint_t *p, uint32_t *added)
{
    zxm_link_t *l = p->link;
    if (!l || !added) return ZXM_E_ARG;
    *added = 0;
    for (int round = 0; round < 2; round++) { /* 2 x 256 covers zxvd's ring of 512 */
        static pawprint_event_t e;
        uint64_t next = 0, dropped = 0;
        zxm_begin(l, "log.tail");
        zxm_p_u64(l, "since", p->next);
        zxm_p_u64(l, "max", 256);
        int rc = zxm_call(l);
        if (rc) return rc;
        int32_t arr = zxm_get(l, l->result, "entries");
        if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_get_u64(l, l->result, "next", &next) ||
            zxm_get_u64(l, l->result, "dropped", &dropped) || next < p->next ||
            zxm_len(l, arr) > 256u)
            return ZXM_E_PROTOCOL;
        uint32_t n = zxm_len(l, arr);
        for (uint32_t i = 0; i < n; i++) {
            if (read_event(l, zxm_at(l, arr, i), &e) || e.seq <= p->next || e.seq > next)
                return ZXM_E_PROTOCOL;
            keep_event(p, &e);
            (*added)++;
        }
        p->missed += dropped;
        bool more = n == 256u && next > p->next;
        p->next = next;
        if (!more) break;
    }
    return ZXM_OK;
}

const pawprint_event_t *pawprint_event(const pawprint_t *p, uint32_t i)
{
    return i < p->nev ? &p->ev[(p->first + i) % PAWPRINT_EVENTS] : NULL;
}

int pawprint_history(pawprint_t *p, const char *find)
{
    static pawprint_event_t page[PAWPRINT_PAGE];
    zxm_link_t *l = p->link;
    bool on = false, more = false;
    if (!l) return ZXM_E_STATE;
    if (find && bounded_len(find, 63) > 63) return ZXM_E_ARG; /* journal.query's text */
    zxm_begin(l, "journal.query");
    zxm_p_u64(l, "max", PAWPRINT_PAGE);
    if (find && find[0]) zxm_p_str(l, "text", find);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "entries");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_get_bool(l, l->result, "on", &on) ||
        zxm_get_bool(l, l->result, "more", &more) || zxm_len(l, arr) > PAWPRINT_PAGE)
        return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr);
    for (uint32_t i = 0; i < n; i++) /* newest first */
        if (read_event(l, zxm_at(l, arr, i), &page[i]) || (i && page[i].seq >= page[i - 1].seq))
            return ZXM_E_PROTOCOL;
    memcpy(p->hist, page, n * sizeof page[0]);
    p->nhist = n;
    p->journal_on = on;
    p->more = more && n > 0;
    if (!on) say(p, "zxvd keeps no journal: only the live events can be read.");
    return ZXM_OK;
}
