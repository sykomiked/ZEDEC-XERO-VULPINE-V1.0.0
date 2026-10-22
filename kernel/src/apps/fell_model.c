/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fell_model.c — Fell's model (fell_model.h FM1-FM9), composed of
 * Waymark's, Aerie's and Commons's models and Watchfire's rules, over
 * zxvd's place.here, place.list, place.add, ehop.bands, ehop.seal,
 * ehop.open, harmonic.encode, harmonic.decode, freight.pack, chat.post,
 * chat.feed, pspace.post (through its parts) and files.read and
 * files.write. */
#include "fell_model.h"

#include <stdio.h>
#include <string.h>

static void say(fell_t *f, const char *text)
{
    snprintf(f->notice, sizeof f->notice, "%s", text);
}

static int not_confirmed(fell_t *f)
{
    say(f, "Fell changes this only when you confirm it.");
    return ZXM_E_STATE;
}

/* append text to buf (cap bytes with its 0); false once it no longer fits */
static bool add(char *buf, uint32_t cap, uint32_t *used, const char *text)
{
    size_t n = strlen(text);
    if (*used + n + 1u > cap) return false;
    memcpy(buf + *used, text, n);
    *used += (uint32_t) n;
    buf[*used] = 0;
    return true;
}

void fell_init(fell_t *f, zxm_link_t *link)
{
    memset(f, 0, sizeof *f);
    f->link = link;
    f->lead = LPRES_STATE_NEITHER;
    waymark_init(&f->map, link);
    aerie_init(&f->radio, link);
    commons_init(&f->room, link);
}

/* ----- FM1, FM2, FM3: the incident, its grid and sectors ----- */

int fell_open_incident(fell_t *f, int64_t incident, bool owner_confirmed)
{
    if (!f || !fell_in(incident, 1, FELL_INCIDENT_MAX)) return ZXM_E_ARG;
    if (f->open) return ZXM_E_STATE;
    if (!owner_confirmed) return not_confirmed(f);
    f->open = true;
    f->incident = incident;
    return ZXM_OK;
}

int fell_set_grid(fell_t *f, const char *corner, int64_t cell, int64_t cols, bool owner_confirmed)
{
    if (!f || !corner || !fell_in(cell, FELL_CELL_MIN, FELL_CELL_MAX) ||
        !fell_in(cols, 1, FELL_COLS_MAX))
        return ZXM_E_ARG;
    const waymark_place_t *p = waymark_find(&f->map, corner);
    if (!p) return ZXM_E_ARG; /* only a place Waymark keeps */
    if (!f->open) return ZXM_E_STATE;
    if (!owner_confirmed) return not_confirmed(f);
    f->lat0 = p->lat;
    f->lon0 = p->lon;
    f->cell = cell;
    f->cols = cols;
    f->grid_set = true;
    return ZXM_OK;
}

int fell_sector_at(const fell_t *f, int32_t lat, int32_t lon, int64_t *sector)
{
    if (!f || !sector) return ZXM_E_ARG;
    if (!f->grid_set) return ZXM_E_STATE;
    int64_t s =
        fell_grid_sector((int64_t) lon - f->lon0, (int64_t) lat - f->lat0, f->cell, f->cols);
    if (s == FELL_REFUSED || s > FELL_SECTOR_ID_MAX) return ZXM_E_ARG;
    *sector = s;
    return ZXM_OK;
}

int32_t fell_find_sector(const fell_t *f, int64_t sector)
{
    if (!f) return -1;
    for (uint32_t i = 0; i < f->nsector; i++)
        if (f->sector[i].id == sector) return (int32_t) i;
    return -1;
}

int fell_add_sector(fell_t *f, int64_t sector, int64_t poa, bool owner_confirmed)
{
    if (!f || !fell_in(sector, 1, FELL_SECTOR_ID_MAX) || !fell_in(poa, 0, FELL_X100) ||
        fell_find_sector(f, sector) >= 0)
        return ZXM_E_ARG;
    if (!f->open || f->nsector >= FELL_SECTOR_MAX) return ZXM_E_STATE;
    if (!owner_confirmed) return not_confirmed(f);
    fell_sector_t *s = &f->sector[f->nsector++];
    memset(s, 0, sizeof *s);
    s->id = sector;
    s->poa = poa;
    s->seen = LPRES_STATE_NEITHER;
    f->ranked = false;
    return ZXM_OK;
}

int fell_set_coverage(fell_t *f, int64_t sector, int64_t cov, bool owner_confirmed)
{
    if (!f || !fell_in(cov, 0, FELL_X100)) return ZXM_E_ARG;
    int32_t k = fell_find_sector(f, sector);
    if (k < 0) return ZXM_E_ARG;
    if (!owner_confirmed) return not_confirmed(f);
    f->sector[k].cov = cov;
    f->ranked = false;
    return ZXM_OK;
}

int fell_priorities(fell_t *f)
{
    int64_t poa[FELL_SECTOR_MAX], cov[FELL_SECTOR_MAX], pr[FELL_SECTOR_MAX];
    if (!f) return ZXM_E_ARG;
    if (!f->nsector) return ZXM_E_STATE;
    for (uint32_t i = 0; i < f->nsector; i++) poa[i] = f->sector[i].poa, cov[i] = f->sector[i].cov;
    if (!fell_sector_priority(poa, cov, f->nsector, pr)) {
        say(f, "No sector is left that is likely: every one is searched through or has no "
               "probability. The commander decides what next.");
        return ZXM_E_STATE; /* the last priorities stay */
    }
    uint32_t best = 0;
    for (uint32_t i = 0; i < f->nsector; i++) {
        f->sector[i].priority = pr[i];
        if (pr[i] > pr[best]) best = i;
    }
    f->ranked = true;
    char line[160];
    snprintf(line, sizeof line,
             "Suggested next: sector %lld, %lld.%02lld%% of what is left. The commander decides.",
             (long long) f->sector[best].id, (long long) (pr[best] / 100),
             (long long) (pr[best] % 100));
    say(f, line);
    return ZXM_OK;
}

uint32_t fell_unsearched_count(const fell_t *f, int64_t bar)
{
    uint32_t n = 0;
    for (uint32_t i = 0; f && i < f->nsector; i++) n += fell_unsearched(f->sector[i].cov, bar);
    return n;
}

/* ----- FM4: sightings and leads ----- */

int fell_sighting(fell_t *f, int64_t sector, int64_t stamp)
{
    if (!f || fell_find_sector(f, sector) < 0 || !fell_in(stamp, 0, FELL_STAMP_MAX / 1000))
        return ZXM_E_ARG;
    if (f->nsight >= FELL_SIGHTINGS) return ZXM_E_STATE; /* full: refused, never overwritten */
    f->sight[f->nsight].sector = sector;
    f->sight[f->nsight].stamp = stamp;
    f->nsight++;
    return ZXM_OK;
}

int fell_sighting_at(fell_t *f, const char *place, int64_t stamp)
{
    int64_t sector = 0;
    if (!f || !place) return ZXM_E_ARG;
    const waymark_place_t *p = waymark_find(&f->map, place);
    if (!p) return ZXM_E_ARG;
    int rc = fell_sector_at(f, p->lat, p->lon, &sector);
    if (rc != ZXM_OK) return rc;
    return fell_sighting(f, sector, stamp);
}

int fell_leads(fell_t *f, int64_t now)
{
    int64_t stamps[FELL_SIGHTINGS], seen[FELL_SIGHTINGS];
    uint32_t n = 0;
    if (!f || !fell_in(now, 0, FELL_STAMP_MAX / 1000)) return ZXM_E_ARG;
    for (uint32_t i = 0; i < f->nsight; i++) stamps[i] = f->sight[i].stamp;
    int64_t from = now - FELL_SIGHT_WINDOW;
    f->recent = (uint32_t) fell_sightings_between(stamps, f->nsight, from, now); /* FR5 */
    for (uint32_t i = 0; i < f->nsight; i++)
        if (fell_in(f->sight[i].stamp, from, now)) seen[n++] = f->sight[i].sector;
    f->lead = LPRES_STATE_NEITHER;
    f->nlead = 0;
    for (uint32_t i = 0; i < f->nsector; i++) {
        fell_sector_t *s = &f->sector[i];
        s->seen = n ? fell_sector_state(seen, n, s->id) : LPRES_STATE_NEITHER;
        f->lead = i ? lpres_disjoin(f->lead, s->seen) : s->seen;
        f->nlead += fell_is_lead(s->seen) ? 1u : 0u;
    }
    uint32_t used = 0;
    char line[96];
    f->notice[0] = 0;
    if (!f->nlead) {
        add(f->notice, sizeof f->notice, &used, "No sighting in the last 15 minutes.");
        return ZXM_OK;
    }
    snprintf(line, sizeof line, "%u sightings in the last 15 minutes%s; leads:", f->recent,
             f->lead == LPRES_STATE_BOTH ? " disagree (held as BOTH, none dropped)" : "");
    bool ok = add(f->notice, sizeof f->notice, &used, line);
    for (uint32_t i = 0; ok && i < f->nsector; i++) {
        if (!fell_is_lead(f->sector[i].seen)) continue;
        snprintf(line, sizeof line, " sector %lld", (long long) f->sector[i].id);
        ok = add(f->notice, sizeof f->notice, &used, line);
    }
    if (ok) add(f->notice, sizeof f->notice, &used, ". The commander decides.");
    return ZXM_OK;
}

/* ----- FM5: crews ----- */

static int32_t find_crew(const fell_t *f, int64_t crew)
{
    for (uint32_t i = 0; i < f->ncrew; i++)
        if (f->crew[i].id == crew) return (int32_t) i;
    return -1;
}

int fell_add_crew(fell_t *f, int64_t crew, int64_t start, bool owner_confirmed)
{
    if (!f || !fell_in(crew, 1, FELL_CREW_MAX) || start < 0 || find_crew(f, crew) >= 0)
        return ZXM_E_ARG;
    if (!f->open || f->ncrew >= FELL_CREWS) return ZXM_E_STATE;
    if (!owner_confirmed) return not_confirmed(f);
    fell_crew_t *c = &f->crew[f->ncrew++];
    c->id = crew;
    c->start = start;
    c->last = start; /* the briefing is the first check-in */
    c->overdue = false;
    return ZXM_OK;
}

int fell_check_in(fell_t *f, int64_t crew, int64_t now)
{
    if (!f) return ZXM_E_ARG;
    int32_t k = find_crew(f, crew);
    if (k < 0 || now < f->crew[k].last) return ZXM_E_ARG; /* never before its last */
    f->crew[k].last = now;
    return ZXM_OK;
}

int fell_crew_watch(fell_t *f, int64_t now)
{
    if (!f || now < 0) return ZXM_E_ARG;
    uint32_t used = 0;
    char line[160];
    f->overdue = 0;
    f->notice[0] = 0;
    bool ok = true;
    for (uint32_t i = 0; i < f->ncrew; i++) {
        fell_crew_t *c = &f->crew[i];
        c->overdue = !fell_crew_safe(now - c->start, now - c->last);
        if (!c->overdue) continue;
        f->overdue++;
        snprintf(line, sizeof line,
                 "Crew %lld overdue: %lld min on shift, last check-in %lld min ago. ",
                 (long long) c->id, (long long) (now - c->start), (long long) (now - c->last));
        if (ok) ok = add(f->notice, sizeof f->notice, &used, line);
    }
    if (ok && f->overdue) add(f->notice, sizeof f->notice, &used, FELL_NO_SERVICES);
    return ZXM_OK;
}

/* ----- FM6: callout claims ----- */

int fell_add_claim(fell_t *f, int64_t crew, int64_t hours, int64_t rate, int64_t miles,
                   int64_t permile, bool owner_confirmed)
{
    if (!f || find_crew(f, crew) < 0) return ZXM_E_ARG;
    int64_t claim = fell_callout_claim(hours, rate, miles, permile);
    if (claim == FELL_REFUSED) return ZXM_E_ARG;
    if (f->nclaim >= FELL_CLAIMS || f->claims_total > FELL_TOTAL_MAX - claim) return ZXM_E_STATE;
    if (!owner_confirmed) return not_confirmed(f);
    fell_claim_t *c = &f->claim[f->nclaim++];
    c->crew = crew;
    c->hours = hours;
    c->rate = rate;
    c->miles = miles;
    c->permile = permile;
    c->claim = claim;
    f->claims_total += claim;
    return ZXM_OK;
}

/* ----- FM7: casualties ----- */

/* a note as typed: no control characters, so it stays one line of the file */
static bool note_ok(const char *s)
{
    size_t n = strlen(s);
    if (n > FELL_NOTE_MAX) return false;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char) s[i] < 0x20u || s[i] == 0x7f) return false;
    return true;
}

static int32_t find_casualty(const fell_t *f, int64_t casualty)
{
    for (uint32_t i = 0; i < f->ncas; i++)
        if (f->cas[i].casualty == casualty) return (int32_t) i;
    return -1;
}

/* FR7 and WF4; the sector kept only when `kept` asks for it */
static bool casualty_ok(const fell_t *f, int64_t incident, int64_t casualty, int64_t sector,
                        int64_t triage, int64_t stamp, int64_t field, const char *note, bool kept)
{
    if (!note || !fell_casualty_fits(incident, casualty, sector, triage, stamp) ||
        !fell_in(casualty, 1, FELL_CASUALTY_MAX) || !fell_in(sector, 1, FELL_SECTOR_ID_MAX) ||
        !fell_in(triage, 0, FELL_TRIAGE_MAX) || !fell_in(stamp, 0, FELL_STAMP_MAX) ||
        !note_ok(note) || find_casualty(f, casualty) >= 0)
        return false;
    if (kept && fell_find_sector(f, sector) < 0) return false;
    if (field == 0) return !note[0];
    return fell_in(field, 1, WATCHFIRE_MED_FIELDS) && note[0] &&
           watchfire_medical_fits(field, (int64_t) strlen(note), stamp);
}

static void put_casualty(fell_t *f, int64_t casualty, int64_t sector, int64_t triage, int64_t stamp,
                         int64_t field, const char *note)
{
    fell_casualty_t *c = &f->cas[f->ncas++];
    c->casualty = casualty;
    c->sector = sector;
    c->triage = triage;
    c->stamp = stamp;
    c->field = field;
    snprintf(c->note, sizeof c->note, "%s", note);
}

int fell_add_casualty(fell_t *f, int64_t casualty, int64_t sector, int64_t triage, int64_t stamp,
                      int64_t field, const char *note, bool owner_confirmed)
{
    if (!f || !casualty_ok(f, f->incident, casualty, sector, triage, stamp, field, note, true))
        return ZXM_E_ARG;
    if (!f->open || f->ncas >= FELL_CASUALTIES) return ZXM_E_STATE;
    if (!owner_confirmed) return not_confirmed(f);
    put_casualty(f, casualty, sector, triage, stamp, field, note);
    return ZXM_OK;
}

int fell_save(fell_t *f, bool owner_confirmed)
{
    if (!f || !f->link) return ZXM_E_ARG;
    if (!f->open) return ZXM_E_STATE;
    if (!owner_confirmed) return not_confirmed(f);
    char line[FELL_NOTE_MAX + 96];
    uint32_t used = 0;
    f->file[0] = 0;
    snprintf(line, sizeof line, "fell 1\nincident %lld\n", (long long) f->incident);
    bool ok = add(f->file, sizeof f->file, &used, line);
    for (uint32_t i = 0; ok && i < f->ncas; i++) {
        const fell_casualty_t *c = &f->cas[i];
        snprintf(line, sizeof line, "cas %lld %lld %lld %lld %lld%s%s\n", (long long) c->casualty,
                 (long long) c->sector, (long long) c->triage, (long long) c->stamp,
                 (long long) c->field, c->field ? " " : "", c->note);
        ok = add(f->file, sizeof f->file, &used, line);
    }
    if (!ok) return ZXM_E_TOOBIG;
    zxm_begin(f->link, "files.write");
    zxm_p_str(f->link, "path", FELL_FILE);
    zxm_p_strn(f->link, "text", f->file, used);
    return zxm_call(f->link);
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
static bool load_line(fell_t *t, const char *s, uint32_t at)
{
    int64_t v[5];
    if (at == 0) return !strcmp(s, "fell 1");
    if (at == 1) {
        if (strncmp(s, "incident ", 9)) return false;
        s += 9;
        if (!number(&s, &v[0]) || *s || !fell_in(v[0], 1, FELL_INCIDENT_MAX)) return false;
        t->incident = v[0];
        return true;
    }
    if (strncmp(s, "cas ", 4) || t->ncas >= FELL_CASUALTIES) return false;
    s += 4;
    for (uint32_t k = 0; k < 5u; k++)
        if (!number(&s, &v[k])) return false;
    if (!casualty_ok(t, t->incident, v[0], v[1], v[2], v[3], v[4], s, false)) return false;
    put_casualty(t, v[0], v[1], v[2], v[3], v[4], s);
    return true;
}

int fell_load(fell_t *f)
{
    static fell_t t;
    static char text[FELL_FILE_MAX + 2];
    if (!f || !f->link) return ZXM_E_ARG;
    zxm_link_t *l = f->link;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", FELL_FILE);
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    bool eof = false;
    if (zxm_get_str(l, l->result, "text", text, sizeof text) != ZXM_OK ||
        zxm_get_bool(l, l->result, "eof", &eof) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    if (!eof || strlen(text) > FELL_FILE_MAX) return ZXM_E_TOOBIG;
    memset(&t, 0, sizeof t);
    uint32_t at = 0;
    char *s = text;
    while (*s) {
        char *nl = strchr(s, '\n');
        if (!nl) return ZXM_E_PROTOCOL; /* every line ends */
        *nl = 0;
        if (!load_line(&t, s, at++)) {
            say(f, "Fell's casualty file is damaged; nothing was changed.");
            return ZXM_E_PROTOCOL;
        }
        s = nl + 1;
    }
    if (at < 2u) return ZXM_E_PROTOCOL;
    if (f->open && f->incident != t.incident) return ZXM_E_STATE; /* another incident's file */
    /* the whole file is good: only now is the model changed */
    f->open = true;
    f->incident = t.incident;
    memcpy(f->cas, t.cas, sizeof f->cas);
    f->ncas = t.ncas;
    return ZXM_OK;
}

/* ----- Waymark's places ----- */

int fell_refresh_map(fell_t *f)
{
    if (!f || !f->link) return ZXM_E_ARG;
    return waymark_refresh(&f->map);
}

int fell_add_place(fell_t *f, const char *name, int32_t lat, int32_t lon, const char *note,
                   bool owner_confirmed)
{
    if (!f || !name || !name[0] || strlen(name) > WAYMARK_NAME ||
        (note && strlen(note) > WAYMARK_NOTE))
        return ZXM_E_ARG;
    if (!owner_confirmed) return not_confirmed(f);
    return waymark_add(&f->map, name, lat, lon, note ? note : "");
}

/* ----- FM8: the team room ----- */

int fell_room_feed(fell_t *f)
{
    if (!f) return ZXM_E_ARG;
    return commons_feed(&f->room);
}

int fell_room_post(fell_t *f, uint32_t room, const char *text, bool owner_confirmed)
{
    if (!f || !text || room >= f->room.nroom) return ZXM_E_ARG;
    if (!owner_confirmed) return not_confirmed(f);
    if (!commons_may_post(&f->room, room)) return ZXM_E_STATE;
    return commons_post(&f->room, room, text, NULL);
}

int fell_set_share(fell_t *f, bool on, bool owner_confirmed)
{
    if (!f) return ZXM_E_ARG;
    if (!owner_confirmed) return not_confirmed(f);
    f->share_optin = on;
    return ZXM_OK;
}

/* millionths of a degree as decimal text, its sign kept */
static void degrees(char *out, size_t cap, int32_t v)
{
    int64_t a = v < 0 ? -(int64_t) v : (int64_t) v;
    snprintf(out, cap, "%s%lld.%06lld", v < 0 ? "-" : "", (long long) (a / 1000000),
             (long long) (a % 1000000));
}

int fell_share_post(fell_t *f, int64_t crew, bool owner_confirmed)
{
    if (!f || !fell_in(crew, 1, FELL_CREW_MAX)) return ZXM_E_ARG;
    if (!owner_confirmed) return not_confirmed(f);
    int rc = waymark_refresh(&f->map);
    if (rc != ZXM_OK) return rc;
    const waymark_here_t *h = &f->map.here;
    if (!watchfire_place_share_ok(f->open ? 1 : 0, f->share_optin ? 1 : 0,
                                  find_crew(f, crew) >= 0 ? 1 : 0, h->known && !h->stale ? 1 : 0)) {
        say(f, "Where the command post is is not shared: the incident must be open, sharing "
               "turned on, the crew on the roster and the location fresh.");
        return ZXM_E_STATE;
    }
    char text[160], lat[24], lon[24];
    degrees(lat, sizeof lat, h->lat);
    degrees(lon, sizeof lon, h->lon);
    snprintf(text, sizeof text, "Crew %lld: the command post is at %s, %s (within %u m).",
             (long long) crew, lat, lon, h->accuracy_m);
    return commons_post(&f->room, 0, text, NULL);
}

/* ----- FM9: radio ----- */

int fell_radio_bands(fell_t *f, uint32_t mask, bool owner_confirmed)
{
    if (!f) return ZXM_E_ARG;
    if (!owner_confirmed) return not_confirmed(f);
    int rc = aerie_bands(&f->radio);
    if (rc != ZXM_OK) return rc;
    return aerie_set_bands(&f->radio, mask);
}

int fell_radio_seal(fell_t *f, const aerie_chan_t *ch, const char *text, int64_t now_ms)
{
    if (!f || !ch || !text || !text[0]) return ZXM_E_ARG;
    size_t n = strlen(text);
    if (ch->band >= f->radio.nbands ||
        !aerie_band_fits((int64_t) n, f->radio.band[ch->band].max_frame))
        return ZXM_E_ARG;
    return aerie_seal(&f->radio, ch, (const uint8_t *) text, (uint32_t) n, now_ms);
}

int fell_radio_open(fell_t *f, const aerie_chan_t *ch, const char *frame_hex, int64_t now_ms)
{
    if (!f) return ZXM_E_ARG;
    return aerie_open(&f->radio, ch, frame_hex, now_ms);
}

int fell_line_send(fell_t *f, int64_t sync, int64_t shell, int64_t trunk)
{
    /* both rails full scale and agreeing: the frame says TRUE */
    static const int64_t plus[3] = {65536, 65536, 0}, minus[2] = {65536, 65536};
    if (!f || aerie_frame_tag(sync, shell, trunk) == AERIE_REFUSED) return ZXM_E_ARG;
    return aerie_line_encode(&f->radio, sync, shell, trunk, plus, minus);
}

int fell_line_read(fell_t *f, const char *frame_hex, int64_t *sync, int64_t *shell, int64_t *trunk)
{
    if (!f || !frame_hex || !sync || !shell || !trunk) return ZXM_E_ARG;
    int rc = aerie_line_decode(&f->radio, frame_hex);
    if (rc != ZXM_OK) return rc;
    if (f->radio.line.reserved || !aerie_tag_split(f->radio.line.tag, sync, shell, trunk))
        return ZXM_E_PROTOCOL; /* a reserved tag carries no Fell code */
    return ZXM_OK;
}

int fell_freight(fell_t *f, const char *text)
{
    if (!f || !text) return ZXM_E_ARG;
    size_t n = strlen(text);
    if (n < 1u || n > AERIE_FREIGHT_MAX) return ZXM_E_ARG;
    return aerie_freight_pack(&f->radio, (const uint8_t *) text, (uint32_t) n);
}
