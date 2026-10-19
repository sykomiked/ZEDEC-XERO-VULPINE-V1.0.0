/* zxv_scale.c — the capacity oracle (see zxv_scale.h).
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * Freestanding: no libc, no float, no allocation. The oracle is a pure
 * integer function of (hardware, mode): the same machine always earns the
 * same rung, so the unfolded OS is deterministic per hardware profile.
 */
#include "zxv_scale.h"

/* --gc-sections KEEP — see mirror_ui.h: no boot caller yet (boot_features
 * adopts it), retain the API. */
#if defined(__GNUC__) && (__GNUC__ >= 11)
#    define SCALE_KEEP __attribute__((used, retain))
#else
#    define SCALE_KEEP __attribute__((used))
#endif

/* The rung thresholds, in MiB, for the three feature tiers. These are the
 * machine's "soil": a plant grows as deep as the soil allows. Chosen so a
 * 512 MB node earns the E8 base, a 2 GB node the full stack, a 4 GB node
 * the swarm, and 8 GB the complete 144-dimensional plant. */
#define SCALE_MEM_STACK 2048u /* MiB for rings 7..9 (filesystem+net)      */
#define SCALE_MEM_SWARM 4096u /* MiB for rings 10..11 (Chiglet+tensor)    */
#define SCALE_MEM_FULL  8192u /* MiB for ring 12 (the full plant)         */

/* The working margin the oracle keeps free: the next ring's arena plus 1 MiB
 * of breathing room. A machine below the margin is refused the ring. */
#define SCALE_MARGIN_MIB 1u

/* Conservative defaults when a field is unknown (0): never optimistic. An
 * unknown RAM is assumed tiny (the base tier only); an unknown core count
 * is assumed 1; an unknown display/network is assumed absent. */
#define SCALE_DEFAULT_MEM_MIB 256u

static uint32_t clamp_rung(uint32_t rung)
{
    if (rung < 1u) return 1u;
    if (rung > ZXV_GROW_RUNGS) return ZXV_GROW_RUNGS;
    return rung;
}

SCALE_KEEP void zxv_scale_compute(const zxv_hw_t *hw, bool headless, bool safe, zxv_scale_t *out)
{
    uint32_t mem;
    uint32_t rung = 1u;
    zxv_tier_t tier = ZXV_TIER_CORE;

    if (!out) return;
    out->fail_closed = false;

    /* safe mode: the reduced plant, always the E8 base only. */
    if (safe) {
        out->rung = 6u;
        out->tier = ZXV_TIER_CORE;
        out->human_depth = 0u;
        out->swarm_depth = 0u;
        out->mesh_depth = 0u;
        out->fail_closed = true;
        return;
    }

    /* unknown RAM is assumed tiny: never optimistic. */
    mem = (hw && hw->mem_mib != 0u) ? hw->mem_mib : SCALE_DEFAULT_MEM_MIB;

    /* the soil decides the depth: RAM first, then the presence gates. */
    if (mem >= SCALE_MEM_FULL) {
        rung = 12u;
        tier = ZXV_TIER_FULL;
    } else if (mem >= SCALE_MEM_SWARM) {
        rung = 11u;
        tier = ZXV_TIER_SWARM;
    } else if (mem >= SCALE_MEM_STACK) {
        rung = 9u;
        tier = ZXV_TIER_STACK;
    } else {
        rung = 6u;
        tier = ZXV_TIER_CORE;
    }

    /* a machine with no display cannot unfold a GUI, whatever its RAM. */
    if (headless || (hw && !hw->has_display)) {
        if (tier == ZXV_TIER_FULL || tier == ZXV_TIER_SWARM) {
            rung = 9u;
            tier = ZXV_TIER_STACK; /* server: stack, no GUI     */
        }
    }
    /* a machine with no network cannot unfold the mesh, whatever its RAM. */
    if (hw && !hw->has_net) {
        if (tier == ZXV_TIER_FULL) {
            rung = 11u;
            tier = ZXV_TIER_SWARM; /* no mesh: stop at the swarm */
        }
    }

    rung = clamp_rung(rung);
    out->rung = rung;
    out->tier = tier;

    /* the three mirrored states scale together, from the same rung. */
    switch (tier) {
    case ZXV_TIER_CORE:
        out->human_depth = (headless || (hw && !hw->has_display)) ? 0u : 1u;
        out->swarm_depth = 0u;
        out->mesh_depth = 0u;
        break;
    case ZXV_TIER_STACK:
        out->human_depth = (headless || (hw && !hw->has_display)) ? 0u : 2u;
        out->swarm_depth = 1u;
        out->mesh_depth = (hw && !hw->has_net) ? 0u : 1u;
        break;
    case ZXV_TIER_SWARM:
        out->human_depth = (headless || (hw && !hw->has_display)) ? 0u : 3u;
        out->swarm_depth = 2u;
        out->mesh_depth = (hw && !hw->has_net) ? 0u : 2u;
        break;
    default: /* ZXV_TIER_FULL */
        out->human_depth = (headless || (hw && !hw->has_display)) ? 0u : 4u;
        out->swarm_depth = 3u;
        out->mesh_depth = (hw && !hw->has_net) ? 0u : 3u;
        break;
    }
}

SCALE_KEEP uint32_t zxv_scale_bytes(const zxv_scale_t *s)
{
    if (!s) return 0u;
    return zxv_grow_bytes(s->rung);
}

SCALE_KEEP bool zxv_scale_can_grow(const zxv_scale_t *s, uint32_t mem_mib)
{
    uint32_t next, need;
    if (!s) return false;
    if (s->rung >= ZXV_GROW_RUNGS) return false; /* already the full plant */
    next = s->rung + 1u;
    need = zxv_grow_bytes(next);
    /* the next ring's arena plus the working margin must fit in RAM. */
    if (need + SCALE_MARGIN_MIB * 1024u * 1024u > mem_mib * 1024u * 1024u) return false;
    return true;
}

SCALE_KEEP const char *zxv_tier_name(zxv_tier_t t)
{
    static const char *const n[4] = {
        "core",
        "stack",
        "swarm",
        "full",
    };
    if (t > ZXV_TIER_FULL) return "?";
    return n[t];
}