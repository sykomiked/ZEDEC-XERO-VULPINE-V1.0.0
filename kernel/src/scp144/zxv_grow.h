/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_grow.h — stage-0 fractal unfold: the seed grows the way a seed grows
 * a plant. The ELF carries the compressed geometry; this iterative state
 * machine unfolds it into RAM in twelve rings, one per Fibonacci rung.
 *
 * Ring k (dimension F_k) emits exactly what the jump F_{k-1} -> F_k adds:
 * the rung's Cassini determinant, the Zeckendorf modes of the new
 * coordinates, and the F_k^2 - F_{k-1}^2 shell of lattice cells — so the
 * twelve rings partition the whole 144-dimensional geometry once, each
 * ring's bytes nested inside the next the way a tree's rings are. Every
 * byte is regenerated from the same pure functions that wrote the ROM
 * (never copied from it); each grown element is also compared against the
 * ROM so the ring both unfolds the plant and audits it.
 *
 * The running transcript digests to zxv_grow_t.root; gen_seed_blob.py
 * mirrors the serialization byte-for-byte and commits that root in the
 * seed as zxv_seed_grow_root, so scp144_boot_selfcheck can prove the ELF
 * regrows the exact plant the release was built from — and fail closed
 * when it does not.
 *
 * Bounded by design: max_rung 1..12 selects how far the seed unfolds.
 * A node that only needs the E8 anchor grows to rung 6 (596 bytes); a
 * full node grows to rung 12 (166,824 bytes). No recursion, no heap,
 * caller-supplied arena. Freestanding: no libc, no floats. */
#ifndef ZXV_GROW_H
#define ZXV_GROW_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define ZXV_GROW_RUNGS 12u

enum {
    ZXV_GROW_OK        =  0,
    ZXV_GROW_ERR_ARG   = -1,
    ZXV_GROW_ERR_SPACE = -2,
};

typedef struct zxv_grow {
    uint32_t rings;      /* rings unfolded so far (0..12)               */
    uint32_t written;    /* canonical bytes emitted into the arena      */
    uint32_t rom_diff;   /* grown elements that differ from the ROM     */
    uint8_t  root[32];   /* transcript digest; final after ring 12      */
    int      fault;      /* 0 or ZXV_GROW_ERR_*                          */
} zxv_grow_t;

/* Arena bytes a grow to `max_rung` (1..12, clamped) needs. */
uint32_t zxv_grow_bytes(uint32_t max_rung);

/* Unfold the seed into `arena` through `max_rung`. Fills `st`; returns the
 * number of rings grown, or -1 on fault (st->fault set). The caller may
 * then compare st->root to zxv_seed_grow_root from zxv_seed_blob.h. */
int zxv_grow(zxv_grow_t *st, uint8_t *arena, uint32_t cap, uint32_t max_rung);

#endif /* ZXV_GROW_H */
