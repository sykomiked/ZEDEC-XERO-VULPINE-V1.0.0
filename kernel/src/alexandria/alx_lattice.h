/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx_lattice.h — lattice geometry: one coordinate for every place in the
 * matrix, one resolver for every store (docs/spec/MATRIX.md section 8).
 *
 * A matrix word is a 64-bit grid word in domain 0x30, big-endian fields:
 *   63..56 domain 0x30
 *   55..48 layer, 0..144
 *   47..24 booklet + 1 (0: the layer itself)
 *   23..0  atom + 1    (0: the whole booklet)
 * so (layer, booklet, atom) is exactly a glue ref, and a word names a layer,
 * a booklet or one chunk.
 *
 * alx_lat_resolve turns a word into a CID and checked bytes by walking the
 * root, the layer's fan-out tree and the booklet, asking a fetch function
 * for each block by CID and checking every block it is given against that
 * CID. The runtime fetches from its block store or peers; a browser maps an
 * image into WASM memory; bare metal reads an image's sectors. All three
 * call this same resolver with a different fetch function, so all three
 * reach the same bytes for the same word, which the tests check. A block
 * that is missing or does not match is reported with its CID, so the caller
 * can fetch that exact block from somewhere else; nothing is guessed.
 *
 * ALXI image, big-endian (one file or partition holding a matrix's blocks):
 *   header, 64 bytes: "ALXI", 1, 0, u16 zero, u32 blocks (n), u32 sector
 *     size (4096), root CID (36), 12 zero
 *   table at 64: n entries of 48 bytes, in rising CID order: CID (36),
 *     u32 first sector, u32 length, u32 zero
 *   blocks: each at its first sector, in table order, never overlapping
 *
 * Freestanding C11: integers only, no allocation. */
#ifndef ZXV_ALX_LATTICE_H
#define ZXV_ALX_LATTICE_H

#include "alx.h"

#define ALX_G_MATRIX   0x30u
#define ALX_LAT_MAXIDX 0xFFFFFEu /* the largest booklet or atom index a word holds */
#define ALX_IMG_SECTOR 4096u
#define ALX_IMG_ENTRY  48u

enum { ALX_LAT_LAYER = 1, ALX_LAT_BOOKLET = 2, ALX_LAT_ATOM = 3 };

/* Words for a layer, a booklet of it, and an atom of that booklet; 0 for an
 * index out of range. */
uint64_t alx_lat_layer(uint32_t layer);
uint64_t alx_lat_booklet(uint32_t layer, uint32_t booklet);
uint64_t alx_lat_atom(uint32_t layer, uint32_t booklet, uint32_t atom);
/* A word's kind (ALX_LAT_LAYER, _BOOKLET or _ATOM) and fields, or
 * ALX_E_DOMAIN for another domain, ALX_E_FORM for a malformed word. */
int alx_lat_parse(uint64_t w, uint32_t *layer, uint32_t *booklet, uint32_t *atom);

/* The block named by cid, or 0 when this store does not have it. The
 * resolver checks whatever it is given. */
typedef const uint8_t *(*alx_lat_fetch_fn)(void *ctx, const uint8_t cid[36], uint32_t *len);

typedef struct {
    uint32_t kind, layer, booklet, atom;
    uint8_t cid[36];      /* what the word names (a layer: its tree's root
                             node; a booklet: its stored block; an atom: its
                             chunk); on a failure, the block that failed */
    const uint8_t *bytes; /* the checked bytes: a layer's top node, a plain
                             booklet, or the atom */
    uint32_t len;
} alx_lat_hit_t;

/* Resolve w under a root (root_cid, when given, is checked against the
 * root's bytes). scratch: room for one plain booklet (1 MiB), used when a
 * booklet is stored compressed. Returns ALX_OK, or ALX_E_FORM / DOMAIN for
 * the word, ALX_E_NAME for a place that does not exist (an empty layer, a
 * booklet or atom past the end) or a block the store does not have,
 * ALX_E_HASH for a block that is not the one its CID names, ALX_E_LIMIT for
 * too little scratch. On NAME for a missing block and on HASH, h->cid holds
 * that block's CID. */
int alx_lat_resolve(const uint8_t *root, uint32_t root_len, const uint8_t *root_cid, uint64_t w,
                    alx_lat_fetch_fn fetch, void *ctx, uint8_t *scratch, uint32_t scratch_len,
                    alx_lat_hit_t *h);

typedef struct {
    const uint8_t *b;
    uint64_t len;
    uint32_t n, sector;
} alx_img_t;

/* An image's form: the header, the table in rising CID order, every block
 * inside the image in table order with no overlap. It does not hash the
 * blocks; the resolver checks each one it fetches. */
int alx_img_open(alx_img_t *im, const uint8_t *b, uint64_t len);
/* the root CID the image names */
const uint8_t *alx_img_root(const alx_img_t *im);
/* an alx_lat_fetch_fn over an open image (ctx: the alx_img_t) */
const uint8_t *alx_img_fetch(void *ctx, const uint8_t cid[36], uint32_t *len);

#endif
