/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_hear.c — where what the person says goes. See zui_hear.h. */
#include "zui_hear.h"

void zui_hear_init(zui_hear_t *h, zui_cv_t *cv, zui_voicectl_t *vc)
{
    if (!h) return;
    uint8_t *p = (uint8_t *) h;
    for (uint32_t i = 0; i < sizeof *h; i++) p[i] = 0;
    h->cv = cv;
    h->vc = vc;
    h->last = ZUI_HEAR_IGNORED;
}

void zui_hear_set_facts(zui_hear_t *h, void (*fn)(zui_cv_facts_t *f, void *ctx), void *ctx)
{
    if (!h) return;
    h->facts = fn;
    h->facts_ctx = ctx;
}

void zui_hear_tick(zui_hear_t *h, uint32_t now_ms)
{
    if (h) h->now = now_ms;
}

void zui_hear_start(zui_hear_t *h, uint32_t now_ms)
{
    if (!h) return;
    h->now = now_ms;
    h->listening = true;
    if (h->cv) {
        if (zui_cv_talking(h->cv) && h->cv->playing) h->barge_ins++;
        if (zui_cv_talking(h->cv)) zui_cv_stop(h->cv, now_ms); /* HE2 */
        zui_cv_set_busy(h->cv, true, now_ms);
    }
}

void zui_hear_end(zui_hear_t *h, uint32_t now_ms)
{
    if (!h) return;
    h->now = now_ms;
    h->listening = false;
    if (h->cv) zui_cv_set_busy(h->cv, false, now_ms);
}

/* a focused, editable text field that AUTO dictation would type into */
static bool typing(const zui_hear_t *h)
{
    if (!h->vc || !h->vc->ui || h->vc->dictation == ZUI_VC_DICT_OFF) return false;
    zui_widget_t *w = zui_w(h->vc->ui, h->vc->ui->focus);
    return w && w->kind == ZUI_W_FIELD && !w->u.field.readonly && w->u.field.buf && !w->disabled;
}

static char low(char c)
{
    return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
}

/* the text after "chiglet" / "hey chiglet" / "ok chiglet" and a comma or
 * space, or NULL when it does not start with the name */
static const char *after_name(const char *s)
{
    while (*s == ' ') s++;
    static const char *const LEAD[] = {"hey ", "hi ", "ok ", "okay ", ""};
    for (uint32_t k = 0; k < 5; k++) {
        const char *p = s, *l = LEAD[k];
        while (*l && low(*p) == *l) p++, l++;
        if (*l) continue;
        const char *n = "chiglet";
        while (*n && low(*p) == *n) p++, n++;
        if (*n) continue;
        if (*p && *p != ' ' && *p != ',' && *p != '.' && *p != '!' && *p != '?') continue;
        while (*p == ' ' || *p == ',' || *p == '.' || *p == '!') p++;
        return p;
    }
    return 0;
}

zui_hear_result_t zui_hear_text(zui_hear_t *h, const char *utf8, uint32_t now_ms)
{
    if (!h) return ZUI_HEAR_IGNORED;
    h->now = now_ms;
    h->listening = false;
    if (h->cv) zui_cv_set_busy(h->cv, false, now_ms);
    uint32_t n = 0;
    if (utf8)
        for (; utf8[n] && n + 1 < sizeof h->heard; n++) h->heard[n] = utf8[n];
    h->heard[n] = 0;
    bool blank = true;
    for (uint32_t i = 0; i < n; i++)
        if (h->heard[i] != ' ' && h->heard[i] != '.') blank = false;
    if (blank) { /* HE4 */
        h->ignored++;
        return h->last = ZUI_HEAR_IGNORED;
    }
    zui_cv_facts_t f;
    zui_cv_facts_init(&f);
    if (h->facts) h->facts(&f, h->facts_ctx);

    /* HE1: dictation on: it is all text */
    bool dict_on = h->vc && h->vc->dictation == ZUI_VC_DICT_ON;
    if (!dict_on && h->cv) {
        const char *rest = after_name(h->heard);
        if (rest) {
            zui_cv_result_t r = zui_cv_hear(h->cv, rest, &f, now_ms);
            if (r == ZUI_CV_NOT_MINE || r == ZUI_CV_BAD) /* "Chiglet!" alone: a tap */
                zui_cv_tap(h->cv, &f, now_ms);
            h->to_chiglet++;
            return h->last = ZUI_HEAR_CHIGLET;
        }
        /* while a field is being typed into, only "Chiglet, ..." reaches it */
        if (!typing(h) && zui_cv_hear(h->cv, h->heard, &f, now_ms) != ZUI_CV_NOT_MINE) {
            h->to_chiglet++;
            return h->last = ZUI_HEAR_CHIGLET;
        }
    }
    if (h->vc) {
        zui_vc_result_t r = zui_vc_hear(h->vc, h->heard);
        if (r == ZUI_VC_DICTATED) {
            h->dictated++;
            return h->last = ZUI_HEAR_DICTATED;
        }
        if (r != ZUI_VC_UNKNOWN && r != ZUI_VC_EMPTY) {
            h->to_control++;
            return h->last = ZUI_HEAR_CONTROL;
        }
    }
    h->unknown++;
    return h->last = ZUI_HEAR_UNKNOWN;
}

void zui_hear_on_event(const speech_event_t *ev, void *hear)
{
    zui_hear_t *h = hear;
    if (!h || !ev) return;
    switch (ev->type) {
    case SPEECH_EV_UTTERANCE_START:
        zui_hear_start(h, h->now);
        break;
    case SPEECH_EV_TRANSCRIPT:
        if (ev->flags & SPEECH_EVF_LOSSY) { /* HE4 */
            zui_hear_end(h, h->now);
            h->ignored++;
            h->last = ZUI_HEAR_IGNORED;
        } else {
            zui_hear_text(h, ev->text, h->now);
        }
        break;
    case SPEECH_EV_ERROR:
        zui_hear_end(h, h->now);
        break;
    default:
        break;
    }
}
