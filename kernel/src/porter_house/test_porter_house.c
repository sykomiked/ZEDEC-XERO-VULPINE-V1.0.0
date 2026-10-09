/* test_porter_house.c — Functional correctness tests for the Porter
 * House port-seal / wall-interface firewall.
 */
#include "porter_house.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static int feq(double a, double b, double eps)
{
    double diff = fabs(a - b);
    double scale = fabs(a) > fabs(b) ? fabs(a) : fabs(b);
    if (scale < 1.0) scale = 1.0;
    return diff <= eps * scale;
}

static word168_t make_peer(uint8_t seed)
{
    word168_t w;
    for (int i = 0; i < WORD168_OCTETS; i++) w.bytes[i] = (uint8_t) (seed + i);
    return w;
}

int main(void)
{
    /* ===== init defaults ===== */
    porter_house_t ph;
    porter_house_init(&ph, 1, "MainEntrance");
    assert(ph.device_id == 1);
    assert(strcmp(ph.name, "MainEntrance") == 0);
    assert(ph.num_seals == 0);
    assert(ph.total_admitted == 0 && ph.total_rejected == 0);
    assert(feq(ph.m5.r, 1.0, 1e-9));   /* no attempts yet -> neutral pass */
    assert(feq(ph.m5.ell, 0.0, 1e-9)); /* no seals -> unstaffed */

    /* ===== unsealed port: admits by default (opt-in firewall) ===== */
    {
        word168_t p = make_peer(1);
        assert(porter_house_admit(&ph, 8080, &p, 0));
        assert(ph.total_admitted == 1);
        assert(porter_house_find_seal(&ph, 8080) == -1); /* still no seal record created */
    }

    /* ===== PH_SEAL_CLOSED rejects unconditionally ===== */
    {
        int32_t idx = porter_house_seal_port(&ph, 9090, PH_SEAL_CLOSED, 0);
        assert(idx >= 0);
        word168_t p = make_peer(2);
        assert(!porter_house_admit(&ph, 9090, &p, 1000)); /* even max trust is refused */
        assert(ph.seals[idx].rejected_count == 1);
        assert(ph.total_rejected == 1);
    }

    /* ===== PH_SEAL_TRUSTED: threshold semantics ===== */
    {
        porter_house_seal_port(&ph, 7000, PH_SEAL_TRUSTED, 500);
        word168_t p = make_peer(3);
        assert(!porter_house_admit(&ph, 7000, &p, 400)); /* below threshold */
        assert(porter_house_admit(&ph, 7000, &p, 500));  /* exactly at threshold: admitted */
        assert(porter_house_admit(&ph, 7000, &p, 600));  /* above threshold */
    }

    /* ===== PH_SEAL_ALLOWLIST ===== */
    {
        porter_house_seal_port(&ph, 6000, PH_SEAL_ALLOWLIST, 0);
        word168_t listed = make_peer(10);
        word168_t stranger = make_peer(99);

        assert(
            !porter_house_admit(&ph, 6000, &listed, 1000)); /* not listed yet, trust irrelevant */
        assert(porter_house_allowlist_add(&ph, 6000, &listed) == 0);
        assert(porter_house_admit(&ph, 6000, &listed, 0));       /* listed now, trust irrelevant */
        assert(!porter_house_admit(&ph, 6000, &stranger, 1000)); /* never listed */

        /* Idempotent add */
        int32_t seal_idx = porter_house_find_seal(&ph, 6000);
        uint32_t before = ph.seals[seal_idx].allowlist_count;
        assert(porter_house_allowlist_add(&ph, 6000, &listed) == 0);
        assert(ph.seals[seal_idx].allowlist_count == before);
    }

    /* ===== allowlist capacity ===== */
    {
        porter_house_t cap;
        porter_house_init(&cap, 2, "CapTest");
        porter_house_seal_port(&cap, 5000, PH_SEAL_ALLOWLIST, 0);
        for (uint32_t i = 0; i < PH_MAX_ALLOWLIST; i++) {
            word168_t p = make_peer((uint8_t) (i + 1));
            assert(porter_house_allowlist_add(&cap, 5000, &p) == 0);
        }
        word168_t overflow = make_peer(250);
        assert(porter_house_allowlist_add(&cap, 5000, &overflow) == -2);
    }

    /* ===== seal capacity ===== */
    {
        porter_house_t cap;
        porter_house_init(&cap, 3, "SealCapTest");
        for (uint32_t i = 0; i < PH_MAX_SEALS; i++) {
            int32_t idx = porter_house_seal_port(&cap, (uint16_t) (1000 + i), PH_SEAL_OPEN, 0);
            assert(idx == (int32_t) i);
        }
        assert(porter_house_seal_port(&cap, 9999, PH_SEAL_OPEN, 0) == -1);
    }

    /* ===== open_port / close_port ===== */
    {
        porter_house_t g;
        porter_house_init(&g, 4, "GateTest");
        word168_t p = make_peer(20);

        porter_house_seal_port(&g, 4000, PH_SEAL_TRUSTED, 999999); /* effectively unpassable */
        assert(!porter_house_admit(&g, 4000, &p, 100));

        porter_house_close_port(&g, 4000);
        assert(ph_seal_mode_name(g.seals[porter_house_find_seal(&g, 4000)].mode) &&
               strcmp(ph_seal_mode_name(PH_SEAL_CLOSED), "CLOSED") == 0);
        assert(!porter_house_admit(&g, 4000, 0,
                                   1000000)); /* closed beats even NULL/omnipotent trust */

        porter_house_open_port(&g, 4000);
        assert(porter_house_admit(&g, 4000, 0, 0)); /* fully open now */

        /* close_port on a NEVER-sealed port must still take effect --
         * a lockdown call silently doing nothing would be a real bug. */
        assert(porter_house_find_seal(&g, 3333) == -1);
        porter_house_close_port(&g, 3333);
        int32_t idx = porter_house_find_seal(&g, 3333);
        assert(idx >= 0);
        assert(g.seals[idx].mode == PH_SEAL_CLOSED);
        assert(!porter_house_admit(&g, 3333, &p, 1000));
    }

    /* ===== reconfiguring a seal preserves allowlist/stats ===== */
    {
        porter_house_t r;
        porter_house_init(&r, 5, "ReconfigTest");
        porter_house_seal_port(&r, 2222, PH_SEAL_ALLOWLIST, 0);
        word168_t p = make_peer(30);
        porter_house_allowlist_add(&r, 2222, &p);
        porter_house_admit(&r, 2222, &p, 0); /* admitted_count -> 1 */

        int32_t idx = porter_house_seal_port(&r, 2222, PH_SEAL_TRUSTED, 100);
        assert(r.seals[idx].mode == PH_SEAL_TRUSTED);
        assert(r.seals[idx].allowlist_count == 1); /* preserved even though now irrelevant */
        assert(r.seals[idx].admitted_count == 1);  /* history preserved */
    }

    /* ===== coverage formula (independently recomputed) ===== */
    {
        porter_house_t c;
        porter_house_init(&c, 6, "CoverageTest");
        porter_house_seal_port(&c, 100, PH_SEAL_OPEN, 0);
        porter_house_seal_port(&c, 200, PH_SEAL_TRUSTED, 500);
        word168_t p = make_peer(40);

        porter_house_admit(&c, 100, &p, 0);   /* admitted (OPEN) */
        porter_house_admit(&c, 200, &p, 600); /* admitted (TRUSTED, passes) */
        porter_house_admit(&c, 200, &p, 100); /* rejected (TRUSTED, fails) */

        surplus_real_t cov = porter_house_update_coverage(&c);
        double expected_r = 2.0 / 3.0;   /* 2 admitted / 3 attempts */
        double expected_ell = 1.0 / 2.0; /* 1 of 2 seals staffed (non-OPEN) */
        assert(feq(c.m5.r, expected_r, 1e-9));
        assert(feq(c.m5.ell, expected_ell, 1e-9));
        double expected_cov = (expected_r * expected_ell) / 1.8;
        assert(feq(cov, expected_cov, 1e-9));
    }

    /* ===== ph_seal_mode_name ===== */
    {
        assert(strcmp(ph_seal_mode_name(PH_SEAL_OPEN), "OPEN") == 0);
        assert(strcmp(ph_seal_mode_name(PH_SEAL_TRUSTED), "TRUSTED") == 0);
        assert(strcmp(ph_seal_mode_name(PH_SEAL_ALLOWLIST), "ALLOWLIST") == 0);
        assert(strcmp(ph_seal_mode_name(PH_SEAL_CLOSED), "CLOSED") == 0);
        assert(strcmp(ph_seal_mode_name((ph_seal_mode_t) 99), "UNKNOWN") == 0);
    }

    printf("All Porter House tests passed\n");
    return 0;
}
