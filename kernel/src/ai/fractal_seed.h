/* fractal_seed.h — the holographic seed: weights synthesised from fractal
 * recurrence, not stored. See fractal_seed.c.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * THE HOLOGRAPHIC SEED. A language model is not a linear array of weights;
 * it is a self-similar manifold. This module stores the GENERATIVE DNA — a
 * small set of affine transform coefficients and scaling factors (an iterated
 * function system) — and unfolds arbitrarily deep weight layers from it on
 * demand, deterministically. The seed file is megabytes, not gigabytes: the
 * fractal recurrence IS the model.
 *
 * EXACT ONLY. Every coefficient is surplus_real_t, which in a kernel image
 * is int64_t Q32.32 (kernel/src/surplus/surplus.h): no float, no drift, the
 * same weight unfolded twice is bit-identical. The unfolding is phase-locked
 * to the 12 Solfeggio frequencies and the SCP-144 144-dimensional lattice
 * geometry, so the generated weights align with the system's topological
 * invariants.
 *
 * The host compiler (kernel/src/ai/fractal_seed_compile.py) ingests open
 * weights, extracts the recurring motifs by spectral decomposition, and
 * writes this compact binary seed; the kernel unfolds it into chg_model_t
 * (the Chiglet's expert/label space) at macro-cycle init.
 */
#ifndef ZXV_FRACTAL_SEED_H
#define ZXV_FRACTAL_SEED_H

#include <stdint.h>
#include <stdbool.h>

#include "surplus.h"       /* surplus_real_t — int64 Q32.32 in kernel images */
#include "../chiglet/chiglet.h"  /* chg_model_t — the unfolded target        */

/* ---- bounds (bounded, freestanding) -------------------------------------- */

#define FRS_MAX_COEFFS      64u   /* IFS affine coefficients per layer       */
#define FRS_MAX_LAYERS      12u   /* one per Solfeggio frequency (174..963)  */
#define FRS_NAME_LEN        32u
#define FRS_DIGEST_LEN      32u   /* sha256 of the seed bytes                */

/* ---- the seed file header --------------------------------------------------
 * The on-disk seed is: 'FRSE' | version | layer_count | digest[32] | then
 * per-layer coefficient blocks. The digest covers the WHOLE file so a
 * tampered seed unfolds differently and is refused at load. */
#define FRS_MAGIC0 'F'
#define FRS_MAGIC1 'R'
#define FRS_MAGIC2 'S'
#define FRS_MAGIC3 'E'
#define FRS_VERSION 1u
#define FRS_HDR_SIZE 44u   /* magic4 | version4 | layers4 | digest32        */

typedef struct {
    uint32_t version;
    uint32_t layer_count;
    uint8_t  digest[FRS_DIGEST_LEN];
} fractal_seed_hdr_t;

/* One IFS layer: the affine map that unfolds a weight layer. `a`,`b`,`c`,
 * `d` are the linear coefficients (Q32.32), `tx`,`ty` the translation, and
 * `freq` the Solfeggio frequency this layer is phase-locked to (0 = free).
 * All fields are exact integers; the generator fills them from spectral
 * decomposition of the source weights. */
typedef struct {
    int32_t a, b, c, d;        /* Q32.32 linear coefficients                */
    int32_t tx, ty;            /* Q32.32 translation                        */
    uint32_t freq;             /* Solfeggio Hz anchor (0 = unbound)         */
    uint32_t scale;            /* Q32.32 output scale                       */
    char     name[FRS_NAME_LEN];
} fractal_seed_layer_t;

/* ---- the in-kernel synthesizer --------------------------------------------- */

typedef struct {
    fractal_seed_hdr_t  hdr;
    fractal_seed_layer_t layers[FRS_MAX_LAYERS];
    uint32_t            layer_count;
    bool                valid;
} fractal_seed_t;

/* Parse + fully validate a seed image (magic, version, layer bounds, digest
 * over the whole file). Fail-closed: valid stays false unless every check
 * passed. The image is BORROWED, not copied. */
bool fractal_seed_parse(const uint8_t *img, uint32_t len, fractal_seed_t *out);

/* Unfold one weight value at (layer, i, j) from the IFS recurrence.
 * Deterministic: the same (seed, layer, i, j) always yields the same exact
 * integer. Returns 0 for an invalid seed or out-of-range coordinates. */
surplus_real_t fractal_seed_unfold(const fractal_seed_t *s, uint32_t layer,
                                   uint32_t i, uint32_t j);

/* Unfold an entire chg_model_t (the Chiglet's expert/label space) from the
 * seed: prototypes are synthesised layer by layer, phase-locked to the layer
 * frequencies. Deterministic: same seed -> same model, byte-identical.
 * Returns false on an invalid seed or a model too large for the layers. */
bool fractal_seed_build_model(const fractal_seed_t *s, chg_model_t *m);

/* The digest of the seed that built the current model, for provenance. */
const uint8_t *fractal_seed_digest(const fractal_seed_t *s);

/* ---- the host compiler (kernel/src/ai/fractal_seed_compile.py) emits this
 * ---- binary; the Makefile target generate-holographic-seed binds its CID. */

#endif /* ZXV_FRACTAL_SEED_H */