/* fab.h — file fabrication: an artifact rebuilt locally from a recipe of
 * public content-addressed blocks plus an optional private diff.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 *
 * WHAT IT IS
 * ----------
 * A recipe names public blocks by CID (Alexandria booklets, model blocks,
 * any raw or dag-pb block up to 2 MiB) and a list of operations that copy
 * byte ranges out of them, in target order, interleaved with literal bytes
 * from a diff. The recipient fetches the blocks (ipfs_node, each verified
 * against its CID), applies the operations into RAM, and accepts the result
 * only if its UnixFS CID (Kubo defaults, CIDv1, raw leaves) equals the
 * target CID the recipe names. Nothing is released before that check.
 *
 * Two modes:
 *   PUBLIC  plan and diff in clear, signed by the publisher. App installs:
 *           an app is its public blocks plus a little glue, nothing secret.
 *   SEALED  plan and diff encrypted to one recipient: ML-KEM-1024 to a fresh
 *           shared secret, HKDF-SHA256 to one key for the plan and one for
 *           the diff, ChaCha20-Poly1305 for each. Private user data.
 * Both are signed with ML-DSA-87 by the sender over everything before the
 * public key. Elliptic-curve keys are not accepted anywhere.
 *
 * Two kinds:
 *   FILE    one artifact.
 *   STREAM  the open recipe of a stream (always SEALED): it fabricates frame
 *           0 and gives both ends a stream key. Each later frame is a small
 *           AEAD record under that key (fab_frame_*) whose plan may also copy
 *           from the previous frame (PREV). Signed checkpoints over a window
 *           of frame CIDs let a viewer detect frames forged by another key
 *           holder (fab_checkpoint_*).
 *
 * WIRE FORMAT (big-endian)
 * ------------------------
 * Recipe:
 *   0   "FABR"
 *   4   version 1
 *   5   kind: 0 FILE, 1 STREAM
 *   6   mode: 0 PUBLIC, 1 SEALED
 *   7   zero
 *   8   sender id    SHA-256 of the sender's ML-DSA-87 public key
 *   40  recipient id SHA-256 of the recipient's ML-KEM-1024 key; zero if PUBLIC
 *   72  plan length
 *   76  diff length
 *   80  ML-KEM-1024 ciphertext (1568)                     SEALED only
 *       plan (plan length) [+ Poly1305 tag (16) if SEALED]
 *       diff (diff length) [+ Poly1305 tag (16) if SEALED]
 *       sender ML-DSA-87 public key (2592)
 *       ML-DSA-87 signature (4627) over SHA-256(every byte before the key),
 *       context "zxv.fabrication.recipe.v1"
 *
 * Plan (the same layout in recipes and frames):
 *   0   target CID (36: CIDv1, sha2-256, raw or dag-pb)
 *   36  target length (u64)
 *   44  block count (<= FAB_MAX_BLOCKS)
 *   48  op count (<= FAB_MAX_OPS)
 *   52  frame period in microseconds: > 0 in a stream open, else 0
 *   56  zero (8)
 *   64  blocks: CID (36) + length (u32), no CID twice
 *       ops:    kind (1) zero (1) block (u16) source offset (u32) length (u32)
 * Ops are applied in order and fill the target with no gap or overlap:
 *   SLICE  bytes [src, src+len) of block `block`
 *   LIT    the next `len` bytes of the diff (block and src zero)
 *   PREV   bytes [src, src+len) of the previous frame (frames only)
 * The op lengths sum to the target length and the LIT lengths to the diff
 * length. A listed block that no op uses is a decoy: fetched and verified
 * like the rest, never copied (see docs/spec/FABRICATION.md on what decoys
 * do and do not hide).
 *
 * Frame:
 *   0   "FABF"   4 version 1   5 zero (3)
 *   8   stream id (16)
 *   24  sequence (u64, strictly rising, frame 0 is the open recipe)
 *   32  base sequence (u64): the frame PREV ops copy from
 *   40  slot (u64): presentation time in frame periods
 *   48  plan length   52 diff length
 *   56  plan + tag (16), diff + tag (16)
 *
 * KEYS
 * ----
 *   prk     = HKDF-Extract(salt = SHA-256(header || KEM ciphertext), ss)
 *   plan    = HKDF-Expand(prk, "zxv.fab.v1 plan", 32)     nonce 0, AAD header
 *   diff    = HKDF-Expand(prk, "zxv.fab.v1 diff", 32)     nonce 0, AAD header
 *   stream  = HKDF-Expand(prk, "zxv.fab.v1 stream", 32)
 *   id      = first 16 bytes of HKDF-Expand(prk, "zxv.fab.v1 stream id", 32)
 * Each recipe key encrypts exactly one message, so a fixed nonce is safe.
 * Frame nonces are 0x01 (plan) or 0x02 (diff), three zero bytes, then the
 * sequence; a receiver refuses any sequence it has already passed.
 *
 * AIR-GAP ORDER
 * -------------
 * fab_open_plan uses the recipient key only to learn which blocks to fetch.
 * fab_hydrate fetches and verifies all of them. fab_assemble then asks the
 * seal callback and proceeds only on LPRES_STATE_TRUE (BOTH and NEITHER
 * refuse), and only then decapsulates again for the diff key. So the diff
 * key and the plaintext exist only after the node reports itself sealed.
 *
 * Integer only, no libc, no allocation: every buffer is the caller's.
 * A fabricated artifact is data. Nothing here maps it executable; code
 * enters only through verified-slot admission like any other file.
 */
#ifndef ZXV_FAB_H
#define ZXV_FAB_H

#include <stdbool.h>
#include <stdint.h>

#include "../ipfs_node/ipfs_node.h"
#include "../lpres/lpres.h"

#define FAB_VERSION 1u

#define FAB_KIND_FILE   0u
#define FAB_KIND_STREAM 1u

#define FAB_MODE_PUBLIC 0u
#define FAB_MODE_SEALED 1u

#define FAB_HDR    80u
#define FAB_FHDR   56u
#define FAB_ID     32u
#define FAB_SID    16u
#define FAB_CID    36u
#define FAB_TAG    16u
#define FAB_KEY    32u
#define FAB_KEM_EK 1568u
#define FAB_KEM_DK 3168u
#define FAB_KEM_CT 1568u
#define FAB_SIG_PK 2592u
#define FAB_SIG_SK 4896u
#define FAB_SIG    4627u

#define FAB_PLAN_HDR   64u
#define FAB_BLOCK_REC  40u
#define FAB_OP_REC     12u
#define FAB_MAX_BLOCKS 4096u
#define FAB_MAX_OPS    (1u << 20)
#define FAB_BLOCK_MAX  IPFSN_BLOCK_MAX

#define FAB_OP_SLICE 1u
#define FAB_OP_LIT   2u
#define FAB_OP_PREV  3u

/* What a plan belongs to; decides which fields and ops are legal. */
#define FAB_PLAN_FILE  0u
#define FAB_PLAN_OPEN  1u
#define FAB_PLAN_FRAME 2u

/* Checkpoint: "FABC" 1 zero(3) id(16) from(u64) to(u64) digest(32), then
 * the sender's ML-DSA-87 signature over those 72 bytes. digest is SHA-256
 * over (sequence u64 || frame CID 36) for every frame from..to. */
#define FAB_CP_BODY    72u
#define FAB_CP_LEN     (FAB_CP_BODY + FAB_SIG)
#define FAB_CHAIN_RING 128u

#define FAB_RECIPE_CTX "zxv.fabrication.recipe.v1"
#define FAB_CP_CTX     "zxv.fabrication.checkpoint.v1"

enum {
    FAB_OK = 0,
    FAB_ERR_ARG = -1,     /* bad argument                                       */
    FAB_ERR_FORM = -2,    /* recipe or frame does not parse under the rules     */
    FAB_ERR_TRUST = -3,   /* sender is not one the caller trusts                */
    FAB_ERR_SIGN = -4,    /* signature does not verify                          */
    FAB_ERR_KEY = -5,     /* recipient key is not the one this recipe names     */
    FAB_ERR_AUTH = -6,    /* AEAD tag failed: tampered, or the wrong key        */
    FAB_ERR_PLAN = -7,    /* plan breaks a rule (sizes, ranges, sums, CIDs)     */
    FAB_ERR_SPACE = -8,   /* a caller buffer is too small                       */
    FAB_ERR_FETCH = -9,   /* the block source could not supply a block          */
    FAB_ERR_BLOCK = -10,  /* a block's bytes are not what its CID names         */
    FAB_ERR_TARGET = -11, /* assembled bytes are not the target CID: discarded  */
    FAB_ERR_SEAL = -12,   /* air-gap seal is not TRUE                           */
    FAB_ERR_REPLAY = -13, /* frame sequence already passed                      */
    FAB_ERR_RESYNC = -14, /* PREV frame names a base this receiver does not hold */
    FAB_ERR_STREAM = -15  /* wrong stream, or a checkpoint contradicts the chain */
};

/* A parsed recipe: views into the caller's buffer. */
typedef struct {
    const uint8_t *buf;
    uint32_t len;
    uint8_t kind;
    uint8_t mode;
    const uint8_t *sender_id;
    const uint8_t *recipient_id;
    uint32_t plan_len;
    uint32_t diff_len;
    const uint8_t *kem_ct; /* NULL when PUBLIC */
    const uint8_t *plan;
    const uint8_t *plan_tag; /* NULL when PUBLIC */
    const uint8_t *diff;
    const uint8_t *diff_tag; /* NULL when PUBLIC */
    const uint8_t *sender_pk;
    const uint8_t *sig;
    uint32_t signed_len; /* bytes the signature covers */
    bool checked;        /* set by fab_recipe_check; required to open */
} fab_recipe_t;

/* A parsed plan: views into a plaintext plan buffer. */
typedef struct {
    ipfsn_cid_t target;
    uint64_t target_len;
    uint32_t n_blocks;
    uint32_t n_ops;
    uint32_t slot_us;
    uint8_t which; /* FAB_PLAN_* */
    uint64_t lit_total;
    uint64_t prev_need; /* highest byte a PREV op reads, + 1 */
    const uint8_t *blocks;
    const uint8_t *ops;
} fab_plan_t;

/* Workspace: the UnixFS builder and one block. Large; give it static or
 * heap storage, never the stack of a kernel thread. */
typedef struct {
    ipfsn_ufs_t ufs;
    uint8_t chunk[IPFSN_UFS_CHUNK];
    uint8_t block[FAB_BLOCK_MAX];
    int32_t loaded;                      /* block index in `block`, -1 none */
    const uint8_t *view[FAB_MAX_BLOCKS]; /* blocks viewed and verified in this call */
} fab_work_t;

/* Zero-copy block source: point at bytes the caller holds in RAM (a
 * hydrated blockstore, a mapped disc booklet) for the rest of the call. Each
 * viewed block is verified against its CID once per call and then read in
 * place, so the caller must not change those bytes until the call returns. */
typedef int (*fab_view_fn)(void *ctx, const ipfsn_cid_t *cid, const uint8_t **data, uint32_t *len);

/* Air-gap seal: TRUE only when the node has no live network path. */
typedef lpres_state_t (*fab_seal_fn)(void *ctx);

typedef struct {
    ipfsn_get_fn get; /* block source: ipfsn_bs_source over a blockstore, or a fetcher */
    void *get_ctx;
    fab_view_fn view; /* when set, used instead of get */
    void *view_ctx;
    fab_seal_fn seal; /* NULL: no air-gap requirement */
    void *seal_ctx;
    uint8_t *diff_buf; /* receives the plaintext diff */
    uint32_t diff_cap;
    uint8_t *out; /* receives the artifact or frame */
    uint32_t out_cap;
    fab_work_t *work;
} fab_io_t;

/* Stream state, one per end. The receiver fills it from the open recipe
 * (fab_assemble); the sender from fab_recipe_seal. */
typedef struct {
    uint8_t id[FAB_SID];
    uint8_t key[FAB_KEY];
    uint8_t sender_pk[FAB_SIG_PK];
    uint32_t slot_us;
    uint64_t seq;                      /* last frame sealed or accepted */
    uint64_t prev_seq;                 /* the frame now held as PREV source */
    uint64_t ring_seq[FAB_CHAIN_RING]; /* UINT64_MAX: empty */
    uint8_t ring_cid[FAB_CHAIN_RING][FAB_CID];
    uint64_t checked_seq; /* highest sequence a checkpoint confirmed */
    bool live;
} fab_stream_t;

/* ---- receiving ---------------------------------------------------------- */

/* FORM: header, sizes and layout only. */
int fab_recipe_parse(const uint8_t *buf, uint32_t len, fab_recipe_t *r);
/* TRUST then SIGN: the sender id must be in `trusted` (n may be 0 only if
 * trusted is NULL, which accepts any sender whose key matches its id) and
 * the signature must verify. */
int fab_recipe_check(fab_recipe_t *r, const uint8_t (*trusted)[FAB_ID], uint32_t n_trusted);
/* Recover and parse the plan into plan_buf (>= r->plan_len). dk is the
 * recipient's ML-KEM-1024 decapsulation key (SEALED) or NULL (PUBLIC).
 * Only the plan key is derived; it is wiped before returning. */
int fab_open_plan(const fab_recipe_t *r, const uint8_t *dk, uint8_t *plan_buf, uint32_t cap,
                  fab_plan_t *p);
/* Plan rules (above). diff_len is the diff the plan must consume. */
int fab_plan_parse(const uint8_t *plan, uint32_t len, uint8_t which, uint32_t diff_len,
                   fab_plan_t *p);
int fab_plan_block(const fab_plan_t *p, uint32_t i, ipfsn_cid_t *cid, uint32_t *len);
/* Fetch and verify every listed block, decoys included, through io->get. */
int fab_hydrate(const fab_plan_t *p, fab_io_t *io, uint32_t *fetched);
/* Seal check, diff key, diff, ops, target check. FILE: s must be NULL.
 * STREAM: s receives the stream state and *out_len is frame 0. On any
 * failure out and diff_buf are wiped. */
int fab_assemble(const fab_recipe_t *r, const fab_plan_t *p, const uint8_t *dk, fab_io_t *io,
                 fab_stream_t *s, uint32_t *out_len);

/* One frame. prev holds frame s->prev_seq (prev_len bytes) and must not
 * alias io->out. plan_buf receives the frame's plan (>= its plan length).
 * On success the caller makes io->out the next prev. */
int fab_frame_apply(fab_stream_t *s, const uint8_t *frame, uint32_t len, const uint8_t *prev,
                    uint32_t prev_len, uint8_t *plan_buf, uint32_t plan_cap, fab_io_t *io,
                    uint32_t *out_len);
/* FAB_ERR_SIGN unless the stream sender signed it. Then, over the frames
 * this receiver holds: FAB_OK when they match (checked_seq advances),
 * FAB_ERR_STREAM when they contradict it (a key holder forged frames),
 * FAB_ERR_RESYNC when any frame in the window is missing here (lost or
 * aged out of the ring), which proves nothing either way. */
int fab_checkpoint_verify(fab_stream_t *s, const uint8_t *cp, uint32_t len);

/* ---- sending ------------------------------------------------------------ */

/* The UnixFS CID of `data` (Kubo defaults, CIDv1, raw leaves). */
int fab_target_cid(const uint8_t *data, uint32_t len, fab_work_t *w, ipfsn_cid_t *cid);

/* A source for the builder: a public block (prev = false) or the previous
 * frame (prev = true; cid unused). */
typedef struct {
    const uint8_t *data;
    uint32_t len;
    ipfsn_cid_t cid;
    bool prev;
} fab_src_t;

/* Hash table entry for the builder (caller array of 1 << bits entries). */
typedef struct {
    uint32_t hash;
    uint32_t off;
    uint16_t src;
    uint16_t used;
} fab_ient_t;

#define FAB_WINDOW_MIN 8u

/* Find runs of target bytes that occur in the sources (windows of `window`
 * bytes, aligned in the sources, extended both ways) and emit ops; the rest
 * becomes diff. Only the last source may be prev, so op block numbers are
 * source indexes. ops_cap is in ops (12 bytes each). 4 <= table_bits <= 24. */
int fab_build(const fab_src_t *src, uint32_t nsrc, const uint8_t *target, uint32_t tlen,
              uint32_t window, fab_ient_t *table, uint32_t table_bits, uint8_t *ops,
              uint32_t ops_cap, uint32_t *n_ops, uint8_t *diff, uint32_t diff_cap,
              uint32_t *diff_len);
/* Plan bytes: every non-prev source is listed as a block, in order. */
int fab_plan_write(const ipfsn_cid_t *target, uint64_t tlen, uint32_t slot_us, const fab_src_t *src,
                   uint32_t nsrc, const uint8_t *ops, uint32_t n_ops, uint8_t *out, uint32_t cap,
                   uint32_t *len);

typedef struct {
    uint8_t kind;
    uint8_t mode;
    const uint8_t *sender_pk;    /* FAB_SIG_PK */
    const uint8_t *sender_sk;    /* FAB_SIG_SK */
    const uint8_t *recipient_ek; /* FAB_KEM_EK, SEALED only */
    const uint8_t *kem_coins;    /* 32 fresh random bytes, SEALED only */
    const uint8_t *sig_rnd;      /* 32 fresh random bytes, or NULL (deterministic) */
} fab_seal_args_t;

/* Write a signed recipe. STREAM: s receives the sender's stream state and
 * the plan's target is taken as frame 0 for the chain. */
int fab_recipe_seal(const fab_seal_args_t *a, const uint8_t *plan, uint32_t plan_len,
                    const uint8_t *diff, uint32_t diff_len, fab_stream_t *s, uint8_t *out,
                    uint32_t cap, uint32_t *len);
/* Seal frame s->seq + 1 built on frame `base`. */
int fab_frame_seal(fab_stream_t *s, uint64_t base, uint64_t slot, const uint8_t *plan,
                   uint32_t plan_len, const uint8_t *diff, uint32_t diff_len, uint8_t *out,
                   uint32_t cap, uint32_t *len);
/* Sign frames from..to (all still in the ring, to - from < FAB_CHAIN_RING). */
int fab_checkpoint_sign(const fab_stream_t *s, uint64_t from, uint64_t to, const uint8_t *sender_sk,
                        const uint8_t *rnd, uint8_t out[FAB_CP_LEN]);

/* Key helpers: ids are SHA-256 of the public key. */
void fab_sig_keypair(const uint8_t seed[32], uint8_t pk[FAB_SIG_PK], uint8_t sk[FAB_SIG_SK]);
void fab_kem_keypair(const uint8_t d[32], const uint8_t z[32], uint8_t ek[FAB_KEM_EK],
                     uint8_t dk[FAB_KEM_DK]);
void fab_key_id(const uint8_t *pk, uint32_t len, uint8_t id[FAB_ID]);

#endif /* ZXV_FAB_H */
