/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sp_fleet.c — mission checks, allocation and deconfliction (roadmap S.4,
 * planning part). See sp_fleet.h for the rules.
 *
 * HONEST LIMITS: advisory plans for a person and each vehicle's own
 * autopilot/safety layer; nothing here is a control loop. Freestanding,
 * integer-only, no allocation, no libc.
 */
#include "sp_fleet.h"

int sp_purpose_valid(int p)
{
    return p >= SP_PURPOSE_DELIVERY && p <= SP_PURPOSE_AGRICULTURE;
}

/* ================= validation ================= */

int sp_path_validate(const sp_path *p, int64_t delay_ms)
{
    uint32_t i;
    if (!p) return SP_ERR_NULL;
    if (p->n < 2 || p->n > SP_MAX_WP) return SP_ERR_DEGENERATE;
    if (delay_ms < 0 || delay_ms > SP_TIME_MAX) return SP_ERR_RANGE;
    for (i = 0; i < p->n; i++) {
        if (sp_vec3_check(&p->wp[i].p)) return SP_ERR_RANGE;
        if (p->wp[i].t_ms < 0 || p->wp[i].t_ms > SP_TIME_MAX - delay_ms) return SP_ERR_RANGE;
        if (i > 0 && p->wp[i].t_ms <= p->wp[i - 1].t_ms) return SP_ERR_ORDER;
    }
    return SP_OK;
}

int sp_mission_validate(const sp_mission *m)
{
    uint32_t i;
    int st;
    if (!m) return SP_ERR_NULL;
    if (!sp_purpose_valid((int) m->purpose)) return SP_ERR_PURPOSE;
    if (m->epoch_ms < 0 || m->epoch_ms > INT64_MAX / 2) return SP_ERR_RANGE;
    if (m->has_keep_in != 0 && m->has_keep_in != 1) return SP_ERR_PARAM;
    if (m->has_keep_in && (st = sp_poly_validate(&m->keep_in)) != SP_OK) return st;
    if ((st = sp_alt_band_validate(&m->band)) != SP_OK) return st;
    if (m->clearance_mm < 0 || m->clearance_mm > SP_COORD_MAX) return SP_ERR_RANGE;
    if (m->energy_budget_mwh < 0 || m->energy_budget_mwh > SP_ENERGY_MAX) return SP_ERR_RANGE;
    if (m->n_zones > SP_MAX_ZONES) return SP_ERR_CAPACITY;
    for (i = 0; i < m->n_zones; i++) {
        const sp_zone *z = &m->zones[i];
        if (z->kind != SP_ZONE_KEEP_OUT && z->kind != SP_ZONE_PROTECTED_HABITAT)
            return SP_ERR_PARAM;
        if ((st = sp_poly_validate(&z->area)) != SP_OK) return st;
        if (z->always_active != 0 && z->always_active != 1) return SP_ERR_PARAM;
        if (!z->always_active && (z->active_from_ms < 0 || z->active_until_ms <= z->active_from_ms))
            return SP_ERR_PARAM;
        if (z->noise_buffer_mm < 0 || z->noise_buffer_mm > SP_COORD_MAX) return SP_ERR_RANGE;
        if (z->noise_ceiling_db < 0 || z->noise_ceiling_db > SP_NOISE_MAX_DB) return SP_ERR_RANGE;
    }
    return SP_OK;
}

int sp_vehicle_validate(const sp_vehicle *v)
{
    if (!v) return SP_ERR_NULL;
    if (v->vclass < SP_VEH_MULTIROTOR || v->vclass > SP_VEH_GROUND) return SP_ERR_PARAM;
    if (v->purpose_mask == 0 || (v->purpose_mask & ~SP_PURPOSE_ALL) != 0) return SP_ERR_PURPOSE;
    if (sp_vec3_check(&v->home)) return SP_ERR_RANGE;
    if (v->payload_capacity_g < 0 || v->payload_capacity_g > SP_ENERGY_MAX) return SP_ERR_RANGE;
    if (v->energy_budget_mwh < 0 || v->energy_budget_mwh > SP_ENERGY_MAX) return SP_ERR_RANGE;
    if (v->rate_dist_mwh_per_km < 0 || v->rate_dist_mwh_per_km > SP_RATE_MAX ||
        v->rate_climb_mwh_per_m < 0 || v->rate_climb_mwh_per_m > SP_RATE_MAX ||
        v->rate_time_mwh_per_h < 0 || v->rate_time_mwh_per_h > SP_RATE_MAX)
        return SP_ERR_RANGE;
    if (v->cruise_speed_mm_s < 1 || v->cruise_speed_mm_s > SP_SPEED_MAX) return SP_ERR_RANGE;
    if (v->noise_db < 0 || v->noise_db > SP_NOISE_MAX_DB) return SP_ERR_RANGE;
    return SP_OK;
}

/* ================= energy ================= */

int sp_path_energy(const sp_vehicle *v, const sp_path *p, int64_t *mwh)
{
    int64_t len = 0, climb = 0, l, dur, a, b, c, e;
    uint32_t i;
    int st;
    if (!v || !p || !mwh) return SP_ERR_NULL;
    if ((st = sp_vehicle_validate(v)) != SP_OK) return st;
    if ((st = sp_path_validate(p, 0)) != SP_OK) return st;
    for (i = 0; i + 1 < p->n; i++) {
        if ((st = sp_len3_ceil(&p->wp[i].p, &p->wp[i + 1].p, &l)) != SP_OK) return st;
        len += l; /* <= 15 * 2^31 */
        if (p->wp[i + 1].p.z > p->wp[i].p.z) climb += p->wp[i + 1].p.z - p->wp[i].p.z;
    }
    dur = p->wp[p->n - 1].t_ms - p->wp[0].t_ms;
    if ((st = sp_mul_i64(len, v->rate_dist_mwh_per_km, &a)) != SP_OK) return st;
    if ((st = sp_ceil_div_i64(a, 1000000, &a)) != SP_OK) return st;
    if ((st = sp_mul_i64(climb, v->rate_climb_mwh_per_m, &b)) != SP_OK) return st;
    if ((st = sp_ceil_div_i64(b, 1000, &b)) != SP_OK) return st;
    if ((st = sp_mul_i64(dur, v->rate_time_mwh_per_h, &c)) != SP_OK) return st;
    if ((st = sp_ceil_div_i64(c, 3600000, &c)) != SP_OK) return st;
    if ((st = sp_add_i64(a, b, &e)) != SP_OK) return st;
    if ((st = sp_add_i64(e, c, &e)) != SP_OK) return st;
    *mwh = e;
    return SP_OK;
}

/* ================= path check ================= */

static void sp_xy(const sp_vec3 *v, sp_vec2 *o)
{
    o->x = v->x;
    o->y = v->y;
}

/* Is the planar segment ab closer than m to the polygon boundary?
 * *near = 1 if some edge is at distance < m, or touches/crosses when m = 0. */
static int sp_seg_near_poly(const sp_vec2 *a, const sp_vec2 *b, const sp_poly *pg, int64_t m,
                            int *near)
{
    uint32_t k;
    int st, c;
    sp_isect_t is;
    *near = 0;
    for (k = 0; k < pg->n; k++) {
        const sp_vec2 *e0 = &pg->v[k], *e1 = &pg->v[(k + 1) % pg->n];
        if ((st = sp_seg_intersect(a, b, e0, e1, &is)) != SP_OK) return st;
        if (is != SP_ISECT_NONE) {
            *near = 1;
            return SP_OK;
        }
        if ((st = sp_dist_cmp_seg_seg2(a, b, e0, e1, m, &c)) != SP_OK) return st;
        if (c < 0) {
            *near = 1;
            return SP_OK;
        }
    }
    return SP_OK;
}

static void sp_refuse(sp_check *out, sp_refuse_t r, uint32_t idx, int32_t zone)
{
    out->ok = 0;
    out->reason = r;
    out->index = idx;
    out->zone = zone;
}

int sp_path_check(const sp_mission *m, const sp_vehicle *v, const sp_path *p, int64_t delay_ms,
                  sp_check *out)
{
    uint32_t i, z;
    int st, in;
    int64_t e;
    if (!m || !v || !p || !out) return SP_ERR_NULL;
    if ((st = sp_mission_validate(m)) != SP_OK) return st;
    if ((st = sp_vehicle_validate(v)) != SP_OK) return st;
    if ((st = sp_path_validate(p, delay_ms)) != SP_OK) return st;
    out->ok = 1;
    out->reason = SP_REFUSE_NONE;
    out->index = 0;
    out->zone = -1;
    out->energy_mwh = 0;
    if ((v->purpose_mask & SP_PURPOSE_BIT(m->purpose)) == 0) {
        sp_refuse(out, SP_REFUSE_PURPOSE, 0, -1);
        return SP_OK;
    }
    for (i = 0; i < p->n; i++) {
        if ((st = sp_alt_band_contains(&m->band, p->wp[i].p.z, &in)) != SP_OK) return st;
        if (!in) {
            sp_refuse(out, SP_REFUSE_ALTITUDE, i, -1);
            return SP_OK;
        }
    }
    for (i = 0; i + 1 < p->n; i++) {
        sp_vec2 a, b;
        int64_t ts, te;
        int near;
        sp_xy(&p->wp[i].p, &a);
        sp_xy(&p->wp[i + 1].p, &b);
        if (m->has_keep_in) {
            sp_loc_t la, lb;
            if ((st = sp_point_in_poly(&m->keep_in, &a, &la)) != SP_OK) return st;
            if ((st = sp_point_in_poly(&m->keep_in, &b, &lb)) != SP_OK) return st;
            if (la != SP_LOC_IN || lb != SP_LOC_IN) {
                sp_refuse(out, SP_REFUSE_KEEP_IN, i, -1);
                return SP_OK;
            }
            if ((st = sp_seg_near_poly(&a, &b, &m->keep_in, m->clearance_mm, &near)) != SP_OK)
                return st;
            if (near) {
                sp_refuse(out, SP_REFUSE_KEEP_IN, i, -1);
                return SP_OK;
            }
        }
        /* absolute time span of this segment; epoch <= 2^62, t <= 2^31 */
        ts = m->epoch_ms + p->wp[i].t_ms + delay_ms;
        te = m->epoch_ms + p->wp[i + 1].t_ms + delay_ms;
        for (z = 0; z < m->n_zones; z++) {
            const sp_zone *zn = &m->zones[z];
            sp_loc_t la, lb;
            sp_refuse_t why =
                zn->kind == SP_ZONE_PROTECTED_HABITAT ? SP_REFUSE_PROTECTED : SP_REFUSE_KEEP_OUT;
            if (!zn->always_active && !(ts < zn->active_until_ms && te >= zn->active_from_ms))
                continue; /* not active during this segment */
            if ((st = sp_point_in_poly(&zn->area, &a, &la)) != SP_OK) return st;
            if ((st = sp_point_in_poly(&zn->area, &b, &lb)) != SP_OK) return st;
            near = la != SP_LOC_OUT || lb != SP_LOC_OUT;
            if (!near &&
                (st = sp_seg_near_poly(&a, &b, &zn->area, m->clearance_mm, &near)) != SP_OK)
                return st;
            if (near) {
                sp_refuse(out, why, i, (int32_t) z);
                return SP_OK;
            }
            if (zn->kind == SP_ZONE_PROTECTED_HABITAT && v->noise_db > zn->noise_ceiling_db) {
                if ((st = sp_seg_near_poly(&a, &b, &zn->area, zn->noise_buffer_mm, &near)) != SP_OK)
                    return st;
                if (near) {
                    sp_refuse(out, SP_REFUSE_NOISE, i, (int32_t) z);
                    return SP_OK;
                }
            }
        }
    }
    if ((st = sp_path_energy(v, p, &e)) != SP_OK) return st;
    out->energy_mwh = e;
    if (e > v->energy_budget_mwh || e > m->energy_budget_mwh) {
        sp_refuse(out, SP_REFUSE_ENERGY, 0, -1);
        return SP_OK;
    }
    return SP_OK;
}

/* ================= allocation ================= */

static int sp_leg(const sp_vec3 *a, const sp_vec3 *b, int64_t speed, int64_t *len, int64_t *ms)
{
    int64_t t;
    int st;
    if ((st = sp_len3_ceil(a, b, len)) != SP_OK) return st;
    if ((st = sp_mul_i64(*len, 1000, &t)) != SP_OK) return st;
    if ((st = sp_ceil_div_i64(t, speed, ms)) != SP_OK) return st;
    if (*ms < 1) *ms = 1;
    return SP_OK;
}

static int sp_task_validate(const sp_task *t)
{
    if (!t) return SP_ERR_NULL;
    if (!sp_purpose_valid((int) t->purpose)) return SP_ERR_PURPOSE;
    if (sp_vec3_check(&t->pickup) || sp_vec3_check(&t->dropoff)) return SP_ERR_RANGE;
    if (t->payload_g < 0 || t->payload_g > SP_ENERGY_MAX) return SP_ERR_RANGE;
    return SP_OK;
}

int sp_route_build(const sp_vehicle *v, const sp_task *t, int64_t depart_ms, sp_path *out)
{
    const sp_vec3 *pts[4];
    int64_t now = depart_ms, len, ms;
    uint32_t i;
    int st;
    if (!v || !t || !out) return SP_ERR_NULL;
    if ((st = sp_vehicle_validate(v)) != SP_OK) return st;
    if ((st = sp_task_validate(t)) != SP_OK) return st;
    if (depart_ms < 0 || depart_ms > SP_TIME_MAX) return SP_ERR_RANGE;
    pts[0] = &v->home;
    pts[1] = &t->pickup;
    pts[2] = &t->dropoff;
    pts[3] = &v->home;
    for (i = 0; i < 4; i++) {
        if (i > 0) {
            if ((st = sp_leg(pts[i - 1], pts[i], v->cruise_speed_mm_s, &len, &ms)) != SP_OK)
                return st;
            now += ms;
            if (now > SP_TIME_MAX) return SP_ERR_RANGE;
        }
        out->wp[i].p.x = pts[i]->x;
        out->wp[i].p.y = pts[i]->y;
        out->wp[i].p.z = pts[i]->z;
        out->wp[i].t_ms = now;
    }
    out->n = 4;
    return SP_OK;
}

int sp_alloc_pair(const sp_vehicle *v, const sp_task *t, const sp_alloc_cfg *cfg,
                  sp_unassigned_t *status, int64_t *cost)
{
    sp_path route;
    int64_t e, len = 0, l, dm, c1, c2;
    uint32_t i;
    int st;
    if (!v || !t || !cfg || !status || !cost) return SP_ERR_NULL;
    if ((st = sp_vehicle_validate(v)) != SP_OK) return st;
    if ((st = sp_task_validate(t)) != SP_OK) return st;
    if (cfg->w_dist_per_m < 0 || cfg->w_dist_per_m > ((int64_t) 1 << 20) ||
        cfg->w_energy_per_mwh < 0 || cfg->w_energy_per_mwh > ((int64_t) 1 << 20))
        return SP_ERR_RANGE;
    *cost = 0;
    if ((v->purpose_mask & SP_PURPOSE_BIT(t->purpose)) == 0 ||
        (cfg->mission && cfg->mission->purpose != t->purpose)) {
        *status = SP_UA_PURPOSE;
        return SP_OK;
    }
    if (t->payload_g > v->payload_capacity_g) {
        *status = SP_UA_PAYLOAD;
        return SP_OK;
    }
    if ((st = sp_route_build(v, t, cfg->depart_ms, &route)) != SP_OK) return st;
    if ((st = sp_path_energy(v, &route, &e)) != SP_OK) return st;
    if (e > v->energy_budget_mwh) {
        *status = SP_UA_ENERGY;
        return SP_OK;
    }
    if (cfg->mission) {
        sp_check ck;
        if ((st = sp_path_check(cfg->mission, v, &route, 0, &ck)) != SP_OK) return st;
        if (!ck.ok) {
            *status = SP_UA_ROUTE;
            return SP_OK;
        }
    }
    for (i = 0; i + 1 < route.n; i++) {
        if ((st = sp_len3_ceil(&route.wp[i].p, &route.wp[i + 1].p, &l)) != SP_OK) return st;
        len += l;
    }
    if ((st = sp_ceil_div_i64(len, 1000, &dm)) != SP_OK) return st;
    if ((st = sp_mul_i64(dm, cfg->w_dist_per_m, &c1)) != SP_OK) return st;
    if ((st = sp_mul_i64(e, cfg->w_energy_per_mwh, &c2)) != SP_OK) return st;
    if ((st = sp_add_i64(c1, c2, &c1)) != SP_OK) return st;
    if (c1 > SP_COST_MAX) return SP_ERR_OVERFLOW;
    *status = SP_UA_ASSIGNED;
    *cost = c1;
    return SP_OK;
}

int sp_hungarian(uint32_t n, const int64_t cost[SP_HUNG_MAX][SP_HUNG_MAX], int32_t *row_to_col,
                 int64_t *total)
{
    /* potentials method, 1-indexed; p[j] = row matched to column j */
    int64_t u[SP_HUNG_MAX + 1], v[SP_HUNG_MAX + 1], minv[SP_HUNG_MAX + 1];
    uint32_t p[SP_HUNG_MAX + 1], way[SP_HUNG_MAX + 1];
    int used[SP_HUNG_MAX + 1];
    const int64_t INF = (int64_t) 1 << 62;
    uint32_t i, j;
    int64_t sum = 0;
    if (!cost || !row_to_col || !total) return SP_ERR_NULL;
    if (n < 1 || n > SP_HUNG_MAX) return SP_ERR_CAPACITY;
    for (i = 0; i < n; i++)
        for (j = 0; j < n; j++)
            if (cost[i][j] < 0 || cost[i][j] > SP_HUNG_COST_MAX) return SP_ERR_RANGE;
    for (j = 0; j <= n; j++) {
        u[j] = 0;
        v[j] = 0;
        p[j] = 0;
        way[j] = 0;
    }
    for (i = 1; i <= n; i++) {
        uint32_t j0 = 0;
        p[0] = i;
        for (j = 0; j <= n; j++) {
            minv[j] = INF;
            used[j] = 0;
        }
        do {
            uint32_t i0 = p[j0], j1 = 0;
            int64_t delta = INF;
            used[j0] = 1;
            for (j = 1; j <= n; j++) {
                if (!used[j]) {
                    int64_t cur = cost[i0 - 1][j - 1] - u[i0] - v[j];
                    if (cur < minv[j]) {
                        minv[j] = cur;
                        way[j] = j0;
                    }
                    if (minv[j] < delta) {
                        delta = minv[j];
                        j1 = j;
                    }
                }
            }
            for (j = 0; j <= n; j++) {
                if (used[j]) {
                    u[p[j]] += delta;
                    v[j] -= delta;
                } else {
                    minv[j] -= delta;
                }
            }
            j0 = j1;
        } while (p[j0] != 0);
        do {
            uint32_t j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0 != 0);
    }
    for (j = 1; j <= n; j++) {
        row_to_col[p[j] - 1] = (int32_t) (j - 1);
        sum += cost[p[j] - 1][j - 1];
    }
    *total = sum;
    return SP_OK;
}

int sp_allocate(const sp_vehicle *v, uint32_t nv, const sp_task *t, uint32_t nt,
                const sp_alloc_cfg *cfg, sp_alloc_result *out)
{
    int64_t cm[SP_HUNG_MAX][SP_HUNG_MAX];
    int32_t r2c[SP_HUNG_MAX];
    sp_unassigned_t best[SP_MAX_TASKS];
    uint32_t i, j, n;
    int64_t big, total;
    int st;
    if (!v || !t || !cfg || !out) return SP_ERR_NULL;
    if (nv < 1 || nt < 1 || nv > SP_MAX_VEHICLES || nt > SP_MAX_TASKS) return SP_ERR_CAPACITY;
    if (cfg->mission && (st = sp_mission_validate(cfg->mission)) != SP_OK) return st;
    n = nv > nt ? nv : nt;
    /* BIG = n * SP_COST_MAX + 1 > any total of n feasible costs */
    big = (int64_t) n * SP_COST_MAX + 1;
    for (j = 0; j < nt; j++) best[j] = SP_UA_PURPOSE;
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            sp_unassigned_t s;
            int64_t c;
            cm[i][j] = big;
            if (i >= nv || j >= nt) continue;
            if ((st = sp_alloc_pair(&v[i], &t[j], cfg, &s, &c)) != SP_OK) return st;
            if (s == SP_UA_ASSIGNED) {
                cm[i][j] = c;
                best[j] = SP_UA_OUTNUMBERED;
            } else if (best[j] != SP_UA_OUTNUMBERED && s > best[j]) {
                best[j] = s;
            }
        }
    }
    if ((st = sp_hungarian(n, (const int64_t(*)[SP_HUNG_MAX]) cm, r2c, &total)) != SP_OK) return st;
    out->advisory = 1;
    out->requires_human_approval = 1;
    out->n_vehicles = nv;
    out->n_tasks = nt;
    out->n_assigned = 0;
    out->total_cost = 0;
    for (j = 0; j < nt; j++) {
        out->task_vehicle[j] = -1;
        out->task_status[j] = best[j];
        out->task_cost[j] = 0;
    }
    for (i = 0; i < nv; i++) {
        uint32_t c = (uint32_t) r2c[i];
        out->vehicle_task[i] = -1;
        if (c < nt && cm[i][c] < big) {
            out->vehicle_task[i] = (int32_t) c;
            out->task_vehicle[c] = (int32_t) i;
            out->task_status[c] = SP_UA_ASSIGNED;
            out->task_cost[c] = cm[i][c];
            out->total_cost += cm[i][c];
            out->n_assigned++;
        }
    }
    return SP_OK;
}

/* ================= deconfliction ================= */

static int sp_bmul_ii(sp_big *r, int64_t a, int64_t b)
{
    sp_big x, y;
    sp_big_from_i64(&x, a);
    sp_big_from_i64(&y, b);
    return sp_big_mul(r, &x, &y);
}

/* r += a * b */
static int sp_bmac(sp_big *r, const sp_big *a, const sp_big *b)
{
    sp_big t;
    int st = sp_big_mul(&t, a, b);
    if (st) return st;
    return sp_big_add(r, r, &t);
}

int sp_seg_conflict(const sp_waypoint *a0, const sp_waypoint *a1, int64_t da, const sp_waypoint *b0,
                    const sp_waypoint *b1, int64_t db, int64_t sep, int *loss, int64_t *t_from,
                    int64_t *t_until)
{
    int64_t ta0, ta1, tb0, tb1, t0, t1, DA, DB, L;
    sp_big P[3], Q[3], a, b, c, thr, tmp, tmp2, bDA, bDB;
    int k, st, cmpv;
    if (!a0 || !a1 || !b0 || !b1 || !loss || !t_from || !t_until) return SP_ERR_NULL;
    if (sep < 1 || sep > 2 * SP_COORD_MAX) return SP_ERR_RANGE;
    if (sp_vec3_check(&a0->p) || sp_vec3_check(&a1->p) || sp_vec3_check(&b0->p) ||
        sp_vec3_check(&b1->p))
        return SP_ERR_RANGE;
    if (da < 0 || db < 0 || da > SP_TIME_MAX || db > SP_TIME_MAX) return SP_ERR_RANGE;
    ta0 = a0->t_ms + da;
    ta1 = a1->t_ms + da;
    tb0 = b0->t_ms + db;
    tb1 = b1->t_ms + db;
    if (a0->t_ms < 0 || b0->t_ms < 0 || ta1 > SP_TIME_MAX || tb1 > SP_TIME_MAX) return SP_ERR_RANGE;
    if (ta1 <= ta0 || tb1 <= tb0) return SP_ERR_ORDER;
    *loss = 0;
    t0 = ta0 > tb0 ? ta0 : tb0;
    t1 = ta1 < tb1 ? ta1 : tb1;
    *t_from = t0;
    *t_until = t1;
    if (t0 > t1) return SP_OK; /* never active at the same time */
    DA = ta1 - ta0;
    DB = tb1 - tb0;
    L = t1 - t0;
    sp_big_from_i64(&bDA, DA);
    sp_big_from_i64(&bDB, DB);
    /* Scaled relative position n(s) = DA*DB*(posA - posB) at t0 + s is
     * P + Q s with
     *   P = DB*(A0*DA + VA*(t0-ta0)) - DA*(B0*DB + VB*(t0-tb0))
     *   Q = DB*VA - DA*VB                                          */
    for (k = 0; k < 3; k++) {
        int64_t A0 = k == 0 ? a0->p.x : (k == 1 ? a0->p.y : a0->p.z);
        int64_t A1 = k == 0 ? a1->p.x : (k == 1 ? a1->p.y : a1->p.z);
        int64_t B0 = k == 0 ? b0->p.x : (k == 1 ? b0->p.y : b0->p.z);
        int64_t B1 = k == 0 ? b1->p.x : (k == 1 ? b1->p.y : b1->p.z);
        int64_t VA = A1 - A0, VB = B1 - B0, pa, pb, x, y;
        if ((st = sp_mul_i64(A0, DA, &x)) || (st = sp_mul_i64(VA, t0 - ta0, &y)) ||
            (st = sp_add_i64(x, y, &pa)))
            return st;
        if ((st = sp_mul_i64(B0, DB, &x)) || (st = sp_mul_i64(VB, t0 - tb0, &y)) ||
            (st = sp_add_i64(x, y, &pb)))
            return st;
        if ((st = sp_bmul_ii(&tmp, DB, pa)) || (st = sp_bmul_ii(&tmp2, DA, pb)) ||
            (st = sp_big_sub(&P[k], &tmp, &tmp2)))
            return st;
        if ((st = sp_bmul_ii(&tmp, DB, VA)) || (st = sp_bmul_ii(&tmp2, DA, VB)) ||
            (st = sp_big_sub(&Q[k], &tmp, &tmp2)))
            return st;
    }
    sp_big_from_i64(&a, 0);
    sp_big_from_i64(&b, 0);
    sp_big_from_i64(&c, 0);
    for (k = 0; k < 3; k++) {
        if ((st = sp_bmac(&a, &Q[k], &Q[k])) || (st = sp_bmac(&b, &P[k], &Q[k])) ||
            (st = sp_bmac(&c, &P[k], &P[k])))
            return st;
    }
    /* thr = sep^2 * (DA*DB)^2 */
    if ((st = sp_big_mul(&tmp, &bDA, &bDB)) || (st = sp_big_mul(&tmp, &tmp, &tmp)) ||
        (st = sp_bmul_ii(&tmp2, sep, sep)) || (st = sp_big_mul(&thr, &tmp, &tmp2)))
        return st;
    if (sp_big_sign(&a) == 0 || sp_big_sign(&b) >= 0) {
        /* constant relative position, or moving apart: minimum at s = 0 */
        cmpv = sp_big_cmp(&c, &thr);
    } else {
        sp_big negb, aL, bL;
        sp_big_from_i64(&tmp, L);
        sp_big_from_i64(&tmp2, 0);
        if ((st = sp_big_sub(&negb, &tmp2, &b)) || (st = sp_big_mul(&aL, &a, &tmp))) return st;
        if (sp_big_cmp(&negb, &aL) >= 0) {
            /* still closing at s = L: f(L) = c + 2 b L + a L^2 */
            if ((st = sp_big_mul(&bL, &b, &tmp)) || (st = sp_big_add(&tmp2, &c, &bL)) ||
                (st = sp_big_add(&tmp2, &tmp2, &bL)) || (st = sp_bmac(&tmp2, &aL, &tmp)))
                return st;
            cmpv = sp_big_cmp(&tmp2, &thr);
        } else {
            /* interior minimum c - b^2/a: compare a*c - b^2 with a*thr */
            sp_big ac, bb, at;
            if ((st = sp_big_mul(&ac, &a, &c)) || (st = sp_big_mul(&bb, &b, &b)) ||
                (st = sp_big_sub(&ac, &ac, &bb)) || (st = sp_big_mul(&at, &a, &thr)))
                return st;
            cmpv = sp_big_cmp(&ac, &at);
        }
    }
    *loss = cmpv < 0;
    return SP_OK;
}

int sp_paths_conflict(const sp_path *a, int64_t da, const sp_path *b, int64_t db, int64_t sep,
                      int *loss, sp_conflict *first)
{
    uint32_t i, j;
    int st;
    if (!a || !b || !loss) return SP_ERR_NULL;
    if ((st = sp_path_validate(a, da)) != SP_OK) return st;
    if ((st = sp_path_validate(b, db)) != SP_OK) return st;
    *loss = 0;
    for (i = 0; i + 1 < a->n; i++) {
        for (j = 0; j + 1 < b->n; j++) {
            int64_t f, u;
            int l;
            if ((st = sp_seg_conflict(&a->wp[i], &a->wp[i + 1], da, &b->wp[j], &b->wp[j + 1], db,
                                      sep, &l, &f, &u)) != SP_OK)
                return st;
            if (l) {
                *loss = 1;
                if (first) {
                    first->seg_a = i;
                    first->seg_b = j;
                    first->t_from = f;
                    first->t_until = u;
                }
                return SP_OK;
            }
        }
    }
    return SP_OK;
}

int sp_deconflict(const sp_mission *m, const sp_vehicle *veh, const sp_path *paths, uint32_t n,
                  const sp_deconf_cfg *cfg, sp_deconf_plan *out)
{
    uint32_t order[SP_MAX_VEHICLES], fixed[SP_MAX_VEHICLES], nfixed = 0, i, j, k;
    int st;
    if (!m || !veh || !paths || !cfg || !out) return SP_ERR_NULL;
    if (n < 1 || n > SP_MAX_VEHICLES) return SP_ERR_CAPACITY;
    if (cfg->sep_mm < 1 || cfg->sep_mm > 2 * SP_COORD_MAX) return SP_ERR_RANGE;
    if (cfg->step_ms < 1 || cfg->step_ms > SP_TIME_MAX || cfg->max_steps > SP_STEPS_MAX)
        return SP_ERR_RANGE;
    if ((st = sp_mission_validate(m)) != SP_OK) return st;
    for (i = 0; i < n; i++) {
        if ((st = sp_vehicle_validate(&veh[i])) != SP_OK) return st;
        if ((st = sp_path_validate(&paths[i], 0)) != SP_OK) return st;
    }
    out->advisory = 1;
    out->requires_human_approval = 1;
    out->n = n;
    out->n_cleared = out->n_unresolved = out->n_refused = 0;
    out->n_conflicts_initial = out->n_conflicts_stored = 0;
    /* conflicts in the plan as given */
    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            sp_conflict c;
            int loss;
            if ((st = sp_paths_conflict(&paths[i], 0, &paths[j], 0, cfg->sep_mm, &loss, &c)) !=
                SP_OK)
                return st;
            if (!loss) continue;
            out->n_conflicts_initial++;
            if (out->n_conflicts_stored < SP_MAX_CONFLICTS) {
                sp_conflict *d = &out->conflicts[out->n_conflicts_stored++];
                d->a = i;
                d->b = j;
                d->seg_a = c.seg_a;
                d->seg_b = c.seg_b;
                d->t_from = c.t_from;
                d->t_until = c.t_until;
            }
        }
    }
    /* stable order by priority, highest first */
    for (i = 0; i < n; i++) {
        uint32_t x = i;
        j = i;
        while (j > 0 && veh[order[j - 1]].priority < veh[x].priority) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = x;
    }
    for (k = 0; k < n; k++) {
        uint32_t vi = order[k], step;
        sp_check ck;
        int placed = 0;
        int64_t last = paths[vi].wp[paths[vi].n - 1].t_ms;
        out->delay_ms[vi] = 0;
        out->refuse[vi] = SP_REFUSE_NONE;
        if ((st = sp_path_check(m, &veh[vi], &paths[vi], 0, &ck)) != SP_OK) return st;
        if (!ck.ok) {
            out->status[vi] = SP_DC_REFUSED;
            out->refuse[vi] = ck.reason;
            out->n_refused++;
            continue;
        }
        for (step = 0; step <= cfg->max_steps && !placed; step++) {
            int64_t delay;
            int clash = 0;
            if (sp_mul_i64((int64_t) step, cfg->step_ms, &delay) != SP_OK ||
                delay > SP_TIME_MAX - last)
                break; /* would leave the time range: stop trying */
            if (step > 0) {
                if ((st = sp_path_check(m, &veh[vi], &paths[vi], delay, &ck)) != SP_OK) return st;
                if (!ck.ok) continue; /* e.g. delay runs into a nesting season */
            }
            for (j = 0; j < nfixed && !clash; j++) {
                uint32_t fj = fixed[j];
                int loss;
                if ((st = sp_paths_conflict(&paths[vi], delay, &paths[fj], out->delay_ms[fj],
                                            cfg->sep_mm, &loss, (sp_conflict *) 0)) != SP_OK)
                    return st;
                clash = loss;
            }
            if (!clash) {
                placed = 1;
                out->delay_ms[vi] = delay;
            }
        }
        if (placed) {
            out->status[vi] = SP_DC_CLEARED;
            fixed[nfixed++] = vi;
            out->n_cleared++;
        } else {
            out->status[vi] = SP_DC_UNRESOLVED;
            out->n_unresolved++;
        }
    }
    return SP_OK;
}
