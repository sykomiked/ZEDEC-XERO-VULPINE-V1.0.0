/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ratchet_model.c — Ratchet, maintenance and repair (see ratchet_model.h). */
#include <stdio.h>
#include <string.h>

#include "homestead_rules.h"
#include "mockingbird_rules.h"
#include "ratchet_model.h"
#include "treadle_rules.h"

static void say(ratchet_t *r, const char *s)
{
    size_t n = strlen(s);
    if (n > RATCHET_NOTICE_MAX) n = RATCHET_NOTICE_MAX;
    memcpy(r->notice, s, n);
    r->notice[n] = 0;
}

static bool name_ok(const char *s)
{
    if (!s || !s[0]) return false;
    for (uint32_t i = 0; s[i]; i++)
        if (i >= RATCHET_NAME_MAX || (unsigned char) s[i] < 0x20) return false;
    return true;
}

void ratchet_init(ratchet_t *r, zxm_link_t *link)
{
    memset(r, 0, sizeof *r);
    r->link = link;
    tidewatch_init(&r->tide, link);
    r->walk.failed_stage = MOCK_STAGES;
}

int32_t ratchet_find(const ratchet_t *r, int64_t part)
{
    for (uint32_t i = 0; i < r->np; i++)
        if (r->p[i].part == part) return (int32_t) i;
    return -1;
}

int ratchet_add_part(ratchet_t *r, int64_t part, int64_t parent, int64_t kind, const char *name,
                     const ratchet_map_t *map, bool owner_confirmed)
{
    if (part < 1 || part > RATCHET_PART_MAX || kind < RATCHET_KIND_MACHINE ||
        kind > RATCHET_KIND_OTHER || !name_ok(name) || ratchet_find(r, part) >= 0 ||
        (parent != 0 && ratchet_find(r, parent) < 0) ||
        !ratchet_node_ok(RATCHET_MACHINE, part, parent, kind, 0))
        return ZXM_E_ARG;
    /* RM1: a place on the map is a device record (mockingbird.emu_device_ok) */
    if (map && (map->size < 1 || !mockingbird_device_record_ok((int64_t) r->np, kind, map->base,
                                                               map->size, map->irq)))
        return ZXM_E_ARG;
    if (r->np >= RATCHET_PARTS || !owner_confirmed) return ZXM_E_STATE;
    ratchet_part_t *p = &r->p[r->np];
    memset(p, 0, sizeof *p);
    p->part = part;
    p->parent = parent;
    p->kind = kind;
    p->state = LPRES_STATE_NEITHER;
    for (uint32_t m = 0; m < RATCHET_MEASURES; m++) p->says[m] = LPRES_STATE_NEITHER;
    memcpy(p->name, name, strlen(name) + 1);
    r->np++;
    return ZXM_OK;
}

int ratchet_set_limit(ratchet_t *r, int64_t part, uint32_t measure, int64_t lo, int64_t hi,
                      bool owner_confirmed)
{
    int32_t at = ratchet_find(r, part);
    if (at < 0 || measure >= RATCHET_MEASURES || lo > hi || lo < -RATCHET_WORN_MAX ||
        hi > RATCHET_WORN_MAX || hi - lo > RATCHET_MARGIN_MAX)
        return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    ratchet_part_t *p = &r->p[at];
    p->has_limit[measure] = true;
    p->lo[measure] = lo;
    p->hi[measure] = hi;
    return ZXM_OK;
}

/* RM3: keep one reading; on the grid when it has a limit */
static int keep(ratchet_t *r, uint32_t at, uint32_t measure, int64_t value, int64_t hour)
{
    ratchet_part_t *p = &r->p[at];
    int64_t above = 0, below = 0, fault = 0;
    bool limited = p->has_limit[measure];
    if (limited) {
        if (r->ngrid >= RATCHET_GRID_ROWS) return ZXM_E_STATE; /* backpressure */
        if (!ratchet_margins(value, p->lo[measure], p->hi[measure], &above, &below))
            return ZXM_E_ARG;
        fault = ratchet_limits_ok(above, below) ? 0 : 1;
    }
    p->has_reading[measure] = true;
    p->reading[measure] = value;
    p->says[measure] = limited ? ratchet_measure_says(above, below) : LPRES_STATE_NEITHER;
    if (limited) {
        r->grid_sensor[r->ngrid] = p->part * 10 + (int64_t) measure;
        r->grid_hour[r->ngrid] = hour;
        r->grid_fault[r->ngrid] = fault;
        r->ngrid++;
    }
    return ZXM_OK;
}

int ratchet_reading(ratchet_t *r, int64_t part, uint32_t measure, int64_t value, int64_t hour)
{
    int32_t at = ratchet_find(r, part);
    if (at < 0 || measure >= RATCHET_MEASURES || hour < 0 || hour > RATCHET_WORN_MAX ||
        value < -RATCHET_WORN_MAX || value > RATCHET_WORN_MAX)
        return ZXM_E_ARG;
    return keep(r, (uint32_t) at, measure, value, hour);
}

/* The machine's readings for every part of one kind */
static int put_kind(ratchet_t *r, int64_t kind, uint32_t measure, int64_t value)
{
    for (uint32_t i = 0; i < r->np; i++) {
        if (r->p[i].kind != kind) continue;
        int rc = keep(r, i, measure, value, r->hour);
        if (rc) return rc;
    }
    return ZXM_OK;
}

/* What one refresh read, checked before anything is kept */
typedef struct {
    int64_t draw, temp, faults, used;
    bool has_temp;
    int64_t domain[RATCHET_METERS], uj[RATCHET_METERS];
    uint32_t nmeters;
} ratchet_read_t;

static int read_power(zxm_link_t *l, ratchet_read_t *o)
{
    uint64_t mw, v;
    zxm_begin(l, "power.status");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_u64(l, l->result, "total_mw", &mw) || mw > (uint64_t) RATCHET_WORN_MAX)
        return ZXM_E_PROTOCOL;
    o->draw = (int64_t) mw;
    int32_t bt = zxm_get(l, l->result, "battery");
    if (bt < 0) return ZXM_E_PROTOCOL;
    if (l->tok[bt].type == W4_J_OBJ && zxm_get(l, bt, "temp_dc") >= 0) {
        if (zxm_get_i64(l, bt, "temp_dc", &o->temp) || o->temp < -RATCHET_MARGIN_MAX ||
            o->temp > RATCHET_MARGIN_MAX)
            return ZXM_E_PROTOCOL;
        o->has_temp = true;
    } else if (l->tok[bt].type != W4_J_OBJ && l->tok[bt].type != W4_J_NULL) {
        return ZXM_E_PROTOCOL;
    }
    int32_t ms = zxm_get(l, l->result, "meters");
    if (ms < 0 || l->tok[ms].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l, ms);
    for (uint32_t i = 0; i < n; i++) {
        int32_t m = zxm_at(l, ms, i);
        uint64_t dom, uj;
        if (m < 0 || zxm_get_u64(l, m, "faults", &v) || zxm_get_u64(l, m, "domain", &dom) ||
            zxm_get_u64(l, m, "lifetime_uj", &uj) || v > (uint64_t) RATCHET_MARGIN_MAX ||
            dom > (uint64_t) RATCHET_MARGIN_MAX || uj > (uint64_t) INT64_MAX)
            return ZXM_E_PROTOCOL;
        o->faults += (int64_t) v;
        if (o->nmeters < RATCHET_METERS) {
            o->domain[o->nmeters] = (int64_t) dom;
            o->uj[o->nmeters] = (int64_t) uj;
            o->nmeters++;
        }
    }
    return ZXM_OK;
}

/* sys.disk: the Hoard's volume used, used_share.used_x100's floor(10000 * (total - left) /
 * total), exact, by long division four decimal places at a time; -1 for no volume */
static int read_disk(zxm_link_t *l, int64_t *used)
{
    uint64_t total, freeb;
    zxm_begin(l, "sys.disk");
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_u64(l, l->result, "total", &total) || zxm_get_u64(l, l->result, "free", &freeb) ||
        freeb > total || total > UINT64_MAX / 10u)
        return ZXM_E_PROTOCOL;
    *used = -1;
    if (!total) return ZXM_OK;
    uint64_t rest = total - freeb, x = 0;
    for (uint32_t d = 0; d < 4u; d++) {
        rest *= 10u; /* below 10 * total, which fits */
        x = x * 10u + rest / total;
        rest %= total;
    }
    *used = (int64_t) x;
    return ZXM_OK;
}

/* Room on the grid for every reading one refresh keeps (backpressure) */
static uint32_t grid_need(const ratchet_t *r, const ratchet_read_t *o, bool load, bool mem)
{
    uint32_t need = 0;
    for (uint32_t i = 0; i < r->np; i++) {
        const ratchet_part_t *p = &r->p[i];
        if (p->kind == RATCHET_KIND_CPU) need += load && p->has_limit[RATCHET_M_LOAD];
        if (p->kind == RATCHET_KIND_MEMORY) need += mem && p->has_limit[RATCHET_M_USED];
        if (p->kind == RATCHET_KIND_DISK) need += o->used >= 0 && p->has_limit[RATCHET_M_USED];
        if (p->kind == RATCHET_KIND_BATTERY) need += o->has_temp && p->has_limit[RATCHET_M_TEMP];
        if (p->kind == RATCHET_KIND_POWER)
            need += (uint32_t) p->has_limit[RATCHET_M_DRAW] + p->has_limit[RATCHET_M_FAULTS];
    }
    return need;
}

int ratchet_refresh(ratchet_t *r)
{
    zxm_link_t *l = r->link;
    ratchet_read_t o;
    memset(&o, 0, sizeof o);
    int rc = read_disk(l, &o.used);
    if (rc) return rc;
    if ((rc = read_power(l, &o))) return rc;
    /* sys.info last, through a copy of Tidewatch's sampler: kept only when all is kept */
    tidewatch_t tide = r->tide;
    if ((rc = tidewatch_sample(&tide))) return rc;
    const tidewatch_sample_t *s = tidewatch_latest(&tide);
    if (!s || !tide.cpus) return ZXM_E_PROTOCOL;
    bool load = s->load1_x100 >= 0, mem = s->mem_used_x100 >= 0;
    if (r->ngrid + grid_need(r, &o, load, mem) > RATCHET_GRID_ROWS) return ZXM_E_STATE;
    /* everything read and checked: now the parts */
    r->tide = tide;
    r->hour = (int64_t) (s->time_ms / 3600000u);
    r->have = true;
    memcpy(r->meter_domain, o.domain, sizeof o.domain);
    memcpy(r->meter_uj, o.uj, sizeof o.uj);
    r->nmeters = o.nmeters;
    /* load per CPU in hundredths of a percent: a load of 1.00 on one CPU is 10000 */
    if (load && (rc = put_kind(r, RATCHET_KIND_CPU, RATCHET_M_LOAD,
                               (int64_t) s->load1_x100 * 100 / (int64_t) tide.cpus)))
        return rc;
    if (mem && (rc = put_kind(r, RATCHET_KIND_MEMORY, RATCHET_M_USED, s->mem_used_x100))) return rc;
    if (o.used >= 0 && (rc = put_kind(r, RATCHET_KIND_DISK, RATCHET_M_USED, o.used))) return rc;
    if (o.has_temp && (rc = put_kind(r, RATCHET_KIND_BATTERY, RATCHET_M_TEMP, o.temp))) return rc;
    if ((rc = put_kind(r, RATCHET_KIND_POWER, RATCHET_M_DRAW, o.draw))) return rc;
    return put_kind(r, RATCHET_KIND_POWER, RATCHET_M_FAULTS, o.faults);
}

int32_t ratchet_task_of(const ratchet_t *r, int64_t part, int64_t kind)
{
    for (uint32_t i = 0; i < r->ntask; i++)
        if (r->task[i].part == part && r->task[i].kind == kind) return (int32_t) i;
    return -1;
}

/* RM4, RM6: a raised task waits for the owner's word; never dropped */
static void raise_task(ratchet_t *r, int64_t part, int64_t kind)
{
    if (ratchet_task_of(r, part, kind) >= 0) return;
    if (r->ntask >= RATCHET_TASKS) {
        r->waiting++;
        return;
    }
    ratchet_task_t *t = &r->task[r->ntask++];
    memset(t, 0, sizeof *t);
    t->part = part;
    t->kind = kind;
    t->period = TREADLE_DAY_MIN; /* once a day until the owner times it */
    t->next = -1;                /* not timed until approved */
}

int ratchet_diagnose(ratchet_t *r, int64_t part, lpres_state_t *state)
{
    int32_t at = ratchet_find(r, part);
    if (at < 0 || !state) return ZXM_E_ARG;
    ratchet_part_t *p = &r->p[at];
    lpres_state_t s = ratchet_part_state(p->says, RATCHET_MEASURES);
    char text[RATCHET_NOTICE_MAX + 1];
    p->state = s;
    *state = s;
    if (s == LPRES_STATE_BOTH) {
        raise_task(r, p->part, RATCHET_TASK_INSPECT);
        snprintf(text, sizeof text,
                 "%s: one reading is inside its limits and another is outside them; "
                 "an inspection is raised and the part is not passed as sound.",
                 p->name);
        say(r, text);
    } else if (s == LPRES_STATE_FALSE) {
        raise_task(r, p->part, RATCHET_TASK_REPAIR);
        snprintf(text, sizeof text, "%s: a reading is outside its safe limits; a repair is raised.",
                 p->name);
        say(r, text);
    }
    return ZXM_OK;
}

lpres_state_t ratchet_machine_state(ratchet_t *r)
{
    lpres_state_t m = LPRES_STATE_TRUE;
    bool any = false;
    for (uint32_t i = 0; i < r->np; i++) {
        lpres_state_t s = LPRES_STATE_NEITHER;
        ratchet_diagnose(r, r->p[i].part, &s);
        if (s == LPRES_STATE_NEITHER) continue;
        any = true;
        m = lpres_conjoin(m, s);
    }
    return any ? m : LPRES_STATE_NEITHER;
}

int ratchet_log_run(ratchet_t *r, int64_t part, int64_t hours, int64_t load)
{
    int32_t at = ratchet_find(r, part);
    if (at < 0 || !ratchet_in(hours, 0, RATCHET_HOURS_MAX) ||
        !ratchet_in(load, 0, RATCHET_LOAD_FULL))
        return ZXM_E_ARG;
    ratchet_part_t *p = &r->p[at];
    if (r->nrun >= RATCHET_RUNS || !ratchet_digits(p->hours + hours, 9)) return ZXM_E_STATE;
    r->run[r->nrun++] = (ratchet_run_t){part, hours, load};
    p->hours += hours;
    return ZXM_OK;
}

int ratchet_wear_of(const ratchet_t *r, int64_t part, int64_t *worn)
{
    int64_t h[RATCHET_RUNS], ld[RATCHET_RUNS];
    uint32_t n = 0;
    if (!worn || ratchet_find(r, part) < 0) return ZXM_E_ARG;
    for (uint32_t i = 0; i < r->nrun; i++) {
        if (r->run[i].part != part) continue;
        h[n] = r->run[i].hours;
        ld[n] = r->run[i].load;
        n++;
    }
    int64_t w = ratchet_wear(h, ld, n);
    if (w == RATCHET_REFUSED) return ZXM_E_STATE;
    *worn = w;
    return ZXM_OK;
}

int ratchet_service_due(const ratchet_t *r, int64_t part, int64_t interval, int64_t load,
                        int64_t *num, int64_t *den)
{
    int64_t worn = 0;
    int rc = ratchet_wear_of(r, part, &worn);
    if (rc) return rc;
    if (!ratchet_service_left(interval, worn, load, num, den)) return ZXM_E_ARG;
    return ZXM_OK;
}

/* RM6: a task's run, timed and budgeted (treadle.next_run, budget_ok, task_ok) */
static bool timed(uint32_t idx, int64_t kind, int64_t start, int64_t period, int64_t now,
                  int64_t cost, int64_t left, int64_t *next)
{
    int64_t n = treadle_next_run(start, period, now);
    if (n < 0 || !treadle_budget_ok(cost, left) ||
        !treadle_record_fits((int64_t) idx + 1, kind, start, period, cost, 1))
        return false;
    *next = n;
    return true;
}

int ratchet_schedule(ratchet_t *r, int64_t part, int64_t kind, int64_t start, int64_t period,
                     int64_t now, int64_t cost, int64_t left, bool owner_confirmed, int64_t *next)
{
    int64_t n = 0;
    int32_t have = ratchet_task_of(r, part, kind);
    uint32_t idx = have >= 0 ? (uint32_t) have : r->ntask;
    if (!next || ratchet_find(r, part) < 0 || kind < RATCHET_TASK_INSPECT ||
        kind > RATCHET_TASK_REPAIR || !timed(idx, kind, start, period, now, cost, left, &n))
        return ZXM_E_ARG;
    if (idx >= RATCHET_TASKS || !owner_confirmed) return ZXM_E_STATE;
    if (have < 0) r->ntask++;
    r->task[idx] = (ratchet_task_t){part, kind, start, period, cost, n, true};
    *next = n;
    return ZXM_OK;
}

int ratchet_approve_task(ratchet_t *r, uint32_t idx, int64_t now, bool owner_confirmed)
{
    int64_t n = 0;
    if (idx >= r->ntask) return ZXM_E_ARG;
    ratchet_task_t *t = &r->task[idx];
    if (!timed(idx, t->kind, t->start, t->period, now, t->cost, 0, &n)) return ZXM_E_ARG;
    if (!owner_confirmed) return ZXM_E_STATE;
    t->next = n;
    t->approved = true;
    return ZXM_OK;
}

static const char *const STEP[] = {
    "No repair needed: every reading of the part is inside its limits.",
    "Isolate the power: switch the machine off at its isolator, lock it off and test that it is "
    "dead before touching it.",
    "Isolate the power and keep it isolated, then repair or replace the faulty part.",
    "Isolate the power until the part is refitted and its covers are on, then restore the power "
    "and run the self-check.",
    "Record the repair and schedule the part's next service.",
};

const char *ratchet_step_text(int64_t step)
{
    return step >= RATCHET_STEP_NONE && step <= RATCHET_STEP_RECORD ? STEP[step] : "";
}

int ratchet_walk_start(ratchet_t *r, int64_t part)
{
    int32_t at = ratchet_find(r, part);
    if (at < 0) return ZXM_E_ARG;
    lpres_state_t s = r->p[at].state;
    ratchet_walk_t *w = &r->walk;
    memset(w, 0, sizeof *w);
    w->open = true;
    w->part = (uint32_t) at;
    /* a fault, or measures that disagree, is walked; never passed as sound */
    w->fault = (s == LPRES_STATE_FALSE || s == LPRES_STATE_BOTH) ? 1 : 0;
    w->failed_stage = MOCK_STAGES;
    w->step = ratchet_repair_step(w->fault, 0, 0, 0);
    say(r, ratchet_step_text(w->step));
    return ZXM_OK;
}

int ratchet_walk_confirm(ratchet_t *r, bool person_confirmed, const int64_t *selfcheck)
{
    ratchet_walk_t *w = &r->walk;
    if (!w->open || w->step == RATCHET_REFUSED) return ZXM_E_STATE;
    if (w->step == RATCHET_STEP_RESTORE && !selfcheck) return ZXM_E_ARG;
    if (!person_confirmed) return ZXM_E_STATE; /* nothing moves without the person */
    ratchet_walk_t n = *w;
    ratchet_part_t *p = &r->p[w->part];
    switch (w->step) {
    case RATCHET_STEP_NONE:
        n.open = false;
        break;
    case RATCHET_STEP_ISOLATE:
        n.isolated = 1;
        break;
    case RATCHET_STEP_REPAIR:
        n.fixed = 1;
        break;
    case RATCHET_STEP_RESTORE: {
        /* restoring the power: a plug's setpoint 1 in 0..1, the person's own act at the
         * switch (owed -1: approved), never an AI's; shown, never sent */
        n.restore_allowed = homestead_command_ok(1, 0, -1, 0);
        n.failed_stage = mockingbird_first_failed(selfcheck);
        n.tested = mockingbird_boot_passed(selfcheck) ? 1 : 0;
        if (!n.tested) n.fixed = 0, n.isolated = 0; /* back to isolating the power */
        break;
    }
    default: /* RATCHET_STEP_RECORD: the part starts again from no reading */
        for (uint32_t m = 0; m < RATCHET_MEASURES; m++) {
            p->has_reading[m] = false;
            p->says[m] = LPRES_STATE_NEITHER;
        }
        p->state = LPRES_STATE_NEITHER;
        for (uint32_t i = r->ntask; i-- > 0;) {
            ratchet_task_t *t = &r->task[i];
            if (t->part != p->part || t->kind == RATCHET_TASK_SERVICE) continue;
            memmove(t, t + 1, (r->ntask - i - 1u) * sizeof *t); /* the job is done */
            r->ntask--;
        }
        n.open = false;
        break;
    }
    if (n.open) n.step = ratchet_repair_step(n.fault, n.isolated, n.fixed, n.tested);
    *w = n;
    say(r, n.open ? ratchet_step_text(n.step) : "The repair walk is closed.");
    return ZXM_OK;
}

int ratchet_job_cost(const int64_t *qty, const int64_t *price, const int64_t *minutes,
                     const int64_t *rate, uint32_t n, int64_t *total)
{
    int64_t s = 0;
    if (!total || ((!qty || !price || !minutes || !rate) && n)) return ZXM_E_ARG;
    for (uint32_t i = 0; i < n; i++) {
        int64_t c = ratchet_line_cost(qty[i], price[i], minutes[i], rate[i]);
        if (c == RATCHET_REFUSED || s > INT64_MAX - c) return ZXM_E_ARG;
        s += c;
    }
    *total = s;
    return ZXM_OK;
}

int ratchet_grid(const ratchet_t *r, int64_t *cell, uint32_t cap, uint32_t *n)
{
    if (!cell || !n || cap < r->ngrid) return ZXM_E_ARG;
    for (uint32_t i = 0; i < r->ngrid; i++) {
        cell[i] = ratchet_grid_cell(r->grid_sensor, r->grid_hour, r->grid_fault, r->ngrid, i);
        if (cell[i] == RATCHET_REFUSED) return ZXM_E_STATE;
    }
    *n = r->ngrid;
    return ZXM_OK;
}

int ratchet_domain_energy(const ratchet_t *r, int64_t domain, int64_t *uj)
{
    if (!uj) return ZXM_E_ARG;
    return homestead_energy(r->meter_domain, r->meter_uj, r->nmeters, domain, uj) ? ZXM_E_STATE
                                                                                  : ZXM_OK;
}
