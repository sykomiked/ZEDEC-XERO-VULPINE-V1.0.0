/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ipp.c — see ipp.h */
#include "ipp.h"

#include <string.h>

/* ===================== writing ===================== */

void ipp_w_init(ipp_w_t *w, uint8_t *buf, uint32_t cap)
{
    w->p = buf;
    w->len = 0;
    w->cap = cap;
    w->over = false;
}

static void put(ipp_w_t *w, const void *d, uint32_t n)
{
    if (w->over || n > w->cap - w->len) {
        w->over = true;
        return;
    }
    if (!n) return; /* a name-less value: name is NULL */
    memcpy(w->p + w->len, d, n);
    w->len += n;
}

static void put8(ipp_w_t *w, uint8_t v)
{
    put(w, &v, 1);
}

static void put16(ipp_w_t *w, uint32_t v)
{
    uint8_t b[2] = {(uint8_t) (v >> 8), (uint8_t) v};
    put(w, b, 2);
}

static void put32(ipp_w_t *w, uint32_t v)
{
    uint8_t b[4] = {(uint8_t) (v >> 24), (uint8_t) (v >> 16), (uint8_t) (v >> 8), (uint8_t) v};
    put(w, b, 4);
}

void ipp_w_head(ipp_w_t *w, uint16_t op, uint32_t request_id)
{
    put8(w, 2);
    put8(w, 0);
    put16(w, op);
    put32(w, request_id);
}

void ipp_w_group(ipp_w_t *w, uint8_t tag)
{
    put8(w, tag);
}

static void attr(ipp_w_t *w, uint8_t tag, const char *name, const void *v, uint32_t n)
{
    uint32_t nl = name ? (uint32_t) strlen(name) : 0;
    if (nl > 0xFFFFu || n > 0xFFFFu) {
        w->over = true;
        return;
    }
    put8(w, tag);
    put16(w, nl);
    put(w, name, nl);
    put16(w, n);
    put(w, v, n);
}

void ipp_w_str(ipp_w_t *w, uint8_t tag, const char *name, const char *v)
{
    attr(w, tag, name, v, (uint32_t) strlen(v));
}

void ipp_w_int(ipp_w_t *w, uint8_t tag, const char *name, int32_t v)
{
    uint32_t u = (uint32_t) v;
    uint8_t b[4] = {(uint8_t) (u >> 24), (uint8_t) (u >> 16), (uint8_t) (u >> 8), (uint8_t) u};
    attr(w, tag, name, b, 4);
}

void ipp_w_bool(ipp_w_t *w, const char *name, bool v)
{
    uint8_t b = v ? 1 : 0;
    attr(w, IPP_T_BOOLEAN, name, &b, 1);
}

static void op_attrs(ipp_w_t *w, uint16_t op, uint32_t id, const ipp_target_t *t)
{
    ipp_w_head(w, op, id);
    ipp_w_group(w, IPP_G_OPERATION);
    ipp_w_str(w, IPP_T_CHARSET, "attributes-charset", "utf-8");
    ipp_w_str(w, IPP_T_LANGUAGE, "attributes-natural-language", "en");
    ipp_w_str(w, IPP_T_URI, "printer-uri", t->uri);
    ipp_w_str(w, IPP_T_NAME, "requesting-user-name", t->user && t->user[0] ? t->user : "zxv");
}

static int done(ipp_w_t *w)
{
    ipp_w_group(w, IPP_G_END);
    return w->over ? IPP_E_ROOM : IPP_OK;
}

int ipp_req_printer(ipp_w_t *w, uint32_t id, const ipp_target_t *t)
{
    static const char *const WANT[] = {"printer-name",
                                       "printer-info",
                                       "printer-make-and-model",
                                       "printer-location",
                                       "printer-state",
                                       "printer-state-reasons",
                                       "printer-is-accepting-jobs",
                                       "color-supported",
                                       "queued-job-count",
                                       "document-format-supported",
                                       "media-supported",
                                       "media-default",
                                       "sides-supported",
                                       "copies-supported"};
    op_attrs(w, IPP_OP_GET_PRINTER_ATTRIBUTES, id, t);
    for (uint32_t i = 0; i < sizeof WANT / sizeof *WANT; i++)
        ipp_w_str(w, IPP_T_KEYWORD, i ? NULL : "requested-attributes", WANT[i]);
    return done(w);
}

static int job_req(ipp_w_t *w, uint16_t op, uint32_t id, const ipp_target_t *t,
                   const ipp_job_opts_t *o)
{
    op_attrs(w, op, id, t);
    if (o->job_name && o->job_name[0]) ipp_w_str(w, IPP_T_NAME, "job-name", o->job_name);
    if (o->format && o->format[0]) ipp_w_str(w, IPP_T_MIME, "document-format", o->format);
    if (o->copies > 0 || o->sides || o->color || o->media) {
        ipp_w_group(w, IPP_G_JOB);
        if (o->copies > 0) ipp_w_int(w, IPP_T_INTEGER, "copies", o->copies);
        if (o->sides) ipp_w_str(w, IPP_T_KEYWORD, "sides", o->sides);
        if (o->color) ipp_w_str(w, IPP_T_KEYWORD, "print-color-mode", o->color);
        if (o->media) ipp_w_str(w, IPP_T_KEYWORD, "media", o->media);
    }
    return done(w);
}

int ipp_req_print_job(ipp_w_t *w, uint32_t id, const ipp_target_t *t, const ipp_job_opts_t *o)
{
    return job_req(w, IPP_OP_PRINT_JOB, id, t, o);
}

int ipp_req_validate_job(ipp_w_t *w, uint32_t id, const ipp_target_t *t, const ipp_job_opts_t *o)
{
    return job_req(w, IPP_OP_VALIDATE_JOB, id, t, o);
}

int ipp_req_job(ipp_w_t *w, uint16_t op, uint32_t id, const ipp_target_t *t, int32_t job_id)
{
    op_attrs(w, op, id, t);
    ipp_w_int(w, IPP_T_INTEGER, "job-id", job_id);
    return done(w);
}

/* ===================== reading ===================== */

static uint32_t be16(const uint8_t *p)
{
    return (uint32_t) p[0] << 8 | p[1];
}

int ipp_r_init(ipp_r_t *r, const uint8_t *msg, uint32_t len)
{
    memset(r, 0, sizeof *r);
    if (len < 8) return IPP_E_FORMAT;
    r->p = msg;
    r->len = len;
    r->major = msg[0];
    r->minor = msg[1];
    r->status = (uint16_t) be16(msg + 2);
    r->request_id = be16(msg + 4) << 16 | be16(msg + 6);
    r->off = 8;
    if (r->major < 1 || r->major > 2) return IPP_E_FORMAT;
    return IPP_OK;
}

/* one attribute's tag, name and value, without interpreting them */
static int raw(ipp_r_t *r, uint8_t *tag, uint32_t *noff, uint32_t *nl, uint32_t *voff, uint32_t *vl)
{
    if (r->len - r->off < 5) return IPP_E_FORMAT;
    *tag = r->p[r->off];
    *nl = be16(r->p + r->off + 1);
    *noff = r->off + 3;
    if (r->len - *noff < *nl + 2u) return IPP_E_FORMAT;
    *vl = be16(r->p + *noff + *nl);
    *voff = *noff + *nl + 2;
    if (r->len - *voff < *vl) return IPP_E_FORMAT;
    r->off = *voff + *vl;
    return IPP_OK;
}

int ipp_next(ipp_r_t *r, ipp_attr_t *a)
{
    for (;;) {
        if (r->done) return 0;
        if (r->off >= r->len) return IPP_E_FORMAT; /* no end tag */
        uint8_t t = r->p[r->off];
        if (t < 0x10) { /* a delimiter */
            r->off++;
            if (t == IPP_G_END) {
                r->done = true;
                return 0;
            }
            if (t == 0) return IPP_E_FORMAT;
            r->group = t;
            continue;
        }
        if (!r->group) return IPP_E_FORMAT; /* an attribute before any group */
        uint8_t tag;
        uint32_t noff, nl, voff, vl;
        if (raw(r, &tag, &noff, &nl, &voff, &vl)) return IPP_E_FORMAT;
        a->group = r->group;
        a->tag = tag;
        a->name = (const char *) r->p + noff;
        a->name_len = (uint16_t) nl;
        a->val = r->p + voff;
        a->val_len = (uint16_t) vl;
        if (tag == IPP_T_END_COLLECTION || tag == IPP_T_MEMBER_NAME) return IPP_E_FORMAT;
        if (tag == IPP_T_BEGIN_COLLECTION) { /* skip its members (P4) */
            uint32_t depth = 1;
            while (depth) {
                uint8_t mt;
                uint32_t a1, a2, a3, a4;
                if (r->off >= r->len || r->p[r->off] < 0x10) return IPP_E_FORMAT;
                if (raw(r, &mt, &a1, &a2, &a3, &a4)) return IPP_E_FORMAT;
                if (mt == IPP_T_BEGIN_COLLECTION)
                    depth++;
                else if (mt == IPP_T_END_COLLECTION)
                    depth--;
            }
        }
        return 1;
    }
}

uint32_t ipp_r_end(const ipp_r_t *r)
{
    return r->off;
}

bool ipp_name_is(const ipp_attr_t *a, const char *name)
{
    size_t n = strlen(name);
    return a->name_len == n && !memcmp(a->name, name, n);
}

bool ipp_int(const ipp_attr_t *a, int32_t *v)
{
    if ((a->tag == IPP_T_INTEGER || a->tag == IPP_T_ENUM) && a->val_len == 4) {
        *v = (int32_t) ((uint32_t) a->val[0] << 24 | (uint32_t) a->val[1] << 16 |
                        (uint32_t) a->val[2] << 8 | a->val[3]);
        return true;
    }
    if (a->tag == IPP_T_BOOLEAN && a->val_len == 1) {
        *v = a->val[0] != 0;
        return true;
    }
    return false;
}

void ipp_str(const ipp_attr_t *a, char *out, uint32_t cap)
{
    if (!cap) return;
    uint32_t n = a->val_len < cap - 1 ? a->val_len : cap - 1;
    if (n < a->val_len) /* cut: end before a character's continuation bytes */
        while (n > 0 && (a->val[n] & 0xC0) == 0x80) n--;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t c = a->val[i];
        out[i] = c < 0x20 || c == 0x7f ? ' ' : (char) c;
    }
    out[n] = 0;
}

/* ===================== summaries ===================== */

static bool text_tag(uint8_t t)
{
    return t >= IPP_T_TEXT && t <= IPP_T_MIME;
}

/* a list value: kept while there is room, counted past it */
static void keep(const ipp_attr_t *a, char (*list)[IPP_WORD], uint32_t *n, uint32_t *more)
{
    if (!text_tag(a->tag)) return;
    if (*n == IPP_LIST) {
        (*more)++;
        return;
    }
    ipp_str(a, list[*n], IPP_WORD);
    (*n)++;
}

/* a set of keywords as one comma-separated line */
static void join(const ipp_attr_t *a, char *out, uint32_t cap)
{
    if (!text_tag(a->tag)) return;
    uint32_t n = (uint32_t) strlen(out);
    if (n && n + 2 < cap) {
        out[n++] = ',';
        out[n] = 0;
    }
    if (n + 1 < cap) ipp_str(a, out + n, cap - n);
}

int ipp_parse_printer(const uint8_t *msg, uint32_t len, ipp_printer_t *p)
{
    memset(p, 0, sizeof *p);
    p->queued = -1;
    ipp_r_t r;
    ipp_attr_t a;
    if (ipp_r_init(&r, msg, len)) return IPP_E_FORMAT;
    p->status = r.status;
    const char *name = NULL;
    uint16_t name_len = 0;
    int rc;
    while ((rc = ipp_next(&r, &a)) == 1) {
        if (a.name_len) { /* a new attribute; else another value of the one before */
            name = a.name;
            name_len = a.name_len;
        }
        if (!name) return IPP_E_FORMAT;
        ipp_attr_t n = a;
        n.name = name;
        n.name_len = name_len;
        int32_t v;
        if (a.group != IPP_G_PRINTER) continue;
        if (ipp_name_is(&n, "printer-name"))
            ipp_str(&a, p->name, sizeof p->name);
        else if (ipp_name_is(&n, "printer-info"))
            ipp_str(&a, p->info, sizeof p->info);
        else if (ipp_name_is(&n, "printer-make-and-model"))
            ipp_str(&a, p->make, sizeof p->make);
        else if (ipp_name_is(&n, "printer-location"))
            ipp_str(&a, p->where, sizeof p->where);
        else if (ipp_name_is(&n, "printer-state") && ipp_int(&a, &v))
            p->state = v;
        else if (ipp_name_is(&n, "printer-state-reasons"))
            join(&a, p->reasons, sizeof p->reasons);
        else if (ipp_name_is(&n, "printer-is-accepting-jobs") && ipp_int(&a, &v))
            p->accepting = v != 0;
        else if (ipp_name_is(&n, "color-supported") && ipp_int(&a, &v))
            p->color = v != 0;
        else if (ipp_name_is(&n, "queued-job-count") && ipp_int(&a, &v))
            p->queued = v;
        else if (ipp_name_is(&n, "document-format-supported"))
            keep(&a, p->formats, &p->nformats, &p->more);
        else if (ipp_name_is(&n, "media-supported"))
            keep(&a, p->media, &p->nmedia, &p->more);
        else if (ipp_name_is(&n, "media-default"))
            ipp_str(&a, p->media_default, sizeof p->media_default);
        else if (ipp_name_is(&n, "sides-supported"))
            keep(&a, p->sides, &p->nsides, &p->more);
        else if (ipp_name_is(&n, "copies-supported") && a.tag == IPP_T_RANGE && a.val_len == 8)
            p->copies_max = (int32_t) ((uint32_t) a.val[4] << 24 | (uint32_t) a.val[5] << 16 |
                                       (uint32_t) a.val[6] << 8 | a.val[7]);
    }
    return rc < 0 ? IPP_E_FORMAT : IPP_OK;
}

int ipp_parse_job(const uint8_t *msg, uint32_t len, ipp_job_t *j)
{
    memset(j, 0, sizeof *j);
    j->pages = -1;
    ipp_r_t r;
    ipp_attr_t a;
    if (ipp_r_init(&r, msg, len)) return IPP_E_FORMAT;
    j->status = r.status;
    const char *name = NULL;
    uint16_t name_len = 0;
    char status_message[IPP_TEXT] = "";
    int rc;
    while ((rc = ipp_next(&r, &a)) == 1) {
        if (a.name_len) {
            name = a.name;
            name_len = a.name_len;
        }
        if (!name) return IPP_E_FORMAT;
        ipp_attr_t n = a;
        n.name = name;
        n.name_len = name_len;
        int32_t v;
        if (a.group == IPP_G_OPERATION) {
            if (ipp_name_is(&n, "status-message"))
                ipp_str(&a, status_message, sizeof status_message);
            continue;
        }
        if (a.group != IPP_G_JOB) continue;
        if (ipp_name_is(&n, "job-id") && ipp_int(&a, &v))
            j->id = v;
        else if (ipp_name_is(&n, "job-state") && ipp_int(&a, &v))
            j->state = v;
        else if (ipp_name_is(&n, "job-state-reasons"))
            join(&a, j->reasons, sizeof j->reasons);
        else if (ipp_name_is(&n, "job-state-message"))
            ipp_str(&a, j->message, sizeof j->message);
        else if (ipp_name_is(&n, "job-impressions-completed") && ipp_int(&a, &v))
            j->pages = v;
    }
    if (rc < 0) return IPP_E_FORMAT;
    if (!j->message[0]) memcpy(j->message, status_message, sizeof j->message);
    return IPP_OK;
}

bool ipp_takes(const ipp_printer_t *p, const char *format)
{
    for (uint32_t i = 0; i < p->nformats; i++)
        if (!strcmp(p->formats[i], format) || !strcmp(p->formats[i], "application/octet-stream"))
            return true;
    return false;
}

const char *ipp_printer_state_name(int32_t state)
{
    switch (state) {
    case IPP_PRINTER_IDLE:
        return "idle";
    case IPP_PRINTER_PROCESSING:
        return "printing";
    case IPP_PRINTER_STOPPED:
        return "stopped";
    default:
        return "unknown";
    }
}

const char *ipp_job_state_name(int32_t state)
{
    static const char *const N[] = {"pending",  "held",    "printing", "stopped",
                                    "canceled", "aborted", "completed"};
    return state >= IPP_JOB_PENDING && state <= IPP_JOB_COMPLETED ? N[state - IPP_JOB_PENDING]
                                                                  : "unknown";
}
