/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* talewind_model.c — Talewind, interactive stories (see talewind_model.h).
 * A drafted passage is the swarm's suggestion, held until the author
 * accepts it under Sieve's rule (sieve_rules.h SR1). */
#include <string.h>

#include "sieve_rules.h"
#include "talewind_model.h"

/* ---- text ------------------------------------------------------------------ */

/* The bytes of s before its NUL, looking at no more than most + 1 of them. */
static uint32_t bounded_len(const char *s, uint32_t most)
{
    uint32_t n = 0;
    while (n <= most && s[n]) n++;
    return n;
}

/* 1..most bytes, none below 0x20 (TM1). */
static bool text_ok(const char *s, uint32_t most)
{
    if (!s) return false;
    uint32_t n = bounded_len(s, most);
    if (n == 0 || n > most) return false;
    for (uint32_t i = 0; i < n; i++)
        if ((unsigned char) s[i] < 0x20u) return false;
    return true;
}

/* Append n bytes of s to out at *at, bounded by cap (room for a NUL is kept). */
static bool put_n(char *out, uint32_t cap, uint32_t *at, const char *s, uint32_t n)
{
    if (*at + n + 1u > cap) return false;
    memcpy(out + *at, s, n);
    *at += n;
    out[*at] = 0;
    return true;
}

static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    return put_n(out, cap, at, s, (uint32_t) strlen(s));
}

static bool put_num(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char t[24];
    int k = 23;
    bool neg = v < 0;
    uint64_t u = neg ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    t[k] = 0;
    do {
        t[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u && k > 1);
    if (neg) t[--k] = '-';
    return put(out, cap, at, t + k);
}

/* ---- the story as text (TM1) ------------------------------------------------ */

static const char HEAD[] = "talewind 1 ";

/* The whole story; its length, or -1 when it does not fit. */
static int32_t compose(const talewind_t *t, char *out, uint32_t cap)
{
    uint32_t at = 0;
    if (!put(out, cap, &at, HEAD) || !put_num(out, cap, &at, t->story) || !put(out, cap, &at, "\n"))
        return -1;
    for (uint32_t n = 1; n <= TALEWIND_PASSAGES; n++) {
        const talewind_passage_t *p = &t->passage[n];
        if (!p->present) continue;
        if (!put(out, cap, &at, "p ") || !put_num(out, cap, &at, n) || !put(out, cap, &at, " ") ||
            !put_num(out, cap, &at, p->rating) || !put(out, cap, &at, " ") ||
            !put(out, cap, &at, p->text) || !put(out, cap, &at, "\n"))
            return -1;
    }
    for (uint32_t n = 1; n <= TALEWIND_PASSAGES; n++) {
        const talewind_passage_t *p = &t->passage[n];
        for (uint32_t k = 0; p->present && k < p->nchoices; k++) {
            const talewind_choice_t *c = &p->choice[k];
            if (!put(out, cap, &at, "c ") || !put_num(out, cap, &at, n) ||
                !put(out, cap, &at, " ") || !put_num(out, cap, &at, k + 1u) ||
                !put(out, cap, &at, " ") || !put_num(out, cap, &at, c->target) ||
                !put(out, cap, &at, " ") || !put_num(out, cap, &at, c->need) ||
                !put(out, cap, &at, " ") || !put_num(out, cap, &at, c->sets) ||
                !put(out, cap, &at, " ") || !put(out, cap, &at, c->label) ||
                !put(out, cap, &at, "\n"))
                return -1;
        }
    }
    return (int32_t) at;
}

/* A plain decimal of 1 to 6 digits, no leading zero, at s[*at], ended by ' '. */
static bool get_num(const char *s, uint32_t n, uint32_t *at, int64_t *v)
{
    uint32_t i = *at, digits = 0;
    int64_t x = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9' && digits < 7u) {
        x = x * 10 + (s[i] - '0');
        i++;
        digits++;
    }
    if (!digits || digits > 6u || i >= n || s[i] != ' ') return false;
    if (digits > 1u && s[*at] == '0') return false;
    *v = x;
    *at = i + 1u;
    return true;
}

/* The rest of the line, 1..most bytes with none below 0x20, into out. */
static bool get_text(const char *s, uint32_t n, uint32_t *at, char *out, uint32_t most)
{
    uint32_t i = *at;
    while (i < n && s[i] != '\n') {
        if ((unsigned char) s[i] < 0x20u) return false;
        i++;
    }
    uint32_t len = i - *at;
    if (i >= n || len == 0 || len > most) return false;
    memcpy(out, s + *at, len);
    out[len] = 0;
    *at = i + 1u;
    return true;
}

static bool passage_no(int64_t n)
{
    return n >= 1 && n <= (int64_t) TALEWIND_PASSAGES;
}

/* Parse text into next (TM1): false when it is not a story. */
static bool parse(talewind_t *next, const char *s, uint32_t n)
{
    uint32_t at = (uint32_t) strlen(HEAD);
    int64_t story, last_p = 0, last_c = 0;
    bool choices = false;
    if (n < at || memcmp(s, HEAD, at)) return false;
    uint32_t i = at;
    while (i < n && s[i] != '\n') i++;
    if (i >= n || i == at || i - at > 6u || (i - at > 1u && s[at] == '0')) return false;
    story = 0;
    for (uint32_t k = at; k < i; k++) {
        if (s[k] < '0' || s[k] > '9') return false;
        story = story * 10 + (s[k] - '0');
    }
    if (story < 1) return false;
    next->story = story;
    memset(next->passage, 0, sizeof next->passage);
    at = i + 1u;
    while (at < n) {
        int64_t p, a, b, c, d;
        if (n - at < 2u || s[at + 1u] != ' ') return false;
        char kind = s[at];
        at += 2u;
        if (kind == 'p' && !choices) {
            if (!get_num(s, n, &at, &p) || !get_num(s, n, &at, &a) || !passage_no(p) ||
                p <= last_p || a > TALEWIND_RATING_MAX)
                return false;
            talewind_passage_t *q = &next->passage[p];
            if (!get_text(s, n, &at, q->text, TALEWIND_TEXT_MAX) ||
                !talewind_passage_ok(story, p, a, (int64_t) strlen(q->text)))
                return false;
            q->present = true;
            q->rating = a;
            last_p = p;
        } else if (kind == 'c') {
            choices = true;
            if (!get_num(s, n, &at, &p) || !get_num(s, n, &at, &a) || !get_num(s, n, &at, &b) ||
                !get_num(s, n, &at, &c) || !get_num(s, n, &at, &d) || !passage_no(p) ||
                !passage_no(b) || !next->passage[p].present || !next->passage[b].present ||
                !talewind_mask_ok(c) || !talewind_mask_ok(d) ||
                !talewind_choice_ok(story, p, a, b, c, d))
                return false;
            talewind_passage_t *q = &next->passage[p];
            /* by passage, then k = 1, 2, ... with no gap */
            if (p < last_c || a != (int64_t) q->nchoices + 1 || a > (int64_t) TALEWIND_CHOICES)
                return false;
            talewind_choice_t *e = &q->choice[q->nchoices];
            if (!get_text(s, n, &at, e->label, TALEWIND_LABEL_MAX)) return false;
            e->target = b;
            e->need = c;
            e->sets = d;
            q->nchoices++;
            last_c = p;
        } else {
            return false;
        }
    }
    return true;
}

/* ---- zxvd ------------------------------------------------------------------ */

/* One request's text: whole lines of at most this many bytes. Each byte
 * escapes to at most two, well inside the request line. */
#define PIECE 6000u

static int write_text(zxm_link_t *l, const char *path, const char *text, uint32_t len)
{
    uint32_t at = 0;
    bool first = true;
    do {
        uint32_t n = len - at;
        if (n > PIECE) {
            n = PIECE;
            while (n && text[at + n - 1u] != '\n') n--;
            if (!n) return ZXM_E_TOOBIG;
        }
        bool last = at + n >= len;
        zxm_begin(l, "files.write");
        zxm_p_str(l, "path", path);
        zxm_p_strn(l, "text", text + at, n);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        int rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
    } while (at < len);
    return ZXM_OK;
}

static talewind_t NEXT; /* a change before it is written (TM2) */

/* Write NEXT's story whole, then keep it (TM2). */
static int commit(talewind_t *t)
{
    static char text[TALEWIND_FILE_MAX];
    int32_t len = compose(&NEXT, text, sizeof text);
    if (len < 0) return ZXM_E_TOOBIG;
    int rc = write_text(t->link, t->path, text, (uint32_t) len);
    if (rc) return rc;
    memcpy(t->passage, NEXT.passage, sizeof t->passage);
    t->version++;
    return ZXM_OK;
}

static void begin_change(const talewind_t *t)
{
    NEXT.story = t->story;
    memcpy(NEXT.passage, t->passage, sizeof NEXT.passage);
}

void talewind_init(talewind_t *t, zxm_link_t *link)
{
    memset(t, 0, sizeof *t);
    t->link = link;
}

int talewind_open(talewind_t *t, const char *path, int64_t story)
{
    static char text[TALEWIND_FILE_MAX];
    static char chunk[TALEWIND_FILE_MAX];
    zxm_link_t *l = t->link;
    if (!text_ok(path, TALEWIND_PATH_MAX) || story < 1 || story > TALEWIND_STORY_FIELD)
        return ZXM_E_ARG;
    uint32_t len = 0;
    uint64_t off = 0;
    for (;;) {
        zxm_begin(l, "files.read");
        zxm_p_str(l, "path", path);
        zxm_p_u64(l, "offset", off);
        int rc = zxm_call(l);
        if (rc == ZXM_E_REMOTE && l->err_code == TALEWIND_E_FILES && !off &&
            strstr(l->err, "no such file")) {
            talewind_init(t, l); /* not made yet: a new, empty story */
            memcpy(t->path, path, strlen(path) + 1u);
            t->story = story;
            t->opened = true;
            return ZXM_OK;
        }
        if (rc) return rc;
        uint64_t size, next_off;
        bool eof;
        int32_t tk = zxm_get(l, l->result, "text");
        if (zxm_get_u64(l, l->result, "size", &size) ||
            zxm_get_u64(l, l->result, "next", &next_off) ||
            zxm_get_bool(l, l->result, "eof", &eof) || tk < 0)
            return ZXM_E_PROTOCOL;
        if (size >= TALEWIND_FILE_MAX) return ZXM_E_TOOBIG;
        int32_t n = w4_json_str(l->resp, &l->tok[tk], chunk, sizeof chunk);
        if (n < 0 || len + (uint32_t) n >= TALEWIND_FILE_MAX) return ZXM_E_TOOBIG;
        memcpy(text + len, chunk, (size_t) n);
        len += (uint32_t) n;
        if (eof) break;
        if (next_off <= off) return ZXM_E_PROTOCOL;
        off = next_off;
    }
    if (!parse(&NEXT, text, len)) {
        t->opened = false; /* TM1: never overwritten */
        return ZXM_E_PROTOCOL;
    }
    talewind_init(t, l);
    memcpy(t->path, path, strlen(path) + 1u);
    t->story = NEXT.story;
    memcpy(t->passage, NEXT.passage, sizeof t->passage);
    t->opened = true;
    return ZXM_OK;
}

int talewind_set_passage(talewind_t *t, int64_t n, int64_t rating, const char *text,
                         bool owner_confirmed)
{
    if (!passage_no(n) || rating < 0 || rating > TALEWIND_RATING_MAX ||
        !text_ok(text, TALEWIND_TEXT_MAX))
        return ZXM_E_ARG;
    if (!owner_confirmed || !t->opened) return ZXM_E_STATE;
    begin_change(t);
    talewind_passage_t *p = &NEXT.passage[n];
    p->present = true;
    p->rating = rating;
    memcpy(p->text, text, strlen(text) + 1u);
    return commit(t);
}

int talewind_add_choice(talewind_t *t, int64_t n, int64_t target, int64_t need, int64_t sets,
                        const char *label, bool owner_confirmed)
{
    if (!passage_no(n) || !passage_no(target) || !talewind_mask_ok(need) ||
        !talewind_mask_ok(sets) || !text_ok(label, TALEWIND_LABEL_MAX))
        return ZXM_E_ARG;
    if (!owner_confirmed || !t->opened) return ZXM_E_STATE;
    const talewind_passage_t *p = &t->passage[n];
    if (!p->present || !t->passage[target].present) return ZXM_E_ARG;
    if (p->nchoices >= TALEWIND_CHOICES) return ZXM_E_TOOBIG;
    begin_change(t);
    talewind_passage_t *q = &NEXT.passage[n];
    talewind_choice_t *c = &q->choice[q->nchoices];
    c->target = target;
    c->need = need;
    c->sets = sets;
    memcpy(c->label, label, strlen(label) + 1u);
    q->nchoices++;
    return commit(t);
}

/* ---- drafts (TM3) ---------------------------------------------------------- */

static const char ASK_HEAD[] = "Draft one passage of an interactive story, as plain text in "
                               "one paragraph, for age rating ";
static const char ASK_RATING[] = " of 0 (all ages) to 4 (adults only). The author asks: ";

int talewind_draft(talewind_t *t, int64_t n, int64_t rating, const char *prompt, int64_t now,
                   int64_t ttl)
{
    static char ask[ZXM_ASK_MAX + 1];
    static char got[ZXM_ANSWER_MAX + 1];
    zxm_link_t *l = t->link;
    uint32_t room = ZXM_ASK_MAX - (uint32_t) (sizeof ASK_HEAD - 1u + 1u + sizeof ASK_RATING - 1u);
    if (!passage_no(n) || rating < 0 || rating > TALEWIND_RATING_MAX || !text_ok(prompt, room) ||
        !sieve_hold_ok(SIEVE_SRC_ANSWER, ttl, now))
        return ZXM_E_ARG;
    if (!t->opened || t->draft.pending) return ZXM_E_STATE;
    uint32_t at = 0;
    char digit[2] = {(char) ('0' + (int) rating), 0};
    if (!put(ask, sizeof ask, &at, ASK_HEAD) || !put(ask, sizeof ask, &at, digit) ||
        !put(ask, sizeof ask, &at, ASK_RATING) || !put(ask, sizeof ask, &at, prompt))
        return ZXM_E_TOOBIG;
    zxm_begin(l, "swarm.ask");
    zxm_p_str(l, "prompt", ask);
    int rc = zxm_call(l);
    if (rc) return rc;
    rc = zxm_get_str(l, l->result, "answer", got, sizeof got);
    if (rc) return rc == ZXM_E_TOOBIG ? ZXM_E_TOOBIG : ZXM_E_PROTOCOL;
    talewind_draft_t *d = &t->draft;
    uint32_t len = bounded_len(got, ZXM_ANSWER_MAX);
    /* line breaks and other controls become spaces; then the ends are trimmed */
    for (uint32_t i = 0; i < len; i++)
        if ((unsigned char) got[i] < 0x20u) got[i] = ' ';
    uint32_t from = 0;
    while (from < len && got[from] == ' ') from++;
    while (len > from && got[len - 1u] == ' ') len--;
    if (len == from) return ZXM_E_PROTOCOL; /* nothing to hold */
    uint32_t keep = len - from;
    bool cut = keep > TALEWIND_TEXT_MAX;
    if (cut) keep = zxm_utf8_cut(got + from, TALEWIND_TEXT_MAX);
    memset(d, 0, sizeof *d);
    memcpy(d->text, got + from, keep);
    d->text[keep] = 0;
    d->cut = cut;
    d->passage = n;
    d->rating = rating;
    d->made = now;
    d->ttl = ttl;
    d->version = t->version;
    /* zxm.h M4: swarm.ask reports no count, so one token per four bytes */
    memcpy(d->cost.agent, "swarm", 6);
    d->cost.tokens = ((uint64_t) at + len + 3u) / 4u;
    d->cost.exact = false;
    d->pending = true;
    return ZXM_OK;
}

int talewind_draft_accept(talewind_t *t, int64_t now, bool owner_confirmed)
{
    if (now < 0 || now > SIEVE_NOW_MAX) return ZXM_E_ARG;
    if (!owner_confirmed || !t->opened || !t->draft.pending) return ZXM_E_STATE;
    const talewind_draft_t *d = &t->draft;
    int64_t left = sieve_seconds_left(d->made, d->ttl, now);
    int64_t drift = sieve_drift(t->version, d->version);
    if (!sieve_apply_ok(1, left, drift)) return ZXM_E_STATE; /* lapsed, or the story changed */
    begin_change(t);
    talewind_passage_t *p = &NEXT.passage[d->passage];
    p->present = true;
    p->rating = d->rating;
    memcpy(p->text, d->text, strlen(d->text) + 1u);
    int rc = commit(t);
    if (rc) return rc;
    memset(&t->draft, 0, sizeof t->draft);
    return ZXM_OK;
}

int talewind_draft_reject(talewind_t *t, bool owner_confirmed)
{
    if (!owner_confirmed || !t->draft.pending) return ZXM_E_STATE;
    memset(&t->draft, 0, sizeof t->draft);
    return ZXM_OK;
}

/* ---- reading (TM4) --------------------------------------------------------- */

int talewind_read_begin(talewind_t *t, int64_t ceiling)
{
    if (ceiling < 0 || ceiling > TALEWIND_RATING_MAX) return ZXM_E_ARG;
    const talewind_passage_t *p = &t->passage[1];
    if (!t->opened || !p->present || talewind_rating_room(ceiling, p->rating) < 0)
        return ZXM_E_STATE;
    t->reading = true;
    t->at = 1;
    t->held = 0;
    t->ceiling = ceiling;
    return ZXM_OK;
}

bool talewind_shown(const talewind_t *t, uint32_t k)
{
    if (!t->reading || !passage_no(t->at)) return false;
    const talewind_passage_t *p = &t->passage[t->at];
    if (k >= p->nchoices) return false;
    const talewind_choice_t *c = &p->choice[k];
    if (!passage_no(c->target) || !t->passage[c->target].present) return false;
    int64_t lacking = talewind_flags_lacking(c->need, t->held);
    int64_t room = talewind_rating_room(t->ceiling, t->passage[c->target].rating);
    return talewind_choice_shown(lacking, room);
}

uint32_t talewind_shown_list(const talewind_t *t, uint32_t out[TALEWIND_CHOICES])
{
    uint32_t n = 0;
    for (uint32_t k = 0; k < TALEWIND_CHOICES; k++)
        if (talewind_shown(t, k)) out[n++] = k;
    return n;
}

int talewind_choose(talewind_t *t, uint32_t k)
{
    if (!talewind_shown(t, k)) return ZXM_E_STATE;
    const talewind_choice_t *c = &t->passage[t->at].choice[k];
    int64_t after = talewind_flags_after(t->held, c->sets);
    if (after < 0) return ZXM_E_STATE;
    t->held = after;
    t->at = c->target;
    return ZXM_OK;
}
