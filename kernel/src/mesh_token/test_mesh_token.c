/* test_mesh_token.c — Mesh-Token External Settlement tests
 *
 * Tests settlement requests, Porter House admission gating,
 * state machine transitions (ADMITTED -> IN_TRANSIT -> CONFIRMED),
 * timeout detection, rejection handling, and M5 coverage.
 *
 * Author: Michael Laurence Curzi (c)
 * 36N9 Genetics, LLC — All Rights Reserved
 * License: SEL-3.3 (kernel component)
 */
#include "mesh_token.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static int feq(double a, double b, double eps) {
    double diff = fabs(a - b);
    double scale = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    if (scale < 1.0) scale = 1.0;
    return diff <= eps * scale;
}

static word168_t make_peer(uint8_t seed) {
    word168_t w;
    for (int i = 0; i < WORD168_OCTETS; i++) w.bytes[i] = (uint8_t)(seed + i);
    return w;
}

int main(void) {
    /* ===== init ===== */
    {
        porter_house_t ph;
        count_house_t ch;
        mesh_token_t mt;

        porter_house_init(&ph, 1, "test-porter");
        count_house_init(&ch, 1, "test-count");
        mesh_token_init(&mt, 1, "test-mesh", &ph, &ch);

        assert(mt.device_id == 1);
        assert(strcmp(mt.name, "test-mesh") == 0);
        assert(mt.num_settlements == 0);
        assert(mt.porter == &ph);
        assert(mt.count_house == &ch);
    }

    /* ===== settlement with Porter House TRUSTED seal: admitted ===== */
    {
        porter_house_t ph;
        count_house_t ch;
        mesh_token_t mt;

        porter_house_init(&ph, 1, "porter");
        count_house_init(&ch, 1, "count");
        count_house_set_crypto_reserves(&ch, SR_FROM_INT(1000));

        /* Seal settlement port as TRUSTED with min trust 500 */
        porter_house_seal_port(&ph, MT_SETTLEMENT_PORT, PH_SEAL_TRUSTED, 500);

        mesh_token_init(&mt, 1, "mesh", &ph, &ch);

        word168_t sender = make_peer(1);
        word168_t receiver = make_peer(2);

        /* High-trust peer: admitted */
        int32_t sid = mesh_token_settle(&mt, &sender, &receiver, 500, 600, 0);
        assert(sid > 0);

        mt_settlement_t *s = mesh_token_get(&mt, (uint32_t)sid);
        assert(s != NULL);
        assert(s->state == MT_SETTLEMENT_ADMITTED);
        assert(s->amount == 500);
        assert(s->created_cycle == 0);
    }

    /* ===== settlement rejected by Porter House (low trust) ===== */
    {
        porter_house_t ph;
        count_house_t ch;
        mesh_token_t mt;

        porter_house_init(&ph, 1, "porter");
        count_house_init(&ch, 1, "count");

        porter_house_seal_port(&ph, MT_SETTLEMENT_PORT, PH_SEAL_TRUSTED, 500);
        mesh_token_init(&mt, 1, "mesh", &ph, &ch);

        word168_t sender = make_peer(1);
        word168_t receiver = make_peer(2);

        /* Low-trust peer: rejected */
        int32_t sid = mesh_token_settle(&mt, &sender, &receiver, 500, 100, 0);
        assert(sid == -2);
        assert(mt.total_rejected == 1);
    }

    /* ===== state machine: ADMITTED -> IN_TRANSIT -> CONFIRMED ===== */
    {
        porter_house_t ph;
        count_house_t ch;
        mesh_token_t mt;

        porter_house_init(&ph, 1, "porter");
        count_house_init(&ch, 1, "count");
        porter_house_seal_port(&ph, MT_SETTLEMENT_PORT, PH_SEAL_TRUSTED, 500);
        mesh_token_init(&mt, 1, "mesh", &ph, &ch);

        word168_t sender = make_peer(1);
        word168_t receiver = make_peer(2);

        int32_t sid = mesh_token_settle(&mt, &sender, &receiver, 1000, 800, 10);
        assert(sid > 0);

        mt_settlement_t *s = mesh_token_get(&mt, (uint32_t)sid);
        assert(s->state == MT_SETTLEMENT_ADMITTED);

        /* Advance to IN_TRANSIT */
        assert(mesh_token_advance(&mt, 20) == 0);
        assert(s->state == MT_SETTLEMENT_IN_TRANSIT);

        /* Acknowledge */
        assert(mesh_token_ack(&mt, (uint32_t)sid, 30) == 0);
        assert(s->state == MT_SETTLEMENT_CONFIRMED);
        assert(s->confirmed_cycle == 30);
        assert(mt.total_confirmed == 1);
        assert(mt.total_settled == 1000);
    }

    /* ===== timeout detection ===== */
    {
        porter_house_t ph;
        count_house_t ch;
        mesh_token_t mt;

        porter_house_init(&ph, 1, "porter");
        count_house_init(&ch, 1, "count");
        porter_house_seal_port(&ph, MT_SETTLEMENT_PORT, PH_SEAL_OPEN, 0);
        mesh_token_init(&mt, 1, "mesh", &ph, &ch);

        word168_t sender = make_peer(1);
        word168_t receiver = make_peer(2);

        int32_t sid = mesh_token_settle(&mt, &sender, &receiver, 200, 0, 0);
        assert(sid > 0);

        mesh_token_advance(&mt, 0);

        mt_settlement_t *s = mesh_token_get(&mt, (uint32_t)sid);
        assert(s->state == MT_SETTLEMENT_IN_TRANSIT);

        /* Not yet timed out */
        uint32_t timed = mesh_token_check_timeouts(&mt, 500);
        assert(timed == 0);
        assert(s->state == MT_SETTLEMENT_IN_TRANSIT);

        /* Now times out */
        timed = mesh_token_check_timeouts(&mt, MT_ACK_TIMEOUT_CYCLES + 10);
        assert(timed == 1);
        assert(s->state == MT_SETTLEMENT_TIMEOUT);
        assert(mt.total_timeout == 1);
    }

    /* ===== Porter House CLOSED blocks all settlements ===== */
    {
        porter_house_t ph;
        count_house_t ch;
        mesh_token_t mt;

        porter_house_init(&ph, 1, "porter");
        count_house_init(&ch, 1, "count");
        porter_house_close_port(&ph, MT_SETTLEMENT_PORT);
        mesh_token_init(&mt, 1, "mesh", &ph, &ch);

        word168_t sender = make_peer(1);
        word168_t receiver = make_peer(2);

        int32_t sid = mesh_token_settle(&mt, &sender, &receiver, 500, 1000, 0);
        assert(sid == -2);
        assert(mt.total_rejected == 1);
    }

    /* ===== ALLOWLIST: only listed peers can settle ===== */
    {
        porter_house_t ph;
        count_house_t ch;
        mesh_token_t mt;

        porter_house_init(&ph, 1, "porter");
        count_house_init(&ch, 1, "count");
        porter_house_seal_port(&ph, MT_SETTLEMENT_PORT, PH_SEAL_ALLOWLIST, 0);

        word168_t trusted_peer = make_peer(10);
        porter_house_allowlist_add(&ph, MT_SETTLEMENT_PORT, &trusted_peer);

        mesh_token_init(&mt, 1, "mesh", &ph, &ch);

        word168_t sender = make_peer(1);
        word168_t stranger = make_peer(99);

        /* Listed peer: admitted */
        int32_t sid1 = mesh_token_settle(&mt, &sender, &trusted_peer, 300, 0, 0);
        assert(sid1 > 0);

        /* Stranger: rejected */
        int32_t sid2 = mesh_token_settle(&mt, &sender, &stranger, 300, 1000, 0);
        assert(sid2 == -2);
    }

    /* ===== coverage computation ===== */
    {
        porter_house_t ph;
        count_house_t ch;
        mesh_token_t mt;

        porter_house_init(&ph, 1, "porter");
        count_house_init(&ch, 1, "count");
        porter_house_seal_port(&ph, MT_SETTLEMENT_PORT, PH_SEAL_OPEN, 0);
        mesh_token_init(&mt, 1, "mesh", &ph, &ch);

        word168_t sender = make_peer(1);
        word168_t receiver = make_peer(2);

        /* Create and complete one settlement */
        int32_t sid = mesh_token_settle(&mt, &sender, &receiver, 100, 0, 0);
        mesh_token_advance(&mt, 0);
        mesh_token_ack(&mt, (uint32_t)sid, 50);

        /* Create one rejected settlement */
        porter_house_close_port(&ph, MT_SETTLEMENT_PORT);
        mesh_token_settle(&mt, &sender, &receiver, 100, 0, 0);

        mesh_token_update_coverage(&mt);
        /* r = 1 confirmed / (1 confirmed + 1 rejected) = 0.5
         * ell = 0 in_progress / 2 active = 0 (both resolved) */
        assert(feq(mt.m5.r, 0.5, 1e-9));
        assert(feq(mt.m5.ell, 0.0, 1e-9));
    }

    /* ===== capacity limit ===== */
    {
        porter_house_t ph;
        count_house_t ch;
        mesh_token_t mt;

        porter_house_init(&ph, 1, "porter");
        count_house_init(&ch, 1, "count");
        porter_house_seal_port(&ph, MT_SETTLEMENT_PORT, PH_SEAL_OPEN, 0);
        mesh_token_init(&mt, 1, "mesh", &ph, &ch);

        word168_t sender = make_peer(1);
        word168_t receiver = make_peer(2);

        for (uint32_t i = 0; i < MT_MAX_SETTLEMENTS; i++) {
            int32_t sid = mesh_token_settle(&mt, &sender, &receiver, 1, 0, 0);
            assert(sid > 0);
        }

        /* Next should fail */
        int32_t sid = mesh_token_settle(&mt, &sender, &receiver, 1, 0, 0);
        assert(sid == -1);
    }

    printf("All Mesh-Token External Settlement tests passed\n");
    return 0;
}
