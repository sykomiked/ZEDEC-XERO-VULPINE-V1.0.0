/* test_zxv_route.c — prove the lattice routing verifier fail-closed.
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sha256.h"
#include "zxv_route.h"
#include "zxv_lattice_route.h" /* generated: zxv_lattice_route[] + root */

static int checks;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (cond) {                                                                                \
            checks++;                                                                              \
        } else {                                                                                   \
            printf("[FAIL] %s:%d %s\n", __FILE__, __LINE__, #cond);                                \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

int main(void)
{
    int fails = 0;
    size_t bad;
    uint8_t *img = (uint8_t *) zxv_lattice_route;
    const size_t len = ZXV_LATTICE_ROUTE_LEN;
    const size_t rec0 = 4 + 1 + 3 + 4 + 4 + ZXV_ROUTE_WORLDS * 4;
    uint32_t nmods = ((uint32_t) img[12] << 24) | ((uint32_t) img[13] << 16) |
                     ((uint32_t) img[14] << 8) | img[15];
    uint8_t *cpy;
    bool ok;

    CHECK(zxv_route_verify(img, len, &bad) == ZXV_ROUTE_OK);

    /* every tamper class fails closed */
    cpy = malloc(len);
    memcpy(cpy, img, len);
    cpy[0] = 'z';
    CHECK(zxv_route_verify(cpy, len, &bad) == ZXV_ROUTE_E_MAGIC);
    memcpy(cpy, img, len);
    cpy[4] = 9;
    CHECK(zxv_route_verify(cpy, len, &bad) == ZXV_ROUTE_E_VERSION);
    memcpy(cpy, img, len);
    cpy[8] = 7; /* stratum count */
    CHECK(zxv_route_verify(cpy, len, &bad) == ZXV_ROUTE_E_COUNT);
    memcpy(cpy, img, len);
    cpy[len - 1] ^= 0xFF;
    CHECK(zxv_route_verify(cpy, len, &bad) == ZXV_ROUTE_E_DIGEST);
    /* record-level faults: tamper, then re-sign so the verifier reaches
     * the record walk */
    memcpy(cpy, img, len);
    cpy[rec0 + 6] = 200; /* i out of lattice */
    sha256(cpy, len - 32, cpy + len - 32);
    CHECK(zxv_route_verify(cpy, len, &bad) == ZXV_ROUTE_E_CELL && bad == rec0);
    memcpy(cpy, img, len);
    cpy[rec0 + 5] = (uint8_t) (cpy[rec0 + 5] + 1); /* ring lies about cell */
    sha256(cpy, len - 32, cpy + len - 32);
    CHECK(zxv_route_verify(cpy, len, &bad) == ZXV_ROUTE_E_RING && bad == rec0);
    memcpy(cpy, img, len);
    cpy[rec0 + 4] = 0xFE; /* stratum out of range */
    sha256(cpy, len - 32, cpy + len - 32);
    CHECK(zxv_route_verify(cpy, len, &bad) == ZXV_ROUTE_E_STRATUM && bad == rec0);
    /* truncated and extended images fail */
    CHECK(zxv_route_verify(img, len - 8, &bad) == ZXV_ROUTE_E_LENGTH);
    CHECK(zxv_route_verify(img, len + 8, &bad) != ZXV_ROUTE_OK);

    /* the digest the header commits equals the image's tail digest */
    CHECK(memcmp(zxv_lattice_route_root, img + len - 32, 32) == 0);

    /* every record sits on a unique cell whose ring matches */
    {
        uint16_t seen[144 * 144 / 16 + 1];
        size_t off;
        uint32_t n;
        memset(seen, 0, sizeof(seen));
        for (n = 0, off = rec0; n < nmods; n++, off += ZXV_ROUTE_REC) {
            uint8_t i = img[off + 6], j = img[off + 7];
            uint16_t c = (uint16_t) i * 144 + j;
            CHECK(zxv_route_ring(i, j) == img[off + 5]);
            CHECK(!(seen[c / 16] & (1u << (c % 16))));
            seen[c / 16] |= (uint16_t) (1u << (c % 16));
        }
    }

    /* the three worlds light distinct strata */
    CHECK(zxv_route_world_mask(img, len, 0, &ok) && ok);
    CHECK(zxv_route_world_mask(img, len, 1, &ok) != 0);
    CHECK(zxv_route_world_mask(img, len, 2, &ok) != 0);
    CHECK(zxv_route_world_mask(img, len, 3, &ok) == 0 && !ok);
    CHECK(zxv_route_world_mask(img, len, 0, &ok) != zxv_route_world_mask(img, len, 1, &ok));

    printf("zxv_route: %d checks passed (%u modules routed over the lattice)\n", checks, nmods);
    free(cpy);
    return fails ? 1 : 0;
}
