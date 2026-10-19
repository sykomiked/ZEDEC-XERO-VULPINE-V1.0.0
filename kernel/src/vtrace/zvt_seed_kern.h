/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zvt_seed_kern.h — the seed geometry unfolded on the GPU, one thread per
 * lattice cell. Written once in the subset that is C11, CUDA C++ and Metal
 * Shading Language — the same discipline as zvt_kern.h — so the CPU
 * reference and the GPU kernels share the source by construction, and a GPU
 * result is the CPU's bits (integer arithmetic has one answer in any order).
 *
 * WHAT THIS UNFOLDS: the ELF's seed geometry, not its bytes. The SCP-144
 * 144-dimensional lattice is 20,736 cells, each a pure function of (i, j):
 * the Zeckendorf mode of the coordinates (the Fibonacci/Casimir cavity
 * boundary), the Casimir mode energy, and the Cassini determinant that
 * drives the AC push-pull. Every cell is independent — embarrassingly
 * parallel, one thread per cell. The holographic seed's weights and the
 * seed bank's 408 vector icons are the same kind of geometry: pure
 * functions of coordinates, unfolded in parallel, never copied.
 *
 * Rules (identical to zvt_kern.h): no preprocessor lines and no `#` (the
 * source is stringified for NVRTC); pointers only as ZK_DEV; types zi
 * (int32), zu (uint32), zb (uint8); no 64-bit arithmetic — every sum here
 * fits 32 bits for the caps the lattice sets.
 *
 * The stages (one thread per output element):
 *   zk_seed_mode    the Zeckendorf support mask of cell (i, j)
 *   zk_seed_energy  the Casimir mode energy of the cell
 *   zk_seed_cassini the Cassini determinant F_{k+1}*F_{k-1} - F_k^2 = +-1
 *                   folded with the cell's residues (the boundary seal)
 * Not a header to include anywhere else. */

#ifndef ZVT_SEED_KERN_ONCE
#define ZVT_SEED_KERN_ONCE

ZVT_KSRC(

    /* the 12-rung Fibonacci ladder, F1..F12 = 1..144 (F0 = 0, F13 = 233) */
    static inline zu zk_fib(zu k) {
        zu f0 = 0u, f1 = 1u, t;
        if (k == 0u) return 0u;
        if (k == 1u) return 1u;
        for (zu i = 2u; i <= k; i++) { t = f0 + f1; f0 = f1; f1 = t; }
        return f1;
    }

    /* the unique non-consecutive Zeckendorf support mask of v in 0..144.
     * Mirrors scp144_zeckendorf_mode()'s core loop exactly, as integers. */
    static inline zu zk_zeckendorf(zu v, zu *energy) {
        zu mask = 0u, e = 0u, pos = 11u;
        zu f12 = zk_fib(12u);
        v %= f12 + 1u;
        while (v > 0u && pos > 0u) {
            zu f = zk_fib(pos);
            if (f <= v) {
                v -= f;
                mask |= (zu) 1u << pos;
                if ((pos & 1u) != 0u) e += f * (pos + 1u);
                else                  e += f * (13u - pos);
                pos = pos > 1u ? pos - 2u : 0u;   /* non-consecutive rule  */
            } else {
                pos--;
            }
        }
        if (v > 0u) { mask |= 1u; e += 1u; }
        if (energy) *energy = e;
        return mask;
    }

    /* the Cassini identity: F_{k+1}*F_{k-1} - F_k^2 is exactly +-1, the
     * bijective contraction/expansion the AC push-pull alternates. */
    static inline zi zk_cassini(zu k) {
        zu fk = zk_fib(k), fp = zk_fib(k + 1u), fm = k > 0u ? zk_fib(k - 1u) : 0u;
        zu p = fp * fm;
        zu sq = fk * fk;
        return p >= sq ? (zi)(p - sq) : -(zi)(sq - p);
    }

    /* one cell of the lattice: mode mask + casimir energy + cassini sign.
     * (i, j) are the cell's coordinates in the 144x144 lattice. */
    static inline void zk_seed_cell(zu i, zu j, ZK_DEV zu *mode, ZK_DEV zu *energy,
                                    ZK_DEV zi *cassini) {
        zu e = 0u;
        *mode = zk_zeckendorf(i, &e);
        *energy = e;
        /* the Cassini fold with the cell's residue: the boundary seal. */
        {
            zu k = (i + j) % 12u + 1u;
            zi c = zk_cassini(k);
            /* the coprime residue fold: (i mod 7, j mod 13) toggle the sign
             * of the determinant — the push-pull polarity per cell. */
            if ((i % 7u) == (j % 13u)) c = -c;
            *cassini = c;
        }
    }
)

#endif /* ZVT_SEED_KERN_ONCE */
