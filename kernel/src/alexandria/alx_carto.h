/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx_carto.h — the cartographer: index nodes that map 64-bit grid words
 * to content (docs/spec/CARTOGRAPHY.md).
 *
 * A CORE index ships sealed with the library (ALEXANDRIA.IDX, written by
 * tools/alexandria/build_index.py): one entry per block manifest and per
 * language, giving the target's own CID, the booklet that holds it and its
 * offset and length there. An OVERLAY is a cartographer's signed claim
 * about content outside the library (datasets, archives, weights): it maps
 * grid words in one division to external CIDs, names the index it extends
 * (its parent anchor), and is signed with ML-DSA-65 (tools/zxv-index).
 *
 * Both are the same file format, big-endian:
 *   header, 64 bytes: "ALXI", version 1, kind (0 core, 1 overlay),
 *     u16 division (overlay: the division every entry lies in; core: 0),
 *     u32 entries, u32 zero, parent CID (36 bytes), 12 zero bytes
 *   entries, 96 bytes each, words strictly rising:
 *     0 u64 grid word   8 target CID (36)   44 u32 sector (LBA; 0: fetch by
 *     CID)   48 u32 length   52 u32 offset in the container
 *     56 container CID (36; zero when the target stands alone)   92 u32 zero
 *   core: the SHA-256 of all of the above, 32 bytes
 *   overlay: the cartographer's public key (1952 bytes) and an ML-DSA-65
 *     signature (3309 bytes) over the SHA-256 of everything before the key,
 *     with the context "zxv.alexandria.index.v1"
 *
 * Rules: a core index holds only code words (domain 0x10, extension 0). An
 * overlay holds no code: executable content enters only as a verified slot
 * of the sealed library, never through a cartographer's claim. Every
 * overlay entry is a claim, trusted provisionally and demoted on contrary
 * evidence (alx_carto_demote): the good-nature rule. Two mounted overlays
 * that give one word different targets are BOTH, and neither is used.
 *
 * Mounting checks the format, the signature, that the cartographer's key is
 * one the operator trusts, and that the parent anchor is the core index or
 * an overlay already mounted. Finding overlays on the network and deciding
 * which keys to trust are the node's (zxvd, evo_comm), not this file's.
 *
 * Freestanding C11: integers only, no allocation; uses sha256() and the
 * caller's signature check. */
#ifndef ZXV_ALX_CARTO_H
#define ZXV_ALX_CARTO_H

#include "alx.h"

#define ALX_IDX_HEAD    64u
#define ALX_IDX_ENTRY   96u
#define ALX_IDX_PK      1952u
#define ALX_IDX_SIG     3309u
#define ALX_IDX_MAX     10000u /* entries: an index fits one 1 MiB block */
#define ALX_CARTO_MOUNT 16u    /* overlays mounted at once */
#define ALX_IDX_CTX     "zxv.alexandria.index.v1"

enum { ALX_IDX_CORE = 0, ALX_IDX_OVERLAY = 1 };
/* how far a hit is trusted */
enum { ALX_T_SEALED = 1, ALX_T_CLAIM = 2, ALX_T_DEMOTED = 3 };

enum {
    ALX_E_BOTH = -13,  /* two overlays give one word different targets */
    ALX_E_TRUST = -14, /* the cartographer's key is not one the operator trusts */
    ALX_E_SIGN = -15   /* the signature does not verify */
};

/* the caller's ML-DSA-65 check (alx_carto_mldsa.c binds pq_security) */
typedef bool (*alx_verify_fn)(void *ctx, const uint8_t pk[ALX_IDX_PK], const uint8_t *msg,
                              uint32_t len, const uint8_t sig[ALX_IDX_SIG]);

typedef struct {
    const uint8_t *b; /* the index bytes; the caller keeps them */
    uint32_t n;       /* entries */
    uint8_t cid[36];  /* this index's CID (CIDv1 raw sha2-256 of all its bytes) */
    uint32_t key;     /* overlay: which trusted key signed it */
} alx_idx_t;

typedef struct {
    alx_idx_t core;
    alx_idx_t over[ALX_CARTO_MOUNT];
    uint32_t nover;
    uint8_t (*demoted)[36]; /* targets with contrary evidence: the caller's array */
    uint32_t ndemoted, cap_demoted;
} alx_carto_t;

typedef struct {
    uint64_t word;
    uint8_t target[36], container[36];
    uint32_t lba, len, off;
    uint32_t trust; /* ALX_T_* */
    uint32_t from;  /* 0 the core, k overlay k - 1 */
} alx_carto_hit_t;

/* Open a core index (and forget any mounted overlays). It must list every
 * manifest and language of the library g was built from, each word giving
 * exactly the booklet, offset and length the library gives, and each
 * manifest's target CID matching its content id; so a core index can never
 * point anywhere the sealed library does not. */
int alx_carto_core(alx_carto_t *c, const alx_grid_t *g, const uint8_t *b, uint32_t len);
/* An overlay's form and signature, whoever signed it (for tools that
 * build or inspect overlays; mounting also asks whether the key is trusted
 * and where the overlay hangs). */
int alx_carto_overlay_check(const uint8_t *b, uint32_t len, alx_verify_fn verify, void *vctx);
/* Mount an overlay signed by one of the n trusted keys. */
int alx_carto_mount(alx_carto_t *c, const uint8_t *b, uint32_t len,
                    const uint8_t (*keys)[ALX_IDX_PK], uint32_t nkeys, alx_verify_fn verify,
                    void *vctx);
/* The demoted list: storage for cap CIDs, kept by pointer and emptied. */
void alx_carto_demoted(alx_carto_t *c, uint8_t (*store)[36], uint32_t cap);
/* Record contrary evidence about an overlay's target (it failed its check
 * when fetched, or a policy refused it). Sealed core targets are never
 * demoted (FORM). ALX_OK, or ALX_E_LIMIT when the list is full. */
int alx_carto_demote(alx_carto_t *c, const uint8_t cid[36]);
/* A word to its target: the core first, then the overlays. */
int alx_carto_resolve(const alx_carto_t *c, uint64_t word, alx_carto_hit_t *h);
/* The CID of index bytes (CIDv1 raw sha2-256). */
void alx_carto_cid(const uint8_t *b, uint32_t len, uint8_t cid[36]);

#endif
