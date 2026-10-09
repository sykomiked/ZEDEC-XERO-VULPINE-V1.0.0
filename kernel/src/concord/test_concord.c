/* test_concord.c — the Commons.
 *
 * Each test pins one property of the mutual-sovereignty design:
 * complementary matching, symmetric divides, rehabilitative quarantine,
 * non-coercion, and a companion that grows with diverse use.
 */
#include <stdio.h>
#include <string.h>
#include "concord.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)
#define D(x) ((double) (x) / (double) SR_ONE)

static void vec(surplus_real_t v[CON_DIM], int axis, double mag)
{
    for (int i = 0; i < (int) CON_DIM; i++) v[i] = SR_ZERO;
    v[axis] = SR_FROM_FLOAT(mag);
}

int main(void)
{
    printf("=== the Commons: ISF-driven, non-coercive social matching ===\n");

    /* ---- complementary compatibility: the core claim ---- */
    {
        con_person_t a, b, same, opp;
        vec(a.interest, 0, 1.0);
        vec(b.interest, 3, 1.0); /* orthogonal -> complementary */
        memcpy(same.interest, a.interest, sizeof same.interest);
        for (int i = 0; i < (int) CON_DIM; i++) opp.interest[i] = SR_ZERO;
        opp.interest[0] = SR_FROM_FLOAT(-1.0); /* diametrically opposed */

        double s_comp = D(con_surplus(&a, &b));
        double s_same = D(con_surplus(&a, &same));
        double s_opp = D(con_surplus(&a, &opp));
        printf("       surplus: complementary=%.3f  identical=%.3f  opposite=%.3f\n", s_comp,
               s_same, s_opp);
        CHECK(s_comp > 0.9, "complementary interests yield HIGH surplus");
        CHECK(s_same < 0.05, "identical interests yield near-zero surplus (no echo chamber)");
        CHECK(s_opp < 0.05, "diametrically opposed also near-zero (no clash-pairing)");
        CHECK(s_comp > s_same && s_comp > s_opp,
              "the commons is pulled toward COMPLEMENTARY, not same or opposite");
    }

    /* ---- recommendation ranks by surplus, never by engagement ---- */
    {
        con_commons_t c;
        con_init(&c);
        surplus_real_t v[CON_DIM];
        vec(v, 0, 1.0);
        con_join(&c, 1, v, 200); /* me, sociable */
        vec(v, 1, 1.0);
        con_join(&c, 2, v, 200); /* complementary */
        vec(v, 0, 1.0);
        con_join(&c, 3, v, 200); /* identical to me */
        vec(v, 4, 1.0);
        con_join(&c, 4, v, 200); /* complementary */
        con_match_t out[8];
        uint32_t n = con_recommend(&c, 1, out, 8);
        printf("       recommended %u people; top surplus=%.3f (id %u)\n", n, D(out[0].surplus),
               out[0].id);
        CHECK(n >= 2, "recommendations returned");
        CHECK(out[0].id != 3, "the identical person is NOT the top pick");
        CHECK(D(out[0].surplus) >= D(out[n - 1].surplus),
              "ranked by surplus, best first — no recency/retention term");
    }

    /* ---- non-coercion: an introvert is not pushed past their budget ---- */
    {
        con_commons_t c;
        con_init(&c);
        surplus_real_t v[CON_DIM];
        vec(v, 0, 1.0);
        con_join(&c, 10, v, 0); /* deep introvert */
        for (uint32_t k = 0; k < 20; k++) {
            vec(v, (int) (k % CON_DIM), 1.0);
            con_join(&c, 100 + k, v, 255);
        }
        con_match_t out[32];
        uint32_t n = con_recommend(&c, 10, out, 32);
        printf("       deep introvert (sociability 0) offered %u connections\n", n);
        CHECK(n == 0, "a deep introvert is left alone until THEY reach out (non-coercion)");

        vec(v, 0, 1.0);
        con_join(&c, 11, v, 255); /* extrovert */
        n = con_recommend(&c, 11, out, 32);
        printf("       extrovert (sociability 255) offered %u connections\n", n);
        CHECK(n > 0 && n <= 16, "an extrovert is offered many, but never beyond their budget");
    }

    /* ---- symmetric divide: no one-way blocking / spying ---- */
    {
        con_commons_t c;
        con_init(&c);
        surplus_real_t v[CON_DIM];
        vec(v, 0, 1.0);
        con_join(&c, 1, v, 200);
        vec(v, 1, 1.0);
        con_join(&c, 2, v, 200);
        CHECK(con_can_see(&c, 1, 2) && con_can_see(&c, 2, 1), "before: both can see each other");
        con_report_boundary(&c, 1, 2, 100); /* 1 reports a boundary from 2 */
        CHECK(!con_can_see(&c, 1, 2), "reporter can no longer see the other");
        CHECK(!con_can_see(&c, 2, 1),
              "AND the other can no longer see the reporter — the divide is SYMMETRIC "
              "(no one-way block that permits spying)");
        CHECK(!con_may_match(&c, 1, 2), "divided people are never introduced");
    }

    /* ---- rehabilitative quarantine: bully meets bullies, then recovers ---- */
    {
        con_commons_t c;
        con_init(&c);
        surplus_real_t v[CON_DIM];
        vec(v, 0, 1.0);
        con_join(&c, 1, v, 200); /* ordinary member / witness */
        vec(v, 1, 1.0);
        con_join(&c, 2, v, 200); /* the bully */
        vec(v, 2, 1.0);
        con_join(&c, 3, v, 200); /* another bully */
        vec(v, 3, 1.0);
        con_join(&c, 4, v, 200); /* a third witness */
        vec(v, 4, 1.0);
        con_join(&c, 5, v, 200); /* a fourth witness */

        /* Person 2 crosses boundaries against DISTINCT peers. One reporter can
         * never quarantine anyone: a repeat by the SAME reporter is a no-op, so
         * quarantine takes REAL consensus. Two distinct hits (−40 each from 100)
         * only STRAIN; the third distinct reporter clears CON_QUAR_MAX. */
        con_report_boundary(&c, 1, 2, 5);
        con_report_boundary(&c, 3, 2, 5);
        con_report_boundary(&c, 1, 2, 5); /* SAME reporter again: deliberately a no-op */
        CHECK(con_standing(con_get(&c, 2)) != CON_QUARANTINED,
              "two distinct reporters + a repeat only STRAIN — no unilateral quarantine");
        con_report_boundary(&c, 4, 2, 5); /* the THIRD distinct reporter tips it */
        con_person_t *bully = con_get(&c, 2);
        printf("       bully standing after distinct reports: %s (score %d)\n",
               con_standing_name(con_standing(bully)), bully->standing_score);
        CHECK(con_standing(bully) == CON_QUARANTINED,
              "THREE distinct reporters (real consensus) -> QUARANTINED");

        /* drive person 3 to quarantine too — three DISTINCT reporters again */
        con_report_boundary(&c, 1, 3, 5);
        con_report_boundary(&c, 4, 3, 5);
        con_report_boundary(&c, 5, 3, 5);
        /* let the mutual divides between the bullies expire so matching is
         * about STANDING pools, not the specific incidents */
        con_tick(&c, 6);
        CHECK(con_standing(con_get(&c, 2)) == CON_QUARANTINED &&
                  con_standing(con_get(&c, 3)) == CON_QUARANTINED,
              "both bullies quarantined");
        CHECK(
            con_may_match(&c, 2, 3),
            "a quarantined person IS matched with other quarantined people (bully meets bullies)");
        CHECK(!con_may_match(&c, 1, 2),
              "but NOT with people in good standing (no pairing bullies onto victims)");

        /* time passes; behaviour settles; standing recovers; rejoin commons */
        con_tick(&c, 100);
        CHECK(con_standing(con_get(&c, 2)) == CON_OK,
              "standing RECOVERS over time — rehabilitation, not a permanent brand");
        CHECK(con_may_match(&c, 1, 2), "the recovered person rejoins the whole commons");
    }

    /* ---- divides are temporary ---- */
    {
        con_commons_t c;
        con_init(&c);
        surplus_real_t v[CON_DIM];
        vec(v, 0, 1.0);
        con_join(&c, 1, v, 200);
        vec(v, 1, 1.0);
        con_join(&c, 2, v, 200);
        con_report_boundary(&c, 1, 2, 10);
        CHECK(con_divided(&c, 1, 2), "divide is active");
        con_tick(&c, 11);
        CHECK(!con_divided(&c, 1, 2), "divide expired after its duration (temporary, not forever)");
    }

    /* ---- the companion grows with DIVERSE use across instances ---- */
    {
        con_companion_t redundant;
        con_companion_init(&redundant);
        surplus_real_t e[CHG_DIM];
        vec(e, 0, 1.0);
        for (int k = 0; k < 5; k++) con_companion_link(&redundant, e); /* same use x5 */
        uint32_t dr = 0;
        double Rr = D(con_companion_reach(&redundant, &dr));
        printf("       5 redundant instances -> R=%.3f distinct=%u\n", Rr, dr);
        CHECK(dr == 1 && Rr < 1.05,
              "five identical instances do NOT inflate the companion (honest growth)");

        con_companion_t diverse;
        con_companion_init(&diverse);
        for (int k = 0; k < 5; k++) {
            vec(e, k, 1.0);
            con_companion_link(&diverse, e);
        }
        uint32_t dd = 0;
        double Rd = D(con_companion_reach(&diverse, &dd));
        printf("       5 diverse instances   -> R=%.3f distinct=%u\n", Rd, dd);
        CHECK(dd == 5 && Rd > 4.0,
              "diverse use across devices genuinely grows the companion's reach");
        CHECK(Rd > Rr + 3.0, "linking the SAME companion into many devices helps only when the "
                             "devices bring different experience");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
