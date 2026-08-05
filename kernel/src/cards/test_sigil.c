/* test_sigil.c — the spell shape as a circuit.
 *
 * The headline fixture is REAL: every number in the seal_24525 case was
 * measured off the shipped 1222x1455 PNG (Acoustomancy_Set1/seal_24525.png,
 * OLPIRT HPOU) — the 12-point star, the chord step of 5, the 49px lattice,
 * and the ink-visible red trace. If the extractor or the graph model
 * drifts, this test stops agreeing with the physical card.
 *
 * KNOWN LIMIT OF INK MEASUREMENT: the card's true generative path has 16
 * nodes and 15 strokes (SHA-256 derivation — see test_refinery.c, which
 * carries the byte-exact fixture). Two nodes, (0,1) and (0,2) in
 * generator coordinates, sit under heavy star ink and are invisible to a
 * pixel scan, so the measured subgraph here has 14 nodes. This is the
 * measured-ink truth, kept deliberately: the Refinery DERIVES circuits
 * from text; rasters only ever verify, precisely because of this gap.
 */
#include <stdio.h>
#include "sigil.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

/* ---- seal_24525 "OLPIRT HPOU", as measured ---- */
static const uint8_t NODES[][2] = {
    {0,0},{1,1},{2,1},{3,1},{4,1},{1,2},{2,2},
    {3,2},{4,2},{4,3},{0,3},{1,3},{2,3},{3,3}
};
static const uint8_t EDGES[][2] = {
    {0,4},{0,5},{1,2},{2,3},{2,6},{3,9},{5,6},
    {6,7},{6,12},{7,8},{8,9},{10,11},{11,12},{12,13}
};

static void build_24525(sigil_t *s) {
    sig_init(s, 24525);
    s->fab_n = 12; s->fab_k = 5;      /* the black star: {12/5} */
    s->pitch = 49;                    /* measured lattice spacing */
    for (unsigned i = 0; i < sizeof(NODES)/sizeof(NODES[0]); i++)
        sig_add_node(s, NODES[i][0], NODES[i][1]);
    for (unsigned i = 0; i < sizeof(EDGES)/sizeof(EDGES[0]); i++)
        sig_add_edge(s, EDGES[i][0], EDGES[i][1]);
}

int main(void) {
    printf("=== the spell shape is the circuit diagram ===\n");

    /* ---------- gcd: the arithmetic that decides parallelism ---------- */
    CHECK(sig_gcd(12,5) == 1, "gcd(12,5)=1");
    CHECK(sig_gcd(12,4) == 4, "gcd(12,4)=4");
    CHECK(sig_gcd(12,6) == 6, "gcd(12,6)=6");

    /* ---------- THE CENTRAL CLAIM ----------
     * The star's step decides how many independent lanes the fabric has. */
    {
        sigil_t s; sig_init(&s, 0);
        s.fab_n = 12;
        printf("\n       fabric lanes by chord step, N=12:\n");
        const uint8_t k[]  = {1,2,3,4,5,6};
        const uint32_t exp[]= {1,2,3,4,1,6};
        bool ok = true;
        for (unsigned i = 0; i < 6; i++) {
            s.fab_k = k[i];
            uint32_t lanes = sig_fabric_lanes(&s);
            printf("         {12/%u} -> %u lane%s, lane length %u, diameter %u\n",
                   k[i], lanes, lanes==1?"":"s", 12u/lanes, sig_fabric_diameter(&s));
            if (lanes != exp[i]) ok = false;
        }
        CHECK(ok, "LANE COUNT = gcd(N,k) — the shape sets the parallelism");

        s.fab_k = 5;
        CHECK(sig_fabric_unicursal(&s),
              "{12/5} is unicursal: one circuit, fully serialized");
        s.fab_k = 4;
        CHECK(!sig_fabric_unicursal(&s) && sig_fabric_lanes(&s) == 4,
              "{12/4} splits into 4 independent parallel lanes");
        s.fab_k = 6;
        CHECK(sig_fabric_degree(&s) == 1,
              "{12/6} is diameters only — degree 1, +k and -k coincide");
        s.fab_k = 5;
        CHECK(sig_fabric_degree(&s) == 2, "a proper star has degree 2");
    }

    /* ---------- the real card ---------- */
    {
        sigil_t s; build_24525(&s);
        printf("\n       seal_24525 OLPIRT HPOU (measured off the shipped PNG):\n");
        printf("         fabric  {%u/%u}  lanes=%u  diameter=%u\n",
               s.fab_n, s.fab_k, sig_fabric_lanes(&s), sig_fabric_diameter(&s));
        printf("         circuit %u nodes, %u strokes, %ux%u lattice @%upx\n",
               s.n_nodes, s.n_edges, s.cols, s.rows, s.pitch);

        CHECK(s.n_nodes == 14, "14 nodes recovered");
        CHECK(s.n_edges == 14, "14 primitive strokes recovered");
        CHECK(s.cols == 5 && s.rows == 4, "5x4 kamea lattice");
        CHECK(sig_components(&s) == 1,
              "the trace is ONE connected circuit (matches the single red blob)");
        CHECK(sig_odd_vertices(&s) == 6, "6 loose ends, as measured");
        CHECK(sig_degree(&s, 6) == 4, "node (2,2) is the 4-way hub");

        sig_schedule_t sc;
        CHECK(sig_schedule(&s, -1, &sc), "the circuit schedules");
        printf("\n         event-sequence schedule (no clock consulted):\n");
        for (uint8_t w = 0; w < sc.n_waves; w++) {
            printf("           wave %u:", w);
            for (uint8_t i = sc.wave_start[w]; i < sc.wave_start[w+1]; i++)
                printf(" (%u,%u)", s.node[sc.order[i]].col, s.node[sc.order[i]].row);
            printf("\n");
        }
        printf("         critical path %u waves, peak concurrency %u\n",
               sc.n_waves, sc.width);
        CHECK(sc.n_scheduled == s.n_nodes, "EVERY node gets scheduled");
        CHECK(sc.width > 1, "the shape admits genuine concurrency");
        CHECK(sc.n_waves < s.n_nodes,
              "the critical path is SHORTER than the node count — "
              "the circuit is not merely a chain");
    }

    /* ---------- a disconnected trace must still fully schedule ---------- */
    {
        sigil_t s; sig_init(&s, 1);
        sig_add_node(&s,0,0); sig_add_node(&s,1,0);   /* component A */
        sig_add_node(&s,3,3); sig_add_node(&s,4,3);   /* component B */
        sig_add_edge(&s,0,1); sig_add_edge(&s,2,3);
        CHECK(sig_components(&s) == 2, "two components detected");
        sig_schedule_t sc; sig_schedule(&s, -1, &sc);
        CHECK(sc.n_scheduled == 4,
              "an unreachable component is still scheduled, never dropped");
    }

    /* ---------- red-team regression: entry index must not be truncated ----
     * entry>=256 whose low byte was < n_nodes used to pass the guard and
     * then `seen[e0]` wrote out of bounds on a 32-byte stack array. It must
     * now fall back to the canonical entry and schedule cleanly. Run this
     * file under -fsanitize=address,undefined to prove no OOB. */
    {
        sigil_t s; sig_init(&s, 9);
        sig_add_node(&s,0,0); sig_add_node(&s,1,0);
        sig_add_node(&s,2,0); sig_add_node(&s,3,0);
        sig_add_edge(&s,0,1); sig_add_edge(&s,1,2); sig_add_edge(&s,2,3);
        sig_schedule_t sc;
        CHECK(sig_schedule(&s, 256, &sc),
              "entry=256 (low byte 0 < n_nodes) is accepted via safe fallback");
        CHECK(sc.n_scheduled == 4,
              "and every node scheduled exactly once (no OOB, no double-count)");
        CHECK(sig_schedule(&s, 100000, &sc) && sc.n_scheduled == 4,
              "a large entry also falls back safely");
    }

    /* ---------- structural guards ---------- */
    {
        sigil_t s; sig_init(&s, 2);
        sig_add_node(&s,0,0); sig_add_node(&s,1,0);
        CHECK(!sig_add_node(&s,0,0), "one node per lattice site");
        CHECK(sig_add_edge(&s,0,1), "edge added");
        CHECK(!sig_add_edge(&s,1,0), "the same stroke cannot be added twice");
        CHECK(!sig_add_edge(&s,0,0), "a stroke cannot loop to its own node");
        CHECK(!sig_add_edge(&s,0,9), "an edge to a nonexistent node is refused");

        sigil_t e; sig_init(&e, 3);
        sig_schedule_t sc;
        CHECK(!sig_schedule(&e, -1, &sc), "an empty sigil does not schedule");
        CHECK(sig_components(&e) == 0, "an empty sigil has no components");
        CHECK(sig_fabric_lanes(&e) == 0, "no star means no fabric");
    }

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
