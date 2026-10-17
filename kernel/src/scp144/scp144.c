/* scp144.c — Sicilian Crown Protection, 144-dimensional.
 *
 * The binding maps the two public handshake identifiers onto the Fibonacci
 * ladder (F1..F12 = 1..144). A 32-byte shake256 of the concatenated
 * identifiers is reduced mod (F12 + 1), then decomposed in Zeckendorf
 * form: every integer has a unique representation as a sum of
 * NON-CONSECUTIVE Fibonacci numbers, so the ladder point is canonical and
 * collision-free per digest. The norm (the sum of the ladder weights at
 * the set positions) and the residues under a few moduli are then folded
 * with the identifiers into the 32-byte binding that the session key
 * schedule mixes in (vna_session.c).
 *
 * Exact integers throughout: no float, no division by zero, no
 * allocation. Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9
 * Genetics, LLC. SPDX-License-Identifier: Apache-2.0.
 */
#include "scp144.h"

#include <stddef.h>

/* The Fibonacci ladder up to F12 = 144. */
static const uint64_t scp_fib[12] = {
    1u, 1u, 2u, 3u, 5u, 8u, 13u, 21u, 34u, 55u, 89u, 144u
};

/* The moduli for the residue fold; all are coprime with 2. */
static const uint8_t scp_mods[4] = { 7u, 13u, 31u, 89u };

void scp144_lattice_bind(const uint8_t init_id[32],
                         const uint8_t resp_id[32],
                         uint8_t out[32])
{
    uint8_t seed[64];
    uint8_t digest[32];
    uint64_t v = 0;
    unsigned i;

    if (!init_id || !resp_id || !out)
        return;

    for (i = 0; i < 32; i++) {
        seed[i] = init_id[i];
        seed[i + 32] = resp_id[i];
    }
    /* The kernel's SHAKE-256 (see kernel/src/mlkem/keccak.c / sha3.c). */
    extern void shake256(const uint8_t *in, size_t inlen, uint8_t *out, size_t outlen);
    shake256(seed, sizeof seed, digest, sizeof digest);

    /* v = the digest as a big-endian integer, reduced to a ladder value. */
    for (i = 0; i < 32; i++)
        v = (v << 8) | digest[i];
    v %= (uint64_t) scp_fib[11] + 1u;

    /* Zeckendorf decomposition: greedily subtract the largest Fibonacci
     * number that fits, then skip the next position (non-consecutive). */
    {
        uint64_t norm = 0;
        uint8_t residues[4];
        uint8_t fold[64 + 4];
        unsigned pos = 11;

        while (v > 0 && pos > 0) {
            if (scp_fib[pos] <= v) {
                v -= scp_fib[pos];
                norm += scp_fib[pos];
                pos = pos > 1 ? pos - 2 : 0; /* non-consecutive */
            } else {
                pos--;
            }
        }
        if (v > 0) { /* the final F1 unit if it still fits */
            norm += 1u;
        }

        for (i = 0; i < sizeof residues; i++)
            residues[i] = (uint8_t) (norm % scp_mods[i]);

        for (i = 0; i < 32; i++) {
            fold[i] = init_id[i];
            fold[i + 32] = resp_id[i];
        }
        fold[64] = (uint8_t) (norm >> 24);
        fold[65] = (uint8_t) (norm >> 16);
        fold[66] = (uint8_t) (norm >> 8);
        fold[67] = (uint8_t) norm;
        fold[64 + 4] = residues[0];
        fold[64 + 5] = residues[1];
        fold[64 + 6] = residues[2];
        fold[64 + 7] = residues[3];

        shake256(fold, sizeof fold, out, 32);
    }
}