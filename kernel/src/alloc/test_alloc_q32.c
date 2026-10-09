/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* test_alloc_q32.c — the proportional split, on the REAL TARGET numeric type.
 *
 * This test is compiled WITHOUT -DTEST_HOST, so surplus_real_t is the Q32.32
 * int64 fixed-point used on the actual target — not host double. The earlier
 * SR_DIV(SR_MUL(pool, units), total) order silently OVERFLOWED int64 here and
 * produced NEGATIVE shares for contributions of a few tens of thousands of
 * units; the host (double) suite could never see it. This pins the fraction-
 * first order to the target math with magnitudes above 2^15.
 */
#include <stdio.h>
#include "alloc.h"

static int fails = 0;
#define CK(c,m) do { if(!(c)){printf("[FAIL] %s\n",m);fails++;} else printf("[PASS] %s\n",m); } while(0)

int main(void) {
    printf("=== alloc: proportional split on the Q32.32 TARGET type ===\n");

    /* 50000 + 50000 units -> each gets back 50000 exactly. The overflow bug gave
     * 7050 / 92949 here; a negative share is the unmistakable overflow signature. */
    alloc_pool_t p; alloc_pool_init(&p);
    alloc_contribute(&p, 1, ZCAP_INTELLECTUAL, SR_FROM_INT(50000));
    alloc_contribute(&p, 2, ZCAP_INTELLECTUAL, SR_FROM_INT(50000));
    alloc_result_t r;
    alloc_distribute(&p, ZCAP_INTELLECTUAL, &r);
    CK(r.count == 2, "two contributors distributed");
    CK(SR_CMP(r.shares[0].amount, SR_ZERO) > 0 &&
       SR_CMP(r.shares[1].amount, SR_ZERO) > 0,
       "both shares are POSITIVE (no Q32.32 overflow)");
    CK(SR_CMP(r.shares[0].amount, SR_FROM_INT(50000)) == 0, "share0 == 50000 exactly");
    CK(SR_CMP(r.shares[1].amount, SR_FROM_INT(50000)) == 0, "share1 == 50000 exactly");
    CK(SR_CMP(r.total, SR_FROM_INT(100000)) == 0, "conserved: total == pool (100000)");

    /* 32768 + 32768: exactly the boundary the overflow bug flipped negative. */
    alloc_pool_t p2; alloc_pool_init(&p2);
    alloc_contribute(&p2, 1, ZCAP_HUMAN, SR_FROM_INT(32768));
    alloc_contribute(&p2, 2, ZCAP_HUMAN, SR_FROM_INT(32768));
    alloc_result_t r2;
    alloc_distribute(&p2, ZCAP_HUMAN, &r2);
    CK(SR_CMP(r2.shares[0].amount, SR_ZERO) > 0, "32768-unit share is positive (bug gave -32768)");
    CK(SR_CMP(r2.shares[0].amount, SR_FROM_INT(32768)) == 0, "32768-unit share == 32768 exactly");

    /* Uneven split 30000 / 70000 of a 100000 pool. A ratio like 0.3 is NOT
     * exactly representable in Q32.32, so individual shares are correct only to a
     * fixed-point tick — but they are POSITIVE (no overflow) and conservation is
     * still EXACT (the last share absorbs the sub-tick remainder). We assert the
     * honest invariant: each share within one whole unit of ideal, total exact. */
    alloc_pool_t p3; alloc_pool_init(&p3);
    alloc_contribute(&p3, 1, ZCAP_MANUFACTURED, SR_FROM_INT(30000));
    alloc_contribute(&p3, 2, ZCAP_MANUFACTURED, SR_FROM_INT(70000));
    alloc_result_t r3;
    alloc_distribute(&p3, ZCAP_MANUFACTURED, &r3);
    surplus_real_t d0 = SR_SUB(r3.shares[0].amount, SR_FROM_INT(30000));
    if (SR_CMP(d0, SR_ZERO) < 0) d0 = SR_SUB(SR_ZERO, d0);
    surplus_real_t d1 = SR_SUB(r3.shares[1].amount, SR_FROM_INT(70000));
    if (SR_CMP(d1, SR_ZERO) < 0) d1 = SR_SUB(SR_ZERO, d1);
    CK(SR_CMP(r3.shares[0].amount, SR_ZERO) > 0 && SR_CMP(d0, SR_FROM_INT(1)) < 0,
       "uneven: 30000 share positive, within 1 unit of ideal");
    CK(SR_CMP(r3.shares[1].amount, SR_ZERO) > 0 && SR_CMP(d1, SR_FROM_INT(1)) < 0,
       "uneven: 70000 share positive, within 1 unit of ideal");
    CK(SR_CMP(r3.total, SR_FROM_INT(100000)) == 0, "uneven: conserved to the pool EXACTLY");

    printf("\n%s: %d failure(s)\n", fails ? "*** FAILED ***" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
