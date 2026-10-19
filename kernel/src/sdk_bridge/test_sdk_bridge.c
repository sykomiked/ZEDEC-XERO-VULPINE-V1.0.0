/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_sdk_bridge.c — capability grants: one value per capability, the
 * descriptor of that capability (not of its array slot) decides, and an
 * attestation that is not TRUE gets nothing. */
#include <stdio.h>
#include <string.h>
#include "sdk_bridge.h"

static int pass, fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("[FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__);                                \
        }                                                                                          \
    } while (0)

static sdk_bridge_t B;
static sb_context_t C;

static void fresh_ctx(lpres_state_t att, bool sandboxed)
{
    memset(&C, 0, sizeof C);
    C.coverage_ratio = SR_FROM_FLOAT(10.0);
    C.process_attestation = att;
    C.sandboxed = sandboxed;
}

static const sb_capability_desc_t *desc(sb_capability_t c)
{
    for (uint32_t i = 0; i < B.num_capabilities; i++)
        if (B.capabilities[i].id == c) return &B.capabilities[i];
    return NULL;
}

int main(void)
{
    memset(&B, 0, sizeof B);
    sb_init(&B, NULL, NULL, NULL);
    CHECK(B.num_capabilities == (uint32_t) SB_CAP_MAX - 1, "every capability is registered once");
    int uniq = 1, named = 1;
    for (uint32_t i = 0; i < B.num_capabilities; i++) {
        for (uint32_t j = i + 1; j < B.num_capabilities; j++)
            uniq &= B.capabilities[i].id != B.capabilities[j].id;
        named &= !strcmp(sb_capability_name(B.capabilities[i].id), B.capabilities[i].name);
    }
    CHECK(uniq, "no two capabilities share a value");
    CHECK(named, "each value's name is its descriptor's name");
    CHECK(SB_CAP_TELECOM_GLARE_DETECT != SB_CAP_AI_INFERENCE &&
              SB_CAP_AI_TRAINING != SB_CAP_QUANTUM_OPERATION,
          "the once-aliased pairs are distinct");

    /* granting AI inference grants nothing else */
    fresh_ctx(LPRES_STATE_TRUE, false);
    sb_result_t r = sb_grant_capability(&B, &C, SB_CAP_AI_INFERENCE);
    CHECK(r.code == 0 && sb_has_capability(&C, SB_CAP_AI_INFERENCE), "AI inference granted");
    CHECK(!sb_has_capability(&C, SB_CAP_TELECOM_GLARE_DETECT) &&
              !sb_has_capability(&C, SB_CAP_AI_TRAINING),
          "and nothing else with it");
    r = sb_grant_capability(&B, &C, SB_CAP_AI_INFERENCE);
    CHECK(r.code == 0 && C.num_capabilities == 1, "granting twice keeps one entry");

    /* the capability's own descriptor decides: AI training needs the HSM
     * sandbox, quantum too; their neighbours in the array do not */
    CHECK(desc(SB_CAP_AI_TRAINING)->requires_hsm && !desc(SB_CAP_AI_INFERENCE)->requires_hsm,
          "fixture: training needs the sandbox, inference does not");
    r = sb_grant_capability(&B, &C, SB_CAP_AI_TRAINING);
    CHECK(r.code != 0 && !sb_has_capability(&C, SB_CAP_AI_TRAINING),
          "AI training refused outside the sandbox");
    r = sb_grant_capability(&B, &C, SB_CAP_QUANTUM_OPERATION);
    CHECK(r.code != 0, "quantum refused outside the sandbox");
    fresh_ctx(LPRES_STATE_TRUE, true);
    CHECK(sb_grant_capability(&B, &C, SB_CAP_AI_TRAINING).code == 0, "and granted inside it");

    /* every registered capability: refused below its own coverage, granted
     * at it (outside the sandbox only if it does not need one) */
    int cov = 1;
    for (uint32_t i = 0; i < B.num_capabilities; i++) {
        const sb_capability_desc_t *d = &B.capabilities[i];
        fresh_ctx(LPRES_STATE_TRUE, d->requires_hsm);
        C.coverage_ratio = d->min_coverage - SR_FROM_FLOAT(0.01);
        cov &= sb_grant_capability(&B, &C, d->id).code != 0;
        C.coverage_ratio = d->min_coverage;
        cov &= sb_grant_capability(&B, &C, d->id).code == 0 && sb_has_capability(&C, d->id);
    }
    CHECK(cov, "each capability is checked against its own minimum coverage");

    /* attestation: only TRUE passes a TRUE requirement */
    const lpres_state_t bad[3] = {LPRES_STATE_FALSE, LPRES_STATE_BOTH, LPRES_STATE_NEITHER};
    int att = 1;
    for (int k = 0; k < 3; k++) {
        fresh_ctx(bad[k], false);
        att &=
            sb_grant_capability(&B, &C, SB_CAP_AI_INFERENCE).code != 0 && C.num_capabilities == 0;
    }
    CHECK(att, "FALSE, BOTH and NEITHER attestations are refused");

    fresh_ctx(LPRES_STATE_TRUE, true);
    CHECK(sb_grant_capability(&B, &C, (sb_capability_t) 0).code != 0 &&
              sb_grant_capability(&B, &C, SB_CAP_MAX).code != 0 &&
              sb_grant_capability(&B, &C, (sb_capability_t) 99).code != 0 &&
              C.num_capabilities == 0,
          "unknown capabilities are refused");
    printf("%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
