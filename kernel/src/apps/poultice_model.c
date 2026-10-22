/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* poultice_model.c — Poultice's model (poultice_model.h). Advisory and
 * human-approved: never a diagnosis, never a dose decided or changed. */
#include <string.h>

#include "poultice_model.h"

#define POULTICE_E_FILES (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */
#define POULTICE_TEXT    40960u   /* the whole plan, every line full */
#define POULTICE_PIECE   6000u    /* bytes of text in one files.write */
#define ADVICE           " Advice only: tell the person's clinician; Poultice changes no dose."

static const char *const VITAL_NAME[POULTICE_VITALS + 1u] = {
    "",         "heart rate", "oxygen saturation", "temperature", "breathing rate",
    "systolic", "diastolic",  "glucose",           "weight"};

/* ---- small text helpers, bounded ---- */

static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    size_t n = strlen(s);
    if (*at + n + 1u > cap) return false;
    memcpy(out + *at, s, n + 1u);
    *at += (uint32_t) n;
    return true;
}

static bool put_i64(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char d[24];
    uint32_t k = sizeof d - 1u;
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    d[k] = 0;
    do {
        d[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u);
    if (v < 0) d[--k] = '-';
    return put(out, cap, at, d + k);
}

static uint32_t len_of(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    while (s && n < cap && s[n]) n++;
    return n;
}

/* a name or a medical ID text: 1..max bytes, no control byte (no tab or newline) */
static bool text_ok(const char *s, uint32_t max)
{
    uint32_t n = len_of(s, max + 1u);
    if (!s || !n || n > max) return false;
    for (uint32_t i = 0; i < n; i++)
        if ((unsigned char) s[i] < 0x20u || s[i] == 0x7F) return false;
    return true;
}

static void copy_text(char *out, const char *s, uint32_t max)
{
    uint32_t n = len_of(s, max);
    memcpy(out, s, n);
    out[n] = 0;
}

static void say(poultice_t *p, const char *a, const char *b)
{
    uint32_t at = 0;
    p->notice[0] = 0;
    if (put(p->notice, sizeof p->notice, &at, a) && b) put(p->notice, sizeof p->notice, &at, b);
}

/* A decimal integer of t[*at..n) up to `end`, a sign allowed; false when
 * it is empty, not a number, past 18 digits or has no end. */
static bool get_i64(const char *t, uint32_t n, uint32_t *at, char end, int64_t *v)
{
    uint32_t i = *at, digits = 0;
    bool neg = i < n && t[i] == '-';
    int64_t x = 0;
    if (neg) i++;
    while (i < n && t[i] >= '0' && t[i] <= '9') {
        if (++digits > 18u) return false;
        x = x * 10 + (t[i++] - '0');
    }
    if (!digits || i >= n || t[i] != end) return false;
    *v = neg ? -x : x;
    *at = i + 1u;
    return true;
}

/* text of t[*at..n) up to `end`, at most cap - 1 bytes */
static bool get_text(const char *t, uint32_t n, uint32_t *at, char end, char *out, uint32_t cap)
{
    uint32_t k = 0, i = *at;
    while (i < n && t[i] != end) {
        if (k + 1u >= cap) return false;
        out[k++] = t[i++];
    }
    if (i >= n) return false;
    out[k] = 0;
    *at = i + 1u;
    return true;
}

/* ---- the plan's records (PM1, PM2, PM4) ---- */

static bool med_ok(const poultice_med_t *m)
{
    return m->id >= 1 && m->id <= POULTICE_ID_MAX && m->perdose >= 1 &&
           m->perdose <= POULTICE_DOSE_MAX && m->daymax >= 1 && m->daymax <= POULTICE_DAY_MAX &&
           m->gap >= 0 && m->gap <= POULTICE_GAP_MAX && m->start >= 0 &&
           m->start <= SOLSTICE_DAY_MAX && m->every >= 0 && m->every <= POULTICE_EVERY &&
           m->skip >= 0 && m->skip <= BOWLINE_SKIP_MAX && m->review >= 0 &&
           m->review <= SOLSTICE_DAY_MAX && text_ok(m->name, POULTICE_NAME_MAX);
}

static bool range_ok(int64_t vital, int64_t lo, int64_t hi)
{
    return vital >= 1 && vital <= (int64_t) POULTICE_VITALS && lo >= 0 && hi <= POULTICE_READING &&
           wellspring_band(lo, lo, hi) == 0;
}

static bool carer_ok(int64_t seq, int64_t peer, int64_t stamp, const char *name)
{
    return text_ok(name, POULTICE_NAME_MAX) && seq >= 1 && peer >= 1 && stamp >= 0 &&
           watchfire_contact_fits(seq, peer, (int64_t) len_of(name, POULTICE_NAME_MAX), stamp);
}

static bool item_ok(int64_t id, int64_t price, int64_t perday, const char *name)
{
    return id >= 1 && id <= POULTICE_ID_MAX && text_ok(name, POULTICE_NAME_MAX) &&
           poultice_month_cost(price, perday, 31) >= 0;
}

static bool field_ok(int64_t field, int64_t stamp, const char *text)
{
    return field >= 1 && field <= (int64_t) POULTICE_FIELDS && stamp >= 0 &&
           text_ok(text, POULTICE_TEXT_MAX) &&
           watchfire_medical_fits(field, (int64_t) len_of(text, POULTICE_TEXT_MAX), stamp);
}

static int32_t find_carer(const poultice_t *p, int64_t seq)
{
    for (uint32_t i = 0; i < p->ncarer; i++)
        if (p->carer[i].seq == seq) return (int32_t) i;
    return -1;
}

int32_t poultice_find_med(const poultice_t *p, int64_t id)
{
    for (uint32_t i = 0; i < p->nmed; i++)
        if (p->med[i].id == id) return (int32_t) i;
    return -1;
}

/* PM4: an entry fits the plan it goes into */
static bool entry_ok(const poultice_t *p, const poultice_entry_t *e)
{
    if (e->day < 0 || e->day > POULTICE_DAY_LAST || e->minute < 0 ||
        e->minute >= POULTICE_MINUTES || find_carer(p, e->carer) < 0 ||
        !poultice_node_ok(p->patient, e->kind, e->item, e->day, e->minute, e->carer, e->value))
        return false;
    switch (e->kind) {
    case POULTICE_SYMPTOM:
        return e->item >= 1 && e->item <= POULTICE_ID_MAX && e->value >= 0 &&
               e->value <= POULTICE_SEVERITY;
    case POULTICE_DOSE:
        return poultice_find_med(p, e->item) >= 0 && e->value >= 1 && e->value <= POULTICE_DOSE_MAX;
    case POULTICE_VITAL:
        return e->item >= 1 && e->item <= (int64_t) POULTICE_VITALS && e->value >= 0 &&
               e->value <= POULTICE_READING &&
               (e->item != POULTICE_HEART || wellspring_log_ok(e->day, WELLSPRING_PULSE, e->value));
    default:
        return false;
    }
}

static bool same_node(const poultice_entry_t *a, const poultice_entry_t *b)
{
    return a->kind == b->kind && a->item == b->item && a->day == b->day && a->minute == b->minute &&
           a->carer == b->carer;
}

/* ---- PM1: the plan as text ---- */

static bool put_row(char *out, uint32_t cap, uint32_t *at, const char *tag, const int64_t *v,
                    uint32_t n, const char *text)
{
    if (!put(out, cap, at, tag)) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!put(out, cap, at, "\t") || !put_i64(out, cap, at, v[i])) return false;
    if (text && (!put(out, cap, at, "\t") || !put(out, cap, at, text))) return false;
    return put(out, cap, at, "\n");
}

static int32_t compose(const poultice_t *p, char *out, uint32_t cap)
{
    uint32_t at = 0;
    out[0] = 0;
    if (!put(out, cap, &at, "poultice 1 ") || !put_i64(out, cap, &at, p->patient) ||
        !put(out, cap, &at, "\n"))
        return -1;
    for (uint32_t i = 0; i < p->nmed; i++) {
        const poultice_med_t *m = &p->med[i];
        const int64_t v[8] = {m->id,    m->perdose, m->daymax, m->gap,
                              m->start, m->every,   m->skip,   m->review};
        if (!put_row(out, cap, &at, "M", v, 8, m->name)) return -1;
    }
    for (uint32_t k = 1; k <= POULTICE_VITALS; k++) {
        const int64_t v[3] = {(int64_t) k, p->range[k].lo, p->range[k].hi};
        if (p->range[k].set && !put_row(out, cap, &at, "V", v, 3, NULL)) return -1;
    }
    for (uint32_t i = 0; i < p->ncarer; i++) {
        const int64_t v[3] = {p->carer[i].seq, p->carer[i].peer, p->carer[i].stamp};
        if (!put_row(out, cap, &at, "C", v, 3, p->carer[i].name)) return -1;
    }
    for (uint32_t i = 0; i < p->nitem; i++) {
        const int64_t v[3] = {p->item[i].id, p->item[i].price, p->item[i].perday};
        if (!put_row(out, cap, &at, "I", v, 3, p->item[i].name)) return -1;
    }
    for (uint32_t k = 1; k <= POULTICE_FIELDS; k++) {
        const int64_t v[2] = {(int64_t) k, p->field[k].stamp};
        if (p->field[k].set && !put_row(out, cap, &at, "D", v, 2, p->field[k].text)) return -1;
    }
    for (uint32_t i = 0; i < p->nlog; i++) {
        const poultice_entry_t *e = &p->log[i];
        const int64_t v[6] = {e->kind, e->day, e->minute, e->item, e->value, e->carer};
        if (!put_row(out, cap, &at, "L", v, 6, NULL)) return -1;
    }
    return (int32_t) at;
}

/* one line of the plan into q; false when it does not fit (PM1) */
static bool parse_line(poultice_t *q, const char *t, uint32_t n, uint32_t *at)
{
    if (*at + 1u >= n || t[*at + 1u] != '\t') return false;
    char tag = t[*at];
    *at += 2u;
    if (tag == 'M') {
        if (q->nmed >= POULTICE_MEDS) return false;
        poultice_med_t *m = &q->med[q->nmed];
        int64_t *f[8] = {&m->id,    &m->perdose, &m->daymax, &m->gap,
                         &m->start, &m->every,   &m->skip,   &m->review};
        for (uint32_t k = 0; k < 8u; k++)
            if (!get_i64(t, n, at, '\t', f[k])) return false;
        if (!get_text(t, n, at, '\n', m->name, sizeof m->name) || !med_ok(m) ||
            poultice_find_med(q, m->id) >= 0)
            return false;
        q->nmed++;
        return true;
    }
    if (tag == 'V') {
        int64_t k = 0, lo = 0, hi = 0;
        if (!get_i64(t, n, at, '\t', &k) || !get_i64(t, n, at, '\t', &lo) ||
            !get_i64(t, n, at, '\n', &hi) || !range_ok(k, lo, hi) || q->range[k].set)
            return false;
        q->range[k] = (poultice_range_t){true, lo, hi};
        return true;
    }
    if (tag == 'C') {
        if (q->ncarer >= POULTICE_CARERS) return false;
        poultice_carer_t *c = &q->carer[q->ncarer];
        if (!get_i64(t, n, at, '\t', &c->seq) || !get_i64(t, n, at, '\t', &c->peer) ||
            !get_i64(t, n, at, '\t', &c->stamp) ||
            !get_text(t, n, at, '\n', c->name, sizeof c->name) ||
            !carer_ok(c->seq, c->peer, c->stamp, c->name) || find_carer(q, c->seq) >= 0)
            return false;
        q->ncarer++;
        return true;
    }
    if (tag == 'I') {
        if (q->nitem >= POULTICE_ITEMS) return false;
        poultice_item_t *it = &q->item[q->nitem];
        if (!get_i64(t, n, at, '\t', &it->id) || !get_i64(t, n, at, '\t', &it->price) ||
            !get_i64(t, n, at, '\t', &it->perday) ||
            !get_text(t, n, at, '\n', it->name, sizeof it->name) ||
            !item_ok(it->id, it->price, it->perday, it->name))
            return false;
        for (uint32_t i = 0; i < q->nitem; i++)
            if (q->item[i].id == it->id) return false;
        q->nitem++;
        return true;
    }
    if (tag == 'D') {
        int64_t k = 0, stamp = 0;
        static char text[POULTICE_TEXT_MAX + 1u];
        if (!get_i64(t, n, at, '\t', &k) || !get_i64(t, n, at, '\t', &stamp) ||
            !get_text(t, n, at, '\n', text, sizeof text) || !field_ok(k, stamp, text) ||
            q->field[k].set)
            return false;
        q->field[k].set = true;
        q->field[k].stamp = stamp;
        copy_text(q->field[k].text, text, POULTICE_TEXT_MAX);
        return true;
    }
    if (tag == 'L') {
        if (q->nlog >= POULTICE_LOG) return false;
        poultice_entry_t *e = &q->log[q->nlog];
        int64_t *f[6] = {&e->kind, &e->day, &e->minute, &e->item, &e->value, &e->carer};
        for (uint32_t k = 0; k < 6u; k++)
            if (!get_i64(t, n, at, k == 5u ? '\n' : '\t', f[k])) return false;
        if (!entry_ok(q, e)) return false;
        for (uint32_t i = 0; i < q->nlog; i++)
            if (same_node(&q->log[i], e)) return false;
        q->nlog++;
        return true;
    }
    return false;
}

static bool parse(poultice_t *q, const char *t, uint32_t n)
{
    static const char HEAD[] = "poultice 1 ";
    uint32_t at = sizeof HEAD - 1u;
    if (!n) return true; /* an empty file is an empty plan */
    if (n < at || memcmp(t, HEAD, at) || !get_i64(t, n, &at, '\n', &q->patient) || q->patient < 1 ||
        q->patient > POULTICE_ID_MAX)
        return false;
    while (at < n)
        if (!parse_line(q, t, n, &at)) return false;
    return true;
}

/* ---- zxvd ---- */

static int write_text(zxm_link_t *l, const char *text, uint32_t len)
{
    uint32_t at = 0;
    bool first = true;
    do {
        uint32_t n = len - at;
        if (n > POULTICE_PIECE) {
            n = POULTICE_PIECE;
            while (n && text[at + n - 1u] != '\n') n--;
            if (!n) return ZXM_E_TOOBIG;
        }
        bool last = at + n >= len;
        zxm_begin(l, "files.write");
        zxm_p_str(l, "path", POULTICE_PLAN);
        zxm_p_strn(l, "text", text + at, n);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        int rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
    } while (at < len);
    return ZXM_OK;
}

static poultice_t NEXT; /* a change before it is written (PM1) */

/* write NEXT whole, then keep it */
static int commit(poultice_t *p)
{
    static char text[POULTICE_TEXT];
    int32_t len = compose(&NEXT, text, sizeof text);
    if (len < 0) return ZXM_E_TOOBIG;
    int rc = write_text(p->link, text, (uint32_t) len);
    if (rc) return rc;
    NEXT.link = p->link;
    NEXT.loaded = true;
    *p = NEXT;
    return ZXM_OK;
}

void poultice_init(poultice_t *p, zxm_link_t *link)
{
    memset(p, 0, sizeof *p);
    p->link = link;
    p->patient = 1;
}

int poultice_load(poultice_t *p)
{
    static char text[POULTICE_TEXT];
    static char chunk[POULTICE_TEXT];
    zxm_link_t *l = p->link;
    uint32_t len = 0;
    uint64_t off = 0;
    for (;;) {
        zxm_begin(l, "files.read");
        zxm_p_str(l, "path", POULTICE_PLAN);
        zxm_p_u64(l, "offset", off);
        int rc = zxm_call(l);
        if (rc == ZXM_E_REMOTE && l->err_code == POULTICE_E_FILES && !off &&
            strstr(l->err, "no such file")) {
            poultice_init(p, l); /* not made yet: an empty plan */
            p->loaded = true;
            return ZXM_OK;
        }
        if (rc) return rc;
        uint64_t size, next_off;
        bool eof;
        int32_t t = zxm_get(l, l->result, "text");
        if (zxm_get_u64(l, l->result, "size", &size) ||
            zxm_get_u64(l, l->result, "next", &next_off) ||
            zxm_get_bool(l, l->result, "eof", &eof) || t < 0)
            return ZXM_E_PROTOCOL;
        if (size >= POULTICE_TEXT) return ZXM_E_TOOBIG;
        int32_t n = w4_json_str(l->resp, &l->tok[t], chunk, sizeof chunk);
        if (n < 0 || len + (uint32_t) n >= POULTICE_TEXT) return ZXM_E_TOOBIG;
        memcpy(text + len, chunk, (size_t) n);
        len += (uint32_t) n;
        if (eof) break;
        if (next_off <= off) return ZXM_E_PROTOCOL;
        off = next_off;
    }
    poultice_init(&NEXT, l);
    if (!parse(&NEXT, text, len)) {
        p->loaded = false; /* PM1: never written over */
        say(p, "The care plan does not read as a plan; it is left as it is.", NULL);
        return ZXM_E_PROTOCOL;
    }
    NEXT.loaded = true;
    *p = NEXT;
    return ZXM_OK;
}

int poultice_set_patient(poultice_t *p, int64_t patient, bool owner_confirmed)
{
    if (!p->loaded) return ZXM_E_STATE;
    if (patient < 1 || patient > POULTICE_ID_MAX) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    NEXT = *p;
    NEXT.patient = patient;
    for (uint32_t i = 0; i < NEXT.nlog; i++)
        if (!entry_ok(&NEXT, &NEXT.log[i])) return ZXM_E_ARG;
    return commit(p);
}

/* ---- PM2: the plan's limits, carers, items and medical ID ---- */

int poultice_set_med(poultice_t *p, const poultice_med_t *m, bool owner_confirmed)
{
    if (!p->loaded) return ZXM_E_STATE;
    if (!m || !med_ok(m)) return ZXM_E_ARG;
    int32_t i = poultice_find_med(p, m->id);
    if (i < 0 && p->nmed >= POULTICE_MEDS) return ZXM_E_TOOBIG;
    if (!owner_confirmed) return ZXM_E_STATE;
    NEXT = *p;
    NEXT.med[i < 0 ? NEXT.nmed++ : (uint32_t) i] = *m;
    int rc = commit(p);
    if (!rc) say(p, "The clinician's limits are entered for ", m->name);
    return rc;
}

int poultice_set_range(poultice_t *p, int64_t vital, int64_t lo, int64_t hi, bool owner_confirmed)
{
    if (!p->loaded) return ZXM_E_STATE;
    if (!range_ok(vital, lo, hi)) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    NEXT = *p;
    NEXT.range[vital] = (poultice_range_t){true, lo, hi};
    return commit(p);
}

int poultice_add_carer(poultice_t *p, int64_t seq, int64_t peer, int64_t stamp, const char *name,
                       bool owner_confirmed)
{
    if (!p->loaded) return ZXM_E_STATE;
    if (!carer_ok(seq, peer, stamp, name) || find_carer(p, seq) >= 0) return ZXM_E_ARG;
    if (p->ncarer >= POULTICE_CARERS) return ZXM_E_TOOBIG;
    if (!owner_confirmed) return ZXM_E_STATE;
    NEXT = *p;
    poultice_carer_t *c = &NEXT.carer[NEXT.ncarer++];
    c->seq = seq;
    c->peer = peer;
    c->stamp = stamp;
    copy_text(c->name, name, POULTICE_NAME_MAX);
    return commit(p);
}

int poultice_set_item(poultice_t *p, int64_t id, int64_t price, int64_t perday, const char *name,
                      bool owner_confirmed)
{
    if (!p->loaded) return ZXM_E_STATE;
    if (!item_ok(id, price, perday, name)) return ZXM_E_ARG;
    int32_t i = -1;
    for (uint32_t k = 0; k < p->nitem; k++)
        if (p->item[k].id == id) i = (int32_t) k;
    if (i < 0 && p->nitem >= POULTICE_ITEMS) return ZXM_E_TOOBIG;
    if (!owner_confirmed) return ZXM_E_STATE;
    NEXT = *p;
    poultice_item_t *it = &NEXT.item[i < 0 ? NEXT.nitem++ : (uint32_t) i];
    it->id = id;
    it->price = price;
    it->perday = perday;
    copy_text(it->name, name, POULTICE_NAME_MAX);
    return commit(p);
}

int poultice_set_field(poultice_t *p, int64_t field, int64_t stamp, const char *text,
                       bool owner_confirmed)
{
    if (!p->loaded) return ZXM_E_STATE;
    if (!field_ok(field, stamp, text)) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    NEXT = *p;
    NEXT.field[field].set = true;
    NEXT.field[field].stamp = stamp;
    copy_text(NEXT.field[field].text, text, POULTICE_TEXT_MAX); /* kept as typed */
    return commit(p);
}

/* ---- PM3: a dose proposed ---- */

lpres_state_t poultice_dose_check(const poultice_t *p, int64_t med, int64_t dose, int64_t day,
                                  int64_t minute, int64_t margin[3])
{
    int32_t m = poultice_find_med(p, med);
    if (m < 0 || !margin || dose < 1 || dose > POULTICE_DOSE_MAX || day < 0 ||
        day > POULTICE_DAY_LAST || minute < 0 || minute >= POULTICE_MINUTES)
        return LPRES_STATE_NEITHER;
    int64_t at = day * POULTICE_MINUTES + minute, taken = 1, last = -1;
    for (uint32_t i = 0; i < p->nlog; i++) {
        const poultice_entry_t *e = &p->log[i];
        int64_t t = e->day * POULTICE_MINUTES + e->minute;
        if (e->kind != POULTICE_DOSE || e->item != med || t > at) continue;
        taken += t > at - POULTICE_MINUTES; /* the 24 hours up to it */
        if (t > last) last = t;
    }
    const poultice_med_t *d = &p->med[m];
    poultice_margins(dose, d->perdose, taken, d->daymax, last < 0 ? POULTICE_WAIT_MAX : at - last,
                     d->gap, margin);
    return poultice_dose_in_limit(margin[0], margin[1], margin[2]) ? LPRES_STATE_TRUE
                                                                   : LPRES_STATE_FALSE;
}

/* ---- PM4, PM5: the log ---- */

lpres_state_t poultice_reading(const poultice_t *p, int64_t vital, int64_t day, int64_t minute)
{
    int64_t v[POULTICE_LOG];
    uint32_t n = 0;
    for (uint32_t i = 0; i < p->nlog; i++) {
        const poultice_entry_t *e = &p->log[i];
        if (e->kind == POULTICE_VITAL && e->item == vital && e->day == day && e->minute == minute)
            v[n++] = e->value;
    }
    return poultice_reading_state(v, n);
}

/* PM5: the same notice for every carer, each addressed by name */
static void escalate(poultice_t *p, const char *what, const char *name, int64_t value)
{
    p->nescalate = 0;
    for (uint32_t k = 0; k < p->ncarer; k++) {
        char *o = p->escalate[k];
        uint32_t at = 0;
        o[0] = 0;
        if (put(o, POULTICE_NOTICE, &at, "To ") && put(o, POULTICE_NOTICE, &at, p->carer[k].name) &&
            put(o, POULTICE_NOTICE, &at, ": ") && put(o, POULTICE_NOTICE, &at, name) &&
            put(o, POULTICE_NOTICE, &at, " ") && put_i64(o, POULTICE_NOTICE, &at, value) &&
            put(o, POULTICE_NOTICE, &at, what))
            put(o, POULTICE_NOTICE, &at, ADVICE);
        p->nescalate++;
    }
}

int poultice_log_entry(poultice_t *p, int64_t kind, int64_t day, int64_t minute, int64_t item,
                       int64_t value, int64_t carer, bool owner_confirmed)
{
    if (!p->loaded) return ZXM_E_STATE;
    poultice_entry_t e = {kind, day, minute, item, value, carer};
    if (!entry_ok(p, &e)) return ZXM_E_ARG;
    for (uint32_t i = 0; i < p->nlog; i++)
        if (same_node(&p->log[i], &e)) return ZXM_E_ARG; /* PM4: one node */
    if (p->nlog >= POULTICE_LOG) return ZXM_E_TOOBIG;
    if (!owner_confirmed) return ZXM_E_STATE;
    int64_t margin[3] = {0, 0, 0};
    lpres_state_t advice = kind == POULTICE_DOSE
                               ? poultice_dose_check(p, item, value, day, minute, margin)
                               : LPRES_STATE_TRUE;
    NEXT = *p;
    NEXT.log[NEXT.nlog++] = e;
    int rc = commit(p);
    if (rc) return rc;
    p->nescalate = 0;
    say(p, "Logged.", NULL);
    if (kind == POULTICE_DOSE && advice != LPRES_STATE_TRUE) {
        escalate(p, " was given outside the plan's declared limits; it is recorded as given.",
                 p->med[poultice_find_med(p, item)].name, value);
        say(p, "Logged as given, outside the declared limits: every carer is told.", NULL);
    } else if (kind == POULTICE_VITAL) {
        lpres_state_t st = poultice_reading(p, item, day, minute);
        const poultice_range_t *r = &p->range[item];
        if (st == LPRES_STATE_BOTH) {
            escalate(p,
                     " and another reading for the same minute disagree; both are kept. Check "
                     "the person now.",
                     VITAL_NAME[item], value);
            say(p, "Two readings for one minute disagree: both kept, every carer is told.", NULL);
        } else if (r->set && wellspring_band(value, r->lo, r->hi) != 0) {
            escalate(p, " is outside the plan's declared range.", VITAL_NAME[item], value);
            say(p, "Logged, outside the declared range: every carer is told.", NULL);
        }
    }
    return ZXM_OK;
}

/* ---- PM6: times ---- */

int64_t poultice_next_dose_day(const poultice_t *p, int64_t med, int64_t today)
{
    int32_t m = poultice_find_med(p, med);
    if (m < 0 || !p->med[m].every) return POULTICE_REFUSED; /* as needed: no next day */
    int64_t next = solstice_next(p->med[m].start, p->med[m].every, today);
    return next < 0 ? POULTICE_REFUSED : bowline_unskipped(next, p->med[m].skip);
}

bool poultice_review_overdue(const poultice_t *p, int64_t med, int64_t today)
{
    int32_t m = poultice_find_med(p, med);
    if (m < 0 || !p->med[m].review || today < 0 || today > SOLSTICE_DAY_MAX) return false;
    return bowline_overdue(today - p->med[m].review, 1);
}

bool poultice_windows_clash(const poultice_t *p, int64_t meda, int64_t at_a, int64_t medb,
                            int64_t at_b)
{
    const int64_t last = (POULTICE_DAY_LAST + 1) * POULTICE_MINUTES;
    int32_t a = poultice_find_med(p, meda), b = poultice_find_med(p, medb);
    if (a < 0 || b < 0 || at_a < 0 || at_a >= last || at_b < 0 || at_b >= last) return false;
    int64_t aover = (at_a + p->med[a].gap - at_b) * 60, bover = (at_b + p->med[b].gap - at_a) * 60;
    return solstice_overlap(aover, bover);
}

/* ---- PM7: measures ---- */

int poultice_vital_week(const poultice_t *p, int64_t vital, int64_t first, int64_t out[3])
{
    int64_t v[POULTICE_LOG];
    uint32_t n = 0;
    if (!out || vital < 1 || vital > (int64_t) POULTICE_VITALS || first < 0 ||
        first > POULTICE_DAY_LAST)
        return ZXM_E_ARG;
    for (uint32_t i = 0; i < p->nlog; i++) {
        const poultice_entry_t *e = &p->log[i];
        if (e->kind == POULTICE_VITAL && e->item == vital && e->day >= first && e->day <= first + 6)
            v[n++] = e->value;
    }
    if (!n) return ZXM_E_STATE;
    return weatherglass_week(v, n, out) ? ZXM_E_ARG : ZXM_OK;
}

int64_t poultice_symptom_cell(const poultice_t *p, uint32_t i)
{
    int64_t sym[POULTICE_LOG], day[POULTICE_LOG];
    uint32_t n = 0;
    for (uint32_t k = 0; k < p->nlog; k++)
        if (p->log[k].kind == POULTICE_SYMPTOM) {
            sym[n] = p->log[k].item;
            day[n++] = p->log[k].day;
        }
    return i < n ? cartulary_cell(sym, day, n, i) : POULTICE_REFUSED;
}

int poultice_dose_week(const poultice_t *p, int64_t med, int64_t first, int64_t *total)
{
    int64_t v[POULTICE_LOG];
    uint32_t n = 0;
    if (!total || poultice_find_med(p, med) < 0 || first < 0 || first > POULTICE_DAY_LAST)
        return ZXM_E_ARG;
    for (uint32_t i = 0; i < p->nlog; i++) {
        const poultice_entry_t *e = &p->log[i];
        if (e->kind == POULTICE_DOSE && e->item == med && e->day >= first && e->day <= first + 6)
            v[n++] = e->value;
    }
    int64_t t = wellspring_week_total(v, n);
    if (t < 0) return ZXM_E_TOOBIG;
    *total = t;
    return ZXM_OK;
}

int poultice_month_total(const poultice_t *p, int64_t year, int64_t month, int64_t *total)
{
    int64_t cost[POULTICE_ITEMS];
    int64_t days = solstice_month_days(year, month);
    if (!total || days < 0) return ZXM_E_ARG;
    for (uint32_t i = 0; i < p->nitem; i++) {
        cost[i] = poultice_month_cost(p->item[i].price, p->item[i].perday, days);
        if (cost[i] < 0) return ZXM_E_ARG;
    }
    int64_t t = wellspring_week_total(cost, p->nitem);
    if (t < 0) return ZXM_E_TOOBIG;
    *total = t;
    return ZXM_OK;
}

/* ---- PM8: notices ---- */

int poultice_notices(poultice_t *p)
{
    char source[16];
    uint64_t unread = 0;
    zxm_link_t *l = p->link;
    zxm_begin(l, "notes.list");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "notes");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_len(l, arr) > POULTICE_NOTES ||
        zxm_get_u64(l, l->result, "unread", &unread) || unread > UINT32_MAX)
        return ZXM_E_PROTOCOL;
    uint32_t total = zxm_len(l, arr), ours = 0;
    for (uint32_t i = 0; i < total; i++) {
        int32_t o = zxm_at(l, arr, i);
        bool read = false;
        if (o < 0 || zxm_get_bool(l, o, "read", &read)) return ZXM_E_PROTOCOL;
        rc = zxm_get_str(l, o, "source", source, sizeof source);
        if (rc == ZXM_E_TOOBIG) continue; /* a long source is not Poultice */
        if (rc) return ZXM_E_PROTOCOL;
        ours += !read && !strcmp(source, POULTICE_SOURCE);
    }
    p->unread = (uint32_t) unread; /* kept only once the whole answer is read */
    p->ours = ours;
    return ZXM_OK;
}

int poultice_mark_read(poultice_t *p, uint32_t *marked, bool owner_confirmed)
{
    uint64_t n = 0;
    if (!owner_confirmed) return ZXM_E_STATE; /* every notice is marked: the owner's word */
    zxm_link_t *l = p->link;
    zxm_begin(l, "notes.mark_read");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_u64(l, l->result, "marked", &n) || n > UINT32_MAX) return ZXM_E_PROTOCOL;
    p->unread = 0;
    p->ours = 0;
    if (marked) *marked = (uint32_t) n;
    return ZXM_OK;
}
