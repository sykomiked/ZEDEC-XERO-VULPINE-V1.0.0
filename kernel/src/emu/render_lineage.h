/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* render_lineage.h — the graphics-engine evolution as a first-class system.
 *
 * DESIGN PIVOT: past the 5th generation (PlayStation/N64 and beyond), the STORY
 * corpus is saturated (more ROMs = more examples, not new narrative), but the
 * GRAPHICS underwent a revolution — and that era's graphics run on ENGINES. So
 * instead of emulating 3D console hardware, this system INTEGRATES open-source
 * engines that embody the graphics evolution and builds upon them. The engines
 * become bootable GRAPHICS MegaROMs on the existing render stack (holo +
 * zxv_shell + ramfb + megarom).
 *
 * The genuinely-open lineage that IS the 3D graphics evolution (id Tech / the
 * Doom-Quake family is GPL; Godot is MIT). NOTE: Unreal Engine is source-
 * available under a PROPRIETARY, royalty-bearing EULA — it is NOT open source or
 * public domain and cannot be folded into this open ShareAlike stack; it appears
 * here only flagged, as a reference. All real engine names are `ref:` DEVELOPMENT
 * references — the shipped lineage entries carry their own proprietary names.
 *
 * Each engine is an 8-dim rendering-capability signature. The framework proves
 * the evolution is DIVERSE and CONTINUOUS (the same ISF logic as the console
 * trajectory), and gates INTEGRABILITY by license — permissive (MIT/zlib/public
 * domain) folds straight in, copyleft (GPL) integrates but obliges the derived
 * layer, proprietary is refused. */
#ifndef ZXV_RENDER_LINEAGE_H
#define ZXV_RENDER_LINEAGE_H

#include <stdint.h>
#include "surplus.h"

#define RL_DIM 8   /* == CHG_DIM. dims:
                    * 0 world_geometry  1 software_raster  2 lighting
                    * 3 hardware_accel   4 shaders          5 shadows
                    * 6 material_pbr     7 era                                    */

typedef enum {
    LIC_PUBLIC_DOMAIN = 0,
    LIC_MIT           = 1,
    LIC_ZLIB          = 2,
    LIC_GPL2          = 3,
    LIC_GPL3          = 4,
    LIC_PROPRIETARY   = 5,
} rl_license_t;

/* Integrability into the open ShareAlike stack:
 *   2 = permissive (public domain / MIT / zlib) — folds straight in
 *   1 = copyleft   (GPL) — integrable, but the derived graphics layer is GPL
 *   0 = refused    (proprietary) — reference only, never integrated            */
int rl_integrability(rl_license_t lic);

typedef struct render_engine {
    const char   *name;        /* our proprietary lineage name                  */
    const char   *ref;         /* the real open engine it is modeled on (dev ref)*/
    rl_license_t  license;
    surplus_real_t sig[RL_DIM];
} render_engine_t;

typedef struct rl_result {
    uint32_t       total;
    uint32_t       open_count;         /* engines that are actually integrable  */
    uint32_t       distinct_archetypes;/* rendering diversity (ISF)             */
    surplus_real_t continuity;         /* mean adjacent-engine relationship     */
    uint8_t        proprietary_flagged;/* 1 = every proprietary engine refused  */
    uint8_t        coherent;           /* diverse AND continuous AND open-usable */
} rl_result_t;

/* The built-in lineage table (era-ordered). */
const render_engine_t *render_lineage_table(int *count_out);

/* Assess the lineage: diversity + continuity over the OPEN engines, and confirm
 * every proprietary engine is refused integration. */
void render_lineage_assess(const render_engine_t *e, uint32_t n, rl_result_t *out);

/* On-target self-check. Returns 1 if the open graphics evolution is a coherent,
 * continuous, integrable lineage and proprietary engines are correctly refused.
 * Outputs the distinct archetype count and continuity permille. */
int  render_lineage_selfcheck(uint32_t *distinct_out, uint32_t *continuity_permille_out);

#endif /* ZXV_RENDER_LINEAGE_H */
