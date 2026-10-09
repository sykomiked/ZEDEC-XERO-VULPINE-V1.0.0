/* test_count_house_fractal.c — Fractal scaling tests for Count House
 *
 * Tests recursive Scale-0 / Alliance / Global tier aggregation,
 * Fibonacci mint curve allowances, tier validation, and recursive
 * audit propagation.
 *
 * Author: Michael Laurence Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "count_house.h"
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

static uint8_t sig_nonzero[CH_PROOF_SIG_LEN];

int main(void) {
    memset(sig_nonzero, 0xAA, CH_PROOF_SIG_LEN);

    /* ===== init_fractal: scale and level assignment ===== */
    {
        count_house_t s0, alliance, global;
        count_house_init_fractal(&s0, 1, "node-0", CH_SCALE_0);
        count_house_init_fractal(&alliance, 2, "alliance-1", CH_SCALE_ALLIANCE);
        count_house_init_fractal(&global, 3, "global-1", CH_SCALE_GLOBAL);

        assert(s0.scale == CH_SCALE_0);
        assert(alliance.scale == CH_SCALE_ALLIANCE);
        assert(global.scale == CH_SCALE_GLOBAL);

        assert(s0.fractal_level == 1);
        assert(alliance.fractal_level == 3);
        assert(global.fractal_level == 5);

        assert(strcmp(ch_scale_name(CH_SCALE_0), "Scale-0") == 0);
        assert(strcmp(ch_scale_name(CH_SCALE_ALLIANCE), "Alliance") == 0);
        assert(strcmp(ch_scale_name(CH_SCALE_GLOBAL), "Global") == 0);

        /* Leaf node: v_fractal == v_local */
        assert(feq(s0.v_fractal, s0.v_local, 1e-9));
    }

    /* ===== Fibonacci mint allowance ===== */
    {
        count_house_t s0, alliance, global;
        count_house_init_fractal(&s0, 1, "s0", CH_SCALE_0);
        count_house_init_fractal(&alliance, 2, "al", CH_SCALE_ALLIANCE);
        count_house_init_fractal(&global, 3, "gl", CH_SCALE_GLOBAL);

        uint64_t base = 1000;

        /* Scale-0: level 1 -> F(2) = 1 -> allowance = 1000 */
        assert(count_house_fib_mint_allowance(&s0, base) == 1000);

        /* Alliance: level 3 -> F(4) = 3 -> allowance = 3000 */
        assert(count_house_fib_mint_allowance(&alliance, base) == 3000);

        /* Global: level 5 -> F(6) = 8 -> allowance = 8000 */
        assert(count_house_fib_mint_allowance(&global, base) == 8000);

        /* base_unit = 0 -> 0 */
        assert(count_house_fib_mint_allowance(&s0, 0) == 0);
    }

    /* ===== tier validation: add_child rejects wrong tiers ===== */
    {
        count_house_t s0, alliance, global;
        count_house_init_fractal(&s0, 1, "s0", CH_SCALE_0);
        count_house_init_fractal(&alliance, 2, "al", CH_SCALE_ALLIANCE);
        count_house_init_fractal(&global, 3, "gl", CH_SCALE_GLOBAL);

        /* Scale-0 can't have children */
        assert(count_house_add_child(&s0, &alliance) == -2);

        /* Alliance can only take Scale-0 children */
        assert(count_house_add_child(&alliance, &global) == -2);
        assert(count_house_add_child(&alliance, &s0) == 0);

        /* Global can only take Alliance children */
        assert(count_house_add_child(&global, &s0) == -2);
        assert(count_house_add_child(&global, &alliance) == 0);
    }

    /* ===== capacity limit ===== */
    {
        count_house_t alliance;
        count_house_init_fractal(&alliance, 10, "al-cap", CH_SCALE_ALLIANCE);

        count_house_t children[CH_MAX_CHILDREN + 1];
        for (uint32_t i = 0; i < CH_MAX_CHILDREN; i++) {
            count_house_init_fractal(&children[i], i + 100, "c", CH_SCALE_0);
            assert(count_house_add_child(&alliance, &children[i]) == 0);
        }
        count_house_init_fractal(&children[CH_MAX_CHILDREN], 999, "overflow", CH_SCALE_0);
        assert(count_house_add_child(&alliance, &children[CH_MAX_CHILDREN]) == -1);
        assert(alliance.num_children == CH_MAX_CHILDREN);
    }

    /* ===== recursive valuation: parent aggregates children ===== */
    {
        /* Create 3 Scale-0 nodes with known reserves */
        count_house_t c0, c1, c2;
        count_house_init_fractal(&c0, 1, "c0", CH_SCALE_0);
        count_house_init_fractal(&c1, 2, "c1", CH_SCALE_0);
        count_house_init_fractal(&c2, 3, "c2", CH_SCALE_0);

        /* Give each some crypto reserves so v_local is nonzero */
        count_house_set_crypto_reserves(&c0, SR_FROM_INT(100));
        count_house_set_crypto_reserves(&c1, SR_FROM_INT(200));
        count_house_set_crypto_reserves(&c2, SR_FROM_INT(300));

        /* With zero supply, v_local == total_reserves */
        assert(feq(c0.v_local, SR_FROM_INT(100), 1e-9));
        assert(feq(c1.v_local, SR_FROM_INT(200), 1e-9));
        assert(feq(c2.v_local, SR_FROM_INT(300), 1e-9));

        /* Create Alliance parent */
        count_house_t alliance;
        count_house_init_fractal(&alliance, 4, "al", CH_SCALE_ALLIANCE);
        count_house_set_crypto_reserves(&alliance, SR_FROM_INT(50));

        /* v_fractal for alliance with no children == v_local == 50 */
        assert(feq(alliance.v_fractal, SR_FROM_INT(50), 1e-9));

        /* Add children */
        count_house_add_child(&alliance, &c0);
        count_house_add_child(&alliance, &c1);
        count_house_add_child(&alliance, &c2);

        /* v_fractal = (50 + 100 + 200 + 300) / 4 = 162.5 */
        surplus_real_t expected = SR_DIV(SR_FROM_INT(650), SR_FROM_INT(4));
        assert(feq(alliance.v_fractal, expected, 1e-9));
    }

    /* ===== recursive audit: hyperinflation in child fails parent ===== */
    {
        count_house_t good, bad, alliance;
        count_house_init_fractal(&good, 1, "good", CH_SCALE_0);
        count_house_init_fractal(&bad, 2, "bad", CH_SCALE_0);
        count_house_init_fractal(&alliance, 3, "al", CH_SCALE_ALLIANCE);

        /* Good node: has reserves, small supply -> healthy */
        count_house_set_crypto_reserves(&good, SR_FROM_INT(1000));
        count_house_mint(&good, 100);
        assert(count_house_audit(&good));

        /* Bad node: mint validly first, then slash reserves to trigger
         * hyperinflation post-hoc (count_house_mint prevents minting
         * INTO hyperinflation by design, but reserves can shrink after). */
        count_house_set_crypto_reserves(&bad, SR_FROM_INT(1000));
        count_house_mint(&bad, 100000);
        assert(count_house_audit(&bad)); /* still healthy after mint */
        count_house_set_crypto_reserves(&bad, SR_FROM_INT(1));
        assert(!count_house_audit(&bad)); /* now hyperinflated */

        /* Alliance with only good child passes */
        count_house_add_child(&alliance, &good);
        assert(count_house_fractal_audit(&alliance));

        /* Add bad child -> alliance fails recursive audit */
        count_house_add_child(&alliance, &bad);
        assert(!count_house_fractal_audit(&alliance));
    }

    /* ===== three-tier recursion: Global -> Alliance -> Scale-0 ===== */
    {
        count_house_t leaf1, leaf2, alliance, global;
        count_house_init_fractal(&leaf1, 1, "l1", CH_SCALE_0);
        count_house_init_fractal(&leaf2, 2, "l2", CH_SCALE_0);
        count_house_init_fractal(&alliance, 3, "al", CH_SCALE_ALLIANCE);
        count_house_init_fractal(&global, 4, "gl", CH_SCALE_GLOBAL);

        count_house_set_crypto_reserves(&leaf1, SR_FROM_INT(400));
        count_house_set_crypto_reserves(&leaf2, SR_FROM_INT(600));
        count_house_set_crypto_reserves(&alliance, SR_FROM_INT(100));
        count_house_set_crypto_reserves(&global, SR_FROM_INT(50));

        count_house_add_child(&alliance, &leaf1);
        count_house_add_child(&alliance, &leaf2);

        /* Alliance v_fractal = (100 + 400 + 600) / 3 = 366.67 */
        surplus_real_t al_expected = SR_DIV(SR_FROM_INT(1100), SR_FROM_INT(3));
        assert(feq(alliance.v_fractal, al_expected, 1e-9));

        count_house_add_child(&global, &alliance);

        /* Global v_fractal = (50 + 366.67) / 2 = 208.33 */
        surplus_real_t gl_expected = SR_DIV(SR_ADD(SR_FROM_INT(50), al_expected), SR_FROM_INT(2));
        assert(feq(global.v_fractal, gl_expected, 1e-9));

        /* Full recursive audit passes */
        assert(count_house_fractal_audit(&global));
    }

    printf("All Count House fractal scaling tests passed\n");
    return 0;
}
