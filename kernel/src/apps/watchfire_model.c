/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* watchfire_model.c — Watchfire's model (watchfire_model.h WM1-WM6) over
 * zxvd's call.start, place.ask, place.here, phone.list and files.*. */
#include "watchfire_model.h"

#include <stdio.h>
#include <string.h>

static const char *const MED_LABEL[WATCHFIRE_MED_FIELDS + 1] = {
    "", "Name", "Born", "Allergies", "Medications", "Conditions", "Notes"};

static void say(watchfire_t *w, const char *text)
{
    snprintf(w->notice, sizeof w->notice, "%s", text);
}

void watchfire_init(watchfire_t *w, zxm_link_t *link)
{
    memset(w, 0, sizeof *w);
    w->link = link;
    w->window = WATCHFIRE_WINDOW_DEFAULT;
    w->state = WATCHFIRE_IDLE;
    for (uint32_t i = 0; i < WATCHFIRE_CONTACTS; i++) w->contact[i].line = -1;
}

/* ----- WM1: as typed ----- */

/* n bytes with no control character (WM1); UTF-8 passes as it is */
static bool typed_ok(const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if ((unsigned char) s[i] < 0x20 || s[i] == 0x7f) return false;
    return true;
}

static bool phone_id_ok(const char *s)
{
    if (!s[0]) return true;
    if (strlen(s) != WATCHFIRE_PHONE_ID_LEN) return false;
    for (const char *p = s; *p; p++)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f'))) return false;
    return true;
}

int watchfire_set_medical(watchfire_t *w, int32_t field, const char *text, int64_t stamp)
{
    if (!w || !text || field < 1 || field > WATCHFIRE_MED_FIELDS || stamp < 0 ||
        stamp > WATCHFIRE_STAMP_MAX)
        return ZXM_E_ARG;
    size_t n = strlen(text);
    if (n > WATCHFIRE_TEXT_MAX || !typed_ok(text, n)) return ZXM_E_ARG;
    if (!watchfire_medical_fits(field, (int64_t) n, stamp)) return ZXM_E_ARG;
    watchfire_med_t *m = &w->med[field];
    memcpy(m->text, text, n);
    m->text[n] = 0;
    m->stamp = stamp;
    return ZXM_OK;
}

static int32_t find_contact(const watchfire_t *w, int64_t peer)
{
    for (uint32_t i = 0; i < w->ncontacts; i++)
        if (w->contact[i].peer == peer) return (int32_t) i;
    return -1;
}

int watchfire_add_contact(watchfire_t *w, const char *name, int64_t peer, const char *phone_id,
                          int64_t stamp)
{
    if (!w || !name || peer < 1 || peer > WATCHFIRE_PEER_MAX || stamp < 0 ||
        stamp > WATCHFIRE_STAMP_MAX)
        return ZXM_E_ARG;
    const char *ph = phone_id ? phone_id : "";
    size_t n = strlen(name);
    if (n == 0 || n > WATCHFIRE_NAME_MAX || !typed_ok(name, n) || !phone_id_ok(ph))
        return ZXM_E_ARG;
    if (find_contact(w, peer) >= 0 || w->ncontacts >= WATCHFIRE_CONTACTS ||
        w->state == WATCHFIRE_COUNTDOWN)
        return ZXM_E_STATE;
    int64_t seq = (int64_t) w->ncontacts + 1;
    if (!watchfire_contact_fits(seq, peer, (int64_t) n, stamp)) return ZXM_E_ARG;
    watchfire_contact_t *c = &w->contact[w->ncontacts++];
    memset(c, 0, sizeof *c);
    memcpy(c->name, name, n);
    c->peer = peer;
    memcpy(c->phone_id, ph, strlen(ph) + 1u);
    c->stamp = stamp;
    c->line = -1;
    return ZXM_OK;
}

int watchfire_remove_contact(watchfire_t *w, int64_t peer)
{
    if (!w) return ZXM_E_ARG;
    int32_t i = find_contact(w, peer);
    if (i < 0 || w->state == WATCHFIRE_COUNTDOWN) return ZXM_E_STATE;
    memmove(&w->contact[i], &w->contact[i + 1],
            sizeof w->contact[0] * (w->ncontacts - (uint32_t) i - 1u));
    w->ncontacts--;
    memset(&w->contact[w->ncontacts], 0, sizeof w->contact[0]);
    w->contact[w->ncontacts].line = -1;
    return ZXM_OK;
}

int watchfire_set_optin(watchfire_t *w, bool on, bool confirmed)
{
    if (!w) return ZXM_E_ARG;
    if (!confirmed) {
        say(w, "Sharing where this machine is changes only when you confirm it.");
        return ZXM_E_STATE;
    }
    w->optin = on;
    return ZXM_OK;
}

int watchfire_set_window(watchfire_t *w, int64_t secs)
{
    if (!w || !watchfire_window_ok(secs)) return ZXM_E_ARG;
    if (w->state == WATCHFIRE_COUNTDOWN) return ZXM_E_STATE;
    w->window = secs;
    return ZXM_OK;
}

/* ----- the lock-screen card ----- */

static bool add(char *out, uint32_t cap, uint32_t *used, const char *text)
{
    size_t n = strlen(text);
    if (*used + n >= cap) return false;
    memcpy(out + *used, text, n);
    *used += (uint32_t) n;
    out[*used] = 0;
    return true;
}

int32_t watchfire_lock_text(const watchfire_t *w, char *out, uint32_t cap)
{
    if (!w || !out || cap == 0) return ZXM_E_ARG;
    uint32_t used = 0;
    out[0] = 0;
    bool ok = add(out, cap, &used, "Medical ID, as its owner typed it:\n");
    uint32_t set = 0;
    for (int32_t f = 1; ok && f <= WATCHFIRE_MED_FIELDS; f++) {
        if (!w->med[f].text[0]) continue;
        set++;
        ok = add(out, cap, &used, MED_LABEL[f]) && add(out, cap, &used, ": ") &&
             add(out, cap, &used, w->med[f].text) && add(out, cap, &used, "\n");
    }
    if (ok && !set) ok = add(out, cap, &used, "(nothing entered)\n");
    if (ok && w->ncontacts) ok = add(out, cap, &used, "Emergency contacts:");
    for (uint32_t i = 0; ok && i < w->ncontacts; i++)
        ok = add(out, cap, &used, i ? ", " : " ") && add(out, cap, &used, w->contact[i].name);
    if (ok && w->ncontacts) ok = add(out, cap, &used, "\n");
    if (ok) ok = add(out, cap, &used, WATCHFIRE_NO_SERVICES);
    if (!ok) {
        out[0] = 0;
        return ZXM_E_TOOBIG;
    }
    return (int32_t) used;
}

/* ----- zxvd: devices and place ----- */

int watchfire_refresh_devices(watchfire_t *w)
{
    if (!w || !w->link) return ZXM_E_ARG;
    zxm_link_t *l = w->link;
    zxm_begin(l, "phone.list");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(l, l->result, "phones");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr);
    bool paired[WATCHFIRE_CONTACTS] = {false};
    int64_t conns[WATCHFIRE_CONTACTS] = {0};
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, arr, i);
        char id[WATCHFIRE_PHONE_ID_LEN + 2];
        uint64_t open = 0;
        if (o < 0 || zxm_get_str(l, o, "phone_id", id, sizeof id) != ZXM_OK || !id[0] ||
            !phone_id_ok(id))
            return ZXM_E_PROTOCOL;
        if (zxm_get_u64(l, o, "connected", &open) != ZXM_OK || open > 1000u) open = 0;
        for (uint32_t k = 0; k < w->ncontacts; k++)
            if (w->contact[k].phone_id[0] && !strcmp(w->contact[k].phone_id, id)) {
                paired[k] = true;
                conns[k] = (int64_t) open;
            }
    }
    /* checked whole: only now is the model changed */
    for (uint32_t k = 0; k < w->ncontacts; k++) {
        w->contact[k].paired = paired[k];
        w->contact[k].phones = conns[k];
    }
    return ZXM_OK;
}

int watchfire_refresh_place(watchfire_t *w)
{
    if (!w || !w->link) return ZXM_E_ARG;
    zxm_link_t *l = w->link;
    zxm_begin(l, "place.here");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    bool known = false, stale = false;
    if (zxm_get_bool(l, l->result, "known", &known) != ZXM_OK) return ZXM_E_PROTOCOL;
    if (known && zxm_get_bool(l, l->result, "stale", &stale) != ZXM_OK) stale = false;
    w->place_known = known;
    w->place_stale = known && stale;
    return ZXM_OK;
}

/* ----- WM2-WM4: the emergency ----- */

int watchfire_start(watchfire_t *w, int64_t now_s, int64_t owner, int64_t approved)
{
    if (!w || now_s < 0 || now_s > WATCHFIRE_WAIT_MAX) return ZXM_E_ARG;
    if (w->state == WATCHFIRE_COUNTDOWN || w->state == WATCHFIRE_RINGING) return ZXM_E_STATE;
    if (!warren_approval_ok(owner, approved)) {
        say(w, "Only you can start an emergency, from your own session.");
        return ZXM_E_STATE;
    }
    if (!w->ncontacts) {
        say(w, "Add an emergency contact first.");
        return ZXM_E_STATE;
    }
    w->state = WATCHFIRE_COUNTDOWN;
    w->started_s = now_s;
    w->rang = 0;
    for (uint32_t i = 0; i < w->ncontacts; i++) {
        w->contact[i].line = -1;
        w->contact[i].shared = false;
        w->contact[i].share[0] = 0;
    }
    snprintf(w->notice, sizeof w->notice,
             "Ringing your contacts in %lld s. Cancel to stop; nobody is called before then.",
             (long long) w->window);
    return ZXM_OK;
}

int64_t watchfire_left(const watchfire_t *w, int64_t now_s)
{
    if (!w || w->state != WATCHFIRE_COUNTDOWN || now_s < w->started_s) return WATCHFIRE_REFUSED;
    return watchfire_cancel_left(w->window, now_s - w->started_s);
}

int watchfire_cancel(watchfire_t *w)
{
    if (!w) return ZXM_E_ARG;
    if (w->state != WATCHFIRE_COUNTDOWN) return ZXM_E_STATE;
    w->state = WATCHFIRE_CANCELLED;
    say(w, "Cancelled. Nobody was called.");
    return ZXM_OK;
}

int watchfire_ring(watchfire_t *w, int64_t now_s)
{
    if (!w || !w->link) return ZXM_E_ARG;
    if (w->state != WATCHFIRE_COUNTDOWN && w->state != WATCHFIRE_CANCELLED) return ZXM_E_STATE;
    int64_t left = now_s < w->started_s ? WATCHFIRE_REFUSED
                                        : watchfire_cancel_left(w->window, now_s - w->started_s);
    if (left < 0) return ZXM_E_ARG;
    int64_t cancelled = w->state == WATCHFIRE_CANCELLED ? 1 : 0;
    if (!watchfire_ring_ok(w->window, left, cancelled)) {
        if (cancelled)
            say(w, "Cancelled. Nobody was called.");
        else
            snprintf(w->notice, sizeof w->notice,
                     "Ringing your contacts in %lld s. Cancel to stop.", (long long) left);
        return ZXM_E_STATE;
    }
    w->state = WATCHFIRE_RINGING;
    zxm_link_t *l = w->link;
    int last = ZXM_OK;
    uint32_t tried = 0;
    w->rang = 0;
    for (uint32_t i = 0; i < w->ncontacts && tried < WATCHFIRE_LINES; i++, tried++) {
        watchfire_contact_t *c = &w->contact[i];
        zxm_begin(l, "call.start");
        zxm_p_u64(l, "peer", (uint64_t) c->peer);
        zxm_p_str(l, "media", "audio");
        int rc = zxm_call(l);
        uint64_t line = 0;
        if (rc == ZXM_OK && (zxm_get_u64(l, l->result, "line", &line) != ZXM_OK || line > 4u))
            rc = ZXM_E_PROTOCOL;
        if (rc != ZXM_OK) {
            last = rc;
            continue;
        }
        c->line = (int32_t) line;
        w->rang++;
    }
    snprintf(w->notice, sizeof w->notice,
             "Ringing %u of %u contacts (signalling only). Watchfire does not call emergency "
             "services.",
             w->rang, w->ncontacts);
    return w->rang ? ZXM_OK : last;
}

/* millionths of a degree as text, "-12.345678" */
static void degrees(char *out, size_t cap, int64_t v)
{
    uint64_t a = v < 0 ? 0u - (uint64_t) v : (uint64_t) v;
    snprintf(out, cap, "%s%llu.%06llu", v < 0 ? "-" : "", (unsigned long long) (a / 1000000u),
             (unsigned long long) (a % 1000000u));
}

int watchfire_share_place(watchfire_t *w, uint32_t *n)
{
    if (!w || !w->link || !n) return ZXM_E_ARG;
    *n = 0;
    if (w->state != WATCHFIRE_RINGING) return ZXM_E_STATE;
    zxm_link_t *l = w->link;
    zxm_begin(l, "place.ask");
    zxm_p_str(l, "app", WATCHFIRE_APP);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    char answer[16], source[24] = "";
    int64_t lat = 0, lon = 0, acc = 0;
    if (zxm_get_str(l, l->result, "answer", answer, sizeof answer) != ZXM_OK) return ZXM_E_PROTOCOL;
    bool allowed = !strcmp(answer, "allowed");
    if (allowed && (zxm_get_i64(l, l->result, "lat", &lat) != ZXM_OK ||
                    zxm_get_i64(l, l->result, "lon", &lon) != ZXM_OK || lat < -90000000 ||
                    lat > 90000000 || lon < -180000000 || lon > 180000000))
        return ZXM_E_PROTOCOL;
    if (allowed) {
        if (zxm_get_i64(l, l->result, "accuracy_m", &acc) != ZXM_OK || acc < 1 || acc > 1000000)
            acc = 0;
        if (zxm_get_str(l, l->result, "source", source, sizeof source) != ZXM_OK) source[0] = 0;
    }
    char la[24], lo[24];
    degrees(la, sizeof la, lat);
    degrees(lo, sizeof lo, lon);
    /* started 1: ringing began from the person's own start (WM2); listed 1:
     * every contact here is on the emergency list */
    for (uint32_t i = 0; i < w->ncontacts; i++) {
        watchfire_contact_t *c = &w->contact[i];
        c->shared = watchfire_place_share_ok(1, w->optin ? 1 : 0, 1, allowed ? 1 : 0);
        c->share[0] = 0;
        if (!c->shared) continue;
        if (acc)
            snprintf(c->share, sizeof c->share,
                     "For %s: this machine is at %s, %s (within %lld m%s%s).", c->name, la, lo,
                     (long long) acc, source[0] ? ", from " : "", source);
        else
            snprintf(c->share, sizeof c->share, "For %s: this machine is at %s, %s.", c->name, la,
                     lo);
        (*n)++;
    }
    if (!w->optin)
        say(w, "Where this machine is stays private: you have not opted in to sharing it.");
    else if (!strcmp(answer, "pending"))
        say(w, "Where this machine is waits for your answer to Watchfire's location ask.");
    else if (!allowed)
        say(w, "Where this machine is is not shared: no location is allowed or set.");
    else
        snprintf(w->notice, sizeof w->notice,
                 "Where this machine is, ready for %u contacts. Read it to them on the call.", *n);
    return ZXM_OK;
}

int watchfire_finish(watchfire_t *w)
{
    if (!w) return ZXM_E_ARG;
    if (w->state == WATCHFIRE_COUNTDOWN) return ZXM_E_STATE; /* cancel it first */
    w->state = WATCHFIRE_IDLE;
    w->notice[0] = 0;
    return ZXM_OK;
}

/* ----- WM6: the file ----- */

int watchfire_save(watchfire_t *w)
{
    if (!w || !w->link) return ZXM_E_ARG;
    uint32_t used = 0;
    char line[WATCHFIRE_TEXT_MAX + 64];
    bool ok = true;
    w->file[0] = 0;
    snprintf(line, sizeof line, "watchfire 1\nwindow %lld\noptin %d\n", (long long) w->window,
             w->optin ? 1 : 0);
    ok = add(w->file, sizeof w->file, &used, line);
    for (int32_t f = 1; ok && f <= WATCHFIRE_MED_FIELDS; f++) {
        if (!w->med[f].text[0]) continue;
        snprintf(line, sizeof line, "med %d %lld %s\n", (int) f, (long long) w->med[f].stamp,
                 w->med[f].text);
        ok = add(w->file, sizeof w->file, &used, line);
    }
    for (uint32_t i = 0; ok && i < w->ncontacts; i++) {
        const watchfire_contact_t *c = &w->contact[i];
        snprintf(line, sizeof line, "contact %lld %lld %s %s\n", (long long) c->peer,
                 (long long) c->stamp, c->phone_id[0] ? c->phone_id : "-", c->name);
        ok = add(w->file, sizeof w->file, &used, line);
    }
    if (!ok) return ZXM_E_TOOBIG;
    zxm_link_t *l = w->link;
    zxm_begin(l, "files.stat");
    zxm_p_str(l, "path", WATCHFIRE_DIR);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE) {
        zxm_begin(l, "files.mkdir");
        zxm_p_str(l, "path", WATCHFIRE_DIR);
        rc = zxm_call(l);
    }
    if (rc != ZXM_OK) return rc;
    zxm_begin(l, "files.write");
    zxm_p_str(l, "path", WATCHFIRE_FILE);
    zxm_p_strn(l, "text", w->file, used);
    return zxm_call(l);
}

/* a whole number of 1..18 digits, then a space or the end; *p moves past it */
static bool number(const char **p, int64_t *v)
{
    const char *s = *p;
    int64_t x = 0;
    uint32_t d = 0;
    while (*s >= '0' && *s <= '9' && d < 18u) {
        x = x * 10 + (*s++ - '0');
        d++;
    }
    if (!d || (*s != ' ' && *s != 0)) return false;
    if (*s == ' ') s++;
    *p = s;
    *v = x;
    return true;
}

/* one line of the file into t; false when it is not one */
static bool load_line(watchfire_t *t, const char *s, bool *head)
{
    int64_t a = 0, b = 0;
    if (!strcmp(s, "watchfire 1")) {
        *head = true;
        return true;
    }
    if (!*head) return false;
    if (!strncmp(s, "window ", 7)) {
        s += 7;
        return number(&s, &a) && !*s && watchfire_set_window(t, a) == ZXM_OK;
    }
    if (!strncmp(s, "optin ", 6)) {
        s += 6;
        if (!number(&s, &a) || *s || a > 1) return false;
        t->optin = a == 1;
        return true;
    }
    if (!strncmp(s, "med ", 4)) {
        s += 4;
        return number(&s, &a) && number(&s, &b) && a <= WATCHFIRE_MED_FIELDS &&
               !t->med[a].text[0] && *s && watchfire_set_medical(t, (int32_t) a, s, b) == ZXM_OK;
    }
    if (!strncmp(s, "contact ", 8)) {
        char ph[WATCHFIRE_PHONE_ID_LEN + 1];
        s += 8;
        if (!number(&s, &a) || !number(&s, &b)) return false;
        const char *sp = strchr(s, ' ');
        size_t k = sp ? (size_t) (sp - s) : 0;
        if (!sp || k == 0 || k > WATCHFIRE_PHONE_ID_LEN) return false;
        memcpy(ph, s, k);
        ph[k] = 0;
        if (!strcmp(ph, "-")) ph[0] = 0;
        return watchfire_add_contact(t, sp + 1, a, ph, b) == ZXM_OK;
    }
    return false;
}

int watchfire_load(watchfire_t *w)
{
    static watchfire_t t;
    static char text[WATCHFIRE_FILE_MAX + 2];
    if (!w || !w->link) return ZXM_E_ARG;
    if (w->state == WATCHFIRE_COUNTDOWN) return ZXM_E_STATE;
    zxm_link_t *l = w->link;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", WATCHFIRE_FILE);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    bool eof = false;
    if (zxm_get_str(l, l->result, "text", text, sizeof text) != ZXM_OK ||
        zxm_get_bool(l, l->result, "eof", &eof) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    if (!eof || strlen(text) > WATCHFIRE_FILE_MAX) return ZXM_E_TOOBIG;
    watchfire_init(&t, w->link);
    bool head = false;
    char *s = text;
    while (*s) {
        char *nl = strchr(s, '\n');
        if (!nl) return ZXM_E_PROTOCOL; /* every line ends */
        *nl = 0;
        if (!load_line(&t, s, &head)) {
            say(w, "Watchfire's file is damaged; nothing was changed.");
            return ZXM_E_PROTOCOL;
        }
        s = nl + 1;
    }
    if (!head) return ZXM_E_PROTOCOL;
    /* the whole file is good: only now is the model changed */
    memcpy(w->med, t.med, sizeof w->med);
    memcpy(w->contact, t.contact, sizeof w->contact);
    w->ncontacts = t.ncontacts;
    w->optin = t.optin;
    w->window = t.window;
    return ZXM_OK;
}
