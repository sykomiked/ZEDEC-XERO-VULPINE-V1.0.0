/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* chainman_model.c — Chainman's model (chainman_model.h). */
#include <string.h>

#include "chainman_model.h"

#define CHAINMAN_E_FILES (-32014)      /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */
#define CHAINMAN_TEXT    (48u * 1024u) /* the field book at most: one files.read */
#define CHAINMAN_PIECE   6000u         /* bytes of text in one files.write */
#define CHAINMAN_FIELDS  9u            /* integer fields of a line at most */
#define CHAINMAN_PREFIX  "Chainman "   /* Chainman's places on the map (CM4) */

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

static void say(chainman_t *c, const char *a)
{
    uint32_t at = 0;
    c->notice[0] = 0;
    put(c->notice, sizeof c->notice, &at, a);
}

/* A decimal integer of s[*at..n) up to `end`, a sign allowed; false when
 * it is empty, not a number, past 18 digits or has no end. */
static bool get_i64(const char *s, uint32_t n, uint32_t *at, char end, int64_t *v)
{
    uint32_t i = *at, digits = 0;
    bool neg = i < n && s[i] == '-';
    int64_t x = 0;
    if (neg) i++;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
        if (++digits > 18u) return false;
        x = x * 10 + (s[i++] - '0');
    }
    if (!digits || i >= n || s[i] != end) return false;
    *v = neg ? -x : x;
    *at = i + 1u;
    return true;
}

/* ---- the rules the model keeps (CM1, CM2) ---- */

static bool coord_ok(int64_t v)
{
    return gnomon_coord_ok(v);
}

static bool parcel_ok(const chainman_parcel_t *p)
{
    return chainman_in(p->parcel, 1, CHAINMAN_PARCEL_MAX) &&
           chainman_in(p->kind, UNDERSTORY_BUILT, UNDERSTORY_WILD) &&
           chainman_in(p->title, 0, CHAINMAN_TITLE_MAX) && coord_ok(p->x0) && coord_ok(p->y0) &&
           chainman_in(p->tol, 0, CHAINMAN_TOL_MAX);
}

/* A taped length is never shorter than the straight line it spans:
 * de^2 + dn^2 <= len^2, all under 10^13. */
static bool leg_ok(const chainman_leg_t *g)
{
    if (!chainman_in(g->parcel, 1, CHAINMAN_PARCEL_MAX) ||
        !chainman_in(g->len, 1, CHAINMAN_LEN_MAX) ||
        !chainman_in(g->de, -2 * GNOMON_COORD_MAX, 2 * GNOMON_COORD_MAX) ||
        !chainman_in(g->dn, -2 * GNOMON_COORD_MAX, 2 * GNOMON_COORD_MAX) ||
        g->de * g->de + g->dn * g->dn > g->len * g->len)
        return false;
    if (g->steps == 0) return !g->c1x && !g->c1y && !g->c2x && !g->c2y;
    return chainman_in(g->steps, 2, CHAINMAN_STEPS_MAX) &&
           chainman_in(g->c1x, -2 * GNOMON_COORD_MAX, 2 * GNOMON_COORD_MAX) &&
           chainman_in(g->c1y, -2 * GNOMON_COORD_MAX, 2 * GNOMON_COORD_MAX) &&
           chainman_in(g->c2x, -2 * GNOMON_COORD_MAX, 2 * GNOMON_COORD_MAX) &&
           chainman_in(g->c2y, -2 * GNOMON_COORD_MAX, 2 * GNOMON_COORD_MAX);
}

const chainman_parcel_t *chainman_find(const chainman_t *c, int64_t parcel)
{
    for (uint32_t i = 0; c && i < c->nparcels; i++)
        if (c->parcel[i].parcel == parcel) return &c->parcel[i];
    return NULL;
}

static uint32_t legs_of(const chainman_t *c, int64_t parcel)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < c->nlegs; i++) n += c->leg[i].parcel == parcel;
    return n;
}

/* CM2: the outline from a parcel and the legs given (legs, n), so a leg
 * can be tried before it is kept. */
static int walk(const chainman_parcel_t *p, const chainman_leg_t *legs, uint32_t nlegs, int64_t *x,
                int64_t *y, uint32_t cap, uint32_t *n)
{
    uint32_t k = 0, mine = 0, seen = 0;
    for (uint32_t i = 0; i < nlegs; i++) mine += legs[i].parcel == p->parcel;
    if (!cap) return ZXM_E_TOOBIG;
    int64_t ax = p->x0, ay = p->y0;
    x[k] = ax, y[k] = ay, k++;
    for (uint32_t i = 0; i < nlegs; i++) {
        const chainman_leg_t *g = &legs[i];
        if (g->parcel != p->parcel) continue;
        bool last = ++seen == mine;
        int64_t bx = ax + g->de, by = ay + g->dn;
        if (!coord_ok(bx) || !coord_ok(by)) return ZXM_E_ARG;
        if (g->steps) {
            int64_t p1x = ax + g->c1x, p1y = ay + g->c1y, p2x = ax + g->c2x, p2y = ay + g->c2y;
            for (int64_t s = 1; s < g->steps; s++) {
                int64_t qx = burin_bezier_at(ax, p1x, p2x, bx, s, g->steps);
                int64_t qy = burin_bezier_at(ay, p1y, p2y, by, s, g->steps);
                if (qx == BURIN_REFUSED || qy == BURIN_REFUSED || !coord_ok(qx) || !coord_ok(qy))
                    return ZXM_E_ARG;
                if (k >= cap) return ZXM_E_TOOBIG;
                x[k] = qx, y[k] = qy, k++;
            }
        }
        if (!last) {
            if (k >= cap) return ZXM_E_TOOBIG;
            x[k] = bx, y[k] = by, k++;
        }
        ax = bx, ay = by;
    }
    *n = k;
    return ZXM_OK;
}

int chainman_outline(const chainman_t *c, int64_t parcel, int64_t *x, int64_t *y, uint32_t cap,
                     uint32_t *n)
{
    const chainman_parcel_t *p = chainman_find(c, parcel);
    if (!p || !x || !y || !n) return ZXM_E_ARG;
    return walk(p, c->leg, c->nlegs, x, y, cap, n);
}

static int64_t OX[CHAINMAN_VERTS], OY[CHAINMAN_VERTS];

/* CM2: every corner of every parcel fits ^ZXCHAIN, every outline stays
 * in gnomon's domain under CHAINMAN_VERTS corners. */
static bool book_ok(const chainman_t *c, const chainman_leg_t *legs, uint32_t nlegs)
{
    for (uint32_t i = 0; i < c->nparcels; i++) {
        uint32_t n = 0;
        if (walk(&c->parcel[i], legs, nlegs, OX, OY, CHAINMAN_VERTS, &n) != ZXM_OK) return false;
        for (uint32_t k = 0; k < n; k++)
            if (!chainman_corner_node_ok(c->survey, c->parcel[i].parcel, (int64_t) k, OX[k], OY[k]))
                return false;
    }
    return true;
}

bool chainman_path_record_ok(const chainman_t *c, int64_t parcel, int64_t mtime)
{
    uint32_t n = 0;
    if (chainman_outline(c, parcel, OX, OY, CHAINMAN_VERTS, &n) != ZXM_OK) return false;
    return burin_record_ok(parcel, (int64_t) n, CHAINMAN_STROKE, mtime) && mtime >= 0;
}

/* ---- CM3 ---- */

int chainman_area2(const chainman_t *c, int64_t parcel, int64_t *area2)
{
    uint32_t n = 0;
    if (!area2) return ZXM_E_ARG;
    int rc = chainman_outline(c, parcel, OX, OY, CHAINMAN_VERTS, &n);
    if (rc) return rc;
    return gnomon_polygon_area2(OX, OY, n, area2) ? ZXM_OK : ZXM_E_STATE;
}

int chainman_area_m2(const chainman_t *c, int64_t parcel, int64_t *m2)
{
    int64_t a = 0;
    if (!m2) return ZXM_E_ARG;
    int rc = chainman_area2(c, parcel, &a);
    if (rc) return rc;
    *m2 = (a < 0 ? -a : a) / 2000000; /* twice mm^2 to whole square metres, floored */
    return ZXM_OK;
}

int chainman_closure(const chainman_t *c, int64_t parcel, int64_t *dx, int64_t *dy, int64_t *perim)
{
    static int64_t pos[CHAINMAN_LEGS], lo[CHAINMAN_LEGS], de[CHAINMAN_LEGS], dn[CHAINMAN_LEGS],
        len[CHAINMAN_LEGS];
    int64_t mm = 0;
    if (!chainman_find(c, parcel) || !dx || !dy || !perim) return ZXM_E_ARG;
    if (!legs_of(c, parcel)) return ZXM_E_STATE;
    for (uint32_t i = 0; i < c->nlegs; i++) {
        pos[i] = c->leg[i].parcel;
        lo[i] = parcel;
        de[i] = c->leg[i].de;
        dn[i] = c->leg[i].dn;
        len[i] = c->leg[i].len;
    }
    if (!furrow_range_total(pos, lo, lo, de, c->nlegs, dx) ||
        !furrow_range_total(pos, lo, lo, dn, c->nlegs, dy) ||
        !furrow_range_total(pos, lo, lo, len, c->nlegs, &mm))
        return ZXM_E_STATE;
    *perim = mm / 1000; /* whole metres, floored */
    return ZXM_OK;
}

lpres_state_t chainman_closes(const chainman_t *c, int64_t parcel)
{
    int64_t dx = 0, dy = 0, perim = 0, slack = 0;
    const chainman_parcel_t *p = chainman_find(c, parcel);
    if (!p || chainman_closure(c, parcel, &dx, &dy, &perim) != ZXM_OK ||
        !chainman_closure_slack(dx, dy, perim, p->tol, &slack))
        return LPRES_STATE_NEITHER;
    return chainman_traverse_closes(slack) ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
}

/* ---- CM7 ---- */

lpres_state_t chainman_cell(const chainman_t *c, int64_t cx, int64_t cy)
{
    static int64_t x[CHAINMAN_CLAIMS + 1u], y[CHAINMAN_CLAIMS + 1u], p[CHAINMAN_CLAIMS + 1u];
    uint32_t n = c->nclaims;
    int64_t claims = 0;
    for (uint32_t i = 0; i < n; i++) {
        x[i] = c->claim[i].cx, y[i] = c->claim[i].cy, p[i] = c->claim[i].parcel;
        claims += x[i] == cx && y[i] == cy;
    }
    int64_t rivals = 0;
    for (uint32_t i = 0; i < n; i++)
        if (x[i] == cx && y[i] == cy) rivals += chainman_cell_rivals(x, y, p, n, i);
    return chainman_cell_state(claims, rivals);
}

/* ---- zxvd ---- */

static bool no_such(const zxm_link_t *l, int rc)
{
    return rc == ZXM_E_REMOTE && l->err_code == CHAINMAN_E_FILES && strstr(l->err, "no such");
}

static int32_t read_book(chainman_t *c, char *text, uint32_t cap)
{
    zxm_link_t *l = c->link;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", CHAINMAN_BOOK);
    zxm_p_u64(l, "max", cap - 1u);
    int rc = zxm_call(l);
    if (no_such(l, rc)) return ZXM_E_STATE;
    if (rc) return rc;
    bool eof = false;
    int32_t t = zxm_get(l, l->result, "text");
    if (t < 0 || zxm_get_bool(l, l->result, "eof", &eof)) return ZXM_E_PROTOCOL;
    if (!eof) return ZXM_E_TOOBIG;
    int32_t len = w4_json_str(l->resp, &l->tok[t], text, cap);
    return len < 0 ? ZXM_E_TOOBIG : len;
}

static int write_book(chainman_t *c, const char *text, uint32_t len)
{
    zxm_link_t *l = c->link;
    zxm_begin(l, "files.mkdir");
    zxm_p_str(l, "path", CHAINMAN_DIR);
    int rc = zxm_call(l);
    if (rc && !(rc == ZXM_E_REMOTE && l->err_code == CHAINMAN_E_FILES &&
                strstr(l->err, "already exists")))
        return rc;
    uint32_t at = 0;
    bool first = true;
    do {
        uint32_t n = len - at < CHAINMAN_PIECE ? len - at : CHAINMAN_PIECE;
        bool last = at + n >= len;
        zxm_begin(l, "files.write");
        zxm_p_str(l, "path", CHAINMAN_BOOK);
        zxm_p_strn(l, "text", text + at, n);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
    } while (at < len);
    return ZXM_OK;
}

/* ---- CM1: the field book ---- */

static bool line(char *t, uint32_t cap, uint32_t *at, char kind, const int64_t *f, uint32_t n)
{
    char k[2] = {kind, 0};
    if (!put(t, cap, at, k)) return false;
    for (uint32_t i = 0; i < n; i++)
        if (!put(t, cap, at, "\t") || !put_i64(t, cap, at, f[i])) return false;
    return put(t, cap, at, "\n");
}

static int save(chainman_t *c)
{
    static char text[CHAINMAN_TEXT];
    uint32_t at = 0;
    text[0] = 0;
    bool ok = line(text, sizeof text, &at, 'S', &c->survey, 1);
    for (uint32_t i = 0; ok && i < c->nparcels; i++) {
        const chainman_parcel_t *p = &c->parcel[i];
        int64_t f[6] = {p->parcel, p->kind, p->title, p->x0, p->y0, p->tol};
        ok = line(text, sizeof text, &at, 'P', f, 6);
    }
    for (uint32_t i = 0; ok && i < c->nlegs; i++) {
        const chainman_leg_t *g = &c->leg[i];
        int64_t f[9] = {g->parcel, g->de, g->dn, g->len, g->steps, g->c1x, g->c1y, g->c2x, g->c2y};
        ok = line(text, sizeof text, &at, 'L', f, 9);
    }
    for (uint32_t i = 0; ok && i < c->nmarks; i++) {
        const chainman_mark_t *m = &c->mark[i];
        int64_t f[4] = {m->parcel, m->corner, m->lat, m->lon};
        ok = line(text, sizeof text, &at, 'K', f, 4);
    }
    for (uint32_t i = 0; ok && i < c->nclaims; i++) {
        const chainman_claim_t *m = &c->claim[i];
        int64_t f[3] = {m->cx, m->cy, m->parcel};
        ok = line(text, sizeof text, &at, 'C', f, 3);
    }
    if (!ok) return ZXM_E_TOOBIG;
    return write_book(c, text, at);
}

static bool mark_ok(const chainman_t *c, const chainman_mark_t *m)
{
    return chainman_find(c, m->parcel) && chainman_in(m->corner, 0, 9999) &&
           m->corner < (int64_t) legs_of(c, m->parcel) && chainman_place_ok(m->lat, m->lon);
}

static bool claim_ok(const chainman_t *c, const chainman_claim_t *m)
{
    return chainman_find(c, m->parcel) &&
           chainman_in(m->cx, -CHAINMAN_GRID_MAX, CHAINMAN_GRID_MAX) &&
           chainman_in(m->cy, -CHAINMAN_GRID_MAX, CHAINMAN_GRID_MAX);
}

static bool claimed(const chainman_t *c, int64_t cx, int64_t cy, int64_t parcel)
{
    for (uint32_t i = 0; i < c->nclaims; i++)
        if (c->claim[i].cx == cx && c->claim[i].cy == cy && c->claim[i].parcel == parcel)
            return true;
    return false;
}

/* one line: its letter, then n fields; false when it does not fit */
static bool read_line(const char *t, uint32_t len, uint32_t *at, char *kind, int64_t *f,
                      uint32_t *n)
{
    static const char KINDS[] = "SPLKC";
    static const uint32_t COUNT[] = {1, 6, 9, 4, 3};
    if (*at + 1u >= len) return false;
    const char *k = strchr(KINDS, t[*at]);
    if (!k || !*k || t[*at + 1u] != '\t') return false;
    uint32_t want = COUNT[k - KINDS];
    *at += 2u;
    for (uint32_t i = 0; i < want; i++)
        if (!get_i64(t, len, at, i + 1u == want ? '\n' : '\t', &f[i])) return false;
    *kind = *k;
    *n = want;
    return true;
}

static chainman_t LOADED;

int chainman_load(chainman_t *c)
{
    static char text[CHAINMAN_TEXT];
    if (!c || !c->link || !c->survey) return ZXM_E_ARG;
    int32_t len = read_book(c, text, sizeof text);
    chainman_t *t = &LOADED;
    memset(t, 0, sizeof *t);
    t->link = c->link;
    t->survey = c->survey;
    if (len == ZXM_E_STATE) len = 0;
    if (len < 0) return len;
    uint32_t at = 0;
    bool head = false;
    while (at < (uint32_t) len) {
        char kind = 0;
        int64_t f[CHAINMAN_FIELDS];
        uint32_t n = 0;
        if (!read_line(text, (uint32_t) len, &at, &kind, f, &n)) return ZXM_E_PROTOCOL;
        if (kind == 'S') {
            if (head || f[0] != c->survey) return ZXM_E_PROTOCOL;
            head = true;
        } else if (kind == 'P') {
            chainman_parcel_t p = {f[0], f[1], f[2], f[3], f[4], f[5]};
            if (t->nparcels >= CHAINMAN_PARCELS || !parcel_ok(&p) || chainman_find(t, p.parcel))
                return ZXM_E_PROTOCOL;
            t->parcel[t->nparcels++] = p;
        } else if (kind == 'L') {
            chainman_leg_t g = {f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8]};
            if (t->nlegs >= CHAINMAN_LEGS || !leg_ok(&g) || !chainman_find(t, g.parcel))
                return ZXM_E_PROTOCOL;
            t->leg[t->nlegs++] = g;
        } else if (kind == 'K') {
            chainman_mark_t m = {f[0], f[1], f[2], f[3]};
            if (t->nmarks >= CHAINMAN_MARKS || !mark_ok(t, &m)) return ZXM_E_PROTOCOL;
            t->mark[t->nmarks++] = m;
        } else {
            chainman_claim_t m = {f[0], f[1], f[2]};
            if (t->nclaims >= CHAINMAN_CLAIMS || !claim_ok(t, &m) ||
                claimed(t, m.cx, m.cy, m.parcel))
                return ZXM_E_PROTOCOL;
            t->claim[t->nclaims++] = m;
        }
    }
    if (len && !head) return ZXM_E_PROTOCOL;
    if (!book_ok(t, t->leg, t->nlegs)) return ZXM_E_PROTOCOL;
    uint32_t places = c->nplaces;
    *c = *t;
    c->nplaces = places;
    c->loaded = true;
    say(c, "field book read");
    return ZXM_OK;
}

void chainman_init(chainman_t *c, zxm_link_t *link, int64_t survey)
{
    memset(c, 0, sizeof *c);
    c->link = link;
    c->survey = chainman_in(survey, 1, CHAINMAN_PARCEL_MAX) ? survey : 0;
}

static int ready(const chainman_t *c)
{
    return !c || !c->loaded || !c->survey ? ZXM_E_STATE : ZXM_OK;
}

int chainman_add_parcel(chainman_t *c, int64_t parcel, int64_t kind, int64_t x0, int64_t y0,
                        int64_t tol)
{
    if (ready(c)) return ZXM_E_STATE;
    chainman_parcel_t p = {parcel, kind, 0, x0, y0, tol};
    if (!parcel_ok(&p) || chainman_find(c, parcel) ||
        !chainman_corner_node_ok(c->survey, parcel, 0, x0, y0))
        return ZXM_E_ARG;
    if (c->nparcels >= CHAINMAN_PARCELS) return ZXM_E_TOOBIG;
    c->parcel[c->nparcels++] = p;
    int rc = save(c);
    if (rc)
        c->nparcels--; /* what was shown stays */
    else
        say(c, "parcel added");
    return rc;
}

static int add(chainman_t *c, const chainman_leg_t *g)
{
    if (ready(c)) return ZXM_E_STATE;
    if (!leg_ok(g) || !chainman_find(c, g->parcel)) return ZXM_E_ARG;
    if (c->nlegs >= CHAINMAN_LEGS) return ZXM_E_TOOBIG;
    c->leg[c->nlegs] = *g;
    if (!book_ok(c, c->leg, c->nlegs + 1u)) return ZXM_E_ARG; /* checked before it is kept */
    c->nlegs++;
    int rc = save(c);
    if (rc)
        c->nlegs--;
    else
        say(c, "leg measured");
    return rc;
}

int chainman_add_leg(chainman_t *c, int64_t parcel, int64_t de, int64_t dn, int64_t len)
{
    chainman_leg_t g = {parcel, de, dn, len, 0, 0, 0, 0, 0};
    return add(c, &g);
}

int chainman_add_curve(chainman_t *c, int64_t parcel, int64_t de, int64_t dn, int64_t len,
                       int64_t c1x, int64_t c1y, int64_t c2x, int64_t c2y, int64_t steps)
{
    chainman_leg_t g = {parcel, de, dn, len, steps, c1x, c1y, c2x, c2y};
    if (steps == 0) return ZXM_E_ARG; /* a straight leg is chainman_add_leg */
    return add(c, &g);
}

/* ---- CM4 ---- */

int chainman_places(chainman_t *c)
{
    zxm_link_t *l = c->link;
    zxm_begin(l, "place.list");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "places");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr), mine = 0;
    for (uint32_t i = 0; i < n; i++) {
        char name[64];
        int32_t o = zxm_at(l, arr, i);
        if (o < 0 || zxm_get_str(l, o, "name", name, sizeof name)) return ZXM_E_PROTOCOL;
        mine += !strncmp(name, CHAINMAN_PREFIX, sizeof CHAINMAN_PREFIX - 1u);
    }
    c->nplaces = mine;
    return ZXM_OK;
}

int chainman_mark(chainman_t *c, int64_t parcel, int64_t corner)
{
    if (ready(c)) return ZXM_E_STATE;
    if (!chainman_find(c, parcel) || corner < 0 || corner >= (int64_t) legs_of(c, parcel))
        return ZXM_E_ARG;
    for (uint32_t i = 0; i < c->nmarks; i++)
        if (c->mark[i].parcel == parcel && c->mark[i].corner == corner) return ZXM_E_ARG;
    if (c->nmarks >= CHAINMAN_MARKS) return ZXM_E_TOOBIG;
    zxm_link_t *l = c->link;
    zxm_begin(l, "place.here");
    int rc = zxm_call(l);
    if (rc) return rc;
    bool known = false;
    int64_t lat = 0, lon = 0;
    if (zxm_get_bool(l, l->result, "known", &known)) return ZXM_E_PROTOCOL;
    if (!known) {
        say(c, "where the machine stands is not known: say where first");
        return ZXM_E_STATE;
    }
    if (zxm_get_i64(l, l->result, "lat", &lat) || zxm_get_i64(l, l->result, "lon", &lon) ||
        !chainman_place_ok(lat, lon))
        return ZXM_E_PROTOCOL;
    char name[64];
    uint32_t at = 0;
    if (!put(name, sizeof name, &at, CHAINMAN_PREFIX) ||
        !put_i64(name, sizeof name, &at, c->survey) || !put(name, sizeof name, &at, "-") ||
        !put_i64(name, sizeof name, &at, parcel) || !put(name, sizeof name, &at, "-") ||
        !put_i64(name, sizeof name, &at, corner))
        return ZXM_E_TOOBIG;
    zxm_begin(l, "place.add");
    zxm_p_str(l, "name", name);
    zxm_p_i64(l, "lat", lat);
    zxm_p_i64(l, "lon", lon);
    zxm_p_str(l, "note", "survey corner, advisory: not a legal boundary");
    if ((rc = zxm_call(l)) != ZXM_OK) return rc;
    c->mark[c->nmarks++] = (chainman_mark_t){parcel, corner, lat, lon};
    if ((rc = save(c)) != ZXM_OK) {
        c->nmarks--;
        return rc;
    }
    say(c, "corner marked on the map");
    return chainman_places(c);
}

/* ---- CM5 ---- */

int chainman_title(chainman_t *c, int64_t parcel, int64_t title, bool confirmed)
{
    if (ready(c)) return ZXM_E_STATE;
    chainman_parcel_t *p = NULL;
    int64_t m2 = 0;
    for (uint32_t i = 0; i < c->nparcels; i++)
        if (c->parcel[i].parcel == parcel) p = &c->parcel[i];
    if (!p) return ZXM_E_ARG;
    if (!confirmed) {
        say(c, "a title is filed only on the owner's word");
        return ZXM_E_STATE;
    }
    int rc = chainman_area_m2(c, parcel, &m2);
    if (rc) return rc;
    if (!chainman_title_fits(title, parcel, m2) || !chainman_title_record_ok(title, parcel, m2) ||
        !understory_land_entry_ok(parcel, p->kind, m2) || !understory_land_ok(parcel, p->kind, m2))
        return ZXM_E_ARG;
    int64_t was = p->title;
    p->title = title;
    if ((rc = save(c)) != ZXM_OK) {
        p->title = was;
        return rc;
    }
    say(c, "title filed: advisory, the land registry decides");
    return ZXM_OK;
}

/* ---- CM6 ---- */

int chainman_claim_cells(chainman_t *c, int64_t parcel, uint32_t *added)
{
    if (ready(c)) return ZXM_E_STATE;
    uint32_t n = 0, k = 0, was = c->nclaims;
    if (!added) return ZXM_E_ARG;
    *added = 0;
    int rc = chainman_outline(c, parcel, OX, OY, CHAINMAN_VERTS, &n);
    if (rc) return rc;
    if (n < 3u) return ZXM_E_STATE;
    int64_t x0 = OX[0], x1 = OX[0], y0 = OY[0], y1 = OY[0];
    for (uint32_t i = 1; i < n; i++) {
        x0 = OX[i] < x0 ? OX[i] : x0, x1 = OX[i] > x1 ? OX[i] : x1;
        y0 = OY[i] < y0 ? OY[i] : y0, y1 = OY[i] > y1 ? OY[i] : y1;
    }
    int64_t a = chainman_cell_of(x0), b = chainman_cell_of(x1), d = chainman_cell_of(y0),
            e = chainman_cell_of(y1);
    if (b - a >= CHAINMAN_SCAN || e - d >= CHAINMAN_SCAN) return ZXM_E_TOOBIG;
    for (int64_t cx = a; cx <= b; cx++)
        for (int64_t cy = d; cy <= e; cy++) {
            if (!chainman_inside(OX, OY, n, cx, cy) || claimed(c, cx, cy, parcel)) continue;
            if (c->nclaims >= CHAINMAN_CLAIMS) {
                c->nclaims = was;
                return ZXM_E_TOOBIG;
            }
            c->claim[c->nclaims++] = (chainman_claim_t){cx, cy, parcel};
            k++;
        }
    if (!k) return ZXM_OK;
    if ((rc = save(c)) != ZXM_OK) {
        c->nclaims = was;
        return rc;
    }
    *added = k;
    say(c, "cells claimed; a disputed cell is shown, never settled");
    return ZXM_OK;
}
