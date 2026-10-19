/* zxv_scale.h — the capacity oracle: how far the seed grows on THIS machine.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * THE SCALING RULE. The same seed grows into a different-sized plant
 * depending on the soil — and the plant never outgrows the soil. This
 * module reads the hardware the kernel actually booted on (CPU count, RAM,
 * display presence, network presence, PQ capability) and computes, EXACTLY
 * and DETERMINISTICALLY, how many growth rings of the seed unfold. A
 * 2-core 512 MB node earns the E8-anchored rungs; a 64-core server earns
 * the full 12. The geometry is the SAME seed; only the unfold depth varies.
 *
 * The three mirrored states scale TOGETHER: the rung the machine earns sets
 * the feature tier for the human space (zui depth), the Chiglet space
 * (swarm depth) and the network space (mesh depth) at the same phase tick.
 * Nothing ever exceeds the machine's budget: the oracle checks capacity
 * BEFORE each ring (check-invariants-then-mutate) and refuses to grow past
 * it — fail closed, never a partial overrun.
 *
 * EXACT ONLY. No float: capacity is in MiB and cores, the rung is an
 * integer 1..12, the tiers are integers. The oracle is a pure function of
 * (hardware, mode): same machine, same mode -> same rung, always.
 */
#ifndef ZXV_SCALE_H
#define ZXV_SCALE_H

#include <stdint.h>
#include <stdbool.h>

#include "zxv_grow.h" /* ZXV_GROW_RUNGS — the unfold depth ceiling          */

/* ---- the hardware the machine actually has ---------------------------------- */

typedef struct {
    uint32_t cores;   /* logical CPU count (0 = unknown, probe-default) */
    uint32_t mem_mib; /* RAM in MiB (0 = unknown)                       */
    bool has_display; /* a framebuffer / panel negotiated               */
    bool has_net;     /* a NIC present (the GATE's baked peers or DHCP)  */
    bool has_pq;      /* post-quantum primitives linked (ML-KEM/ML-DSA)  */
} zxv_hw_t;

/* ---- the feature tiers the earned rung unlocks ------------------------------- */

typedef enum {
    ZXV_TIER_CORE = 0,  /* rings 1..6:  the E8-anchored base              */
    ZXV_TIER_STACK = 1, /* rings 7..9:  filesystem, network, the GATE     */
    ZXV_TIER_SWARM = 2, /* rings 10..11: the Chiglet + tensor + mirror    */
    ZXV_TIER_FULL = 3,  /* ring 12:     the full 144-dimensional plant    */
} zxv_tier_t;

/* What the earned rung means for each of the three mirrored states. */
typedef struct {
    uint32_t rung;        /* growth rings to unfold (1..ZXV_GROW_RUNGS)     */
    zxv_tier_t tier;      /* the tier that rung unlocks                     */
    uint32_t human_depth; /* zui / shell depth (0 = headless)               */
    uint32_t swarm_depth; /* Chiglet experts available (0 = none)           */
    uint32_t mesh_depth;  /* network/mesh capacity (0 = isolated)           */
    bool fail_closed;     /* capacity unknown or below the base: safe mode  */
} zxv_scale_t;

/* ---- API -------------------------------------------------------------------- */

/* Compute the unfold depth for the hardware `hw` under `headless` (server /
 * no-display) and `safe` (reduced) modes. Deterministic: same hw + same
 * mode -> same scale. `hw` fields of 0 mean "unknown" and are probed or
 * defaulted conservatively (never optimistically). */
void zxv_scale_compute(const zxv_hw_t *hw, bool headless, bool safe, zxv_scale_t *out);

/* The arena bytes the earned rung needs (zxv_grow_bytes of out->rung). */
uint32_t zxv_scale_bytes(const zxv_scale_t *s);

/* Can the seed unfold one more ring within `mem_mib`? The oracle's
 * check-invariants-then-mutate gate: refuses (false) if the machine cannot
 * hold the next ring's arena plus a working margin. Exact integer. */
bool zxv_scale_can_grow(const zxv_scale_t *s, uint32_t mem_mib);

/* The tier names, for the boot banner and the mirror. */
const char *zxv_tier_name(zxv_tier_t t);

#endif /* ZXV_SCALE_H */