/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_answer_tag.c — zxv_answer_tag.h T1-T4 on built verdicts.
 *   sh kernel/arch/hosted/test_hosted.sh runs it. */
#include <stdio.h>
#include <string.h>

#include "zxv_answer_tag.h"

static int failures = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) failures++;                                                                      \
        printf((c) ? "  ok   " : "  FAIL ");                                                       \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
    } while (0)

static zxv_zt_verdict_t vd(uint32_t n, uint64_t mean0, uint64_t mean1)
{
    zxv_zt_verdict_t v;
    memset(&v, 0, sizeof v);
    v.n_gen = n;
    v.n_first = n / 2;
    v.n_second = n - n / 2;
    v.margin_first = mean0 * v.n_first;
    v.margin_second = mean1 * v.n_second;
    return v;
}

static lpx_t tag(zxv_zt_verdict_t v, uint8_t *why)
{
    return zxv_answer_tag(&v, why);
}

int main(void)
{
    uint8_t why = 99;
    lpx_t t = tag(vd(10, 8000, 8000), &why);
    CHECK(lpx_base(t) == LPX_B_TRUE && lpx_pol(t) == LPX_P_NEUTRAL &&
              lpx_phase(t) == LPX_PH_MANIFEST && lpx_action(t) == LPX_ACT_PROCEED &&
              why == ZXV_TAG_R_NONE,
          "steady clean reply: true/neutral/manifest, proceed");

    t = tag(vd(10, 8000, 9001), &why);
    CHECK(lpx_pol(t) == LPX_P_NEGENTROPIC && lpx_action(t) == LPX_ACT_PROCEED,
          "margin up by more than an eighth: negentropic, proceed");
    t = tag(vd(10, 8000, 9000), NULL);
    CHECK(lpx_pol(t) == LPX_P_NEUTRAL, "up by exactly an eighth: still neutral");
    t = tag(vd(10, 8000, 6999), NULL);
    CHECK(lpx_pol(t) == LPX_P_ENTROPIC && lpx_action(t) == LPX_ACT_WAIT,
          "margin down by more than an eighth: entropic, wait");
    t = tag(vd(10, 8000, 7000), NULL);
    CHECK(lpx_pol(t) == LPX_P_NEUTRAL, "down by exactly an eighth: still neutral");
    t = tag(vd(10, 0, 1), NULL);
    CHECK(lpx_pol(t) == LPX_P_NEGENTROPIC, "from zero margin any rise is negentropic");
    t = tag(vd(1, 0, 50000), NULL);
    CHECK(lpx_pol(t) == LPX_P_NEUTRAL && lpx_base(t) == LPX_B_TRUE, "one token: no trend, neutral");
    zxv_zt_verdict_t big = vd(4, 0, 0);
    big.margin_first = (uint64_t) 1 << 40;
    big.margin_second = (uint64_t) 1 << 41;
    t = tag(big, NULL);
    CHECK(lpx_pol(t) == LPX_P_NEUTRAL, "means past 32 bits clamp (both rails: neutral)");

    zxv_zt_verdict_t s = vd(10, 8000, 9000);
    s.n_sat = 1;
    t = tag(s, &why);
    CHECK(lpx_forbidden(t) && lpx_base(t) == LPX_B_NEITHER && lpx_phase(t) == LPX_PH_EMERGING &&
              lpx_action(t) == LPX_ACT_ROLLBACK && why == ZXV_TAG_R_SATURATED,
          "one saturated logit: forbidden, rollback, reason saturated");
    zxv_zt_verdict_t f = vd(3, 8000, 8000);
    f.forward_error = true;
    f.n_sat = 2;
    t = tag(f, &why);
    CHECK(lpx_forbidden(t) && why == ZXV_TAG_R_FORWARD,
          "failed forward pass: forbidden, reason forward (wins over saturation)");

    t = tag(vd(0, 0, 0), &why);
    CHECK(lpx_base(t) == LPX_B_NEITHER && lpx_phase(t) == LPX_PH_POTENTIAL &&
              lpx_action(t) == LPX_ACT_WAIT && why == ZXV_TAG_R_NONE,
          "no tokens: neither/potential, wait");
    t = zxv_answer_tag(NULL, &why);
    CHECK(lpx_forbidden(t) && why == ZXV_TAG_R_FORWARD, "no verdict: forbidden (fail closed)");

    printf(failures ? "answer tag test FAILED (%d)\n" : "answer tag test passed\n", failures);
    return failures ? 1 : 0;
}
