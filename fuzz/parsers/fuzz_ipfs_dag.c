/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* fuzz_ipfs_dag.c — the IPFS block-level decoders in ipfs_node: dag-pb,
 * UnixFS Data, UnixFS directories (plain and HAMT), CAR v1 archives (with a
 * directory walk over the blocks the CAR carries) and the UBH-168 block
 * envelope.
 *
 * Byte 0 (mod 3) picks: 0 one block for the dag-pb / UnixFS / directory
 * parsers; 1 a CAR file; 2 a UBH-168 envelope (and the same bytes through
 * the negotiated wire decoder in both modes). Properties: every pointer a
 * parser hands back lies inside the input. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ipfs_node.h"
#include "ipfsn_dir.h"
#include "fuzz_in.h"

static const uint8_t *g_lo, *g_hi;

static void inside(const uint8_t *p, uint64_t n)
{
    if (n == 0) return;
    if (!p || p < g_lo || p > g_hi || n > (uint64_t) (g_hi - p)) abort();
}

static void block_parsers(const uint8_t *b, uint32_t len)
{
    ipfsn_pbnode_t pb;
    if (ipfsn_dagpb_parse(b, len, &pb) == IPFSN_OK) {
        inside(pb.data, pb.data_len);
        uint32_t pos = 0;
        ipfsn_pblink_t l;
        for (int i = 0; i < 1024 && ipfsn_dagpb_next_link(&pb, &pos, &l) == IPFSN_OK; i++)
            inside(l.name, l.name_len);
        if (pb.data) {
            ipfsn_unixfs_t u;
            if (ipfsn_unixfs_parse(pb.data, pb.data_len, &u) == IPFSN_OK) {
                inside(u.data, u.data_len);
                uint32_t bp = 0;
                uint64_t bs;
                for (int i = 0; i < 1024 && ipfsn_unixfs_next_blocksize(&u, &bp, &bs) == IPFSN_OK;
                     i++) {
                }
            }
        }
    }
    ipfsn_unixfs_t u2;
    if (ipfsn_unixfs_parse(b, len, &u2) == IPFSN_OK) inside(u2.data, u2.data_len);
    ipfsn_dirnode_t d;
    (void) ipfsn_dir_parse(b, len, &d);
}

static int count_cb(void *ctx, const ipfsn_dirent_t *e)
{
    (void) e;
    return ++*(uint32_t *) ctx > 4096u;
}

static uint8_t g_scratch[1u << 18];

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1 || size > (1u << 18)) return 0;
    uint8_t mode = data[0] % 3u;
    uint8_t *b = fz_dup(data + 1, size - 1);
    uint32_t len = (uint32_t) (size - 1);
    g_lo = b;
    g_hi = b + len;

    if (mode == 0) {
        block_parsers(b, len);
    } else if (mode == 1) {
        ipfsn_car_t car;
        if (ipfsn_car_open(&car, b, len) == IPFSN_OK) {
            ipfsn_cid_t cid;
            const uint8_t *blk;
            uint32_t bl;
            for (int i = 0; i < 512; i++) {
                int r = ipfsn_car_next(&car, &cid, &blk, &bl);
                if (r != IPFSN_OK && r != IPFSN_ERR_HASH) break;
                inside(blk, bl);
                if (r == IPFSN_OK) block_parsers(blk, bl);
            }
            if (car.nroots > 0) {
                static ipfsn_dir_walk_t w;
                uint32_t cnt = 0, emitted = 0;
                (void) ipfsn_dir_list(&w, &car.roots[0], ipfsn_car_source, &car, g_scratch,
                                      sizeof g_scratch, count_cb, &cnt, &emitted);
            }
        }
    } else {
        ipfsn_cid_t cid;
        (void) ipfsn_ubh_decode_cid(b, len, &cid);
        const uint8_t *blk = NULL;
        uint32_t bl = 0;
        if (ipfsn_ubh_decode_block(b, len, &cid, &blk, &bl) == IPFSN_OK) {
            inside(blk, bl);
            if (ipfsn_cid_verify(&cid, blk, bl) != IPFSN_OK) abort();
        }
        ipfsn_cid_t want;
        (void) ipfsn_cid_sha256(IPFSN_MC_RAW, b, len, &want);
        if (ipfsn_wire_decode_block(IPFSN_WIRE_PLAIN, &want, b, len, &blk, &bl) == IPFSN_OK)
            inside(blk, bl);
        if (ipfsn_wire_decode_block(IPFSN_WIRE_UBH168, &want, b, len, &blk, &bl) == IPFSN_OK)
            inside(blk, bl);
        (void) ipfsn_wire_negotiate(true, b, len);
    }
    free(b);
    return 0;
}
