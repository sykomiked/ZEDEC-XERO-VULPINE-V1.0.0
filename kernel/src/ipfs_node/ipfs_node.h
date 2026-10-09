/* ipfs_node.h — a local IPFS node for ZXV: real CIDs, UnixFS, blockstore,
 * visibility-aware pins, an OS file index, verified network fetch, and the
 * UBH-168 wire syntax that ZXV nodes speak to each other.
 *
 * WHAT THIS IS
 * ------------
 * Real IPFS content identifiers, not the bare SHA-256 that src/ipfs/ calls a
 * "CID" (that module is unchanged). A CID made here is byte-for-byte what Kubo
 * makes: `ipfs add --cid-version=1` of the same bytes gives the same root CID
 * (fixed 256 KiB chunks, raw leaves, balanced DAG of at most 174 links per
 * dag-pb node). The tests check this against CIDs produced by Kubo 0.32.1.
 *
 * WHAT THIS IS NOT
 * ----------------
 * Not a libp2p peer. There is no libp2p, no Bitswap, no public IPFS DHT, and
 * no peer ID here. ZXV interoperates with the IPFS network through trustless
 * HTTP gateways (?format=raw / ?format=car) and through CIDs, which are the
 * same CIDs every IPFS implementation computes. Blocks from ZXV peers (the
 * Carracho economy) arrive through a function-pointer hook. Whatever the
 * source, every block is re-hashed against its CID before it is accepted.
 *
 * UBH-168
 * -------
 * Between ZXV nodes the default wire syntax is UBH-168 (src/ubh): a 21-octet
 * CONTENT_OBJECT header frame, the payload in 21-octet frames, and a 21-octet
 * INTEGRITY trailer. When the other side does not speak it (a gateway, Kubo,
 * any non-ZXV peer) the node falls back to plain IPFS bytes. The CID is
 * identical either way. The 168 bits are the FRAMING; hashes are never cut to
 * 168 bits (a 168-bit digest would give only 84-bit collision resistance).
 *
 * Freestanding C11: integer only, no libc, no allocation (every table and
 * buffer is supplied by the caller), no 64-bit division.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_IPFS_NODE_H
#define ZXV_IPFS_NODE_H

#include <stdint.h>
#include <stdbool.h>

/* ===== Results ============================================================ */

enum {
    IPFSN_OK = 0,
    IPFSN_ERR_ARG = -1,        /* bad argument (NULL, out of range)            */
    IPFSN_ERR_MALFORMED = -2,  /* input does not parse under the strict rules  */
    IPFSN_ERR_UNSUPP = -3,     /* well-formed but not supported (codec, hash)  */
    IPFSN_ERR_SPACE = -4,      /* caller buffer too small                       */
    IPFSN_ERR_HASH = -5,       /* bytes do not hash to the CID                  */
    IPFSN_ERR_NOTFOUND = -6,   /* no such block / pin / path                    */
    IPFSN_ERR_PRIVATE = -7,    /* refused: the CID is private                   */
    IPFSN_ERR_IO = -8,         /* host storage op failed                        */
    IPFSN_ERR_FULL = -9,       /* caller-provided table is full                 */
    IPFSN_ERR_CRYPTO = -10,    /* AEAD authentication failed                    */
    IPFSN_ERR_CONFLICT = -11,  /* visibility conflict (public vs private)       */
    IPFSN_ERR_TRANSPORT = -12, /* no transport bound, or the transport failed   */
    IPFSN_ERR_NOKEY = -13,     /* private operation without a node key          */
    IPFSN_ERR_DEPTH = -14      /* DAG deeper than IPFSN_DAG_MAX_DEPTH            */
};

/* ===== 1. Multiformats ==================================================== */

#define IPFSN_MH_IDENTITY 0x00u
#define IPFSN_MH_SHA2_256 0x12u
#define IPFSN_MC_RAW      0x55u
#define IPFSN_MC_DAG_PB   0x70u
#define IPFSN_MC_DAG_CBOR 0x71u /* only to read CAR headers */

#define IPFSN_VARINT_MAX  9u   /* multiformats unsigned-varint: <= 9 bytes, 63 bits */
#define IPFSN_DIGEST_MAX  64u  /* identity multihash payloads are capped here       */
#define IPFSN_CID_BIN_MAX 80u  /* version + codec + mh code + mh len + digest       */
#define IPFSN_CID_STR_MAX 140u /* "b" + base32 of IPFSN_CID_BIN_MAX + NUL           */

typedef struct {
    uint8_t version;    /* 0 or 1                                   */
    uint32_t codec;     /* multicodec; v0 is always dag-pb          */
    uint32_t mh_code;   /* sha2-256 or identity                     */
    uint8_t digest_len; /* sha2-256: always 32                      */
    uint8_t digest[IPFSN_DIGEST_MAX];
} ipfsn_cid_t;

/* Unsigned varint. put: bytes written, 0 if cap too small or v >= 2^63.
 * get: bytes consumed (>0) or IPFSN_ERR_MALFORMED (truncated, over 9 bytes,
 * or not minimally encoded). */
uint32_t ipfsn_varint_put(uint64_t v, uint8_t *out, uint32_t cap);
int ipfsn_varint_get(const uint8_t *in, uint32_t len, uint64_t *v);

/* Build a CIDv1 over `data` with sha2-256 (codec raw or dag-pb). */
int ipfsn_cid_sha256(uint32_t codec, const uint8_t *data, uint32_t len, ipfsn_cid_t *cid);
/* Build a CIDv1 with an identity multihash (the data IS the digest, <= 64). */
int ipfsn_cid_identity(uint32_t codec, const uint8_t *data, uint32_t len, ipfsn_cid_t *cid);
/* Check that `data` is what `cid` names. IPFSN_OK or IPFSN_ERR_HASH. */
int ipfsn_cid_verify(const ipfsn_cid_t *cid, const uint8_t *data, uint32_t len);
bool ipfsn_cid_equal(const ipfsn_cid_t *a, const ipfsn_cid_t *b);
/* v0 -> v1 (dag-pb, same multihash). A v1 CID is returned unchanged. */
void ipfsn_cid_to_v1(const ipfsn_cid_t *in, ipfsn_cid_t *out);

/* Binary form. encode: bytes written (>0) or negative. decode parses a CID
 * at the START of `in` and returns the bytes consumed (it may be followed by
 * other data, as in a CAR section); use decode_exact when it must fill `len`. */
int ipfsn_cid_encode(const ipfsn_cid_t *cid, uint8_t *out, uint32_t cap);
int ipfsn_cid_decode(const uint8_t *in, uint32_t len, ipfsn_cid_t *cid);
int ipfsn_cid_decode_exact(const uint8_t *in, uint32_t len, ipfsn_cid_t *cid);

/* String form. v1 -> multibase base32 lower ("b..."), v0 -> base58btc ("Qm...").
 * Returns string length (NUL written) or negative. parse accepts exactly those
 * two forms; anything else (uppercase, padding, other multibases, stray bits)
 * is rejected. */
int ipfsn_cid_to_string(const ipfsn_cid_t *cid, char *out, uint32_t cap);
int ipfsn_cid_parse(const char *s, uint32_t len, ipfsn_cid_t *cid);

/* Raw codecs, exposed for tests and for the UBH text form. */
int ipfsn_base32_encode(const uint8_t *in, uint32_t len, char *out, uint32_t cap);
int ipfsn_base32_decode(const char *in, uint32_t len, uint8_t *out, uint32_t cap);
int ipfsn_base58_encode(const uint8_t *in, uint32_t len, char *out, uint32_t cap);
int ipfsn_base58_decode(const char *in, uint32_t len, uint8_t *out, uint32_t cap);

/* ===== 2. UnixFS / dag-pb ================================================= */

#define IPFSN_UFS_CHUNK     262144u    /* Kubo default chunker: size-262144          */
#define IPFSN_UFS_LINKS     174u       /* Kubo default balanced-layout link limit    */
#define IPFSN_DAG_MAX_DEPTH 8u         /* 174^7 * 256 KiB: far beyond any disk       */
#define IPFSN_NODE_MAX      16384u     /* largest dag-pb node built or walked here   */
#define IPFSN_BLOCK_MAX     (2u << 20) /* largest block accepted from anywhere    */

#define IPFSN_UFS_RAW  0u
#define IPFSN_UFS_DIR  1u
#define IPFSN_UFS_FILE 2u

/* Where blocks go. Called once per block, children before parents; the
 * last call of a build is the root. Non-zero return aborts the build. */
typedef int (*ipfsn_block_sink_fn)(void *ctx, const ipfsn_cid_t *cid, const uint8_t *block,
                                   uint32_t len);

typedef struct {
    ipfsn_cid_t cid;
    uint64_t tsize; /* bytes of the whole sub-DAG (Kubo's Tsize)  */
    uint64_t fsize; /* file bytes under this link (blocksizes[i]) */
} ipfsn_ufs_link_t;

typedef struct {
    uint8_t *chunk; /* caller buffer of chunk_size bytes */
    uint32_t chunk_size;
    uint32_t fill;
    uint32_t max_links;
    ipfsn_block_sink_fn sink;
    void *sink_ctx;
    ipfsn_ufs_link_t level[IPFSN_DAG_MAX_DEPTH][IPFSN_UFS_LINKS];
    uint32_t nlinks[IPFSN_DAG_MAX_DEPTH];
    uint32_t top; /* highest level in use */
    uint64_t total;
    uint32_t leaves;
    int err; /* sticky */
    uint8_t node[IPFSN_NODE_MAX];
} ipfsn_ufs_t;

/* Streaming UnixFS file builder. chunk_size 0 -> 262144, max_links 0 -> 174.
 * Kubo-compatible root CIDs need the defaults; other values exist so tests
 * can exercise deep trees cheaply (Kubo's --chunker=size-N agrees). Memory is
 * bounded: the state struct plus one chunk buffer, whatever the file size. */
int ipfsn_ufs_init(ipfsn_ufs_t *u, uint8_t *chunk_buf, uint32_t chunk_size, uint32_t max_links,
                   ipfsn_block_sink_fn sink, void *sink_ctx);
int ipfsn_ufs_update(ipfsn_ufs_t *u, const uint8_t *data, uint32_t len);
/* Finish: emits the remaining blocks and writes the root CID. A file of at
 * most one chunk (including the empty file) is a single raw block. */
int ipfsn_ufs_final(ipfsn_ufs_t *u, ipfsn_cid_t *root, uint64_t *file_size);

/* dag-pb reader (strict: Links before Data, no duplicates, no unknown fields). */
typedef struct {
    const uint8_t *block;
    uint32_t len;
    const uint8_t *data; /* PBNode.Data, or NULL */
    uint32_t data_len;
    uint32_t nlinks;
    uint32_t links_end; /* offset where the Links region ends */
} ipfsn_pbnode_t;

typedef struct {
    ipfsn_cid_t cid;
    const uint8_t *name;
    uint32_t name_len;
    bool has_tsize;
    uint64_t tsize;
} ipfsn_pblink_t;

int ipfsn_dagpb_parse(const uint8_t *block, uint32_t len, ipfsn_pbnode_t *n);
/* Iterate links: *pos starts at 0; returns IPFSN_OK, or IPFSN_ERR_NOTFOUND at the end. */
int ipfsn_dagpb_next_link(const ipfsn_pbnode_t *n, uint32_t *pos, ipfsn_pblink_t *l);

/* UnixFS Data message (fields Type, Data, filesize, blocksizes; mode/mtime skipped). */
typedef struct {
    uint32_t type;
    const uint8_t *data;
    uint32_t data_len;
    bool has_filesize;
    uint64_t filesize;
    uint32_t nblocksizes;
    const uint8_t *msg; /* to iterate blocksizes */
    uint32_t msg_len;
} ipfsn_unixfs_t;

int ipfsn_unixfs_parse(const uint8_t *msg, uint32_t len, ipfsn_unixfs_t *u);
int ipfsn_unixfs_next_blocksize(const ipfsn_unixfs_t *u, uint32_t *pos, uint64_t *bs);

/* ===== DAG walk: reassemble a UnixFS file ================================== */

/* Fetch a block into buf. The walker re-hashes what it gets, so a source
 * that lies is caught here, not trusted. */
typedef int (*ipfsn_get_fn)(void *ctx, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap,
                            uint32_t *len);
typedef int (*ipfsn_write_fn)(void *ctx, const uint8_t *data, uint32_t len);

typedef struct {
    uint8_t node[IPFSN_NODE_MAX];
    ipfsn_pbnode_t pb;
    ipfsn_unixfs_t fs;
    uint32_t link_pos;
    uint32_t bs_pos;
    uint64_t start;       /* bytes emitted when this node began */
    uint64_t child_start; /* bytes emitted when the current child began */
    uint64_t child_expect;
} ipfsn_walk_frame_t;

typedef struct {
    ipfsn_walk_frame_t f[IPFSN_DAG_MAX_DEPTH];
    uint8_t *leaf; /* caller buffer for leaves; IPFSN_BLOCK_MAX is always enough */
    uint32_t leaf_cap;
} ipfsn_walk_t;

/* Stream the file named by `root` to `out`. Checks every block's hash, every
 * blocksizes[] entry and every filesize. */
int ipfsn_cat(ipfsn_walk_t *w, const ipfsn_cid_t *root, ipfsn_get_fn get, void *get_ctx,
              ipfsn_write_fn out, void *out_ctx, uint64_t *size);

/* ===== 3. Blockstore ====================================================== */

/* Host storage: one linear byte space, appended to. read/write return 0 on
 * success. Production backs this with a zxvfs file or a raw partition. */
typedef struct {
    void *ctx;
    int (*read)(void *ctx, uint64_t off, uint8_t *buf, uint32_t len);
    int (*write)(void *ctx, uint64_t off, const uint8_t *buf, uint32_t len);
    uint64_t (*size)(void *ctx);
} ipfsn_storage_ops_t;

#define IPFSN_VIS_PUBLIC  1u
#define IPFSN_VIS_PRIVATE 2u

#define IPFSN_KEY_LEN     32u
#define IPFSN_REC_HDR     12u
#define IPFSN_SEAL_OVER   (12u + 16u + 1u + IPFSN_CID_BIN_MAX) /* nonce, tag, cid */
#define IPFSN_SCRATCH_MIN (IPFSN_BLOCK_MAX + IPFSN_SEAL_OVER)

typedef struct {
    ipfsn_cid_t cid; /* key: plaintext CID, or private CID (SEALED_KEY) */
    uint64_t off;    /* record offset in storage */
    uint32_t len;    /* payload length */
    uint8_t flags;
    uint8_t used;
} ipfsn_bs_entry_t;

#define IPFSN_BSE_PRIVATE    0x01u /* record is sealed (encrypted at rest) */
#define IPFSN_BSE_SEALED_KEY 0x02u /* this entry's key is the private CID */

typedef struct {
    ipfsn_storage_ops_t ops;
    ipfsn_bs_entry_t *tab;
    uint32_t cap; /* power of two */
    uint32_t count;
    uint64_t end;
    uint8_t *scratch; /* >= IPFSN_SCRATCH_MIN for private blocks */
    uint32_t scratch_cap;
    uint8_t seal_key[IPFSN_KEY_LEN];  /* derived: ChaCha20-Poly1305 key   */
    uint8_t nonce_key[IPFSN_KEY_LEN]; /* derived: deterministic nonces    */
    bool have_key;
} ipfsn_bs_t;

int ipfsn_bs_init(ipfsn_bs_t *bs, const ipfsn_storage_ops_t *ops, ipfsn_bs_entry_t *table,
                  uint32_t cap_pow2, uint8_t *scratch, uint32_t scratch_cap);
/* The node key that seals PRIVATE blocks. The caller owns where it comes from
 * (sealed keystore, TPM, passphrase KDF) and must keep it secret. */
void ipfsn_bs_set_key(ipfsn_bs_t *bs, const uint8_t key[IPFSN_KEY_LEN]);
/* Rebuild the index from storage. Without the key, private blocks are
 * indexed only by their private CID. */
int ipfsn_bs_mount(ipfsn_bs_t *bs);
/* Store a block. The bytes are hashed first; a block that does not match its
 * CID is refused. PRIVATE blocks are sealed with ChaCha20-Poly1305; their
 * private CID (CIDv1 raw sha2-256 of the sealed record) goes to *pcid. */
int ipfsn_bs_put(ipfsn_bs_t *bs, const ipfsn_cid_t *cid, const uint8_t *block, uint32_t len,
                 uint32_t vis, ipfsn_cid_t *pcid);
/* Get by plaintext CID or private CID. Always re-hashes before returning. */
int ipfsn_bs_get(ipfsn_bs_t *bs, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len);
bool ipfsn_bs_has(ipfsn_bs_t *bs, const ipfsn_cid_t *cid);
/* IPFSN_VIS_PUBLIC / IPFSN_VIS_PRIVATE / IPFSN_ERR_NOTFOUND. */
int ipfsn_bs_visibility(ipfsn_bs_t *bs, const ipfsn_cid_t *cid);
/* Adapter: ipfsn_get_fn over a blockstore (ctx = ipfsn_bs_t *). */
int ipfsn_bs_source(void *ctx, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len);

/* In-memory storage for tests and RAM disks. */
typedef struct {
    uint8_t *buf;
    uint64_t cap;
    uint64_t len;
} ipfsn_memstore_t;
void ipfsn_memstore_ops(ipfsn_memstore_t *m, ipfsn_storage_ops_t *ops);

/* ===== 4. Pins, visibility, announcements ================================= */

typedef struct {
    ipfsn_cid_t cid;    /* plaintext root CID                                     */
    ipfsn_cid_t handle; /* what the OS and the economy see: cid, or the private CID */
    uint8_t vis;
    uint8_t private_cid; /* handle is the private CID */
    uint8_t used;
} ipfsn_pin_t;

/* Announce a public CID to the network/economy (gateway pinning service, the
 * Carracho provider table, ...). Supplied by the host. */
typedef struct {
    void *ctx;
    int (*announce)(void *ctx, const ipfsn_cid_t *cid);
} ipfsn_provider_ops_t;

/* Trustless gateway transport: the host performs the HTTPS GET of `url` with
 * the given Accept header and writes the body to buf. 0 on success. */
typedef struct {
    void *ctx;
    int (*https_get)(void *ctx, const char *url, const char *accept, uint8_t *buf, uint32_t cap,
                     uint32_t *len);
    const char *base; /* e.g. "https://trustless-gateway.link" (no trailing slash) */
} ipfsn_gateway_ops_t;

#define IPFSN_WIRE_PLAIN  0u /* plain IPFS block bytes */
#define IPFSN_WIRE_UBH168 1u /* UBH-168 framed (ZXV default) */

/* "Pull from peers": the Carracho layer supplies blocks. `cid_bin` is the
 * binary CID wanted; the peer writes either plain block bytes or a UBH-168
 * block envelope, per `wire` (the negotiated mode). Verified here. */
typedef struct {
    void *ctx;
    int (*want)(void *ctx, const uint8_t *cid_bin, uint32_t cid_len, uint8_t *buf, uint32_t cap,
                uint32_t *len);
    uint32_t wire;
} ipfsn_peer_ops_t;

typedef struct {
    ipfsn_bs_t *bs;
    ipfsn_pin_t *pins;
    uint32_t pin_cap;
    ipfsn_provider_ops_t prov;
    ipfsn_gateway_ops_t gw;
    ipfsn_peer_ops_t peer;
    uint8_t *net_buf; /* >= IPFSN_BLOCK_MAX + 64, for peer/gateway replies */
    uint32_t net_cap;
    uint32_t announced; /* count, for audit */
    bool local_only;    /* set by ipfsn_node_cat while walking a private DAG */
} ipfsn_node_t;

int ipfsn_node_init(ipfsn_node_t *n, ipfsn_bs_t *bs, ipfsn_pin_t *pins, uint32_t pin_cap,
                    uint8_t *net_buf, uint32_t net_cap);
/* Pin a root. Pinning a CID public that is pinned private (or the reverse)
 * is IPFSN_ERR_CONFLICT: making something public is a deliberate act. */
int ipfsn_pin(ipfsn_node_t *n, const ipfsn_cid_t *cid, uint32_t vis, const ipfsn_cid_t *handle);
int ipfsn_unpin(ipfsn_node_t *n, const ipfsn_cid_t *cid);
/* True if the CID (plaintext or private) is private anywhere in this node:
 * pinned private, a private CID, or held only as a sealed block. */
bool ipfsn_is_private(ipfsn_node_t *n, const ipfsn_cid_t *cid);
/* Announce one CID: IPFSN_ERR_PRIVATE for anything private, IPFSN_ERR_NOTFOUND
 * unless pinned public. */
int ipfsn_provide(ipfsn_node_t *n, const ipfsn_cid_t *cid);
/* Announce every public pin; returns how many were announced (or negative). */
int ipfsn_provide_all(ipfsn_node_t *n);
/* Serve a block to a peer in `wire` syntax. Refuses private blocks. */
int ipfsn_serve_block(ipfsn_node_t *n, const ipfsn_cid_t *cid, uint32_t wire, uint8_t *out,
                      uint32_t cap, uint32_t *len);

/* Add a whole buffer as a UnixFS file: blocks into the blockstore with `vis`,
 * root pinned. For PRIVATE with private_cid, *handle is the private CID of the
 * root block; otherwise it is the root CID. `u` and chunk_buf are scratch. */
int ipfsn_add(ipfsn_node_t *n, ipfsn_ufs_t *u, uint8_t *chunk_buf, const uint8_t *data,
              uint32_t len, uint32_t vis, bool private_cid, ipfsn_cid_t *root, ipfsn_cid_t *handle);

/* ===== 6. Network fetch ==================================================== */

#define IPFSN_FMT_RAW 0u
#define IPFSN_FMT_CAR 1u

/* "<base>/ipfs/<cid>?format=raw|car". Returns length or negative. */
int ipfsn_gw_url(const char *base, const ipfsn_cid_t *cid, uint32_t fmt, char *out, uint32_t cap);
/* One block via ?format=raw; verified, then stored PUBLIC. Refuses private CIDs
 * (a private CID is never sent to a gateway). */
int ipfsn_gw_fetch_block(ipfsn_node_t *n, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap,
                         uint32_t *len);
/* Whole DAG via ?format=car into `carbuf`; every block verified BEFORE any
 * is stored; the CAR must list `root`. *nblocks = blocks stored. */
int ipfsn_gw_fetch_car(ipfsn_node_t *n, const ipfsn_cid_t *root, uint8_t *carbuf, uint32_t cap,
                       uint32_t *nblocks);
/* One block from the peer hook; verified, stored PUBLIC. Refuses private CIDs. */
int ipfsn_peer_fetch(ipfsn_node_t *n, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap,
                     uint32_t *len);
/* ipfsn_get_fn (ctx = ipfsn_node_t *): local blockstore, then peers, then the
 * gateway. A child of a private file is not itself marked private, so prefer
 * ipfsn_node_cat, which keeps a private DAG's walk local. */
int ipfsn_node_source(void *ctx, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len);
/* ipfsn_cat through ipfsn_node_source. If `root` is private the whole walk is
 * local-only: a missing block is IPFSN_ERR_NOTFOUND, never a network request. */
int ipfsn_node_cat(ipfsn_node_t *n, ipfsn_walk_t *w, const ipfsn_cid_t *root, ipfsn_write_fn out,
                   void *out_ctx, uint64_t *size);

/* CAR v1 reader. */
typedef struct {
    const uint8_t *buf;
    uint32_t len;
    uint32_t first; /* first section */
    uint32_t pos;   /* next section */
    uint32_t nroots;
    ipfsn_cid_t roots[4];
} ipfsn_car_t;

int ipfsn_car_open(ipfsn_car_t *c, const uint8_t *buf, uint32_t len);
/* Next block, verified against its CID. IPFSN_ERR_NOTFOUND at the clean end;
 * IPFSN_ERR_HASH for a tampered block. */
int ipfsn_car_next(ipfsn_car_t *c, ipfsn_cid_t *cid, const uint8_t **block, uint32_t *len);
/* ipfsn_get_fn over a CAR held in memory (ctx = ipfsn_car_t *). */
int ipfsn_car_source(void *ctx, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len);
/* CAR v1 writer: header with one root. Sections appended with car_put. */
int ipfsn_car_begin(const ipfsn_cid_t *root, uint8_t *out, uint32_t cap);
int ipfsn_car_put(const ipfsn_cid_t *cid, const uint8_t *block, uint32_t len, uint8_t *out,
                  uint32_t cap);

/* ===== 5. File index for the OS ============================================ */

/* Paths are VFS paths (src/vfs: VFS_PATH_LEN 256 including the NUL), absolute
 * and canonical. zxvfs's flat 32-byte names appear under their VFS mount
 * point, e.g. "/zxvfs/notes.txt". */
#define IPFSN_PATH_MAX 256u

typedef struct {
    char path[IPFSN_PATH_MAX];
    uint32_t path_len;
    ipfsn_cid_t cid; /* the handle: plaintext CID, or private CID */
    uint64_t size;
    uint8_t vis;
    uint8_t used;
} ipfsn_fidx_entry_t;

typedef struct {
    ipfsn_fidx_entry_t *e;
    uint32_t cap;
    uint32_t count;
} ipfsn_fidx_t;

void ipfsn_fidx_init(ipfsn_fidx_t *x, ipfsn_fidx_entry_t *entries, uint32_t cap);
/* Map path -> cid (replacing an earlier mapping). The path must be absolute,
 * canonical (no "//", ".", "..", no trailing '/') and shorter than 256. */
int ipfsn_fidx_set(ipfsn_fidx_t *x, const char *path, const ipfsn_cid_t *cid, uint64_t size,
                   uint32_t vis);
int ipfsn_fidx_lookup(const ipfsn_fidx_t *x, const char *path, ipfsn_cid_t *cid, uint64_t *size,
                      uint32_t *vis);
int ipfsn_fidx_remove(ipfsn_fidx_t *x, const char *path);
/* cid -> paths: *iter starts at 0; returns IPFSN_OK with *path set, or
 * IPFSN_ERR_NOTFOUND when there are no more. */
int ipfsn_fidx_next_path(const ipfsn_fidx_t *x, const ipfsn_cid_t *cid, uint32_t *iter,
                         const char **path);
uint32_t ipfsn_fidx_refs(const ipfsn_fidx_t *x, const ipfsn_cid_t *cid);
uint32_t ipfsn_fidx_unique(const ipfsn_fidx_t *x);

/* ===== UBH-168 wire syntax ================================================= */

#define IPFSN_UBH_FRAME    21u
#define IPFSN_UBH_SCHEMA   0x49504653u /* "IPFS" */
#define IPFSN_UBH_T_CID    0x0001u
#define IPFSN_UBH_T_BLOCK  0x0002u
#define IPFSN_UBH_TEXT_PFX "ubh168:"

/* Envelope size for a payload of n bytes: header + ceil(n/21) frames + trailer. */
uint32_t ipfsn_ubh_size(uint32_t payload_len);
int ipfsn_ubh_encode_cid(const ipfsn_cid_t *cid, uint8_t *out, uint32_t cap, uint32_t *len);
int ipfsn_ubh_decode_cid(const uint8_t *in, uint32_t len, ipfsn_cid_t *cid);
int ipfsn_ubh_encode_block(const ipfsn_cid_t *cid, const uint8_t *block, uint32_t blen,
                           uint8_t *out, uint32_t cap, uint32_t *len);
/* Validates every frame, then verifies the block against the CID it carries.
 * *block points into `in` (the payload frames are contiguous). */
int ipfsn_ubh_decode_block(const uint8_t *in, uint32_t len, ipfsn_cid_t *cid, const uint8_t **block,
                           uint32_t *blen);
/* "ubh168:b<base32 of the UBH-168 CID envelope>". The ':' never occurs in a
 * multibase string, so this cannot be mistaken for a plain CID string. */
int ipfsn_ubh_cid_to_text(const ipfsn_cid_t *cid, char *out, uint32_t cap);
int ipfsn_ubh_cid_from_text(const char *s, uint32_t len, ipfsn_cid_t *cid);

/* Negotiation. A ZXV node opens with a 21-octet UBH-168 FORMAT_IDENTITY
 * frame. If the other side's opening is one, both use UBH-168; anything else
 * (no hello, an HTTP gateway, Kubo, a non-ZXV peer) means plain IPFS bytes. */
void ipfsn_wire_hello(uint8_t out[IPFSN_UBH_FRAME]);
uint32_t ipfsn_wire_negotiate(bool local_ubh, const uint8_t *peer_hello, uint32_t len);
int ipfsn_wire_encode_block(uint32_t wire, const ipfsn_cid_t *cid, const uint8_t *block,
                            uint32_t blen, uint8_t *out, uint32_t cap, uint32_t *len);
/* Decode per the negotiated mode and verify against `want`. The mode is never
 * sniffed from the bytes: plain block bytes may begin with anything. */
int ipfsn_wire_decode_block(uint32_t wire, const ipfsn_cid_t *want, const uint8_t *in, uint32_t len,
                            const uint8_t **block, uint32_t *blen);

#endif /* ZXV_IPFS_NODE_H */
