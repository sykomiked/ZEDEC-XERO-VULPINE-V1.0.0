#include <stdio.h>
#include <string.h>
#include "sephirot.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (cond) printf("PASS: %s\n", msg); \
    else { printf("FAIL: %s\n", msg); failures++; } \
} while (0)

static rational_t R(int64_t n, int64_t d) { rational_t r = {n, d}; return r; }
static bool rat_equal_test(rational_t a, rational_t b) { return a.num * b.den == b.num * a.den; }

int main(void) {
    printf("=== 1. Light/Shadow encoding ===\n");
    {
        /* Every sephirah 1..10: to_shadow/to_light must be a true
         * involution, and shadow values must never collide with any
         * valid light value (1..13) or with each other. */
        l13_state_t seen_shadows[11] = {0};
        bool collision = false, roundtrip_ok = true, no_light_overlap = true;
        for (int n = 1; n <= 10; n++) {
            l13_state_t s = l13_to_shadow((l13_phase_t)n);
            if (s == 0) roundtrip_ok = false;
            if (l13_to_light(s) != (l13_phase_t)n) roundtrip_ok = false;
            if (l13_is_light(s)) no_light_overlap = false; /* shadow must not also parse as light */
            for (int j = 1; j < n; j++) if (seen_shadows[j] == s) collision = true;
            seen_shadows[n] = s;
        }
        CHECK(roundtrip_ok, "to_shadow/to_light is a true involution for all 10 Sephirot");
        CHECK(!collision, "no two Qliphothic shadow values collide");
        CHECK(no_light_overlap, "shadow values never parse as valid light phases");

        /* Veils 11-13 must have NO shadow. */
        bool veils_rejected = true;
        for (int n = 11; n <= 13; n++)
            if (l13_to_shadow((l13_phase_t)n) != 0) veils_rejected = false;
        CHECK(veils_rejected, "Veils (Ain, Ain Soph, Ain Soph Aur) correctly have no shadow");

        /* Spot check the actual encoding: Tiferet(6) light=0x06, shadow=0x60. */
        CHECK(l13_light(SEPH_TIFERET) == 0x06, "Tiferet light encodes as 0x06");
        CHECK(l13_to_shadow(SEPH_TIFERET) == 0x60, "Tiferet shadow (Thagirion) encodes as 0x60 (nibble-swapped)");
    }

    printf("\n=== 2. cyc13_t ring operations ===\n");
    {
        cyc13_t z = cyc13_zero();
        CHECK(cyc13_is_zero(z), "cyc13_zero() is the zero vector");

        cyc13_t e1 = cyc13_basis(1), e2 = cyc13_basis(2), e3 = cyc13_basis(3);
        CHECK(!cyc13_equal(e1, e2), "distinct basis vectors are not equal");

        /* zeta^1 * zeta^2 = zeta^3 */
        cyc13_t prod = cyc13_mul(e1, e2);
        CHECK(cyc13_equal(prod, e3), "zeta^1 * zeta^2 == zeta^3 (cyclic convolution)");

        /* zeta^7 * zeta^9 = zeta^(16 mod 13) = zeta^3 */
        cyc13_t wrap = cyc13_mul(cyc13_basis(7), cyc13_basis(9));
        CHECK(cyc13_equal(wrap, e3), "zeta^7 * zeta^9 == zeta^3 (wraps mod 13)");

        /* addition */
        cyc13_t sum = cyc13_add(e1, e2);
        CHECK(rat_equal_test(sum.c[1], R(1,1)) && rat_equal_test(sum.c[2], R(1,1)),
              "cyc13_add places both terms in their own slots");

        /* scale */
        cyc13_t scaled = cyc13_scale(e1, R(3, 2));
        CHECK(rat_equal_test(scaled.c[1], R(3,2)), "cyc13_scale multiplies the coefficient exactly (3/2)");

        /* subtraction gives zero */
        CHECK(cyc13_is_zero(cyc13_sub(e1, e1)), "x - x == 0 exactly");

        /* cyc13_from_phase: phase 13 (Ain Soph Aur) -> zeta^0 (identity) */
        cyc13_t p13 = cyc13_from_phase(VEIL_AIN_SOPH_AUR);
        CHECK(cyc13_equal(p13, cyc13_basis(0)), "phase 13 (Ain Soph Aur) maps to zeta^0, the multiplicative identity");
        cyc13_t p6 = cyc13_from_phase(SEPH_TIFERET);
        CHECK(cyc13_equal(p6, cyc13_basis(6)), "phase 6 (Tiferet) maps to zeta^6");
    }

    printf("\n=== 3. Galois automorphism sigma_a ===\n");
    {
        cyc13_t e5 = cyc13_basis(5);
        cyc13_t id = cyc13_galois_apply(e5, 1);
        CHECK(cyc13_equal(id, e5), "sigma_1 (exponent=1) is the identity automorphism");

        /* Homomorphism property: sigma_a(x*y) == sigma_a(x)*sigma_a(y) */
        cyc13_t x = cyc13_add(cyc13_basis(2), cyc13_scale(cyc13_basis(5), R(3,1)));
        cyc13_t y = cyc13_add(cyc13_basis(4), cyc13_basis(7));
        cyc13_t lhs = cyc13_galois_apply(cyc13_mul(x, y), 6);
        cyc13_t rhs = cyc13_mul(cyc13_galois_apply(x, 6), cyc13_galois_apply(y, 6));
        CHECK(cyc13_equal(lhs, rhs), "sigma_6 respects multiplication (ring homomorphism)");

        cyc13_t lhs2 = cyc13_galois_apply(cyc13_add(x, y), 6);
        cyc13_t rhs2 = cyc13_add(cyc13_galois_apply(x, 6), cyc13_galois_apply(y, 6));
        CHECK(cyc13_equal(lhs2, rhs2), "sigma_6 respects addition (ring homomorphism)");

        /* Composition: sigma_a(sigma_b(x)) == sigma_(a*b mod 13)(x) */
        cyc13_t composed = cyc13_galois_apply(cyc13_galois_apply(x, 3), 5);
        cyc13_t direct = cyc13_galois_apply(x, (3 * 5) % 13);
        CHECK(cyc13_equal(composed, direct), "sigma_3 . sigma_5 == sigma_15mod13 (Galois group composition law)");
    }

    printf("\n=== 4. Subgroup closure (verifying by-hand derivation programmatically) ===\n");
    {
        struct { const char *name; uint32_t h[12]; int n; } groups[] = {
            {"H_DIM1 (order 12)",  {1,2,3,4,5,6,7,8,9,10,11,12}, 12},
            {"H_DIM2 (order 6)",   {1,3,4,9,10,12}, 6},
            {"H_DIM3 (order 4)",   {1,5,8,12}, 4},
            {"H_DIM4 (order 3)",   {1,3,9}, 3},
            {"H_DIM6 (order 2)",   {1,12}, 2},
        };
        for (int g = 0; g < 5; g++) {
            bool closed = true;
            for (int i = 0; i < groups[g].n; i++)
                for (int j = 0; j < groups[g].n; j++) {
                    uint32_t prod = (groups[g].h[i] * groups[g].h[j]) % 13;
                    bool found = false;
                    for (int k = 0; k < groups[g].n; k++) if (groups[g].h[k] == prod) found = true;
                    if (!found) closed = false;
                }
            char msg[128];
            snprintf(msg, sizeof(msg), "%s is closed under multiplication mod 13 (genuine subgroup)", groups[g].name);
            CHECK(closed, msg);
        }
    }

    printf("\n=== 5. Trace/projection invariance (the core correctness property) ===\n");
    {
        /* For each dimension, l13_project(x, d) must be FIXED by every
         * element of the corresponding subgroup -- this is what makes
         * it a genuine trace map into that fixed subfield. */
        cyc13_t x = cyc13_add(cyc13_basis(1), cyc13_add(cyc13_scale(cyc13_basis(4), R(2,1)), cyc13_basis(9)));

        struct { l13_dim_t dim; uint32_t h[12]; int n; const char *name; } cases[] = {
            {L13_DIM_2,  {1,3,4,9,10,12}, 6, "K2 (dim 2)"},
            {L13_DIM_3,  {1,5,8,12}, 4, "K3 (dim 3)"},
            {L13_DIM_4,  {1,3,9}, 3, "K4 (dim 4)"},
            {L13_DIM_6,  {1,12}, 2, "K6 (dim 6)"},
            {L13_DIM_1,  {1,2,3,4,5,6,7,8,9,10,11,12}, 12, "Q (dim 1)"},
        };
        for (int c = 0; c < 5; c++) {
            cyc13_t proj = l13_project(x, cases[c].dim);
            bool invariant = true;
            for (int i = 0; i < cases[c].n; i++) {
                cyc13_t applied = cyc13_galois_apply(proj, cases[c].h[i]);
                if (!cyc13_equal(applied, proj)) invariant = false;
            }
            char msg[160];
            snprintf(msg, sizeof(msg), "projection to %s is invariant under its full fixing subgroup (genuine trace map)", cases[c].name);
            CHECK(invariant, msg);
        }

        /* L13_DIM_12 (trivial subgroup) must be the identity projection. */
        cyc13_t full = l13_project(x, L13_DIM_12);
        CHECK(cyc13_equal(full, x), "projection to full 12D space (trivial subgroup) is the identity");
    }

    printf("\n=== 6. Da'at switch ===\n");
    {
        CHECK(daat_switch(cyc13_zero()) == DAAT_RESET,
              "Da'at resets to substrate zero when the collapsed state is exactly zero");
        CHECK(daat_switch(cyc13_basis(3)) == DAAT_CONTINUE,
              "Da'at continues onward to the Veils for any nonzero collapsed state");
    }

    printf("\n=== 7. Naming ===\n");
    {
        CHECK(strcmp(l13_phase_name(SEPH_TIFERET), "Tiferet") == 0, "Tiferet name correct");
        CHECK(strcmp(l13_phase_name(VEIL_AIN_SOPH_AUR), "Ain Soph Aur") == 0, "Ain Soph Aur name correct");
        CHECK(strcmp(l13_qliphah_name(SEPH_MALKUTH), "Lilith") == 0, "Malkuth's Qliphah (Lilith) name correct");
    }

    if (failures == 0) printf("\n=== ALL SEPHIROT/L13 TESTS PASSED ===\n");
    else printf("\n=== %d FAILURE(S) ===\n", failures);
    return failures;
}
