/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_fee.c — ABI 1.5: the 0.08889% assurance fee and its split over
 * kernel/src/pay/pay_assure. Every number is the kernel's (rules F1, F2 and
 * B1 in pay_assure.h); this file only validates and reports. */
#include "zxv_internal.h"

#include "pay_assure.h"

/* The ABI constants are the kernel's; a mismatch must not compile. */
_Static_assert(ZXV_FEE_NUM == PAY_ASSURE_NUM && ZXV_FEE_DEN == PAY_ASSURE_DEN, "fee rate");
_Static_assert(ZXV_FEE_BUCKETS == PAY_ASSURE_BUCKETS &&
                   ZXV_FEE_RESERVE_FLOOR == PAY_ASSURE_RESERVE_FLOOR &&
                   ZXV_FEE_VBILL_DIVIDEND == PAY_ASSURE_VBILL_DIVIDEND &&
                   ZXV_FEE_INFRA_BOUNTY == PAY_ASSURE_INFRA_BOUNTY &&
                   ZXV_FEE_REGEN_CAPITAL == PAY_ASSURE_REGEN_CAPITAL,
               "fee buckets");
_Static_assert(ZXV_FEE_UNITS == PAY_UNIT_KIND_COUNT && ZXV_FEE_UNIT_MONEY == PAY_UNIT_MONEY &&
                   ZXV_FEE_UNIT_COMPUTE == PAY_UNIT_COMPUTE &&
                   ZXV_FEE_UNIT_BANDWIDTH == PAY_UNIT_BANDWIDTH &&
                   ZXV_FEE_UNIT_STORAGE == PAY_UNIT_STORAGE,
               "fee units");

uint32_t zxvi_feat2_fee(void)
{
    return ZXV_FEAT2_FEE;
}

int32_t zxv_fee_assure(uint64_t gross, uint64_t *out_fee)
{
    if (!out_fee) return zxvi_fail(ZXV_E_ARG, "fee_assure: out_fee is NULL");
    *out_fee = pay_assure_fee(gross);
    return zxvi_ok();
}

int32_t zxv_fee_assure_carry(uint64_t rem_in, uint64_t gross, uint64_t *out_fee, uint64_t *out_rem)
{
    uint64_t fee, rem;
    if (rem_in >= PAY_ASSURE_DEN)
        return zxvi_fail(ZXV_E_RANGE, "fee_assure_carry: carry %llu is not below %u (corrupt)",
                         (unsigned long long) rem_in, PAY_ASSURE_DEN);
    if (!pay_assure_fee_carry(rem_in, gross, &fee, &rem))
        return zxvi_fail(ZXV_E_INTERNAL, "fee_assure_carry: pay_assure_fee_carry refused");
    if (out_fee) *out_fee = fee;
    if (out_rem) *out_rem = rem;
    return zxvi_ok();
}

int32_t zxv_fee_split(uint64_t fee, uint64_t out[4])
{
    if (!out) return zxvi_fail(ZXV_E_ARG, "fee_split: out is NULL");
    uint64_t part[PAY_ASSURE_BUCKETS];
    pay_assure_split(fee, part);
    /* B1 holds by construction; check it anyway before handing it out. */
    uint64_t sum = 0;
    for (int i = 0; i < PAY_ASSURE_BUCKETS; i++) {
        if (part[i] > fee || sum > fee - part[i])
            return zxvi_fail(ZXV_E_INTERNAL, "fee_split: the buckets do not sum to the fee");
        sum += part[i];
    }
    if (sum != fee) return zxvi_fail(ZXV_E_INTERNAL, "fee_split: the buckets do not sum to the fee");
    for (int i = 0; i < PAY_ASSURE_BUCKETS; i++) out[i] = part[i];
    return zxvi_ok();
}

int32_t zxv_fee_policy_default(uint32_t out_pct[4], int32_t *out_allow_below,
                               uint64_t out_rate_num[4], uint64_t out_rate_den[4],
                               uint64_t *out_mult_num, uint64_t *out_mult_den,
                               uint64_t *out_commons_num, uint64_t *out_commons_den)
{
    pay_assure_policy_t p;
    pay_assure_policy_default(&p);
    for (int i = 0; i < PAY_ASSURE_BUCKETS; i++)
        if (out_pct) out_pct[i] = pay_assure_bucket_pct[i];
    if (out_allow_below) *out_allow_below = p.allow_below_fee ? 1 : 0;
    for (int i = 0; i < PAY_UNIT_KIND_COUNT; i++) {
        if (out_rate_num) out_rate_num[i] = p.vfv_rate[i].num;
        if (out_rate_den) out_rate_den[i] = p.vfv_rate[i].den;
    }
    if (out_mult_num) *out_mult_num = p.credit_mult.num;
    if (out_mult_den) *out_mult_den = p.credit_mult.den;
    if (out_commons_num) *out_commons_num = p.commons_cap_share.num;
    if (out_commons_den) *out_commons_den = p.commons_cap_share.den;
    return zxvi_ok();
}
