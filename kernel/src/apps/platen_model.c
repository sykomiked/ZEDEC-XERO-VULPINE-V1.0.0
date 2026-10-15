/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* platen_model.c — Platen (see platen_model.h). */
#include <stdio.h>
#include <string.h>

#include "platen_model.h"

const zxm_ai_action_t PLATEN_AI[PLATEN_AI_COUNT] = {
    {"Explain this printer's trouble", "Companion", 300},
};

void platen_init(platen_t *p, zxm_link_t *link)
{
    memset(p, 0, sizeof *p);
    p->link = link;
}

int platen_set_space(platen_t *p, const char *space)
{
    if (!space) space = "";
    if (strlen(space) >= sizeof p->space) return ZXM_E_ARG;
    snprintf(p->space, sizeof p->space, "%s", strcmp(space, "home") ? space : "");
    return ZXM_OK;
}

/* a list of strings from the result, as many as fit */
static int words(zxm_link_t *l, const char *key, char (*out)[PLATEN_WORD], uint32_t *n)
{
    int32_t a = zxm_get(l, l->result, key);
    *n = 0;
    if (a < 0 || l->tok[a].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < zxm_len(l, a) && *n < PLATEN_WORDS; i++) {
        int32_t t = zxm_at(l, a, i);
        if (t < 0 || l->tok[t].type != W4_J_STR ||
            w4_json_str(l->resp, &l->tok[t], out[*n], PLATEN_WORD) < 0)
            return ZXM_E_PROTOCOL;
        (*n)++;
    }
    return ZXM_OK;
}

static int status(platen_t *p, platen_printer_t *pr)
{
    zxm_link_t *l = p->link;
    zxm_begin(l, "print.status");
    zxm_p_str(l, "uri", pr->uri);
    int rc = zxm_call(l);
    if (rc) {
        pr->known = false;
        return rc;
    }
    static platen_printer_t n;
    n = *pr;
    if (zxm_get_str(l, l->result, "name", n.name, sizeof n.name) ||
        zxm_get_str(l, l->result, "make", n.make, sizeof n.make) ||
        zxm_get_str(l, l->result, "state", n.state, sizeof n.state) ||
        zxm_get_str(l, l->result, "reasons", n.reasons, sizeof n.reasons) ||
        zxm_get_bool(l, l->result, "accepting", &n.accepting) ||
        zxm_get_bool(l, l->result, "color", &n.color) ||
        words(l, "formats", n.formats, &n.nformats) || words(l, "sides", n.sides, &n.nsides)) {
        pr->known = false;
        return ZXM_E_PROTOCOL;
    }
    n.known = true;
    *pr = n;
    return ZXM_OK;
}

int platen_add_printer(platen_t *p, const char *uri)
{
    if (!uri || !uri[0] || strlen(uri) > PLATEN_URI) return ZXM_E_ARG;
    for (uint32_t i = 0; i < p->nprinters; i++)
        if (!strcmp(p->printer[i].uri, uri)) return ZXM_E_STATE;
    if (p->nprinters == PLATEN_DEVICES) return ZXM_E_STATE;
    platen_printer_t *pr = &p->printer[p->nprinters];
    memset(pr, 0, sizeof *pr);
    memcpy(pr->uri, uri, strlen(uri) + 1);
    int rc = status(p, pr);
    if (rc) return rc;
    p->nprinters++;
    return ZXM_OK;
}

int platen_refresh_printer(platen_t *p, uint32_t i)
{
    if (i >= p->nprinters) return ZXM_E_ARG;
    return status(p, &p->printer[i]);
}

int platen_remove_printer(platen_t *p, uint32_t i)
{
    if (i >= p->nprinters) return ZXM_E_ARG;
    memmove(&p->printer[i], &p->printer[i + 1], (p->nprinters - i - 1) * sizeof *p->printer);
    p->nprinters--;
    /* jobs on it keep their place but no longer point at a printer */
    for (uint32_t k = 0; k < p->njobs; k++) {
        if (p->job[k].printer == i)
            p->job[k].printer = PLATEN_DEVICES;
        else if (p->job[k].printer > i && p->job[k].printer < PLATEN_DEVICES)
            p->job[k].printer--;
    }
    return ZXM_OK;
}

const char *platen_format_of(const char *name)
{
    static const struct {
        const char *ext, *mime;
    } F[] = {{"pdf", "application/pdf"}, {"jpg", "image/jpeg"},       {"jpeg", "image/jpeg"},
             {"png", "image/png"},       {"pwg", "image/pwg-raster"}, {"urf", "image/urf"},
             {"txt", "text/plain"}};
    const char *dot = strrchr(name, '.');
    if (!dot || strchr(dot, '/')) return NULL;
    for (size_t i = 0; i < sizeof F / sizeof *F; i++) {
        const char *a = dot + 1, *b = F[i].ext;
        while (*a && *b && (*a | 0x20) == *b) a++, b++;
        if (!*a && !*b) return F[i].mime;
    }
    return NULL;
}

bool platen_takes(const platen_t *p, uint32_t i, const char *name)
{
    const char *f = platen_format_of(name);
    if (i >= p->nprinters || !f) return false;
    const platen_printer_t *pr = &p->printer[i];
    for (uint32_t k = 0; k < pr->nformats; k++)
        if (!strcmp(pr->formats[k], f) || !strcmp(pr->formats[k], "application/octet-stream"))
            return true;
    return false;
}

static int read_job(zxm_link_t *l, platen_job_t *j)
{
    uint64_t id;
    int64_t pages;
    platen_job_t n = *j;
    if (zxm_get_u64(l, l->result, "job_id", &id) || id > INT32_MAX ||
        zxm_get_str(l, l->result, "state", n.state, sizeof n.state) ||
        zxm_get_str(l, l->result, "message", n.message, sizeof n.message) ||
        zxm_get_i64(l, l->result, "pages", &pages) || pages < -1 || pages > INT32_MAX)
        return ZXM_E_PROTOCOL;
    n.id = (int32_t) id;
    n.pages = (int32_t) pages;
    *j = n;
    return ZXM_OK;
}

int platen_print(platen_t *p, uint32_t printer, const char *path, const platen_opts_t *o)
{
    if (printer >= p->nprinters || !path || !path[0]) return ZXM_E_ARG;
    zxm_link_t *l = p->link;
    zxm_begin(l, "print.submit");
    zxm_p_space(l, p->space);
    zxm_p_str(l, "uri", p->printer[printer].uri);
    zxm_p_str(l, "path", path);
    if (o && o->copies) zxm_p_u64(l, "copies", o->copies);
    if (o && o->sides) zxm_p_str(l, "sides", o->sides);
    if (o && o->mono) zxm_p_str(l, "color", "monochrome");
    int rc = zxm_call(l);
    if (rc) return rc;
    platen_job_t j;
    memset(&j, 0, sizeof j);
    if ((rc = read_job(l, &j)) != 0) return rc;
    j.printer = printer;
    uint32_t n = (uint32_t) strlen(path);
    if (n >= sizeof j.path) n = zxm_utf8_cut(path, sizeof j.path - 1);
    memcpy(j.path, path, n);
    if (p->njobs == PLATEN_JOBS) { /* the oldest goes */
        memmove(&p->job[0], &p->job[1], (PLATEN_JOBS - 1) * sizeof *p->job);
        p->njobs--;
    }
    p->job[p->njobs++] = j;
    return ZXM_OK;
}

bool platen_job_done(const platen_job_t *j)
{
    return !strcmp(j->state, "completed") || !strcmp(j->state, "canceled") ||
           !strcmp(j->state, "aborted");
}

static int job_call(platen_t *p, uint32_t k, const char *method)
{
    platen_job_t *j = &p->job[k];
    if (j->printer >= p->nprinters) return ZXM_E_STATE; /* its printer was removed */
    zxm_link_t *l = p->link;
    zxm_begin(l, method);
    zxm_p_str(l, "uri", p->printer[j->printer].uri);
    zxm_p_u64(l, "job_id", (uint64_t) j->id);
    int rc = zxm_call(l);
    return rc ? rc : read_job(l, j);
}

int platen_follow(platen_t *p)
{
    int first = ZXM_OK;
    for (uint32_t k = 0; k < p->njobs; k++) {
        if (platen_job_done(&p->job[k]) || p->job[k].printer >= p->nprinters) continue;
        int rc = job_call(p, k, "print.job");
        if (rc && !first) first = rc; /* keep going: one printer may be off */
    }
    return first;
}

int platen_cancel(platen_t *p, uint32_t job)
{
    if (job >= p->njobs) return ZXM_E_ARG;
    if (platen_job_done(&p->job[job])) return ZXM_E_STATE;
    return job_call(p, job, "print.cancel");
}

int platen_add_scanner(platen_t *p, const char *url)
{
    if (!url || !url[0] || strlen(url) > PLATEN_URI) return ZXM_E_ARG;
    for (uint32_t i = 0; i < p->nscanners; i++)
        if (!strcmp(p->scanner[i].url, url)) return ZXM_E_STATE;
    if (p->nscanners == PLATEN_DEVICES) return ZXM_E_STATE;
    zxm_link_t *l = p->link;
    zxm_begin(l, "scan.status");
    zxm_p_str(l, "url", url);
    int rc = zxm_call(l);
    if (rc) return rc;
    platen_scanner_t s;
    memset(&s, 0, sizeof s);
    memcpy(s.url, url, strlen(url) + 1);
    int32_t a = zxm_get(l, l->result, "resolutions");
    if (zxm_get_str(l, l->result, "make", s.make, sizeof s.make) ||
        zxm_get_str(l, l->result, "state", s.state, sizeof s.state) ||
        zxm_get_bool(l, l->result, "platen", &s.platen) ||
        zxm_get_bool(l, l->result, "feeder", &s.feeder) || a < 0 || l->tok[a].type != W4_J_ARR)
        return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < zxm_len(l, a) && s.ndpi < 8; i++) {
        int32_t t = zxm_at(l, a, i);
        int64_t v;
        if (t < 0 || w4_json_i64(l->resp, &l->tok[t], &v) != W4_OK || v < 1 || v > 100000)
            return ZXM_E_PROTOCOL;
        s.dpi[s.ndpi++] = (uint32_t) v;
    }
    s.known = true;
    p->scanner[p->nscanners++] = s;
    return ZXM_OK;
}

int platen_remove_scanner(platen_t *p, uint32_t i)
{
    if (i >= p->nscanners) return ZXM_E_ARG;
    memmove(&p->scanner[i], &p->scanner[i + 1], (p->nscanners - i - 1) * sizeof *p->scanner);
    p->nscanners--;
    return ZXM_OK;
}

int platen_scan(platen_t *p, uint32_t scanner, const char *folder, const platen_scan_t *s)
{
    if (scanner >= p->nscanners) return ZXM_E_ARG;
    zxm_link_t *l = p->link;
    zxm_begin(l, "scan.start");
    zxm_p_space(l, p->space);
    zxm_p_str(l, "url", p->scanner[scanner].url);
    if (folder && folder[0]) zxm_p_str(l, "folder", folder);
    if (s && s->feeder) zxm_p_str(l, "source", "feeder");
    if (s && s->color) zxm_p_str(l, "color", s->color);
    if (s && s->dpi) zxm_p_u64(l, "dpi", s->dpi);
    if (s && s->format) zxm_p_str(l, "format", s->format);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t a = zxm_get(l, l->result, "saved");
    if (a < 0 || l->tok[a].type != W4_J_ARR || zxm_len(l, a) > PLATEN_PAGES) return ZXM_E_PROTOCOL;
    static char got[PLATEN_PAGES][256];
    uint32_t n = zxm_len(l, a);
    for (uint32_t i = 0; i < n; i++) {
        int32_t t = zxm_at(l, a, i);
        if (t < 0 || l->tok[t].type != W4_J_STR ||
            w4_json_str(l->resp, &l->tok[t], got[i], sizeof got[i]) < 0)
            return ZXM_E_PROTOCOL;
    }
    memcpy(p->saved, got, n * sizeof *got);
    p->nsaved = n;
    return ZXM_OK;
}

int platen_ai(platen_t *p, int32_t action, uint32_t printer)
{
    if (action < 0 || action >= PLATEN_AI_COUNT || printer >= p->nprinters) return ZXM_E_ARG;
    const platen_printer_t *pr = &p->printer[printer];
    if (!pr->reasons[0] || !strcmp(pr->reasons, "none")) return ZXM_E_STATE; /* nothing wrong */
    static char in[ZXM_ASK_MAX];
    int k = snprintf(in, sizeof in, "Printer: %s (%s)\nState: %s\nReasons: %s\n", pr->name,
                     pr->make, pr->state, pr->reasons);
    if (k < 0) return ZXM_E_ARG;
    if ((size_t) k >= sizeof in) k = (int) zxm_utf8_cut(in, sizeof in - 1);
    return zxm_ai_ask(p->link, &PLATEN_AI[action], action,
                      "These are a printer's IPP state reasons. Say in plain words what is wrong "
                      "and what the owner can do about it. Be brief.",
                      in, (uint32_t) k, &p->held);
}

int platen_ai_accept(platen_t *p)
{
    return zxm_held_accept(&p->held);
}

int platen_ai_reject(platen_t *p)
{
    return zxm_held_reject(&p->held);
}
