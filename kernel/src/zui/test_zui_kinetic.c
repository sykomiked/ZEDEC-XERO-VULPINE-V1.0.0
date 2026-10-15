/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_kinetic.c — the kinetic state engine for vector app icons
 * (zui_kinetic.h KI1..KI8). */
#include <stdio.h>
#include <string.h>

#include "zui_kinetic.h"
#include "zui_motion.h"
#include "zui_tokens.h"

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL (%s:%d) ", __FILE__, __LINE__);                                         \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

#define ONE  ZUI_KINETIC_ONE
#define CORE ZUI_KINETIC_CORE
#define EL   ZUI_KINETIC_ELEMENT
#define CH   ZUI_KINETIC_CHASSIS

static int32_t iabs(int32_t v)
{
    return v < 0 ? -v : v;
}

/* angular distance in tenths of a degree */
static int32_t adist(int32_t a, int32_t b)
{
    int32_t d = ((a - b) % 3600 + 3600) % 3600;
    return d > 1800 ? 3600 - d : d;
}

static int32_t rot_of(const zui_kinetic_group_t *g)
{
    return zui_kinetic_atan2(g->m[1], g->m[0]);
}

static void reduce(bool on)
{
    zui_motion_set_flag(ZUI_PREF_REDUCE_MOTION, on);
}

static zui_kinetic_t k, k2;
static zui_kinetic_frame_t f, f2;

static void setup(zui_kinetic_tier_t tier, zui_kinetic_activity_t a, uint32_t t0)
{
    zui_kinetic_init(&k, tier, t0);
    zui_kinetic_set_activity(&k, a, t0);
    zui_kinetic_frame(&k, t0, &f);
}

/* ---- helpers ------------------------------------------------------------- */

static void test_sin(void)
{
    CHECK(zui_kinetic_sin(0) == 0, "sin 0");
    CHECK(zui_kinetic_sin(900) == ONE, "sin 90");
    CHECK(zui_kinetic_sin(1800) == 0, "sin 180");
    CHECK(zui_kinetic_sin(2700) == -ONE, "sin 270");
    CHECK(zui_kinetic_sin(300) == 32768, "sin 30 = 0.5");
    CHECK(zui_kinetic_sin(-900) == -ONE, "sin -90");
    CHECK(zui_kinetic_sin(3600 + 450) == 46341, "sin wraps a turn");
    CHECK(zui_kinetic_sin(5) == 572, "sin 0.5 interpolates (%d)", zui_kinetic_sin(5));
    CHECK(zui_kinetic_sin(2100) == -32768, "sin 210");
    CHECK(zui_kinetic_cos(0) == ONE && zui_kinetic_cos(1800) == -ONE, "cos ends");
    CHECK(zui_kinetic_cos(600) == 32768, "cos 60");
    int mono = 1, pyth = 1;
    for (int32_t a = 1; a <= 900; a++)
        if (zui_kinetic_sin(a) < zui_kinetic_sin(a - 1)) mono = 0;
    for (int32_t a = -3600; a < 7200; a += 7) {
        int64_t s = zui_kinetic_sin(a), c = zui_kinetic_cos(a);
        int64_t r = (s * s + c * c) >> 16;
        if (r < ONE - 40 || r > ONE + 40) pyth = 0;
        if (zui_kinetic_sin(a) != -zui_kinetic_sin(-a)) pyth = 0;
    }
    CHECK(mono, "sin rises over the first quadrant");
    CHECK(pyth, "sin^2 + cos^2 = 1 and sin is odd");
}

static void test_atan2(void)
{
    CHECK(zui_kinetic_atan2(0, 0) == 0, "atan2 origin");
    CHECK(zui_kinetic_atan2(0, 5) == 0, "atan2 +x");
    CHECK(zui_kinetic_atan2(5, 0) == 900, "atan2 down is 90");
    CHECK(zui_kinetic_atan2(0, -5) == 1800, "atan2 -x");
    CHECK(zui_kinetic_atan2(-5, 0) == 2700, "atan2 up is 270");
    CHECK(zui_kinetic_atan2(7, 7) == 450, "atan2 45");
    CHECK(zui_kinetic_atan2(7, -7) == 1350, "atan2 135");
    CHECK(zui_kinetic_atan2(-7, -7) == 2250, "atan2 225");
    CHECK(zui_kinetic_atan2(-7, 7) == 3150, "atan2 315");
    CHECK(iabs(zui_kinetic_atan2(1, 2) - 266) <= 1, "atan2(1,2) = 26.6 (%d)",
          zui_kinetic_atan2(1, 2));
    CHECK(zui_kinetic_atan2(2000000000, 1) == 900, "atan2 huge values");
    int ok = 1, worst = 0;
    for (int32_t a = 0; a < 3600; a += 7) {
        int32_t dx = (int32_t) (((int64_t) zui_kinetic_cos(a) * 1000) >> 16);
        int32_t dy = (int32_t) (((int64_t) zui_kinetic_sin(a) * 1000) >> 16);
        int32_t e = adist(zui_kinetic_atan2(dy, dx), a);
        if (e > worst) worst = e;
        if (e > 2) ok = 0;
    }
    CHECK(ok, "atan2 inverts sin/cos to 0.2 degree (worst %d)", worst);
}

static void test_rates(void)
{
    CHECK(zui_kinetic_period_ms(ZUI_KINETIC_NOT_RUNNING) == 0, "STILL has no period");
    uint32_t s = zui_kinetic_period_ms(ZUI_KINETIC_OPEN_IDLE);
    uint32_t m = zui_kinetic_period_ms(ZUI_KINETIC_RUNNING);
    uint32_t q = zui_kinetic_period_ms(ZUI_KINETIC_NEEDS_INPUT);
    CHECK(s == 4000 && m == 1600 && q == 750, "the tempo periods");
    CHECK(q < m && m < s, "FAST < MEDIUM < SLOW, distinct");
    CHECK(q * 3 > 1000, "FAST is under 3 cycles a second");
    CHECK(zui_kinetic_period_ms((zui_kinetic_activity_t) 9) == 0, "unknown activity is still");
    CHECK(zui_kinetic_fire_rate(0) == 0, "no work, no turn");
    CHECK(zui_kinetic_fire_rate(1) == 60, "6 degrees/s per token");
    CHECK(zui_kinetic_fire_rate(59) == 3540, "59 tokens");
    CHECK(zui_kinetic_fire_rate(60) == 3600, "the cap is one turn a second");
    CHECK(zui_kinetic_fire_rate(4000000000u) == 3600, "no overflow past the cap");
    CHECK(ZUI_KINETIC_LIFT == 70779, "1.08x");
    CHECK(ZUI_KINETIC_BREATH == 983, "1.5 %%");
    /* amber is not the gold */
    int dr = iabs((int) ((ZUI_KINETIC_QUARANTINE >> 16) & 255) - (int) ((ZUI_GOLD >> 16) & 255));
    int dg = iabs((int) ((ZUI_KINETIC_QUARANTINE >> 8) & 255) - (int) ((ZUI_GOLD >> 8) & 255));
    int db = iabs((int) (ZUI_KINETIC_QUARANTINE & 255) - (int) (ZUI_GOLD & 255));
    CHECK(ZUI_KINETIC_QUARANTINE != ZUI_GOLD && dr + dg + db >= 64 && dg >= 40,
          "quarantine amber is distinct from the gold (%d %d %d)", dr, dg, db);
}

static void test_spring(void)
{
    zui_kinetic_spring_t s = {ONE, 0};
    int32_t peak = 0, n = 0;
    while (n < 400 && iabs(s.x - ZUI_KINETIC_LIFT) > 8) {
        zui_kinetic_spring_step(&s, ZUI_KINETIC_LIFT);
        if (s.x > peak) peak = s.x;
        n++;
    }
    CHECK(n > 30 && n < 150, "the bare spring nears its target in 120..600 ms (%d steps)", n);
    for (int i = 0; i < 200; i++) {
        zui_kinetic_spring_step(&s, ZUI_KINETIC_LIFT);
        if (s.x > peak) peak = s.x;
    }
    CHECK(peak - ZUI_KINETIC_LIFT <= 60, "and barely overshoots (%d)", peak - ZUI_KINETIC_LIFT);
    /* golden trajectory: w = 25/256 a step, damping 2w (critically damped) */
    zui_kinetic_spring_t g = {ONE, 0};
    int32_t top = 0;
    for (int i = 1; i <= 300; i++) {
        zui_kinetic_spring_step(&g, ZUI_KINETIC_LIFT);
        if (g.x > top) top = g.x;
        if (i == 25) CHECK(g.x == 68769, "spring at 100 ms (%d)", g.x);
        if (i == 50) CHECK(g.x == 70215, "spring at 200 ms (%d)", g.x);
        if (i == 100) CHECK(g.x == 70736, "spring at 400 ms (%d)", g.x);
    }
    CHECK(top <= ZUI_KINETIC_LIFT, "critically damped: never overshoots (%d)", top);
    zui_kinetic_spring_t a = {0, 0};
    zui_kinetic_spring_step(&a, 1000);
    CHECK(a.x > 0 && a.v > 0, "a step moves toward the target");
    zui_kinetic_spring_t b = {0, 0};
    zui_kinetic_spring_step(&b, -1000);
    CHECK(b.x < 0 && b.v < 0, "and the other way");
}

/* ---- KI2, KI3: tempo and tiers ------------------------------------------ */

/* the distance between the starts of successive top-of-breath plateaus */
static int32_t measured_period(zui_kinetic_activity_t a)
{
    static int32_t v[9001];
    setup(ZUI_KINETIC_L2, a, 1000);
    int32_t hi = -(1 << 30), first = -1;
    for (uint32_t t = 0; t <= 9000; t++) {
        zui_kinetic_frame(&k, 1000 + t, &f);
        v[t] = f.g[CORE].m[0];
        if (v[t] > hi) hi = v[t];
    }
    for (int32_t t = 1; t <= 9000; t++)
        if (v[t] == hi && v[t - 1] != hi) {
            if (first >= 0) return t - first;
            first = t;
        }
    return 0;
}

static void test_brackets(void)
{
    int32_t ps = measured_period(ZUI_KINETIC_OPEN_IDLE);
    int32_t pm = measured_period(ZUI_KINETIC_RUNNING);
    int32_t pf = measured_period(ZUI_KINETIC_NEEDS_INPUT);
    CHECK(iabs(ps - 4000) <= 4, "SLOW breathes every 4.0 s (%d)", ps);
    CHECK(iabs(pm - 1600) <= 4, "MEDIUM every 1.6 s (%d)", pm);
    CHECK(iabs(pf - 750) <= 4, "FAST every 0.75 s (%d)", pf);
    CHECK(pf < pm && pm < ps, "the measured brackets are ordered");
    CHECK(pf >= 334, "FAST stays under 3 Hz");
}

static void test_l2_breath(void)
{
    setup(ZUI_KINETIC_L2, ZUI_KINETIC_OPEN_IDLE, 0);
    int32_t lo = 1 << 30, hi = -(1 << 30), tlo = 1 << 30, thi = -(1 << 30);
    int same = 1, chassis = 1;
    for (uint32_t t = 1; t <= 4000; t++) {
        zui_kinetic_frame(&k, t, &f);
        int32_t s = f.g[CORE].m[0];
        if (s < lo) lo = s;
        if (s > hi) hi = s;
        if (f.g[CORE].m[5] < tlo) tlo = f.g[CORE].m[5];
        if (f.g[CORE].m[5] > thi) thi = f.g[CORE].m[5];
        if (f.g[EL].m[0] != s) same = 0;
        if (f.g[CH].m[0] != ONE || f.g[CH].m[4] || f.g[CH].m[5] || f.g[CH].m[1]) chassis = 0;
        if (t == 1000) {
            CHECK(iabs(s - (ONE + ZUI_KINETIC_BREATH)) <= 1, "top of the breath at 1/4 period");
            CHECK(f.g[CORE].m[5] == -ZUI_KINETIC_CHEST * ONE, "chest risen at the top");
            CHECK(f.redraw && f.animating, "a breathing icon redraws");
        }
        if (t == 3000) CHECK(iabs(s - (ONE - ZUI_KINETIC_BREATH)) <= 1, "bottom at 3/4 period");
    }
    CHECK(iabs(hi - (ONE + 983)) <= 1 && iabs(lo - (ONE - 983)) <= 1, "+/-1.5 %% (%d..%d)", lo, hi);
    CHECK(thi == 6 * ONE && tlo == -6 * ONE, "chest +/-6 units");
    CHECK(same, "the kinetic element breathes with the core in IDLE");
    CHECK(chassis, "the chassis stays still");
    CHECK(f.g[CORE].grad_shift == 0, "L2 does not shimmer");
    CHECK(f.g[CORE].m[1] == 0 && f.g[CORE].m[2] == 0, "breath is a pure scale");
    CHECK(f.g[CORE].m[0] == f.g[CORE].m[3], "uniform scale");
}

static void test_l1(void)
{
    setup(ZUI_KINETIC_L1, ZUI_KINETIC_OPEN_IDLE, 0);
    int levels = 1, clicks = 1, redraws = 0, changes = 0;
    int32_t prev_rot = rot_of(&f.g[EL]);
    for (uint32_t t = 1; t <= 8000; t++) {
        zui_kinetic_frame(&k, t, &f);
        int32_t s = f.g[CORE].m[0];
        if (s != ONE && s != ONE + 983 && s != ONE - 983) levels = 0;
        int32_t r = rot_of(&f.g[EL]);
        if (r % 150) clicks = 0;
        if (r != prev_rot) changes++;
        prev_rot = r;
        if (f.redraw) redraws++;
        if (t == 999) CHECK(s == ONE, "L1 holds the breath until the quarter");
        if (t == 1000) CHECK(s == ONE + 983, "and steps at it");
        if (t == 1999) CHECK(r == 0, "no click before the half period");
        if (t == 2000) CHECK(r == 150, "one 15 degree click at the half period (%d)", r);
        if (t == 4000) CHECK(r == 300, "two clicks a period");
    }
    CHECK(levels, "L1 breath has three held levels only");
    CHECK(clicks, "L1 rotation is in 15 degree clicks");
    CHECK(changes == 4, "2 clicks per period over 2 periods (%d)", changes);
    CHECK(redraws == 8, "L1 redraws only when a step lands: 4 per period (%d)", redraws);
}

static void test_l3(void)
{
    setup(ZUI_KINETIC_L3, ZUI_KINETIC_RUNNING, 0);
    int geom = 1;
    int32_t lo = 1 << 30, hi = -(1 << 30);
    for (uint32_t t = 1; t <= 1600; t++) {
        zui_kinetic_frame(&k, t, &f);
        for (int i = 0; i < 3; i++)
            if (f.g[i].m[0] != ONE || f.g[i].m[1] || f.g[i].m[4] || f.g[i].m[5]) geom = 0;
        int32_t g = f.g[CORE].grad_shift;
        if (g < lo) lo = g;
        if (g > hi) hi = g;
        if (t == 400) CHECK(f.g[EL].grad_shift == g, "the element shimmers with the core");
    }
    CHECK(geom, "L3 changes no geometry at idle");
    CHECK(iabs(hi - ZUI_KINETIC_SHIMMER) <= 1 && iabs(lo + ZUI_KINETIC_SHIMMER) <= 1,
          "L3 shimmer +/-6 %% (%d..%d)", lo, hi);
    CHECK(f.g[CH].grad_shift == 0, "the chassis does not shimmer");
}

/* ---- KI5: redraws -------------------------------------------------------- */

static int count_redraws(uint32_t from, uint32_t to, uint32_t step)
{
    int n = 0;
    for (uint32_t t = from; t <= to; t += step)
        if (zui_kinetic_frame(&k, t, &f)) n++;
    return n;
}

static void test_redraws(void)
{
    for (int tier = 1; tier <= 3; tier++) {
        setup((zui_kinetic_tier_t) tier, ZUI_KINETIC_NOT_RUNNING, 0);
        CHECK(f.redraw, "the first frame draws (L%d)", tier);
        CHECK(count_redraws(16, 10000, 16) == 0, "STILL costs no redraws (L%d)", tier);
        CHECK(!f.animating, "and is not animating");
        CHECK(f.pip == ZUI_KINETIC_PIP_NONE && !f.pip_required, "no badge when not running");
        for (int i = 0; i < 3; i++)
            CHECK(f.g[i].m[0] == ONE && f.g[i].m[1] == 0 && f.g[i].opacity == 256,
                  "STILL is the identity");
    }
    setup(ZUI_KINETIC_L2, ZUI_KINETIC_OPEN_IDLE, 0);
    CHECK(count_redraws(16, 1600, 16) == 100, "SLOW redraws every frame on L2");
    zui_kinetic_set_visible(&k, false);
    CHECK(count_redraws(1616, 9000, 16) == 0, "an invisible icon never redraws");
    CHECK(!f.animating, "nor animates");
    zui_kinetic_set_visible(&k, true);
    CHECK(zui_kinetic_frame(&k, 9001, &f), "becoming visible redraws");
    /* hover on a STILL icon: redraws while the spring moves, then stops */
    setup(ZUI_KINETIC_L2, ZUI_KINETIC_NOT_RUNNING, 0);
    zui_kinetic_set_state(&k, ZUI_KINETIC_HOVER, 0);
    int during = count_redraws(16, 800, 16);
    CHECK(during > 10, "hover lift redraws while it moves (%d)", during);
    CHECK(count_redraws(816, 5000, 16) == 0, "and stops once it has landed");
    CHECK(f.g[CORE].m[0] == ZUI_KINETIC_LIFT, "landed on 1.08x");
    /* a state change alone redraws once */
    setup(ZUI_KINETIC_L2, ZUI_KINETIC_NOT_RUNNING, 0);
    zui_kinetic_set_pivot(&k, 512, 512);
    CHECK(zui_kinetic_frame(&k, 1, &f), "a change marks the frame dirty");
    CHECK(!zui_kinetic_frame(&k, 2, &f), "then quiet");
}

/* ---- WCAG 2.3.1: flashes -------------------------------------------------- */

/* the most direction reversals of v[] in any 1000-sample (1 s) window; a
 * reversal counts once the value has come back more than eps from its last
 * extreme, so 1-unit rounding does not count as motion */
static int max_reversals(const int32_t *v, int n, int32_t eps)
{
    static int rev[8000];
    int dir = 0, cnt = 0, best = 0;
    int32_t ext = v[0];
    rev[0] = 0;
    for (int i = 1; i < n; i++) {
        rev[i] = 0;
        if (dir >= 0 && v[i] > ext)
            ext = v[i], dir = dir ? dir : 1;
        else if (dir <= 0 && v[i] < ext)
            ext = v[i], dir = dir ? dir : -1;
        if (dir >= 0 && v[i] < ext - eps) {
            rev[i] = dir > 0;
            dir = -1;
            ext = v[i];
        } else if (dir <= 0 && v[i] > ext + eps) {
            rev[i] = dir < 0;
            dir = 1;
            ext = v[i];
        }
    }
    for (int i = 1; i < n; i++) {
        cnt += rev[i];
        if (i > 1000) cnt -= rev[i - 1000];
        if (cnt > best) best = cnt;
    }
    return best;
}

static int32_t series[6][8000];

static void test_flash(void)
{
    static const zui_kinetic_state_t sts[] = {ZUI_KINETIC_IDLE, ZUI_KINETIC_STALL, ZUI_KINETIC_BOTH,
                                              ZUI_KINETIC_HOVER};
    int worst = 0, steady_tint = 1;
    for (int tier = 1; tier <= 3; tier++)
        for (int si = 0; si < 4; si++) {
            setup((zui_kinetic_tier_t) tier, ZUI_KINETIC_NEEDS_INPUT, 0);
            zui_kinetic_set_state(&k, sts[si], 0);
            for (int t = 0; t < 8000; t++) {
                zui_kinetic_frame(&k, (uint32_t) t, &f);
                series[0][t] = f.g[CORE].m[0];
                series[1][t] = rot_of(&f.g[EL]);
                series[2][t] = f.ghost[CORE].opacity;
                series[3][t] = f.g[CORE].grad_shift;
                series[4][t] = f.g[EL].m[0];
                series[5][t] = f.g[CORE].m[5];
                if (t > 400 && (f.g[CORE].tint_amt != (sts[si] == ZUI_KINETIC_STALL  ? 40
                                                       : sts[si] == ZUI_KINETIC_BOTH ? 128
                                                                                     : 0)))
                    steady_tint = 0;
            }
            for (int s = 0; s < 6; s++) {
                static const int32_t eps[6] = {8, 2, 1, 8, 8, 4096};
                int r = max_reversals(series[s], 8000, eps[s]);
                if (r > worst) worst = r;
                CHECK(r <= 6, "L%d state %d channel %d: %d reversals/s (3 flashes max)", tier,
                      (int) sts[si], s, r);
            }
        }
    CHECK(worst <= 4, "FAST is at most 1.33 cycles a second (worst %d reversals)", worst);
    CHECK(worst >= 2, "and FAST does move");
    CHECK(steady_tint, "tints are held constant, never flashed");
}

/* ---- KI4: states --------------------------------------------------------- */

static void test_hover(void)
{
    setup(ZUI_KINETIC_L2, ZUI_KINETIC_NOT_RUNNING, 0);
    zui_kinetic_set_state(&k, ZUI_KINETIC_HOVER, 0);
    int32_t peak = 0, prev = ONE;
    int mono = 1, landed_at = -1;
    for (uint32_t t = 1; t <= 1000; t++) {
        zui_kinetic_frame(&k, t, &f);
        int32_t s = f.g[CORE].m[0];
        if (s > peak) peak = s;
        if (s < prev - 2) mono = 0;
        prev = s;
        if (landed_at < 0 && s == ZUI_KINETIC_LIFT && !f.animating) landed_at = (int) t;
        if (t == 100) CHECK(s > ONE + 2000 && s < ZUI_KINETIC_LIFT, "rising at 100 ms (%d)", s);
    }
    CHECK(peak <= ZUI_KINETIC_LIFT + 60, "critically damped: no real overshoot (%d)", peak);
    CHECK(mono, "the lift rises monotonically");
    CHECK(landed_at > 150 && landed_at < 600, "settles in 150..600 ms (%d)", landed_at);
    CHECK(f.g[CH].m[0] == ZUI_KINETIC_LIFT, "the whole icon lifts");
    zui_kinetic_set_state(&k, ZUI_KINETIC_IDLE, 1000);
    zui_kinetic_frame(&k, 2000, &f);
    CHECK(f.g[CORE].m[0] == ONE, "back to 1.0 when the pointer leaves");

    /* cadence independence: 1 ms frames vs 17 ms frames */
    zui_kinetic_init(&k, ZUI_KINETIC_L2, 0);
    zui_kinetic_init(&k2, ZUI_KINETIC_L2, 0);
    zui_kinetic_set_state(&k, ZUI_KINETIC_HOVER, 0);
    zui_kinetic_set_state(&k2, ZUI_KINETIC_HOVER, 0);
    int same = 1;
    for (uint32_t t = 1; t <= 17 * 30; t++) {
        zui_kinetic_frame(&k, t, &f);
        if (t % 17 == 0) {
            zui_kinetic_frame(&k2, t, &f2);
            if (f.g[CORE].m[0] != f2.g[CORE].m[0]) same = 0;
        }
    }
    CHECK(same, "the spring is the same at any frame rate");

    /* L1: four equal steps */
    setup(ZUI_KINETIC_L1, ZUI_KINETIC_NOT_RUNNING, 0);
    zui_kinetic_set_state(&k, ZUI_KINETIC_HOVER, 0);
    int ok = 1, seen = 0;
    int32_t last = ONE;
    for (uint32_t t = 1; t <= 800; t++) {
        zui_kinetic_frame(&k, t, &f);
        int32_t s = f.g[CORE].m[0], lv = -1;
        for (int i = 0; i <= 4; i++)
            if (s == ONE + i * (ZUI_KINETIC_LIFT - ONE) / 4) lv = i;
        if (lv < 0) ok = 0;
        if (s != last) seen++;
        last = s;
    }
    CHECK(ok, "L1 lift sits on 5 levels only");
    CHECK(seen >= 3 && seen <= 6, "and steps through them (%d changes)", seen);
    CHECK(last == ZUI_KINETIC_LIFT, "ending at 1.08x");
}

static int32_t aim_after(zui_kinetic_tier_t tier, int32_t x, int32_t y)
{
    setup(tier, ZUI_KINETIC_NOT_RUNNING, 0);
    zui_kinetic_set_state(&k, ZUI_KINETIC_HOVER, 0);
    zui_kinetic_set_pointer(&k, x, y, true);
    zui_kinetic_frame(&k, 2000, &f);
    int32_t r = rot_of(&f.g[EL]);
    return r > 1800 ? r - 3600 : r;
}

static void test_aim(void)
{
    CHECK(aim_after(ZUI_KINETIC_L2, 512, 100) == 0, "pointer straight up: no turn");
    CHECK(aim_after(ZUI_KINETIC_L2, 912, 512) == 300, "pointer right: clamped to +30");
    CHECK(aim_after(ZUI_KINETIC_L2, 112, 512) == -300, "pointer left: clamped to -30");
    CHECK(aim_after(ZUI_KINETIC_L2, 512, 900) == -300 || aim_after(ZUI_KINETIC_L2, 512, 900) == 300,
          "pointer below: clamped");
    int32_t a = aim_after(ZUI_KINETIC_L2, 612, 112); /* atan(100/400) = 14.04 deg */
    CHECK(iabs(a - 140) <= 2, "aim follows the angle (%d)", a);
    CHECK(aim_after(ZUI_KINETIC_L1, 612, 112) == 150, "L1 aims in 15 degree clicks");
    CHECK(aim_after(ZUI_KINETIC_L1, 562, 112) == 0, "L1 rounds a 7 degree aim to 0");
    CHECK(aim_after(ZUI_KINETIC_L2, 512, 512) == 0, "pointer on the pivot: no turn");
    /* pivot moves the aim */
    setup(ZUI_KINETIC_L2, ZUI_KINETIC_NOT_RUNNING, 0);
    zui_kinetic_set_pivot(&k, 700, 512);
    zui_kinetic_set_state(&k, ZUI_KINETIC_HOVER, 0);
    zui_kinetic_set_pointer(&k, 700, 100, true);
    zui_kinetic_frame(&k, 2000, &f);
    CHECK(rot_of(&f.g[EL]) == 0 && f.g[EL].px == 700 * 16 && f.g[EL].py == 512 * 16,
          "aim about the pivot");
    zui_kinetic_set_pointer(&k, 1000, 512, false);
    zui_kinetic_frame(&k, 4000, &f);
    CHECK(rot_of(&f.g[EL]) == 0, "pointer gone: aim returns to rest");
    /* L2 aim eases, L1 snaps */
    setup(ZUI_KINETIC_L2, ZUI_KINETIC_NOT_RUNNING, 0);
    zui_kinetic_set_state(&k, ZUI_KINETIC_HOVER, 0);
    zui_kinetic_set_pointer(&k, 912, 512, true);
    zui_kinetic_frame(&k, 40, &f);
    int32_t r = rot_of(&f.g[EL]);
    CHECK(r > 0 && r < 300, "L2 aim eases toward the pointer (%d)", r);
    setup(ZUI_KINETIC_L1, ZUI_KINETIC_NOT_RUNNING, 0);
    zui_kinetic_set_state(&k, ZUI_KINETIC_HOVER, 0);
    zui_kinetic_set_pointer(&k, 912, 512, true);
    zui_kinetic_frame(&k, 4, &f);
    CHECK(rot_of(&f.g[EL]) == 300, "L1 aim snaps, no easing");
}

static void test_firing(void)
{
    setup(ZUI_KINETIC_L2, ZUI_KINETIC_NOT_RUNNING, 0);
    zui_kinetic_set_work(&k, 6); /* 36 degrees a second */
    zui_kinetic_set_state(&k, ZUI_KINETIC_FIRING, 0);
    zui_kinetic_frame(&k, 500, &f);
    CHECK(adist(rot_of(&f.g[EL]), 180) <= 1, "L2 at 500 ms: 18 degrees (%d)", rot_of(&f.g[EL]));
    zui_kinetic_frame(&k, 1000, &f);
    CHECK(adist(rot_of(&f.g[EL]), 360) <= 1, "L2 at 1 s: 36 degrees");
    CHECK(f.animating && f.redraw, "firing animates");
    zui_kinetic_set_work(&k, 12);
    zui_kinetic_frame(&k, 2000, &f);
    CHECK(adist(rot_of(&f.g[EL]), 1080) <= 1, "rate follows the work (doubling)");
    zui_kinetic_set_work(&k, 100000);
    zui_kinetic_frame(&k, 2250, &f);
    CHECK(adist(rot_of(&f.g[EL]), 1080 + 900) <= 1, "capped at one turn a second");
    CHECK(f.g[CORE].m[1] == 0 && f.g[CH].m[1] == 0, "only the kinetic element turns");
    zui_kinetic_set_work(&k, 0);
    zui_kinetic_frame(&k, 2300, &f);
    CHECK(!zui_kinetic_frame(&k, 3000, &f) && !f.animating, "no work: no turn, no redraw");

    /* L1 escapement */
    setup(ZUI_KINETIC_L1, ZUI_KINETIC_NOT_RUNNING, 0);
    zui_kinetic_set_work(&k, 6);
    zui_kinetic_set_state(&k, ZUI_KINETIC_FIRING, 0);
    int clicks = 1, redraws = 0;
    for (uint32_t t = 1; t <= 1000; t++) {
        if (zui_kinetic_frame(&k, t, &f)) redraws++;
        if (rot_of(&f.g[EL]) % 150) clicks = 0;
        if (t == 416) CHECK(rot_of(&f.g[EL]) == 0, "no click before 15 degrees");
        if (t == 417) CHECK(rot_of(&f.g[EL]) == 150, "the first click at 15 degrees (417 ms)");
    }
    CHECK(clicks, "L1 firing steps in 15 degree clicks");
    CHECK(redraws == 3, "L1 redraws on the state change and per click only (%d)", redraws);
    CHECK(rot_of(&f.g[EL]) == 300, "36 degrees -> 2 clicks");
    /* L2 smooth: not all multiples of 15 */
    setup(ZUI_KINETIC_L2, ZUI_KINETIC_NOT_RUNNING, 0);
    zui_kinetic_set_work(&k, 6);
    zui_kinetic_set_state(&k, ZUI_KINETIC_FIRING, 0);
    zui_kinetic_frame(&k, 100, &f);
    CHECK(rot_of(&f.g[EL]) == 36, "L2 is smooth (3.6 degrees at 100 ms)");
}

static void test_stall(void)
{
    setup(ZUI_KINETIC_L2, ZUI_KINETIC_NOT_RUNNING, 0);
    zui_kinetic_set_work(&k, 10);
    zui_kinetic_set_state(&k, ZUI_KINETIC_FIRING, 0);
    zui_kinetic_frame(&k, 500, &f);
    CHECK(zui_kinetic_set_state(&k, ZUI_KINETIC_STALL, 500), "into STALL");
    zui_kinetic_frame(&k, 650, &f);
    int32_t mid = rot_of(&f.g[EL]);
    CHECK(mid > 300 && mid < 380, "taking up the strain (%d)", mid);
    zui_kinetic_frame(&k, 800, &f);
    CHECK(rot_of(&f.g[EL]) == 300 + ZUI_KINETIC_STRAIN, "strained 8 degrees past (%d)",
          rot_of(&f.g[EL]));
    zui_kinetic_frame(&k, 5000, &f);
    CHECK(rot_of(&f.g[EL]) == 380, "and does not advance");
    CHECK(f.g[EL].tint == ZUI_KINETIC_STALL_TINT && f.g[EL].tint_amt == 40, "stall tint");
    CHECK(f.g[CH].tint_amt == 0, "the chassis is not tinted");
    CHECK(!zui_kinetic_frame(&k, 6000, &f), "a still STALL costs no redraw");
    zui_kinetic_set_state(&k, ZUI_KINETIC_FIRING, 6000);
    zui_kinetic_frame(&k, 6100, &f);
    CHECK(rot_of(&f.g[EL]) == 360, "firing resumes from where it stalled");

    /* jitter */
    setup(ZUI_KINETIC_L2, ZUI_KINETIC_NEEDS_INPUT, 0);
    zui_kinetic_set_state(&k, ZUI_KINETIC_STALL, 0);
    int32_t lo = 9999, hi = -9999;
    for (uint32_t t = 300; t <= 2000; t++) {
        zui_kinetic_frame(&k, t, &f);
        int32_t r = rot_of(&f.g[EL]);
        if (r < lo) lo = r;
        if (r > hi) hi = r;
    }
    CHECK(lo >= 80 - ZUI_KINETIC_JITTER && hi <= 80 + ZUI_KINETIC_JITTER && hi - lo >= 18,
          "jitter is +/-1 degree about the strain (%d..%d)", lo, hi);
    /* L1 quarter steps */
    setup(ZUI_KINETIC_L1, ZUI_KINETIC_NOT_RUNNING, 0);
    zui_kinetic_set_state(&k, ZUI_KINETIC_STALL, 0);
    zui_kinetic_frame(&k, 100, &f);
    CHECK(rot_of(&f.g[EL]) == 20, "L1 strain in quarter steps (%d)", rot_of(&f.g[EL]));
    zui_kinetic_frame(&k, 300, &f);
    CHECK(rot_of(&f.g[EL]) == 80, "L1 full strain");
    /* L3 jitter is shimmer, not geometry */
    setup(ZUI_KINETIC_L3, ZUI_KINETIC_NEEDS_INPUT, 0);
    zui_kinetic_set_state(&k, ZUI_KINETIC_STALL, 0);
    int fixed = 1, shim = 0;
    for (uint32_t t = 300; t <= 1500; t++) {
        zui_kinetic_frame(&k, t, &f);
        if (rot_of(&f.g[EL]) != 80) fixed = 0;
        if (f.g[EL].grad_shift) shim = 1;
    }
    CHECK(fixed && shim, "L3 stall holds its geometry and shimmers");
}

static void test_both(void)
{
    setup(ZUI_KINETIC_L2, ZUI_KINETIC_NEEDS_INPUT, 0);
    CHECK(zui_kinetic_set_state(&k, ZUI_KINETIC_BOTH, 0), "into BOTH");
    zui_kinetic_frame(&k, 100, &f);
    CHECK(f.ghost_on, "a second draw");
    CHECK(f.g[CH].m[4] == -6 * ONE && f.ghost[CH].m[4] == 6 * ONE, "half parted at 100 ms (%d)",
          f.g[CH].m[4]);
    zui_kinetic_frame(&k, 200, &f);
    CHECK(f.g[CORE].m[4] == -12 * ONE && f.ghost[CORE].m[4] == 12 * ONE, "12 units either side");
    int tinted = 1, oprange = 1;
    for (uint32_t t = 200; t <= 3000; t++) {
        zui_kinetic_frame(&k, t, &f);
        for (int i = 0; i < 3; i++) {
            if (f.g[i].tint != ZUI_KINETIC_QUARANTINE ||
                f.ghost[i].tint != ZUI_KINETIC_QUARANTINE || f.g[i].tint_amt != 128 ||
                f.ghost[i].tint_amt != 128)
                tinted = 0;
            if (f.ghost[i].opacity < 168 || f.ghost[i].opacity > 184 || f.g[i].opacity != 256)
                oprange = 0;
        }
    }
    CHECK(tinted, "both copies quarantine amber");
    CHECK(oprange, "the ghost pulse is low contrast (168..184 of 256)");
    CHECK(!zui_kinetic_set_state(&k, ZUI_KINETIC_IDLE, 3000), "escrow refuses IDLE");
    CHECK(!zui_kinetic_set_state(&k, ZUI_KINETIC_FIRING, 3000), "and FIRING");
    CHECK(zui_kinetic_set_state(&k, ZUI_KINETIC_BOTH, 3000), "BOTH again is fine");
    CHECK(k.state == ZUI_KINETIC_BOTH, "still held");
    CHECK(!zui_kinetic_settle(&k, ZUI_KINETIC_BOTH, 3000), "cannot settle into BOTH");
    zui_kinetic_frame(&k, 3500, &f);
    CHECK(f.ghost_on, "held until settled");
    CHECK(zui_kinetic_settle(&k, ZUI_KINETIC_STALL, 4000), "settle releases");
    CHECK(k.state == ZUI_KINETIC_STALL && !k.escrow, "into the settled state");
    zui_kinetic_frame(&k, 4001, &f);
    CHECK(!f.ghost_on && f.g[CH].m[4] == 0, "one draw again");
    CHECK(!zui_kinetic_settle(&k, ZUI_KINETIC_IDLE, 4002), "nothing left to settle");
    CHECK(zui_kinetic_set_state(&k, ZUI_KINETIC_IDLE, 4003), "free again");
    /* L1 parts at once */
    setup(ZUI_KINETIC_L1, ZUI_KINETIC_NOT_RUNNING, 0);
    zui_kinetic_set_state(&k, ZUI_KINETIC_BOTH, 0);
    zui_kinetic_frame(&k, 1, &f);
    CHECK(f.g[CORE].m[4] == -12 * ONE, "L1 parts in one step");
    CHECK(!zui_kinetic_frame(&k, 5000, &f), "a still held contradiction costs no redraw");
    CHECK(f.ghost[CORE].opacity == ZUI_KINETIC_GHOST, "no pulse without a tempo");
}

/* ---- KI6: reduce motion -------------------------------------------------- */

static void test_reduce(void)
{
    reduce(true);
    for (int tier = 1; tier <= 3; tier++) {
        setup((zui_kinetic_tier_t) tier, ZUI_KINETIC_NEEDS_INPUT, 0);
        CHECK(count_redraws(16, 8000, 16) == 0, "reduce: FAST does not move (L%d)", tier);
        CHECK(!f.animating, "reduce: not animating");
        CHECK(f.g[CORE].m[0] == ONE && f.g[CORE].grad_shift == 0 && f.g[CORE].m[5] == 0,
              "reduce: no breath, chest or shimmer");
        CHECK(f.pip == ZUI_KINETIC_PIP_RING && f.pip_required && f.pip_color == ZUI_GOLD_HI,
              "reduce: NEEDS_INPUT shows a bright ring");
    }
    setup(ZUI_KINETIC_L2, ZUI_KINETIC_OPEN_IDLE, 0);
    CHECK(f.pip == ZUI_KINETIC_PIP_DIM && f.pip_opacity == 128, "OPEN_IDLE: a dim pip");
    zui_kinetic_set_activity(&k, ZUI_KINETIC_RUNNING, 0);
    zui_kinetic_frame(&k, 1, &f);
    CHECK(f.pip == ZUI_KINETIC_PIP_STEADY && f.pip_color == ZUI_GOLD, "RUNNING: a steady pip");
    zui_kinetic_set_activity(&k, ZUI_KINETIC_NOT_RUNNING, 1);
    zui_kinetic_frame(&k, 2, &f);
    CHECK(f.pip == ZUI_KINETIC_PIP_NONE && !f.pip_required, "NOT_RUNNING: none");

    setup(ZUI_KINETIC_L2, ZUI_KINETIC_RUNNING, 0);
    zui_kinetic_set_state(&k, ZUI_KINETIC_HOVER, 0);
    zui_kinetic_set_pointer(&k, 912, 512, true);
    zui_kinetic_frame(&k, 1, &f);
    CHECK(f.g[CORE].m[0] == ZUI_KINETIC_LIFT, "reduce: hover lift is a cut");
    CHECK(rot_of(&f.g[EL]) == 0, "reduce: no aim");
    CHECK(!zui_kinetic_frame(&k, 500, &f), "reduce: nothing further");

    setup(ZUI_KINETIC_L2, ZUI_KINETIC_RUNNING, 0);
    zui_kinetic_set_work(&k, 30);
    zui_kinetic_set_state(&k, ZUI_KINETIC_FIRING, 0);
    zui_kinetic_frame(&k, 1, &f);
    CHECK(f.g[EL].tint == ZUI_KINETIC_FIRE_TINT && f.g[EL].tint_amt == 48, "reduce: firing tint");
    CHECK(count_redraws(16, 4000, 16) == 0 && rot_of(&f.g[EL]) == 0, "reduce: no rotation");

    setup(ZUI_KINETIC_L2, ZUI_KINETIC_NEEDS_INPUT, 0);
    zui_kinetic_set_state(&k, ZUI_KINETIC_STALL, 0);
    zui_kinetic_frame(&k, 1, &f);
    CHECK(rot_of(&f.g[EL]) == 80 && f.g[EL].tint_amt == 40, "reduce: strain held, tinted");
    CHECK(count_redraws(16, 4000, 16) == 0, "reduce: no jitter");

    setup(ZUI_KINETIC_L2, ZUI_KINETIC_NEEDS_INPUT, 0);
    zui_kinetic_set_state(&k, ZUI_KINETIC_BOTH, 0);
    zui_kinetic_frame(&k, 1, &f);
    CHECK(f.ghost_on && f.ghost[CORE].m[4] == 12 * ONE && f.g[CORE].m[4] == -12 * ONE,
          "reduce: fully split at once");
    CHECK(f.ghost[CORE].opacity == ZUI_KINETIC_GHOST, "reduce: no pulse");
    CHECK(count_redraws(16, 4000, 16) == 0, "reduce: BOTH is static");
    reduce(false);
    CHECK(zui_kinetic_frame(&k, 4100, &f) || f.ghost[CORE].opacity != 0,
          "motion back on: the pulse may resume");
    zui_kinetic_frame(&k, 4300, &f);
    CHECK(!f.pip_required, "motion on: the badge is optional");
}

/* ---- KI1, KI2, KI8 -------------------------------------------------------- */

static void script(zui_kinetic_t *x, zui_kinetic_frame_t *o, uint32_t *h)
{
    zui_kinetic_init(x, ZUI_KINETIC_L2, 5);
    zui_kinetic_set_activity(x, ZUI_KINETIC_RUNNING, 5);
    *h = 2166136261u;
    for (uint32_t t = 5; t < 6000; t += 7) {
        if (t == 509) zui_kinetic_set_state(x, ZUI_KINETIC_HOVER, t);
        if (t == 803) zui_kinetic_set_pointer(x, 800, 300, true);
        if (t == 1503) zui_kinetic_set_work(x, 9), zui_kinetic_set_state(x, ZUI_KINETIC_FIRING, t);
        if (t == 2804) zui_kinetic_set_state(x, ZUI_KINETIC_STALL, t);
        if (t == 3505) zui_kinetic_set_state(x, ZUI_KINETIC_BOTH, t);
        if (t == 4502) zui_kinetic_settle(x, ZUI_KINETIC_IDLE, t);
        if (t == 5006) zui_kinetic_set_activity(x, ZUI_KINETIC_NEEDS_INPUT, t);
        zui_kinetic_frame(x, t, o);
        for (int i = 0; i < 3; i++) {
            const int32_t v[10] = {o->g[i].m[0],       o->g[i].m[1],     o->g[i].m[4],
                                   o->g[i].m[5],       o->g[i].opacity,  (int32_t) o->g[i].tint,
                                   o->g[i].grad_shift, o->ghost[i].m[4], o->ghost[i].opacity,
                                   o->redraw};
            for (int j = 0; j < 10; j++) *h = (*h ^ (uint32_t) v[j]) * 16777619u;
        }
    }
}

static void test_determinism(void)
{
    uint32_t h1, h2;
    script(&k, &f, &h1);
    script(&k2, &f2, &h2);
    CHECK(h1 == h2, "same inputs, same outputs");
    CHECK(h1 != 2166136261u, "the script produced output");
}

static void test_phase_and_clock(void)
{
    setup(ZUI_KINETIC_L2, ZUI_KINETIC_OPEN_IDLE, 0);
    zui_kinetic_frame(&k, 700, &f);
    int32_t before = f.g[CORE].m[0];
    zui_kinetic_set_activity(&k, ZUI_KINETIC_NEEDS_INPUT, 700);
    zui_kinetic_frame(&k, 700, &f);
    CHECK(iabs(f.g[CORE].m[0] - before) <= 4, "changing tempo keeps the phase (%d vs %d)",
          f.g[CORE].m[0], before);
    zui_kinetic_frame(&k, 701, &f);
    CHECK(iabs(f.g[CORE].m[0] - before) <= 12, "and continues smoothly");

    /* the clock wraps */
    uint32_t t0 = 0xFFFFFF00u;
    setup(ZUI_KINETIC_L2, ZUI_KINETIC_NOT_RUNNING, t0);
    zui_kinetic_set_work(&k, 6);
    zui_kinetic_set_state(&k, ZUI_KINETIC_FIRING, t0);
    zui_kinetic_frame(&k, t0 + 512u, &f);
    CHECK(rot_of(&f.g[EL]) == 184, "time across the wrap (%d)", rot_of(&f.g[EL]));
    zui_kinetic_frame(&k, t0 + 100u, &f);
    CHECK(rot_of(&f.g[EL]) == 184, "a clock that steps back moves nothing");
    zui_kinetic_frame(&k, t0 + 1012u, &f);
    CHECK(rot_of(&f.g[EL]) == 364, "and resumes from where it was");
}

static void test_misc(void)
{
    setup(ZUI_KINETIC_L2, ZUI_KINETIC_NOT_RUNNING, 0);
    CHECK(strcmp(f.g[CH].group, "chassis") == 0 && strcmp(f.g[CORE].group, "core-artifact") == 0 &&
              strcmp(f.g[EL].group, "kinetic-element") == 0,
          "the three group names");
    CHECK(f.g[CH].px == 512 * 16 && f.g[CORE].py == 512 * 16, "default pivots at the centre");
    CHECK(!f.ghost_on && f.ghost[CH].group == f.g[CH].group, "no ghost by default");
    CHECK(!zui_kinetic_set_state(&k, (zui_kinetic_state_t) 9, 0), "unknown state refused");
    CHECK(!zui_kinetic_set_state(0, ZUI_KINETIC_IDLE, 0), "NULL refused");
    CHECK(!zui_kinetic_frame(0, 0, &f) && !zui_kinetic_frame(&k, 0, 0), "NULL frame");
    zui_kinetic_init(0, ZUI_KINETIC_L1, 0);
    zui_kinetic_set_activity(0, ZUI_KINETIC_RUNNING, 0);
    zui_kinetic_set_visible(0, true);
    zui_kinetic_set_pointer(0, 0, 0, true);
    zui_kinetic_set_work(0, 1);
    zui_kinetic_set_pivot(0, 0, 0);
    CHECK(!zui_kinetic_settle(0, ZUI_KINETIC_IDLE, 0), "NULL calls are harmless");
    zui_kinetic_init(&k, (zui_kinetic_tier_t) 7, 0);
    CHECK(k.tier == ZUI_KINETIC_L2, "an unknown tier falls back to L2");
    zui_kinetic_set_activity(&k, (zui_kinetic_activity_t) 8, 0);
    CHECK(k.act == ZUI_KINETIC_NOT_RUNNING, "an unknown activity is ignored");
    CHECK(zui_kinetic_set_state(&k, ZUI_KINETIC_IDLE, 0), "same state is fine");
}

int main(void)
{
    test_sin();
    test_atan2();
    test_rates();
    test_spring();
    test_brackets();
    test_l2_breath();
    test_l1();
    test_l3();
    test_redraws();
    test_flash();
    test_hover();
    test_aim();
    test_firing();
    test_stall();
    test_both();
    test_reduce();
    test_determinism();
    test_phase_and_clock();
    test_misc();
    printf("%s test_zui_kinetic: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
