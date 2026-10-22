/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* damper_model.c — Damper's model (damper_model.h DM1-DM9), composed of
 * Wake, Brazier and Watchfire's models and Homestead's and Conch's rules,
 * over zxvd's log.tail, journal.query, sys.info, power.status, phone.list,
 * call.start, place.ask, place.here (through its parts) and call.hangup. */
#include "damper_model.h"

#include <stdio.h>
#include <string.h>

static void say(damper_t *d, const char *text)
{
    snprintf(d->notice, sizeof d->notice, "%s", text);
}

void damper_init(damper_t *d, zxm_link_t *link)
{
    memset(d, 0, sizeof *d);
    d->link = link;
    d->flash = CONCH_PULSES_MAX; /* default taken: three flashes and three pulses */
    d->vibrate = CONCH_PULSES_MAX;
    d->fire = LPRES_STATE_NEITHER;
    brazier_init(&d->power, link);
    wake_init(&d->wake, link);
    watchfire_init(&d->contacts, link);
}

/* ----- DM1, DM2, DM6: the building, its sensors and heat sources ----- */

static int32_t find_room(const damper_t *d, int64_t room)
{
    for (uint32_t i = 0; i < d->nroom; i++)
        if (d->room[i].room == room) return (int32_t) i;
    return -1;
}

int32_t damper_find_sensor(const damper_t *d, int64_t id)
{
    if (!d) return -1;
    for (uint32_t i = 0; i < d->nsensor; i++)
        if (d->sensor[i].id == id) return (int32_t) i;
    return -1;
}

static int32_t find_source(const damper_t *d, int64_t device)
{
    for (uint32_t i = 0; i < d->nsource; i++)
        if (d->source[i].device == device) return (int32_t) i;
    return -1;
}

static int not_confirmed(damper_t *d)
{
    say(d, "Damper's settings change only when you confirm them.");
    return ZXM_E_STATE;
}

int damper_add_room(damper_t *d, int64_t floor, int64_t room, int64_t toward, int64_t exits,
                    bool owner_confirmed)
{
    if (!d || !damper_room_ok(floor, room, toward, exits) ||
        !damper_in(floor, DAMPER_FLOOR_MIN, DAMPER_FLOOR_MAX) ||
        !damper_in(room, 1, DAMPER_ROOM_MAX) || !damper_in(exits, 0, DAMPER_EXITS_MAX))
        return ZXM_E_ARG;
    /* DM1: toward outside needs a door out; otherwise a room kept before */
    if (toward == 0 ? exits < 1 : find_room(d, toward) < 0) return ZXM_E_ARG;
    if (find_room(d, room) >= 0) return ZXM_E_ARG;
    if (d->nroom >= DAMPER_ROOMS) return ZXM_E_STATE;
    if (!owner_confirmed) return not_confirmed(d);
    damper_room_t *r = &d->room[d->nroom++];
    r->floor = floor;
    r->room = room;
    r->toward = toward;
    r->exits = exits;
    return ZXM_OK;
}

int damper_add_sensor(damper_t *d, int64_t sensor, int64_t room, int64_t limit, int64_t rate,
                      bool owner_confirmed)
{
    if (!d || !damper_in(sensor, 1, DAMPER_SENSOR_MAX) || !damper_limits_ok(limit, rate) ||
        find_room(d, room) < 0 || damper_find_sensor(d, sensor) >= 0)
        return ZXM_E_ARG;
    if (d->nsensor >= DAMPER_SENSORS) return ZXM_E_STATE;
    if (!owner_confirmed) return not_confirmed(d);
    damper_sensor_t *s = &d->sensor[d->nsensor++];
    memset(s, 0, sizeof *s);
    s->id = sensor;
    s->room = room;
    s->limit = limit;
    s->rate = rate;
    s->fire = LPRES_STATE_NEITHER;
    s->quiet = true;
    return ZXM_OK;
}

int damper_add_source(damper_t *d, int64_t device, int64_t kind, int64_t lo, int64_t hi,
                      int64_t setpoint, int64_t room, bool owner_confirmed)
{
    if (!d || !homestead_device_ok(device, kind, lo, hi, setpoint) || lo > hi ||
        (kind != HOMESTEAD_PLUG && kind != HOMESTEAD_HEATER && kind != HOMESTEAD_OVEN) ||
        find_room(d, room) < 0 || find_source(d, device) >= 0)
        return ZXM_E_ARG;
    if (d->nsource >= DAMPER_SOURCES) return ZXM_E_STATE;
    if (!owner_confirmed) return not_confirmed(d);
    damper_source_t *s = &d->source[d->nsource++];
    s->device = device;
    s->kind = kind;
    s->room = room;
    return ZXM_OK;
}

int damper_set_alert(damper_t *d, int64_t flash, int64_t vibrate, bool owner_confirmed)
{
    if (!d || !conch_alert_map_ok(CONCH_SOUND_ALARM, flash, vibrate)) return ZXM_E_ARG;
    if (!owner_confirmed) return not_confirmed(d);
    d->flash = flash;
    d->vibrate = vibrate;
    return ZXM_OK;
}

int damper_set_tone(damper_t *d, int64_t gain, int64_t total, int64_t cap, bool owner_confirmed)
{
    if (!d || !conch_level_ok(gain, total, cap)) return ZXM_E_ARG;
    if (!owner_confirmed) return not_confirmed(d);
    d->has_tone = true;
    d->gain = gain;
    d->total = total;
    d->cap = cap;
    return ZXM_OK;
}

/* ----- DM3, DM6: readings ----- */

int damper_reading(damper_t *d, int64_t sensor, int64_t stamp, int64_t temp, int64_t smoke)
{
    if (!d) return ZXM_E_ARG;
    int32_t k = damper_find_sensor(d, sensor);
    if (k < 0 || !damper_in(stamp, 0, DAMPER_STAMP_MAX) ||
        !damper_in(temp, DAMPER_TEMP_MIN, DAMPER_TEMP_MAX) ||
        (smoke != 0 && smoke != 1 && smoke != DAMPER_SMOKE_NONE))
        return ZXM_E_ARG;
    damper_sensor_t *s = &d->sensor[k];
    if (s->n && stamp < s->stamp[s->n - 1u]) return ZXM_E_ARG; /* never before the last */
    if (s->n == DAMPER_WINDOW) { /* the window slides: the oldest leaves */
        memmove(s->temp, s->temp + 1, sizeof s->temp[0] * (DAMPER_WINDOW - 1u));
        memmove(s->stamp, s->stamp + 1, sizeof s->stamp[0] * (DAMPER_WINDOW - 1u));
        memmove(s->smoke, s->smoke + 1, sizeof s->smoke[0] * (DAMPER_WINDOW - 1u));
        s->n--;
    }
    s->temp[s->n] = temp;
    s->stamp[s->n] = stamp;
    s->smoke[s->n] = smoke;
    s->n++;
    return ZXM_OK;
}

int damper_energy(damper_t *d, int64_t device, int64_t wh)
{
    if (!d || find_source(d, device) < 0 || !damper_in(wh, 0, DAMPER_WH_MAX)) return ZXM_E_ARG;
    if (d->nen >= DAMPER_ENERGY) return ZXM_E_STATE; /* full: refused, never overwritten */
    d->en_device[d->nen] = device;
    d->en_wh[d->nen] = wh;
    d->nen++;
    return ZXM_OK;
}

int damper_source_energy(const damper_t *d, int64_t device, int64_t *wh)
{
    if (!d || !wh || find_source(d, device) < 0) return ZXM_E_ARG;
    if (homestead_energy(d->en_device, d->en_wh, d->nen, device, wh) != 0) return ZXM_E_ARG;
    return ZXM_OK;
}

/* ----- DM4: the alarm ----- */

/* DR6 over the sensor's latest second; DR1's inputs and its quiet */
static void judge(damper_sensor_t *s)
{
    s->fire = LPRES_STATE_NEITHER;
    s->margin = 0;
    s->has_smoke = 0;
    s->split = 0;
    s->quiet = true;
    s->rising = false;
    if (!s->n) return;
    int64_t last = s->stamp[s->n - 1u], hottest = DAMPER_TEMP_MIN;
    lpres_state_t heat = LPRES_STATE_NEITHER, smoke = LPRES_STATE_NEITHER;
    for (uint32_t i = 0; i < s->n; i++) {
        if (s->stamp[i] != last) continue;
        bool over = s->temp[i] > s->limit;
        if (s->temp[i] > hottest) hottest = s->temp[i];
        heat = lpres_kjoin(heat, damper_said(over));
        smoke = lpres_kjoin(smoke, damper_smoke_said(s->smoke[i]));
        s->fire = lpres_kjoin(s->fire, damper_said(over || s->smoke[i] == 1));
        if (s->smoke[i] == 1) s->has_smoke = 1;
    }
    s->margin = s->limit - hottest;
    s->split = (heat == LPRES_STATE_BOTH || smoke == LPRES_STATE_BOTH) ? 1 : 0;
    s->quiet = damper_quiet(s->margin, s->has_smoke, s->split);
    rat_t ex = rat_zero();
    s->rising = damper_rise_excess(s->temp, s->stamp, s->n, s->rate, &ex) && ex.num > 0;
}

/* append text to the notice; false once it no longer fits */
static bool add(damper_t *d, uint32_t *used, const char *text)
{
    size_t n = strlen(text);
    if (*used + n > DAMPER_NOTICE_MAX) return false;
    memcpy(d->notice + *used, text, n);
    *used += (uint32_t) n;
    d->notice[*used] = 0;
    return true;
}

static const char *why(const damper_sensor_t *s)
{
    if (s->split) return "two reports of one second disagree (held as BOTH)";
    if (s->has_smoke) return "smoke reported";
    return "over its temperature limit";
}

static bool room_raised(const damper_t *d, int64_t room)
{
    for (uint32_t i = 0; i < d->nsensor; i++)
        if (d->sensor[i].room == room && d->sensor[i].n && !d->sensor[i].quiet) return true;
    return false;
}

int damper_way_out(const damper_t *d, int64_t room, int64_t *path, uint32_t cap, uint32_t *n,
                   uint32_t *through)
{
    if (!d || !path || !n || !through) return ZXM_E_ARG;
    *n = 0;
    *through = 0;
    int32_t at = find_room(d, room);
    if (at < 0) return ZXM_E_ARG;
    /* DM1: each step goes to a room kept earlier, so at most nroom steps */
    for (uint32_t step = 0; at >= 0 && step < d->nroom; step++) {
        if (*n >= cap) return ZXM_E_TOOBIG;
        path[(*n)++] = d->room[at].room;
        if (room_raised(d, d->room[at].room)) (*through)++;
        if (d->room[at].toward == 0) return ZXM_OK;
        at = find_room(d, d->room[at].toward);
    }
    return ZXM_E_STATE; /* a tree that does not end outside: never kept (DM1) */
}

int damper_assess(damper_t *d)
{
    if (!d) return ZXM_E_ARG;
    int64_t rooms[DAMPER_SENSORS], raised[DAMPER_SENSORS];
    d->fire = LPRES_STATE_NEITHER;
    d->raised = false;
    d->raised_n = d->rising_n = 0;
    for (uint32_t i = 0; i < d->nsensor; i++) {
        damper_sensor_t *s = &d->sensor[i];
        judge(s);
        d->fire = i ? lpres_disjoin(d->fire, s->fire) : s->fire;
        rooms[i] = s->room;
        raised[i] = s->n && !s->quiet ? 1 : 0;
        d->raised_n += (uint32_t) raised[i];
        d->rising_n += s->rising ? 1u : 0u;
    }
    for (uint32_t i = 0; i < d->nsensor; i++)
        d->sensor[i].room_raised = damper_room_cell(rooms, raised, d->nsensor, i);
    d->raised = d->raised_n > 0;

    uint32_t used = 0;
    char line[192];
    d->notice[0] = 0;
    if (!d->raised) {
        snprintf(line, sizeof line, "No alarm: %u sensors quiet", d->nsensor);
        bool ok = add(d, &used, line);
        if (ok && d->rising_n) {
            snprintf(line, sizeof line, "; %u rising faster than their declared rate (advice)",
                     d->rising_n);
            ok = add(d, &used, line);
        }
        if (ok) ok = add(d, &used, ". ");
        if (ok) add(d, &used, DAMPER_NOT_A_SMOKE_ALARM);
        return ZXM_OK;
    }
    snprintf(line, sizeof line, "Fire alarm (advice): %u of %u sensors raised.", d->raised_n,
             d->nsensor);
    bool ok = add(d, &used, line);
    for (uint32_t i = 0; ok && i < d->nsensor; i++) {
        const damper_sensor_t *s = &d->sensor[i];
        if (!s->n || s->quiet) continue;
        const damper_room_t *r = &d->room[find_room(d, s->room)];
        snprintf(line, sizeof line, " Sensor %lld, room %lld, floor %lld: %s.", (long long) s->id,
                 (long long) r->room, (long long) r->floor, why(s));
        ok = add(d, &used, line);
        int64_t path[DAMPER_ROOMS];
        uint32_t n = 0, through = 0;
        if (ok && damper_way_out(d, s->room, path, DAMPER_ROOMS, &n, &through) == ZXM_OK) {
            ok = add(d, &used, " Way out:");
            for (uint32_t k = 0; ok && k < n; k++) {
                snprintf(line, sizeof line, " room %lld,", (long long) path[k]);
                ok = add(d, &used, line);
            }
            if (ok) ok = add(d, &used, " outside.");
            if (ok && through > 1u) ok = add(d, &used, " It passes another raised room.");
        }
        for (uint32_t k = 0; ok && k < d->nsource; k++) {
            int64_t wh = 0;
            if (d->source[k].room != s->room ||
                damper_source_energy(d, d->source[k].device, &wh) != ZXM_OK)
                continue;
            snprintf(line, sizeof line,
                     " Device %lld here used %lld Wh; switch it off yourself "
                     "if that is safe.",
                     (long long) d->source[k].device, (long long) wh);
            ok = add(d, &used, line);
        }
    }
    if (ok) {
        snprintf(line, sizeof line, " Shown as %lld flashes and %lld vibrations, 334 ms apart. ",
                 (long long) d->flash, (long long) d->vibrate);
        ok = add(d, &used, line);
    }
    if (ok) add(d, &used, DAMPER_NOT_A_SMOKE_ALARM);
    return ZXM_OK;
}

/* ----- DM7: power and warnings ----- */

int damper_refresh_power(damper_t *d)
{
    if (!d || !d->link) return ZXM_E_ARG;
    return brazier_refresh(&d->power);
}

int damper_follow(damper_t *d, uint32_t *added)
{
    if (!d || !d->link || !added) return ZXM_E_ARG;
    int rc = wake_follow(&d->wake, added);
    if (rc != ZXM_OK) return rc;
    rc = wake_filter(&d->wake, WAKE_WARN, "");
    if (rc != ZXM_OK) return rc;
    d->warnings = wake_count(&d->wake);
    return ZXM_OK;
}

int damper_history(damper_t *d, uint64_t since_ms)
{
    if (!d || !d->link) return ZXM_E_ARG;
    int rc = wake_filter(&d->wake, WAKE_WARN, "");
    if (rc != ZXM_OK) return rc;
    return wake_history(&d->wake, since_ms, 0);
}

/* ----- DM8, DM9: contacts ----- */

int damper_add_contact(damper_t *d, const char *name, int64_t peer, const char *phone_id,
                       int64_t stamp, bool owner_confirmed)
{
    if (!d || !name || !name[0] || peer < 1 || peer > WATCHFIRE_PEER_MAX || stamp < 0 ||
        stamp > WATCHFIRE_STAMP_MAX)
        return ZXM_E_ARG;
    if (!owner_confirmed) return not_confirmed(d);
    return watchfire_add_contact(&d->contacts, name, peer, phone_id, stamp);
}

int damper_set_window(damper_t *d, int64_t secs, bool owner_confirmed)
{
    if (!d || !watchfire_window_ok(secs)) return ZXM_E_ARG;
    if (!owner_confirmed) return not_confirmed(d);
    return watchfire_set_window(&d->contacts, secs);
}

int damper_set_share(damper_t *d, bool on, bool owner_confirmed)
{
    if (!d) return ZXM_E_ARG;
    int rc = watchfire_set_optin(&d->contacts, on, owner_confirmed);
    if (rc == ZXM_E_STATE) return not_confirmed(d);
    return rc;
}

int damper_refresh_contacts(damper_t *d)
{
    if (!d || !d->link) return ZXM_E_ARG;
    int rc = watchfire_refresh_devices(&d->contacts);
    if (rc != ZXM_OK) return rc;
    return watchfire_refresh_place(&d->contacts);
}

int damper_call_contacts(damper_t *d, int64_t now_s, int64_t owner, int64_t approved)
{
    if (!d) return ZXM_E_ARG;
    int rc = watchfire_start(&d->contacts, now_s, owner, approved);
    say(d, d->contacts.notice);
    return rc;
}

int64_t damper_call_left(const damper_t *d, int64_t now_s)
{
    if (!d) return WATCHFIRE_REFUSED;
    return watchfire_left(&d->contacts, now_s);
}

int damper_cancel_call(damper_t *d)
{
    if (!d) return ZXM_E_ARG;
    int rc = watchfire_cancel(&d->contacts);
    say(d, d->contacts.notice);
    return rc;
}

int damper_ring(damper_t *d, int64_t now_s)
{
    if (!d) return ZXM_E_ARG;
    int rc = watchfire_ring(&d->contacts, now_s);
    say(d, d->contacts.notice);
    return rc;
}

int damper_share_place(damper_t *d, uint32_t *n)
{
    if (!d) return ZXM_E_ARG;
    int rc = watchfire_share_place(&d->contacts, n);
    say(d, d->contacts.notice);
    return rc;
}

int damper_stand_down(damper_t *d, bool owner_confirmed)
{
    if (!d || !d->link) return ZXM_E_ARG;
    watchfire_t *w = &d->contacts;
    if (w->state != WATCHFIRE_RINGING && w->state != WATCHFIRE_CANCELLED) return ZXM_E_STATE;
    if (!owner_confirmed) {
        say(d, "The calls end only when you confirm it.");
        return ZXM_E_STATE;
    }
    zxm_link_t *l = d->link;
    int last = ZXM_OK;
    for (uint32_t i = 0; i < w->ncontacts; i++) {
        if (w->contact[i].line < 0) continue;
        zxm_begin(l, "call.hangup");
        zxm_p_u64(l, "line", (uint64_t) w->contact[i].line);
        int rc = zxm_call(l);
        if (rc != ZXM_OK) {
            last = rc; /* the line stays named, so a second stand-down tries it again */
            continue;
        }
        w->contact[i].line = -1;
    }
    if (last != ZXM_OK) {
        say(d, "A call could not be ended; try again.");
        return last;
    }
    int rc = watchfire_finish(w);
    if (rc == ZXM_OK) say(d, "Stood down: every call Damper's contacts were rung on has ended.");
    return rc;
}
