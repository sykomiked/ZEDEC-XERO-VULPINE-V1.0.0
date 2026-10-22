/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* binnacle_model.c — Binnacle (see binnacle_model.h). */
#include <string.h>

#include "binnacle_model.h"
#include "weatherglass_model.h"

void binnacle_init(binnacle_t *b, zxm_link_t *link)
{
    memset(b, 0, sizeof *b);
    b->link = link;
    b->planet_mdeg[0] = b->planet_mdeg[1] = -1;
}

/* ---- text, bounded ---- */

static uint32_t len_of(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    if (!s) return 0;
    while (n < cap && s[n]) n++;
    return n;
}

/* n bytes of well-formed UTF-8 with no control byte */
static bool text_ok(const char *s, uint32_t n)
{
    const unsigned char *p = (const unsigned char *) s;
    uint32_t i = 0;
    while (i < n) {
        unsigned char c = p[i];
        uint32_t more;
        if (c < 0x20u || c == 0x7fu) return false;
        if (c < 0x80u) {
            i++;
            continue;
        }
        if (c >= 0xc2u && c <= 0xdfu)
            more = 1;
        else if (c >= 0xe0u && c <= 0xefu)
            more = 2;
        else if (c >= 0xf0u && c <= 0xf4u)
            more = 3;
        else
            return false;
        if (more > n - 1u - i) return false;
        for (uint32_t k = 1; k <= more; k++)
            if ((p[i + k] & 0xc0u) != 0x80u) return false;
        if (c == 0xe0u && p[i + 1] < 0xa0u) return false;
        if (c == 0xedu && p[i + 1] > 0x9fu) return false;
        if (c == 0xf0u && p[i + 1] < 0x90u) return false;
        if (c == 0xf4u && p[i + 1] > 0x8fu) return false;
        i += more + 1u;
    }
    return true;
}

/* a name of 1 .. BINNACLE_NAME_MAX bytes, not all spaces */
static bool name_ok(const char *s)
{
    uint32_t n = len_of(s, BINNACLE_NAME_MAX + 1u);
    bool solid = false;
    if (!n || n > BINNACLE_NAME_MAX || !text_ok(s, n)) return false;
    for (uint32_t i = 0; i < n; i++) solid = solid || s[i] != ' ';
    return solid;
}

static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    uint32_t n = len_of(s, cap);
    if (*at + n >= cap) return false;
    memcpy(out + *at, s, n);
    *at += n;
    out[*at] = 0;
    return true;
}

static bool put_i64(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char t[24];
    uint32_t k = 0;
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    do {
        t[k++] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u);
    if (v < 0) t[k++] = '-';
    if (*at + k >= cap) return false;
    while (k) out[(*at)++] = t[--k];
    out[*at] = 0;
    return true;
}

/* v in units of 1 / 10^places, as "i.fff" (places 1 .. 6) */
static bool put_fixed(char *out, uint32_t cap, uint32_t *at, int64_t v, uint32_t places)
{
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v, scale = 1;
    char frac[8];
    for (uint32_t i = 0; i < places; i++) scale *= 10u;
    uint64_t f = u % scale;
    for (uint32_t i = places; i > 0; i--) {
        frac[i - 1u] = (char) ('0' + (int) (f % 10u));
        f /= 10u;
    }
    frac[places] = 0;
    return (v >= 0 || put(out, cap, at, "-")) && put_i64(out, cap, at, (int64_t) (u / scale)) &&
           put(out, cap, at, ".") && put(out, cap, at, frac);
}

static bool put_place(char *out, uint32_t cap, uint32_t *at, int64_t lat, int64_t lon)
{
    return put_fixed(out, cap, at, lat, 6) && put(out, cap, at, ",") &&
           put_fixed(out, cap, at, lon, 6);
}

/* ---- BM1: the clock ---- */

int binnacle_clock(binnacle_t *b)
{
    if (!b || !b->link) return ZXM_E_ARG;
    zxm_link_t *l = b->link;
    zxm_begin(l, "sys.info");
    int rc = zxm_call(l);
    if (rc) return rc;
    uint64_t ms = 0;
    if (zxm_get_u64(l, l->result, "time_ms", &ms) || ms > UINT64_C(253402300799999))
        return ZXM_E_PROTOCOL;
    b->now_s = (int64_t) (ms / 1000u);
    b->clock_known = true;
    return ZXM_OK;
}

/* ---- BM2: the chart ---- */

int binnacle_here(binnacle_t *b)
{
    if (!b || !b->link) return ZXM_E_ARG;
    zxm_link_t *l = b->link;
    zxm_begin(l, "place.here");
    int rc = zxm_call(l);
    if (rc) return rc;
    bool known = false;
    int64_t lat = 0, lon = 0;
    if (zxm_get_bool(l, l->result, "known", &known)) return ZXM_E_PROTOCOL;
    if (known && (zxm_get_i64(l, l->result, "lat", &lat) ||
                  zxm_get_i64(l, l->result, "lon", &lon) || !binnacle_place_ok(lat, lon)))
        return ZXM_E_PROTOCOL;
    b->here_known = known;
    b->here_lat = known ? lat : 0;
    b->here_lon = known ? lon : 0;
    return ZXM_OK;
}

int binnacle_places(binnacle_t *b)
{
    if (!b || !b->link) return ZXM_E_ARG;
    zxm_link_t *l = b->link;
    zxm_begin(l, "place.list");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "places");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr), k = 0;
    for (uint32_t i = 0; i < n && k < BINNACLE_WPTS; i++) {
        int32_t o = zxm_at(l, arr, i);
        binnacle_wpt_t *w = &b->wpt[k];
        if (o < 0 || zxm_get_str(l, o, "name", w->name, sizeof w->name) || !w->name[0] ||
            zxm_get_i64(l, o, "lat", &w->lat) || zxm_get_i64(l, o, "lon", &w->lon) ||
            !binnacle_place_ok(w->lat, w->lon))
            continue; /* waymark.place_ok: off the globe, left off the chart */
        k++;
    }
    if (k != b->nwpt) memset(b->nroute, 0, sizeof b->nroute); /* the routes named old places */
    b->nwpt = k;
    return ZXM_OK;
}

int binnacle_add_waypoint(binnacle_t *b, const char *name, int64_t lat, int64_t lon,
                          const char *note, bool owner_confirmed)
{
    uint32_t tn = len_of(note, BINNACLE_NOTE_MAX + 1u);
    if (!b || !b->link || !name_ok(name) || !binnacle_place_ok(lat, lon) ||
        tn > BINNACLE_NOTE_MAX || !text_ok(note, tn))
        return ZXM_E_ARG;
    if (!owner_confirmed || b->nwpt >= BINNACLE_WPTS) return ZXM_E_STATE;
    zxm_link_t *l = b->link;
    zxm_begin(l, "place.add");
    zxm_p_str(l, "name", name);
    zxm_p_i64(l, "lat", lat);
    zxm_p_i64(l, "lon", lon);
    if (tn) zxm_p_str(l, "note", note);
    int rc = zxm_call(l);
    if (rc) return rc;
    binnacle_wpt_t *w = &b->wpt[b->nwpt++];
    memcpy(w->name, name, len_of(name, BINNACLE_NAME_MAX) + 1u);
    w->lat = lat;
    w->lon = lon;
    return ZXM_OK;
}

/* ---- BM3: routes ---- */

int binnacle_route_add(binnacle_t *b, uint32_t route, uint32_t wpt)
{
    if (!b || route < 1u || route > BINNACLE_ROUTES || wpt >= b->nwpt) return ZXM_E_ARG;
    uint32_t *n = &b->nroute[route - 1u];
    if (*n >= BINNACLE_ROUTE_WPTS) return ZXM_E_STATE;
    const binnacle_wpt_t *w = &b->wpt[wpt];
    if (!binnacle_node_ok((int64_t) route, (int64_t) *n + 1, w->lat, w->lon)) return ZXM_E_ARG;
    b->route[route - 1u][(*n)++] = wpt;
    return ZXM_OK;
}

int binnacle_mark_hazard(binnacle_t *b, int64_t lo, int64_t hi)
{
    if (!b || binnacle_hazard_margin(0, lo, hi) == BINNACLE_REFUSED) return ZXM_E_ARG;
    if (b->nhz >= BINNACLE_HAZARDS) return ZXM_E_STATE;
    b->hz[b->nhz].lo = lo;
    b->hz[b->nhz].hi = hi;
    b->nhz++;
    return ZXM_OK;
}

/* ---- BM4: a star fix ---- */

/* a latitude in millionths to the nearest whole degree */
static int64_t whole_degree(int64_t udeg)
{
    return binnacle_floor_div(udeg + 500000, 1000000);
}

/* wrap a longitude into -180 .. 180 degrees */
static int64_t wrap_lon(int64_t lon)
{
    if (lon > BINNACLE_LON_MAX) lon -= 2 * BINNACLE_LON_MAX;
    if (lon < -BINNACLE_LON_MAX) lon += 2 * BINNACLE_LON_MAX;
    return lon;
}

static bool sight_ok(const binnacle_sight_t *s, int64_t lat_deg)
{
    return s->body >= 0 && s->body <= ORRERY_PLANETS && s->az >= 0 && s->az <= 359 &&
           s->p >= -BINNACLE_P_SIGHT && s->p <= BINNACLE_P_SIGHT &&
           orrery_above_horizon(lat_deg, s->dec, s->hour);
}

int binnacle_star_fix(binnacle_t *b, int64_t ap_lat, int64_t ap_lon,
                      const binnacle_sight_t sight[2])
{
    if (!b || !sight || !binnacle_place_ok(ap_lat, ap_lon)) return ZXM_E_ARG;
    int64_t lat_deg = whole_degree(ap_lat);
    for (uint32_t i = 0; i < 2u; i++)
        if (!sight_ok(&sight[i], lat_deg)) return ZXM_E_ARG;
    if (!b->clock_known) return ZXM_E_STATE;
    int64_t cs = binnacle_cos_lat(ap_lat);
    if (cs < 1000) return ZXM_E_STATE; /* past 84 degrees: no plane chart */
    int64_t sa = orrery_sin(sight[0].az), ca = orrery_cos(sight[0].az);
    int64_t sb = orrery_sin(sight[1].az), cb = orrery_cos(sight[1].az);
    rat_t east =
        binnacle_lop_fix(sa, ca, BINNACLE_UNIT * sight[0].p, sb, cb, BINNACLE_UNIT * sight[1].p, 0);
    rat_t north =
        binnacle_lop_fix(sa, ca, BINNACLE_UNIT * sight[0].p, sb, cb, BINNACLE_UNIT * sight[1].p, 1);
    if (!east.valid || !north.valid) return ZXM_E_STATE;        /* the lines are parallel */
    rat_t lon_off = rat_mul(east, rat_make(BINNACLE_UNIT, cs)); /* arc to longitude */
    if (!lon_off.valid) return ZXM_E_STATE;
    int64_t lat = ap_lat + binnacle_floor_div(north.num, north.den);
    int64_t lon = wrap_lon(ap_lon + binnacle_floor_div(lon_off.num, lon_off.den));
    if (!binnacle_place_ok(lat, lon)) return ZXM_E_STATE; /* waymark.place_ok */
    int64_t day = binnacle_floor_div(b->now_s, 86400);
    for (uint32_t i = 0; i < 2u; i++)
        b->planet_mdeg[i] =
            sight[i].body ? orrery_planet_longitude(sight[i].body, day) : ORRERY_REFUSED;
    b->star_east = east;
    b->star_north = north;
    b->star_lat = lat;
    b->star_lon = lon;
    b->star_time = b->now_s;
    b->star_known = true;
    b->pos_known = false;
    return ZXM_OK;
}

/* ---- BM5: a clock fix ---- */

int binnacle_clock_fix(binnacle_t *b, int64_t lat, int64_t lon, int64_t since, int64_t course,
                       int64_t speed)
{
    if (!b || !binnacle_place_ok(lat, lon) || course < 0 || course >= BINNACLE_TURN || speed < 0 ||
        speed > BINNACLE_SPEED_MAX || since < 0)
        return ZXM_E_ARG;
    if (!b->clock_known || since > b->now_s) return ZXM_E_STATE;
    int64_t run = binnacle_leg_run(speed, b->now_s - since);
    int64_t cs = binnacle_cos_lat(lat);
    if (run == BINNACLE_REFUSED || cs < 1000) return ZXM_E_STATE;
    int64_t deg = binnacle_mod((course + 50) / 100, 360);
    int64_t dn = binnacle_floor_div(run * orrery_cos(deg), BINNACLE_UNIT);
    int64_t de = binnacle_floor_div(run * orrery_sin(deg), BINNACLE_UNIT);
    int64_t dlat = binnacle_floor_div(dn * 1000000, BINNACLE_M_PER_DEG);
    int64_t dlon = binnacle_floor_div(de * 1000000 * BINNACLE_UNIT, BINNACLE_M_PER_DEG * cs);
    int64_t to_lat = lat + dlat, to_lon = wrap_lon(lon + dlon);
    if (!binnacle_place_ok(to_lat, to_lon)) return ZXM_E_STATE; /* waymark.place_ok */
    b->dr_lat = to_lat;
    b->dr_lon = to_lon;
    b->clock_fix_known = true;
    b->pos_known = false;
    return ZXM_OK;
}

/* ---- BM6: where the boat is ---- */

int binnacle_position(binnacle_t *b, int64_t tol_m)
{
    if (!b || tol_m < 0 || tol_m > BINNACLE_ROOM_MAX) return ZXM_E_ARG;
    if (!b->star_known || !b->clock_fix_known) return ZXM_E_STATE;
    binnacle_box_t box;
    if (!binnacle_position_join(b->star_lat, b->star_lon, b->dr_lat, b->dr_lon, tol_m, &box))
        return ZXM_E_STATE;
    b->pos = box;
    b->pos_known = true;
    return ZXM_OK;
}

int32_t binnacle_position_text(const binnacle_t *b, char *out, uint32_t cap)
{
    uint32_t at = 0;
    if (!b || !out || cap < 2u) return ZXM_E_ARG;
    if (!b->pos_known) return ZXM_E_STATE;
    out[0] = 0;
    bool ok;
    if (b->pos.said == LPRES_STATE_TRUE)
        ok = put(out, cap, &at, "Position ") &&
             put_place(out, cap, &at, b->star_lat, b->star_lon) &&
             put(out, cap, &at, " (the star fix and the clock fix agree). ");
    else
        ok = put(out, cap, &at, "Position unsure: the star fix ") &&
             put_place(out, cap, &at, b->star_lat, b->star_lon) &&
             put(out, cap, &at, " and the clock fix ") &&
             put_place(out, cap, &at, b->dr_lat, b->dr_lon) &&
             put(out, cap, &at, " disagree; the boat is somewhere in the box ") &&
             put_place(out, cap, &at, b->pos.south, b->pos.west) && put(out, cap, &at, " to ") &&
             put_place(out, cap, &at, b->pos.north, b->pos.east) &&
             put(out, cap, &at, ". Take another sight. ");
    if (!ok || !put(out, cap, &at, BINNACLE_ADVISORY)) return ZXM_E_TOOBIG;
    return (int32_t) at;
}

/* ---- BM7: the way on ---- */

/* the plotted point: the star fix, from a TRUE position only */
static bool plotted(const binnacle_t *b)
{
    return b->pos_known && b->pos.said == LPRES_STATE_TRUE;
}

int binnacle_to_waypoints(const binnacle_t *b, int64_t *bearing, int64_t *range, uint32_t cap,
                          uint32_t *n)
{
    if (!b || !bearing || !range || !n || cap < b->nwpt) return ZXM_E_ARG;
    if (!plotted(b)) return ZXM_E_STATE;
    for (uint32_t i = 0; i < b->nwpt; i++) {
        int64_t dn = 0, de = 0;
        if (!binnacle_metres(b->star_lat, b->star_lon, b->wpt[i].lat, b->wpt[i].lon, &dn, &de))
            return ZXM_E_STATE;
        bearing[i] = dn == 0 && de == 0 ? 0 : binnacle_bearing(dn, de); /* here: 0 */
        range[i] = binnacle_range(dn, de);
    }
    *n = b->nwpt;
    return ZXM_OK;
}

int binnacle_heading_check(const binnacle_t *b, int64_t heading, uint32_t from, uint32_t to,
                           int64_t xtk_limit, bool *safe)
{
    if (!b || !safe || heading < 0 || heading >= BINNACLE_TURN || from >= b->nwpt ||
        to >= b->nwpt || from == to || xtk_limit < 0 || xtk_limit > BINNACLE_ROOM_MAX)
        return ZXM_E_ARG;
    if (!plotted(b)) return ZXM_E_STATE;
    /* BR4: the least margin over every marked sector */
    int64_t margin = BINNACLE_TURN - 1;
    for (uint32_t i = 0; i < b->nhz; i++) {
        int64_t m = binnacle_hazard_margin(heading, b->hz[i].lo, b->hz[i].hi);
        if (m < margin) margin = m;
    }
    /* the cross-track: gnomon.triangle_area2 of leg start, leg end and the
     * fix, east and north metres off the leg's start, over the leg */
    int64_t tn = 0, te = 0, fn = 0, fe = 0, area2 = 0, lensq = 0;
    const binnacle_wpt_t *a = &b->wpt[from], *z = &b->wpt[to];
    if (!binnacle_metres(a->lat, a->lon, z->lat, z->lon, &tn, &te) ||
        !binnacle_metres(a->lat, a->lon, b->star_lat, b->star_lon, &fn, &fe) ||
        !gnomon_triangle_area2(0, 0, te, tn, fe, fn, &area2) ||
        !gnomon_side_len_sq(0, 0, te, tn, &lensq))
        return ZXM_E_STATE; /* a leg past Gnomon's 1000 km, or a leg of no length */
    int64_t len = binnacle_root(lensq);
    if (len < 1) return ZXM_E_STATE;
    int64_t xtk = (area2 < 0 ? -area2 : area2) / len;
    *safe = binnacle_heading_safe(heading, margin, xtk_limit - xtk);
    return ZXM_OK;
}

/* ---- BM8: the leg's notice ---- */

int32_t binnacle_leg_notice(const binnacle_t *b, int64_t wind, int64_t temp, int64_t offset,
                            char *out, uint32_t cap)
{
    uint32_t at = 0;
    int64_t num = 0, den = 1;
    if (!b || !out || cap < 2u) return ZXM_E_ARG;
    int32_t flags = weatherglass_warnings(temp, wind);
    if (weatherglass_wind_kmh(wind, &num, &den) || flags < 0) return ZXM_E_ARG;
    if (!b->clock_known) return ZXM_E_STATE;
    int64_t utc = binnacle_floor_div(binnacle_mod(b->now_s, 86400), 60);
    int64_t local = sundial_zone_minute(utc, offset);
    if (local == SUNDIAL_REFUSED) return ZXM_E_ARG;
    out[0] = 0;
    bool ok =
        put(out, cap, &at, "Leg: wind ") &&
        put_fixed(out, cap, &at, weatherglass_floor(num, den), 1) && put(out, cap, &at, " km/h") &&
        (!(flags & 4) || put(out, cap, &at, ", gale warning")) &&
        (!(flags & 1) || put(out, cap, &at, ", frost warning")) &&
        (!(flags & 2) || put(out, cap, &at, ", heat warning")) &&
        put(out, cap, &at, "; local time ") && put_i64(out, cap, &at, local / 60) &&
        put(out, cap, &at, local % 60 < 10 ? ":0" : ":") && put_i64(out, cap, &at, local % 60) &&
        put(out, cap, &at, ". ") && put(out, cap, &at, BINNACLE_ADVISORY);
    if (!ok) return ZXM_E_TOOBIG;
    return (int32_t) at;
}
