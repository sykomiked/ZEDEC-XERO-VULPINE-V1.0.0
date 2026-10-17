/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cisex_idx.h — CISEX, the Crown Intelligence Service Exchange: the bounded
 * global IPFS index the 8-node armada carries entirely in RAM.
 *
 * WHAT IT IS
 * ----------
 * A shallow Merkle-DAG crawler plus an inverted keyword index plus a
 * paraconsistent provider-health map. The armada indexes the CONTENT
 * ADDRESS space — CIDs, filenames, child links, sizes, magic-byte kinds and
 * who serves what — never the bulk payload bytes. A dag-pb directory block
 * is fetched into a caller buffer, harvested for links, and dropped; the
 * entry it leaves behind is ~160 bytes on the wire it is a fixed 168-byte
 * UBH-168 envelope (CISEX_T_INDEX).
 *
 * DISCOVERY PATHS (all three land in the same table)
 *   1. Passive wantlist sniffing (cisex_want): a peer asking the DHT for a
 *      CID creates a NEITHER/POTENTIAL stub and bumps `wants` (demand).
 *   2. Shallow dag-pb harvesting (cisex_index_dagpb): one verified dag-pb
 *      block yields every child name/size/CID; each child is a citation
 *      (cites++), which is the EpistemicRank in-degree. The parent is marked
 *      `crawled` so re-scanning the same block never double-counts.
 *   3. First-chunk header peeking (cisex_peek): the first <= 4 KiB of a leaf
 *      is classified by magic bytes (PDF/ELF/Wasm/GGUF/CAR/SQLite/ZIP/TAR/
 *      gzip/images/text) and scored with a fixed-point Shannon entropy
 *      (x1024) that flags encrypted padding or junk.
 *
 * PARACONSISTENT PROVIDER HEALTH (8 regional probes)
 *   Each of the 8 armada regions reports lpres observations per CID
 *   (cisex_observe / cisex_provide). Regions kjoin their own observations;
 *   the entry's base is the kjoin across all 8 regions. TRUE means at least
 *   one region verified a clean fetch; BOTH means regions disagree (e.g. a
 *   block served in US-West but unreachable or truncated in IND — a real
 *   censorship/split-brain signal, never silently resolved); NEITHER means
 *   referenced but never seen served; FALSE means attested unavailable.
 *   The lpx phase walks the Gray cycle POTENTIAL -> EMERGING (first index or
 *   observation) -> MANIFEST (multi-region TRUE) -> DISSOLVING (fading), and
 *   polarity comes from the pos/neg evidence trend via lpx_polarity.
 *
 * EPISTEMICRANK (exact integers, no floats)
 *   rank = cites*13 + wants*8 + live_regions*5 + (joined == TRUE ? 21 : 0)
 *   DAG links are immutable cryptographic citations; demand and verified
 *   multi-region availability add to a CID's rank deterministically.
 *
 * MISSING-BLOCK BOUNTIES
 *   cisex_bounty lists entries that are cited (cites > 0) but not healthy:
 *   no verified provider, joined state not TRUE, or entropic (fading). Each
 *   is an automatic archival bounty — re-pin and verify over :8724 for
 *   Social Crown standing.
 *
 * Freestanding C11: integer only, no libc, no allocation (the caller owns
 * every table), no 64-bit division.
 */
#ifndef CISEX_IDX_H
#define CISEX_IDX_H

#include <stdbool.h>
#include <stdint.h>
#include "../ipfs_node/ipfs_node.h"
#include "../lpres/lpres.h"
#include "../lpres/lpres_hex.h"
#include "../ubh/ubh.h"

#define CISEX_ENV_BYTES 168u
#define CISEX_PAYLOAD   126u /* 6 * 21 octets, like vna_exchange */
#define CISEX_FRAME     21u
#define CISEX_SCHEMA    0x43495358u /* "CISX" */
#define CISEX_T_INDEX   3u          /* UBH target_type for an index record */

#define CISEX_REGIONS 8u
#define CISEX_PROV_MAX 4u
#define CISEX_NAME_MAX 32u
#define CISEX_KW_MAX   3u
#define CISEX_KW_MIN   3u
#define CISEX_KW_MAXTK 24u
#define CISEX_HDR_PEEK 4096u

/* Content kind: extension guess or magic-byte verdict (cisex_peek). */
typedef enum {
    CISEX_K_UNKNOWN = 0,
    CISEX_K_DIR = 1,
    CISEX_K_FILE = 2, /* dag-pb UnixFS file interior node */
    CISEX_K_PDF = 3,
    CISEX_K_ELF = 4,
    CISEX_K_WASM = 5,
    CISEX_K_CAR = 6,
    CISEX_K_GGUF = 7,
    CISEX_K_SQLITE = 8,
    CISEX_K_ZIP = 9,
    CISEX_K_GZ = 10,
    CISEX_K_TAR = 11,
    CISEX_K_IMG = 12,
    CISEX_K_MD = 13,
    CISEX_K_C = 14,
    CISEX_K_TEX = 15
} cisex_kind_t;

/* One provider observation: who (where) serves the CID, seen by `region`. */
typedef struct {
    uint8_t addr[6]; /* IPv4 (4B) + UDP port (2B BE) */
    uint8_t region;  /* 0..7: the armada probe that observed it */
    uint8_t state;   /* lpres_state_t */
} cisex_prov_t;

typedef struct {
    uint8_t used;
    uint8_t digest[32]; /* CIDv1 sha2-256 multihash digest (identity key) */
    uint8_t codec;      /* multicodec of the CID (raw, dag-pb, ...) */
    uint8_t kind;       /* cisex_kind_t */
    uint8_t crawled;    /* dag-pb block already harvested: cites counted */
    char name[CISEX_NAME_MAX];
    uint8_t name_len;
    uint64_t size;    /* subtree bytes (link Tsize) when known */
    uint32_t cites;   /* in-degree: distinct crawled DAGs linking this CID */
    uint32_t wants;   /* passive wantlist/demand count */
    uint64_t kw[CISEX_KW_MAX];
    uint8_t n_kw;
    lpx_t lpx;                       /* 64-state hexagram tag */
    uint8_t armada;                  /* mask of regions reporting TRUE */
    uint8_t rst[CISEX_REGIONS];      /* joined lpres per region */
    cisex_prov_t prov[CISEX_PROV_MAX];
    uint8_t n_prov;
    uint32_t pos_ev, neg_ev;
    uint16_t entropy_x1024; /* header-peek entropy, x1024 bits */
} cisex_ent_t;

typedef struct {
    cisex_ent_t *e;
    uint32_t cap;
    uint32_t count;
    uint32_t n_indexed; /* dag-pb blocks harvested */
    uint32_t n_wants;   /* passive wantlist records */
    uint32_t n_obs;     /* provider observations joined */
    uint32_t n_merged;  /* armada index records merged */
} cisex_t;

/* Decoded form of a 168-byte CISEX index envelope. */
typedef struct {
    uint8_t digest[32];
    uint8_t codec;
    uint8_t kind;
    uint8_t region; /* origin armada region */
    uint8_t armada; /* sender's TRUE mask */
    uint8_t lpx;
    uint8_t n_prov;
    uint8_t addr[6];
    uint64_t size;
    uint32_t cites;
    uint32_t wants;
    uint64_t kw[CISEX_KW_MAX];
    uint16_t entropy_x1024;
    char name[CISEX_NAME_MAX];
    uint8_t name_len;
} cisex_wire_t;

void cisex_init(cisex_t *x, cisex_ent_t *ents, uint32_t cap);
cisex_ent_t *cisex_find(const cisex_t *x, const uint8_t digest[32]);
const cisex_ent_t *cisex_at(const cisex_t *x, uint32_t idx);

/* Passive wantlist sniffing: record demand for a CID. Creates the
 * NEITHER/POTENTIAL stub when unknown. IPFSN_OK or IPFSN_ERR_FULL. */
int cisex_want(cisex_t *x, const uint8_t digest[32], uint8_t region);

/* Harvest one dag-pb block: verify it against `cid`, classify the node
 * (DIR/FILE), and index every link as a citation (child name, size,
 * keywords, cites++). Idempotent per block: a `crawled` parent is not
 * re-counted. *n_links receives the link count. IPFSN_OK / IPFSN_ERR_HASH /
 * IPFSN_ERR_MALFORMED / IPFSN_ERR_FULL. */
int cisex_index_dagpb(cisex_t *x, const ipfsn_cid_t *cid, const uint8_t *block, uint32_t len,
                      uint8_t region, uint32_t *n_links);

/* First-chunk classification: magic bytes -> kind (if not already known)
 * plus the fixed-point header entropy (x1024 bits) used to flag encrypted
 * noise. hdr may be up to CISEX_HDR_PEEK bytes. */
int cisex_peek(cisex_t *x, const uint8_t digest[32], const uint8_t *hdr, uint32_t len,
               uint8_t region, uint8_t *kind_out, uint16_t *entropy_out);

/* Join one regional lpres observation into the entry (NEITHER is a no-op).
 * Creates the stub when unknown. Recomputes the armada mask, the joined
 * base, the polarity from the evidence trend, and walks the phase. */
int cisex_observe(cisex_t *x, const uint8_t digest[32], uint8_t region, lpres_state_t st);

/* A verified provider record: registers the endpoint and joins a TRUE
 * observation for `region` (one positive evidence count). */
int cisex_provide(cisex_t *x, const uint8_t digest[32], uint8_t region, const uint8_t addr[6]);

/* EpistemicRank: exact-integer citation gravity + demand + verified
 * multi-region availability. */
uint64_t cisex_rank(const cisex_ent_t *e);

/* Keyword search: hash the (single, normalized) query token, return the
 * indices of matching entries ordered by rank descending. */
uint32_t cisex_search(const cisex_t *x, const char *kw, uint32_t kw_len, uint32_t *out_idx,
                      uint32_t cap);

/* Missing-block bounties: cited entries that are not healthy (no verified
 * provider, joined state not TRUE, or entropic polarity), ranked
 * descending. */
uint32_t cisex_bounty(const cisex_t *x, uint32_t *out_idx, uint32_t cap);

/* 168-byte UBH-168 index envelope (schema "CISX", type CISEX_T_INDEX) for
 * armada-to-armada sync over Vinea :8724. */
int cisex_pack_idx(const cisex_ent_t *e, uint8_t region, uint8_t out[CISEX_ENV_BYTES]);
int cisex_unpack_idx(const uint8_t in[CISEX_ENV_BYTES], cisex_wire_t *out);

/* Merge a peer's index record: every region the sender reports TRUE is
 * kjoin'ed TRUE locally, the sender's joined base is kjoin'ed into the
 * sender's region slot, scalar fields take the max (idempotent: merging
 * the same record twice changes nothing). */
int cisex_merge(cisex_t *x, const uint8_t in[CISEX_ENV_BYTES]);

/* Fixed-point Shannon entropy of d[0..n) in x1024 bits (linear-interpolated
 * integer log2, max ~0.086 bit error; a junk/encryption filter, not a
 * measurement). Uniform over 256 symbols gives 8192. */
uint16_t cisex_entropy_x1024(const uint8_t *d, uint32_t n);

/* FNV-1a 64 over the lowercased token — the keyword hash used for the
 * inverted index and wire records. */
uint64_t cisex_kw_hash(const char *tok, uint32_t len);

#endif /* CISEX_IDX_H */
