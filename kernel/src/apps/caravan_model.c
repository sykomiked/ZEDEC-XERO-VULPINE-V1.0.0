/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* caravan_model.c — Caravan, freight and shipments (see caravan_model.h).
 * Which carrier to take is advice; the owner confirms it. */
#include <string.h>

#include "caravan_model.h"

/* ---- parcels and carriers (CR1-CR3) ----------------------------------------- */

int caravan_parcel(int64_t mass, int64_t a, int64_t b, int64_t c, int64_t divisor,
                   caravan_parcel_t *p)
{
    int64_t s[3] = {a, b, c};
    if (!p) return ZXM_E_ARG;
    for (int i = 1; i < 3; i++) /* longest first */
        for (int j = i; j > 0 && s[j] > s[j - 1]; j--) {
            int64_t t = s[j];
            s[j] = s[j - 1];
            s[j - 1] = t;
        }
    int64_t charged = caravan_charged(mass, s[0], s[1], s[2], divisor);
    int64_t girth = caravan_girth(s[1], s[2]);
    if (charged < 0 || girth < 0) return ZXM_E_ARG;
    *p = (caravan_parcel_t){mass, s[0], s[1], s[2], girth, s[0] + girth, charged};
    return ZXM_OK;
}

caravan_margin_t caravan_margins(const caravan_carrier_t *k, const caravan_parcel_t *p)
{
    return (caravan_margin_t){k->mass_max - p->mass, k->length_max - p->length, k->lg_max - p->lg};
}

bool caravan_within(const caravan_carrier_t *k, const caravan_parcel_t *p)
{
    caravan_margin_t m = caravan_margins(k, p);
    return caravan_fits(m.mass, m.length, m.lg);
}

bool caravan_carrier_ok(const caravan_carrier_t *k)
{
    return k && k->id >= 1 && k->id <= CARAVAN_CARRIER_MAX && k->mass_max >= 1 &&
           k->mass_max <= CARAVAN_GRAMS_MAX && k->length_max >= 1 &&
           k->length_max <= CARAVAN_SIDE_MAX && k->lg_max >= 1 && k->lg_max <= CARAVAN_LG_MAX &&
           k->divisor >= 1 && k->divisor <= CARAVAN_DIVISOR_MAX && k->quick >= 1 &&
           k->quick <= k->slow && k->slow <= CARAVAN_HOURS_MAX;
}

/* A place's name as place.add allows it: 1..63 bytes, no control byte. */
static bool name_ok(const char *s)
{
    if (!s) return false;
    size_t n = strlen(s);
    if (!n || n > CARAVAN_PLACE_MAX) return false;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char) s[i] < 0x20 || s[i] == 0x7f) return false;
    return true;
}

/* A shipment as its record (CR5) and its parcel at its carrier's divisor. */
static bool ship_parcel(const caravan_carrier_t *k, const caravan_ship_t *s, caravan_parcel_t *p)
{
    return caravan_parcel(s->mass, s->side[0], s->side[1], s->side[2], k->divisor, p) == ZXM_OK &&
           caravan_ship_ok(s->id, s->carrier, s->mass, p->charged, s->status, s->stamp);
}

const caravan_carrier_t *caravan_find_carrier(const caravan_t *c, int64_t id)
{
    for (uint32_t i = 0; i < c->ncarriers; i++)
        if (c->carrier[i].id == id) return &c->carrier[i];
    return NULL;
}

const caravan_ship_t *caravan_find_ship(const caravan_t *c, int64_t id)
{
    for (uint32_t i = 0; i < c->nships; i++)
        if (c->ship[i].id == id) return &c->ship[i];
    return NULL;
}

/* ---- the file as text (CM1-CM4) -------------------------------------------- */

static const char HEAD[] = "caravan 1\n";

/* Append s to out at *at, bounded by cap (room for a NUL is kept). */
static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    uint32_t n = (uint32_t) strlen(s);
    if (*at + n + 1u > cap) return false;
    memcpy(out + *at, s, n);
    *at += n;
    out[*at] = 0;
    return true;
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

/* n numbers, each after a space. */
static bool put_nums(char *out, uint32_t cap, uint32_t *at, const int64_t *v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (!put(out, cap, at, " ") || !put_num(out, cap, at, v[i])) return false;
    return true;
}

/* The whole file for c (CM1-CM3); its length, or -1 when it does not fit. */
static int32_t compose(const caravan_t *c, char *out, uint32_t cap)
{
    uint32_t at = 0;
    if (!put(out, cap, &at, HEAD)) return -1;
    for (uint32_t i = 0; i < c->ncarriers; i++) {
        const caravan_carrier_t *k = &c->carrier[i];
        int64_t v[7] = {k->id,      k->mass_max, k->length_max, k->lg_max,
                        k->divisor, k->quick,    k->slow};
        if (!put(out, cap, &at, "carrier") || !put_nums(out, cap, &at, v, 7) ||
            !put(out, cap, &at, "\n"))
            return -1;
    }
    for (uint32_t i = 0; i < c->nships; i++) {
        const caravan_ship_t *s = &c->ship[i];
        int64_t v[8] = {s->id,      s->carrier, s->mass,   s->side[0],
                        s->side[1], s->side[2], s->status, s->stamp};
        if (!put(out, cap, &at, "ship") || !put_nums(out, cap, &at, v, 8) ||
            !put(out, cap, &at, " ") || !put(out, cap, &at, s->to) || !put(out, cap, &at, "\n"))
            return -1;
    }
    return (int32_t) at;
}

/* A plain decimal of 1 to 10 digits, no leading zero, at s[*at], ended by `end`. */
static bool get_num(const char *s, uint32_t n, uint32_t *at, char end, int64_t *v)
{
    uint32_t i = *at, digits = 0;
    int64_t x = 0;
    while (i < n && s[i] >= '0' && s[i] <= '9' && digits < 11u) {
        x = x * 10 + (s[i] - '0');
        i++;
        digits++;
    }
    if (!digits || digits > 10u || i >= n || s[i] != end) return false;
    if (digits > 1u && s[*at] == '0') return false;
    *v = x;
    *at = i + 1u;
    return true;
}

static bool get_nums(const char *s, uint32_t n, uint32_t *at, int64_t *v, uint32_t count, char end)
{
    for (uint32_t i = 0; i < count; i++)
        if (!get_num(s, n, at, i + 1u < count ? ' ' : end, &v[i])) return false;
    return true;
}

static bool lead(const char *s, uint32_t n, uint32_t *at, const char *word)
{
    uint32_t k = (uint32_t) strlen(word);
    if (n - *at < k || memcmp(s + *at, word, k)) return false;
    *at += k;
    return true;
}

/* Parse text into next (CM1-CM4): false when it is not Caravan's file. */
static bool parse(caravan_t *next, const char *s, uint32_t n)
{
    uint32_t at = (uint32_t) strlen(HEAD);
    if (n < at || memcmp(s, HEAD, at)) return false;
    next->ncarriers = next->nships = 0;
    while (at < n) {
        int64_t v[8];
        if (!next->nships && lead(s, n, &at, "carrier ")) {
            if (next->ncarriers >= CARAVAN_CARRIERS || !get_nums(s, n, &at, v, 7, '\n') ||
                v[0] > CARAVAN_CARRIER_MAX || v[5] > CARAVAN_HOURS_MAX || v[6] > CARAVAN_HOURS_MAX)
                return false;
            caravan_carrier_t k = {(int32_t) v[0], v[1],           v[2],          v[3],
                                   v[4],           (int32_t) v[5], (int32_t) v[6]};
            if (!caravan_carrier_ok(&k) ||
                (next->ncarriers && next->carrier[next->ncarriers - 1u].id >= k.id))
                return false;
            next->carrier[next->ncarriers++] = k;
            continue;
        }
        if (!lead(s, n, &at, "ship ") || next->nships >= CARAVAN_SHIPS ||
            !get_nums(s, n, &at, v, 8, ' '))
            return false;
        uint32_t end = at;
        while (end < n && s[end] != '\n') end++;
        if (end >= n || end - at > CARAVAN_PLACE_MAX) return false;
        char to[CARAVAN_PLACE_MAX + 1] = {0};
        memcpy(to, s + at, end - at);
        at = end + 1u;
        if (v[0] > CARAVAN_SHIP_MAX || v[1] > CARAVAN_CARRIER_MAX || v[6] > CARAVAN_RETURNED ||
            !name_ok(to))
            return false;
        caravan_ship_t *sh = &next->ship[next->nships];
        *sh = (caravan_ship_t){
            (int32_t) v[0], (int32_t) v[1], (int32_t) v[6], v[2], {v[3], v[4], v[5]}, v[7], {0}};
        memcpy(sh->to, to, sizeof to);
        caravan_parcel_t p;
        const caravan_carrier_t *k = caravan_find_carrier(next, sh->carrier);
        if (!k || !ship_parcel(k, sh, &p) ||
            (next->nships && next->ship[next->nships - 1u].id >= sh->id))
            return false;
        next->nships++;
    }
    return true;
}

/* ---- zxvd ---------------------------------------------------------------- */

/* One request's text: whole lines of at most this many bytes. Each byte
 * escapes to at most two ("\n"), well inside the request line. */
#define PIECE 6000u

static int write_text(zxm_link_t *l, const char *text, uint32_t len)
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
        zxm_p_str(l, "path", CARAVAN_FILE);
        zxm_p_strn(l, "text", text + at, n);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        int rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
    } while (at < len);
    return ZXM_OK;
}

static caravan_t NEXT; /* a change before it is written (CM5) */

static void begin_change(const caravan_t *c)
{
    memcpy(NEXT.carrier, c->carrier, (size_t) c->ncarriers * sizeof c->carrier[0]);
    NEXT.ncarriers = c->ncarriers;
    memcpy(NEXT.ship, c->ship, (size_t) c->nships * sizeof c->ship[0]);
    NEXT.nships = c->nships;
}

/* Write NEXT whole, then keep it (CM5). */
static int commit(caravan_t *c)
{
    static char text[CARAVAN_TEXT_MAX];
    int32_t len = compose(&NEXT, text, sizeof text);
    if (len < 0) return ZXM_E_TOOBIG;
    int rc = write_text(c->link, text, (uint32_t) len);
    if (rc) return rc;
    memcpy(c->carrier, NEXT.carrier, (size_t) NEXT.ncarriers * sizeof c->carrier[0]);
    c->ncarriers = NEXT.ncarriers;
    memcpy(c->ship, NEXT.ship, (size_t) NEXT.nships * sizeof c->ship[0]);
    c->nships = NEXT.nships;
    return ZXM_OK;
}

void caravan_init(caravan_t *c, zxm_link_t *link)
{
    memset(c, 0, sizeof *c);
    c->link = link;
}

int caravan_load(caravan_t *c)
{
    static char text[CARAVAN_TEXT_MAX];
    static char chunk[CARAVAN_TEXT_MAX];
    zxm_link_t *l = c->link;
    uint32_t len = 0;
    uint64_t off = 0;
    for (;;) {
        zxm_begin(l, "files.read");
        zxm_p_str(l, "path", CARAVAN_FILE);
        zxm_p_u64(l, "offset", off);
        int rc = zxm_call(l);
        if (rc == ZXM_E_REMOTE && l->err_code == CARAVAN_E_FILES && !off &&
            strstr(l->err, "no such file")) {
            c->ncarriers = c->nships = 0; /* not made yet: an empty file */
            c->loaded = true;
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
        if (size >= CARAVAN_TEXT_MAX) return ZXM_E_TOOBIG;
        int32_t n = w4_json_str(l->resp, &l->tok[t], chunk, sizeof chunk);
        if (n < 0 || len + (uint32_t) n >= CARAVAN_TEXT_MAX) return ZXM_E_TOOBIG;
        memcpy(text + len, chunk, (size_t) n);
        len += (uint32_t) n;
        if (eof) break;
        if (next_off <= off) return ZXM_E_PROTOCOL;
        off = next_off;
    }
    if (!parse(&NEXT, text, len)) {
        c->loaded = false; /* CM4: never overwritten */
        return ZXM_E_PROTOCOL;
    }
    memcpy(c->carrier, NEXT.carrier, (size_t) NEXT.ncarriers * sizeof c->carrier[0]);
    c->ncarriers = NEXT.ncarriers;
    memcpy(c->ship, NEXT.ship, (size_t) NEXT.nships * sizeof c->ship[0]);
    c->nships = NEXT.nships;
    c->loaded = true;
    return ZXM_OK;
}

int caravan_places(caravan_t *c)
{
    static caravan_place_t got[CARAVAN_PLACES];
    zxm_link_t *l = c->link;
    zxm_begin(l, "place.list");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "places");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr);
    if (n > CARAVAN_PLACES) return ZXM_E_TOOBIG;
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, arr, i), d = o < 0 ? -1 : zxm_get(l, o, "distance_m");
        caravan_place_t *p = &got[i];
        memset(p, 0, sizeof *p);
        if (d < 0 || zxm_get_str(l, o, "name", p->name, sizeof p->name) || !name_ok(p->name))
            return ZXM_E_PROTOCOL;
        p->distance_m = -1;
        if (l->tok[d].type != W4_J_NULL) {
            uint64_t m;
            if (zxm_get_u64(l, o, "distance_m", &m) || m > INT64_MAX) return ZXM_E_PROTOCOL;
            p->distance_m = (int64_t) m;
        }
    }
    for (uint32_t i = 1; i < n; i++) /* nearest first; unknown distances last, as given */
        for (uint32_t j = i; j > 0; j--) {
            int64_t a = got[j - 1u].distance_m, b = got[j].distance_m;
            if (b < 0 || (a >= 0 && a <= b)) break;
            caravan_place_t t = got[j];
            got[j] = got[j - 1u];
            got[j - 1u] = t;
        }
    memcpy(c->place, got, (size_t) n * sizeof got[0]);
    c->nplaces = n;
    return ZXM_OK;
}

/* ---- changes (CM5-CM7) ------------------------------------------------------ */

static int ship_index(const caravan_t *c, int64_t id)
{
    for (uint32_t i = 0; i < c->nships; i++)
        if (c->ship[i].id == id) return (int) i;
    return -1;
}

static const char *place_named(const caravan_t *c, const char *name)
{
    for (uint32_t i = 0; i < c->nplaces; i++) {
        const char *a = c->place[i].name, *b = name;
        while (*a && *b) {
            char x = *a >= 'A' && *a <= 'Z' ? (char) (*a + 32) : *a;
            char y = *b >= 'A' && *b <= 'Z' ? (char) (*b + 32) : *b;
            if (x != y) break;
            a++;
            b++;
        }
        if (!*a && !*b) return c->place[i].name;
    }
    return NULL;
}

int caravan_set_carrier(caravan_t *c, const caravan_carrier_t *k)
{
    if (!caravan_carrier_ok(k)) return ZXM_E_ARG;
    if (!c->loaded) return ZXM_E_STATE;
    begin_change(c);
    uint32_t i = 0;
    while (i < NEXT.ncarriers && NEXT.carrier[i].id < k->id) i++;
    if (i < NEXT.ncarriers && NEXT.carrier[i].id == k->id) {
        NEXT.carrier[i] = *k;
    } else {
        if (NEXT.ncarriers >= CARAVAN_CARRIERS) return ZXM_E_TOOBIG;
        memmove(&NEXT.carrier[i + 1u], &NEXT.carrier[i],
                (size_t) (NEXT.ncarriers - i) * sizeof NEXT.carrier[0]);
        NEXT.carrier[i] = *k;
        NEXT.ncarriers++;
    }
    for (uint32_t s = 0; s < NEXT.nships; s++) { /* every record still holds (CR5) */
        caravan_parcel_t p;
        const caravan_carrier_t *kk = caravan_find_carrier(&NEXT, NEXT.ship[s].carrier);
        if (!kk || !ship_parcel(kk, &NEXT.ship[s], &p)) return ZXM_E_ARG;
    }
    return commit(c);
}

/* What is over a carrier's limits, for the notice (CM5). */
static void over_notice(caravan_t *c, const caravan_carrier_t *k, caravan_margin_t m)
{
    char *s = c->notice;
    uint32_t cap = sizeof c->notice, at = 0;
    s[0] = 0;
    bool ok = put(s, cap, &at, "Over carrier ") && put_num(s, cap, &at, k->id) &&
              put(s, cap, &at, "'s limits:");
    if (m.mass < 0)
        ok = ok && put(s, cap, &at, " mass by ") && put_num(s, cap, &at, -m.mass) &&
             put(s, cap, &at, " g;");
    if (m.length < 0)
        ok = ok && put(s, cap, &at, " longest side by ") && put_num(s, cap, &at, -m.length) &&
             put(s, cap, &at, " mm;");
    if (m.lg < 0)
        ok = ok && put(s, cap, &at, " length and girth by ") && put_num(s, cap, &at, -m.lg) &&
             put(s, cap, &at, " mm;");
    ok = ok && put(s, cap, &at, " not added.");
    if (!ok) s[0] = 0;
}

int caravan_add(caravan_t *c, int64_t carrier, int64_t mass, int64_t a, int64_t b, int64_t s,
                const char *to, int64_t stamp, int64_t *id)
{
    caravan_parcel_t p;
    if (!id || !name_ok(to) || stamp < 0 || stamp > CARAVAN_STAMP_MAX) return ZXM_E_ARG;
    const caravan_carrier_t *k = caravan_find_carrier(c, carrier);
    if (!k || caravan_parcel(mass, a, b, s, k->divisor, &p) != ZXM_OK) return ZXM_E_ARG;
    if (!c->loaded) return ZXM_E_STATE;
    const char *place = place_named(c, to);
    if (!place) return ZXM_E_STATE; /* only a place zxvd keeps (caravan_places) */
    if (!caravan_within(k, &p)) {
        over_notice(c, k, caravan_margins(k, &p));
        return ZXM_E_ARG;
    }
    int64_t next = c->nships ? c->ship[c->nships - 1u].id + 1 : 1;
    if (c->nships >= CARAVAN_SHIPS || next > CARAVAN_SHIP_MAX) return ZXM_E_TOOBIG;
    if (!caravan_ship_ok(next, carrier, mass, p.charged, CARAVAN_DRAFTED, stamp)) return ZXM_E_ARG;
    begin_change(c);
    caravan_ship_t *sh = &NEXT.ship[NEXT.nships++];
    *sh = (caravan_ship_t){
        (int32_t) next, (int32_t) carrier, CARAVAN_DRAFTED, mass, {a, b, s}, stamp, {0}};
    memcpy(sh->to, place, strlen(place) + 1u);
    int rc = commit(c);
    if (rc == ZXM_OK) *id = next;
    return rc;
}

/* CM7: the statuses a shipment may move on to from `from`. */
static bool forward(int64_t from, int64_t to)
{
    if (to == CARAVAN_RETURNED) return from == CARAVAN_HANDED || from == CARAVAN_TRANSIT;
    return from < CARAVAN_DELIVERED && to == from + 1;
}

int caravan_advance(caravan_t *c, int64_t id, int64_t status, int64_t stamp)
{
    if (status < CARAVAN_DRAFTED || status > CARAVAN_RETURNED || stamp < 0 ||
        stamp > CARAVAN_STAMP_MAX)
        return ZXM_E_ARG;
    if (!c->loaded) return ZXM_E_STATE;
    int i = ship_index(c, id);
    if (i < 0) return ZXM_E_ARG;
    if (!forward(c->ship[i].status, status) || stamp < c->ship[i].stamp) return ZXM_E_STATE;
    begin_change(c);
    NEXT.ship[i].status = (int32_t) status;
    NEXT.ship[i].stamp = stamp;
    return commit(c);
}

int caravan_remove(caravan_t *c, int64_t id)
{
    if (!c->loaded) return ZXM_E_STATE;
    int i = ship_index(c, id);
    if (i < 0) return ZXM_E_ARG;
    if (c->ship[i].status != CARAVAN_DRAFTED) return ZXM_E_STATE;
    begin_change(c);
    memmove(&NEXT.ship[i], &NEXT.ship[i + 1],
            (size_t) (NEXT.nships - (uint32_t) i - 1u) * sizeof NEXT.ship[0]);
    NEXT.nships--;
    return commit(c);
}

int caravan_route(const caravan_t *c, int64_t id, caravan_route_t *r)
{
    if (!r) return ZXM_E_ARG;
    const caravan_ship_t *s = caravan_find_ship(c, id);
    if (!s) return ZXM_E_ARG;
    memset(r, 0, sizeof *r);
    for (uint32_t i = 0; i < c->ncarriers; i++) {
        const caravan_carrier_t *k = &c->carrier[i];
        caravan_parcel_t p;
        if (caravan_parcel(s->mass, s->side[0], s->side[1], s->side[2], k->divisor, &p) != ZXM_OK ||
            !caravan_within(k, &p))
            continue;
        r->payoff[2 * r->nopt] = -(int64_t) k->quick;
        r->payoff[2 * r->nopt + 1] = -(int64_t) k->slow;
        r->carrier[r->nopt++] = k->id;
    }
    if (!r->nopt) return ZXM_E_STATE;
    r->lo[0] = r->lo[1] = 0;
    r->hi[0] = r->hi[1] = 1;
    r->den = 1;
    return ZXM_OK;
}

int caravan_choose(caravan_t *c, int64_t id, int64_t carrier, bool confirmed)
{
    if (!c->loaded || !confirmed) return ZXM_E_STATE; /* CM6: the owner's word */
    int i = ship_index(c, id);
    const caravan_carrier_t *k = caravan_find_carrier(c, carrier);
    if (i < 0 || !k) return ZXM_E_ARG;
    const caravan_ship_t *s = &c->ship[i];
    caravan_parcel_t p;
    if (s->status != CARAVAN_DRAFTED) return ZXM_E_STATE;
    if (caravan_parcel(s->mass, s->side[0], s->side[1], s->side[2], k->divisor, &p) != ZXM_OK)
        return ZXM_E_ARG;
    if (!caravan_within(k, &p)) {
        over_notice(c, k, caravan_margins(k, &p));
        return ZXM_E_ARG;
    }
    begin_change(c);
    NEXT.ship[i].carrier = (int32_t) carrier;
    return commit(c);
}

static const char *const STATUS[CARAVAN_RETURNED + 1] = {"drafted",    "labelled",  "handed over",
                                                         "in transit", "delivered", "returned"};

int caravan_notice(caravan_t *c, int64_t id)
{
    const caravan_ship_t *s = caravan_find_ship(c, id);
    if (!s) return ZXM_E_ARG;
    const caravan_carrier_t *k = caravan_find_carrier(c, s->carrier);
    caravan_parcel_t p;
    if (!k || !ship_parcel(k, s, &p)) return ZXM_E_STATE;
    char *t = c->notice;
    uint32_t cap = sizeof c->notice, at = 0;
    t[0] = 0;
    bool ok = put(t, cap, &at, "Shipment ") && put_num(t, cap, &at, s->id) &&
              put(t, cap, &at, " to ") && put(t, cap, &at, s->to) && put(t, cap, &at, ": ") &&
              put(t, cap, &at, STATUS[s->status]) && put(t, cap, &at, " with carrier ") &&
              put_num(t, cap, &at, k->id) && put(t, cap, &at, ", ") &&
              put_num(t, cap, &at, p.charged) && put(t, cap, &at, " g charged (mass ") &&
              put_num(t, cap, &at, p.mass) && put(t, cap, &at, " g), ") &&
              put_num(t, cap, &at, k->quick) && put(t, cap, &at, " to ") &&
              put_num(t, cap, &at, k->slow) && put(t, cap, &at, " hours on the way.");
    if (!ok) {
        t[0] = 0;
        return ZXM_E_TOOBIG;
    }
    return ZXM_OK;
}
