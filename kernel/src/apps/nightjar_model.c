/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* nightjar_model.c — Nightjar (see nightjar_model.h). */
#include <string.h>

#include "nightjar_model.h"

static const char *const ROLE_WORD[NIGHTJAR_ROLES] = {
    "group",       "text",           "button",   "list",    "field",
    "scroll area", "answer waiting", "progress", "picture", "separator"};

void nightjar_init(nightjar_t *n, zxm_link_t *link)
{
    memset(n, 0, sizeof *n);
    n->link = link;
    n->focus = -1;
    n->wpm = NIGHTJAR_WPM_START;
    n->pitch = NIGHTJAR_PITCH_START;
}

static uint32_t len_of(const char *s, uint32_t cap)
{
    uint32_t k = 0;
    while (s && k < cap && s[k]) k++;
    return k;
}

/* a name: 1..NIGHTJAR_NAME_MAX bytes, no control characters */
static bool name_ok(const char *s)
{
    uint32_t k = len_of(s, NIGHTJAR_NAME_MAX + 1u);
    if (k < 1u || k > NIGHTJAR_NAME_MAX) return false;
    for (uint32_t i = 0; i < k; i++)
        if ((unsigned char) s[i] < 0x20u || s[i] == 0x7f) return false;
    return true;
}

int nightjar_add_element(nightjar_t *n, int64_t role, const char *name, int64_t row, int64_t column,
                         int64_t state, uint32_t *index)
{
    if (role < 0 || role >= NIGHTJAR_ROLES || !name_ok(name) || row < 0 ||
        row > NIGHTJAR_GRID_MAX || column < 0 || column > NIGHTJAR_GRID_MAX || state < 0 ||
        state > NIGHTJAR_STATE_MAX)
        return ZXM_E_ARG;
    uint32_t k = len_of(name, NIGHTJAR_NAME_MAX);
    if (!nightjar_element_fits(n->nel, role, k, row, column, state)) return ZXM_E_ARG;
    if (n->nel >= NIGHTJAR_ELEMENTS) return ZXM_E_STATE;
    nightjar_element_t *e = &n->el[n->nel];
    memset(e, 0, sizeof *e);
    e->role = role;
    e->row = row;
    e->column = column;
    e->state = state;
    memcpy(e->name, name, k);
    if (index) *index = n->nel;
    n->nel++;
    return ZXM_OK;
}

/* NJ4: keep only what cuts in, oldest first */
static void drop_pages(nightjar_t *n)
{
    static nightjar_say_t keep[NIGHTJAR_QUEUE];
    uint32_t m = 0;
    for (uint32_t i = n->tail; i != n->head; i++) {
        const nightjar_say_t *s = &n->queue[i % NIGHTJAR_QUEUE];
        if (nightjar_cuts_in(s->kind))
            keep[m++] = *s;
        else
            n->dropped++;
    }
    for (uint32_t i = 0; i < m; i++) n->queue[(n->tail + i) % NIGHTJAR_QUEUE] = keep[i];
    n->head = n->tail + m;
}

void nightjar_clear(nightjar_t *n)
{
    memset(n->el, 0, sizeof n->el);
    n->nel = 0;
    n->focus = -1;
    drop_pages(n);
}

int nightjar_order(const nightjar_t *n, uint32_t *order, uint32_t cap, uint32_t *count)
{
    static int64_t row[NIGHTJAR_ELEMENTS], col[NIGHTJAR_ELEMENTS];
    if (!order || !count || cap < n->nel) return ZXM_E_ARG;
    for (uint32_t i = 0; i < n->nel; i++) {
        row[i] = n->el[i].row;
        col[i] = n->el[i].column;
    }
    /* each place 1..nel (NR4); ties keep the order they came in */
    uint32_t at = 0;
    for (int64_t p = 1; p <= (int64_t) n->nel; p++)
        for (uint32_t i = 0; i < n->nel; i++)
            if (nightjar_reading_place(row, col, n->nel, i) == p) order[at++] = i;
    *count = at;
    return ZXM_OK;
}

static void put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    for (uint32_t i = 0; s[i] && *at + 1u < cap; i++) out[(*at)++] = s[i];
    out[*at] = 0;
}

static void put_u(char *out, uint32_t cap, uint32_t *at, uint32_t v)
{
    char d[11];
    uint32_t k = 0;
    do {
        d[k++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v && k < sizeof d);
    char s[12];
    for (uint32_t i = 0; i < k; i++) s[i] = d[k - 1u - i];
    s[k] = 0;
    put(out, cap, at, s);
}

int32_t nightjar_describe(const nightjar_t *n, uint32_t i, char *out, uint32_t cap)
{
    static uint32_t order[NIGHTJAR_ELEMENTS];
    uint32_t count = 0, at = 0;
    if (!out || cap < 2u || i >= n->nel) return -1;
    const nightjar_element_t *e = &n->el[i];
    out[0] = 0;
    put(out, cap, &at, e->name);
    if (!n->simple) {
        put(out, cap, &at, ", ");
        put(out, cap, &at, ROLE_WORD[e->role]);
    }
    if (e->state & NIGHTJAR_SELECTED) put(out, cap, &at, ", selected");
    if (e->state & NIGHTJAR_DIMMED) put(out, cap, &at, ", dimmed");
    if (e->state & NIGHTJAR_CHECKED) put(out, cap, &at, ", checked");
    if (!n->simple && nightjar_order(n, order, NIGHTJAR_ELEMENTS, &count) == ZXM_OK) {
        uint32_t k = 0;
        while (k < count && order[k] != i) k++;
        put(out, cap, &at, ", ");
        put_u(out, cap, &at, k + 1u);
        put(out, cap, &at, " of ");
        put_u(out, cap, &at, count);
    }
    return (int32_t) at;
}

static bool tag_ok(const char *t)
{
    uint32_t k = len_of(t, NIGHTJAR_TAG_MAX + 1u);
    if (k < 2u || k > NIGHTJAR_TAG_MAX) return false;
    for (uint32_t i = 0; i < k; i++) {
        char c = t[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '-'))
            return false;
    }
    return true;
}

/* NJ5: the language of text into lang; "und" when zxvd refuses or cannot tell */
static int detect(zxm_link_t *l, const char *text, char *lang)
{
    char tag[NIGHTJAR_TAG_MAX + 1];
    memcpy(lang, "und", 4u);
    zxm_begin(l, "xlate.detect");
    zxm_p_str(l, "text", text);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE) return ZXM_OK;
    if (rc) return rc;
    if (!zxm_get_str(l, l->result, "language", tag, sizeof tag) && tag_ok(tag))
        memcpy(lang, tag, len_of(tag, sizeof tag) + 1u);
    return ZXM_OK;
}

/* NJ4: queue one utterance; ZXM_E_STATE when the ring is full */
static int enqueue(nightjar_t *n, int64_t kind, int32_t element, const char *text, const char *lang)
{
    if (kind != NIGHTJAR_ALERT && kind != NIGHTJAR_FOCUS && kind != NIGHTJAR_PAGE) return ZXM_E_ARG;
    bool cut = nightjar_cuts_in(kind);
    uint32_t keep = 0;
    for (uint32_t i = n->tail; i != n->head; i++)
        keep += nightjar_cuts_in(n->queue[i % NIGHTJAR_QUEUE].kind);
    uint32_t after = cut ? keep : n->head - n->tail;
    if (after >= NIGHTJAR_QUEUE) return ZXM_E_STATE; /* checked before anything is dropped */
    if (cut) {
        drop_pages(n);
        n->cut = true;
    }
    nightjar_say_t *s = &n->queue[n->head % NIGHTJAR_QUEUE];
    memset(s, 0, sizeof *s);
    s->kind = kind;
    s->element = element;
    memcpy(s->text, text, len_of(text, NIGHTJAR_SAY_MAX));
    memcpy(s->lang, lang, len_of(lang, NIGHTJAR_TAG_MAX));
    n->head++;
    return ZXM_OK;
}

static int u32(zxm_link_t *l, int32_t o, const char *key, uint32_t max, uint32_t *v)
{
    uint64_t x;
    if (zxm_get_u64(l, o, key, &x) || x > max) return ZXM_E_PROTOCOL;
    *v = (uint32_t) x;
    return ZXM_OK;
}

typedef struct {
    uint32_t seq, pri;
    bool read;
    char source[NIGHTJAR_SOURCE + 1];
    char title[NIGHTJAR_TITLE + 1];
} nightjar_note_t;

static int read_note(zxm_link_t *l, int32_t o, nightjar_note_t *x)
{
    memset(x, 0, sizeof *x);
    if (o < 0 || u32(l, o, "seq", UINT32_MAX, &x->seq) || !x->seq ||
        u32(l, o, "pri", NIGHTJAR_PRI_MAX, &x->pri) || zxm_get_bool(l, o, "read", &x->read) ||
        zxm_get_str(l, o, "source", x->source, sizeof x->source) ||
        zxm_get_str(l, o, "title", x->title, sizeof x->title))
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

int nightjar_refresh(nightjar_t *n, uint32_t *queued)
{
    static nightjar_t t; /* the new state, kept apart until it is whole */
    static nightjar_note_t note[NIGHTJAR_NOTES];
    zxm_link_t *l = n->link;
    bool simple;
    zxm_begin(l, "ui.get");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "simple_mode", &simple)) return ZXM_E_PROTOCOL;
    zxm_begin(l, "notes.list");
    if ((rc = zxm_call(l)) != ZXM_OK) return rc;
    int32_t arr = zxm_get(l, l->result, "notes");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_len(l, arr) > NIGHTJAR_NOTES)
        return ZXM_E_PROTOCOL;
    uint32_t m = zxm_len(l, arr);
    for (uint32_t i = 0; i < m; i++)
        if (read_note(l, zxm_at(l, arr, i), &note[i])) return ZXM_E_PROTOCOL;
    t = *n;
    t.simple = simple;
    uint32_t added = 0;
    for (uint32_t i = m; i-- > 0;) { /* newest first in the reply: oldest said first */
        const nightjar_note_t *x = &note[i];
        if (x->seq > t.last_seq) t.last_seq = x->seq;
        if (x->read || x->seq <= n->last_seq) continue;
        char text[NIGHTJAR_SAY_MAX + 1], lang[NIGHTJAR_TAG_MAX + 1];
        uint32_t at = 0;
        text[0] = 0;
        put(text, sizeof text, &at, "Notice");
        if (x->source[0]) {
            put(text, sizeof text, &at, " from ");
            put(text, sizeof text, &at, x->source);
        }
        put(text, sizeof text, &at, ": ");
        put(text, sizeof text, &at, x->title);
        if ((rc = detect(l, x->title[0] ? x->title : text, lang)) != ZXM_OK) return rc;
        int64_t kind = x->pri >= NIGHTJAR_PRI_HIGH ? NIGHTJAR_ALERT : NIGHTJAR_PAGE;
        if ((rc = enqueue(&t, kind, -1, text, lang)) != ZXM_OK) return rc;
        added++;
    }
    *n = t;
    if (queued) *queued = added;
    return ZXM_OK;
}

int nightjar_read_page(nightjar_t *n, uint32_t *queued)
{
    static nightjar_t t;
    static uint32_t order[NIGHTJAR_ELEMENTS];
    uint32_t count = 0;
    if (!n->nel) return ZXM_E_STATE;
    if (n->head - n->tail + n->nel > NIGHTJAR_QUEUE) return ZXM_E_STATE; /* room for all, or none */
    int rc = nightjar_order(n, order, NIGHTJAR_ELEMENTS, &count);
    if (rc) return rc;
    t = *n;
    for (uint32_t k = 0; k < count; k++) {
        char text[NIGHTJAR_SAY_MAX + 1], lang[NIGHTJAR_TAG_MAX + 1];
        if (nightjar_describe(n, order[k], text, sizeof text) < 0) return ZXM_E_ARG;
        if ((rc = detect(n->link, n->el[order[k]].name, lang)) != ZXM_OK) return rc;
        if ((rc = enqueue(&t, NIGHTJAR_PAGE, (int32_t) order[k], text, lang)) != ZXM_OK) return rc;
    }
    *n = t;
    if (queued) *queued = count;
    return ZXM_OK;
}

int nightjar_focus(nightjar_t *n, uint32_t i)
{
    char text[NIGHTJAR_SAY_MAX + 1], lang[NIGHTJAR_TAG_MAX + 1];
    if (i >= n->nel) return ZXM_E_ARG;
    if (nightjar_describe(n, i, text, sizeof text) < 0) return ZXM_E_ARG;
    int rc = detect(n->link, n->el[i].name, lang);
    if (rc) return rc;
    if ((rc = enqueue(n, NIGHTJAR_FOCUS, (int32_t) i, text, lang)) != ZXM_OK) return rc;
    n->focus = (int32_t) i;
    return ZXM_OK;
}

int nightjar_where(nightjar_t *n)
{
    char host[NIGHTJAR_HOST_MAX + 1], text[NIGHTJAR_SAY_MAX + 1], lang[NIGHTJAR_TAG_MAX + 1];
    zxm_link_t *l = n->link;
    zxm_begin(l, "sys.info");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "host", host, sizeof host)) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; host[i]; i++)
        if ((unsigned char) host[i] < 0x20u || host[i] == 0x7f) return ZXM_E_PROTOCOL;
    uint32_t at = 0;
    text[0] = 0;
    if (n->focus >= 0 && (uint32_t) n->focus < n->nel) {
        int32_t k = nightjar_describe(n, (uint32_t) n->focus, text, sizeof text);
        if (k < 0) return ZXM_E_ARG;
        at = (uint32_t) k;
    } else {
        put(text, sizeof text, &at, "Nothing has focus");
    }
    if (host[0]) {
        put(text, sizeof text, &at, ", on ");
        put(text, sizeof text, &at, host);
    }
    if ((rc = detect(l, text, lang)) != ZXM_OK) return rc;
    if ((rc = enqueue(n, NIGHTJAR_FOCUS, n->focus, text, lang)) != ZXM_OK) return rc;
    memcpy(n->host, host, len_of(host, sizeof host) + 1u);
    return ZXM_OK;
}

int nightjar_next(nightjar_t *n, nightjar_say_t *out)
{
    if (!out) return ZXM_E_ARG;
    if (n->head == n->tail) return ZXM_E_STATE;
    *out = n->queue[n->tail % NIGHTJAR_QUEUE];
    n->tail++;
    n->cut = false;
    return ZXM_OK;
}

uint32_t nightjar_waiting(const nightjar_t *n)
{
    return n->head - n->tail;
}

int nightjar_set_voice(nightjar_t *n, int64_t wpm, int64_t pitch, bool owner_confirmed)
{
    if (!nightjar_voice_ok(wpm, pitch)) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE; /* NJ8: the voice is the owner's to set */
    n->wpm = wpm;
    n->pitch = pitch;
    return ZXM_OK;
}

int64_t nightjar_speak_ms(const char *text, int64_t wpm)
{
    if (!text || wpm < NIGHTJAR_WPM_MIN || wpm > NIGHTJAR_WPM_MAX) return -1;
    int64_t words = 0;
    bool in = false;
    for (uint32_t i = 0; text[i] && i < 65536u; i++) {
        bool space = text[i] == ' ' || text[i] == '\t' || text[i] == '\n';
        if (!space && !in) words++;
        in = !space;
    }
    return (words * 60000 + wpm - 1) / wpm;
}
