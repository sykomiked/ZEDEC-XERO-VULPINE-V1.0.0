/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx_matrix.h — the 144-layer matrix (docs/spec/MATRIX.md).
 *
 * Layer 0 is the core and boots first; layer N is L1 system app N
 * (kernel/src/alexandria/MATRIX.tsv). Content is cut into chunks
 * (alx_cdc.h), each named by its own CID; chunks the glue binds are packed
 * together into booklets; each layer's booklets hang from a fan-out tree;
 * and one root lists the 145 layers. Four node kinds, all big-endian with
 * 64-byte headers, each small enough to be one raw block:
 *
 * ALXB booklet, version 2:
 *   header: "ALXB", 2, tier, u16 layer, u32 atoms, u32 length, 48 zero
 *   directory, 48 bytes per atom: CID (36), u32 offset, u32 length, u32 zero
 *   atoms, each at a 64-byte boundary, in directory order, zero between;
 *   the booklet ends where its last atom ends
 * ALXN fan-out node:
 *   header: "ALXN", 1, level, u16 layer, u32 children (1..256), u32 zero,
 *     u64 booklets beneath, 40 zero
 *   children's CIDs (36 each): booklets at level 0, nodes above. Every
 *   child but the last holds exactly 256^level booklets, so booklet k sits
 *   at digit (k >> 8*level) & 255 on each level: no search, depth
 *   log256(booklets).
 * ALXG glue, version 3, one per layer, empty layers too: the files that
 *   live in the layer, what each is made of, how the files bind each other,
 *   and where the layer sits on the spine
 *   header: "ALXG", 3, state (lpres: 0 N, an empty scaffold layer that
 *     carries nothing; 1 T, it holds content), u16 layer, u32 files,
 *     u32 edges, u32 refs, the glue CID (36) of the anchor the layer hangs
 *     from (zero for the core), u16 that anchor, u16 the pattern layer M of
 *     a branch layer F(k) + M (0 for an anchor), 4 zero
 *   files, 48 bytes each, CIDs strictly rising: the file's CID (36; raw
 *     sha2-256 of all its bytes), u32 first ref, u32 refs, u32 zero
 *   edges, 16 bytes each, rising by (from, to layer, to file): u32 from
 *     file, u16 to layer, u16 type, u32 to file (in that layer's glue),
 *     u16 weight (1..65535), u16 zero; an anchor's edges reach anchors only
 *   refs, 12 bytes each, the file's chunks in order: u16 layer, u16 zero,
 *     u32 booklet (in that layer), u32 atom (in that booklet). A chunk is
 *     stored once in the whole matrix, in the deepest layer of the lineage
 *     of every layer using it, so a ref names the layer itself or one it
 *     may draw on (alx_mx_draws). A T layer with no files holds only chunks
 *     its descendants share.
 *   for a branch layer, the pattern layer's glue CID (36)
 * ALXM root:
 *   header: "ALXM", 2, 0, u16 layers (145), u32 fan-out (256), 52 zero
 *   per layer, 112 bytes: tree root CID (36; zero when the layer holds no
 *     booklets), glue CID (36; every layer has one), icon CID (36; zero
 *     when none), u32 booklets
 *   then the SHA-256 of all of the above
 *
 * The spine and the frame (docs/spec/MATRIX.md sections 9 and 10): the
 * twelve layers whose numbers are Fibonacci numbers (0 1 2 3 5 8 13 21 34
 * 55 89 144) are the anchors, the prime dimensional layers. Every other
 * layer N is a branch, split one way only (Zeckendorf) as N = F(k) + M:
 * it hangs from anchor F(k) and repeats the pattern of layer M. A layer
 * draws chunks only from itself and its lineage: an anchor from the
 * anchors below it, a branch from the anchors up to F(k), M and M's
 * lineage. Each layer has a position on three nested star tetrahedra in
 * doubled E8 coordinates over Z[phi] (alx_mx_pos).
 *
 * Every CID here is CIDv1 raw sha2-256 of the named bytes. Nothing in a
 * node is code: a resolved atom is checked bytes handed to its caller.
 *
 * Freestanding C11: integers only, no allocation. */
#ifndef ZXV_ALX_MATRIX_H
#define ZXV_ALX_MATRIX_H

#include "alx.h"

#define ALX_MX_LAYERS 145u
#define ALX_MX_FAN    256u
#define ALX_MX_LAYER  112u
#define ALX_MX_DIR    48u
#define ALX_MX_FILE   48u
#define ALX_MX_REF    12u
#define ALX_MX_EDGE   16u
#define ALX_MX_DEPTH  4u /* 256^4 booklets per layer */

/* glue edge types */
enum {
    ALX_GLUE_INCLUDE = 1, /* source names a header */
    ALX_GLUE_TEST = 2,    /* a test exercises the file */
    ALX_GLUE_LOGIC = 3,   /* a manifest binds the file as logic */
    ALX_GLUE_SAME = 4     /* the same module: a .c and its .h */
};

typedef struct {
    const uint8_t *root, *glue, *icon; /* 36-byte CIDs inside the root */
    uint32_t booklets;
} alx_mx_layer_t;

/* a booklet: its form, the atoms' offsets, and each atom's CID */
int alx_bk2_check(const uint8_t *b, uint32_t len, uint32_t *layer, uint32_t *atoms);
/* atom i of a checked booklet */
void alx_bk2_atom(const uint8_t *b, uint32_t i, const uint8_t **cid, uint32_t *off, uint32_t *len);

/* a fan-out node's form; level and child count out */
int alx_node_check(const uint8_t *b, uint32_t len, uint32_t *level, uint32_t *n, uint64_t *beneath);
/* child i's CID */
const uint8_t *alx_node_child(const uint8_t *b, uint32_t i);
/* the depth of a layer's tree of n booklets (1 for 1..256) and the child
 * to take at a level for booklet k */
uint32_t alx_mx_depth(uint64_t n);
uint32_t alx_mx_digit(uint64_t k, uint32_t level);

/* the root's form and hash */
int alx_mx_check(const uint8_t *b, uint32_t len);
void alx_mx_layer(const uint8_t *b, uint32_t k, alx_mx_layer_t *l);

/* F's index i when layer k is the Fibonacci number F(i) (1 for layer 1),
 * else -1; and the anchor below layer k (0 for k <= 1) */
int alx_mx_anchor(uint32_t k);
uint32_t alx_mx_anchor_below(uint32_t k);
/* a layer's split: for an anchor, the anchor below it and pattern 0; for
 * a branch, the largest Fibonacci number at most k and the rest */
void alx_mx_split(uint32_t k, uint32_t *anchor, uint32_t *pattern);
/* whether layer k may draw chunks from layer l (l in k's lineage) */
bool alx_mx_draws(uint32_t k, uint32_t l);
/* layer k's position: eight doubled E8 coordinates, each p + q phi as
 * out[2j], out[2j+1]. Anchors sit on the front corners of three star
 * tetrahedra at scales 1, phi^4 and phi^8, the front alternating by star
 * (+ even sign parity, -, +: Seal I); a branch F(k) + M
 * sits at vertex(F(k)) - pos(M) / phi, exact in Z[phi]. */
int alx_mx_pos(uint32_t k, int64_t out[16]);
/* The two half-cycles. alx_mx_fill(i) is the layer at step i of the
 * negative half, core out: the build and fetch order (the core; then star
 * by star, its anchors and then its bands), in which every layer comes
 * after its whole lineage, so CIDs are computed in it. alx_mx_gather(i) is
 * the positive half, outermost in, the same order reversed: the sweep that
 * checks each layer and gathers its state into the anchors, closing at the
 * core. A hash chain cannot be sealed outer-in, since a glue node names
 * its ancestors' CIDs; the inward half checks, it does not hash. Both
 * return ALX_MX_LAYERS past the end. */
uint32_t alx_mx_fill(uint32_t i);
uint32_t alx_mx_gather(uint32_t i);

/* a glue node's form; counts out. Every ref's layer is below 145 and its
 * atom index is checked when the booklet is opened, not here. */
int alx_glue_check(const uint8_t *b, uint32_t len, uint32_t *layer, uint32_t *files,
                   uint32_t *edges, uint32_t *refs);
/* the glue CID of the anchor a checked glue node hangs from (0 for the
 * core) and that anchor's layer */
const uint8_t *alx_glue_anchor(const uint8_t *b, uint32_t *layer);
/* its pattern layer's glue CID (0 for an anchor) and that layer */
const uint8_t *alx_glue_pattern(const uint8_t *b, uint32_t len, uint32_t *layer);
/* its state: 0 N (empty scaffold), 1 T (holds content) */
int alx_glue_state(const uint8_t *b);
/* its verdict (byte 60), lpres as lpres_hex encodes it: T when it holds
 * content and its app passed its gate, N content not yet gated or an empty
 * scaffold, B a contradiction (content its gate fails, or a gate that passes
 * an app the layer does not hold); never F */
enum { ALX_VERDICT_F = 0, ALX_VERDICT_N = 1, ALX_VERDICT_B = 2, ALX_VERDICT_T = 3 };
int alx_glue_verdict(const uint8_t *b);
/* the file with this CID, or -1 */
int32_t alx_glue_find(const uint8_t *b, const uint8_t cid[36]);
/* file f's chunks: how many, and ref i of them */
uint32_t alx_glue_nrefs(const uint8_t *b, uint32_t f);
void alx_glue_ref(const uint8_t *b, uint32_t f, uint32_t i, uint32_t *layer, uint32_t *booklet,
                  uint32_t *atom);
/* What to fetch with file f: the distinct booklets of this layer that hold
 * the chunks of the files its edges reach, heaviest edge first, at most
 * cap, leaving out the booklets f itself needs; and how many of its edges
 * reach other layers. Returns the number written. */
uint32_t alx_glue_prefetch(const uint8_t *b, uint32_t f, uint32_t *out, uint32_t cap,
                           uint32_t *other_layers);

#endif
