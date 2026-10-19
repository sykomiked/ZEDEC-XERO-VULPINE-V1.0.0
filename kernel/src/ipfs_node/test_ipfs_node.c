/* test_ipfs_node.c — host test for src/ipfs_node.
 *
 * Build and run (from kernel/):
 *
 *   export CPATH=$PWD/include:$PWD/src/modbind:$PWD/src/e8:$PWD/src/event_space:$PWD/src/surplus
 *   gcc -std=c11 -Wall -Werror -Wextra -O2 -DTEST_HOST -Isrc/ipfs_node \
 *       src/ipfs_node/test_ipfs_node.c src/ipfs_node/ipfsn_multiformats.c \
 *       src/ipfs_node/ipfsn_unixfs.c src/ipfs_node/ipfsn_store.c src/ipfs_node/ipfsn_net.c \
 *       src/ipfs_node/ipfsn_ubh.c src/ipfs_node/ipfsn_fidx.c \
 *       src/robin_debanks/sha256.c src/tls/aead.c src/tls/hkdf.c \
 *       src/ubh/ubh.c src/event_space/event_envelope.c \
 *       -o /tmp/test_ipfs_node && /tmp/test_ipfs_node
 *
 * (Add -fsanitize=address,undefined -g for the sanitizer run.)
 *
 * WHERE THE EXPECTED CIDS COME FROM
 *   Every root CID below was produced by Kubo 0.32.1 (the official
 *   kubo_v0.32.1_linux-amd64 release binary from github.com/ipfs/kubo,
 *   sha512-checked against the published .sha512) with
 *       ipfs add -Q --offline --cid-version=1 [--chunker=size-N] FILE
 *   over files written by the same xorshift32 generator as gen_fill() below.
 *   The CIDv0 -> v1 pair is `ipfs add` (v0 default) and `ipfs cid base32`.
 *   KUBO_SMALL_CAR is the byte-exact output of `ipfs dag export` for the
 *   size-64 vector. "hello world\n" also checks against its SHA-256
 *   (a948904f...a447, coreutils sha256sum).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ipfs_node.h"
#include "../robin_debanks/sha256.h"

static int failures = 0, passes = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s:%d %s\n", __FILE__, __LINE__, m);                                    \
            failures++;                                                                            \
        } else {                                                                                   \
            passes++;                                                                              \
        }                                                                                          \
    } while (0)

/* ---- deterministic data: xorshift32, 4 bytes LE per step ---------------- */

typedef struct {
    uint32_t x, k;
} gen_t;

static void gen_init(gen_t *g, uint32_t seed)
{
    g->x = seed ? seed : 1;
    g->k = 4;
}

static void gen_fill(gen_t *g, uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (g->k == 4) {
            g->x ^= g->x << 13;
            g->x ^= g->x >> 17;
            g->x ^= g->x << 5;
            g->k = 0;
        }
        b[i] = (uint8_t) (g->x >> (8 * g->k));
        g->k++;
    }
}

static const uint8_t KUBO_SMALL_CAR[] = {
    0x3a, 0xa2, 0x65, 0x72, 0x6f, 0x6f, 0x74, 0x73, 0x81, 0xd8, 0x2a, 0x58, 0x25, 0x00, 0x01, 0x70,
    0x12, 0x20, 0xc9, 0xf1, 0x1b, 0x84, 0x66, 0xa2, 0xdb, 0x33, 0x50, 0xd5, 0x93, 0x69, 0xfb, 0xd6,
    0xe9, 0x1e, 0x45, 0x40, 0xd6, 0x18, 0xa5, 0x94, 0x3d, 0xb0, 0x8f, 0xb0, 0x3e, 0xd4, 0x9d, 0xf6,
    0x32, 0x6f, 0x67, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e, 0x01, 0xe3, 0x01, 0x01, 0x70, 0x12,
    0x20, 0xc9, 0xf1, 0x1b, 0x84, 0x66, 0xa2, 0xdb, 0x33, 0x50, 0xd5, 0x93, 0x69, 0xfb, 0xd6, 0xe9,
    0x1e, 0x45, 0x40, 0xd6, 0x18, 0xa5, 0x94, 0x3d, 0xb0, 0x8f, 0xb0, 0x3e, 0xd4, 0x9d, 0xf6, 0x32,
    0x6f, 0x12, 0x2a, 0x0a, 0x24, 0x01, 0x55, 0x12, 0x20, 0xa9, 0x84, 0xd4, 0xaa, 0x52, 0x7b, 0x31,
    0xcc, 0x74, 0x3c, 0x8b, 0xe7, 0xb1, 0x3c, 0xc7, 0xce, 0xbd, 0x9c, 0x40, 0xe3, 0x01, 0x09, 0x87,
    0x2e, 0x58, 0x8f, 0xff, 0xf6, 0xce, 0xca, 0x77, 0x0c, 0x12, 0x00, 0x18, 0x40, 0x12, 0x2a, 0x0a,
    0x24, 0x01, 0x55, 0x12, 0x20, 0xff, 0xaf, 0x46, 0xce, 0xb6, 0x5d, 0xfe, 0x84, 0xf4, 0x45, 0x4a,
    0x5b, 0xdc, 0xc2, 0x04, 0x1c, 0x9f, 0x45, 0x7b, 0xb2, 0x86, 0x47, 0x3d, 0xf5, 0xb9, 0x9d, 0x94,
    0x0a, 0x33, 0x44, 0x97, 0x02, 0x12, 0x00, 0x18, 0x40, 0x12, 0x2a, 0x0a, 0x24, 0x01, 0x55, 0x12,
    0x20, 0x5a, 0x6b, 0x16, 0x67, 0xf6, 0x6e, 0xac, 0x21, 0xdb, 0x9c, 0xbd, 0xdc, 0xcc, 0xb7, 0x0d,
    0xec, 0x2e, 0x76, 0x21, 0x28, 0xc7, 0x2e, 0xde, 0x18, 0x90, 0x65, 0x8e, 0x1f, 0x84, 0x86, 0xc8,
    0xf7, 0x12, 0x00, 0x18, 0x40, 0x12, 0x2a, 0x0a, 0x24, 0x01, 0x55, 0x12, 0x20, 0x2a, 0x8d, 0x9d,
    0x37, 0x52, 0xc7, 0x56, 0x48, 0xf2, 0x7c, 0xc6, 0x7a, 0x3b, 0xb8, 0xb9, 0x32, 0x8d, 0x73, 0x0c,
    0xde, 0x1f, 0x80, 0x69, 0x00, 0x46, 0xad, 0x36, 0x71, 0x8e, 0x78, 0x5f, 0x48, 0x12, 0x00, 0x18,
    0x08, 0x0a, 0x0d, 0x08, 0x02, 0x18, 0xc8, 0x01, 0x20, 0x40, 0x20, 0x40, 0x20, 0x40, 0x20, 0x08,
    0x64, 0x01, 0x55, 0x12, 0x20, 0xa9, 0x84, 0xd4, 0xaa, 0x52, 0x7b, 0x31, 0xcc, 0x74, 0x3c, 0x8b,
    0xe7, 0xb1, 0x3c, 0xc7, 0xce, 0xbd, 0x9c, 0x40, 0xe3, 0x01, 0x09, 0x87, 0x2e, 0x58, 0x8f, 0xff,
    0xf6, 0xce, 0xca, 0x77, 0x0c, 0x6b, 0x61, 0x2d, 0x00, 0x0b, 0xba, 0x0a, 0x2c, 0x6e, 0x12, 0x37,
    0x12, 0x33, 0xe0, 0x2f, 0xff, 0x47, 0xb5, 0x19, 0x66, 0xff, 0xde, 0x9e, 0x46, 0xbf, 0x7b, 0x6f,
    0x35, 0xd3, 0x20, 0x06, 0x99, 0xfd, 0xa5, 0x91, 0xfe, 0xaa, 0x7d, 0xe2, 0x8f, 0xa1, 0xed, 0xbc,
    0xca, 0x85, 0x22, 0x14, 0xb6, 0x47, 0xef, 0xd7, 0x7a, 0xd8, 0xd7, 0xd6, 0x60, 0x0e, 0xd3, 0x9c,
    0xff, 0xf0, 0x1f, 0x59, 0xda, 0x64, 0x01, 0x55, 0x12, 0x20, 0xff, 0xaf, 0x46, 0xce, 0xb6, 0x5d,
    0xfe, 0x84, 0xf4, 0x45, 0x4a, 0x5b, 0xdc, 0xc2, 0x04, 0x1c, 0x9f, 0x45, 0x7b, 0xb2, 0x86, 0x47,
    0x3d, 0xf5, 0xb9, 0x9d, 0x94, 0x0a, 0x33, 0x44, 0x97, 0x02, 0x43, 0x07, 0x4b, 0xcd, 0xd2, 0x03,
    0xc5, 0x19, 0xad, 0xfe, 0x5c, 0x8e, 0x49, 0x9b, 0xa3, 0x20, 0x2c, 0xc7, 0x98, 0x2a, 0x52, 0x9c,
    0xd6, 0x7d, 0xfc, 0xe4, 0xcd, 0x65, 0xf5, 0x02, 0x13, 0xf3, 0x33, 0x81, 0xf0, 0xfa, 0x58, 0x1f,
    0x04, 0xb0, 0x4f, 0xd3, 0x0f, 0x4e, 0xfc, 0x46, 0xab, 0x38, 0x06, 0x36, 0xe9, 0x5e, 0x52, 0x88,
    0x10, 0x7d, 0xbf, 0x35, 0x45, 0xef, 0x86, 0xd9, 0xac, 0x77, 0x64, 0x01, 0x55, 0x12, 0x20, 0x5a,
    0x6b, 0x16, 0x67, 0xf6, 0x6e, 0xac, 0x21, 0xdb, 0x9c, 0xbd, 0xdc, 0xcc, 0xb7, 0x0d, 0xec, 0x2e,
    0x76, 0x21, 0x28, 0xc7, 0x2e, 0xde, 0x18, 0x90, 0x65, 0x8e, 0x1f, 0x84, 0x86, 0xc8, 0xf7, 0xc8,
    0x96, 0x11, 0x7f, 0xac, 0xb5, 0xde, 0xf4, 0xb9, 0xb7, 0x0f, 0x6f, 0xe5, 0xb3, 0xe3, 0xa6, 0xea,
    0x0e, 0x70, 0xc3, 0x3c, 0xd8, 0x08, 0x97, 0x5b, 0xd9, 0xec, 0x0d, 0x38, 0xb5, 0x39, 0x4e, 0x97,
    0xa7, 0x4f, 0x6b, 0x69, 0x01, 0x1c, 0x68, 0x51, 0xfb, 0x1b, 0xee, 0xc9, 0xee, 0x43, 0xbf, 0x04,
    0x5f, 0xcf, 0x91, 0x73, 0xa0, 0xd3, 0x3f, 0x3d, 0x56, 0x61, 0x30, 0xae, 0x96, 0x79, 0x4e, 0x2c,
    0x01, 0x55, 0x12, 0x20, 0x2a, 0x8d, 0x9d, 0x37, 0x52, 0xc7, 0x56, 0x48, 0xf2, 0x7c, 0xc6, 0x7a,
    0x3b, 0xb8, 0xb9, 0x32, 0x8d, 0x73, 0x0c, 0xde, 0x1f, 0x80, 0x69, 0x00, 0x46, 0xad, 0x36, 0x71,
    0x8e, 0x78, 0x5f, 0x48, 0xf8, 0x77, 0x21, 0xe9, 0xf7, 0x96, 0x1c, 0x3c,
};

/* `ipfs add --chunker=size-64` (CIDv0 default: dag-pb leaves, no raw leaves) of the
 * same 200 bytes, then `ipfs dag export`. Root QmVKGN8KVrEMfCp5wWXLD62FgMDRkc3Gp1ZvqC8rxN8U9B. */
static const uint8_t KUBO_V0_CAR[] = {
    0x38, 0xa2, 0x65, 0x72, 0x6f, 0x6f, 0x74, 0x73, 0x81, 0xd8, 0x2a, 0x58, 0x23, 0x00, 0x12, 0x20,
    0x67, 0xa6, 0xf6, 0x73, 0x32, 0xe3, 0x66, 0x41, 0xf7, 0x3c, 0x97, 0x61, 0xe0, 0x15, 0xd6, 0x1e,
    0x2a, 0xcc, 0x3c, 0xf5, 0x61, 0x64, 0xfe, 0x19, 0xa0, 0xf7, 0x66, 0x9f, 0x32, 0x5c, 0xce, 0x2e,
    0x67, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e, 0x01, 0xd9, 0x01, 0x12, 0x20, 0x67, 0xa6, 0xf6,
    0x73, 0x32, 0xe3, 0x66, 0x41, 0xf7, 0x3c, 0x97, 0x61, 0xe0, 0x15, 0xd6, 0x1e, 0x2a, 0xcc, 0x3c,
    0xf5, 0x61, 0x64, 0xfe, 0x19, 0xa0, 0xf7, 0x66, 0x9f, 0x32, 0x5c, 0xce, 0x2e, 0x12, 0x28, 0x0a,
    0x22, 0x12, 0x20, 0x2b, 0x7d, 0x8c, 0x76, 0x14, 0xb6, 0x27, 0x59, 0x7d, 0xbf, 0xc1, 0xe3, 0x10,
    0x24, 0x09, 0xe1, 0xae, 0x13, 0xba, 0xb7, 0x48, 0xb9, 0x28, 0xe1, 0x2c, 0x0d, 0x11, 0x01, 0xab,
    0x02, 0x0a, 0xf7, 0x12, 0x00, 0x18, 0x48, 0x12, 0x28, 0x0a, 0x22, 0x12, 0x20, 0xa2, 0x2a, 0x67,
    0x1f, 0xaa, 0xf7, 0x0f, 0xdf, 0x39, 0x93, 0xe7, 0xc0, 0xb3, 0xcd, 0xe0, 0x38, 0xcd, 0x93, 0x76,
    0xb4, 0x6a, 0xa4, 0x72, 0x92, 0x84, 0x68, 0xc6, 0xbb, 0x6a, 0xfa, 0xdf, 0xdd, 0x12, 0x00, 0x18,
    0x48, 0x12, 0x28, 0x0a, 0x22, 0x12, 0x20, 0x85, 0xe4, 0x8a, 0xc0, 0xaf, 0x38, 0x24, 0xff, 0x06,
    0x7f, 0xb8, 0x6f, 0xd0, 0x30, 0x45, 0xcd, 0x6b, 0x9d, 0xf6, 0x1c, 0xd4, 0xf9, 0xc4, 0x8f, 0x7f,
    0x45, 0x9d, 0xf4, 0x05, 0x33, 0x65, 0xba, 0x12, 0x00, 0x18, 0x48, 0x12, 0x28, 0x0a, 0x22, 0x12,
    0x20, 0x27, 0x05, 0x86, 0x11, 0x7e, 0xe2, 0xba, 0x5e, 0xa1, 0xd3, 0x9e, 0x6c, 0x40, 0x20, 0xed,
    0xad, 0x5f, 0xb4, 0x7d, 0x69, 0x81, 0x7a, 0x51, 0x61, 0x3c, 0xdb, 0xa0, 0x30, 0xea, 0x5d, 0x9a,
    0xe1, 0x12, 0x00, 0x18, 0x10, 0x0a, 0x0d, 0x08, 0x02, 0x18, 0xc8, 0x01, 0x20, 0x40, 0x20, 0x40,
    0x20, 0x40, 0x20, 0x08, 0x6a, 0x12, 0x20, 0x2b, 0x7d, 0x8c, 0x76, 0x14, 0xb6, 0x27, 0x59, 0x7d,
    0xbf, 0xc1, 0xe3, 0x10, 0x24, 0x09, 0xe1, 0xae, 0x13, 0xba, 0xb7, 0x48, 0xb9, 0x28, 0xe1, 0x2c,
    0x0d, 0x11, 0x01, 0xab, 0x02, 0x0a, 0xf7, 0x0a, 0x46, 0x08, 0x02, 0x12, 0x40, 0x6b, 0x61, 0x2d,
    0x00, 0x0b, 0xba, 0x0a, 0x2c, 0x6e, 0x12, 0x37, 0x12, 0x33, 0xe0, 0x2f, 0xff, 0x47, 0xb5, 0x19,
    0x66, 0xff, 0xde, 0x9e, 0x46, 0xbf, 0x7b, 0x6f, 0x35, 0xd3, 0x20, 0x06, 0x99, 0xfd, 0xa5, 0x91,
    0xfe, 0xaa, 0x7d, 0xe2, 0x8f, 0xa1, 0xed, 0xbc, 0xca, 0x85, 0x22, 0x14, 0xb6, 0x47, 0xef, 0xd7,
    0x7a, 0xd8, 0xd7, 0xd6, 0x60, 0x0e, 0xd3, 0x9c, 0xff, 0xf0, 0x1f, 0x59, 0xda, 0x18, 0x40, 0x6a,
    0x12, 0x20, 0xa2, 0x2a, 0x67, 0x1f, 0xaa, 0xf7, 0x0f, 0xdf, 0x39, 0x93, 0xe7, 0xc0, 0xb3, 0xcd,
    0xe0, 0x38, 0xcd, 0x93, 0x76, 0xb4, 0x6a, 0xa4, 0x72, 0x92, 0x84, 0x68, 0xc6, 0xbb, 0x6a, 0xfa,
    0xdf, 0xdd, 0x0a, 0x46, 0x08, 0x02, 0x12, 0x40, 0x43, 0x07, 0x4b, 0xcd, 0xd2, 0x03, 0xc5, 0x19,
    0xad, 0xfe, 0x5c, 0x8e, 0x49, 0x9b, 0xa3, 0x20, 0x2c, 0xc7, 0x98, 0x2a, 0x52, 0x9c, 0xd6, 0x7d,
    0xfc, 0xe4, 0xcd, 0x65, 0xf5, 0x02, 0x13, 0xf3, 0x33, 0x81, 0xf0, 0xfa, 0x58, 0x1f, 0x04, 0xb0,
    0x4f, 0xd3, 0x0f, 0x4e, 0xfc, 0x46, 0xab, 0x38, 0x06, 0x36, 0xe9, 0x5e, 0x52, 0x88, 0x10, 0x7d,
    0xbf, 0x35, 0x45, 0xef, 0x86, 0xd9, 0xac, 0x77, 0x18, 0x40, 0x6a, 0x12, 0x20, 0x85, 0xe4, 0x8a,
    0xc0, 0xaf, 0x38, 0x24, 0xff, 0x06, 0x7f, 0xb8, 0x6f, 0xd0, 0x30, 0x45, 0xcd, 0x6b, 0x9d, 0xf6,
    0x1c, 0xd4, 0xf9, 0xc4, 0x8f, 0x7f, 0x45, 0x9d, 0xf4, 0x05, 0x33, 0x65, 0xba, 0x0a, 0x46, 0x08,
    0x02, 0x12, 0x40, 0xc8, 0x96, 0x11, 0x7f, 0xac, 0xb5, 0xde, 0xf4, 0xb9, 0xb7, 0x0f, 0x6f, 0xe5,
    0xb3, 0xe3, 0xa6, 0xea, 0x0e, 0x70, 0xc3, 0x3c, 0xd8, 0x08, 0x97, 0x5b, 0xd9, 0xec, 0x0d, 0x38,
    0xb5, 0x39, 0x4e, 0x97, 0xa7, 0x4f, 0x6b, 0x69, 0x01, 0x1c, 0x68, 0x51, 0xfb, 0x1b, 0xee, 0xc9,
    0xee, 0x43, 0xbf, 0x04, 0x5f, 0xcf, 0x91, 0x73, 0xa0, 0xd3, 0x3f, 0x3d, 0x56, 0x61, 0x30, 0xae,
    0x96, 0x79, 0x4e, 0x18, 0x40, 0x32, 0x12, 0x20, 0x27, 0x05, 0x86, 0x11, 0x7e, 0xe2, 0xba, 0x5e,
    0xa1, 0xd3, 0x9e, 0x6c, 0x40, 0x20, 0xed, 0xad, 0x5f, 0xb4, 0x7d, 0x69, 0x81, 0x7a, 0x51, 0x61,
    0x3c, 0xdb, 0xa0, 0x30, 0xea, 0x5d, 0x9a, 0xe1, 0x0a, 0x0e, 0x08, 0x02, 0x12, 0x08, 0xf8, 0x77,
    0x21, 0xe9, 0xf7, 0x96, 0x1c, 0x3c, 0x18, 0x08,
};

/* ---- shared buffers (static: the library never allocates) ----------------- */

static uint8_t g_chunk[IPFSN_UFS_CHUNK];
static ipfsn_ufs_t g_ufs;
static ipfsn_walk_t g_walk;
static uint8_t g_leaf[IPFSN_BLOCK_MAX];
static uint8_t g_piece[1u << 20];

#define STORE_CAP (8u << 20)

typedef struct {
    ipfsn_memstore_t ms;
    ipfsn_storage_ops_t ops;
    ipfsn_bs_t bs;
    ipfsn_bs_entry_t tab[65536];
    ipfsn_pin_t pins[32];
    ipfsn_node_t node;
} host_t;

static uint8_t g_store_a[STORE_CAP], g_store_b[STORE_CAP];
static uint8_t g_scratch_a[IPFSN_SCRATCH_MIN], g_scratch_b[IPFSN_SCRATCH_MIN];
static uint8_t g_net_a[IPFSN_BLOCK_MAX + 256], g_net_b[IPFSN_BLOCK_MAX + 256];
static host_t g_a, g_b;

static void host_init(host_t *h, uint8_t *store, uint8_t *scratch, uint8_t *net, const char *key)
{
    memset(h, 0, sizeof *h);
    h->ms.buf = store;
    h->ms.cap = STORE_CAP;
    h->ms.len = 0;
    ipfsn_memstore_ops(&h->ms, &h->ops);
    ipfsn_bs_init(&h->bs, &h->ops, h->tab, 65536, scratch, IPFSN_SCRATCH_MIN);
    if (key) {
        uint8_t k[IPFSN_KEY_LEN];
        sha256((const uint8_t *) key, strlen(key), k);
        ipfsn_bs_set_key(&h->bs, k);
    }
    ipfsn_node_init(&h->node, &h->bs, h->pins, 32, net, IPFSN_BLOCK_MAX + 256);
}

static bool cid_is(const ipfsn_cid_t *c, const char *want)
{
    char s[IPFSN_CID_STR_MAX];
    int n = ipfsn_cid_to_string(c, s, sizeof s);
    if (n < 0 || strcmp(s, want) != 0) {
        printf("    got  %s\n    want %s\n", n < 0 ? "(error)" : s, want);
        return false;
    }
    return true;
}

static bool mem_contains(const uint8_t *hay, uint64_t hlen, const uint8_t *needle, uint32_t nlen)
{
    for (uint64_t i = 0; i + nlen <= hlen; i++)
        if (hay[i] == needle[0] && memcmp(hay + i, needle, nlen) == 0) return true;
    return false;
}

/* ---- sinks and writers ---------------------------------------------------- */

typedef struct {
    uint32_t blocks, max_node;
} count_sink_t;

static int count_sink(void *ctx, const ipfsn_cid_t *cid, const uint8_t *b, uint32_t len)
{
    count_sink_t *c = (count_sink_t *) ctx;
    c->blocks++;
    if (cid->codec == IPFSN_MC_DAG_PB && len > c->max_node) c->max_node = len;
    return ipfsn_cid_verify(cid, b, len);
}

typedef struct {
    ipfsn_bs_t *bs;
    uint32_t vis;
} bs_sink_t;

static int bs_sink(void *ctx, const ipfsn_cid_t *cid, const uint8_t *b, uint32_t len)
{
    bs_sink_t *s = (bs_sink_t *) ctx;
    return ipfsn_bs_put(s->bs, cid, b, len, s->vis, 0);
}

typedef struct {
    sha256_ctx_t h;
    uint64_t n;
    uint8_t *copy;
    uint32_t cap;
} hash_out_t;

static int hash_out(void *ctx, const uint8_t *d, uint32_t len)
{
    hash_out_t *o = (hash_out_t *) ctx;
    if (o->copy && o->n + len <= o->cap) memcpy(o->copy + o->n, d, len);
    sha256_update(&o->h, d, len);
    o->n += len;
    return 0;
}

/* Build the pattern file (seed, n) with the given chunker; the root CID. */
static int build(uint32_t seed, uint64_t n, uint32_t chunk, ipfsn_block_sink_fn sink, void *ctx,
                 ipfsn_cid_t *root, uint8_t digest[32])
{
    gen_t g;
    sha256_ctx_t h;
    uint64_t left = n;
    uint32_t step = 100003; /* deliberately not a chunk multiple */
    int r;
    gen_init(&g, seed);
    sha256_init(&h);
    if ((r = ipfsn_ufs_init(&g_ufs, g_chunk, chunk, 0, sink, ctx)) != 0) return r;
    while (left) {
        uint32_t take = left < step ? (uint32_t) left : step;
        gen_fill(&g, g_piece, take);
        sha256_update(&h, g_piece, take);
        if ((r = ipfsn_ufs_update(&g_ufs, g_piece, take)) != 0) return r;
        left -= take;
    }
    if (digest) sha256_final(&h, digest);
    return ipfsn_ufs_final(&g_ufs, root, 0);
}

/* ---- 1. varint / multibase / CIDs --------------------------------------------- */

static void test_varint(void)
{
    static const uint64_t vals[] = {0,   1,     127,   128,           255,
                                    300, 16383, 16384, 0xFFFFFFFFull, (1ull << 63) - 1};
    uint8_t b[16];
    uint64_t v;
    for (uint32_t i = 0; i < sizeof vals / sizeof vals[0]; i++) {
        uint32_t n = ipfsn_varint_put(vals[i], b, sizeof b);
        CHECK(n > 0 && ipfsn_varint_get(b, n, &v) == (int) n && v == vals[i], "varint round-trip");
    }
    CHECK(ipfsn_varint_put(300, b, 16) == 2 && b[0] == 0xAC && b[1] == 0x02, "varint 300 = ac 02");
    CHECK(ipfsn_varint_put(1ull << 63, b, 16) == 0, "varint >= 2^63 refused");
    CHECK(ipfsn_varint_put(128, b, 1) == 0, "varint put respects cap");
    static const uint8_t nonmin1[] = {0x80, 0x00}, nonmin2[] = {0x81, 0x80, 0x00},
                         trunc[] = {0x80, 0x80},
                         ten[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01};
    CHECK(ipfsn_varint_get(nonmin1, 2, &v) < 0, "varint non-minimal 80 00 rejected");
    CHECK(ipfsn_varint_get(nonmin2, 3, &v) < 0, "varint non-minimal 81 80 00 rejected");
    CHECK(ipfsn_varint_get(trunc, 2, &v) < 0, "varint truncated rejected");
    CHECK(ipfsn_varint_get(ten, 10, &v) < 0, "varint over 9 bytes rejected");
    CHECK(ipfsn_varint_get(trunc, 0, &v) < 0, "varint empty rejected");
}

static void test_bases(void)
{
    uint8_t in[64], out[64];
    char s[200];
    gen_t g;
    gen_init(&g, 99);
    for (uint32_t len = 0; len <= 64; len++) {
        gen_fill(&g, in, len);
        if (len > 2) in[0] = 0; /* exercise leading zeros in base58 */
        int n = ipfsn_base32_encode(in, len, s, sizeof s);
        CHECK(n >= 0 && ipfsn_base32_decode(s, (uint32_t) n, out, sizeof out) == (int) len &&
                  memcmp(in, out, len) == 0,
              "base32 round-trip");
        n = ipfsn_base58_encode(in, len, s, sizeof s);
        CHECK(n >= 0 && ipfsn_base58_decode(s, (uint32_t) n, out, sizeof out) == (int) len &&
                  memcmp(in, out, len) == 0,
              "base58 round-trip");
    }
    CHECK(ipfsn_base32_decode("MZXW6", 5, out, 64) < 0, "base32 uppercase rejected");
    CHECK(ipfsn_base32_decode("mzxw6===", 8, out, 64) < 0, "base32 padding rejected");
    CHECK(ipfsn_base32_decode("mzx1", 4, out, 64) < 0, "base32 bad char rejected");
    CHECK(ipfsn_base32_decode("abc", 3, out, 64) < 0, "base32 impossible length rejected");
    CHECK(ipfsn_base32_decode("ab", 2, out, 64) < 0, "base32 non-zero trailing bits rejected");
    CHECK(ipfsn_base32_decode("mzxw6", 5, out, 64) == 3 && memcmp(out, "foo", 3) == 0,
          "base32 'foo' (RFC 4648)");
    CHECK(ipfsn_base58_decode("0OIl", 4, out, 64) < 0, "base58 excluded chars rejected");
    {
        /* leading '1's count toward the 96-byte bound, so whatever decodes
         * also re-encodes (fuzz_ipfs_cid found 97 decoded bytes) */
        char t[132];
        static uint8_t big[256];
        memset(t, 'h', sizeof t);
        t[0] = '1';
        t[129] = '1';
        t[130] = '1';
        t[131] = 0;
        CHECK(ipfsn_base58_decode(t, 131, big, sizeof big) < 0,
              "base58 decode refuses more than 96 bytes, leading zeros included");
    }
    CHECK(ipfsn_base58_encode((const uint8_t *) "hello world", 11, s, sizeof s) > 0 &&
              strcmp(s, "StV1DL6CwTryKyV") == 0,
          "base58 'hello world' (draft-msporny-base58)");
}

static void test_known_cids(void)
{
    ipfsn_cid_t c, d;
    char s[IPFSN_CID_STR_MAX];
    uint8_t bin[IPFSN_CID_BIN_MAX];
    static const uint8_t hw_sha[32] = {0xa9, 0x48, 0x90, 0x4f, 0x2f, 0x0f, 0x47, 0x9b,
                                       0x8f, 0x81, 0x97, 0x69, 0x4b, 0x30, 0x18, 0x4b,
                                       0x0d, 0x2e, 0xd1, 0xc1, 0xcd, 0x2a, 0x1e, 0xc0,
                                       0xfb, 0x85, 0xd2, 0x99, 0xa1, 0x92, 0xa4, 0x47};
    ipfsn_cid_sha256(IPFSN_MC_RAW, (const uint8_t *) "", 0, &c);
    CHECK(cid_is(&c, "bafkreihdwdcefgh4dqkjv67uzcmw7ojee6xedzdetojuzjevtenxquvyku"),
          "empty raw CID");
    ipfsn_cid_sha256(IPFSN_MC_RAW, (const uint8_t *) "hello world\n", 12, &c);
    CHECK(memcmp(c.digest, hw_sha, 32) == 0, "hello world\\n digest = sha256sum");
    CHECK(cid_is(&c, "bafkreifjjcie6lypi6ny7amxnfftagclbuxndqonfipmb64f2km2devei4"),
          "hello world\\n raw CID (Kubo)");
    CHECK(ipfsn_cid_encode(&c, bin, sizeof bin) == 36 && bin[0] == 1 && bin[1] == 0x55 &&
              bin[2] == 0x12 && bin[3] == 0x20,
          "CIDv1 binary layout 01 55 12 20");
    /* Parse and round-trip */
    const char *v1 = "bafkreifjjcie6lypi6ny7amxnfftagclbuxndqonfipmb64f2km2devei4";
    CHECK(ipfsn_cid_parse(v1, (uint32_t) strlen(v1), &d) == IPFSN_OK && ipfsn_cid_equal(&c, &d),
          "CIDv1 string parse");
    /* CIDv0 -> v1 */
    const char *v0 = "QmT78zSuBmuS4z925WZfrqQ1qHaJ56DQaTfyMUF7F8ff5o";
    CHECK(ipfsn_cid_parse(v0, 46, &d) == IPFSN_OK && d.version == 0, "CIDv0 parse");
    CHECK(ipfsn_cid_to_string(&d, s, sizeof s) == 46 && strcmp(s, v0) == 0, "CIDv0 re-encode");
    ipfsn_cid_to_v1(&d, &d);
    CHECK(cid_is(&d, "bafybeicg2rebjoofv4kbyovkw7af3rpiitvnl6i7ckcywaq6xjcxnc2mby"),
          "CIDv0 -> v1 (Kubo `ipfs cid base32`)");
    /* Identity */
    CHECK(ipfsn_cid_identity(IPFSN_MC_RAW, (const uint8_t *) "hi", 2, &d) == IPFSN_OK &&
              ipfsn_cid_verify(&d, (const uint8_t *) "hi", 2) == IPFSN_OK &&
              ipfsn_cid_verify(&d, (const uint8_t *) "ho", 2) == IPFSN_ERR_HASH,
          "identity CID verify");
    ipfsn_cid_to_string(&d, s, sizeof s);
    CHECK(ipfsn_cid_parse(s, (uint32_t) strlen(s), &c) == IPFSN_OK && ipfsn_cid_equal(&c, &d),
          "identity CID string round-trip");
    /* Malformed */
    static const char *bad[] = {
        "Bafkreifjjcie6lypi6ny7amxnfftagclbuxndqonfipmb64f2km2devei4",  /* uppercase prefix */
        "bAfkreifjjcie6lypi6ny7amxnfftagclbuxndqonfipmb64f2km2devei4",  /* uppercase body */
        "bafkreifjjcie6lypi6ny7amxnfftagclbuxndqonfipmb64f2km2devei",   /* truncated */
        "bafkreifjjcie6lypi6ny7amxnfftagclbuxndqonfipmb64f2km2devei4a", /* extra */
        "zb2rhe5P4gXftAwvA4eXQ5HJwsER2owDyS9sKaQRRVQPn93bA",            /* base58 v1: unsupp */
        "QmT78zSuBmuS4z925WZfrqQ1qHaJ56DQaTfyMUF7F8ff50",               /* '0' not base58 */
        "QmT78zSuBmuS4z925WZfrqQ1qHaJ56DQaTfyMUF7F8ff5",                /* short v0 */
        "",
        "b",
    };
    for (uint32_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK(ipfsn_cid_parse(bad[i], (uint32_t) strlen(bad[i]), &d) < 0, "malformed CID string");
    /* base32 of CIDv0 bytes inside a multibase string: CIDv0 has no multibase form */
    uint8_t v0bin[34] = {0x12, 0x20};
    s[0] = 'b';
    ipfsn_base32_encode(v0bin, 34, s + 1, sizeof s - 1);
    CHECK(ipfsn_cid_parse(s, (uint32_t) strlen(s), &d) < 0, "multibase CIDv0 rejected");
    /* sha2-512 multihash: well-formed but unsupported */
    static const uint8_t sha512cid[] = {0x01, 0x55, 0x13, 0x40};
    uint8_t big[4 + 64] = {0};
    memcpy(big, sha512cid, 4);
    CHECK(ipfsn_cid_decode(big, sizeof big, &d) == IPFSN_ERR_UNSUPP, "sha2-512 unsupported");
    static const uint8_t badlen[] = {0x01, 0x55, 0x12, 0x1f};
    uint8_t bl[4 + 31] = {0};
    memcpy(bl, badlen, 4);
    CHECK(ipfsn_cid_decode(bl, sizeof bl, &d) < 0, "sha2-256 with 31-byte digest rejected");
    static const uint8_t v2[] = {0x02, 0x55, 0x12, 0x20};
    uint8_t vv[36] = {0};
    memcpy(vv, v2, 4);
    CHECK(ipfsn_cid_decode(vv, sizeof vv, &d) < 0, "CID version 2 rejected");
    CHECK(ipfsn_cid_decode(bin, 35, &d) < 0, "truncated binary CID rejected");
    CHECK(ipfsn_cid_decode_exact(bin, 36, &d) == IPFSN_OK, "exact binary CID");
}

/* ---- 2. UnixFS vs Kubo ------------------------------------------------------ */

typedef struct {
    uint32_t seed;
    uint64_t n;
    uint32_t chunk;
    const char *cid;
    const char *what;
} vec_t;

static const vec_t VECS[] = {
    {3, 262144, 0, "bafkreifirtqwzxgmqfujuxv4dqq7k5d3rtg3bxyqtreqlzdm4bw2zsyl2i",
     "256 KiB: exactly one chunk -> raw leaf"},
    {4, 262145, 0, "bafybeiblcoqeiu2es5gippzqzdwxpubdibjzeomolvxth3qrks7dvmtn2i",
     "256 KiB + 1: two leaves"},
    {1, 1048576, 0, "bafybeie2ua5pl4qjmdr5272vm3sezaacwpff6oa5qovteyusjd5kw6up7y", "1 MiB"},
    {5, 45613056, 0, "bafybeict32n52gtb7winbcf3ezzsy7qmltumcowfpgbbdtyhudeph5ga7m",
     "174 chunks: one full node"},
    {6, 45613057, 0, "bafybeihmrjwat4xdji4r275njybt26x3fce6zdbbu35v3w7azjgmejauwe",
     "174 chunks + 1 byte: depth 2"},
    {2, 52428800, 0, "bafybeibinzr7wqmmapdv6qowlxtinxiwmlc4dg4auop5ag5m2wl4mg5i6m", "50 MiB"},
    {7, 2784, 16, "bafybeieoupkao3eeux54qvchfkdlazphoifpt6uzac2egwvkpyab7yvbzu",
     "size-16: 174 leaves"},
    {8, 2785, 16, "bafybeibhfyysjqndufltmbt7by77u6zffahcpanujxlgs6lfjsdf2il2ke",
     "size-16: 175 leaves"},
    {9, 484416, 16, "bafybeid23mzofzfjjsakmd4sl6plirylfj3idmwmg4bfxhsu7aimrw3whu",
     "size-16: 174^2 leaves, full depth 2"},
    {10, 484417, 16, "bafybeihppppgxoc7xhgn56oit3fcvlgt5gljf37alwodrnix54suq2csai",
     "size-16: 174^2 + 1 leaves, depth 3"},
    {11, 200, 64, "bafybeigj6enyizvc3mzvbvmtnh55n2i6ivanmgffsq63bd5qh3kj35rsn4",
     "size-64: 4 leaves"},
};

static void test_unixfs(void)
{
    ipfsn_cid_t root;
    char msg[160];
    for (uint32_t i = 0; i < sizeof VECS / sizeof VECS[0]; i++) {
        count_sink_t cs = {0, 0};
        int r = build(VECS[i].seed, VECS[i].n, VECS[i].chunk, count_sink, &cs, &root, 0);
        snprintf(msg, sizeof msg, "Kubo root CID: %s", VECS[i].what);
        CHECK(r == IPFSN_OK && cid_is(&root, VECS[i].cid), msg);
        CHECK(cs.max_node <= IPFSN_NODE_MAX, "dag-pb node fits IPFSN_NODE_MAX");
    }
    /* Empty file through the builder = the empty raw block */
    count_sink_t cs = {0, 0};
    ipfsn_ufs_init(&g_ufs, g_chunk, 0, 0, count_sink, &cs);
    CHECK(ipfsn_ufs_final(&g_ufs, &root, 0) == IPFSN_OK &&
              cid_is(&root, "bafkreihdwdcefgh4dqkjv67uzcmw7ojee6xedzdetojuzjevtenxquvyku") &&
              cs.blocks == 1,
          "empty file -> one empty raw block");
    ipfsn_ufs_init(&g_ufs, g_chunk, 0, 0, count_sink, &cs);
    ipfsn_ufs_update(&g_ufs, (const uint8_t *) "hello world\n", 12);
    CHECK(ipfsn_ufs_final(&g_ufs, &root, 0) == IPFSN_OK &&
              cid_is(&root, "bafkreifjjcie6lypi6ny7amxnfftagclbuxndqonfipmb64f2km2devei4"),
          "small file -> single raw block");
    CHECK(ipfsn_ufs_update(&g_ufs, (const uint8_t *) "x", 1) != IPFSN_OK,
          "update after final refused");
    CHECK(ipfsn_ufs_init(&g_ufs, g_chunk, 0, 175, count_sink, &cs) == IPFSN_ERR_ARG,
          "more than 174 links refused");
}

/* Round-trip through the blockstore and the DAG walker. */
static void test_walk(void)
{
    ipfsn_cid_t root;
    uint8_t want[32], got[32];
    static const struct {
        uint32_t seed;
        uint64_t n;
        uint32_t chunk;
    } cases[] = {{11, 200, 64}, {8, 2785, 16}, {10, 484417, 16}, {1, 1048576, 0}, {0, 0, 0}};
    for (uint32_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        host_init(&g_a, g_store_a, g_scratch_a, g_net_a, 0);
        bs_sink_t s = {&g_a.bs, IPFSN_VIS_PUBLIC};
        CHECK(build(cases[i].seed, cases[i].n, cases[i].chunk, bs_sink, &s, &root, want) == 0,
              "build into blockstore");
        hash_out_t o;
        memset(&o, 0, sizeof o);
        sha256_init(&o.h);
        g_walk.leaf = g_leaf;
        g_walk.leaf_cap = sizeof g_leaf;
        uint64_t size = 0;
        int r = ipfsn_cat(&g_walk, &root, ipfsn_bs_source, &g_a.bs, hash_out, &o, &size);
        sha256_final(&o.h, got);
        CHECK(r == IPFSN_OK && size == cases[i].n && memcmp(want, got, 32) == 0,
              "cat reproduces the file exactly");
    }
    /* A source that lies about a block is caught by the walker itself. */
    host_init(&g_a, g_store_a, g_scratch_a, g_net_a, 0);
    bs_sink_t s = {&g_a.bs, IPFSN_VIS_PUBLIC};
    build(11, 200, 64, bs_sink, &s, &root, 0);
    for (uint64_t i = 0; i < g_a.ms.len; i++)
        if (g_store_a[i] == 'Z' && memcmp(g_store_a + i, "ZXIB", 4) == 0) { /* first record */
            g_store_a[i + 12 + 36 + 5] ^= 0x01; /* flip a byte in the first leaf payload */
            break;
        }
    hash_out_t o;
    memset(&o, 0, sizeof o);
    sha256_init(&o.h);
    CHECK(ipfsn_cat(&g_walk, &root, ipfsn_bs_source, &g_a.bs, hash_out, &o, 0) == IPFSN_ERR_HASH,
          "cat over a corrupted block fails with HASH");
}

/* ---- 3. Blockstore --------------------------------------------------------------- */

static void test_blockstore(void)
{
    ipfsn_cid_t c1, c2, bad;
    uint8_t buf[256];
    uint32_t len;
    host_init(&g_a, g_store_a, g_scratch_a, g_net_a, 0);
    ipfsn_cid_sha256(IPFSN_MC_RAW, (const uint8_t *) "alpha", 5, &c1);
    ipfsn_cid_sha256(IPFSN_MC_RAW, (const uint8_t *) "beta", 4, &c2);
    CHECK(ipfsn_bs_put(&g_a.bs, &c1, (const uint8_t *) "alpha", 5, IPFSN_VIS_PUBLIC, 0) == 0,
          "put alpha");
    uint64_t after_one = g_a.ms.len;
    CHECK(ipfsn_bs_put(&g_a.bs, &c1, (const uint8_t *) "alpha", 5, IPFSN_VIS_PUBLIC, 0) == 0 &&
              g_a.ms.len == after_one,
          "second put is deduplicated (no new bytes)");
    CHECK(ipfsn_bs_put(&g_a.bs, &c2, (const uint8_t *) "BETA", 4, IPFSN_VIS_PUBLIC, 0) ==
              IPFSN_ERR_HASH,
          "put with bytes that do not match the CID refused");
    CHECK(ipfsn_bs_put(&g_a.bs, &c2, (const uint8_t *) "beta", 4, IPFSN_VIS_PUBLIC, 0) == 0,
          "put beta");
    CHECK(ipfsn_bs_get(&g_a.bs, &c1, buf, sizeof buf, &len) == 0 && len == 5 &&
              memcmp(buf, "alpha", 5) == 0,
          "get alpha");
    CHECK(ipfsn_bs_get(&g_a.bs, &c1, buf, 4, &len) == IPFSN_ERR_SPACE, "get into small buffer");
    ipfsn_cid_sha256(IPFSN_MC_RAW, (const uint8_t *) "gamma", 5, &bad);
    CHECK(ipfsn_bs_get(&g_a.bs, &bad, buf, sizeof buf, &len) == IPFSN_ERR_NOTFOUND, "get missing");
    /* Persistence: remount the same storage. */
    ipfsn_bs_t bs2;
    static ipfsn_bs_entry_t tab2[64];
    ipfsn_bs_init(&bs2, &g_a.ops, tab2, 64, 0, 0);
    CHECK(ipfsn_bs_mount(&bs2) == 0 && bs2.count == 2, "mount rebuilds the index");
    CHECK(ipfsn_bs_get(&bs2, &c2, buf, sizeof buf, &len) == 0 && len == 4, "get after remount");
    /* Corruption: flip one payload byte of "beta" on the medium. */
    uint8_t *p = 0;
    for (uint64_t i = 0; i + 4 <= g_a.ms.len && !p; i++)
        if (!memcmp(g_store_a + i, "beta", 4)) p = g_store_a + i;
    CHECK(p != 0, "found beta on the medium");
    if (p) p[1] ^= 0x20;
    CHECK(ipfsn_bs_get(&g_a.bs, &c2, buf, sizeof buf, &len) == IPFSN_ERR_HASH,
          "corrupted block detected on get");
    if (p) p[1] ^= 0x20;
    /* A torn final append is dropped on mount, the rest survives. */
    g_a.ms.len -= 2;
    CHECK(ipfsn_bs_mount(&bs2) == 0 && bs2.count == 1 && ipfsn_bs_has(&bs2, &c1) &&
              !ipfsn_bs_has(&bs2, &c2),
          "torn tail record ignored on mount");
    CHECK(ipfsn_bs_put(&bs2, &c2, (const uint8_t *) "beta", 4, IPFSN_VIS_PUBLIC, 0) == 0 &&
              ipfsn_bs_get(&bs2, &c2, buf, sizeof buf, &len) == 0,
          "append after torn tail overwrites it");
    /* v0 and v1 of a dag-pb CID address the same block. */
    ipfsn_cid_t pb, pb0;
    static const uint8_t node[] = {0x0a, 0x02, 0x08, 0x01}; /* UnixFS directory, no links */
    ipfsn_cid_sha256(IPFSN_MC_DAG_PB, node, 4, &pb);
    ipfsn_bs_put(&bs2, &pb, node, 4, IPFSN_VIS_PUBLIC, 0);
    pb0 = pb;
    pb0.version = 0;
    CHECK(ipfsn_bs_get(&bs2, &pb0, buf, sizeof buf, &len) == 0 && len == 4,
          "CIDv0 finds the block stored under v1");
}

/* ---- 4. Private pins ---------------------------------------------------------------- */

typedef struct {
    uint32_t calls;
    ipfsn_cid_t seen[16];
} announce_log_t;

static int announce_rec(void *ctx, const ipfsn_cid_t *cid)
{
    announce_log_t *a = (announce_log_t *) ctx;
    if (a->calls < 16) a->seen[a->calls] = *cid;
    a->calls++;
    return 0;
}

typedef struct {
    uint32_t calls;
} transport_spy_t;

static int spy_get(void *ctx, const char *url, const char *accept, uint8_t *buf, uint32_t cap,
                   uint32_t *len)
{
    (void) url;
    (void) accept;
    (void) buf;
    (void) cap;
    (void) len;
    ((transport_spy_t *) ctx)->calls++;
    return -1;
}

static int spy_want(void *ctx, const uint8_t *cid, uint32_t cl, uint8_t *buf, uint32_t cap,
                    uint32_t *len)
{
    (void) cid;
    (void) cl;
    (void) buf;
    (void) cap;
    (void) len;
    ((transport_spy_t *) ctx)->calls++;
    return -1;
}

static uint8_t g_secret[600000];

static void test_private(void)
{
    ipfsn_cid_t root, handle, pub_root, proot, phandle, pub2;
    uint8_t cb[IPFSN_CID_BIN_MAX];
    uint32_t len;
    gen_t g;
    gen_init(&g, 4242);
    gen_fill(&g, g_secret, sizeof g_secret);

    host_init(&g_a, g_store_a, g_scratch_a, g_net_a, 0);
    CHECK(ipfsn_add(&g_a.node, &g_ufs, g_chunk, g_secret, sizeof g_secret, IPFSN_VIS_PRIVATE, false,
                    &root, 0) == IPFSN_ERR_NOKEY,
          "private add without a node key refused");

    host_init(&g_a, g_store_a, g_scratch_a, g_net_a, "node key A");
    announce_log_t log;
    memset(&log, 0, sizeof log);
    g_a.node.prov.announce = announce_rec;
    g_a.node.prov.ctx = &log;
    transport_spy_t gw = {0}, peer = {0};
    g_a.node.gw.https_get = spy_get;
    g_a.node.gw.ctx = &gw;
    g_a.node.gw.base = "https://gw.example";
    g_a.node.peer.want = spy_want;
    g_a.node.peer.ctx = &peer;

    /* one public file and one private file (private-CID mode) */
    CHECK(ipfsn_add(&g_a.node, &g_ufs, g_chunk, (const uint8_t *) "public notice", 13,
                    IPFSN_VIS_PUBLIC, false, &pub_root, 0) == 0,
          "public add");
    CHECK(ipfsn_add(&g_a.node, &g_ufs, g_chunk, g_secret, sizeof g_secret, IPFSN_VIS_PRIVATE, true,
                    &root, &handle) == 0,
          "private add (private-CID mode)");
    CHECK(!ipfsn_cid_equal(&root, &handle) && handle.codec == IPFSN_MC_RAW,
          "private CID differs from the plaintext CID");

    /* Encrypted at rest: neither the bytes nor the plaintext CIDs are on the medium. */
    int cl = ipfsn_cid_encode(&root, cb, sizeof cb);
    CHECK(!mem_contains(g_store_a, g_a.ms.len, g_secret + 1000, 32) &&
              !mem_contains(g_store_a, g_a.ms.len, g_secret + 300000, 32),
          "private plaintext not stored in the clear");
    CHECK(!mem_contains(g_store_a, g_a.ms.len, cb, (uint32_t) cl),
          "private plaintext root CID not stored in the clear");
    CHECK(mem_contains(g_store_a, g_a.ms.len, (const uint8_t *) "public notice", 13),
          "public content stored as-is");

    /* Reads work by plaintext CID and by private CID, and re-hash. */
    static uint8_t blk[IPFSN_BLOCK_MAX];
    CHECK(ipfsn_bs_get(&g_a.bs, &root, blk, sizeof blk, &len) == 0, "get private by plain CID");
    CHECK(ipfsn_bs_get(&g_a.bs, &handle, blk, sizeof blk, &len) == 0 &&
              ipfsn_cid_verify(&root, blk, len) == 0,
          "get private by private CID yields the plaintext root");
    hash_out_t o;
    memset(&o, 0, sizeof o);
    sha256_init(&o.h);
    uint8_t want[32], got[32];
    sha256(g_secret, sizeof g_secret, want);
    g_walk.leaf = g_leaf;
    g_walk.leaf_cap = sizeof g_leaf;
    CHECK(ipfsn_node_cat(&g_a.node, &g_walk, &root, hash_out, &o, 0) == 0,
          "cat private file locally");
    sha256_final(&o.h, got);
    CHECK(memcmp(want, got, 32) == 0, "private file content intact");

    /* Never announced, never served, never asked of the network. */
    CHECK(ipfsn_provide(&g_a.node, &root) == IPFSN_ERR_PRIVATE, "provide(private root) refused");
    CHECK(ipfsn_provide(&g_a.node, &handle) == IPFSN_ERR_PRIVATE, "provide(private CID) refused");
    CHECK(ipfsn_provide_all(&g_a.node) == 1 && log.calls == 1 &&
              ipfsn_cid_equal(&log.seen[0], &pub_root),
          "provide_all announces only the public pin");
    for (uint32_t i = 0; i < log.calls && i < 16; i++)
        CHECK(!ipfsn_is_private(&g_a.node, &log.seen[i]), "no private CID ever announced");
    uint8_t out[1024];
    CHECK(ipfsn_serve_block(&g_a.node, &root, IPFSN_WIRE_UBH168, out, sizeof out, &len) ==
              IPFSN_ERR_PRIVATE,
          "serve(private root) refused");
    ipfsn_pbnode_t pbn;
    ipfsn_pblink_t l;
    uint32_t pos = 0;
    ipfsn_bs_get(&g_a.bs, &root, blk, sizeof blk, &len);
    CHECK(ipfsn_dagpb_parse(blk, len, &pbn) == 0 && ipfsn_dagpb_next_link(&pbn, &pos, &l) == 0,
          "private root has links");
    CHECK(ipfsn_serve_block(&g_a.node, &l.cid, IPFSN_WIRE_PLAIN, blk, sizeof blk, &len) ==
              IPFSN_ERR_PRIVATE,
          "serve(private leaf) refused");
    CHECK(ipfsn_serve_block(&g_a.node, &pub_root, IPFSN_WIRE_PLAIN, out, sizeof out, &len) == 0 &&
              len == 13,
          "serve(public) allowed");
    CHECK(ipfsn_gw_fetch_block(&g_a.node, &root, blk, sizeof blk, &len) == IPFSN_ERR_PRIVATE &&
              ipfsn_gw_fetch_car(&g_a.node, &handle, blk, sizeof blk, &len) == IPFSN_ERR_PRIVATE &&
              ipfsn_peer_fetch(&g_a.node, &l.cid, blk, sizeof blk, &len) == IPFSN_ERR_PRIVATE &&
              gw.calls == 0 && peer.calls == 0,
          "private CIDs never reach a gateway or a peer");
    CHECK(ipfsn_pin(&g_a.node, &root, IPFSN_VIS_PUBLIC, 0) == IPFSN_ERR_CONFLICT,
          "pinning a private CID public is a conflict");

    /* A private file whose leaves are missing locally: the walk must not go to
     * the network for them (a leaf CID is not itself marked private). */
    {
        static host_t h;
        static uint8_t st[1u << 20];
        uint32_t rl;
        ipfsn_bs_get(&g_a.bs, &root, blk, sizeof blk, &rl);
        memset(&h, 0, sizeof h);
        h.ms.buf = st;
        h.ms.cap = sizeof st;
        ipfsn_memstore_ops(&h.ms, &h.ops);
        ipfsn_bs_init(&h.bs, &h.ops, h.tab, 1024, g_scratch_b, IPFSN_SCRATCH_MIN);
        uint8_t k2[IPFSN_KEY_LEN];
        sha256((const uint8_t *) "node key A", 10, k2);
        ipfsn_bs_set_key(&h.bs, k2);
        ipfsn_node_init(&h.node, &h.bs, h.pins, 32, g_net_b, IPFSN_BLOCK_MAX + 256);
        h.node.gw = g_a.node.gw;
        h.node.peer = g_a.node.peer;
        ipfsn_bs_put(&h.bs, &root, blk, rl, IPFSN_VIS_PRIVATE, 0); /* root only */
        ipfsn_pin(&h.node, &root, IPFSN_VIS_PRIVATE, 0);
        hash_out_t o2;
        memset(&o2, 0, sizeof o2);
        sha256_init(&o2.h);
        CHECK(ipfsn_node_cat(&h.node, &g_walk, &root, hash_out, &o2, 0) == IPFSN_ERR_NOTFOUND &&
                  gw.calls == 0 && peer.calls == 0,
              "private walk with missing leaves stays local");
    }

    /* Tamper with the sealed record: AEAD or hash check fails, nothing returned. */
    ipfsn_bs_entry_t *e = 0;
    for (uint32_t i = 0; i < g_a.bs.cap; i++)
        if (g_a.bs.tab[i].used && ipfsn_cid_equal(&g_a.bs.tab[i].cid, &root)) e = &g_a.bs.tab[i];
    CHECK(e && (e->flags & IPFSN_BSE_PRIVATE), "private index entry");
    if (e) {
        uint64_t at = e->off + IPFSN_REC_HDR + 36 + 20; /* inside the ciphertext */
        g_store_a[at] ^= 0x80;
        int r1 = ipfsn_bs_get(&g_a.bs, &root, blk, sizeof blk, &len);
        CHECK(r1 == IPFSN_ERR_HASH || r1 == IPFSN_ERR_CRYPTO, "tampered sealed block rejected");
        g_store_a[at] ^= 0x80;
    }

    /* Remount: without the key the plaintext CID is unknown; with it, restored. */
    ipfsn_bs_t bs2;
    static ipfsn_bs_entry_t tab2[1024];
    ipfsn_bs_init(&bs2, &g_a.ops, tab2, 1024, g_scratch_b, IPFSN_SCRATCH_MIN);
    ipfsn_bs_mount(&bs2);
    CHECK(!ipfsn_bs_has(&bs2, &root) && ipfsn_bs_has(&bs2, &handle),
          "keyless mount: only private CIDs");
    CHECK(ipfsn_bs_get(&bs2, &handle, blk, sizeof blk, &len) == IPFSN_ERR_NOKEY,
          "keyless get refused");
    uint8_t k[IPFSN_KEY_LEN];
    sha256((const uint8_t *) "node key A", 10, k);
    ipfsn_bs_set_key(&bs2, k);
    ipfsn_bs_mount(&bs2);
    CHECK(ipfsn_bs_get(&bs2, &root, blk, sizeof blk, &len) == 0, "keyed mount restores plain CIDs");
    sha256((const uint8_t *) "wrong key", 9, k);
    ipfsn_bs_set_key(&bs2, k);
    CHECK(ipfsn_bs_get(&bs2, &handle, blk, sizeof blk, &len) == IPFSN_ERR_CRYPTO,
          "wrong key cannot open");

    /* The caveat, demonstrated: the plaintext CID of a private file equals the
     * CID of any public copy. The private CID does not, and differs per node key. */
    host_init(&g_b, g_store_b, g_scratch_b, g_net_b, "node key B");
    CHECK(ipfsn_add(&g_b.node, &g_ufs, g_chunk, g_secret, sizeof g_secret, IPFSN_VIS_PUBLIC, false,
                    &pub2, 0) == 0 &&
              ipfsn_cid_equal(&pub2, &root),
          "caveat: plaintext CID == CID of a public copy");
    host_init(&g_b, g_store_b, g_scratch_b, g_net_b, "node key B");
    CHECK(ipfsn_add(&g_b.node, &g_ufs, g_chunk, g_secret, sizeof g_secret, IPFSN_VIS_PRIVATE, true,
                    &proot, &phandle) == 0 &&
              ipfsn_cid_equal(&proot, &root) && !ipfsn_cid_equal(&phandle, &handle) &&
              !ipfsn_cid_equal(&phandle, &pub2),
          "private CID hides equality (and differs across node keys)");
    /* Deterministic sealing: the same private file seals once (dedup). */
    uint64_t before = g_a.ms.len;
    ipfsn_unpin(&g_a.node, &root);
    CHECK(ipfsn_add(&g_a.node, &g_ufs, g_chunk, g_secret, sizeof g_secret, IPFSN_VIS_PRIVATE, true,
                    &proot, &phandle) == 0 &&
              ipfsn_cid_equal(&phandle, &handle) && g_a.ms.len == before,
          "re-adding a private file writes nothing new");
}

/* ---- 6. CAR + gateway + peers ------------------------------------------------------ */

typedef struct {
    ipfsn_bs_t *remote;
    const uint8_t *car;
    uint32_t car_len;
    int tamper;
    uint32_t calls;
    char last_url[512];
    char last_accept[64];
} fake_gw_t;

static int fake_https_get(void *ctx, const char *url, const char *accept, uint8_t *buf,
                          uint32_t cap, uint32_t *len)
{
    fake_gw_t *f = (fake_gw_t *) ctx;
    ipfsn_cid_t cid;
    f->calls++;
    snprintf(f->last_url, sizeof f->last_url, "%s", url);
    snprintf(f->last_accept, sizeof f->last_accept, "%s", accept);
    const char *p = strstr(url, "/ipfs/"), *q = strchr(url, '?');
    if (!p || !q) return -1;
    if (ipfsn_cid_parse(p + 6, (uint32_t) (q - p - 6), &cid)) return -1;
    if (!strcmp(q, "?format=car")) {
        if (!f->car || f->car_len > cap) return -1;
        memcpy(buf, f->car, f->car_len);
        *len = f->car_len;
    } else {
        if (ipfsn_bs_get(f->remote, &cid, buf, cap, len)) return -1;
    }
    if (f->tamper && *len) buf[*len / 2] ^= 1;
    return 0;
}

static uint8_t g_car[1u << 16];

typedef struct {
    ipfsn_cid_t cid;
    uint8_t b[256];
    uint32_t len;
} small_block_t;

typedef struct {
    small_block_t blk[8];
    uint32_t n;
} collect_t;

static int collect_sink(void *ctx, const ipfsn_cid_t *cid, const uint8_t *b, uint32_t len)
{
    collect_t *c = (collect_t *) ctx;
    if (c->n >= 8 || len > sizeof c->blk[0].b) return -1;
    c->blk[c->n].cid = *cid;
    memcpy(c->blk[c->n].b, b, len);
    c->blk[c->n].len = len;
    c->n++;
    return 0;
}

static void test_car(void)
{
    ipfsn_car_t car;
    ipfsn_cid_t cid, root;
    const uint8_t *b;
    uint32_t len, n = 0;
    CHECK(ipfsn_car_open(&car, KUBO_SMALL_CAR, sizeof KUBO_SMALL_CAR) == 0 && car.nroots == 1 &&
              cid_is(&car.roots[0], VECS[10].cid),
          "Kubo CAR header parsed, root matches");
    int r;
    while ((r = ipfsn_car_next(&car, &cid, &b, &len)) == IPFSN_OK) n++;
    CHECK(r == IPFSN_ERR_NOTFOUND && n == 5, "Kubo CAR: 5 blocks, all verified");
    /* Reassemble from the CAR and compare with the generator. */
    uint8_t want[200], copy[200];
    gen_t g;
    gen_init(&g, 11);
    gen_fill(&g, want, 200);
    hash_out_t o;
    memset(&o, 0, sizeof o);
    sha256_init(&o.h);
    o.copy = copy;
    o.cap = sizeof copy;
    g_walk.leaf = g_leaf;
    g_walk.leaf_cap = sizeof g_leaf;
    uint64_t size = 0;
    CHECK(ipfsn_cat(&g_walk, &car.roots[0], ipfsn_car_source, &car, hash_out, &o, &size) == 0 &&
              size == 200 && memcmp(copy, want, 200) == 0,
          "file reassembled from Kubo CAR");
    /* Our CAR writer reproduces Kubo's export byte for byte (root first, then leaves). */
    collect_t col;
    memset(&col, 0, sizeof col);
    build(11, 200, 64, collect_sink, &col, &root, 0);
    uint32_t w = 0;
    int k = ipfsn_car_begin(&root, g_car, sizeof g_car);
    CHECK(k > 0 && col.n == 5, "car_begin");
    w = (uint32_t) k;
    k = ipfsn_car_put(&col.blk[4].cid, col.blk[4].b, col.blk[4].len, g_car + w, sizeof g_car - w);
    w += (uint32_t) k;
    for (uint32_t i = 0; i < 4; i++) {
        k = ipfsn_car_put(&col.blk[i].cid, col.blk[i].b, col.blk[i].len, g_car + w,
                          sizeof g_car - w);
        w += (uint32_t) k;
    }
    CHECK(w == sizeof KUBO_SMALL_CAR && memcmp(g_car, KUBO_SMALL_CAR, w) == 0,
          "CAR writer == `ipfs dag export` bytes");
    CHECK(memcmp(col.blk[4].b, KUBO_SMALL_CAR + 59 + 2 + 36, col.blk[4].len) == 0,
          "dag-pb root node bytes == Kubo's");
    /* Tampered block inside the CAR. */
    static uint8_t t[sizeof KUBO_SMALL_CAR];
    memcpy(t, KUBO_SMALL_CAR, sizeof t);
    t[sizeof t - 3] ^= 0x04; /* last leaf's data */
    ipfsn_car_open(&car, t, sizeof t);
    n = 0;
    while ((r = ipfsn_car_next(&car, &cid, &b, &len)) == IPFSN_OK) n++;
    CHECK(r == IPFSN_ERR_HASH && n == 4, "tampered CAR block rejected");
    /* Malformed CARs */
    memcpy(t, KUBO_SMALL_CAR, sizeof t);
    t[sizeof t - 42] = 0xff; /* break structure inside the last section's CID */
    ipfsn_car_open(&car, t, sizeof t);
    while ((r = ipfsn_car_next(&car, &cid, &b, &len)) == IPFSN_OK) {
    }
    CHECK(r != IPFSN_ERR_NOTFOUND, "corrupted CAR section does not end cleanly");
    memcpy(t, KUBO_SMALL_CAR, sizeof t);
    t[t[0] - 1 + 1] = 0x02; /* version 2 */
    CHECK(ipfsn_car_open(&car, t, sizeof t) == IPFSN_ERR_UNSUPP, "CAR version 2 refused");
    CHECK(ipfsn_car_open(&car, KUBO_SMALL_CAR, 30) < 0, "truncated CAR header refused");
    /* CIDv0 file with dag-pb leaves (UnixFS Data inside each leaf) */
    ipfsn_cid_t v0;
    const char *v0s = "QmVKGN8KVrEMfCp5wWXLD62FgMDRkc3Gp1ZvqC8rxN8U9B";
    CHECK(ipfsn_cid_parse(v0s, 46, &v0) == 0 &&
              ipfsn_car_open(&car, KUBO_V0_CAR, sizeof KUBO_V0_CAR) == 0 && car.nroots == 1 &&
              ipfsn_cid_equal(&car.roots[0], &v0),
          "Kubo CIDv0 CAR root");
    memset(&o, 0, sizeof o);
    sha256_init(&o.h);
    o.copy = copy;
    o.cap = sizeof copy;
    memset(copy, 0, sizeof copy);
    CHECK(ipfsn_cat(&g_walk, &v0, ipfsn_car_source, &car, hash_out, &o, &size) == 0 &&
              size == 200 && memcmp(copy, want, 200) == 0,
          "CIDv0 file with dag-pb leaves reassembled");
}

static void test_gateway(void)
{
    ipfsn_cid_t root, cid;
    char url[256];
    uint8_t want[32], got[32];
    /* Remote "network" content: the 1 MiB vector, public on node B. */
    host_init(&g_b, g_store_b, g_scratch_b, g_net_b, 0);
    bs_sink_t s = {&g_b.bs, IPFSN_VIS_PUBLIC};
    build(1, 1048576, 0, bs_sink, &s, &root, want);
    CHECK(cid_is(&root, VECS[2].cid), "remote 1 MiB root");

    ipfsn_cid_sha256(IPFSN_MC_RAW, (const uint8_t *) "hello world\n", 12, &cid);
    CHECK(
        ipfsn_gw_url("https://trustless-gateway.link", &cid, IPFSN_FMT_RAW, url, sizeof url) > 0 &&
            !strcmp(url, "https://trustless-gateway.link/ipfs/"
                         "bafkreifjjcie6lypi6ny7amxnfftagclbuxndqonfipmb64f2km2devei4?format=raw"),
        "gateway raw URL");
    CHECK(ipfsn_gw_url("https://gw/", &cid, IPFSN_FMT_RAW, url, sizeof url) < 0,
          "gateway base with trailing slash refused");

    host_init(&g_a, g_store_a, g_scratch_a, g_net_a, 0);
    fake_gw_t f;
    memset(&f, 0, sizeof f);
    f.remote = &g_b.bs;
    g_a.node.gw.https_get = fake_https_get;
    g_a.node.gw.ctx = &f;
    g_a.node.gw.base = "https://gw.example";
    /* Tampered gateway answer: refused and not stored. */
    static uint8_t blk[IPFSN_BLOCK_MAX];
    uint32_t len;
    f.tamper = 1;
    CHECK(ipfsn_gw_fetch_block(&g_a.node, &root, blk, sizeof blk, &len) == IPFSN_ERR_HASH &&
              !ipfsn_bs_has(&g_a.bs, &root),
          "tampered gateway block rejected, not stored");
    CHECK(!strcmp(f.last_accept, "application/vnd.ipld.raw"), "Accept: application/vnd.ipld.raw");
    f.tamper = 0;
    /* Pull the whole file block by block through the gateway. */
    hash_out_t o;
    memset(&o, 0, sizeof o);
    sha256_init(&o.h);
    g_walk.leaf = g_leaf;
    g_walk.leaf_cap = sizeof g_leaf;
    uint64_t size = 0;
    f.calls = 0;
    CHECK(ipfsn_node_cat(&g_a.node, &g_walk, &root, hash_out, &o, &size) == 0 && size == 1048576,
          "cat pulls the file through the gateway");
    sha256_final(&o.h, got);
    CHECK(memcmp(want, got, 32) == 0 && f.calls == 5, "content verified (5 raw fetches)");
    CHECK(ipfsn_bs_has(&g_a.bs, &root) &&
              ipfsn_bs_visibility(&g_a.bs, &root) == (int) IPFSN_VIS_PUBLIC,
          "fetched blocks cached as public");
    /* CAR fetch: good and tampered. */
    host_init(&g_a, g_store_a, g_scratch_a, g_net_a, 0);
    g_a.node.gw.https_get = fake_https_get;
    g_a.node.gw.ctx = &f;
    g_a.node.gw.base = "https://gw.example";
    ipfsn_car_t car;
    ipfsn_car_open(&car, KUBO_SMALL_CAR, sizeof KUBO_SMALL_CAR);
    f.car = KUBO_SMALL_CAR;
    f.car_len = sizeof KUBO_SMALL_CAR;
    f.tamper = 1;
    uint32_t nb = 99;
    CHECK(ipfsn_gw_fetch_car(&g_a.node, &car.roots[0], g_car, sizeof g_car, &nb) ==
                  IPFSN_ERR_HASH &&
              nb == 0 && g_a.bs.count == 0,
          "tampered CAR: nothing stored");
    CHECK(!strcmp(f.last_accept, "application/vnd.ipld.car") && strstr(f.last_url, "?format=car"),
          "CAR request format");
    f.tamper = 0;
    CHECK(ipfsn_gw_fetch_car(&g_a.node, &car.roots[0], g_car, sizeof g_car, &nb) == 0 && nb == 5,
          "CAR fetch stores 5 verified blocks");
    memset(&o, 0, sizeof o);
    sha256_init(&o.h);
    CHECK(ipfsn_cat(&g_walk, &car.roots[0], ipfsn_bs_source, &g_a.bs, hash_out, &o, &size) == 0 &&
              size == 200,
          "cat from blocks fetched by CAR");
    CHECK(ipfsn_gw_fetch_car(&g_a.node, &cid, g_car, sizeof g_car, &nb) == IPFSN_ERR_MALFORMED,
          "CAR whose roots do not include the request refused");
}

/* Carracho-style peer: answers wants by asking the remote node to serve. */
typedef struct {
    ipfsn_node_t *remote;
    uint32_t wire; /* what the peer actually speaks */
    int tamper;
} fake_peer_t;

static int fake_want(void *ctx, const uint8_t *cb, uint32_t cl, uint8_t *buf, uint32_t cap,
                     uint32_t *len)
{
    fake_peer_t *p = (fake_peer_t *) ctx;
    ipfsn_cid_t cid;
    if (ipfsn_cid_decode_exact(cb, cl, &cid)) return -1;
    if (ipfsn_serve_block(p->remote, &cid, p->wire, buf, cap, len)) return -1;
    if (p->tamper) buf[*len - 30] ^= 1;
    return 0;
}

static void test_peers(void)
{
    ipfsn_cid_t root;
    uint8_t want[32], got[32];
    host_init(&g_b, g_store_b, g_scratch_b, g_net_b, 0);
    bs_sink_t s = {&g_b.bs, IPFSN_VIS_PUBLIC};
    build(10, 484417, 16, bs_sink, &s, &root, want); /* ~30k blocks, depth 3 */
    ipfsn_pin(&g_b.node, &root, IPFSN_VIS_PUBLIC, 0);
    for (uint32_t mode = 0; mode < 2; mode++) {
        host_init(&g_a, g_store_a, g_scratch_a, g_net_a, 0);
        fake_peer_t p = {&g_b.node, mode, 0};
        g_a.node.peer.want = fake_want;
        g_a.node.peer.ctx = &p;
        g_a.node.peer.wire = mode;
        hash_out_t o;
        memset(&o, 0, sizeof o);
        sha256_init(&o.h);
        g_walk.leaf = g_leaf;
        g_walk.leaf_cap = sizeof g_leaf;
        uint64_t size = 0;
        int r = ipfsn_node_cat(&g_a.node, &g_walk, &root, hash_out, &o, &size);
        sha256_final(&o.h, got);
        CHECK(r == 0 && size == 484417 && memcmp(want, got, 32) == 0,
              mode ? "pull from peer over UBH-168" : "pull from peer over plain bytes");
        /* tampered answer */
        host_init(&g_a, g_store_a, g_scratch_a, g_net_a, 0);
        p.tamper = 1;
        g_a.node.peer.want = fake_want;
        g_a.node.peer.ctx = &p;
        g_a.node.peer.wire = mode;
        static uint8_t blk[IPFSN_BLOCK_MAX];
        uint32_t len;
        r = ipfsn_peer_fetch(&g_a.node, &root, blk, sizeof blk, &len);
        CHECK(r != 0 && !ipfsn_bs_has(&g_a.bs, &root), "tampered peer block rejected");
        /* mode mismatch fails closed */
        p.tamper = 0;
        g_a.node.peer.wire = 1 - mode;
        r = ipfsn_peer_fetch(&g_a.node, &root, blk, sizeof blk, &len);
        CHECK(r != 0, "peer speaking the other syntax is not misread");
    }
}

/* ---- UBH-168 ----------------------------------------------------------------------- */

static void test_ubh(void)
{
    ipfsn_cid_t c, d;
    uint8_t env[256];
    uint32_t len;
    char txt[300];
    ipfsn_cid_sha256(IPFSN_MC_RAW, (const uint8_t *) "hello world\n", 12, &c);
    CHECK(ipfsn_ubh_encode_cid(&c, env, sizeof env, &len) == 0 && len == 21 * 4,
          "CID envelope: header + 2 frames + trailer");
    CHECK(env[0] == 'Z' && env[1] == 'X' && env[2] == 'V' && (env[3] & 0x0f) == 3 &&
              (env[63 + 3] & 0x0f) == 6,
          "frame classes CONTENT_OBJECT ... INTEGRITY");
    CHECK(ipfsn_ubh_decode_cid(env, len, &d) == 0 && ipfsn_cid_equal(&c, &d),
          "CID envelope round-trip");
    uint32_t caught = 0;
    for (uint32_t i = 0; i < len; i++) {
        for (uint32_t bit = 0; bit < 8; bit++) {
            env[i] ^= (uint8_t) (1u << bit);
            if (ipfsn_ubh_decode_cid(env, len, &d) != 0) caught++;
            env[i] ^= (uint8_t) (1u << bit);
        }
    }
    CHECK(caught == len * 8, "every single-bit flip of the CID envelope rejected");
    CHECK(ipfsn_ubh_decode_cid(env, len - 21, &d) != 0 &&
              ipfsn_ubh_decode_cid(env, len - 1, &d) != 0,
          "truncated envelope rejected");
    /* Text form */
    int tl = ipfsn_ubh_cid_to_text(&c, txt, sizeof txt);
    CHECK(tl > 0 && !strncmp(txt, "ubh168:b", 8), "UBH text form prefix");
    CHECK(ipfsn_ubh_cid_from_text(txt, (uint32_t) tl, &d) == 0 && ipfsn_cid_equal(&c, &d),
          "UBH text round-trip");
    const char *plain = "bafkreifjjcie6lypi6ny7amxnfftagclbuxndqonfipmb64f2km2devei4";
    CHECK(ipfsn_ubh_cid_from_text(plain, (uint32_t) strlen(plain), &d) != 0 &&
              ipfsn_cid_parse(txt, (uint32_t) tl, &d) != 0,
          "UBH text and CID strings never confused");
    txt[tl - 1] = txt[tl - 1] == 'a' ? 'b' : 'a';
    CHECK(ipfsn_ubh_cid_from_text(txt, (uint32_t) tl, &d) != 0, "corrupted UBH text rejected");
    /* Blocks: every length 0..64 frames exactly and round-trips. */
    static uint8_t benv[IPFSN_UFS_CHUNK + 256];
    uint8_t data[64];
    gen_t g;
    gen_init(&g, 5);
    gen_fill(&g, data, 64);
    for (uint32_t n = 0; n <= 64; n++) {
        const uint8_t *b;
        uint32_t bl;
        ipfsn_cid_sha256(IPFSN_MC_RAW, data, n, &c);
        int r = ipfsn_ubh_encode_block(&c, data, n, benv, sizeof benv, &len);
        CHECK(r == 0 && len % 21 == 0 && len == ipfsn_ubh_size(36 + n), "block envelope size");
        CHECK(ipfsn_ubh_decode_block(benv, len, &d, &b, &bl) == 0 && ipfsn_cid_equal(&c, &d) &&
                  bl == n && memcmp(b, data, n) == 0,
              "block envelope round-trip");
        if ((36 + n) % 21) { /* there is padding: it must be zero */
            benv[len - 22] ^= 1;
            CHECK(ipfsn_ubh_decode_block(benv, len, &d, &b, &bl) != 0, "non-zero padding rejected");
            benv[len - 22] ^= 1;
        }
    }
    /* A full 256 KiB leaf: a lying envelope (valid frames, wrong bytes) is caught by the CID. */
    gen_init(&g, 6);
    gen_fill(&g, g_chunk, IPFSN_UFS_CHUNK);
    ipfsn_cid_sha256(IPFSN_MC_RAW, g_chunk, IPFSN_UFS_CHUNK, &c);
    ipfsn_ubh_encode_block(&c, g_chunk, IPFSN_UFS_CHUNK, benv, sizeof benv, &len);
    const uint8_t *b;
    uint32_t bl;
    CHECK(ipfsn_ubh_decode_block(benv, len, &d, &b, &bl) == 0 && bl == IPFSN_UFS_CHUNK,
          "256 KiB leaf over UBH-168");
    g_chunk[1000] ^= 1; /* re-frame different bytes under the same CID */
    ipfsn_ubh_encode_block(&c, g_chunk, IPFSN_UFS_CHUNK, benv, sizeof benv, &len);
    CHECK(ipfsn_ubh_decode_block(benv, len, &d, &b, &bl) == IPFSN_ERR_HASH,
          "well-framed but wrong bytes: CID check catches it");
    g_chunk[1000] ^= 1;
    /* Same CID either way */
    uint8_t plainbuf[64];
    const uint8_t *pb;
    uint32_t pl;
    ipfsn_cid_sha256(IPFSN_MC_RAW, data, 40, &c);
    CHECK(ipfsn_wire_encode_block(IPFSN_WIRE_PLAIN, &c, data, 40, plainbuf, sizeof plainbuf,
                                  &len) == 0 &&
              len == 40 &&
              ipfsn_wire_decode_block(IPFSN_WIRE_PLAIN, &c, plainbuf, len, &pb, &pl) == 0,
          "plain wire round-trip");
    CHECK(ipfsn_wire_encode_block(IPFSN_WIRE_UBH168, &c, data, 40, benv, sizeof benv, &len) == 0 &&
              ipfsn_wire_decode_block(IPFSN_WIRE_UBH168, &c, benv, len, &pb, &pl) == 0 &&
              pl == 40 && memcmp(pb, plainbuf, 40) == 0,
          "UBH wire round-trip carries the same block under the same CID");
    ipfsn_cid_sha256(IPFSN_MC_RAW, data, 41, &d);
    CHECK(ipfsn_wire_decode_block(IPFSN_WIRE_UBH168, &d, benv, len, &pb, &pl) == IPFSN_ERR_HASH,
          "UBH envelope for a different CID than wanted rejected");
    /* Negotiation */
    uint8_t ha[21], hb[21];
    ipfsn_wire_hello(ha);
    ipfsn_wire_hello(hb);
    CHECK(ipfsn_wire_negotiate(true, hb, 21) == IPFSN_WIRE_UBH168, "ZXV <-> ZXV: UBH-168");
    CHECK(ipfsn_wire_negotiate(true, 0, 0) == IPFSN_WIRE_PLAIN, "no hello: plain");
    CHECK(ipfsn_wire_negotiate(true, (const uint8_t *) "HTTP/1.1 200 OK\r\n\r\n\r\n", 21) ==
              IPFSN_WIRE_PLAIN,
          "gateway/HTTP: plain");
    CHECK(ipfsn_wire_negotiate(false, hb, 21) == IPFSN_WIRE_PLAIN, "UBH disabled locally: plain");
    hb[17] ^= 1; /* schema id */
    CHECK(ipfsn_wire_negotiate(true, hb, 21) == IPFSN_WIRE_PLAIN, "foreign UBH schema: plain");
    ipfsn_ubh_encode_cid(&c, env, sizeof env, &len);
    CHECK(ipfsn_wire_negotiate(true, env, 21) == IPFSN_WIRE_PLAIN,
          "a content frame is not a hello");
    CHECK(ipfsn_node_init(&g_a.node, &g_a.bs, g_a.pins, 32, g_net_a, 1) == 0 &&
              g_a.node.peer.wire == IPFSN_WIRE_UBH168,
          "node defaults to UBH-168 for peers");
}

/* ---- 5. File index ------------------------------------------------------------------ */

static void test_fidx(void)
{
    static ipfsn_fidx_entry_t ents[16];
    ipfsn_fidx_t x;
    ipfsn_cid_t c1, c2, c3, h3, got;
    uint64_t size;
    uint32_t vis;
    host_init(&g_a, g_store_a, g_scratch_a, g_net_a, "index key");
    ipfsn_fidx_init(&x, ents, 16);
    static uint8_t doc[300000];
    gen_t g;
    gen_init(&g, 77);
    gen_fill(&g, doc, sizeof doc);
    ipfsn_add(&g_a.node, &g_ufs, g_chunk, doc, sizeof doc, IPFSN_VIS_PUBLIC, false, &c1, 0);
    uint64_t used = g_a.ms.len;
    uint32_t blocks = g_a.bs.count;
    ipfsn_add(&g_a.node, &g_ufs, g_chunk, doc, sizeof doc, IPFSN_VIS_PUBLIC, false, &c2, 0);
    CHECK(ipfsn_cid_equal(&c1, &c2) && g_a.ms.len == used && g_a.bs.count == blocks,
          "same content twice: one CID, stored once");
    CHECK(ipfsn_fidx_set(&x, "/home/user/report.pdf", &c1, sizeof doc, IPFSN_VIS_PUBLIC) == 0 &&
              ipfsn_fidx_set(&x, "/zxvfs/report_copy", &c2, sizeof doc, IPFSN_VIS_PUBLIC) == 0,
          "index two paths");
    CHECK(ipfsn_fidx_refs(&x, &c1) == 2 && ipfsn_fidx_unique(&x) == 1, "dedup: 2 paths, 1 CID");
    uint32_t it = 0, n = 0;
    const char *p;
    while (ipfsn_fidx_next_path(&x, &c1, &it, &p) == 0) n++;
    CHECK(n == 2, "CID -> paths");
    CHECK(ipfsn_fidx_lookup(&x, "/zxvfs/report_copy", &got, &size, &vis) == 0 &&
              ipfsn_cid_equal(&got, &c1) && size == sizeof doc && vis == IPFSN_VIS_PUBLIC,
          "path -> CID");
    /* A private file: the index records its private CID, not the plaintext CID. */
    ipfsn_add(&g_a.node, &g_ufs, g_chunk, (const uint8_t *) "diary", 5, IPFSN_VIS_PRIVATE, true,
              &c3, &h3);
    CHECK(ipfsn_fidx_set(&x, "/home/user/diary.txt", &h3, 5, IPFSN_VIS_PRIVATE) == 0 &&
              ipfsn_fidx_lookup(&x, "/home/user/diary.txt", &got, 0, &vis) == 0 &&
              ipfsn_cid_equal(&got, &h3) && vis == IPFSN_VIS_PRIVATE &&
              ipfsn_is_private(&g_a.node, &got),
          "private file indexed by its private CID");
    uint8_t buf[64];
    uint32_t len;
    CHECK(ipfsn_bs_get(&g_a.bs, &got, buf, sizeof buf, &len) == 0 && len == 5 &&
              !memcmp(buf, "diary", 5),
          "open a private file from its index entry");
    /* Replace and remove */
    CHECK(ipfsn_fidx_set(&x, "/zxvfs/report_copy", &c3, 5, IPFSN_VIS_PUBLIC) == 0 &&
              ipfsn_fidx_refs(&x, &c1) == 1 && x.count == 3,
          "re-set replaces the mapping");
    CHECK(ipfsn_fidx_remove(&x, "/zxvfs/report_copy") == 0 &&
              ipfsn_fidx_lookup(&x, "/zxvfs/report_copy", 0, 0, 0) == IPFSN_ERR_NOTFOUND,
          "remove");
    static const char *bad[] = {"relative/path", "/a//b", "/a/./b", "/a/../b",
                                "/a/",           "",      "/..",    "/."};
    for (uint32_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK(ipfsn_fidx_set(&x, bad[i], &c1, 1, IPFSN_VIS_PUBLIC) == IPFSN_ERR_ARG,
              "non-canonical path refused");
    char longp[300];
    longp[0] = '/';
    memset(longp + 1, 'a', 256);
    longp[257] = 0;
    CHECK(ipfsn_fidx_set(&x, longp, &c1, 1, IPFSN_VIS_PUBLIC) == IPFSN_ERR_ARG,
          "path >= VFS_PATH_LEN refused");
    longp[255] = 0;
    CHECK(ipfsn_fidx_set(&x, longp, &c1, 1, IPFSN_VIS_PUBLIC) == 0, "255-byte path accepted");
    CHECK(ipfsn_fidx_set(&x, "/", &c1, 1, IPFSN_VIS_PUBLIC) == 0 &&
              ipfsn_fidx_set(&x, "/a..b", &c1, 1, IPFSN_VIS_PUBLIC) == 0,
          "'/' and '/a..b' are canonical");
}

/* ---- dag-pb strictness --------------------------------------------------------------- */

static void test_dagpb_strict(void)
{
    ipfsn_pbnode_t n;
    /* Kubo's root node for the size-64 vector, from the CAR */
    const uint8_t *root = KUBO_SMALL_CAR + 59 + 2 + 36;
    uint32_t rl = 227 - 36;
    CHECK(ipfsn_dagpb_parse(root, rl, &n) == 0 && n.nlinks == 4 && n.data_len == 13,
          "Kubo dag-pb node parses");
    static uint8_t m[256];
    memcpy(m, root, rl);
    /* Data before Links: move the Data field to the front */
    static uint8_t swapped[256];
    memcpy(swapped, root + rl - 15, 15);
    memcpy(swapped + 15, root, rl - 15);
    CHECK(ipfsn_dagpb_parse(swapped, rl, &n) == IPFSN_ERR_MALFORMED, "Data before Links rejected");
    m[0] = 0x1a; /* field 3: unknown */
    CHECK(ipfsn_dagpb_parse(m, rl, &n) == IPFSN_ERR_MALFORMED, "unknown PBNode field rejected");
    CHECK(ipfsn_dagpb_parse(root, rl - 1, &n) == IPFSN_ERR_MALFORMED, "truncated node rejected");
    static const uint8_t two_data[] = {0x0a, 0x00, 0x0a, 0x00};
    CHECK(ipfsn_dagpb_parse(two_data, 4, &n) == IPFSN_ERR_MALFORMED, "duplicate Data rejected");
    static const uint8_t empty_link[] = {0x12, 0x00};
    CHECK(ipfsn_dagpb_parse(empty_link, 2, &n) == IPFSN_ERR_MALFORMED,
          "link without Hash rejected");
    ipfsn_unixfs_t u;
    static const uint8_t ufs_bad_order[] = {0x18, 0x01, 0x08, 0x02};
    CHECK(ipfsn_unixfs_parse(ufs_bad_order, 4, &u) == IPFSN_ERR_MALFORMED,
          "UnixFS fields out of order");
    static const uint8_t ufs_notype[] = {0x18, 0x01};
    CHECK(ipfsn_unixfs_parse(ufs_notype, 2, &u) == IPFSN_ERR_MALFORMED, "UnixFS without Type");
    /* A node whose blocksizes lie about a child is caught by cat. */
    ipfsn_car_t car;
    static uint8_t t[sizeof KUBO_SMALL_CAR];
    memcpy(t, KUBO_SMALL_CAR, sizeof t);
    ipfsn_car_open(&car, t, sizeof t);
    /* Re-hash a modified root (blocksizes[0] 64 -> 63) and splice it into a fresh CAR */
    uint8_t node2[256];
    memcpy(node2, root, rl);
    node2[rl - 7] = 63; /* first blocksize */
    ipfsn_cid_t r2;
    ipfsn_cid_sha256(IPFSN_MC_DAG_PB, node2, rl, &r2);
    uint32_t w = (uint32_t) ipfsn_car_begin(&r2, g_car, sizeof g_car);
    w += (uint32_t) ipfsn_car_put(&r2, node2, rl, g_car + w, sizeof g_car - w);
    ipfsn_cid_t cid;
    const uint8_t *b;
    uint32_t len;
    car.pos = car.first;
    ipfsn_car_next(&car, &cid, &b, &len); /* skip the original root */
    while (ipfsn_car_next(&car, &cid, &b, &len) == 0)
        w += (uint32_t) ipfsn_car_put(&cid, b, len, g_car + w, sizeof g_car - w);
    ipfsn_car_t c2;
    hash_out_t o;
    memset(&o, 0, sizeof o);
    sha256_init(&o.h);
    CHECK(ipfsn_car_open(&c2, g_car, w) == 0 && ipfsn_cat(&g_walk, &r2, ipfsn_car_source, &c2,
                                                          hash_out, &o, 0) == IPFSN_ERR_MALFORMED,
          "blocksizes that disagree with the children rejected");
}

int main(void)
{
    test_varint();
    test_bases();
    test_known_cids();
    test_unixfs();
    test_walk();
    test_blockstore();
    test_private();
    test_car();
    test_dagpb_strict();
    test_gateway();
    test_peers();
    test_ubh();
    test_fidx();
    printf("test_ipfs_node: %d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
