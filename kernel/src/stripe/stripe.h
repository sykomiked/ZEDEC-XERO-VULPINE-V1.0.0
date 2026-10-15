/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* stripe.h — the erasure-coded storage service (servers spec, section 6).
 *
 * A STRIPE SET is k data members and m parity members, each an hw_blk_t
 * (kernel/src/hwdrv/hwdrv.h). A member may be a local drive or any block
 * device a transport provides for a drive on another server; the service only
 * sees hw_blk_t. Member i holds piece i of every stripe: pieces 0..k-1 are the
 * data verbatim, pieces k..k+m-1 are parity. The code is freight's systematic
 * Reed-Solomon code over GF(2^8) (kernel/src/freight/freight.h): parity piece
 * r (k <= r < k + m) is sum_j freight_gen_coef(k, r, j) * data piece j, a
 * Cauchy code, so ANY k intact pieces rebuild the stripe and up to m members
 * may be lost or corrupt. Guildhall's two stripes of 16 + 4 are two sets.
 *
 * LAYOUT. Every member is cut into the same number of SLOTS. A slot is
 * hdr_blocks header blocks followed by piece_blocks piece blocks:
 *   lba(slot s) = s * slot_blocks, piece at lba + hdr_blocks.
 * The slot header is the same on every member (little-endian):
 *   0  "ZXST"           4  version, k, m, 0
 *   8  set id (u64)     16 generation (u64)
 *   24 logical id (u32) 28 payload length (u32)
 *   32 piece_blocks     36 slot index
 *   40 SHA-256 of each of the k + m pieces
 *   then the SHA-256 of everything before it, so a torn header is invalid.
 *
 * CRASH CONSISTENCY. Writes are log-structured. A logical stripe is never
 * overwritten in place: a write reserves a slot no live stripe uses, encodes
 * and hashes, invalidates that slot's old headers, writes every piece, and
 * only then writes the header on every member. Mount reads every header copy;
 * for each logical id the highest generation with a valid header wins. A power
 * cut before the first header lands leaves the previous generation live; one
 * after it means every piece already landed. Generations only grow, and mount
 * resumes one past the highest it saw.
 *
 * INTEGRITY. A piece whose SHA-256 differs from its header is an erasure, the
 * same as a member that is gone, so a corrupt piece is rebuilt, never returned.
 * Scrub verifies every live slot on every member and rewrites bad pieces and
 * header copies from parity; rebuild fills a replacement member.
 *
 * RULES. Freestanding C11: no libc, no malloc, no floating point. Tables have
 * fixed capacities (STRIPE_MAX_*); the piece buffers are a workspace the
 * caller supplies (stripe_work_bytes). Order of every write: check bounds,
 * reserve a slot, compute, write pieces, commit the header. */
#ifndef ZXV_STRIPE_H
#define ZXV_STRIPE_H

#include <stdint.h>

#include "hwdrv.h"

#define STRIPE_MAX_MEMBERS 32   /* k + m */
#define STRIPE_MAX_SLOTS   4096 /* slots per member */
#define STRIPE_MIN_BLOCK   16   /* smallest member block size, bytes */
#define STRIPE_MAX_BLOCK   65536
#define STRIPE_MAX_PIECE   (1u << 24) /* bytes per piece */
#define STRIPE_HASH        32         /* SHA-256 */
#define STRIPE_VERSION     1
#define STRIPE_NONE        0xffffffffu

/* Header bytes for n members: fixed fields, n piece hashes, the self hash. */
#define STRIPE_HDR_FIXED    40u
#define STRIPE_HDR_BYTES(n) (STRIPE_HDR_FIXED + STRIPE_HASH * (uint32_t) (n) + STRIPE_HASH)

enum {
    STRIPE_OK = 0,
    STRIPE_DEGRADED = 1,      /* done, but a member is missing or a piece was rebuilt */
    STRIPE_ERR_ARG = -1,      /* bad argument */
    STRIPE_ERR_SPACE = -2,    /* workspace or output buffer too small, or no free slot */
    STRIPE_ERR_GEOM = -3,     /* members short, mismatched, or over capacity */
    STRIPE_ERR_NOENT = -4,    /* logical stripe never written */
    STRIPE_ERR_TOO_FEW = -5,  /* more than m pieces lost or corrupt */
    STRIPE_ERR_IO = -6,       /* the write could not commit; the old generation stands */
    STRIPE_ERR_STATE = -7,    /* not mounted */
    STRIPE_ERR_SINGULAR = -8, /* cannot happen for a Cauchy code; defensive */
};

typedef struct {
    uint64_t set_id;       /* names the set; headers of another set are ignored */
    uint8_t k, m;          /* k >= 1, m >= 1, k + m <= STRIPE_MAX_MEMBERS */
    uint32_t piece_blocks; /* member blocks per piece, >= 1 */
    uint32_t slots;        /* slots per member; 0 = as many as the smallest member holds */
    uint32_t logical;      /* logical stripes, 1 .. slots - 1 (one slot spare for writes) */
} stripe_cfg_t;

/* What a read, scrub or rebuild found. */
typedef struct {
    uint32_t slots;         /* live slots examined */
    uint32_t missing;       /* pieces on members that are gone or failed I/O */
    uint32_t corrupt;       /* pieces whose SHA-256 did not match */
    uint32_t repaired;      /* pieces rewritten from parity */
    uint32_t hdr_bad;       /* header copies invalid or stale */
    uint32_t hdr_repaired;  /* header copies rewritten */
    uint32_t unrecoverable; /* slots with more than m bad pieces */
} stripe_report_t;

typedef struct {
    hw_blk_t *mem[STRIPE_MAX_MEMBERS];
    uint8_t failed[STRIPE_MAX_MEMBERS]; /* 1 once a member's I/O failed */
    stripe_cfg_t cfg;
    uint32_t n;           /* k + m */
    uint32_t block_size;  /* shared by every member */
    uint32_t hdr_bytes;   /* STRIPE_HDR_BYTES(n) */
    uint32_t hdr_blocks;  /* blocks the header occupies */
    uint32_t slot_blocks; /* hdr_blocks + piece_blocks */
    uint32_t piece_bytes; /* piece_blocks * block_size */
    uint32_t slots;
    uint32_t cursor; /* next slot the log tries */
    uint8_t mounted;
    uint64_t next_gen;
    uint8_t *pieces; /* n * piece_bytes, from the workspace */
    uint8_t *hdrbuf; /* hdr_blocks * block_size, from the workspace */
    uint32_t lid_slot[STRIPE_MAX_SLOTS];
    uint64_t lid_gen[STRIPE_MAX_SLOTS];
    uint32_t lid_len[STRIPE_MAX_SLOTS];
    uint32_t slot_lid[STRIPE_MAX_SLOTS]; /* the live logical id in a slot, or STRIPE_NONE */
    uint8_t hash[STRIPE_MAX_MEMBERS][STRIPE_HASH];           /* the header being written or read */
    uint8_t mat[STRIPE_MAX_MEMBERS][2 * STRIPE_MAX_MEMBERS]; /* Gauss-Jordan [A | I] */
} stripe_t;

/* Workspace bytes stripe_init needs for cfg on members of block_size bytes;
 * 0 when cfg or block_size is out of range. */
uint64_t stripe_work_bytes(const stripe_cfg_t *cfg, uint32_t block_size);

/* Bind n = k + m members and the workspace. Checks every bound (members
 * present with read and write, one block size, each member long enough for
 * the slots) and reads nothing. */
int stripe_init(stripe_t *s, const stripe_cfg_t *cfg, hw_blk_t *const *members, uint32_t n,
                uint8_t *work, uint64_t work_len);
/* Invalidate every slot header on every member: an empty set, mounted. */
int stripe_format(stripe_t *s);
/* Read every header copy and pick, per logical id, the highest complete
 * generation. Members whose reads fail are marked failed. */
int stripe_mount(stripe_t *s);

/* Write data[0..len) (len <= k * piece_bytes, zero padded) as the next
 * generation of logical stripe lid. OK when every member took it, DEGRADED
 * when at least k pieces and one header landed, ERR_TOO_FEW (nothing written)
 * when fewer than k members are present, ERR_IO when it could not commit; then
 * the previous generation is still the one read. */
int stripe_write(stripe_t *s, uint32_t lid, const uint8_t *data, uint32_t len);
/* Read the live generation of lid into out (cap bytes); *len is its length.
 * Rebuilds up to m missing or corrupt pieces; writes nothing. rep may be
 * NULL. OK when every piece was intact, DEGRADED when some were rebuilt. */
int stripe_read(stripe_t *s, uint32_t lid, uint8_t *out, uint32_t cap, uint32_t *len,
                stripe_report_t *rep);
/* Verify every live slot on every present member; rewrite bad pieces and
 * header copies from parity. OK when all was clean, DEGRADED when something
 * was missing or repaired, ERR_TOO_FEW when a slot had more than m bad pieces
 * (the scrub still visits every slot). rep may be NULL. */
int stripe_scrub(stripe_t *s, stripe_report_t *rep);
/* Put `replacement` in place of member i and write its piece and header of
 * every live slot (and invalidate its other slots). The replacement must have
 * the set's block size and enough blocks. */
int stripe_rebuild(stripe_t *s, uint32_t member, hw_blk_t *replacement, stripe_report_t *rep);

/* Slot holding the live generation of lid, or STRIPE_NONE. */
uint32_t stripe_slot_of(const stripe_t *s, uint32_t lid);
/* Generation of the live lid, 0 when none. */
uint64_t stripe_gen_of(const stripe_t *s, uint32_t lid);
/* Members currently marked failed. */
uint32_t stripe_failed_count(const stripe_t *s);

/* ---- the six server codes (servers spec, section 6) ---- */
typedef struct {
    const char *name;
    uint8_t k, m;
    uint8_t nodes;   /* servers the pieces of one stripe spread across */
    uint8_t stripes; /* stripe sets per model (Guildhall runs two) */
} stripe_profile_t;

uint32_t stripe_profile_count(void);
const stripe_profile_t *stripe_profile(uint32_t i); /* NULL past the end */
const stripe_profile_t *stripe_profile_find(const char *name);
/* Server holding member i: members are dealt to nodes in equal runs. */
uint32_t stripe_profile_node(const stripe_profile_t *p, uint32_t member);
/* Fill cfg from a profile. */
int stripe_profile_cfg(const stripe_profile_t *p, uint64_t set_id, uint32_t piece_blocks,
                       uint32_t slots, uint32_t logical, stripe_cfg_t *cfg);

#endif /* ZXV_STRIPE_H */
