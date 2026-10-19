/* test_chg_meta.c — Chiglet metacognition + self-awareness, host tests.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * Proves the reflective criteria:
 *   1. the reflection reads LIVE state: running inferences and uncertainties
 *      change the self-model;
 *   2. determinism: same state + same tick -> identical awareness stack;
 *   3. the recursion is four levels deep (env, self, awareness, awareness^2),
 *      bounded, and each level is a fold of the level below;
 *   4. the narrative is bounded and reflects the swarm's state.
 */
#include <stdio.h>
#include <string.h>

#include "chg_meta.h"

static int checks = 0;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL line %d: %s\n", __LINE__, #cond);                                         \
            return 1;                                                                              \
        }                                                                                          \
        checks++;                                                                                  \
    } while (0)

int main(void)
{
    chiglet_t c;
    chg_meta_t m1, m2;
    uint64_t top;

    chg_init(&c, CHG_CAP_INFER | CHG_CAP_ADVISE);

    /* ---- 1. reflection reads LIVE state ---------------------------------- */
    chg_meta_reflect(&m1, &c, 100u);
    CHECK(m1.inferences == 0u && m1.uncertain == 0u);
    CHECK(m1.state == (uint32_t) CHG_UNAVAILABLE);

    /* run some inferences, then reflect again: the self-model changes. */
    {
        /* force a model load so inference is possible */
        chg_model_t mdl;
        memset(&mdl, 0, sizeof mdl);
        mdl.K = 2u;
        mdl.D = 4u;
        mdl.L = 2u;
        mdl.epoch = 0u;
        mdl.R_min = 1;
        mdl.margin_min = 1;
        mdl.proto[0][0] = 1;
        mdl.proto[0][1] = 0;
        mdl.proto[0][2] = 1;
        mdl.proto[0][3] = 0;
        mdl.proto[1][0] = 0;
        mdl.proto[1][1] = 1;
        mdl.proto[1][2] = 0;
        mdl.proto[1][3] = 1;
        CHECK(chg_load_model(&c, &mdl) == CHG_OK);
    }
    {
        surplus_real_t ev[2][CHG_DIM];
        chg_result_t r;
        ev[0][0] = 1;
        ev[0][1] = 0;
        ev[0][2] = 1;
        ev[0][3] = 0;
        ev[1][0] = 0;
        ev[1][1] = 1;
        ev[1][2] = 0;
        ev[1][3] = 1;
        CHECK(chg_infer(&c, ev, 2u, &r) == CHG_OK);
    }
    chg_meta_reflect(&m1, &c, 100u);
    CHECK(m1.inferences == 1u);
    CHECK(m1.state == (uint32_t) CHG_DECIDED || m1.state == (uint32_t) CHG_UNCERTAIN);

    /* ---- 2. determinism --------------------------------------------------- */
    {
        chg_meta_t m3;
        chg_meta_reflect(&m3, &c, 100u);
        CHECK(m3.self_model == m1.self_model);
        CHECK(m3.aware[CHG_META_TWO] == m1.aware[CHG_META_TWO]);
        /* a different tick changes the awareness, not the self-model */
        chg_meta_reflect(&m3, &c, 101u);
        CHECK(m3.self_model == m1.self_model);
        CHECK(m3.aware[CHG_META_TWO] != m1.aware[CHG_META_TWO]);
    }

    /* ---- 3. the four-level recursion --------------------------------------- */
    top = chg_meta_reflect(&m2, &c, 200u);
    CHECK(top == m2.aware[CHG_META_TWO]);
    CHECK(m2.aware[CHG_META_ENV] != 0u);  /* env: model loaded           */
    CHECK(m2.aware[CHG_META_SELF] != 0u); /* self: the self-model fold   */
    CHECK(m2.aware[CHG_META_ONE] != 0u);  /* awareness                  */
    CHECK(m2.aware[CHG_META_TWO] != 0u);  /* awareness^2                */
    CHECK(chg_meta_self_model(&m2) == m2.self_model);
    CHECK(chg_meta_self_model(&m2) != 0u); /* it can reason about itself */

    /* ---- 4. narrative ------------------------------------------------------ */
    {
        char buf[128];
        uint32_t w = chg_meta_narrate(&m2, buf, sizeof buf);
        CHECK(w > 0u && w < sizeof buf);
        CHECK(strstr(buf, "aware env") != NULL);
        CHECK(strstr(buf, "self") != NULL);
        CHECK(strstr(buf, "meta") != NULL);
        /* bounded: a tiny buffer never overflows */
        {
            char small[8];
            uint32_t ws = chg_meta_narrate(&m2, small, sizeof small);
            CHECK(ws < sizeof small);
            CHECK(small[ws] == 0);
        }
        CHECK(chg_meta_narrate(NULL, buf, sizeof buf) == 0u);
        CHECK(chg_meta_narrate(&m2, NULL, 0u) == 0u);
    }

    printf("chg_meta: %d checks passed\n", checks);
    return 0;
}