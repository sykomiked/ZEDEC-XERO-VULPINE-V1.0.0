/* test_abacus.c — multilateral clearing: invariants + measured value.
 *
 *   gcc -std=c11 -Wall -Wextra -Isrc/abacus -Isrc/rational \
 *       src/abacus/test_abacus.c src/abacus/abacus.c src/rational/rational.c \
 *       -o /tmp/test_abacus && /tmp/test_abacus
 */
#include <stdio.h>
#include "abacus.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

static const char *S(rat_t r)
{
    static char b[4][64];
    static int i = 0;
    i = (i + 1) & 3;
    rat_to_string(r, b[i], 64);
    return b[i];
}
static rat_t R(const char *s)
{
    return rat_from_string(s);
}

int main(void)
{
    printf("=== Smaug's Abacus — multilateral mutual-credit clearing ===\n");

    /* ---- the canonical ring: A->B->C->A, all equal. Nets to zero. ---- */
    {
        abacus_t a;
        smaug_init(&a);
        int A = smaug_add_member(&a, "AlphaShop");
        int B = smaug_add_member(&a, "BetaFarm");
        int C = smaug_add_member(&a, "GammaMill");
        CHECK(A == 0 && B == 1 && C == 2, "three members registered");

        smaug_owe(&a, A, B, R("100.00"), 0);
        smaug_owe(&a, B, C, R("100.00"), 0);
        smaug_owe(&a, C, A, R("100.00"), 0);

        rat_t gross0 = smaug_gross(&a);
        printf("       before: gross=%s across %u transfers\n", S(gross0),
               smaug_transfer_count(&a));
        CHECK(rat_eq(gross0, R("300.00")), "gross obligations = 300.00");
        CHECK(rat_is_zero(smaug_conservation(&a)), "conservation: nets sum to zero");
        CHECK(rat_is_zero(smaug_net(&a, A)) && rat_is_zero(smaug_net(&a, B)),
              "a balanced ring leaves every member at net zero");

        rat_t eliminated = smaug_clear(&a);
        printf("       after : gross=%s across %u transfers (eliminated %s)\n", S(smaug_gross(&a)),
               smaug_transfer_count(&a), S(eliminated));
        CHECK(rat_eq(eliminated, R("300.00")), "ENTIRE 300.00 ring cleared with NO cash movement");
        CHECK(smaug_transfer_count(&a) == 0, "zero transfers remain");
        CHECK(rat_is_zero(smaug_conservation(&a)), "conservation holds after clearing");
    }

    /* ---- NET-POSITION PRESERVATION on an unbalanced graph ---- */
    {
        abacus_t a;
        smaug_init(&a);
        for (int i = 0; i < 5; i++) {
            char n[8] = {'M', (char) ('0' + i), 0};
            smaug_add_member(&a, n);
        }
        smaug_owe(&a, 0, 1, R("50.25"), 0);
        smaug_owe(&a, 1, 2, R("30.00"), 0);
        smaug_owe(&a, 2, 0, R("20.75"), 0);
        smaug_owe(&a, 3, 4, R("15.50"), 0);
        smaug_owe(&a, 4, 0, R("10.00"), 0);
        smaug_owe(&a, 1, 4, R("5.25"), 0);

        rat_t before[5];
        for (int m = 0; m < 5; m++) before[m] = smaug_net(&a, m);
        rat_t gross0 = smaug_gross(&a);
        uint32_t t0 = smaug_transfer_count(&a);
        CHECK(rat_is_zero(smaug_conservation(&a)), "conservation before clearing");

        rat_t elim = smaug_clear(&a);

        int preserved = 1;
        for (int m = 0; m < 5; m++)
            if (!rat_eq(before[m], smaug_net(&a, m))) preserved = 0;
        printf("       gross %s -> %s ; transfers %u -> %u ; eliminated %s\n", S(gross0),
               S(smaug_gross(&a)), t0, smaug_transfer_count(&a), S(elim));
        CHECK(preserved, "EVERY member's net position is IDENTICAL after clearing");
        CHECK(rat_is_zero(smaug_conservation(&a)), "conservation after clearing");
        CHECK(rat_cmp(smaug_gross(&a), gross0) < 0, "gross obligations reduced");
        CHECK(smaug_transfer_count(&a) <= 4, "at most (members-1) transfers remain");
        CHECK(elim.valid && elim.num > 0, "a positive amount was eliminated");
    }

    /* ---- exactness: awkward thirds must not lose a minor unit ---- */
    {
        abacus_t a;
        smaug_init(&a);
        smaug_add_member(&a, "X");
        smaug_add_member(&a, "Y");
        smaug_add_member(&a, "Z");
        smaug_owe(&a, 0, 1, rat_make(100, 3), 0); /* 33.333... exactly 100/3 */
        smaug_owe(&a, 1, 2, rat_make(100, 3), 0);
        smaug_owe(&a, 2, 0, rat_make(100, 3), 0);
        CHECK(rat_is_zero(smaug_conservation(&a)), "conservation with non-terminating amounts");
        rat_t e = smaug_clear(&a);
        CHECK(rat_eq(e, rat_make(100, 1)), "3 x (100/3) cleared exactly = 100");
        CHECK(smaug_transfer_count(&a) == 0, "thirds ring fully cleared, nothing lost");
    }

    /* ---- a bilateral pair that partially offsets ---- */
    {
        abacus_t a;
        smaug_init(&a);
        smaug_add_member(&a, "P");
        smaug_add_member(&a, "Q");
        smaug_owe(&a, 0, 1, R("80.00"), 0);
        smaug_owe(&a, 1, 0, R("30.00"), 0);
        rat_t n0 = smaug_net(&a, 0);
        smaug_clear(&a);
        CHECK(rat_eq(smaug_net(&a, 0), n0), "net preserved on a partial offset");
        CHECK(smaug_transfer_count(&a) == 1, "two obligations net to a single transfer");
        CHECK(rat_eq(smaug_gross(&a), R("50.00")), "residual is exactly 50.00");
    }

    /* ---- input validation ---- */
    {
        abacus_t a;
        smaug_init(&a);
        smaug_add_member(&a, "A");
        smaug_add_member(&a, "B");
        CHECK(!smaug_owe(&a, 0, 0, R("10"), 0), "self-obligation rejected");
        CHECK(!smaug_owe(&a, 0, 9, R("10"), 0), "unknown member rejected");
        CHECK(!smaug_owe(&a, 0, 1, R("-5"), 0), "negative amount rejected");
        CHECK(!smaug_owe(&a, 0, 1, R("junk"), 0), "invalid amount rejected");
        CHECK(smaug_owe(&a, 0, 1, R("0"), 0), "zero amount accepted as a no-op");
        CHECK(smaug_transfer_count(&a) == 0, "zero-amount obligation records nothing");
    }

    /* ---- idempotence: clearing an already-cleared book changes nothing ---- */
    {
        abacus_t a;
        smaug_init(&a);
        smaug_add_member(&a, "A");
        smaug_add_member(&a, "B");
        smaug_add_member(&a, "C");
        smaug_owe(&a, 0, 1, R("40.00"), 0);
        smaug_owe(&a, 1, 2, R("25.00"), 0);
        smaug_clear(&a);
        rat_t g1 = smaug_gross(&a);
        uint32_t t1 = smaug_transfer_count(&a);
        smaug_clear(&a);
        CHECK(rat_eq(smaug_gross(&a), g1) && smaug_transfer_count(&a) == t1,
              "re-clearing is idempotent");
    }

    /* ---- a larger circle: report the real headline number ---- */
    {
        abacus_t a;
        smaug_init(&a);
        enum { N = 20 };
        for (int i = 0; i < N; i++) {
            char n[8] = {'V', (char) ('a' + i), 0};
            smaug_add_member(&a, n);
        }
        /* a dense trading circle */
        uint32_t seed = 7;
        for (int i = 0; i < N; i++) {
            for (int k = 0; k < 3; k++) {
                seed = seed * 1103515245u + 12345u;
                uint32_t j = (seed >> 16) % N;
                if ((int) j == i) continue;
                rat_t amt = rat_make((int64_t) (10 + ((seed >> 8) % 90)), 1);
                smaug_owe(&a, (uint32_t) i, j, amt, 0);
            }
        }
        rat_t g0 = smaug_gross(&a);
        uint32_t t0 = smaug_transfer_count(&a);
        rat_t elim = smaug_clear(&a);
        rat_t g1 = smaug_gross(&a);
        char pb[32], pa[32];
        rat_to_fixed(g0, 2, pb, sizeof(pb), 0);
        rat_to_fixed(g1, 2, pa, sizeof(pa), 0);
        printf("       %d members: gross %s -> %s ; transfers %u -> %u\n", N, pb, pa, t0,
               smaug_transfer_count(&a));
        printf("       eliminated without cash: %s\n", S(elim));
        CHECK(rat_is_zero(smaug_conservation(&a)), "conservation holds at scale");
        CHECK(smaug_transfer_count(&a) <= N - 1, "transfers bounded by members-1");
        CHECK(rat_cmp(g1, g0) < 0, "gross materially reduced at scale");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
