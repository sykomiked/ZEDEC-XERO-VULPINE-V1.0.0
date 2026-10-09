/* test_reality.c — a sigil circuit computing against live variables.
 *
 * The circuit is a 4-node chain n0->n1->n2->n3 so every value is computable by
 * hand: n0 is a live input, n1 sums it, n2 sums that, n3 is a NONLINEAR gate.
 * Then the harder anchors: gcd(N,k) lanes come straight from the fabric, and a
 * sensor source returns NOT-BOUND until a backend is attached — the engine
 * never invents a reading.
 */
#include <stdio.h>
#include <string.h>
#include "reality.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

static int g_fired = 0;
static reality_val_t g_last = 0;
static void on_alarm(uint8_t node, reality_val_t v, void *ctx)
{
    (void) node;
    (void) ctx;
    g_fired++;
    g_last = v;
}

/* a fake sensor backend: returns 42 for sensor 7, refuses everything else */
static int fake_sensor(uint32_t id, reality_val_t *out, void *ctx)
{
    (void) ctx;
    if (id == 7) {
        *out = 42;
        return 0;
    }
    return -1;
}

/* build a chain n0-n1-n2-n3 on a {4/1} fabric (one lane) */
static void build_chain(sigil_t *s)
{
    sig_init(s, 1000);
    s->fab_n = 4;
    s->fab_k = 1;
    sig_add_node(s, 0, 0); /* n0 */
    sig_add_node(s, 1, 0); /* n1 */
    sig_add_node(s, 2, 0); /* n2 */
    sig_add_node(s, 3, 0); /* n3 */
    sig_add_edge(s, 0, 1);
    sig_add_edge(s, 1, 2);
    sig_add_edge(s, 2, 3);
}

int main(void)
{
    printf("=== The Reality Engine — a sigil computing against live variables ===\n");

    sigil_t s;
    build_chain(&s);

    reality_engine_t e;
    reality_init(&e, &s);

    /* n0 <- a pushable live feed; n1 = SUM(n0); n2 = SUM(n1);
     * n3 = THRESH: (sum of its inputs >= 3) ? 100 : 0  -- the nonlinearity */
    int32_t feed = reality_add_feed(&e, 0);
    CHECK(feed >= 0, "a live FEED source is created");
    CHECK(reality_bind(&e, 0, feed), "node 0 is bound to it (an input)");
    CHECK(reality_set_op(&e, 1, REAL_OP_SUM), "node 1 sums its predecessor");
    CHECK(reality_set_op(&e, 2, REAL_OP_SUM), "node 2 sums its predecessor");
    CHECK(reality_set_op(&e, 3, REAL_OP_THRESH), "node 3 is a nonlinear gate");
    CHECK(reality_set_thresh(&e, 3, 3, 100), "node 3: fire 100 when input >= 3");

    int rx = reality_on(&e, 3, REAL_CMP_GE, 50, on_alarm, 0);
    CHECK(rx >= 0, "a reaction is armed on node 3 crossing 50");

    /* ---- push a LIVE value of 5 and tick ---- */
    reality_feed(&e, feed, 5);
    uint32_t n = reality_tick(&e, 1);
    CHECK(n == 4, "all four nodes evaluate in one tick");
    CHECK(reality_read(&e, 0) == 5, "n0 carries the live input (5)");
    CHECK(reality_read(&e, 1) == 5, "n1 = SUM(n0) = 5");
    CHECK(reality_read(&e, 2) == 5, "n2 = SUM(n1) = 5");
    CHECK(reality_read(&e, 3) == 100,
          "n3 gate: 5 >= 3 so it fires 100 — the shape computed against reality");
    CHECK(g_fired == 1 && g_last == 100, "the reaction fired exactly once, at 100");

    /* ---- the world changes: push 1, tick; the gate closes ---- */
    reality_feed(&e, feed, 1);
    reality_tick(&e, 2);
    CHECK(reality_read(&e, 2) == 1, "n2 now follows the new input (1)");
    CHECK(reality_read(&e, 3) == 0, "n3 gate: 1 < 3 so it closes to 0");
    CHECK(g_fired == 1, "the reaction did NOT re-fire (it is edge-triggered)");

    /* ---- the world changes back: the reaction re-arms and fires again ---- */
    reality_feed(&e, feed, 9);
    reality_tick(&e, 3);
    CHECK(reality_read(&e, 3) == 100 && g_fired == 2,
          "rising past the threshold again fires the reaction a second time");

    /* ================= parallelism comes from the shape ================= */
    {
        sigil_t f4;
        sig_init(&f4, 1);
        f4.fab_n = 12;
        f4.fab_k = 4;
        sig_add_node(&f4, 0, 0);
        reality_engine_t e4;
        reality_init(&e4, &f4);
        CHECK(reality_lanes(&e4) == 4,
              "a {12/4} fabric runs the reality computation in gcd(12,4)=4 "
              "independent lanes — parallelism read straight off the drawing");

        sigil_t f1;
        sig_init(&f1, 1);
        f1.fab_n = 12;
        f1.fab_k = 5;
        sig_add_node(&f1, 0, 0);
        reality_engine_t e1;
        reality_init(&e1, &f1);
        CHECK(reality_lanes(&e1) == 1, "a {12/5} fabric is a single lane (gcd=1)");
    }

    /* ================= synthetic live signals ================= */
    {
        sigil_t s2;
        build_chain(&s2);
        reality_engine_t e2;
        reality_init(&e2, &s2);
        /* a triangle oscillator: amplitude 100, period 8 */
        int32_t osc = reality_add_osc(&e2, 100, 8);
        reality_bind(&e2, 0, osc);
        reality_set_op(&e2, 1, REAL_OP_SUM);
        reality_set_op(&e2, 2, REAL_OP_MAX);
        reality_set_op(&e2, 3, REAL_OP_SUM);

        reality_tick(&e2, 0);
        CHECK(reality_read(&e2, 0) == 0, "oscillator at phase 0 is 0");
        reality_tick(&e2, 4);
        CHECK(reality_read(&e2, 0) == 100, "oscillator at half period peaks at 100");
        reality_tick(&e2, 2);
        CHECK(reality_read(&e2, 0) == 50, "oscillator at quarter period is 50 — "
                                          "the circuit tracks a changing signal tick by tick");
    }

    /* ================= the sensor boundary (honest) ================= */
    {
        sigil_t s3;
        build_chain(&s3);
        reality_engine_t e3;
        reality_init(&e3, &s3);
        int32_t sen = reality_add_sensor(&e3, 7);
        reality_bind(&e3, 0, sen);
        reality_set_op(&e3, 1, REAL_OP_SUM);

        /* NO backend: a sensor read must NOT invent a value */
        reality_tick(&e3, 1);
        CHECK(!reality_node_valid(&e3, 0),
              "with no sensor backend, the sensor node is NOT VALID — the engine "
              "refuses to invent a reading");
        CHECK(!reality_node_valid(&e3, 1),
              "and invalidity propagates downstream to the nodes that used it");

        /* attach a real backend */
        reality_sensor_ops_t ops = {fake_sensor, 0};
        reality_set_sensor_ops(&e3, &ops);
        reality_tick(&e3, 2);
        CHECK(reality_node_valid(&e3, 0) && reality_read(&e3, 0) == 42,
              "once a backend is bound, the SAME circuit reads real signal (42)");
        CHECK(reality_read(&e3, 1) == 42 && reality_node_valid(&e3, 1),
              "and the value flows through, now marked valid");

        /* a sensor id the backend refuses stays invalid */
        int32_t sen2 = reality_add_sensor(&e3, 99);
        reality_bind(&e3, 2, sen2);
        reality_tick(&e3, 3);
        CHECK(!reality_node_valid(&e3, 2),
              "a sensor the backend cannot read stays invalid — no fabrication");
    }

    /* ================= robustness ================= */
    {
        /* ticking an engine with no bound inputs must not crash */
        sigil_t s4;
        build_chain(&s4);
        reality_engine_t e4;
        reality_init(&e4, &s4);
        for (int i = 0; i < 4; i++) reality_set_op(&e4, (uint8_t) i, REAL_OP_SUM);
        reality_tick(&e4, 1);
        CHECK(reality_read(&e4, 3) == 0, "an all-zero circuit evaluates to zero cleanly");

        /* a DIFF node computes first-minus-rest deterministically */
        sigil_t s5;
        sig_init(&s5, 2);
        s5.fab_n = 4;
        s5.fab_k = 1;
        sig_add_node(&s5, 0, 0);
        sig_add_node(&s5, 1, 0);
        sig_add_node(&s5, 2, 0);
        sig_add_edge(&s5, 0, 2);
        sig_add_edge(&s5, 1, 2); /* n0,n1 -> n2 */
        reality_engine_t e5;
        reality_init(&e5, &s5);
        int32_t a = reality_add_const(&e5, 10), b = reality_add_const(&e5, 3);
        reality_bind(&e5, 0, a);
        reality_bind(&e5, 1, b);
        reality_set_op(&e5, 2, REAL_OP_DIFF);
        reality_tick(&e5, 1);
        CHECK(reality_read(&e5, 2) == 7,
              "DIFF node = first predecessor (10) minus the rest (3) = 7");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
