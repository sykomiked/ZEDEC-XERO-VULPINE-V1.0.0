/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_chgvoice.c — the Chiglet speaks, when it is asked to. See zui_chgvoice.h. */
#include "zui_chgvoice.h"
#include "zui_motion.h"
#include "zui_tokens.h"

/* ---- small string helpers (no libc) --------------------------------------- */

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    if (s)
        while (s[n]) n++;
    return n;
}

static bool seq(const char *a, const char *b)
{
    uint32_t i = 0;
    for (; a[i] && a[i] == b[i]; i++) {
    }
    return a[i] == b[i];
}

static bool starts(const char *s, const char *p)
{
    uint32_t i = 0;
    for (; p[i]; i++)
        if (s[i] != p[i]) return false;
    return true;
}

/* back off from n to a UTF-8 character boundary of s */
static uint32_t utf8_floor(const char *s, uint32_t n)
{
    while (n > 0 && ((uint8_t) s[n] & 0xC0u) == 0x80u) n--;
    return n;
}

/* copy at most cap - 1 bytes, never splitting a character */
static uint32_t scopy(char *d, uint32_t cap, const char *s, uint32_t n)
{
    if (!cap) return 0;
    if (n > cap - 1) n = utf8_floor(s, cap - 1);
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
    d[n] = 0;
    return n;
}

typedef struct {
    char *b;
    uint32_t cap, n;
} sb_t;

static void sb_put(sb_t *o, const char *s, uint32_t n)
{
    if (o->n + 1 >= o->cap) return;
    uint32_t room = o->cap - 1 - o->n;
    if (n > room) n = utf8_floor(s, room);
    for (uint32_t i = 0; i < n; i++) o->b[o->n + i] = s[i];
    o->n += n;
    o->b[o->n] = 0;
}

static void sb_u(sb_t *o, uint32_t u)
{
    char d[10];
    uint32_t k = 0;
    do {
        d[k++] = (char) ('0' + u % 10u);
        u /= 10u;
    } while (u && k < 10);
    char r[10];
    for (uint32_t i = 0; i < k; i++) r[i] = d[k - 1 - i];
    sb_put(o, r, k);
}

/* a pattern: each %s takes the next string, %u the number */
static void sb_fmt(sb_t *o, const char *pat, const char *s1, const char *s2, uint32_t u)
{
    const char *ss[2] = {s1 ? s1 : "", s2 ? s2 : ""};
    uint32_t si = 0;
    for (const char *p = pat; p && *p; p++) {
        if (p[0] == '%' && p[1] == 's') {
            const char *v = si < 2 ? ss[si++] : "";
            sb_put(o, v, slen(v));
            p++;
        } else if (p[0] == '%' && p[1] == 'u') {
            sb_u(o, u);
            p++;
        } else {
            sb_put(o, p, 1);
        }
    }
}

/* ---- CV4: the words --------------------------------------------------------- */

const zui_cv_words_t zui_cv_words_en = {
    .tag = "en",
    .need_one = "One thing is waiting for you.",
    .need_one_what = "One thing is waiting for you: %s.",
    .need_many = "%u things are waiting for you.",
    .need_many_what = "%u things are waiting for you, first %s.",
    .need_none = "Nothing needs you right now.",
    .slow = "I am running slowly.",
    .failing = "Something in me is failing.",
    .stopped = "I have stopped.",
    .feel = "I feel %s.",
    .feel_very = "I feel very %s.",
    .feel_why = "I feel %s, because %s.",
    .feel_very_why = "I feel very %s, because %s.",
    .failed_one = "One task failed.",
    .failed_many = "%u tasks failed.",
    .running_one = "One task is working.",
    .running_many = "%u tasks are working.",
    .running_none = "Nothing is running.",
    .finished_one = "One task finished.",
    .finished_many = "%u tasks finished.",
    .budget = "I have %u percent of my token budget left.",
    .same = "Nothing has changed.",
    .no_voice = "No voice for %s yet, so here are the words.",
    .not_found = "I cannot find %s.",
    .reading = "Reading %s.",
    .nothing = "There is nothing to read.",
    .say = &speech_tidy_en,
    .call_hello = "I am here.",
    .call_bye = "Bye for now.",
    .call_end = "End",
    .call_you = "You: %s",
    .visiting = "%s is visiting.",
    .name = "Chiglet",
};

void zui_cv_facts_init(zui_cv_facts_t *f)
{
    if (!f) return;
    f->need_you = f->running = f->finished = f->failed = f->budget_pct = ZUI_CV_UNKNOWN;
    f->need_what = f->why = 0;
}

static bool known(uint32_t v)
{
    return v != ZUI_CV_UNKNOWN;
}

uint32_t zui_cv_status_text(const zui_cv_words_t *w, const zui_cv_facts_t *f, swarm_feeling_t feel,
                            zui_health_t h, bool full, char *out, uint32_t cap)
{
    if (!out || !cap) return 0;
    out[0] = 0;
    if (!w) w = &zui_cv_words_en;
    zui_cv_facts_t none;
    if (!f) {
        zui_cv_facts_init(&none);
        f = &none;
    }
    sb_t o = {out, cap, 0};
    uint32_t said = 0, limit = full ? 0xFFFFFFFFu : 2u;
    const char *pat;
#define SAY(p, a, b, u)                                                                            \
    do {                                                                                           \
        if (said < limit && (p)) {                                                                 \
            if (said) sb_put(&o, " ", 1);                                                          \
            sb_fmt(&o, (p), (a), (b), (u));                                                        \
            said++;                                                                                \
        }                                                                                          \
    } while (0)

    /* CV10: the facts' words as a person would say them */
    char nw[ZUI_CV_SMALL * 2], wy[ZUI_CV_SMALL * 2];
    const char *need_what = f->need_what, *why_s = f->why;
    if (need_what && need_what[0]) {
        speech_tidy(need_what, slen(need_what), nw, sizeof nw, w->say);
        need_what = nw;
    }
    if (why_s && why_s[0]) {
        speech_tidy(why_s, slen(why_s), wy, sizeof wy, w->say);
        why_s = wy;
    }
    bool what = need_what && need_what[0];
    if (known(f->need_you)) {
        if (f->need_you == 0)
            SAY(w->need_none, 0, 0, 0);
        else if (f->need_you == 1)
            SAY(what ? w->need_one_what : w->need_one, need_what, 0, 1);
        else
            SAY(what ? w->need_many_what : w->need_many, need_what, 0, f->need_you);
    }
    if (h == ZUI_HEALTH_SLOW) SAY(w->slow, 0, 0, 0);
    if (h == ZUI_HEALTH_FAILING) SAY(w->failing, 0, 0, 0);
    if (h == ZUI_HEALTH_STOPPED) SAY(w->stopped, 0, 0, 0);
    bool very = feel.intensity >= 4u;
    bool why = why_s && why_s[0];
    pat = why ? (very ? w->feel_very_why : w->feel_why) : (very ? w->feel_very : w->feel);
    SAY(pat, zui_emote_word(feel.emotion), why_s, 0);
    if (known(f->failed) && f->failed)
        SAY(f->failed == 1 ? w->failed_one : w->failed_many, 0, 0, f->failed);
    if (known(f->running))
        SAY(f->running == 0   ? w->running_none
            : f->running == 1 ? w->running_one
                              : w->running_many,
            0, 0, f->running);
    if (known(f->finished) && f->finished)
        SAY(f->finished == 1 ? w->finished_one : w->finished_many, 0, 0, f->finished);
    if (known(f->budget_pct)) SAY(w->budget, 0, 0, f->budget_pct > 100u ? 100u : f->budget_pct);
#undef SAY
    return o.n;
}

/* ---- CV5: sentences --------------------------------------------------------- */

static bool space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/* a sentence end at i: returns its length in bytes (0 = none) */
static uint32_t sentence_end(const char *t, uint32_t len, uint32_t i)
{
    char c = t[i];
    if (c == '\n') return 1;
    if (c == '.' || c == '!' || c == '?') {
        uint32_t j = i + 1;
        while (j < len && (t[j] == '.' || t[j] == '!' || t[j] == '?' || t[j] == '"' ||
                           t[j] == '\'' || t[j] == ')' || t[j] == ']'))
            j++;
        if (j >= len || space(t[j]) || t[j] == '\n') return j - i;
        return 0;
    }
    if (i + 2 < len && (uint8_t) c == 0xE3u && (uint8_t) t[i + 1] == 0x80u &&
        (uint8_t) t[i + 2] == 0x82u)
        return 3; /* 。 */
    if (i + 2 < len && (uint8_t) c == 0xEFu && (uint8_t) t[i + 1] == 0xBCu &&
        ((uint8_t) t[i + 2] == 0x81u || (uint8_t) t[i + 2] == 0x9Fu))
        return 3; /* ！ ？ */
    return 0;
}

uint32_t zui_cv_chunk_end(const char *t, uint32_t len, uint32_t *start, uint32_t max)
{
    if (!t || !start) return 0;
    uint32_t s = *start;
    while (s < len && (space(t[s]) || t[s] == '\n')) s++;
    *start = s;
    if (s >= len) return s;
    if (max < 4) max = 4;
    uint32_t lim = len - s > max ? s + max : len;
    for (uint32_t i = s; i < lim; i++) {
        uint32_t e = sentence_end(t, len, i);
        if (e) {
            uint32_t end = i + e;
            if (end <= lim) return end;
            break;
        }
    }
    if (lim == len) return len;
    /* too long: the last space inside, else a character boundary */
    for (uint32_t i = lim; i > s + 1; i--)
        if (space(t[i - 1])) return i - 1;
    uint32_t b = utf8_floor(t, lim);
    return b > s ? b : lim;
}

/* ---- set up ------------------------------------------------------------------ */

void zui_cv_init(zui_cv_t *cv, const zui_chiglet_t *who, zui_cv_speak_fn speak, zui_cv_stop_fn stop,
                 void *ctx)
{
    if (!cv) return;
    uint8_t *p = (uint8_t *) cv;
    for (uint32_t i = 0; i < sizeof *cv; i++) p[i] = 0;
    if (who) cv->who = *who;
    cv->speak = speak;
    cv->stop = stop;
    cv->ctx = ctx;
    cv->words = &zui_cv_words_en;
    scopy(cv->lang, sizeof cv->lang, "en", 2);
    cv->mode = ZUI_CV_VOICE;
    cv->volume = 70;
    cv->rate_pct = 100;
    cv->resume_at = ZUI_CV_UNKNOWN;
    cv->feeling.emotion = SWARM_EMO_CALM;
    cv->feeling.intensity = 1;
    zui_emote_init(&cv->face, cv->feeling, 0);
    cv->anchor = (zui_rect_t){0, 0, 48, 48};
}

void zui_cv_set_words(zui_cv_t *cv, const zui_cv_words_t *w)
{
    if (cv) cv->words = w ? w : &zui_cv_words_en;
}

void zui_cv_set_lang(zui_cv_t *cv, const char *bcp47)
{
    if (cv) scopy(cv->lang, sizeof cv->lang, bcp47 ? bcp47 : "", slen(bcp47));
}

void zui_cv_set_mode(zui_cv_t *cv, zui_cv_mode_t m)
{
    if (cv) cv->mode = m == ZUI_CV_WORDS ? ZUI_CV_WORDS : ZUI_CV_VOICE;
}

void zui_cv_set_volume(zui_cv_t *cv, uint32_t v)
{
    if (cv) cv->volume = v > 100u ? 100u : v;
}

void zui_cv_set_rate(zui_cv_t *cv, uint32_t pct)
{
    if (cv) cv->rate_pct = pct < 50u ? 50u : pct > 200u ? 200u : pct;
}

void zui_cv_set_anchor(zui_cv_t *cv, zui_rect_t r)
{
    if (cv && r.w > 0 && r.h > 0) cv->anchor = r;
}

void zui_cv_set_section(zui_cv_t *cv, zui_cv_section_fn fn, void *ctx)
{
    if (!cv) return;
    cv->section = fn;
    cv->section_ctx = ctx;
}

void zui_cv_set_feeling(zui_cv_t *cv, swarm_feeling_t f, zui_health_t h, uint32_t now_ms)
{
    if (!cv) return;
    if (f.emotion != cv->feeling.emotion || f.intensity != cv->feeling.intensity)
        zui_emote_set(&cv->face, f, now_ms);
    cv->feeling = f;
    cv->health = h;
}

void zui_cv_set_genes(zui_cv_t *cv, const zui_chiglet_t *who)
{
    if (cv && who) cv->who = *who;
}

bool zui_cv_talking(const zui_cv_t *cv)
{
    return cv && (cv->playing || cv->waiting || cv->words_until || cv->pos < cv->len);
}

const char *zui_cv_bubble(const zui_cv_t *cv)
{
    return cv ? cv->chunk : "";
}

/* ---- CV9: the pop-up's phases ---------------------------------------------- */

static uint32_t rise_ms(void)
{
    return zui_motion_duration(ZUI_CV_RISE_MS);
}

/* how far up it is, 0..256 */
static uint32_t up_q8(const zui_cv_t *cv, uint32_t now)
{
    uint32_t d = rise_ms(), e = now - cv->phase_ms;
    switch (cv->phase) {
    case ZUI_CV_RISING:
        return d == 0 || e >= d ? 256u : e * 256u / d;
    case ZUI_CV_SHOWN:
        return 256u;
    case ZUI_CV_SETTLING:
        return d == 0 || e >= d ? 0u : 256u - e * 256u / d;
    default:
        return 0u;
    }
}

static void pop_up(zui_cv_t *cv, uint32_t now)
{
    if (cv->phase == ZUI_CV_RISING || cv->phase == ZUI_CV_SHOWN) return;
    uint32_t at = up_q8(cv, now), d = rise_ms();
    cv->phase = ZUI_CV_RISING;
    /* from part way down: start the rise as far along as it already is */
    cv->phase_ms = now - (d ? at * d / 256u : 0u);
    if (d == 0) cv->phase = ZUI_CV_SHOWN;
}

/* ---- CV5, CV6, CV8: giving chunks to the speaker ------------------------- */

static uint32_t words_ms(const zui_cv_t *cv, uint32_t bytes)
{
    uint32_t ms = 600u + 60u * bytes;
    ms = ms * 100u / (cv->rate_pct ? cv->rate_pct : 100u);
    return ms < 1500u ? 1500u : ms > 12000u ? 12000u : ms;
}

static void finish(zui_cv_t *cv, uint32_t now)
{
    cv->playing = cv->waiting = false;
    cv->words_until = 0;
    cv->pos = cv->len;
    cv->hold_until = now + ZUI_CV_HOLD_MS;
    if (cv->hold_until == 0) cv->hold_until = 1;
}

static void next_chunk(zui_cv_t *cv, uint32_t now)
{
    for (;;) {
        if (cv->busy) {
            cv->waiting = true;
            return;
        }
        cv->waiting = false;
        uint32_t s = cv->pos;
        uint32_t e = zui_cv_chunk_end(cv->text, cv->len, &s, ZUI_CV_CHUNK - 1u);
        if (e <= s) {
            finish(cv, now);
            return;
        }
        cv->cur_start = s;
        cv->pos = e;
        scopy(cv->chunk, sizeof cv->chunk, cv->text + s, e - s);
        cv->hold_until = 0;
        if (!cv->words_only) {
            int r = -1;
            if (cv->speak) {
                cv->in_speak = true;
                cv->done_early = false;
                r = cv->speak(cv->chunk, cv->lang, cv->volume, cv->rate_pct, cv->ctx);
                cv->in_speak = false;
            }
            if (r >= 0) {
                if (cv->done_early) { /* it played inside speak: go on */
                    cv->done_early = false;
                    cv->said++;
                    continue;
                }
                cv->playing = true;
                return;
            }
            cv->refused++;
            cv->words_only = true;
            if (!cv->noted) {
                sb_t o = {cv->note, sizeof cv->note, 0};
                cv->note[0] = 0;
                sb_fmt(&o, cv->words->no_voice, cv->lang[0] ? cv->lang : "this language", 0, 0);
                cv->noted = true;
            }
        }
        cv->words_until = now + words_ms(cv, e - s);
        if (cv->words_until == 0) cv->words_until = 1;
        return;
    }
}

static void quiet_now(zui_cv_t *cv)
{
    if (cv->playing && cv->stop) cv->stop(cv->ctx);
    cv->playing = cv->waiting = false;
    cv->words_until = 0;
    cv->level = 0;
}

/* start saying cv->text[0..len) */
static zui_cv_result_t begin_at(zui_cv_t *cv, zui_cv_kind_t kind, uint32_t from, uint32_t now)
{
    quiet_now(cv);
    cv->now = now;
    cv->kind = kind;
    cv->pos = from;
    cv->cur_start = from;
    cv->call_last = now; /* CV11: a call with words in it is not idle */
    cv->words_only = cv->mode == ZUI_CV_WORDS;
    cv->noted = false;
    cv->note[0] = 0;
    cv->chunk[0] = 0;
    if (kind != ZUI_CV_K_READ) cv->resume_at = ZUI_CV_UNKNOWN;
    pop_up(cv, now);
    next_chunk(cv, now);
    return ZUI_CV_SPEAKING;
}

static zui_cv_result_t begin(zui_cv_t *cv, zui_cv_kind_t kind, uint32_t now)
{
    return begin_at(cv, kind, 0, now);
}

static void set_text(zui_cv_t *cv, const char *s)
{
    cv->len = scopy(cv->text, sizeof cv->text, s, slen(s));
}

/* put "Reading <title>. " in front of cv->text */
static void prepend_title(zui_cv_t *cv, const char *title)
{
    if (!title || !title[0] || !cv->words->reading) return;
    char head[ZUI_CV_SMALL * 2];
    sb_t o = {head, sizeof head, 0};
    head[0] = 0;
    sb_fmt(&o, cv->words->reading, title, 0, 0);
    sb_put(&o, " ", 1);
    uint32_t h = o.n, cap = sizeof cv->text - 1u;
    if (h >= cap) return;
    uint32_t keep = cv->len;
    if (keep > cap - h) keep = utf8_floor(cv->text, cap - h);
    for (uint32_t i = keep; i > 0; i--) cv->text[i - 1 + h] = cv->text[i - 1];
    for (uint32_t i = 0; i < h; i++) cv->text[i] = head[i];
    cv->len = keep + h;
    cv->text[cv->len] = 0;
}

/* ---- CV11: face to face ------------------------------------------------------- */

void zui_cv_set_name(zui_cv_t *cv, const char *name)
{
    if (cv) scopy(cv->name, sizeof cv->name, name ? name : "", slen(name));
}

void zui_cv_set_call_hook(zui_cv_t *cv, zui_cv_call_fn fn, void *ctx)
{
    if (!cv) return;
    cv->on_call = fn;
    cv->call_ctx = ctx;
}

bool zui_cv_in_call(const zui_cv_t *cv)
{
    return cv && cv->call;
}

static void end_call(zui_cv_t *cv, bool say, uint32_t now)
{
    cv->call = cv->visiting = cv->listening = false;
    cv->heard[0] = 0;
    cv->hold_until = now ? now : 1u;
    if (cv->on_call) cv->on_call(false, cv->call_ctx);
    if (say && cv->words->call_bye) {
        set_text(cv, cv->words->call_bye);
        begin(cv, ZUI_CV_K_NOTE, now);
    } else {
        quiet_now(cv);
        cv->pos = cv->len;
    }
}

zui_cv_result_t zui_cv_call(zui_cv_t *cv, bool open, uint32_t now_ms)
{
    if (!cv) return ZUI_CV_BAD;
    if (open == cv->call) return ZUI_CV_NOTHING;
    if (!open) {
        end_call(cv, true, now_ms);
        return ZUI_CV_STOPPED;
    }
    cv->call = true;
    cv->calls++;
    cv->call_ms = cv->call_last = now_ms;
    cv->heard[0] = 0;
    pop_up(cv, now_ms);
    if (cv->on_call) cv->on_call(true, cv->call_ctx);
    if (!cv->words->call_hello) return ZUI_CV_SPEAKING;
    set_text(cv, cv->words->call_hello);
    return begin(cv, ZUI_CV_K_NOTE, now_ms);
}

void zui_cv_listening(zui_cv_t *cv, bool on, uint32_t now_ms)
{
    if (!cv) return;
    cv->listening = on;
    cv->call_last = now_ms;
}

void zui_cv_heard(zui_cv_t *cv, const char *utf8, uint32_t now_ms)
{
    if (!cv) return;
    cv->listening = false;
    cv->call_last = now_ms;
    scopy(cv->heard, sizeof cv->heard, utf8 ? utf8 : "", slen(utf8));
}

zui_cv_result_t zui_cv_visit(zui_cv_t *cv, const zui_chiglet_t *who, const char *name,
                             swarm_feeling_t feeling, uint32_t now_ms)
{
    if (!cv || !who) return ZUI_CV_BAD;
    if (!cv->call) return ZUI_CV_NOTHING; /* only in a call the person opened */
    cv->guest = *who;
    cv->guest.mouth = 0;
    scopy(cv->guest_name, sizeof cv->guest_name, name ? name : "", slen(name));
    cv->guest_feeling = feeling;
    cv->visiting = true;
    cv->call_last = now_ms;
    return ZUI_CV_SET;
}

void zui_cv_visit_end(zui_cv_t *cv, uint32_t now_ms)
{
    if (!cv) return;
    cv->visiting = false;
    cv->call_last = now_ms;
}

/* ---- CV2, CV3, CV4: asking --------------------------------------------------- */

zui_cv_result_t zui_cv_status(zui_cv_t *cv, const zui_cv_facts_t *f, bool full, uint32_t now_ms)
{
    if (!cv) return ZUI_CV_BAD;
    quiet_now(cv);
    cv->len =
        zui_cv_status_text(cv->words, f, cv->feeling, cv->health, full, cv->text, sizeof cv->text);
    if (cv->have_status && now_ms - cv->last_status_ms < ZUI_CV_SAME_MS &&
        seq(cv->text, cv->last_status)) {
        set_text(cv, cv->words->same);
        begin(cv, ZUI_CV_K_NOTE, now_ms);
        return ZUI_CV_SAME;
    }
    scopy(cv->last_status, sizeof cv->last_status, cv->text, cv->len);
    cv->last_status_ms = now_ms;
    cv->have_status = true;
    return begin(cv, ZUI_CV_K_STATUS, now_ms);
}

zui_cv_result_t zui_cv_stop(zui_cv_t *cv, uint32_t now_ms)
{
    if (!cv) return ZUI_CV_BAD;
    if (!zui_cv_talking(cv)) return ZUI_CV_NOTHING;
    if (cv->kind == ZUI_CV_K_READ) cv->resume_at = cv->cur_start;
    quiet_now(cv);
    cv->pos = cv->len;
    cv->stops++;
    cv->hold_until = now_ms ? now_ms : 1u; /* settle now */
    return ZUI_CV_STOPPED;
}

zui_cv_result_t zui_cv_tap(zui_cv_t *cv, const zui_cv_facts_t *f, uint32_t now_ms)
{
    if (!cv) return ZUI_CV_BAD;
    if (zui_cv_talking(cv)) return zui_cv_stop(cv, now_ms);
    return zui_cv_status(cv, f, false, now_ms);
}

zui_cv_result_t zui_cv_hold(zui_cv_t *cv, const zui_cv_facts_t *f, uint32_t now_ms)
{
    if (!cv) return ZUI_CV_BAD;
    return zui_cv_status(cv, f, true, now_ms);
}

zui_cv_result_t zui_cv_read(zui_cv_t *cv, const char *text, const char *title, uint32_t now_ms)
{
    if (!cv) return ZUI_CV_BAD;
    quiet_now(cv);
    uint32_t s = 0, n = slen(text);
    if (!text || zui_cv_chunk_end(text, n, &s, 8) <= s) {
        set_text(cv, cv->words->nothing);
        begin(cv, ZUI_CV_K_NOTE, now_ms);
        return ZUI_CV_NOTHING;
    }
    /* CV10: keep it as written, say it as a person would */
    cv->exact_len = scopy(cv->exact, sizeof cv->exact, text, n);
    if (cv->verbatim)
        set_text(cv, cv->exact);
    else
        cv->len = speech_tidy(cv->exact, cv->exact_len, cv->text, sizeof cv->text, cv->words->say);
    if (!cv->len) {
        set_text(cv, cv->words->nothing);
        begin(cv, ZUI_CV_K_NOTE, now_ms);
        return ZUI_CV_NOTHING;
    }
    char t[ZUI_CV_SMALL * 2];
    if (title && title[0] && !cv->verbatim) {
        speech_tidy(title, slen(title), t, sizeof t, cv->words->say);
        title = t;
    }
    prepend_title(cv, title);
    cv->resume_at = ZUI_CV_UNKNOWN;
    return begin(cv, ZUI_CV_K_READ, now_ms);
}

zui_cv_result_t zui_cv_spell(zui_cv_t *cv, uint32_t now_ms)
{
    if (!cv) return ZUI_CV_BAD;
    if (!cv->exact_len) return ZUI_CV_NOTHING;
    quiet_now(cv);
    cv->len = speech_spell(cv->exact, cv->exact_len, cv->text, sizeof cv->text, cv->words->say);
    cv->resume_at = ZUI_CV_UNKNOWN;
    return begin(cv, ZUI_CV_K_READ, now_ms);
}

void zui_cv_set_verbatim(zui_cv_t *cv, bool on)
{
    if (cv) cv->verbatim = on;
}

zui_cv_result_t zui_cv_resume(zui_cv_t *cv, uint32_t now_ms)
{
    if (!cv) return ZUI_CV_BAD;
    if (cv->kind != ZUI_CV_K_READ || cv->resume_at == ZUI_CV_UNKNOWN || cv->resume_at >= cv->len)
        return ZUI_CV_NOTHING;
    uint32_t at = cv->resume_at;
    cv->resume_at = ZUI_CV_UNKNOWN;
    return begin_at(cv, ZUI_CV_K_READ, at, now_ms);
}

zui_cv_result_t zui_cv_repeat(zui_cv_t *cv, uint32_t now_ms)
{
    if (!cv) return ZUI_CV_BAD;
    if (!cv->len || cv->kind == ZUI_CV_K_NONE) return ZUI_CV_NOTHING;
    uint32_t from = cv->kind == ZUI_CV_K_READ ? cv->cur_start : 0u;
    return begin_at(cv, cv->kind, from, now_ms);
}

/* ---- zui_cv_hear --------------------------------------------------------- */

static uint32_t norm(const char *in, char *out, uint32_t cap)
{
    uint32_t n = 0;
    bool sp = true;
    for (const char *p = in; *p && n + 1 < cap; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
        if (c == '\t' || c == '\n' || c == '\r' || c == ',') c = ' ';
        if (c == ' ') {
            if (!sp) out[n++] = ' ';
            sp = true;
            continue;
        }
        out[n++] = c;
        sp = false;
    }
    while (n && (out[n - 1] == ' ' || out[n - 1] == '.' || out[n - 1] == '!' || out[n - 1] == '?' ||
                 out[n - 1] == ','))
        n--;
    out[n] = 0;
    return n;
}

static bool one_of(const char *s, const char *const *list)
{
    for (; *list; list++)
        if (seq(s, *list)) return true;
    return false;
}

static zui_cv_result_t read_section(zui_cv_t *cv, const char *name, uint32_t now)
{
    char title[ZUI_CV_SMALL];
    title[0] = 0;
    int32_t n = -1;
    quiet_now(cv);
    if (cv->section)
        n = cv->section(name, cv->text, sizeof cv->text, title, sizeof title, cv->section_ctx);
    if (n < 0) {
        char line[ZUI_CV_SMALL * 2];
        sb_t o = {line, sizeof line, 0};
        line[0] = 0;
        sb_fmt(&o, cv->words->not_found, name[0] ? name : "that section", 0, 0);
        set_text(cv, line);
        begin(cv, ZUI_CV_K_NOTE, now);
        return ZUI_CV_NOTHING;
    }
    if ((uint32_t) n >= sizeof cv->text) n = (int32_t) (sizeof cv->text - 1u);
    cv->len = utf8_floor(cv->text, (uint32_t) n);
    cv->text[cv->len] = 0;
    return zui_cv_read(cv, cv->text, title[0] ? title : 0, now);
}

zui_cv_result_t zui_cv_hear(zui_cv_t *cv, const char *utf8, const zui_cv_facts_t *f,
                            uint32_t now_ms)
{
    if (!cv || !utf8) return ZUI_CV_BAD;
    char b[ZUI_CV_SMALL * 2];
    norm(utf8, b, sizeof b);
    char *s = b;
    if (starts(s, "please ")) s += 7;
    uint32_t n = slen(s);
    if (n > 7 && seq(s + n - 7, " please")) s[n - 7] = 0;

    static const char *const STOP[] = {"stop", "quiet", "be quiet", "shush", "stop talking", 0};
    static const char *const GO[] = {"continue", "go on", "keep reading", "resume", 0};
    static const char *const AGAIN[] = {"repeat", "again", "say that again", "repeat that", 0};
    static const char *const HOW[] = {"how are you", "how are you feeling", "how do you feel",
                                      "how are things", 0};
    static const char *const REPORT[] = {
        "status",           "status report",   "report", "full report",
        "what is going on", "what's going on", 0};
    static const char *const THIS[] = {
        "read this", "read this section",  "read me this section", "read it", "read this to me",
        "read that", "read the selection", "read me this",         0};
    if (one_of(s, STOP)) {
        zui_cv_result_t r = zui_cv_stop(cv, now_ms);
        return r == ZUI_CV_NOTHING ? ZUI_CV_STOPPED : r;
    }
    if (one_of(s, GO)) return zui_cv_resume(cv, now_ms);
    if (one_of(s, AGAIN)) return zui_cv_repeat(cv, now_ms);
    if (one_of(s, HOW)) return zui_cv_status(cv, f, false, now_ms);
    if (one_of(s, REPORT)) return zui_cv_status(cv, f, true, now_ms);
    static const char *const SPELL[] = {"spell that",     "spell it",          "spell it out",
                                        "spell that out", "read that exactly", "read it exactly",
                                        "say it exactly", "say that exactly",  0};
    if (one_of(s, SPELL)) return zui_cv_spell(cv, now_ms);
    static const char *const CALL[] = {"let's talk",   "lets talk",       "talk to me", "come here",
                                       "face to face", "come talk to me", "let's chat", 0};
    static const char *const BYE[] = {
        "bye",          "goodbye",    "good bye",  "bye bye", "hang up", "end call",
        "end the call", "that's all", "thats all", "see you", 0};
    if (one_of(s, CALL)) return cv->call ? ZUI_CV_SAME : zui_cv_call(cv, true, now_ms);
    if (cv->call && one_of(s, BYE)) return zui_cv_call(cv, false, now_ms);
    if (one_of(s, THIS)) return read_section(cv, "", now_ms);
    if (seq(s, "louder") || seq(s, "speak up")) {
        zui_cv_set_volume(cv, cv->volume + 10u);
        return ZUI_CV_SET;
    }
    if (seq(s, "quieter") || seq(s, "softer")) {
        zui_cv_set_volume(cv, cv->volume >= 10u ? cv->volume - 10u : 0u);
        return ZUI_CV_SET;
    }
    if (seq(s, "faster") || seq(s, "speed up")) {
        zui_cv_set_rate(cv, cv->rate_pct + 10u);
        return ZUI_CV_SET;
    }
    if (seq(s, "slower") || seq(s, "slow down")) {
        zui_cv_set_rate(cv, cv->rate_pct - 10u);
        return ZUI_CV_SET;
    }
    const char *name = 0;
    if (starts(s, "read me the "))
        name = s + 12;
    else if (starts(s, "read me "))
        name = s + 8;
    else if (starts(s, "read the "))
        name = s + 9;
    else if (starts(s, "read "))
        name = s + 5;
    if (name && *name) {
        char nm[ZUI_CV_SMALL];
        uint32_t k = scopy(nm, sizeof nm, name, slen(name));
        if (k > 8 && seq(nm + k - 8, " section")) nm[k - 8] = 0;
        if (k > 6 && seq(nm + k - 6, " to me")) nm[k - 6] = 0;
        return read_section(cv, nm, now_ms);
    }
    return ZUI_CV_NOT_MINE;
}

/* ---- the host's clock and audio --------------------------------------------- */

void zui_cv_spoken(zui_cv_t *cv, uint32_t now_ms)
{
    if (!cv) return;
    if (cv->in_speak) {
        cv->done_early = true;
        return;
    }
    if (!cv->playing) return;
    cv->playing = false;
    cv->said++;
    cv->level = 0;
    next_chunk(cv, now_ms);
}

void zui_cv_level(zui_cv_t *cv, uint32_t level256)
{
    if (!cv) return;
    if (level256 > 256u) level256 = 256u;
    if (level256 >= cv->level)
        cv->level = level256;
    else
        cv->level -= (cv->level - level256 + 2u) / 3u; /* falls over a few frames */
}

uint32_t zui_cv_level_pcm(const int16_t *pcm, uint32_t n)
{
    if (!pcm || !n) return 0;
    uint64_t sum = 0;
    for (uint32_t i = 0; i < n; i++) sum += (uint32_t) (pcm[i] < 0 ? -(int32_t) pcm[i] : pcm[i]);
    uint64_t mean = sum / n;
    uint64_t l = mean * 256u / 6000u; /* a talking voice's mean is a few thousand */
    return l > 256u ? 256u : (uint32_t) l;
}

void zui_cv_set_busy(zui_cv_t *cv, bool busy, uint32_t now_ms)
{
    if (!cv) return;
    cv->busy = busy;
    if (!busy && cv->waiting) next_chunk(cv, now_ms);
}

void zui_cv_tick(zui_cv_t *cv, uint32_t now_ms)
{
    if (!cv) return;
    cv->now = now_ms;
    if (cv->waiting && !cv->busy) next_chunk(cv, now_ms);
    if (cv->words_until && (int32_t) (now_ms - cv->words_until) >= 0) {
        cv->words_until = 0;
        cv->said++;
        next_chunk(cv, now_ms);
    }
    if (!cv->playing) zui_cv_level(cv, 0);
    uint32_t d = rise_ms();
    if (cv->phase == ZUI_CV_RISING && now_ms - cv->phase_ms >= d) {
        cv->phase = ZUI_CV_SHOWN;
        cv->phase_ms = now_ms;
    }
    if (cv->call && !zui_cv_talking(cv) && !cv->listening &&
        now_ms - cv->call_last >= ZUI_CV_CALL_IDLE_MS)
        end_call(cv, false, now_ms); /* CV11: quiet, so it closes quietly */
    if (cv->phase == ZUI_CV_SHOWN && !cv->call && !zui_cv_talking(cv) && cv->hold_until &&
        (int32_t) (now_ms - cv->hold_until) >= 0) {
        cv->phase = ZUI_CV_SETTLING;
        cv->phase_ms = now_ms;
        cv->hold_until = 0;
        if (d == 0) cv->phase = ZUI_CV_HIDDEN;
    }
    if (cv->phase == ZUI_CV_SETTLING && now_ms - cv->phase_ms >= d) cv->phase = ZUI_CV_HIDDEN;
    if (cv->phase == ZUI_CV_HIDDEN && !zui_cv_talking(cv)) {
        cv->chunk[0] = 0;
        cv->note[0] = 0;
    }
}

/* ---- CV9: drawing ------------------------------------------------------------ */

typedef struct {
    int32_t cx, cy, r;
} body_t;

static int32_t rest_r(const zui_cv_t *cv)
{
    int32_t m = cv->anchor.w < cv->anchor.h ? cv->anchor.w : cv->anchor.h;
    return m / 2 - 2 < 8 ? 8 : m / 2 - 2;
}

static body_t body_at(const zui_cv_t *cv, uint32_t now)
{
    int32_t r0 = rest_r(cv), r1 = r0 * 2;
    if (r1 > 96) r1 = r0 > 96 ? r0 : 96;
    int32_t cx = cv->anchor.x + cv->anchor.w / 2, cy0 = cv->anchor.y + cv->anchor.h / 2;
    int32_t cy1 = cy0 - (r1 - r0) - r1 / 2;
    uint32_t up = up_q8(cv, now);
    uint32_t total = 256u, t;
    if (zui_motion_style() != ZUI_MOTION_FULL) {
        /* reduce motion: no travel; it is either resting or up */
        t = up ? total : 0u;
    } else {
        t = zui_motion_ease_q8(up);
    }
    body_t b;
    b.cx = cx;
    b.cy = cy0 + (cy1 - cy0) * (int32_t) t / 256;
    b.r = r0 + (r1 - r0) * (int32_t) t / 256;
    return b;
}

bool zui_cv_hit(const zui_cv_t *cv, int32_t x, int32_t y)
{
    if (!cv) return false;
    zui_rect_t a = cv->anchor;
    if (x >= a.x && y >= a.y && x < a.x + a.w && y < a.y + a.h) return true;
    if (cv->phase == ZUI_CV_HIDDEN) return false;
    body_t b = body_at(cv, cv->now);
    int64_t dx = x - b.cx, dy = y - b.cy;
    return dx * dx + dy * dy <= (int64_t) b.r * b.r;
}

/* the words, wrapped to width w; draws when s is not NULL; returns lines */
static uint32_t wrap(zui_surface_t *s, const zui_text_t *tx, const char *t, int32_t x, int32_t y,
                     int32_t w, int32_t lh, uint32_t max_lines, uint32_t rgb, uint32_t a)
{
    uint32_t n = slen(t), at = 0, lines = 0;
    char line[ZUI_CV_CHUNK];
    while (at < n && lines < max_lines) {
        while (at < n && t[at] == ' ') at++;
        if (at >= n) break;
        uint32_t best = 0, i = at;
        /* grow word by word while it fits */
        while (i < n) {
            uint32_t j = i;
            while (j < n && t[j] == ' ') j++;
            while (j < n && t[j] != ' ') j++;
            scopy(line, sizeof line, t + at, j - at);
            if (best && zui_text_width(tx, line) > w) break;
            best = j - at;
            i = j;
        }
        if (best == 0) best = n - at;
        scopy(line, sizeof line, t + at, best);
        /* a single word wider than the line: cut it between characters */
        if (zui_text_width(tx, line) > w) {
            uint32_t k = best;
            while (k > 1) {
                k = utf8_floor(line, k - 1);
                line[k] = 0;
                if (zui_text_width(tx, line) <= w) break;
            }
            if (k == 0) k = best;
            best = k;
            scopy(line, sizeof line, t + at, best);
        }
        if (s) zui_text_a(s, tx, x, y + (int32_t) lines * lh + zui_text_ascent(tx), rgb, a, line);
        lines++;
        at += best;
    }
    return lines;
}

/* ---- CV11: the call window ------------------------------------------------------ */

zui_rect_t zui_cv_call_rect(const zui_cv_t *cv, int32_t sw, int32_t sh)
{
    int32_t m = ZUI_S4, w = 520, h = 400;
    if (w > sw - 2 * m) w = sw - 2 * m;
    if (h > sh - 2 * m) h = sh - 2 * m;
    if (w < 160) w = sw < 160 ? sw : 160;
    if (h < 160) h = sh < 160 ? sh : 160;
    /* in the corner the Chiglet lives in, like a call tile */
    int32_t ax = cv ? cv->anchor.x + cv->anchor.w / 2 : sw;
    int32_t ay = cv ? cv->anchor.y + cv->anchor.h / 2 : sh;
    int32_t x = ax >= sw / 2 ? sw - m - w : m;
    int32_t y = ay >= sh / 2 ? sh - m - h : m;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    zui_rect_t r = {x, y, w, h};
    return r;
}

static bool inside(zui_rect_t r, int32_t x, int32_t y)
{
    return r.w > 0 && x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h;
}

zui_cv_hit_t zui_cv_call_hit(const zui_cv_t *cv, int32_t x, int32_t y)
{
    if (!cv) return ZUI_CV_HIT_NONE;
    if (!cv->call) return zui_cv_hit(cv, x, y) ? ZUI_CV_HIT_BODY : ZUI_CV_HIT_NONE;
    if (inside(cv->end_rect, x, y)) return ZUI_CV_HIT_END;
    if (inside(cv->call_rect, x, y)) return ZUI_CV_HIT_WINDOW;
    zui_rect_t a = cv->anchor;
    return inside(a, x, y) ? ZUI_CV_HIT_BODY : ZUI_CV_HIT_NONE;
}

static void dot(zui_surface_t *s, int32_t cx, int32_t cy, int32_t r, uint32_t rgb, uint32_t a,
                bool filled)
{
    zui_rect_t d = {cx - r, cy - r, 2 * r, 2 * r};
    if (filled)
        zui_fill_rrect(s, d, r, rgb, a);
    else
        zui_stroke_rrect(s, d, r, 1, rgb, a);
}

/* a label centred under a body, cut to fit */
static void label(zui_surface_t *s, const zui_text_t *t, int32_t cx, int32_t y, int32_t maxw,
                  const char *str)
{
    int32_t tw = zui_text_width(t, str);
    if (tw > maxw) tw = maxw;
    zui_text_fit(s, t, cx - tw / 2, y, maxw, ZUI_MUTED, str);
}

static void draw_call(zui_cv_t *cv, zui_surface_t *s, const zui_text_t *text, uint32_t now,
                      bool reduce, uint32_t tick)
{
    zui_rect_t w = zui_cv_call_rect(cv, s->w, s->h);
    cv->call_rect = w;
    uint32_t d = rise_ms(), e = now - cv->call_ms;
    uint32_t a = reduce || !d || e >= d ? 255u : e * 255u / d;
    uint32_t accent = zui_chiglet_accent(&cv->who);
    zui_fill_rrect(s, w, ZUI_R_PANEL, ZUI_PANEL, zui_motion_alpha(a * 245u / 255u));
    zui_stroke_rrect(s, w, ZUI_R_PANEL, 1, accent, a);
    bool font = text && text->font;
    int32_t lh = font ? zui_text_ascent(text) - zui_text_descent(text) + 4 : 18;
    int32_t pad = ZUI_S3, bar = lh + 2 * ZUI_S2;
    /* the bar: mic mark, name and feeling, End */
    int32_t cy = w.y + bar / 2;
    dot(s, w.x + pad + 5, cy, 5, cv->listening ? ZUI_OK : ZUI_FAINT, a, cv->listening);
    int32_t ew = font ? zui_text_width(text, cv->words->call_end) + 2 * ZUI_S3 : 48;
    cv->end_rect = (zui_rect_t){w.x + w.w - pad - ew, w.y + ZUI_S1, ew, bar - 2 * ZUI_S1};
    zui_fill_rrect(s, cv->end_rect, ZUI_R_CONTROL, ZUI_ALERT, a);
    zui_hline(s, w.x + 1, w.y + bar, w.w - 2, 1, ZUI_HAIRLINE);
    const char *name = cv->name[0] ? cv->name : cv->words->name ? cv->words->name : "";
    if (font) {
        int32_t base = cy + (zui_text_ascent(text) + zui_text_descent(text)) / 2;
        int32_t x = w.x + pad + 10 + ZUI_S2;
        x = zui_text_a(s, text, x, base, ZUI_GOLD, a, name);
        zui_text_a(s, text, x + ZUI_S2, base, ZUI_MUTED, a, zui_emote_word(cv->feeling.emotion));
        zui_text_a(s, text, cv->end_rect.x + ZUI_S3, base, ZUI_VOID, a, cv->words->call_end);
    }
    /* the caption: what the person said, then its words */
    int32_t cap_h = font ? lh * 4 + 2 * pad : 0;
    zui_rect_t stage = {w.x, w.y + bar + 1, w.w, w.h - bar - 1 - cap_h};
    if (font) {
        int32_t y = stage.y + stage.h + pad;
        zui_hline(s, w.x + 1, stage.y + stage.h, w.w - 2, 1, ZUI_HAIRLINE);
        if (cv->heard[0] && cv->words->call_you) {
            char you[ZUI_CV_SMALL * 3];
            sb_t o = {you, sizeof you, 0};
            you[0] = 0;
            sb_fmt(&o, cv->words->call_you, cv->heard, 0, 0);
            zui_text_fit(s, text, w.x + pad, y + zui_text_ascent(text), w.w - 2 * pad, ZUI_MUTED,
                         you);
        }
        y += lh;
        const char *words = cv->chunk[0] ? cv->chunk : cv->note;
        wrap(s, text, words, w.x + pad, y, w.w - 2 * pad, lh, 3, ZUI_TEXT, a);
    }
    /* the stage: it, and a visitor beside it */
    int32_t half = cv->visiting ? stage.w / 4 : stage.w / 2;
    /* the motes orbit at 1.5 r: keep them, and the name under it, on the stage */
    int32_t r = (half - ZUI_S2) * 3 / 5, rv = (stage.h / 2 - lh - ZUI_S2) * 3 / 5;
    if (r > rv) r = rv;
    if (r > 90) r = 90;
    if (r < 8) r = 8;
    int32_t scy = stage.y + stage.h / 2 - lh / 2;
    int32_t mx = stage.x + half;
    zui_chiglet_t who = cv->who;
    who.mouth = 0;
    if (cv->playing || cv->words_until) {
        who.mood = ZUI_CHG_SPEAKING;
        who.mouth =
            cv->playing && !reduce ? (uint8_t) (1u + (cv->level > 254u ? 254u : cv->level)) : 128u;
    }
    zui_chiglet_draw(s, &who, mx, scy, r, tick);
    zui_face_t face = zui_emote_now(&cv->face, now, reduce);
    int32_t fr = r / 3 < 6 ? 6 : r / 3;
    zui_face_draw(s, &face, cv->health, mx + r, scy - r + fr / 2, fr, tick);
    int32_t ly = scy + r * 3 / 2 + ZUI_S1 + (font ? zui_text_ascent(text) : 0);
    if (font) label(s, text, mx, ly, 2 * half - 2 * ZUI_S2, name);
    if (cv->visiting) {
        int32_t gx = stage.x + 3 * half;
        zui_chiglet_draw(s, &cv->guest, gx, scy, r, tick);
        zui_emote_t ge;
        zui_emote_init(&ge, cv->guest_feeling, now);
        zui_face_t gf = zui_emote_now(&ge, now, 1);
        zui_face_draw(s, &gf, ZUI_HEALTH_OK, gx + r, scy - r + fr / 2, fr, tick);
        if (font) {
            char line[ZUI_CV_SMALL * 2];
            sb_t o = {line, sizeof line, 0};
            line[0] = 0;
            sb_fmt(&o, cv->words->visiting, cv->guest_name, 0, 0);
            label(s, text, gx, ly, 2 * half - 2 * ZUI_S2, line);
        }
    }
}

void zui_cv_draw(zui_cv_t *cv, zui_surface_t *s, const zui_text_t *text, uint32_t now_ms)
{
    if (!cv || !s) return;
    cv->now = now_ms;
    if (cv->call) {
        bool rm = zui_motion_style() != ZUI_MOTION_FULL;
        draw_call(cv, s, text, now_ms, rm, rm ? 0u : now_ms / 125u);
        return;
    }
    body_t b = body_at(cv, now_ms);
    zui_chiglet_t who = cv->who;
    bool popped = cv->phase != ZUI_CV_HIDDEN;
    bool reduce = zui_motion_style() != ZUI_MOTION_FULL;
    who.mouth = 0;
    if (popped && (cv->playing || cv->words_until)) {
        who.mood = ZUI_CHG_SPEAKING;
        if (cv->playing && !reduce)
            who.mouth = (uint8_t) (1u + (cv->level > 254u ? 254u : cv->level));
        else
            who.mouth = 128u; /* reduce motion, or words only: held half open */
    }
    /* motes turn with the frame clock unless motion is reduced */
    uint32_t tick = reduce ? 0u : now_ms / 125u;
    zui_chiglet_draw(s, &who, b.cx, b.cy, b.r, tick);
    if (!popped) return;

    /* the feeling: zui_emote's face, top right of the body */
    zui_face_t face = zui_emote_now(&cv->face, now_ms, reduce);
    int32_t fr = b.r / 3 < 6 ? 6 : b.r / 3;
    int32_t fx = b.cx + b.r; /* on the side away from the screen's edge */
    if (fx + fr + 2 > s->w) fx = b.cx - b.r;
    zui_face_draw(s, &face, cv->health, fx, b.cy - b.r + fr / 2, fr, tick);

    if (!text || !text->font) return;
    const char *words = cv->chunk;
    uint32_t a = cv->phase == ZUI_CV_SETTLING
                     ? zui_motion_fade_a255(now_ms - cv->phase_ms, ZUI_CV_RISE_MS)
                 : cv->phase == ZUI_CV_RISING ? (up_q8(cv, now_ms) * 255u) >> 8
                                              : 255u;
    if (reduce && cv->phase == ZUI_CV_RISING) a = 255u;
    if (!words[0] && !cv->note[0]) return;

    int32_t lh = zui_text_ascent(text) - zui_text_descent(text) + 4;
    int32_t pad = ZUI_S3, bw = 360;
    if (bw > s->w - 2 * ZUI_S4) bw = s->w - 2 * ZUI_S4;
    if (bw < 80) bw = 80;
    int32_t tw = bw - 2 * pad;
    uint32_t nl = wrap(0, text, words, 0, 0, tw, lh, 6, 0, 0);
    uint32_t nn = cv->note[0] ? wrap(0, text, cv->note, 0, 0, tw, lh, 2, 0, 0) : 0;
    int32_t bh = pad * 2 + lh * (int32_t) (1u + nn + nl);
    /* left of the body (and its face) when there is room, else right, else
     * above it; only when none fits is it pulled on screen over the body */
    int32_t m = ZUI_S4, reach = b.r + fr;
    int32_t bx = b.cx - reach - ZUI_S3 - bw, by = b.cy - bh / 2;
    if (bx < m) {
        bx = b.cx + reach + ZUI_S3;
        if (bx + bw > s->w - m) {
            bx = b.cx - bw / 2;
            by = b.cy - b.r - fr - ZUI_S3 - bh;
        }
    }
    if (bx + bw > s->w - m) bx = s->w - m - bw;
    if (bx < m) bx = m;
    if (by + bh > s->h - m) by = s->h - m - bh;
    if (by < m) by = m;
    if (bx < 0) bx = 0;
    if (by < 0) by = 0;
    zui_rect_t box = {bx, by, bw, bh};
    uint32_t accent = zui_chiglet_accent(&cv->who);
    zui_fill_rrect(s, box, ZUI_R_PANEL, ZUI_PANEL, zui_motion_alpha(a * 235u / 255u));
    zui_stroke_rrect(s, box, ZUI_R_PANEL, 1, accent, a);
    int32_t y = by + pad;
    /* the head line: how it feels */
    zui_text_a(s, text, bx + pad, y + zui_text_ascent(text), ZUI_GOLD, a,
               zui_emote_word(cv->feeling.emotion));
    y += lh;
    if (nn) {
        wrap(s, text, cv->note, bx + pad, y, tw, lh, 2, ZUI_MUTED, a);
        y += lh * (int32_t) nn;
    }
    wrap(s, text, words, bx + pad, y, tw, lh, 6, ZUI_TEXT, a);
}
