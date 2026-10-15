/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx_vec.h — the vector plane: embeddings beside the matrix
 * (docs/spec/VECTORS.md).
 *
 * Every vector names the exact thing it describes (its CID, and its grid
 * word in domain 0x60 when it has one) and the model that computed it (the
 * model's CID). A vector whose thing or model is no longer current is stale,
 * and is reported as stale, never used as if fresh. Vectors are int8 with an
 * integer scale; every distance is integer arithmetic.
 *
 * ALXV node, big-endian, at most one 1 MiB block:
 *   header, 128 bytes: "ALXV", 1, kind (0 leaf: vectors of files; 1
 *     summary: one row per child node), u16 layer (145 for a summary over
 *     layers), u32 rows (n), u16 dim (32..4096, a multiple of 32), u16 zero,
 *     model CID (36), source CID (36: a leaf's layer glue; a summary's
 *     zero), u32 the summary row's scale, 36 zero
 *   records, 48 bytes each: target CID (36: the file, or the child node),
 *     u64 grid word (domain 0x60, or 0 when the thing has none),
 *     u32 scale (Q16: element value = q * scale / 65536)
 *   at the next 64-byte boundary, n + 1 rows of dim / 32 blocks of 34 bytes:
 *     the bytes 00 3C, then 32 int8. Row n is the node's summary: the mean
 *     of its rows, requantised (alx_vec_summary). The two bytes are the
 *     GGUF Q8_0 block header for a scale of exactly one, so the rows are
 *     Q8_0 rows the tensor engine's kernels (and its GPU path) multiply in
 *     place, with integer results identical to alx_vec_dot.
 *
 * Freestanding C11: integers only, no allocation. */
#ifndef ZXV_ALX_VEC_H
#define ZXV_ALX_VEC_H

#include "alx.h"

#define ALX_VEC_HEAD   128u
#define ALX_VEC_REC    48u
#define ALX_VEC_BLOCK  34u
#define ALX_VEC_MAXDIM 4096u
#define ALX_G_VEC      0x60u /* the grid domain of embeddings (GRID_CHAIN.md) */
#define ALX_VEC_SEEN   2048u /* the shape embedding's scratch beyond dim: 2^16 bits */

enum { ALX_VEC_LEAF = 0, ALX_VEC_SUMMARY = 1 };
enum { ALX_VEC_COS = 1, ALX_VEC_DOT = 2, ALX_VEC_L2 = 3 };

typedef struct {
    const uint8_t *b;
    uint32_t len, kind, layer, n, dim, nb; /* nb: blocks per row */
    const uint8_t *model, *source;         /* 36-byte CIDs inside b */
    const uint8_t *rows;                   /* n + 1 rows of nb * 34 bytes */
} alx_vec_t;

/* Open a node: its form, and that row n is the summary of rows 0..n-1. */
int alx_vec_open(alx_vec_t *v, const uint8_t *b, uint32_t len);
/* record i */
const uint8_t *alx_vec_target(const alx_vec_t *v, uint32_t i);
uint64_t alx_vec_word(const alx_vec_t *v, uint32_t i);
/* record i's scale; i == n: the summary's */
uint32_t alx_vec_scale(const alx_vec_t *v, uint32_t i);
/* row i (i == n: the summary) */
const uint8_t *alx_vec_row(const alx_vec_t *v, uint32_t i);
/* element j of a row */
int8_t alx_vec_q(const uint8_t *row, uint32_t j);

/* The exact integer dot product of a row with a query of nb * 32 int8. */
int32_t alx_vec_dot(const uint8_t *row, const int8_t *q, uint32_t nb);
/* sum of squares of a row, or of a query */
uint32_t alx_vec_norm(const uint8_t *row, uint32_t nb);
uint32_t alx_vec_qnorm(const int8_t *q, uint32_t nb);

/* The dot products of all n rows with q: an accelerator (the tensor
 * engine's GPU or SIMD kernels, alx_vec_zt.c) or this file's loop. It
 * returns 0 when it wrote all n, nonzero (having written nothing to rely
 * on) when it cannot; the loop then runs. */
typedef int (*alx_vec_dots_fn)(void *ctx, const uint8_t *rows, uint32_t n, uint32_t nb,
                               const int8_t *q, int32_t *out);

/* Whether row a ranks before row b for a query: higher cosine, higher dot,
 * or lower squared distance, exactly (the cosine compare is done on
 * integers: dot_a * |dot_a| * norm_b against dot_b * |dot_b| * norm_a);
 * ties go to the lower index. A zero row ranks last under cosine. */
bool alx_vec_better(uint32_t metric, int32_t dot_a, uint32_t norm_a, uint32_t ia, int32_t dot_b,
                    uint32_t norm_b, uint32_t ib, uint32_t qnorm);

/* The k best rows of a node for q (rows 0..n-1, not the summary): their
 * indexes in out[0..], best first. dots: scratch for n values. Returns how
 * many were written (min(k, n)), or a negative status. */
int32_t alx_vec_search(const alx_vec_t *v, uint32_t metric, const int8_t *q, uint32_t k,
                       uint32_t *out, int32_t *dots, alx_vec_dots_fn fn, void *fctx);

/* The summary of rows 0..n-1 of a node being built: the mean of their
 * values (q * scale), requantised to int8 with its own scale. rows: n rows
 * of nb blocks; scales: n values. Writes one row (nb blocks) and its scale. */
void alx_vec_summary(const uint8_t *rows, const uint32_t *scales, uint32_t n, uint32_t nb,
                     uint8_t *out_row, uint32_t *out_scale);

/* The shape embedding: each distinct identifier in a text (a name: a
 * letter or _ then letters, digits and _) counted once, hashed into dim
 * buckets with a sign, requantised to int8 (scale: Q16 count per unit). A
 * lexical fingerprint of which names a text uses, not a learned meaning;
 * deterministic, needs no model, and used until a Tiamat cell's embeddings
 * are given (pack_matrix.py --vectors). Chosen by measurement
 * (docs/spec/VECTORS.md). dim: a power of two from 32 to 4096. work: dim +
 * ALX_VEC_SEEN int32s of scratch. Writes dim / 32 blocks. Returns the scale
 * (0 for a text with no names: an all-zero row). */
uint32_t alx_vec_shape(const uint8_t *b, uint32_t len, uint32_t dim, uint8_t *out_row,
                       int32_t *work);
/* the shape model's CID: the CIDv1 raw sha2-256 of its definition text */
void alx_vec_shape_model(uint32_t dim, uint8_t cid[36]);

/* How many of a leaf's records are stale against the current model and
 * the layer's current glue (a record whose file the glue no longer has: its
 * bytes changed, so its CID did). All of them when the model differs. */
uint32_t alx_vec_stale(const alx_vec_t *v, const uint8_t model[36], const uint8_t *glue);

#endif
