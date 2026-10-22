/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* orrery_model.c — Orrery, the sky from where the machine is (see
 * orrery_model.h). */
#include <string.h>

#include "orrery_model.h"

/* J2000 right ascension and declination, millidegrees */
const orrery_star_t ORRERY_STAR[ORRERY_STARS] = {
    {"Sirius", 101287, -16716}, {"Canopus", 95988, -52696}, {"Arcturus", 213915, 19182},
    {"Vega", 279234, 38784},    {"Capella", 79172, 45998},  {"Rigel", 78634, -8202},
    {"Procyon", 114825, 5225},  {"Polaris", 37955, 89264},
};

const char *const ORRERY_PLANET_NAME[ORRERY_PLANETS] = {"Mercury", "Venus",  "Earth",  "Mars",
                                                        "Jupiter", "Saturn", "Uranus", "Neptune"};

void orrery_init(orrery_t *o, zxm_link_t *link)
{
    memset(o, 0, sizeof *o);
    o->link = link;
    o->next_id = 1;
}

/* ---- OM1, OM2: the clock and the place ---------------------------------------- */

int orrery_refresh(orrery_t *o)
{
    if (!o || !o->link) return ZXM_E_ARG;
    zxm_link_t *l = o->link;
    int64_t ms, lat = 0, lon = 0;
    bool known = false;
    zxm_begin(l, "sys.info");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_i64(l, l->result, "time_ms", &ms) != ZXM_OK) return ZXM_E_PROTOCOL;
    if (ms < 0 || ms / 1000 > SOLSTICE_SECONDS_MAX) return ZXM_E_PROTOCOL; /* off the calendar */

    zxm_begin(l, "place.here");
    if ((rc = zxm_call(l)) != ZXM_OK) return rc;
    if (zxm_get_bool(l, l->result, "known", &known) != ZXM_OK) return ZXM_E_PROTOCOL;
    if (known && (zxm_get_i64(l, l->result, "lat", &lat) != ZXM_OK ||
                  zxm_get_i64(l, l->result, "lon", &lon) != ZXM_OK || lat < -90000000 ||
                  lat > 90000000 || lon < -180000000 || lon > 180000000))
        return ZXM_E_PROTOCOL;

    /* both answers checked: only now is the model changed */
    o->now_s = ms / 1000;
    o->day = o->now_s / SOLSTICE_DAY_S; /* almanac.days: floor(seconds / 86400) */
    o->have = true;
    o->place = known;
    o->lat_u6 = known ? lat : 0;
    o->lon_u6 = known ? lon : 0;
    return ZXM_OK;
}

/* ---- OM3, OM4: the sky ------------------------------------------------------- */

int orrery_planet_now(const orrery_t *o, int64_t planet, int64_t *mdeg)
{
    if (!o || !mdeg || planet < 1 || planet > ORRERY_PLANETS) return ZXM_E_ARG;
    if (!o->have) return ZXM_E_STATE;
    int64_t v = orrery_planet_longitude(planet, o->day); /* OR2 */
    if (v < 0) return ZXM_E_STATE;
    *mdeg = v;
    return ZXM_OK;
}

int orrery_sidereal_now(const orrery_t *o, int64_t *mdeg)
{
    if (!o || !mdeg) return ZXM_E_ARG;
    if (!o->have || !o->place) return ZXM_E_STATE;
    int64_t v = orrery_sidereal(o->now_s, o->lon_u6); /* OR5 */
    if (v < 0) return ZXM_E_STATE;
    *mdeg = v;
    return ZXM_OK;
}

int orrery_star_up(const orrery_t *o, uint32_t star, bool *up)
{
    int64_t lst;
    if (!o || !up || star >= ORRERY_STARS) return ZXM_E_ARG;
    int rc = orrery_sidereal_now(o, &lst);
    if (rc != ZXM_OK) return rc;
    const orrery_star_t *s = &ORRERY_STAR[star];
    int64_t hour = orrery_mod(lst - s->ra, ORRERY_TURN_MDEG) / 1000; /* 0..359 */
    int64_t lat = orrery_floor_div(o->lat_u6, 1000000);
    int64_t dec = orrery_floor_div(s->dec, 1000);
    *up = orrery_above_horizon(lat, dec, hour); /* OR3 */
    return ZXM_OK;
}

int orrery_stars_up(const orrery_t *o, uint32_t *count, bool up[ORRERY_STARS])
{
    bool seen[ORRERY_STARS];
    uint32_t n = 0;
    if (!o || !count) return ZXM_E_ARG;
    for (uint32_t i = 0; i < ORRERY_STARS; i++) {
        int rc = orrery_star_up(o, i, &seen[i]);
        if (rc != ZXM_OK) return rc;
        n += seen[i] ? 1u : 0u;
    }
    *count = n;
    if (up) memcpy(up, seen, sizeof seen);
    return ZXM_OK;
}

/* ---- OM5: observations ------------------------------------------------------- */

bool orrery_observation_valid(int64_t body, int64_t time, int64_t alt, int64_t az)
{
    bool planet = body >= 1 && body <= ORRERY_PLANETS;
    bool star = body > ORRERY_STAR_BASE && body <= ORRERY_STAR_BASE + (int64_t) ORRERY_STARS;
    return (planet || star) && time >= 0 && time <= SOLSTICE_SECONDS_MAX && alt >= -90000 &&
           alt <= 90000 && az >= 0 && az < ORRERY_TURN_MDEG;
}

const orrery_obs_t *orrery_find(const orrery_t *o, int64_t id)
{
    if (!o) return NULL;
    for (uint32_t i = 0; i < o->nobs; i++)
        if (o->obs[i].id == id) return &o->obs[i];
    return NULL;
}

int orrery_observe(orrery_t *o, int64_t body, int64_t alt, int64_t az, int64_t *id)
{
    if (!o || !id) return ZXM_E_ARG;
    if (!o->have) return ZXM_E_STATE;
    if (!orrery_observation_valid(body, o->now_s, alt, az) ||
        !orrery_record_fits(o->next_id, body, o->now_s, alt, az))
        return ZXM_E_ARG;
    if (o->nobs >= ORRERY_OBS_MAX || o->next_id > ORRERY_OBS_ID) return ZXM_E_STATE;
    orrery_obs_t *r = &o->obs[o->nobs];
    r->id = o->next_id;
    r->body = body;
    r->time = o->now_s;
    r->alt = alt;
    r->az = az;
    o->nobs++;
    o->next_id++;
    *id = r->id;
    return ZXM_OK;
}

int orrery_forget(orrery_t *o, int64_t id)
{
    if (!o) return ZXM_E_ARG;
    for (uint32_t i = 0; i < o->nobs; i++)
        if (o->obs[i].id == id) {
            for (uint32_t k = i + 1u; k < o->nobs; k++) o->obs[k - 1u] = o->obs[k];
            o->nobs--;
            return ZXM_OK;
        }
    return ZXM_E_ARG;
}
