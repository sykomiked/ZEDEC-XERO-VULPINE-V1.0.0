/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* render_lineage.c — the open graphics-engine evolution. See render_lineage.h.
 * Proprietary lineage names; real engines are `ref:` development references. */
#include "render_lineage.h"
#include "chiglet.h"

int rl_integrability(rl_license_t lic){
    switch (lic){
        case LIC_PUBLIC_DOMAIN: case LIC_MIT: case LIC_ZLIB: return 2;  /* permissive */
        case LIC_GPL2: case LIC_GPL3:                        return 1;  /* copyleft   */
        default:                                             return 0;  /* proprietary*/
    }
}

/* dims: world_geometry software_raster lighting hardware_accel shaders shadows
 *       material_pbr era. Era-ordered so adjacent entries are consecutive steps. */
static const render_engine_t g_engines[] = {
 /* ref: id Tech 1 / Doom (1993, GPL-2.0) — BSP + span rendering, software-3D birth */
 { "Cinder",   "id Tech 1 / Doom",     LIC_GPL2,
   { SR_FROM_FLOAT(0.30),SR_FROM_FLOAT(0.75),SR_FROM_FLOAT(0.20),SR_FROM_FLOAT(0.00),
     SR_FROM_FLOAT(0.00),SR_FROM_FLOAT(0.00),SR_FROM_FLOAT(0.00),SR_FROM_FLOAT(0.10) } },
 /* ref: id Tech 2 / Quake (1996, GPL-2.0) — true polygonal 3D + lightmaps */
 { "Quill",    "id Tech 2 / Quake",    LIC_GPL2,
   { SR_FROM_FLOAT(0.60),SR_FROM_FLOAT(0.80),SR_FROM_FLOAT(0.50),SR_FROM_FLOAT(0.30),
     SR_FROM_FLOAT(0.00),SR_FROM_FLOAT(0.10),SR_FROM_FLOAT(0.00),SR_FROM_FLOAT(0.25) } },
 /* ref: Quake II (1997, GPL-2.0) — colored lighting + hardware GL */
 { "Verdance", "Quake II",             LIC_GPL2,
   { SR_FROM_FLOAT(0.65),SR_FROM_FLOAT(0.60),SR_FROM_FLOAT(0.65),SR_FROM_FLOAT(0.70),
     SR_FROM_FLOAT(0.10),SR_FROM_FLOAT(0.20),SR_FROM_FLOAT(0.00),SR_FROM_FLOAT(0.33) } },
 /* ref: id Tech 3 / Quake III (1999, GPL-2.0) — curved surfaces + shader scripts */
 { "Arclight", "id Tech 3 / Quake III",LIC_GPL2,
   { SR_FROM_FLOAT(0.85),SR_FROM_FLOAT(0.30),SR_FROM_FLOAT(0.70),SR_FROM_FLOAT(0.90),
     SR_FROM_FLOAT(0.60),SR_FROM_FLOAT(0.30),SR_FROM_FLOAT(0.10),SR_FROM_FLOAT(0.48) } },
 /* ref: id Tech 4 / Doom 3 (2004, GPL-3.0) — unified per-pixel lighting + stencil shadows */
 { "Umbra",    "id Tech 4 / Doom 3",   LIC_GPL3,
   { SR_FROM_FLOAT(0.80),SR_FROM_FLOAT(0.10),SR_FROM_FLOAT(0.95),SR_FROM_FLOAT(0.95),
     SR_FROM_FLOAT(0.85),SR_FROM_FLOAT(0.95),SR_FROM_FLOAT(0.40),SR_FROM_FLOAT(0.62) } },
 /* ref: Godot (2014, MIT) — modern PBR, fully open */
 { "Aurora",   "Godot",                LIC_MIT,
   { SR_FROM_FLOAT(0.85),SR_FROM_FLOAT(0.10),SR_FROM_FLOAT(0.90),SR_FROM_FLOAT(0.98),
     SR_FROM_FLOAT(0.95),SR_FROM_FLOAT(0.90),SR_FROM_FLOAT(0.95),SR_FROM_FLOAT(1.00) } },
 /* ref: Unreal Engine — source-available but PROPRIETARY EULA: reference only,
  * never integrated into the open stack (flagged, not folded in). */
 { "Mirage",   "Unreal Engine",        LIC_PROPRIETARY,
   { SR_FROM_FLOAT(0.90),SR_FROM_FLOAT(0.05),SR_FROM_FLOAT(0.95),SR_FROM_FLOAT(0.98),
     SR_FROM_FLOAT(0.98),SR_FROM_FLOAT(0.95),SR_FROM_FLOAT(0.95),SR_FROM_FLOAT(0.90) } },
};
#define N_ENGINES ((int)(sizeof g_engines / sizeof g_engines[0]))

const render_engine_t *render_lineage_table(int *count_out){
    if (count_out) *count_out = N_ENGINES;
    return g_engines;
}

void render_lineage_assess(const render_engine_t *e, uint32_t n, rl_result_t *out){
    for (unsigned i = 0; i < sizeof *out; i++) ((uint8_t*)out)[i] = 0;
    out->total = n;
    if (n == 0) return;

    /* Split open (integrable) from proprietary. Only the OPEN engines form the
     * lineage we can actually build on. */
    static surplus_real_t open_sig[CHG_MAX_EXPERTS][RL_DIM];
    uint32_t open_n = 0; int all_prop_refused = 1;
    for (uint32_t i = 0; i < n; i++){
        int integ = rl_integrability(e[i].license);
        if (integ == 0) continue;                        /* proprietary -> excluded */
        if (open_n < CHG_MAX_EXPERTS){
            for (int d = 0; d < RL_DIM; d++) open_sig[open_n][d] = e[i].sig[d];
            open_n++;
        }
    }
    out->open_count = open_n;
    /* every proprietary engine must be refused integration (integrability 0) */
    for (uint32_t i = 0; i < n; i++)
        if (e[i].license == LIC_PROPRIETARY && rl_integrability(e[i].license) != 0) all_prop_refused = 0;
    out->proprietary_flagged = all_prop_refused;

    /* diversity: distinct rendering archetypes among the open engines */
    uint32_t distinct = 0;
    (void)chg_effective_experts((const surplus_real_t (*)[CHG_DIM])open_sig, open_n, RL_DIM, &distinct);
    out->distinct_archetypes = distinct;

    /* continuity: mean relationship between adjacent OPEN engines (era-ordered).
     * chg_interaction ~0 = near-identical, ~1 = orthogonal jump; a lineage is
     * related-but-moving, so 0 < mean < 0.5. */
    surplus_real_t sum = SR_ZERO; uint32_t pairs = 0;
    for (uint32_t i = 0; i + 1 < open_n; i++){
        sum = SR_ADD(sum, chg_interaction(open_sig[i], open_sig[i+1], RL_DIM));
        pairs++;
    }
    out->continuity = pairs ? SR_DIV(sum, SR_FROM_INT((int64_t)pairs)) : SR_ZERO;

    int diverse    = (distinct >= 3);
    int continuous = (out->continuity > SR_ZERO) && (out->continuity < SR_FROM_FLOAT(0.5));
    out->coherent  = (diverse && continuous && all_prop_refused && open_n >= 3) ? 1 : 0;
}

int render_lineage_selfcheck(uint32_t *distinct_out, uint32_t *continuity_permille_out){
    rl_result_t r;
    render_lineage_assess(g_engines, (uint32_t)N_ENGINES, &r);
    if (distinct_out) *distinct_out = r.distinct_archetypes;
    if (continuity_permille_out)
        *continuity_permille_out = (uint32_t)((double)r.continuity / (double)SR_ONE * 1000.0);
    return r.coherent;
}
