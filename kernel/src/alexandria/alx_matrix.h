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
 * ALXG glue, one per layer: the files that live in the layer, what each is
 *   made of, and how the files bind each other
 *   header: "ALXG", 1, 0, u16 layer, u32 files, u32 edges, u32 refs, 44 zero
 *   files, 48 bytes each, CIDs strictly rising: the file's CID (36; raw
 *     sha2-256 of all its bytes), u32 first ref, u32 refs, u32 zero
 *   edges, 16 bytes each, rising by (from, to layer, to file): u32 from
 *     file, u16 to layer, u16 type, u32 to file (in that layer's glue),
 *     u16 weight (1..65535), u16 zero
 *   refs, 12 bytes each, the file's chunks in order: u16 layer, u16 zero,
 *     u32 booklet (in that layer), u32 atom (in that booklet). A chunk is
 *     stored once in the whole matrix, so a ref may name another layer.
 * ALXM root:
 *   header: "ALXM", 1, 0, u16 layers (145), u32 fan-out (256), 52 zero
 *   per layer, 112 bytes: tree root CID (36; zero for an empty layer),
 *     glue CID (36), icon CID (36; zero when none), u32 booklets
 *   then the SHA-256 of all of the above
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

/* a glue node's form; counts out. Every ref's layer is below 145 and its
 * atom index is checked when the booklet is opened, not here. */
int alx_glue_check(const uint8_t *b, uint32_t len, uint32_t *layer, uint32_t *files,
                   uint32_t *edges, uint32_t *refs);
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
