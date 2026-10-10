/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* ipfsn_dir.h — read UnixFS directories: plain dag-pb directories and
 * HAMT-sharded ones, the way Kubo writes them.
 *
 * WHAT THIS IS
 * ------------
 * A strict reader for the two directory forms `ipfs add -r` produces:
 *
 *  - PLAIN: a dag-pb node whose Data is UnixFS{Type=Directory} and whose
 *    Links are the entries (Hash, Name, Tsize), sorted by Name bytes.
 *  - HAMT-SHARDED (UnixFS Type=HAMTShard, what Kubo switches to once the
 *    directory's links pass ~256 KiB): Data carries a bitfield, hashType
 *    0x22 (murmur3-x64-64: the first 64 bits of MurmurHash3_x64_128, seed 0)
 *    and a fanout (256 in Kubo). Each link name starts with the slot number
 *    as fixed-width uppercase hex ("%02X" for 256). A name that is ONLY the
 *    prefix is a sub-shard; prefix + name is an entry. The slot at depth d is
 *    the d-th group of log2(fanout) bits of the hash, most significant first.
 *
 * Every block is re-hashed against its CID before it is parsed, so whatever
 * source supplies blocks (blockstore, gateway, peer, CAR) cannot lie.
 *
 * STRICT RULES (a node breaking any of them is IPFSN_ERR_MALFORMED)
 * -----------------------------------------------------------------
 *  - dag-pb as ipfsn_dagpb_parse checks it (Links before Data, field order);
 *  - UnixFS Data fields in ascending order, no duplicates, nothing a
 *    directory cannot have (no filesize, no blocksizes; a plain directory has
 *    no Data bytes, hashType or fanout); mode and mtime are allowed and skipped;
 *  - entry names are 1..255 bytes with no '/' and no NUL and are not "." or
 *    "..". Plain-directory links are strictly ascending by Name bytes (which
 *    also rules out duplicates) and carry a Tsize;
 *  - HAMT: hashType must be 0x22, fanout a power of two in 8..1024 (boxo's
 *    limit), the bitfield at most fanout/8 bytes, one link per set bit in
 *    slot order, each prefix exactly the slot number in uppercase hex, a
 *    sub-shard must be dag-pb with the same fanout and at least one link, and
 *    when listing, every entry's name must hash to the slots it was found
 *    under (an entry filed under the wrong slot is a forged node).
 *
 * HONEST LIMITS
 * -------------
 * Reading only: there is no directory builder here (the tests carry a small
 * one, test-only, checked against Kubo's root CIDs). Only hashType 0x22.
 * Symlinks, mode and mtime are not interpreted. Names are not checked to be
 * UTF-8 (Kubo does not require it either). Sorting is checked for plain
 * directories only; HAMT order is the slot order. Leading zero bytes in a
 * HAMT bitfield are accepted although Kubo strips them. Directory listing is
 * bounded by the caller's scratch: each level of a sharded walk keeps its
 * block in the scratch until that level is done.
 *
 * Freestanding C11: integer only, no libc, no allocation, no 64-bit division.
 */
#ifndef ZXV_IPFSN_DIR_H
#define ZXV_IPFSN_DIR_H

#include <stdint.h>
#include <stdbool.h>
#include "ipfs_node.h"

#define IPFSN_UFS_HAMT         5u
#define IPFSN_HAMT_MURMUR3     0x22u
#define IPFSN_HAMT_FANOUT_MIN  8u
#define IPFSN_HAMT_FANOUT_MAX  1024u
#define IPFSN_HAMT_MAX_LEVELS  22u /* 64 hash bits / 3 bits (fanout 8), plus one */
#define IPFSN_DIR_NAME_MAX     255u
#define IPFSN_DIR_SCRATCH_HINT (IPFSN_BLOCK_MAX + 8u * 131072u) /* root + 8 shard levels */

/* One parsed directory node (plain or one HAMT shard). Points into the block. */
typedef struct {
    uint32_t kind; /* IPFSN_UFS_DIR or IPFSN_UFS_HAMT */
    ipfsn_pbnode_t pb;
    uint32_t fanout;  /* HAMT: table size */
    uint32_t bits;    /* HAMT: log2(fanout) */
    uint32_t padlen;  /* HAMT: hex digits in a link prefix */
    uint32_t entries; /* links that are entries (not sub-shards) */
    uint32_t shards;  /* links that are sub-shards */
} ipfsn_dirnode_t;

typedef struct {
    ipfsn_cid_t cid;
    const uint8_t *name; /* NOT NUL-terminated; valid only as documented per call */
    uint32_t name_len;
    uint64_t tsize;
} ipfsn_dirent_t;

/* Parse and fully validate one node. IPFSN_ERR_UNSUPP if it is valid UnixFS
 * but not a directory (a file, a symlink). */
int ipfsn_dir_parse(const uint8_t *block, uint32_t len, ipfsn_dirnode_t *d);

/* Called once per entry. e->name points into the walk scratch and is valid
 * only during the call. Non-zero stops the walk and is returned. */
typedef int (*ipfsn_dirent_fn)(void *ctx, const ipfsn_dirent_t *e);

typedef struct {
    const uint8_t *block;
    uint32_t len;
    ipfsn_dirnode_t node;
    uint32_t pos;  /* next link offset */
    uint32_t slot; /* slot this shard sits in under its parent */
} ipfsn_dir_frame_t;

typedef struct {
    ipfsn_dir_frame_t f[IPFSN_HAMT_MAX_LEVELS];
} ipfsn_dir_walk_t;

/* Every entry of the directory `root`, in node order (plain: by name; HAMT:
 * by slot). Blocks come from `get` and are verified against their CIDs.
 * `scratch` holds the blocks along the current path (a stack); cap must fit
 * the root plus one shard per level. *count = entries emitted. */
int ipfsn_dir_list(ipfsn_dir_walk_t *w, const ipfsn_cid_t *root, ipfsn_get_fn get, void *gctx,
                   uint8_t *scratch, uint32_t cap, ipfsn_dirent_fn cb, void *cbctx,
                   uint32_t *count);

/* Look one name up. For a HAMT only the shards on the name's hash path are
 * fetched. On IPFSN_OK, out->name/out->name_len are the caller's `name`.
 * IPFSN_ERR_NOTFOUND when absent. `buf` holds one block at a time. */
int ipfsn_dir_lookup(const ipfsn_cid_t *root, ipfsn_get_fn get, void *gctx, const uint8_t *name,
                     uint32_t name_len, uint8_t *buf, uint32_t cap, ipfsn_dirent_t *out);

/* Resolve a relative path "a/b/c" (no leading or trailing '/', no empty, "."
 * or ".." components) one directory at a time. */
int ipfsn_dir_resolve(const ipfsn_cid_t *root, ipfsn_get_fn get, void *gctx, const char *path,
                      uint32_t path_len, uint8_t *buf, uint32_t cap, ipfsn_dirent_t *out);

/* murmur3-x64-64 as go-unixfs uses it: h1 of MurmurHash3_x64_128, seed 0. The
 * HAMT reads the 8 bytes big-endian, so bit 63 is the first bit consumed. */
uint64_t ipfsn_murmur3_64(const uint8_t *data, uint32_t len);

/* The entry-name rule shared by both forms (also used by zx_upcheck). */
bool ipfsn_dir_name_ok(const uint8_t *name, uint32_t len);

#endif /* ZXV_IPFSN_DIR_H */
