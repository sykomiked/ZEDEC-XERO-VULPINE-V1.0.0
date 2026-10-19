/* zxv_route.c — verify the committed lattice interconnect table.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * The generator's record layout (see build_system/gen_lattice_routing.py):
 *
 *   0  "ZLR0"          magic
 *   4  u8 version      ZXV_ROUTE_VERSION
 *   5  u8 flags[3]     reserved, must be zero
 *   8  u32 strata      stratum count (ZXV_ROUTE_STRATA)
 *  12  u32 modules     module record count
 *  16  u32 worlds[3]  the server/user/foundry stratum masks
 *  28  rec[modules]   u32 name_hash | u8 stratum | u8 ring | u8 i | u8 j |
 *                      u8 ndep | u8 pad
 *  --  sha256         digest of every byte before it
 *
 * The verifier walks the image once: header fields, sha256, then each
 * record — cell in-bounds, ring consistent with the cell, cell unique, no
 * dependency at or above the record's ring, stratum in range, world masks
 * only lighting strata that carry modules. Fail-closed on the first fault.
 */
#include "zxv_route.h"

#include <string.h>

#include "sha256.h"

/* the distinct ladder, same as the scaffold kernels' zk_d_fib */
static const uint16_t FIB[ZXV_ROUTE_RINGS + 1] = {0, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144};

uint8_t zxv_route_ring(uint8_t i, uint8_t j)
{
    uint16_t m = i > j ? i : j;
    uint8_t k;
    for (k = 1; k <= ZXV_ROUTE_RINGS; k++)
        if (m < FIB[k]) return k;
    return 0;
}

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) |
           (uint32_t) p[3];
}

zxv_route_err_t zxv_route_verify(const uint8_t *img, size_t len, size_t *bad_off)
{
    static const size_t HDR = 4 + 1 + 3 + 4 + 4 + ZXV_ROUTE_WORLDS * 4;
    uint32_t nstrata, nmods, w;
    size_t want, off;
    uint8_t digest[SHA256_DIGEST_LEN];
    uint16_t occ[144 * 144 / 16 + 1]; /* occupancy bitmap, 16 cells/word */
    uint32_t strata_seen = 0;

    if (bad_off) *bad_off = ~0u;

    if (!img || len < HDR + SHA256_DIGEST_LEN + 1) return ZXV_ROUTE_E_LENGTH;
    if (img[0] != ZXV_ROUTE_MAGIC0 || img[1] != ZXV_ROUTE_MAGIC1 || img[2] != ZXV_ROUTE_MAGIC2 ||
        img[3] != ZXV_ROUTE_MAGIC3)
        return ZXV_ROUTE_E_MAGIC;
    if (img[4] != ZXV_ROUTE_VERSION || img[5] || img[6] || img[7]) return ZXV_ROUTE_E_VERSION;

    nstrata = rd32(img + 8);
    nmods = rd32(img + 12);
    if (nstrata != ZXV_ROUTE_STRATA || nmods < 1 || nmods > 144u * 144u) return ZXV_ROUTE_E_COUNT;

    want = HDR + (size_t) nmods * ZXV_ROUTE_REC + SHA256_DIGEST_LEN;
    if (len != want) return ZXV_ROUTE_E_LENGTH;

    sha256(img, len - SHA256_DIGEST_LEN, digest);
    if (memcmp(digest, img + len - SHA256_DIGEST_LEN, SHA256_DIGEST_LEN)) return ZXV_ROUTE_E_DIGEST;

    memset(occ, 0, sizeof(occ));
    off = HDR;
    for (uint32_t n = 0; n < nmods; n++, off += ZXV_ROUTE_REC) {
        uint8_t st = img[off + 4], ring = img[off + 5];
        uint8_t i = img[off + 6], j = img[off + 7], ndep = img[off + 8];
        uint16_t cell = (uint16_t) i * 144u + j;
        uint8_t want_ring;

        if (st >= nstrata)
            return bad_off ? (*bad_off = off, ZXV_ROUTE_E_STRATUM) : ZXV_ROUTE_E_STRATUM;
        strata_seen |= 1u << st;
        if (i >= 144 || j >= 144)
            return bad_off ? (*bad_off = off, ZXV_ROUTE_E_CELL) : ZXV_ROUTE_E_CELL;
        want_ring = zxv_route_ring(i, j);
        if (ring != want_ring)
            return bad_off ? (*bad_off = off, ZXV_ROUTE_E_RING) : ZXV_ROUTE_E_RING;
        if (occ[cell / 16] & (uint16_t) (1u << (cell % 16)))
            return bad_off ? (*bad_off = off, ZXV_ROUTE_E_DUP) : ZXV_ROUTE_E_DUP;
        occ[cell / 16] |= (uint16_t) (1u << (cell % 16));
        /* the scaffold's predecessor rule: a cell in ring k may hold at most
         * one dependency per axis whose coordinate reaches the ring floor;
         * every dependency lives in ring < k. Ring 1 holds none. */
        if (ring <= 1 && ndep) return bad_off ? (*bad_off = off, ZXV_ROUTE_E_DEP) : ZXV_ROUTE_E_DEP;
        {
            uint8_t cap = 0;
            if (i >= FIB[ring - 1]) cap++;
            if (j >= FIB[ring - 1]) cap++;
            if (ndep > cap) return bad_off ? (*bad_off = off, ZXV_ROUTE_E_DEP) : ZXV_ROUTE_E_DEP;
        }
    }

    /* a world may only light strata that carry modules */
    for (w = 0; w < ZXV_ROUTE_WORLDS; w++)
        if (rd32(img + 16 + w * 4) & ~strata_seen & 0x1FFu) return ZXV_ROUTE_E_WORLD;

    return ZXV_ROUTE_OK;
}

uint16_t zxv_route_world_mask(const uint8_t *img, size_t len, uint8_t world, bool *ok)
{
    static const size_t HDR = 4 + 1 + 3 + 4 + 4 + ZXV_ROUTE_WORLDS * 4;
    if (ok) *ok = false;
    if (!img || world >= ZXV_ROUTE_WORLDS || len < HDR) return 0;
    if (ok) *ok = true;
    return (uint16_t) (rd32(img + 16 + world * 4) & 0x1FFu);
}

bool zxv_route_lit(const uint8_t *img, size_t len, size_t off, uint8_t world)
{
    bool ok = false;
    uint16_t mask = zxv_route_world_mask(img, len, world, &ok);
    if (!ok || off + 5 > len) return false;
    return !!(mask & (1u << img[off + 4]));
}
