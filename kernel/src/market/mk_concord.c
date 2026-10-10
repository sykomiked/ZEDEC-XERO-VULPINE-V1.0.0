/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mk_concord.c — see mk_concord.h. */
#include "mk_concord.h"

static uint32_t to_q16(surplus_real_t s)
{
#ifdef TEST_HOST
    if (!(s > 0.0)) return 0;
    if (s >= 1.0) return MK_Q16;
    return (uint32_t) (s * 65536.0);
#else
    if (s <= 0) return 0;
    if (s >= SR_ONE) return MK_Q16;
    return (uint32_t) ((uint64_t) s >> (SR_SHIFT - 16));
#endif
}

uint32_t mk_concord_isf(con_commons_t *c, uint32_t buyer_concord, uint32_t seller_concord)
{
    if (!c) return MK_Q16 / 2;
    con_person_t *a = con_get(c, buyer_concord);
    con_person_t *b = con_get(c, seller_concord);
    if (!a || !b) return MK_Q16 / 2;
    return to_q16(con_surplus(a, b));
}

bool mk_concord_can_see(const con_commons_t *c, uint32_t buyer_concord, uint32_t seller_concord)
{
    if (!c) return true;
    return con_can_see(c, buyer_concord, seller_concord);
}
