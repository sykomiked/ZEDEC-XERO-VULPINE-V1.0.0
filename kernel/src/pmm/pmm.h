/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pmm.h — physical frame allocator with four-valued frame state.
 *
 * Each 4 KB frame has two bits, in the lpx base order F < N < B < T:
 *
 *   00 F  unusable  firmware, MMIO, bad RAM, or not ours: never handed out
 *   01 N  free      nobody has claimed it
 *   10 B  fenced    contradictory evidence (freed while already free,
 *                   claimed while already owned): never handed out again
 *   11 T  owned     exactly one owner
 *
 * Why four states and not a free bit. A double free is the case a free bit
 * gets wrong: a free list is corrupted or the kernel panics. Here the frame
 * holds both "someone freed it" and "it was already free", so it becomes B:
 * it is fenced for the life of the allocator, counted, and the call reports
 * BOTH so the caller can log it. The machine keeps running and loses one
 * page instead of its integrity.
 *
 * Verdicts (lpres_state_t, so callers can feed lpx_action directly):
 *   pmm_alloc  TRUE + a frame, or NEITHER when none is free (the caller
 *              waits for a free; a pure allocator never decides to kill)
 *   pmm_free   TRUE (freed), BOTH (was free: now fenced), FALSE (not a
 *              frame of this pool, misaligned, unusable or fenced: no change)
 *   pmm_claim  TRUE (a free frame is now owned), BOTH (already owned:
 *              fenced), FALSE (unusable or fenced)
 *
 * Costs: 2 bits per frame (64 KB per GB), no allocation, no float, no libc.
 * pmm_alloc scans from a rotating hint a byte (four frames) at a time and
 * skips any byte with no free frame in one compare.
 * Not thread-safe: the caller serialises (single CPU today).
 */
#ifndef ZXV_PMM_H
#define ZXV_PMM_H

#include <stdint.h>
#include "lpres.h"

#define PMM_FRAME               4096u
#define PMM_BITS_BYTES(nframes) (((nframes) + 3u) / 4u)

enum { PMM_F = 0u, PMM_N = 1u, PMM_B = 2u, PMM_T = 3u };

typedef struct {
    uint64_t base; /* physical address of frame 0, 4 KB aligned */
    uint32_t nframes;
    uint8_t *bits; /* PMM_BITS_BYTES(nframes) bytes, owned by the caller */
    uint32_t hint; /* byte index where the next scan starts */
    uint32_t n_free, n_owned, n_fenced, n_unusable;
} pmm_t;

/* All frames start free (N). Returns 0, or -1 on bad arguments. */
int pmm_init(pmm_t *p, uint64_t base, uint32_t nframes, uint8_t *bits);

/* Mark a free frame unusable (F), e.g. a range firmware reserved.
 * TRUE on success; FALSE if the frame is not free or not in the pool. */
lpres_state_t pmm_reserve(pmm_t *p, uint64_t pa);

lpres_state_t pmm_alloc(pmm_t *p, uint64_t *pa_out);
lpres_state_t pmm_free(pmm_t *p, uint64_t pa);
lpres_state_t pmm_claim(pmm_t *p, uint64_t pa);

/* The frame's two-bit state (PMM_F/N/B/T); PMM_F for an address outside
 * the pool or not frame-aligned. */
unsigned pmm_state(const pmm_t *p, uint64_t pa);

/* Is pa a frame of this pool (aligned and in range)? */
int pmm_owns_range(const pmm_t *p, uint64_t pa);

#endif
