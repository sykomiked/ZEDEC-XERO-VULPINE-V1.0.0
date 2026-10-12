/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_abi15.c — ABI 1.5: the second feature mask. Each group reports
 * its own bit, so a group stays dark until its file says it is live. */
#include "zxv_internal.h"

int32_t zxv_features2(uint32_t *out_mask)
{
    if (!out_mask) return zxvi_fail(ZXV_E_ARG, "out_mask is NULL");
    *out_mask = zxvi_feat2_treaty() | zxvi_feat2_aipi() | zxvi_feat2_coin() | zxvi_feat2_fee() | zxvi_feat2_vhub() | zxvi_feat2_freight() | zxvi_feat2_web4() | zxvi_feat2_paradox();
    return zxvi_ok();
}
