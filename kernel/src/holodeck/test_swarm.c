/* test_swarm.c — the Holodeck swarm.
 *
 * The headline is the INVERSION: on a centralised service the thousandth
 * viewer makes it worse; here every arrival brings upload with it, so the
 * crowd IS the delivery network. Everything else pins the safety and consent
 * properties that make that usable.
 */
#include <stdio.h>
#include <string.h>
#include "swarm.h"
#include "../robin_debanks/sha256.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)
#define D(x) ((double)(x) / (double)SR_ONE)

/* build a room whose chunks are the real digests of some made-up payloads */
static uint8_t PAYLOAD[8][64];
static void build(swarm_room_t *r, uint32_t nchunks, uint32_t origin) {
    uint8_t root[32]; for (int i=0;i<32;i++) root[i]=(uint8_t)i;
    swarm_init(r, root, nchunks, origin);
    for (uint32_t c = 0; c < nchunks; c++) {
        for (uint32_t b = 0; b < 64; b++) PAYLOAD[c][b] = (uint8_t)(c*7 + b);
        uint8_t d[32]; sha256(PAYLOAD[c], 64, d);
        swarm_set_chunk(r, c, d, 64);
    }
}

int main(void) {
    printf("=== the Holodeck swarm: watching together makes it faster ===\n");
    swarm_room_t r; build(&r, 8, 1000);   /* origin can push 1000 kbps */

    /* ============ THE INVERSION ============ */
    CHECK(swarm_capacity_kbps(&r) == 1000, "empty room: only the origin serves");
    uint32_t caps[6]; caps[0] = swarm_capacity_kbps(&r);
    for (uint32_t i = 1; i <= 5; i++) {
        swarm_join(&r, i, 500);           /* each viewer brings 500 kbps */
        caps[i] = swarm_capacity_kbps(&r);
    }
    printf("       capacity by audience size: ");
    for (uint32_t i = 0; i <= 5; i++) printf("%u ", caps[i]);
    printf("kbps\n");
    CHECK(caps[5] == 1000 + 5*500, "each viewer ADDS their upload to the pool");
    bool monotone = true;
    for (uint32_t i = 1; i <= 5; i++) if (caps[i] <= caps[i-1]) monotone = false;
    CHECK(monotone,
          "capacity RISES with every arrival — popularity makes it FASTER, "
          "the inverse of a centralised stream");

    /* The precise property, stated honestly: per-viewer share does not fall
     * to zero as it does with a fixed origin. Centralised gives origin/n
     * (-> 0); the swarm gives origin/n + per_peer, so it asymptotes to a
     * FLOOR of each peer's own upload rather than collapsing. */
    {
        uint32_t swarm_per_viewer  = caps[5] / 5;          /* 3500/5 = 700 */
        uint32_t central_per_viewer = 1000u / 5u;          /* origin only = 200 */
        printf("       per-viewer at n=5: swarm %u kbps vs centralised %u kbps\n",
               swarm_per_viewer, central_per_viewer);
        CHECK(swarm_per_viewer >= 500,
              "per-viewer share has a FLOOR of each peer's own upload (500), "
              "it does not collapse toward zero");
        CHECK(swarm_per_viewer > central_per_viewer * 3,
              "and it beats a fixed-origin stream by a widening margin as the "
              "audience grows");
    }

    /* ---- a peer that has watched becomes a source ---- */
    CHECK(swarm_chunk_sources(&r, 0) == 0, "nobody holds chunk 0 yet");
    swarm_peer_has(&r, 1, 0);
    swarm_peer_has(&r, 2, 0);
    CHECK(swarm_chunk_sources(&r, 0) == 2,
          "viewers who watched that far are now SOURCES for it");
    CHECK(swarm_chunk_sources(&r, 7) == 0, "a chunk nobody reached has no sources");

    /* ---- load spreads: the least-burdened holder is chosen ---- */
    {
        r.peer[0].chunks_served = 10;      /* peer 1 has served a lot */
        r.peer[1].chunks_served = 0;       /* peer 2 has served none */
        uint32_t src = swarm_select_source(&r, 0, 99);
        CHECK(src == 2, "the least-burdened holder is selected (load spreads)");
        uint32_t self = swarm_select_source(&r, 0, 2);
        CHECK(self == 1, "a peer is never told to fetch from itself");
    }

    /* ============ CONTENT ADDRESSING = NO TRUST NEEDED ============ */
    {
        CHECK(swarm_verify_chunk(&r, 3, PAYLOAD[3], 64),
              "genuine bytes match the chunk's own digest");
        uint8_t tampered[64]; memcpy(tampered, PAYLOAD[3], 64); tampered[17] ^= 0x01;
        CHECK(!swarm_verify_chunk(&r, 3, tampered, 64),
              "ONE flipped byte is caught — a hostile peer cannot substitute "
              "content under the right name");
        CHECK(!swarm_verify_chunk(&r, 3, PAYLOAD[4], 64),
              "serving a DIFFERENT chunk under this name is rejected");
        CHECK(!swarm_verify_chunk(&r, 3, PAYLOAD[3], 63), "a truncated chunk is rejected");
    }

    /* ---- shared playback, agreed by event sequence not a clock ---- */
    {
        swarm_advance(&r, 3);
        CHECK(swarm_position(&r) == 3, "the room advances to a shared position");
        swarm_advance(&r, 2);
        CHECK(swarm_position(&r) == 3, "playback never runs backwards on a stale event");
        /* two rooms fed the same ordinals agree without any central clock */
        swarm_room_t a, b; build(&a, 8, 0); build(&b, 8, 0);
        for (uint64_t o = 1; o <= 20; o++) { swarm_advance(&a, o); swarm_advance(&b, o); }
        CHECK(swarm_position(&a) == swarm_position(&b),
              "two independent rooms stay in sync from the ordinal alone "
              "(no central timekeeper)");
    }

    /* ============ COMPANIONS: OPT-IN AND BOUNDED ============ */
    {
        swarm_companion_t me, opted_in, opted_out;
        memset(&me,0,sizeof me); memset(&opted_in,0,sizeof opted_in); memset(&opted_out,0,sizeof opted_out);
        me.peer_id = 1;       me.sharing_enabled = true;       me.view[0] = SR_ONE;
        opted_in.peer_id = 2; opted_in.sharing_enabled = true; opted_in.view[1] = SR_ONE;
        opted_out.peer_id = 3;opted_out.sharing_enabled = false;opted_out.view[2] = SR_ONE;

        CHECK(swarm_companions_may_share(&me, &opted_in), "both opted in -> may share");
        CHECK(!swarm_companions_may_share(&me, &opted_out),
              "one owner opted OUT -> no sharing (consent is mutual)");

        swarm_companion_t others[2] = { opted_in, opted_out };
        uint32_t distinct = 0;
        double gain = D(swarm_companion_gain(&me, others, 2, &distinct));
        printf("       companion gain with 1 consenting + 1 refusing: R=%.2f distinct=%u\n",
               gain, distinct);
        CHECK(distinct == 2,
              "only the CONSENTING companion contributes a perspective");

        /* a crowd of identical companions adds nothing */
        swarm_companion_t clones[4];
        for (int i = 0; i < 4; i++) {
            memset(&clones[i], 0, sizeof clones[i]);
            clones[i].peer_id = (uint32_t)(10+i);
            clones[i].sharing_enabled = true;
            clones[i].view[0] = SR_ONE;              /* same view as me */
        }
        uint32_t dclone = 0;
        double gclone = D(swarm_companion_gain(&me, clones, 4, &dclone));
        printf("       4 IDENTICAL companions -> R=%.2f distinct=%u\n", gclone, dclone);
        CHECK(dclone == 1 && gclone < 1.05,
              "a crowd of identical companions adds NOTHING — growth needs difference");

        /* four genuinely different ones do help */
        swarm_companion_t varied[4];
        for (int i = 0; i < 4; i++) {
            memset(&varied[i], 0, sizeof varied[i]);
            varied[i].peer_id = (uint32_t)(20+i);
            varied[i].sharing_enabled = true;
            varied[i].view[i+1] = SR_ONE;            /* each a different axis */
        }
        uint32_t dv = 0;
        double gv = D(swarm_companion_gain(&me, varied, 4, &dv));
        printf("       4 DIFFERENT companions -> R=%.2f distinct=%u\n", gv, dv);
        CHECK(dv == 5 && gv > 4.0, "differing perspectives genuinely grow the companion");
        CHECK(gv > gclone + 3.0, "difference beats crowd size, by a wide margin");

        /* opting out means you gain nothing either — symmetric */
        swarm_companion_t quiet = me; quiet.sharing_enabled = false;
        CHECK(D(swarm_companion_gain(&quiet, varied, 4, 0)) == 0.0,
              "a companion whose owner opted out gains nothing (symmetric consent)");
    }

    /* ---- leaving shrinks the pool honestly ---- */
    {
        uint32_t before = swarm_capacity_kbps(&r);
        swarm_leave(&r, 1);
        CHECK(swarm_capacity_kbps(&r) == before - 500, "a departing viewer takes their upload with them");
    }

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
