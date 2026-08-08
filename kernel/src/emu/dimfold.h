/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* dimfold.h — Dimensional Fold: the flatten/unflatten mechanic as a general tool.
 *
 * The same mechanic that lifts a 2D ROM into 3D volume (see rom_dimensions.h) is
 * not only for graphics. Its essence is a TWO-WAY, KEYED, DIMENSION-CHANGING map:
 *
 *   FLATTEN  (fold down): a thing -> its IDENTITY, a compact lower-dimensional
 *            form (a flattened matrix / a hash).
 *   UNFLATTEN (fold up): the identity -> the full higher-dimensional thing, with
 *            the interior FILLED IN, keyed so nothing is lost.
 *
 * The keys are the same everywhere in this system:
 *   - PRIME      = the forward LOCK           (here: SHA-256d, double SHA-256)
 *   - FIBONACCI  = the RECOVERY key           (the nested φ-shell decomposition)
 *   - φ (PHI)    = the axiom of ALIGNMENT      (consecutive Fibonacci ratios -> φ)
 *
 * From this one core come many faces, all lossless / exactly invertible:
 *   1. TRANSFORM  — a reversible nested multi-resolution decomposition (an integer
 *                   Haar / S-transform): the Russian-doll matrix, coarse identity
 *                   plus nested detail shells. `dimfold_fold` / `dimfold_unfold`.
 *   2. COMPRESS   — that transform makes smooth/self-similar data sparse; encode
 *                   the coarse identity + sparse shells small. `dimfold_compress`
 *                   / `dimfold_expand`. Lossless.
 *   3. ENCRYPT    — the prime LOCK (SHA-256d keystream) seals; re-applying opens.
 *                   `dimfold_seal` / `dimfold_open`. Involutive.
 *   4. IDENTITY   — `dimfold_identity` = the SHA-256d hash: the thing's identity.
 *
 * All three round-trip exactly; `dimfold_selfcheck` proves it on-target. */
#ifndef ZXV_DIMFOLD_H
#define ZXV_DIMFOLD_H

#include <stdint.h>
#include <stddef.h>

/* ---- 1. TRANSFORM: the reversible nested φ-shell decomposition -------------- */
/* In place over `n` ints (n must be a power of two, n>=2). fold: coarse identity
 * lands at a[0], nested detail shells fill the rest. unfold is its exact inverse. */
void dimfold_fold(int32_t *a, int n);
void dimfold_unfold(int32_t *a, int n);

/* ---- 2. COMPRESS / EXPAND (lossless) --------------------------------------- */
/* Compress `n` bytes of `in` into `out` (capacity must be >= dimfold_bound(n));
 * returns the compressed length. Expand reverses it, writing up to `out_cap`
 * bytes and returning the original length (0 on error). */
int  dimfold_compress(const uint8_t *in, int n, uint8_t *out, int out_cap);
int  dimfold_expand(const uint8_t *in, int n, uint8_t *out, int out_cap);
int  dimfold_bound(int n);           /* worst-case compressed size for n bytes   */

/* ---- 3. SEAL / OPEN (symmetric encryption, SHA-256d keystream) -------------- */
/* XOR `data[0..n)` with a SHA-256d(key||counter) keystream. `dimfold_open` is the
 * same operation (involutive): seal then open with the same key restores data. */
void dimfold_seal(uint8_t *data, int n, const uint8_t *key, int keylen);
void dimfold_open(uint8_t *data, int n, const uint8_t *key, int keylen);

/* ---- 4. IDENTITY ----------------------------------------------------------- */
/* The thing's identity = SHA-256d (the prime lock) over its bytes. */
void dimfold_identity(const uint8_t *data, int n, uint8_t out[32]);

/* On-target self-check: fold/unfold, compress/expand, and seal/open all round-trip
 * losslessly; the Fibonacci recovery key aligns to φ. Returns 1 on pass and writes
 * the achieved compression ratio (permille of original) for a self-similar buffer. */
int  dimfold_selfcheck(uint32_t *ratio_permille_out);

#endif /* ZXV_DIMFOLD_H */
