/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_space.c — spaces and the hall. See zui_space.h. */
#include "zui_space.h"

const char *zsp_kind_name(zsp_kind_t k)
{
    switch (k) {
    case ZSP_HOME:
        return "Home";
    case ZSP_OFFICE:
        return "Office";
    case ZSP_TEAM:
        return "Team";
    case ZSP_CLOUDTOP:
        return "Cloud top";
    case ZSP_PARK:
        return "Park";
    case ZSP_HALL:
        return "Hall";
    }
    return "Hall";
}

int zsp_openness(zsp_kind_t k)
{
    return (k >= ZSP_HOME && k <= ZSP_PARK) ? (int) k : -1;
}

/* rights[kind][role]: bit 0 view, bit 1 write */
static const uint8_t RIGHTS[5][4] = {
    /*            owner member guest public */
    /* HOME     */ {3, 0, 0, 0},
    /* OFFICE   */ {3, 3, 1, 0},
    /* TEAM     */ {3, 3, 1, 0},
    /* CLOUDTOP */ {3, 1, 1, 1},
    /* PARK     */ {3, 3, 3, 3},
};

static uint8_t rights(zsp_kind_t k, zsp_role_t r)
{
    if (zsp_openness(k) < 0 || r < ZSP_OWNER || r > ZSP_PUBLIC) return 0;
    return RIGHTS[k][r];
}

bool zsp_can_view(zsp_kind_t k, zsp_role_t r)
{
    return (rights(k, r) & 1u) != 0;
}
bool zsp_can_write(zsp_kind_t k, zsp_role_t r)
{
    return (rights(k, r) & 2u) != 0;
}

lpx_t zsp_cross(zsp_place_t from, zsp_place_t to)
{
    if (zsp_openness(from.kind) < 0 || zsp_openness(to.kind) < 0 ||
        !zsp_can_view(from.kind, from.role) || !zsp_can_write(to.kind, to.role))
        return lpx_make(LPX_B_FALSE, LPX_P_NEUTRAL, LPX_PH_MANIFEST);
    if (zsp_openness(to.kind) > zsp_openness(from.kind))
        return lpx_make(LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
    return lpx_make(LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST);
}

lpx_t zsp_confirm(lpx_t held)
{
    if (lpx_forbidden(held) || lpx_base(held) != LPX_B_NEITHER) return held;
    return lpx_make(LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST);
}

lpx_t zsp_decide(const zsp_rules_t *rules, zsp_role_t proposer, uint32_t members, uint32_t yes,
                 uint32_t no)
{
    const lpx_t refused = lpx_make(LPX_B_FALSE, LPX_P_NEUTRAL, LPX_PH_MANIFEST);
    const lpx_t passed = lpx_make(LPX_B_TRUE, LPX_P_NEGENTROPIC, LPX_PH_MANIFEST);
    if (!rules || (proposer != ZSP_OWNER && proposer != ZSP_MEMBER)) return refused;
    if (rules->gov == ZSP_GOV_VERTICAL)
        return proposer == ZSP_OWNER ? passed
                                     : lpx_make(LPX_B_NEITHER, LPX_P_NEUTRAL, LPX_PH_EMERGING);
    if (members == 0) return refused;
    if (yes > members) yes = members;
    if (no > members - yes) no = members - yes;
    uint64_t q = rules->quorum_pct ? (rules->quorum_pct > 100 ? 100 : rules->quorum_pct) : 51;
    bool reached = (uint64_t) yes * 100u >= q * members;
    if (reached && no == 0) return passed;
    if (reached) return lpx_make(LPX_B_BOTH, LPX_P_NEUTRAL, LPX_PH_MANIFEST);
    if ((uint64_t) (members - no) * 100u < q * members) return refused; /* cannot pass */
    return lpx_make(LPX_B_NEITHER, LPX_P_NEGENTROPIC, LPX_PH_EMERGING); /* gathering */
}
