/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* swarm_governor.h — self-aware compute: how much of the machine the swarm
 * may use, re-decided from a hardware scan every fundamental cycle.
 *
 *   G1  REMOTE.  On a remote instance the swarm may use up to 99% of the
 *       machine (cores, memory, GPU memory), leaving 1% for the system.
 *   G2  LOCAL.  On the user's own device it is a guest. It may use at most
 *       13/21 (about 62%) of what is FREE, after other programs, never
 *       what they are using, and never more than 13/21 of the machine.
 *   G3  MEMORY FLOOR.  On a local device it always leaves at least 1/8 of
 *       physical memory untouched.
 *   G4  FIBONACCI SIZE.  The swarm's size follows the budget: levels are
 *       added only while the memory budget covers every level full
 *       (level d holds F(d+2) agents, each costing `agent_mb`), so it grows
 *       and shrinks one whole Fibonacci level at a time.
 *   G5  RATE.  Tokens per cycle scale with the cores granted.
 * Integer only; the scan itself is done by the host platform layer.
 */
#ifndef SWARM_GOVERNOR_H
#define SWARM_GOVERNOR_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    bool remote;               /* a remote instance (G1) or the user's device (G2) */
    uint32_t cores;            /* logical CPU cores */
    uint32_t other_load_milli; /* load from other programs, in cores x 1000 */
    uint64_t mem_total_mb;
    uint64_t mem_free_mb; /* free for us now, after other programs */
    uint64_t gpu_mem_mb;  /* dedicated or unified GPU memory, 0 if none */
} swarm_hw_scan_t;

typedef struct {
    uint32_t cores_milli; /* CPU budget in cores x 1000 */
    uint64_t mem_mb;
    uint64_t gpu_mem_mb;
    uint32_t num_levels; /* G4, 0 if not even the companion fits */
    uint32_t num_agents;
    uint64_t tokens_per_cycle; /* G5 */
} swarm_compute_budget_t;

/* Decide the budget (G1-G5). agent_mb is the memory one agent needs;
 * tokens_per_core is the rate one full core sustains per cycle. */
swarm_compute_budget_t swarm_governor_budget(const swarm_hw_scan_t *hw, uint64_t agent_mb,
                                             uint64_t tokens_per_core);

#endif /* SWARM_GOVERNOR_H */
