/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* escapement_model.c — Escapement, the shared clock (escapement_model.h). */
#include "escapement_model.h"

#include <stdio.h>
#include <string.h>

#define ESCAPEMENT_E_FILES (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */
#define ESCAPEMENT_DAY_MS  INT64_C(86400000)
#define ESCAPEMENT_TEXT    512u

static void say(escapement_t *e, const char *text)
{
    snprintf(e->notice, sizeof e->notice, "%s", text);
}

static bool hex_ok(const char *s, uint32_t n)
{
    if (!s || strlen(s) != n) return false;
    for (uint32_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return false;
    }
    return true;
}

void escapement_init(escapement_t *e, zxm_link_t *link)
{
    memset(e, 0, sizeof *e);
    e->link = link;
    e->tol_ms = ESCAPEMENT_TOL_START;
    e->clock = LPRES_STATE_NEITHER;
}

/* ---- EM1: this clock, the roster and the peers -------------------------------------- */

int escapement_refresh(escapement_t *e)
{
    zxm_link_t *l = e->link;
    int64_t ms = 0;
    zxm_begin(l, "sys.info");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_i64(l, l->result, "time_ms", &ms)) return ZXM_E_PROTOCOL;
    if (ms < 0 || ms > ESCAPEMENT_MS_MAX) return ZXM_E_PROTOCOL; /* no time a clock shows */
    e->now_ms = ms;
    e->have = true;
    e->nread = 0; /* EM2: readings belong to the instant they were read at */
    e->voted = false;
    e->clock = LPRES_STATE_NEITHER;
    say(e, "This clock read: give the other devices' readings of this instant.");
    return ZXM_OK;
}

int escapement_read_roster(escapement_t *e)
{
    static char got[ESCAPEMENT_ROSTER][ESCAPEMENT_DEVICE_HEX + 1];
    zxm_link_t *l = e->link;
    zxm_begin(l, "devmesh.devices");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "devices");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr), k = 0;
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, arr, i);
        char id[ESCAPEMENT_DEVICE_HEX + 1], status[16];
        bool self = false;
        if (o < 0 || l->tok[o].type != W4_J_OBJ || zxm_get_str(l, o, "id", id, sizeof id) ||
            !hex_ok(id, ESCAPEMENT_DEVICE_HEX) ||
            zxm_get_str(l, o, "status", status, sizeof status) || zxm_get_bool(l, o, "self", &self))
            return ZXM_E_PROTOCOL;
        if (strcmp(status, "active") && strcmp(status, "revoked")) return ZXM_E_PROTOCOL;
        if (self || strcmp(status, "active")) continue; /* EM1: never this one or a revoked one */
        if (k >= ESCAPEMENT_ROSTER) return ZXM_E_TOOBIG;
        memcpy(got[k++], id, sizeof id);
    }
    memcpy(e->device, got, k * sizeof got[0]);
    e->ndevices = k;
    snprintf(e->notice, sizeof e->notice, "%u other active device%s on the roster.", k,
             k == 1u ? "" : "s");
    return ZXM_OK;
}

int escapement_read_peers(escapement_t *e)
{
    static char got[ESCAPEMENT_PEERS_MAX][ESCAPEMENT_NODE_HEX + 1];
    zxm_link_t *l = e->link;
    zxm_begin(l, "net.peers");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "peers");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, arr);
    if (n > ESCAPEMENT_PEERS_MAX) return ZXM_E_PROTOCOL; /* zxvd lists at most 256 */
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l, arr, i);
        if (o < 0 || l->tok[o].type != W4_J_OBJ || zxm_get_str(l, o, "id", got[i], sizeof got[i]) ||
            !hex_ok(got[i], ESCAPEMENT_NODE_HEX))
            return ZXM_E_PROTOCOL;
    }
    memcpy(e->peer, got, n * sizeof got[0]);
    e->npeers = n;
    snprintf(e->notice, sizeof e->notice, "%u verified peer%s.", n, n == 1u ? "" : "s");
    return ZXM_OK;
}

/* ---- EM2-EM4: readings, the vote, the shared time ----------------------------------- */

static bool known_source(const escapement_t *e, const char *id)
{
    for (uint32_t i = 0; i < e->ndevices; i++)
        if (!strcmp(e->device[i], id)) return true;
    for (uint32_t i = 0; i < e->npeers; i++)
        if (!strcmp(e->peer[i], id)) return true;
    return false;
}

int escapement_reading(escapement_t *e, const char *id, int64_t read_ms)
{
    if (!id || !(hex_ok(id, ESCAPEMENT_DEVICE_HEX) || hex_ok(id, ESCAPEMENT_NODE_HEX)) ||
        read_ms < 0 || read_ms > ESCAPEMENT_MS_MAX)
        return ZXM_E_ARG;
    if (!e->have) return ZXM_E_STATE;
    int64_t off = read_ms - e->now_ms;
    if (off < -ESCAPEMENT_OFFSET_MAX || off > ESCAPEMENT_OFFSET_MAX) {
        say(e, "That reading is more than a day from this clock: not a reading of this instant.");
        return ZXM_E_ARG;
    }
    if (!known_source(e, id)) {
        say(e, "Not a device on the roster or a verified peer: its clock is not asked.");
        return ZXM_E_STATE;
    }
    for (uint32_t i = 0; i < e->nread; i++)
        if (!strcmp(e->reading[i].id, id)) {
            say(e, "That device has given its reading of this instant already.");
            return ZXM_E_STATE;
        }
    if (e->nread >= ESCAPEMENT_READINGS - 1u) return ZXM_E_TOOBIG;
    escapement_reading_t *r = &e->reading[e->nread++];
    snprintf(r->id, sizeof r->id, "%s", id);
    r->read_ms = read_ms;
    return ZXM_OK;
}

int escapement_set_tolerance(escapement_t *e, int64_t tol_ms)
{
    if (tol_ms < 0 || tol_ms > ESCAPEMENT_TOL_MAX) return ZXM_E_ARG;
    e->tol_ms = tol_ms;
    return ZXM_OK;
}

lpres_state_t escapement_vote(escapement_t *e)
{
    int64_t off[ESCAPEMENT_READINGS];
    e->voted = false;
    if (!e->have) {
        e->clock = LPRES_STATE_NEITHER;
        say(e, "No time read yet.");
        return e->clock;
    }
    off[0] = 0; /* this clock */
    for (uint32_t i = 0; i < e->nread; i++) off[i + 1u] = e->reading[i].read_ms - e->now_ms;
    e->clock = escapement_clock_state(off, e->nread + 1u, e->tol_ms, &e->vote_ms, &e->voted);
    if (e->clock == LPRES_STATE_TRUE)
        snprintf(e->notice, sizeof e->notice, "%u clock%s agree within %lld ms.", e->nread + 1u,
                 e->nread ? "s" : "", (long long) e->tol_ms);
    else if (e->voted)
        snprintf(e->notice, sizeof e->notice,
                 "The clocks disagree by more than %lld ms; %u devices vote an offset of %lld ms. "
                 "No clock is changed until you accept it.",
                 (long long) e->tol_ms, e->nread + 1u, (long long) e->vote_ms);
    else
        snprintf(e->notice, sizeof e->notice,
                 "The clocks disagree by more than %lld ms and %u devices are too few to vote: "
                 "the time is held.",
                 (long long) e->tol_ms, e->nread + 1u);
    return e->clock;
}

int escapement_accept_vote(escapement_t *e)
{
    if (!e->voted) {
        say(e, "There is no vote to accept.");
        return ZXM_E_STATE;
    }
    e->offset_ms = e->vote_ms;
    e->clock = LPRES_STATE_TRUE;
    snprintf(e->notice, sizeof e->notice,
             "The shared time is this clock %+lld ms; no clock was set.", (long long) e->offset_ms);
    return ZXM_OK;
}

int64_t escapement_shared_ms(const escapement_t *e)
{
    if (!e->have) return -1;
    int64_t t = e->now_ms + e->offset_ms;
    return t < 0 ? 0 : t;
}

int escapement_week_minute(const escapement_t *e, int64_t offset, int64_t *weekday, int64_t *minute)
{
    if (!weekday || !minute || offset < SUNDIAL_OFFSET_MIN || offset > SUNDIAL_OFFSET_MAX)
        return ZXM_E_ARG;
    int64_t t = escapement_shared_ms(e);
    if (t < 0) return ZXM_E_STATE;
    int64_t local = t + offset * 60000;
    if (local < 0) return ZXM_E_STATE; /* before 1970 in that zone */
    int64_t y = 0, m = 0, d = 0;
    if (!solstice_date_of(local / ESCAPEMENT_DAY_MS, &y, &m, &d)) return ZXM_E_STATE;
    int64_t wd = solstice_weekday(y, m, d);
    int64_t mm = sundial_zone_minute(t / 60000 % SUNDIAL_DAY_MIN, offset);
    if (wd < 1 || mm == SUNDIAL_REFUSED) return ZXM_E_STATE;
    *weekday = wd;
    *minute = (wd - 1) * SUNDIAL_DAY_MIN + mm;
    return ZXM_OK;
}

/* ---- EM5, EM6: shifts and timesheets ------------------------------------------------- */

int escapement_week_total(const escapement_t *e, int64_t person, int64_t week, int64_t *minutes)
{
    if (!minutes) return ZXM_E_ARG;
    int64_t total = 0;
    for (uint32_t i = 0; i < e->nshift; i++) {
        const escapement_shift_t *s = &e->shift[i];
        if (s->person != person || s->week != week) continue;
        total = escapement_week_after(s->start, s->finish, s->unpaid, total);
        if (total == ESCAPEMENT_REFUSED) return ZXM_E_STATE; /* EM5 keeps none such */
    }
    *minutes = total;
    return ZXM_OK;
}

int escapement_shift_add(escapement_t *e, int64_t person, int64_t week, int64_t start,
                         int64_t finish, int64_t unpaid, uint32_t *index)
{
    static int64_t who[ESCAPEMENT_SHIFTS + 1u], from[ESCAPEMENT_SHIFTS + 1u],
        to[ESCAPEMENT_SHIFTS + 1u];
    if (!index || person < 1 || week < 1 || week > ESCAPEMENT_WEEK_MAX) return ZXM_E_ARG;
    int64_t prior = 0, number = 1;
    uint32_t n = 0;
    if (escapement_week_total(e, person, week, &prior) != ZXM_OK) return ZXM_E_STATE;
    int64_t after = escapement_week_after(start, finish, unpaid, prior);
    if (after == ESCAPEMENT_REFUSED) {
        say(e, "That shift is not inside the week, or the week would pass 10080 minutes.");
        return ZXM_E_ARG;
    }
    for (uint32_t i = 0; i < e->nshift; i++) {
        const escapement_shift_t *s = &e->shift[i];
        if (s->week != week) continue;
        number += s->person == person;
        who[n] = s->person, from[n] = s->start, to[n] = s->finish, n++;
    }
    who[n] = person, from[n] = start, to[n] = finish;
    if (!escapement_node_ok(person, week, number, start, finish, finish - start - unpaid)) {
        say(e, "That shift does not fit the shift tree.");
        return ZXM_E_ARG;
    }
    if (escapement_clashes(who, from, to, n + 1u, n) != 0) {
        say(e, "That person is already booked then: nobody is booked twice at once.");
        return ZXM_E_STATE;
    }
    if (e->nshift >= ESCAPEMENT_SHIFTS) return ZXM_E_TOOBIG;
    escapement_shift_t *s = &e->shift[e->nshift];
    s->person = person, s->week = week, s->shift = number;
    s->start = start, s->finish = finish, s->unpaid = unpaid;
    *index = e->nshift++;
    snprintf(e->notice, sizeof e->notice, "Shift %lld kept; the week is %lld minutes.",
             (long long) number, (long long) after);
    return ZXM_OK;
}

static int read_text(escapement_t *e, const char *path, char *text, uint32_t cap)
{
    zxm_link_t *l = e->link;
    bool eof = false;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", path);
    zxm_p_u64(l, "max", cap - 1u);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE && l->err_code == ESCAPEMENT_E_FILES && strstr(l->err, "no such"))
        return ZXM_E_STATE;
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "text", text, cap) || zxm_get_bool(l, l->result, "eof", &eof))
        return ZXM_E_PROTOCOL;
    return eof ? ZXM_OK : ZXM_E_TOOBIG;
}

static int write_text(escapement_t *e, const char *path, const char *text, bool fresh)
{
    zxm_link_t *l = e->link;
    zxm_begin(l, "files.write");
    zxm_p_str(l, "path", path);
    zxm_p_str(l, "text", text);
    if (fresh) zxm_p_bool(l, "exclusive", true); /* EM6: never over an earlier one */
    return zxm_call(l);
}

int escapement_load_register(escapement_t *e)
{
    static const char head[] = "escapement timesheets\nlast ";
    char text[64];
    int rc = read_text(e, ESCAPEMENT_REGISTER, text, sizeof text);
    if (rc == ZXM_E_STATE) { /* none yet: numbering starts at 1 */
        e->last_sheet = 0;
        e->register_loaded = true;
        return ZXM_OK;
    }
    if (rc) return rc;
    if (strncmp(text, head, sizeof head - 1u)) return ZXM_E_PROTOCOL;
    int64_t last = 0;
    uint32_t digits = 0;
    const char *p = text + sizeof head - 1u;
    for (; *p >= '0' && *p <= '9'; p++, digits++) {
        if (digits >= 9u) return ZXM_E_PROTOCOL;
        last = last * 10 + (*p - '0');
    }
    if (!digits || p[0] != '\n' || p[1]) return ZXM_E_PROTOCOL;
    e->last_sheet = last;
    e->register_loaded = true;
    return ZXM_OK;
}

int escapement_issue_timesheet(escapement_t *e, int64_t person, int64_t week, int64_t *number)
{
    char path[64], text[ESCAPEMENT_TEXT];
    int64_t minutes = 0, shifts = 0;
    if (!number || person < 1 || week < 1 || week > ESCAPEMENT_WEEK_MAX) return ZXM_E_ARG;
    if (!e->register_loaded) return ZXM_E_STATE;
    int rc = escapement_week_total(e, person, week, &minutes);
    if (rc) return rc;
    for (uint32_t i = 0; i < e->nshift; i++)
        shifts += e->shift[i].person == person && e->shift[i].week == week;
    if (!shifts) {
        say(e, "No shifts that week: no timesheet.");
        return ZXM_E_STATE;
    }
    int64_t next = tstk_next_number(e->last_sheet);
    if (next == TSTK_REFUSED) {
        say(e, "The timesheet register is at its last number (nine digits).");
        return ZXM_E_STATE;
    }
    snprintf(path, sizeof path, "escapement-timesheet-%lld.txt", (long long) next);
    snprintf(text, sizeof text,
             "Escapement timesheet %lld\nperson %lld\nweek %lld\nshifts %lld\nminutes %lld\n",
             (long long) next, (long long) person, (long long) week, (long long) shifts,
             (long long) minutes);
    rc = write_text(e, path, text, true);
    if (rc) return rc;
    snprintf(text, sizeof text, "escapement timesheets\nlast %lld\n", (long long) next);
    rc = write_text(e, ESCAPEMENT_REGISTER, text, false);
    if (rc) return rc;
    e->last_sheet = next;
    *number = next;
    snprintf(e->notice, sizeof e->notice, "Timesheet %lld: %lld minutes in week %lld.",
             (long long) next, (long long) minutes, (long long) week);
    return ZXM_OK;
}

/* ---- EM7: timed jobs ----------------------------------------------------------------- */

int escapement_job_add(escapement_t *e, int64_t task, int64_t start, int64_t period, int64_t cost,
                       int64_t open_ms, int64_t close_ms, int64_t deadline_ms, uint32_t *index)
{
    if (!index || task < 1 || treadle_next_run(start, period, 0) < 0 ||
        !treadle_record_fits(task, 0, start, period, cost, 0) || cost < 0 || open_ms < 0 ||
        close_ms <= open_ms || deadline_ms < open_ms || close_ms > ESCAPEMENT_MS_MAX ||
        deadline_ms > ESCAPEMENT_MS_MAX)
        return ZXM_E_ARG;
    for (uint32_t i = 0; i < e->njob; i++)
        if (e->job[i].task == task) return ZXM_E_STATE;
    if (e->njob >= ESCAPEMENT_JOBS) return ZXM_E_TOOBIG;
    escapement_job_t *j = &e->job[e->njob];
    j->task = task, j->start = start, j->period = period, j->cost = cost;
    j->open_ms = open_ms, j->close_ms = close_ms, j->deadline_ms = deadline_ms;
    j->approved = false;
    *index = e->njob++;
    say(e, "Job added; it waits for your approval.");
    return ZXM_OK;
}

int escapement_job_approve(escapement_t *e, uint32_t index)
{
    if (index >= e->njob) return ZXM_E_ARG;
    e->job[index].approved = true;
    return ZXM_OK;
}

lpres_state_t escapement_job_state(escapement_t *e, uint32_t index, int64_t offset, int64_t left,
                                   int64_t *next_run)
{
    int64_t wd = 0, minute = 0;
    if (index >= e->njob || !next_run ||
        escapement_week_minute(e, offset, &wd, &minute) != ZXM_OK) {
        say(e, "No job, or no time read yet.");
        return LPRES_STATE_NEITHER;
    }
    const escapement_job_t *j = &e->job[index];
    int64_t now = escapement_shared_ms(e);
    *next_run = treadle_next_run(j->start, j->period, minute % SUNDIAL_DAY_MIN);
    bool may = j->approved && treadle_budget_ok(j->cost, left) &&
               escapement_job_may_run(now - j->open_ms, j->close_ms - now, j->deadline_ms - now);
    lpres_state_t st = lpres_conjoin(e->clock, may ? LPRES_STATE_TRUE : LPRES_STATE_FALSE);
    if (st == LPRES_STATE_TRUE)
        snprintf(e->notice, sizeof e->notice, "Job %lld may run now.", (long long) j->task);
    else if (st == LPRES_STATE_FALSE)
        snprintf(e->notice, sizeof e->notice, "Job %lld may not run now.", (long long) j->task);
    else
        snprintf(e->notice, sizeof e->notice, "Job %lld is held: the clocks are not settled.",
                 (long long) j->task);
    return st;
}
