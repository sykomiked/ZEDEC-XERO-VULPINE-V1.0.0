/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mk_concord.h — wires market discovery to kernel/src/concord.
 *
 * mk_concord_isf returns the Interaction Surplus u = 1 - (x . y)^2 of the
 * buyer's and the seller's concord interest vectors (con_surplus) as Q16 in
 * [0, 65536]: complementary interests rank high, identical (echo chamber) and
 * opposite (clash) rank zero. mk_concord_can_see applies concord's mutual,
 * temporary divides, so a divided pair never sees each other's shops.
 *
 * HONEST LIMITS. Ranking is only as good as the interest vectors people
 * publish to concord; a buyer or seller without a concord entry gets the
 * neutral score 1/2. In TEST_HOST builds concord computes in double (see
 * surplus.h) and the conversion here is the only floating-point step; the
 * freestanding build is Q32.32 integer throughout.
 */
#ifndef ZXV_MK_CONCORD_H
#define ZXV_MK_CONCORD_H

#include "market.h"
#include "concord.h"

/* The operator's mk_hooks_t.isf / .can_see forward to these with its
 * con_commons_t (hooks share one ctx, so the forwarding is two lines). */
uint32_t mk_concord_isf(con_commons_t *c, uint32_t buyer_concord, uint32_t seller_concord);
bool mk_concord_can_see(const con_commons_t *c, uint32_t buyer_concord, uint32_t seller_concord);

#endif /* ZXV_MK_CONCORD_H */
