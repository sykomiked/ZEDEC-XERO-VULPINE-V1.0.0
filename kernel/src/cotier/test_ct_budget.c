/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_ct_budget.c — tier-3 budget controller (ct_budget.h): the cost map,
 * h_t, the storage inequality and its commit hook, and the controller on a
 * simulated machine whose ambient load rises, holds and falls (power,
 * stalls, a lagging temperature, deterministic noise): h_t must stay >= 0
 * with no level chatter, while the same trace run flat out goes negative.
 *
 *   gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc/tensor -Isrc/cotier src/cotier/test_ct_budget.c \
 *       src/cotier/ct_budget.c src/tensor/zt.c src/tensor/zt_isf.c -lm -o /tmp/test_ct_budget
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ct_budget.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        int ok_ = (c) ? 1 : 0;                                                                     \
        if (!ok_) failures++;                                                                      \
        printf(ok_ ? "[PASS] " : "[FAIL] ");                                                       \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
    } while (0)

#define Q(x) ((zt_fx) ((x) * 65536.0))

static ct_budget_cfg_t base_cfg(void)
{
    ct_budget_cfg_t c;
    memset(&c, 0, sizeof c);
    c.eta = Q(1.0);
    c.delta = Q(0.05);
    c.N = 16;
    c.w_watts = Q(2.0);
    c.w_stall = Q(0.5);
    c.w_temp = Q(1.0);
    c.watts_ref_mw = 100000;
    c.headroom_ref_mc = 20000;
    c.h_low = Q(0.25);
    c.h_high = Q(0.6);
    c.hold = 8;
    c.levels = 6;
    c.batch_max = 64;
    c.batch_min = 1;
    c.ctx_max = 4096;
    c.ctx_min = 512;
    c.Q0 = 0;
    return c;
}

/* ---- the simulated machine ---- */

typedef struct {
    uint32_t t;
    const ct_budget_t *b;
    double ambient_w, temp_headroom, noise;
    bool flat_out; /* ignore the controller: level 0 always */
    uint64_t seed;
    bool fail;
    uint32_t speed; /* the ambient trace runs this many times faster (0 = 1) */
} sim_t;

static double ambient(uint32_t t)
{
    if (t < 100) return 0;
    if (t < 400) return 60.0 * (t - 100) / 300.0;
    if (t < 600) return 60.0;
    if (t < 800) return 60.0 * (800 - t) / 200.0;
    return 0;
}

static double noise(uint64_t *s)
{
    uint64_t z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return (z >> 11) * (1.0 / 9007199254740992.0) * 2 - 1;
}

static bool metrics(void *ctx, ct_metrics_t *m)
{
    sim_t *s = ctx;
    if (s->fail) return false;
    uint32_t batch = s->flat_out ? 64 : s->b->batch, cx = s->flat_out ? 4096 : s->b->ctx;
    bool on = s->flat_out || s->b->accel_on;
    double p = 10.0 + ambient(s->t * (s->speed ? s->speed : 1)) + (on ? 1.2 * batch : 0.0) +
               2.0 * noise(&s->seed);
    double target = 40000.0 - 300.0 * p; /* milli-degrees of headroom at equilibrium */
    s->temp_headroom += 0.2 * (target - s->temp_headroom); /* thermal lag */
    double stall = 0.05 + 0.35 * cx / 4096.0 * (on ? 1.0 : 0.2) + 0.01 * noise(&s->seed);
    m->present = CT_M_WATTS | CT_M_STALL | CT_M_TEMP;
    m->watts_mw = (uint32_t) (p * 1000);
    m->total_cycles = 1000000;
    m->stall_cycles = (uint64_t) (stall * 1000000);
    m->headroom_mc = (int32_t) s->temp_headroom;
    return true;
}

typedef struct {
    double h_min;
    uint32_t changes, reversals, chatter, max_level, accel_off_cycles;
    double final_h;
    uint32_t final_level;
} trace_t;

static void run(const ct_budget_cfg_t *cfg, bool flat_out, uint32_t speed, trace_t *tr)
{
    ct_budget_t b;
    ct_budget_init(&b, cfg);
    sim_t s = {0, &b, 0, 40000, 0, flat_out, 77, false, speed};
    uint32_t last_change = 0, last_dir = 0, prev_level = 0;
    memset(tr, 0, sizeof *tr);
    tr->h_min = 1e9;
    for (s.t = 0; s.t < 1000; s.t++) {
        zt_fx S = Q(0.2 + 1.0 * (flat_out ? 64 : b.batch) / 64.0);
        double h = ct_budget_step(&b, metrics, &s, S) / 65536.0;
        if (h < tr->h_min) tr->h_min = h;
        if (b.level != prev_level) {
            uint32_t dir = b.level > prev_level ? 1 : 2;
            if (last_dir && dir != last_dir && s.t - last_change < 2 * cfg->hold) tr->chatter++;
            last_dir = dir;
            last_change = s.t;
            prev_level = b.level;
        }
        if (b.level > tr->max_level) tr->max_level = b.level;
        if (getenv("CT_TRACE"))
            printf("t %4u lvl %u h %.3f C %.3f Q %.3f relief %.3f %.3f %.3f\n", s.t, b.level, h,
                   b.C / 65536.0, b.Q / 65536.0, b.relief[0] / 65536.0, b.relief[1] / 65536.0,
                   b.relief[2] / 65536.0);
        tr->accel_off_cycles += !b.accel_on;
        tr->final_h = h;
    }
    tr->final_level = b.level;
    tr->changes = b.changes;
    tr->reversals = b.reversals;
}

static int32_t commit_calls = 0;
static int32_t commit_ok(void *ctx, const void *rec, uint32_t len)
{
    (void) ctx;
    (void) rec;
    (void) len;
    commit_calls++;
    return 0;
}
static int32_t commit_fail(void *ctx, const void *rec, uint32_t len)
{
    (void) ctx;
    (void) rec;
    (void) len;
    commit_calls++;
    return -7;
}

static bool hot_metrics(void *ctx, ct_metrics_t *m)
{
    (void) ctx;
    m->present = CT_M_WATTS;
    m->watts_mw = 400000;
    return true;
}

static bool fail_metrics(void *ctx, ct_metrics_t *m)
{
    (void) ctx;
    m->present = CT_M_WATTS;
    m->watts_mw = 1000000;
    return false;
}

int main(void)
{
    ct_budget_cfg_t cfg = base_cfg();
    ct_budget_t b;

    /* configuration */
    ct_budget_cfg_t bad = cfg;
    bad.levels = 1;
    CHECK(!ct_budget_init(&b, &bad), "1 level refused");
    bad = cfg;
    bad.h_low = bad.h_high;
    CHECK(!ct_budget_init(&b, &bad), "empty hysteresis band refused");
    bad = cfg;
    bad.delta = Q(1.5);
    CHECK(!ct_budget_init(&b, &bad), "delta > 1 refused");
    CHECK(ct_budget_init(&b, &cfg), "base configuration accepted");
    CHECK(b.batch == 64 && b.ctx == 4096 && b.accel_on, "starts flat out");

    /* the cost map */
    ct_metrics_t m;
    memset(&m, 0, sizeof m);
    CHECK(ct_cost(&cfg, &m) == 0, "no metrics: cost 0");
    m.present = CT_M_WATTS;
    m.watts_mw = 50000;
    CHECK(ct_cost(&cfg, &m) == Q(1.0), "50 W of 100: 2.0 * 0.5 = 1.0");
    m.watts_mw = 4000000000u;
    CHECK(ct_cost(&cfg, &m) == Q(8.0), "power load capped at 4x the reference");
    m.present = CT_M_STALL;
    m.stall_cycles = 1ull << 62;
    m.total_cycles = 1ull << 63;
    CHECK(ct_cost(&cfg, &m) == Q(0.25), "half the cycles stalled: 0.5 * 0.5 (64-bit counts)");
    m.stall_cycles = 5;
    m.total_cycles = 0;
    CHECK(ct_cost(&cfg, &m) == 0, "zero total cycles: stall term absent");
    m.present = CT_M_TEMP;
    m.headroom_mc = 5000;
    CHECK(ct_cost(&cfg, &m) == Q(0.75), "5 of 20 degrees headroom: 1.0 * 0.75");
    m.headroom_mc = -3000;
    CHECK(ct_cost(&cfg, &m) == Q(1.0), "past the throttle point: full thermal cost");
    m.headroom_mc = 90000;
    CHECK(ct_cost(&cfg, &m) == 0, "cool: no thermal cost");

    /* h_t = eta ln N - delta Q - C */
    cfg.Q0 = Q(10.0);
    ct_budget_init(&b, &cfg);
    double want = log(16.0) - 0.05 * 10.0 - 0.5;
    double got = ct_budget_headroom(&b, Q(0.5)) / 65536.0;
    CHECK(fabs(got - want) < 2e-4, "h = ln 16 - 0.05 * 10 - 0.5 = %.4f (got %.4f)", want, got);
    ct_budget_step(&b, fail_metrics, 0, 0);
    CHECK(b.C == 0, "a failing metrics callback adds no cost");
    ct_budget_step(&b, 0, 0, 0);
    CHECK(b.C == 0, "no callback adds no cost");

    /* storage inequality: delta Q + C <= eta F */
    cfg.Q0 = Q(10.0);
    ct_budget_init(&b, &cfg);
    b.C = Q(0.5); /* delta Q = 0.5, so the bar is F >= 1.0 */
    CHECK(!ct_storage_ok(&b, Q(0.99)) && ct_storage_ok(&b, Q(1.0)),
          "storage inequality: F 0.99 refused, 1.0 accepted");
    commit_calls = 0;
    int32_t r1 = ct_budget_commit(&b, Q(0.5), commit_ok, 0, "x", 1);
    int32_t r2 = ct_budget_commit(&b, Q(2.0), commit_ok, 0, "x", 1);
    int32_t r3 = ct_budget_commit(&b, Q(2.0), commit_fail, 0, "x", 1);
    CHECK(r1 == CT_COMMIT_REFUSED && r2 == CT_COMMIT_OK && r3 == -7 && commit_calls == 2 &&
              b.commits == 1 && b.refusals == 1,
          "commit hook: refused without calling, committed, ledger error passed through");
    CHECK(ct_budget_commit(&b, Q(2.0), 0, 0, 0, 0) == CT_COMMIT_EARG,
          "commit without a hook: EARG");

    /* the controller on the rising-cost trace */
    cfg = base_cfg();
    trace_t flat, ctl, nohyst;
    run(&cfg, true, 1, &flat);
    run(&cfg, false, 1, &ctl);
    printf("       flat out:   h_min %.3f\n", flat.h_min);
    printf("       controlled: h_min %.3f, %u level changes, %u reversals, %u chatter, max level "
           "%u, accelerator off %u of 1000 cycles, final h %.3f\n",
           ctl.h_min, ctl.changes, ctl.reversals, ctl.chatter, ctl.max_level, ctl.accel_off_cycles,
           ctl.final_h);
    CHECK(flat.h_min < 0, "the trace is dangerous: flat out, h goes to %.3f", flat.h_min);
    CHECK(ctl.h_min >= 0, "controlled: h_t >= 0 on every one of 1000 cycles (min %.3f)", ctl.h_min);
    CHECK(ctl.chatter == 0, "controlled: no reversal within 2 * hold cycles of the last change");
    CHECK(ctl.reversals <= 3, "controlled: at most 3 reversals over rise, hold and fall (%u)",
          ctl.reversals);
    CHECK(ctl.max_level >= 2, "controlled: it did throttle (max level %u)", ctl.max_level);
    {
        /* a cheaper machine (half the power weight): level 0 is comfortable
         * when idle, so after the ambient load passes it must come back */
        ct_budget_cfg_t c2 = cfg;
        c2.w_watts = Q(1.0);
        ct_budget_t e;
        ct_budget_init(&e, &c2);
        sim_t s = {0, &e, 0, 40000, 0, false, 77, false, 1};
        uint32_t maxl = 0;
        double hmin = 1e9;
        for (s.t = 0; s.t < 1000; s.t++) {
            double h = ct_budget_step(&e, metrics, &s, Q(1.0)) / 65536.0;
            if (e.level > maxl) maxl = e.level;
            if (h < hmin) hmin = h;
        }
        CHECK(maxl >= 1 && hmin >= 0 && e.level == 0 && e.batch == 64 && e.accel_on,
              "cheaper machine: throttles to level %u, h_min %.3f, back to full speed at the end",
              maxl, hmin);
    }
    printf("       (with the base weights, idle level 0 settles at h ~ 0.52, inside the band, so "
           "the controller ends at level %u: by design)\n",
           ctl.final_level);
    {
        trace_t fast, fastflat;
        run(&cfg, true, 4, &fastflat);
        run(&cfg, false, 4, &fast);
        printf("       4x faster ramp: flat out h_min %.3f; controlled h_min %.3f, %u changes, %u "
               "reversals, %u chatter, max level %u\n",
               fastflat.h_min, fast.h_min, fast.changes, fast.reversals, fast.chatter,
               fast.max_level);
        CHECK(fastflat.h_min < 0 && fast.h_min >= 0 && fast.chatter == 0,
              "4x faster ramp: h_t >= 0 (min %.3f), no chatter", fast.h_min);
    }
    ct_budget_cfg_t nh = cfg;
    nh.h_high = nh.h_low + 1;
    nh.hold = 1;
    run(&nh, false, 1, &nohyst);
    printf("       narrow band, hold 1: h_min %.3f, %u changes, %u reversals, %u chatter\n",
           nohyst.h_min, nohyst.changes, nohyst.reversals, nohyst.chatter);
    CHECK(nohyst.changes >= ctl.changes, "hysteresis and hold make fewer level changes (%u vs %u)",
          ctl.changes, nohyst.changes);

    /* sustained overload: the ladder walks to the top and turns the accelerator off */
    {
        ct_budget_t o;
        ct_budget_init(&o, &cfg);
        uint32_t seen_batch[8], seen_ctx[8];
        for (uint32_t t = 0; t < 20; t++) {
            ct_budget_step(&o, hot_metrics, 0, 0);
            if (t < 8) {
                seen_batch[t] = o.batch;
                seen_ctx[t] = o.ctx;
            }
        }
        CHECK(o.level == cfg.levels - 1 && !o.accel_on && o.batch == 2 && o.ctx == 512,
              "sustained overload: top level, accelerator off, batch %u, context %u", o.batch,
              o.ctx);
        CHECK(seen_batch[0] == 16 && seen_ctx[0] == 4096 - (3584 * 2) / 5,
              "h < 0 throttles two levels at once (batch %u, context %u after one cycle)",
              seen_batch[0], seen_ctx[0]);
    }

    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
