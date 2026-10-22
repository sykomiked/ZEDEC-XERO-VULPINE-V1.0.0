/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* levin_model.c — Levin, the home wiring planner (see levin_model.h). */
#include <stdio.h>
#include <string.h>

#include "levin_model.h"

static void say(levin_t *v, const char *s)
{
    size_t n = strlen(s);
    if (n > LEVIN_NOTICE_MAX) n = LEVIN_NOTICE_MAX;
    memcpy(v->notice, s, n);
    v->notice[n] = 0;
}

static bool name_ok(const char *s)
{
    if (!s || !s[0]) return false;
    for (uint32_t i = 0; s[i]; i++)
        if (i >= LEVIN_NAME_MAX || (unsigned char) s[i] < 0x20 || s[i] == 0x7f) return false;
    return true;
}

static void forget_readings(levin_t *v)
{
    for (uint32_t i = 0; i < LEVIN_CIRCUITS; i++) {
        v->has_reading[i] = false;
        v->measured[i] = 0;
        v->state[i] = LPRES_STATE_NEITHER;
        v->tested[i] = -1;
    }
}

void levin_init(levin_t *v, zxm_link_t *link)
{
    memset(v, 0, sizeof *v);
    v->link = link;
    v->draw_mw = -1;
    forget_readings(v);
}

int32_t levin_find(const levin_t *v, int64_t id)
{
    for (uint32_t i = 0; i < v->nc; i++)
        if (v->c[i].id == id) return (int32_t) i;
    return -1;
}

/* ---- the plan's rules (LM2, LM3), shared by the parser and the changes ---- */

static bool circuit_fits(const levin_t *v, const levin_circuit_t *c)
{
    return levin_in(c->id, 1, LEVIN_CIRCUIT_MAX) &&
           (c->parent == 0 || (c->parent < c->id && levin_find(v, c->parent) >= 0)) &&
           levin_in(c->breaker, 1, LEVIN_BREAKER_MAX) && levin_in(c->cable, 1, LEVIN_CABLE_MAX) &&
           levin_in(c->volts, 1, LEVIN_VOLTS_MAX) && levin_in(c->metres, 0, LEVIN_METRES_MAX) &&
           levin_in(c->mohm, 0, LEVIN_MOHM_MAX) && levin_in(c->limit, 0, LEVIN_SLACK_MAX) &&
           levin_node_ok(LEVIN_PANEL, c->id, c->parent, c->breaker, c->cable);
}

static bool load_fits(const levin_t *v, int64_t circuit, int64_t room, int64_t watts)
{
    return levin_find(v, circuit) >= 0 && levin_in(room, 1, LEVIN_ROOM_MAX) &&
           levin_in(watts, 0, LEVIN_WATTS_MAX);
}

/* ---- the plan file (LM1) --------------------------------------------------- */

/* A plain decimal number at s[*at], no leading zero, a minus sign only before
 * a number other than 0, then `end` */
static bool num(const char *s, uint32_t n, uint32_t *at, char end, int64_t *out)
{
    uint32_t i = *at;
    bool neg = false;
    int64_t x = 0;
    if (i < n && s[i] == '-') neg = true, i++;
    uint32_t first = i;
    while (i < n && s[i] >= '0' && s[i] <= '9') {
        if (i - first >= 18u) return false;
        x = x * 10 + (s[i] - '0');
        i++;
    }
    if (i == first || i >= n || s[i] != end) return false;
    if (s[first] == '0' && (i - first > 1u || neg)) return false;
    *out = neg ? -x : x;
    *at = i + 1u;
    return true;
}

static bool parse_circuit(levin_t *next, const char *s, uint32_t n, uint32_t *at)
{
    levin_circuit_t c;
    if (!num(s, n, at, ' ', &c.id) || !num(s, n, at, ' ', &c.parent) ||
        !num(s, n, at, ' ', &c.breaker) || !num(s, n, at, ' ', &c.cable) ||
        !num(s, n, at, ' ', &c.volts) || !num(s, n, at, ' ', &c.metres) ||
        !num(s, n, at, ' ', &c.mohm) || !num(s, n, at, '\n', &c.limit))
        return false;
    if (next->nc >= LEVIN_CIRCUITS || !circuit_fits(next, &c) ||
        (next->nc && c.id <= next->c[next->nc - 1u].id))
        return false;
    next->c[next->nc++] = c;
    return true;
}

static bool parse_load(levin_t *next, const char *s, uint32_t n, uint32_t *at)
{
    levin_load_t L;
    if (!num(s, n, at, ' ', &L.circuit) || !num(s, n, at, ' ', &L.room) ||
        !num(s, n, at, ' ', &L.watts))
        return false;
    uint32_t i = *at, k = 0;
    while (i < n && s[i] != '\n') {
        if (k >= LEVIN_NAME_MAX) return false;
        L.name[k++] = s[i++];
    }
    if (i >= n) return false;
    L.name[k] = 0;
    *at = i + 1u;
    if (next->nld >= LEVIN_LOADS || !name_ok(L.name) ||
        !load_fits(next, L.circuit, L.room, L.watts))
        return false;
    next->ld[next->nld++] = L;
    return true;
}

static bool parse(levin_t *next, const char *s, uint32_t n)
{
    static const char HEAD[] = "levin 1\n";
    static const char *const WORD[2] = {"circuit ", "load "};
    uint32_t at = (uint32_t) strlen(HEAD), part = 0;
    if (n < at || memcmp(s, HEAD, at)) return false;
    next->nc = next->nld = 0;
    while (at < n) {
        uint32_t w = part;
        while (w < 2u && (n - at < strlen(WORD[w]) || memcmp(s + at, WORD[w], strlen(WORD[w]))))
            w++;
        if (w >= 2u) return false; /* an unknown line, or a part out of order */
        part = w;
        at += (uint32_t) strlen(WORD[w]);
        if (!(w == 0u ? parse_circuit(next, s, n, &at) : parse_load(next, s, n, &at))) return false;
    }
    return true;
}

static int32_t compose(const levin_t *p, char *out, uint32_t cap)
{
    uint32_t len = 0;
    int k = snprintf(out, cap, "levin 1\n");
    if (k < 0 || (uint32_t) k >= cap) return -1;
    len = (uint32_t) k;
    for (uint32_t i = 0; i < p->nc; i++) {
        const levin_circuit_t *c = &p->c[i];
        k = snprintf(out + len, cap - len, "circuit %lld %lld %lld %lld %lld %lld %lld %lld\n",
                     (long long) c->id, (long long) c->parent, (long long) c->breaker,
                     (long long) c->cable, (long long) c->volts, (long long) c->metres,
                     (long long) c->mohm, (long long) c->limit);
        if (k < 0 || (uint32_t) k >= cap - len) return -1;
        len += (uint32_t) k;
    }
    for (uint32_t i = 0; i < p->nld; i++) {
        const levin_load_t *L = &p->ld[i];
        k = snprintf(out + len, cap - len, "load %lld %lld %lld %s\n", (long long) L->circuit,
                     (long long) L->room, (long long) L->watts, L->name);
        if (k < 0 || (uint32_t) k >= cap - len) return -1;
        len += (uint32_t) k;
    }
    return (int32_t) len;
}

/* ---- zxvd ------------------------------------------------------------------ */

/* One request's text: whole lines of at most this many bytes. Names hold no
 * control byte, so each byte escapes to at most two. */
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
        zxm_p_str(l, "path", LEVIN_FILE);
        zxm_p_strn(l, "text", text + at, n);
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        int rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
    } while (at < len);
    return ZXM_OK;
}

static void keep(levin_t *v, const levin_t *next)
{
    memcpy(v->c, next->c, (size_t) next->nc * sizeof v->c[0]);
    v->nc = next->nc;
    memcpy(v->ld, next->ld, (size_t) next->nld * sizeof v->ld[0]);
    v->nld = next->nld;
}

/* Write next whole, then keep it (LM2) */
static int commit(levin_t *v, const levin_t *next)
{
    static char text[LEVIN_TEXT_MAX];
    int32_t len = compose(next, text, sizeof text);
    if (len < 0) return ZXM_E_TOOBIG;
    int rc = write_text(v->link, text, (uint32_t) len);
    if (rc) return rc;
    keep(v, next);
    return ZXM_OK;
}

static levin_t NEXT; /* a change before it is written */

int levin_load(levin_t *v)
{
    static char text[LEVIN_TEXT_MAX];
    static char chunk[LEVIN_TEXT_MAX];
    zxm_link_t *l = v->link;
    uint32_t len = 0;
    uint64_t off = 0;
    for (;;) {
        zxm_begin(l, "files.read");
        zxm_p_str(l, "path", LEVIN_FILE);
        zxm_p_u64(l, "offset", off);
        int rc = zxm_call(l);
        if (rc == ZXM_E_REMOTE && l->err_code == LEVIN_E_FILES && !off &&
            strstr(l->err, "no such file")) {
            levin_init(v, l); /* not made yet: an empty plan */
            v->loaded = true;
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
        if (size >= LEVIN_TEXT_MAX) return ZXM_E_TOOBIG;
        int32_t n = w4_json_str(l->resp, &l->tok[t], chunk, sizeof chunk);
        if (n < 0 || len + (uint32_t) n >= LEVIN_TEXT_MAX) return ZXM_E_TOOBIG;
        memcpy(text + len, chunk, (size_t) n);
        len += (uint32_t) n;
        if (eof) break;
        if (next_off <= off) return ZXM_E_PROTOCOL;
        off = next_off;
    }
    memset(&NEXT, 0, sizeof NEXT);
    if (!parse(&NEXT, text, len)) {
        v->loaded = false; /* LM1: never overwritten */
        return ZXM_E_PROTOCOL;
    }
    keep(v, &NEXT);
    forget_readings(v);
    v->nen = 0;
    v->loaded = true;
    return ZXM_OK;
}

int levin_add_circuit(levin_t *v, const levin_circuit_t *c, bool owner_confirmed)
{
    if (!c || !circuit_fits(v, c) || levin_find(v, c->id) >= 0) return ZXM_E_ARG;
    if (!owner_confirmed || !v->loaded || v->nc >= LEVIN_CIRCUITS) return ZXM_E_STATE;
    keep(&NEXT, v);
    /* ids rising: the new circuit takes its place */
    uint32_t at = NEXT.nc;
    while (at && NEXT.c[at - 1u].id > c->id) at--;
    memmove(&NEXT.c[at + 1u], &NEXT.c[at], (size_t) (NEXT.nc - at) * sizeof NEXT.c[0]);
    NEXT.c[at] = *c;
    NEXT.nc++;
    /* a circuit's place moves: its readings move with it */
    int rc = commit(v, &NEXT);
    if (rc) return rc;
    for (uint32_t i = v->nc - 1u; i > at; i--) {
        v->has_reading[i] = v->has_reading[i - 1u];
        v->measured[i] = v->measured[i - 1u];
        v->state[i] = v->state[i - 1u];
        v->tested[i] = v->tested[i - 1u];
    }
    v->has_reading[at] = false;
    v->measured[at] = 0;
    v->state[at] = LPRES_STATE_NEITHER;
    v->tested[at] = -1;
    return ZXM_OK;
}

int levin_add_load(levin_t *v, int64_t circuit, int64_t room, int64_t watts, const char *name,
                   bool owner_confirmed)
{
    if (!name_ok(name) || !load_fits(v, circuit, room, watts)) return ZXM_E_ARG;
    if (!owner_confirmed || !v->loaded || v->nld >= LEVIN_LOADS) return ZXM_E_STATE;
    keep(&NEXT, v);
    levin_load_t *L = &NEXT.ld[NEXT.nld++];
    L->circuit = circuit;
    L->room = room;
    L->watts = watts;
    memcpy(L->name, name, strlen(name) + 1u);
    return commit(v, &NEXT);
}

/* The planned watts of one circuit, or LEVIN_REFUSED past LR2's domain */
static int64_t planned(const levin_t *v, int64_t circuit)
{
    int64_t w = 0;
    for (uint32_t i = 0; i < v->nld; i++) {
        if (v->ld[i].circuit != circuit) continue;
        w += v->ld[i].watts; /* each below 10^6, at most 64 of them */
    }
    return w > LEVIN_WATTS_MAX ? LEVIN_REFUSED : w;
}

int levin_check(levin_t *v, int64_t circuit, levin_report_t *out)
{
    int32_t at = levin_find(v, circuit);
    if (at < 0 || !out) return ZXM_E_ARG;
    const levin_circuit_t *c = &v->c[at];
    levin_report_t r;
    memset(&r, 0, sizeof r);
    r.watts = planned(v, circuit);
    if (r.watts == LEVIN_REFUSED || !levin_load_ma(r.watts, c->volts, &r.load)) return ZXM_E_STATE;
    r.ma = levin_ma_up(r.load);
    if (r.ma == LEVIN_REFUSED || r.ma > LEVIN_MA_MAX) return ZXM_E_STATE;
    r.drop = levin_drop_uv(c->metres, r.ma, c->mohm);
    r.spare = levin_spare(c->breaker, r.load);
    if (r.drop == LEVIN_REFUSED || r.spare == LEVIN_REFUSED) return ZXM_E_STATE;
    r.cable = c->cable - c->breaker;
    r.slack = c->limit - r.drop;
    r.within = levin_within_breaker(r.spare, r.cable, r.slack);
    char text[LEVIN_NOTICE_MAX + 1];
    if (r.within)
        snprintf(text, sizeof text, "Circuit %lld is within its breaker: %lld W, %lld mA.",
                 (long long) circuit, (long long) r.watts, (long long) r.ma);
    else
        snprintf(text, sizeof text,
                 "Circuit %lld is past its breaker (%s%s%s): ask a licensed electrician; "
                 "Levin switches nothing.",
                 (long long) circuit, r.spare < 0 ? "load over 80% of the rating " : "",
                 r.cable < 0 ? "cable rated under the breaker " : "",
                 r.slack < 0 ? "voltage drop over the limit" : "");
    say(v, text);
    *out = r;
    return ZXM_OK;
}

int levin_matrix(const levin_t *v, int64_t *cell, uint32_t cap, uint32_t *n)
{
    int64_t ci[LEVIN_LOADS], rm[LEVIN_LOADS], w[LEVIN_LOADS];
    if (!cell || !n || cap < v->nld) return ZXM_E_ARG;
    for (uint32_t i = 0; i < v->nld; i++) {
        ci[i] = v->ld[i].circuit;
        rm[i] = v->ld[i].room;
        w[i] = v->ld[i].watts;
    }
    for (uint32_t i = 0; i < v->nld; i++) {
        cell[i] = levin_room_cell(ci, rm, w, v->nld, i);
        if (cell[i] == LEVIN_REFUSED) return ZXM_E_STATE;
    }
    *n = v->nld;
    return ZXM_OK;
}

int levin_reading(levin_t *v, int64_t circuit, int64_t watts)
{
    int32_t at = levin_find(v, circuit);
    if (at < 0 || !levin_in(watts, 0, LEVIN_WATTS_MAX)) return ZXM_E_ARG;
    v->has_reading[at] = true;
    v->measured[at] = watts;
    return ZXM_OK;
}

int levin_verify(levin_t *v, lpres_state_t *plan, uint32_t *miswired)
{
    if (!plan || !miswired) return ZXM_E_ARG;
    lpres_state_t s[LEVIN_CIRCUITS];
    for (uint32_t i = 0; i < v->nc; i++) {
        int64_t w = planned(v, v->c[i].id);
        if (w == LEVIN_REFUSED) return ZXM_E_STATE;
        s[i] = levin_circuit_state(w, v->has_reading[i], v->measured[i]);
    }
    /* all checked: now kept */
    lpres_state_t p = v->nc ? LPRES_STATE_TRUE : LPRES_STATE_NEITHER;
    uint32_t bad = 0;
    char text[LEVIN_NOTICE_MAX + 1], *at = text;
    size_t room = sizeof text;
    int k = snprintf(at, room, "Miswired, plan and reading disagree:");
    for (uint32_t i = 0; i < v->nc; i++) {
        v->state[i] = s[i];
        p = lpres_conjoin(p, levin_wired(s[i]));
        if (s[i] != LPRES_STATE_BOTH) continue;
        bad++;
        if (k > 0 && (size_t) k < room) {
            at += k;
            room -= (size_t) k;
            k = snprintf(at, room, " circuit %lld", (long long) v->c[i].id);
        }
    }
    if (bad) say(v, text);
    *plan = p;
    *miswired = bad;
    return ZXM_OK;
}

static const char *const STEP[LEVIN_STEPS] = {
    "continuity", "insulation resistance", "polarity", "earth fault loop", "residual-current trip",
};

const char *levin_step_name(uint32_t step)
{
    return step < LEVIN_STEPS ? STEP[step] : "";
}

int levin_test_run(levin_t *v, int64_t circuit, const int64_t *code, bool *passed, uint32_t *failed)
{
    int32_t at = levin_find(v, circuit);
    if (at < 0 || !code || !passed || !failed) return ZXM_E_ARG;
    for (uint32_t s = 0; s < LEVIN_STEPS; s++)
        if (!levin_in(code[s], MOCK_NOT_RUN, MOCK_CODE_MAX)) return ZXM_E_ARG;
    bool ok = mockingbird_boot_passed(code);
    uint32_t f = mockingbird_first_failed(code);
    char text[LEVIN_NOTICE_MAX + 1];
    v->tested[at] = ok ? 1 : 0;
    if (ok)
        snprintf(text, sizeof text,
                 "Circuit %lld: every test step passed in order; the inspection is the "
                 "electrician's.",
                 (long long) circuit);
    else
        snprintf(text, sizeof text, "Circuit %lld: the %s step did not pass; do not energise it.",
                 (long long) circuit, levin_step_name(f < LEVIN_STEPS ? f : 0u));
    say(v, text);
    *passed = ok;
    *failed = ok ? LEVIN_STEPS : f;
    return ZXM_OK;
}

int32_t levin_panel_heat(levin_t *v, int64_t temp_dc)
{
    int32_t h = brazier_heat(temp_dc);
    if (h == BRAZIER_HEAT_CRITICAL)
        say(v, "The panel is at a critical temperature: keep clear and call a licensed "
               "electrician.");
    else if (h == BRAZIER_HEAT_WARN)
        say(v, "The panel is outside its usual temperature: have it looked at.");
    return h;
}

int levin_refresh_draw(levin_t *v)
{
    zxm_link_t *l = v->link;
    uint64_t mw;
    zxm_begin(l, "power.status");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_u64(l, l->result, "total_mw", &mw) || mw > (uint64_t) LEVIN_WATTS_MAX * 1000u)
        return ZXM_E_PROTOCOL;
    v->draw_mw = (int64_t) mw;
    return ZXM_OK;
}

int levin_energy(levin_t *v, int64_t circuit, int64_t wh)
{
    if (levin_find(v, circuit) < 0 || !levin_in(wh, 0, LEVIN_WH_MAX)) return ZXM_E_ARG;
    if (v->nen >= LEVIN_READINGS) return ZXM_E_STATE; /* backpressure: never overwritten */
    v->en_circuit[v->nen] = circuit;
    v->en_wh[v->nen] = wh;
    v->nen++;
    return ZXM_OK;
}

int levin_circuit_energy(const levin_t *v, int64_t circuit, int64_t *wh)
{
    if (!wh || levin_find(v, circuit) < 0) return ZXM_E_ARG;
    return homestead_energy(v->en_circuit, v->en_wh, v->nen, circuit, wh) ? ZXM_E_STATE : ZXM_OK;
}

bool levin_device_cmd_shown(int64_t kind, int64_t lo, int64_t hi, int64_t setpoint, int64_t owner,
                            int64_t approved)
{
    if (kind < HOMESTEAD_LIGHT || kind > HOMESTEAD_OVEN || !homestead_point(lo) ||
        !homestead_point(hi) || !homestead_point(setpoint) || lo > hi)
        return false;
    return homestead_command_ok(setpoint - lo, hi - setpoint, homestead_owed(kind, owner, approved),
                                0);
}

int levin_permits(const int64_t *base, const int64_t *inspections, const int64_t *visit, uint32_t n,
                  int64_t *total)
{
    int64_t s = 0;
    if (!total || ((!base || !inspections || !visit) && n)) return ZXM_E_ARG;
    for (uint32_t i = 0; i < n; i++) {
        int64_t f = levin_permit_fee(base[i], inspections[i], visit[i]);
        if (f == LEVIN_REFUSED || s > INT64_MAX - f) return ZXM_E_ARG;
        s += f;
    }
    *total = s;
    return ZXM_OK;
}
