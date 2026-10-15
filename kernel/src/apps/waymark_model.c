/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* waymark_model.c — Waymark (see waymark_model.h). */
#include <stdio.h>
#include <string.h>

#include "waymark_model.h"

const zxm_ai_action_t WAYMARK_AI[WAYMARK_AI_COUNT] = {
    {"Plan a day around these places", "Companion", 500},
};

static const char *const MODE[] = {"ask", "once", "always", "never"};

void waymark_init(waymark_t *w, zxm_link_t *link)
{
    memset(w, 0, sizeof *w);
    w->link = link;
}

static int i32(zxm_link_t *l, int32_t o, const char *key, int64_t lim, int32_t *v)
{
    int64_t x;
    if (zxm_get_i64(l, o, key, &x) || x < -lim || x > lim) return ZXM_E_PROTOCOL;
    *v = (int32_t) x;
    return ZXM_OK;
}

static int read_here(zxm_link_t *l, int32_t o, waymark_here_t *h, bool with_stale)
{
    uint64_t acc;
    if (zxm_get_bool(l, o, "known", &h->known)) return ZXM_E_PROTOCOL;
    if (!h->known) return ZXM_OK;
    if (i32(l, o, "lat", 90000000, &h->lat) || i32(l, o, "lon", 180000000, &h->lon) ||
        zxm_get_u64(l, o, "accuracy_m", &acc) || acc < 1 || acc > 1000000 ||
        zxm_get_str(l, o, "source", h->source, sizeof h->source) || !h->source[0] ||
        zxm_get_i64(l, o, "time", &h->time) ||
        (with_stale && zxm_get_bool(l, o, "stale", &h->stale)))
        return ZXM_E_PROTOCOL;
    h->accuracy_m = (uint32_t) acc;
    return ZXM_OK;
}

static int mode_of(const char *s)
{
    for (int i = 0; i < 4; i++)
        if (!strcmp(s, MODE[i])) return i;
    return -1;
}

/* insertion sort by distance; unknown distances keep their order at the end */
static void sort_places(waymark_t *w)
{
    for (uint32_t i = 1; i < w->nplaces; i++) {
        static waymark_place_t t;
        t = w->place[i];
        uint32_t j = i;
        while (j > 0 && t.distance_m >= 0 &&
               (w->place[j - 1].distance_m < 0 || w->place[j - 1].distance_m > t.distance_m)) {
            w->place[j] = w->place[j - 1];
            j--;
        }
        w->place[j] = t;
    }
}

int waymark_refresh(waymark_t *w)
{
    zxm_link_t *l = w->link;
    static waymark_t keep;
    keep = *w;
    int rc;
    zxm_begin(l, "place.here");
    if ((rc = zxm_call(l)) != ZXM_OK) return rc;
    waymark_here_t h;
    memset(&h, 0, sizeof h);
    if (read_here(l, l->result, &h, true)) return ZXM_E_PROTOCOL;

    zxm_begin(l, "place.list");
    if ((rc = zxm_call(l)) != ZXM_OK) return rc;
    bool reset;
    int32_t arr = zxm_get(l, l->result, "places");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_len(l, arr) > WAYMARK_PLACES ||
        zxm_get_bool(l, l->result, "reset", &reset))
        return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr);
    for (uint32_t i = 0; i < n; i++) {
        waymark_place_t *p = &w->place[i];
        int32_t o = zxm_at(l, arr, i), d = o < 0 ? -1 : zxm_get(l, o, "distance_m");
        memset(p, 0, sizeof *p);
        uint64_t dist;
        if (d < 0 || zxm_get_str(l, o, "name", p->name, sizeof p->name) || !p->name[0] ||
            i32(l, o, "lat", 90000000, &p->lat) || i32(l, o, "lon", 180000000, &p->lon) ||
            zxm_get_str(l, o, "note", p->note, sizeof p->note) ||
            (l->tok[d].type == W4_J_NULL) == h.known ||
            (h.known && (zxm_get_u64(l, o, "distance_m", &dist) || dist > 40100000)))
            goto bad;
        p->distance_m = h.known ? (int64_t) dist : -1;
    }
    w->nplaces = n;

    zxm_begin(l, "place.apps");
    if ((rc = zxm_call(l)) != ZXM_OK) goto failed;
    arr = zxm_get(l, l->result, "apps");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_len(l, arr) > WAYMARK_APPS) goto bad;
    n = zxm_len(l, arr);
    uint32_t pending = 0;
    for (uint32_t i = 0; i < n; i++) {
        waymark_app_t *a = &w->app[i];
        int32_t o = zxm_at(l, arr, i);
        char mode[8];
        memset(a, 0, sizeof *a);
        if (o < 0 || zxm_get_str(l, o, "app", a->app, sizeof a->app) || !a->app[0] ||
            zxm_get_str(l, o, "mode", mode, sizeof mode) || mode_of(mode) < 0 ||
            zxm_get_bool(l, o, "pending", &a->pending) || zxm_get_i64(l, o, "asked", &a->asked) ||
            (a->pending && mode_of(mode) != WAYMARK_ASK))
            goto bad;
        a->mode = (uint8_t) mode_of(mode);
        pending += a->pending;
    }
    w->napps = n;
    w->npending = pending;
    w->here = h;
    w->reset = reset;
    sort_places(w);
    return ZXM_OK;
bad:
    rc = ZXM_E_PROTOCOL;
failed:
    *w = keep; /* what was shown stays */
    return rc;
}

static int done(waymark_t *w)
{
    int rc = zxm_call(w->link);
    return rc ? rc : waymark_refresh(w);
}

static bool good_point(int32_t lat, int32_t lon)
{
    return lat >= -90000000 && lat <= 90000000 && lon >= -180000000 && lon <= 180000000;
}

int waymark_set_here(waymark_t *w, int32_t lat, int32_t lon, uint32_t accuracy_m)
{
    if (!good_point(lat, lon) || accuracy_m < 1 || accuracy_m > 1000000) return ZXM_E_ARG;
    zxm_begin(w->link, "place.set_here");
    zxm_p_i64(w->link, "lat", lat);
    zxm_p_i64(w->link, "lon", lon);
    zxm_p_u64(w->link, "accuracy_m", accuracy_m);
    return done(w);
}

int waymark_forget_here(waymark_t *w)
{
    zxm_begin(w->link, "place.forget_here");
    return done(w);
}

int waymark_add(waymark_t *w, const char *name, int32_t lat, int32_t lon, const char *note)
{
    if (!name || !name[0] || strlen(name) > WAYMARK_NAME || !good_point(lat, lon) ||
        (note && strlen(note) > WAYMARK_NOTE))
        return ZXM_E_ARG;
    zxm_begin(w->link, "place.add");
    zxm_p_str(w->link, "name", name);
    zxm_p_i64(w->link, "lat", lat);
    zxm_p_i64(w->link, "lon", lon);
    if (note && note[0]) zxm_p_str(w->link, "note", note);
    return done(w);
}

int waymark_add_here(waymark_t *w, const char *name, const char *note)
{
    if (!w->here.known) return ZXM_E_STATE;
    return waymark_add(w, name, w->here.lat, w->here.lon, note);
}

int waymark_remove(waymark_t *w, const char *name)
{
    if (!name || !name[0] || strlen(name) > WAYMARK_NAME) return ZXM_E_ARG;
    zxm_begin(w->link, "place.remove");
    zxm_p_str(w->link, "name", name);
    return done(w);
}

int waymark_permit(waymark_t *w, const char *app, uint8_t mode)
{
    if (!app || !app[0] || strlen(app) > WAYMARK_APP || mode > WAYMARK_NEVER) return ZXM_E_ARG;
    zxm_begin(w->link, "place.permit");
    zxm_p_str(w->link, "app", app);
    zxm_p_str(w->link, "mode", MODE[mode]);
    return done(w);
}

const waymark_place_t *waymark_find(const waymark_t *w, const char *name)
{
    for (uint32_t i = 0; name && i < w->nplaces; i++)
        if (!strcmp(w->place[i].name, name)) return &w->place[i];
    return NULL;
}

int waymark_ai(waymark_t *w, int32_t action)
{
    if (action < 0 || action >= WAYMARK_AI_COUNT) return ZXM_E_ARG;
    if (!w->nplaces) return ZXM_E_STATE;
    static char in[ZXM_ASK_MAX];
    size_t n = 0;
    for (uint32_t i = 0; i < w->nplaces && n < sizeof in; i++) {
        const waymark_place_t *p = &w->place[i];
        int k = p->distance_m >= 0 ? snprintf(in + n, sizeof in - n, "%s, %lld m away: %s\n",
                                              p->name, (long long) p->distance_m, p->note)
                                   : snprintf(in + n, sizeof in - n, "%s: %s\n", p->name, p->note);
        n += k > 0 ? (size_t) k : 0;
    }
    if (n >= sizeof in) n = zxm_utf8_cut(in, sizeof in - 1);
    return zxm_ai_ask(w->link, &WAYMARK_AI[action], action,
                      "These are places the owner keeps, with how far each is from here when "
                      "known. Suggest an order to visit some of them in a day and what to keep "
                      "in mind. Do not invent places or distances. Be brief.",
                      in, (uint32_t) n, &w->held);
}

int waymark_ai_accept(waymark_t *w)
{
    return zxm_held_accept(&w->held);
}

int waymark_ai_reject(waymark_t *w)
{
    return zxm_held_reject(&w->held);
}
