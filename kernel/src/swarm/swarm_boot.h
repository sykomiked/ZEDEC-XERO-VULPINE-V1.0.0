/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* swarm_boot.h — the swarm's boot self-check: one Fibonacci budget cycle,
 * run by kernel_main before [BOOT_OK] on bare metal and by
 * test_ai_selfcheck on the host, against the same expected constants.
 *
 * THE CYCLE.  L = 4 levels, T = 1000 tokens, levels filled to their R1
 * capacities 1, 2, 3, 5 (model ids 1 | 2 3 | 4 5 6 | 7 8 9 10 11), and a
 * sixth model on level 3 must be refused. R2 weights 5 : 3 : 2 : 1 (sum 11).
 * R3 + R5 by hand:
 *   1000 * {5,3,2,1} / 11 = 454 r6, 272 r8, 181 r9, 90 r10; 997 placed, the
 *   3 left go to the largest remainders (levels 3, 2, 1):
 *   level budgets 454, 273, 182, 91 (sum 1000).
 * R4 inside each level, ties to the lower slot:
 *   454 | 137 136 | 61 61 60 | 19 18 18 18 18.
 * Then model 1 asks for 500 and is granted 454 (never past the allotment),
 * model 7 asks for 10 and gets 10, and the close expires 1000 - 464 = 536
 * unused tokens (R6).
 *
 * THE HASH.  FNV-1a (32-bit, zt_boot_fnv) over, in order: the 11 allotments, the 4 level
 * budgets, the two grants and last_unused, each fed as the 4 little-endian
 * bytes of its low 32 bits. Defined arithmetically, so it is the same on
 * every byte order. SWARM_BOOT_HASH is that value for the numbers above;
 * test_ai_selfcheck recomputes it from the hand-worked list, independently
 * of the allocator, and also runs this function on the host.
 *
 * Freestanding: integer only, no libc, no allocation (the budget is one
 * static swarm_budget_t, about 2.8 KiB of BSS).
 */
#ifndef SWARM_BOOT_H
#define SWARM_BOOT_H

#include <stdbool.h>
#include <stdint.h>

#define SWARM_BOOT_LEVELS 4u
#define SWARM_BOOT_TOKENS 1000u
#define SWARM_BOOT_MODELS 11u
#define SWARM_BOOT_UNUSED 536u
#define SWARM_BOOT_HASH   0x4346b551u
/* fnv(fnv(INIT, SWARM_BOOT_HASH), ZT_BOOT_HASH): the hash on the [AI_OK] line. */
#define AI_BOOT_HASH 0x90553e54u

/* Run the cycle and check every number above. Returns 0 on success, or the
 * 1-based number of the first check that failed. *hash (may be NULL)
 * receives the FNV-1a of what was actually computed, so a mismatch can be
 * reported. */
int swarm_boot_selfcheck(uint32_t *hash);

/* THE COMBINED CHECK, called by kernel_main before [BOOT_OK] on arm64 and
 * x86_64. Runs swarm_boot_selfcheck and zt_boot_selfcheck (zt_boot.h) and
 * prints ONE line through `out` (newline included):
 *   "[AI_OK] swarm+tensor selfcheck 0x<AI_BOOT_HASH>"           on success,
 *   "[FAULT] AI selfcheck mismatch: swarm step N 0x.., tensor step M 0x.."
 * otherwise. Returns true only when both pass and the combined hash equals
 * AI_BOOT_HASH; on false the caller halts. */
bool ai_boot_selfcheck(void (*out)(const char *));

#endif /* SWARM_BOOT_H */
