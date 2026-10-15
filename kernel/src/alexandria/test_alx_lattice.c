/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_alx_lattice.c — lattice geometry (alx_lattice.h): matrix words, and
 * that every store resolves every word to the same bytes. argv: the packed
 * matrix directory and its ALXI image (tools/alexandria/mkimage.py).
 *
 * The block-store fetch (blocks/<CID>, as a runtime or peer serves them)
 * and the image fetch (one file read by offset, as bare metal reads sectors
 * and a browser maps WASM memory) must give the same CID and bytes for every
 * layer, every booklet and every atom; a damaged image block is reported
 * with its CID and the block store gives the right one. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alx_lattice.h"
#include "alx_matrix.h"
#include "../robin_debanks/sha256.h"

static unsigned checks, fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("[FAIL] " __VA_ARGS__);                                                         \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static const char *DIR_;

static void cid_str(const uint8_t c[36], char out[64])
{
    static const char A[] = "abcdefghijklmnopqrstuvwxyz234567";
    uint32_t acc = 0, bits = 0, k = 0;
    out[k++] = 'b';
    for (int i = 0; i < 36; i++) {
        acc = acc << 8 | c[i];
        bits += 8;
        while (bits >= 5) out[k++] = A[(acc >> (bits -= 5)) & 31];
    }
    if (bits) out[k++] = A[(acc << (5 - bits)) & 31];
    out[k] = 0;
}

static uint8_t *slurp(const char *path, uint64_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(len ? (size_t) len : 1u);
    *n = fread(b, 1, (size_t) len, f);
    fclose(f);
    return b;
}

/* ---- the block store: blocks/<CID> and the root, cached ---- */

enum { CACHE = 1u << 14 };
static struct {
    uint8_t cid[36];
    uint8_t *b;
    uint32_t len;
} C[CACHE];
static uint8_t *ROOT;
static uint32_t ROOTN;
static uint8_t ROOTCID[36];

static const uint8_t *dir_fetch(void *ctx, const uint8_t cid[36], uint32_t *len)
{
    (void) ctx;
    if (!memcmp(cid, ROOTCID, 36)) {
        *len = ROOTN;
        return ROOT;
    }
    uint32_t h = ((uint32_t) cid[4] << 8 | cid[5]) % CACHE;
    for (uint32_t i = 0; i < CACHE; i++, h = (h + 1u) % CACHE) {
        if (C[h].b && !memcmp(C[h].cid, cid, 36)) {
            *len = C[h].len;
            return C[h].b;
        }
        if (!C[h].b) {
            char name[64], path[4096];
            cid_str(cid, name);
            snprintf(path, sizeof path, "%s/blocks/%s", DIR_, name);
            uint64_t n;
            uint8_t *b = slurp(path, &n);
            if (!b) return 0;
            memcpy(C[h].cid, cid, 36);
            C[h].b = b;
            C[h].len = (uint32_t) n;
            *len = (uint32_t) n;
            return b;
        }
    }
    return 0;
}

static const uint8_t *none_fetch(void *ctx, const uint8_t cid[36], uint32_t *len)
{
    (void) ctx, (void) cid, (void) len;
    return 0;
}

static uint8_t S1[1u << 20], S2[1u << 20];

static void test_words(void)
{
    uint64_t s = 7;
    for (uint32_t i = 0; i < 20000; i++) {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        uint32_t l = (uint32_t) (s >> 33) % ALX_MX_LAYERS, b = (uint32_t) (s >> 20) % 0xFFFFFFu,
                 a = (uint32_t) s % 0xFFFFFFu, L, B, A;
        uint64_t w = alx_lat_atom(l, b, a);
        CHECK(w && alx_lat_parse(w, &L, &B, &A) == ALX_LAT_ATOM && L == l && B == b && A == a,
              "atom word %u.%u.%u", l, b, a);
        w = alx_lat_booklet(l, b);
        CHECK(w && alx_lat_parse(w, &L, &B, &A) == ALX_LAT_BOOKLET && L == l && B == b,
              "booklet word %u.%u", l, b);
        w = alx_lat_layer(l);
        CHECK(w && alx_lat_parse(w, &L, 0, 0) == ALX_LAT_LAYER && L == l, "layer word %u", l);
    }
    /* the documented layout: 0x30, layer, booklet + 1, atom + 1 */
    CHECK(alx_lat_atom(7, 0, 0) == 0x3007000001000001ull, "word layout: %016llx",
          (unsigned long long) alx_lat_atom(7, 0, 0));
    CHECK(alx_lat_layer(144) == 0x3090000000000000ull &&
              alx_lat_booklet(0, 0) == 0x3000000001000000ull,
          "layer and booklet words");
    CHECK(alx_lat_layer(145) == 0 && alx_lat_booklet(1, ALX_LAT_MAXIDX + 1u) == 0 &&
              alx_lat_atom(1, 0, ALX_LAT_MAXIDX + 1u) == 0,
          "indexes out of range have no word");
    CHECK(alx_lat_parse(0x1002000100000001ull, 0, 0, 0) == ALX_E_DOMAIN,
          "a code word is not a matrix word");
    CHECK(alx_lat_parse(0x3091000000000000ull, 0, 0, 0) == ALX_E_FORM, "layer 145");
    CHECK(alx_lat_parse(0x3001000000000001ull, 0, 0, 0) == ALX_E_FORM, "an atom without a booklet");
    CHECK(alx_lat_parse(0x3001FFFFFF000000ull, 0, 0, 0) == ALX_E_FORM &&
              alx_lat_parse(0x3001000001FFFFFFull, 0, 0, 0) == ALX_E_FORM,
          "all-ones indexes");
}

static int same_hit(const alx_lat_hit_t *a, const alx_lat_hit_t *b)
{
    return a->kind == b->kind && !memcmp(a->cid, b->cid, 36) && a->len == b->len &&
           !memcmp(a->bytes, b->bytes, a->len);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        printf("usage: test_alx_lattice MATRIX-DIR IMAGE\n");
        return 2;
    }
    DIR_ = argv[1];
    test_words();
    char path[4096];
    snprintf(path, sizeof path, "%s/MATRIX.ROOT", DIR_);
    uint64_t n64;
    ROOT = slurp(path, &n64);
    if (!ROOT) return printf("[FAIL] no root\n"), 1;
    ROOTN = (uint32_t) n64;
    ROOTCID[0] = 1, ROOTCID[1] = 0x55, ROOTCID[2] = 0x12, ROOTCID[3] = 0x20;
    sha256(ROOT, ROOTN, ROOTCID + 4);
    uint64_t in;
    uint8_t *img = slurp(argv[2], &in);
    alx_img_t im;
    CHECK(img && alx_img_open(&im, img, in) == ALX_OK, "the image opens");
    if (!img || fails) return 1;
    CHECK(!memcmp(alx_img_root(&im), ROOTCID, 36), "the image names the root");
    uint32_t irn;
    const uint8_t *iroot = alx_img_fetch(&im, ROOTCID, &irn);
    CHECK(iroot && irn == ROOTN && !memcmp(iroot, ROOT, ROOTN), "the image holds the root");

    /* every layer, booklet and atom: the block store and the image agree */
    uint32_t layers = 0, booklets = 0, atoms = 0, empty = 0;
    for (uint32_t k = 0; k < ALX_MX_LAYERS; k++) {
        alx_lat_hit_t a, b;
        int ra = alx_lat_resolve(ROOT, ROOTN, ROOTCID, alx_lat_layer(k), dir_fetch, 0, S1,
                                 sizeof S1, &a);
        int rb = alx_lat_resolve(iroot, irn, ROOTCID, alx_lat_layer(k), alx_img_fetch, &im, S2,
                                 sizeof S2, &b);
        alx_mx_layer_t l;
        alx_mx_layer(ROOT, k, &l);
        if (!l.booklets) {
            CHECK(ra == ALX_E_NAME && rb == ALX_E_NAME, "empty layer %u has no tree", k);
            empty++;
            continue;
        }
        CHECK(ra == ALX_OK && rb == ALX_OK && same_hit(&a, &b) && !memcmp(a.cid, l.root, 36),
              "layer %u: the same tree root", k);
        layers++;
        for (uint32_t i = 0; i < l.booklets; i++) {
            ra = alx_lat_resolve(ROOT, ROOTN, ROOTCID, alx_lat_booklet(k, i), dir_fetch, 0, S1,
                                 sizeof S1, &a);
            rb = alx_lat_resolve(iroot, irn, ROOTCID, alx_lat_booklet(k, i), alx_img_fetch, &im, S2,
                                 sizeof S2, &b);
            CHECK(ra == ALX_OK && rb == ALX_OK && same_hit(&a, &b), "booklet %u.%u: %d %d", k, i,
                  ra, rb);
            if (ra != ALX_OK || rb != ALX_OK) continue;
            booklets++;
            uint32_t lay, na;
            alx_bk2_check(a.bytes, a.len, &lay, &na);
            /* the first, middle and last atoms, and one past the end (every
             * atom of every booklet is rebuilt by test_alx_matrix) */
            uint32_t pick[4] = {0, na / 2u, na - 1u, na};
            for (uint32_t pi = 0; pi < 4; pi++) {
                uint32_t j = pick[pi];
                if (pi && j < na && j == pick[pi - 1]) continue;
                alx_lat_hit_t x, y;
                ra = alx_lat_resolve(ROOT, ROOTN, ROOTCID, alx_lat_atom(k, i, j), dir_fetch, 0, S1,
                                     sizeof S1, &x);
                rb = alx_lat_resolve(iroot, irn, ROOTCID, alx_lat_atom(k, i, j), alx_img_fetch, &im,
                                     S2, sizeof S2, &y);
                if (j == na) { /* one past the last atom does not exist */
                    CHECK(ra == ALX_E_NAME && rb == ALX_E_NAME, "atom %u.%u.%u past the end", k, i,
                          j);
                    break;
                }
                uint8_t h[32];
                sha256(x.bytes, x.len, h);
                CHECK(ra == ALX_OK && rb == ALX_OK && same_hit(&x, &y) && !memcmp(h, x.cid + 4, 32),
                      "atom %u.%u.%u", k, i, j);
                atoms++;
            }
        }
        alx_lat_hit_t x;
        CHECK(alx_lat_resolve(ROOT, ROOTN, ROOTCID, alx_lat_booklet(k, l.booklets), dir_fetch, 0,
                              S1, sizeof S1, &x) == ALX_E_NAME,
              "layer %u: no booklet past the end", k);

        /* the glue's refs are words: a file is its refs' atoms, in order */
        uint32_t gn, files, edges, refs, lay;
        const uint8_t *gb = dir_fetch(0, l.glue, &gn);
        if (gb && alx_glue_check(gb, gn, &lay, &files, &edges, &refs) == ALX_OK && files) {
            uint32_t f = files / 2u, nr = alx_glue_nrefs(gb, f);
            sha256_ctx_t c;
            sha256_init(&c);
            int ok = 1;
            for (uint32_t r = 0; r < nr && ok; r++) {
                uint32_t L, B, A;
                alx_glue_ref(gb, f, r, &L, &B, &A);
                ok = alx_lat_resolve(iroot, irn, ROOTCID, alx_lat_atom(L, B, A), alx_img_fetch, &im,
                                     S2, sizeof S2, &x) == ALX_OK;
                if (ok) sha256_update(&c, x.bytes, x.len);
            }
            uint8_t h[32];
            sha256_final(&c, h);
            CHECK(ok && !memcmp(h, gb + 64u + f * ALX_MX_FILE + 4, 32),
                  "layer %u: a file rebuilt from its words is the file its glue names", k);
        }
    }
    CHECK(layers > 60 && booklets > 100 && atoms > 1000, "%u layers, %u booklets, %u atoms", layers,
          booklets, atoms);

    /* a damaged image block is caught and named; the block store heals it */
    {
        alx_mx_layer_t l;
        alx_mx_layer(ROOT, 0, &l);
        alx_lat_hit_t good, bad;
        CHECK(alx_lat_resolve(ROOT, ROOTN, ROOTCID, alx_lat_booklet(0, 0), dir_fetch, 0, S1,
                              sizeof S1, &good) == ALX_OK,
              "booklet 000");
        /* the stored block the tree names: find it in the image and flip one byte */
        alx_lat_hit_t top;
        alx_lat_resolve(ROOT, ROOTN, ROOTCID, alx_lat_layer(0), dir_fetch, 0, S1, sizeof S1, &top);
        uint32_t bn;
        const uint8_t *child = alx_node_child(top.bytes, 0); /* depth 1: booklet 0 */
        const uint8_t *blk = alx_img_fetch(&im, child, &bn);
        CHECK(blk != 0, "booklet 000's block is in the image");
        if (blk) {
            uint8_t *m = (uint8_t *) blk + bn / 2u;
            *m ^= 0x10;
            int r = alx_lat_resolve(iroot, irn, ROOTCID, alx_lat_booklet(0, 0), alx_img_fetch, &im,
                                    S2, sizeof S2, &bad);
            CHECK(r == ALX_E_HASH && !memcmp(bad.cid, child, 36), "a damaged block is named: %d",
                  r);
            CHECK(alx_lat_resolve(iroot, irn, ROOTCID, alx_lat_booklet(0, 0), dir_fetch, 0, S2,
                                  sizeof S2, &bad) == ALX_OK &&
                      same_hit(&bad, &good),
                  "fetched again by its CID, it is whole");
            *m ^= 0x10;
        }
        int r = alx_lat_resolve(ROOT, ROOTN, ROOTCID, alx_lat_atom(0, 0, 0), none_fetch, 0, S1,
                                sizeof S1, &bad);
        CHECK(r == ALX_E_NAME && !memcmp(bad.cid, l.root, 36), "a missing block is named: %d", r);
        uint8_t other[36];
        memcpy(other, ROOTCID, 36);
        other[35] ^= 1;
        CHECK(alx_lat_resolve(ROOT, ROOTN, other, alx_lat_layer(0), dir_fetch, 0, S1, sizeof S1,
                              &bad) == ALX_E_HASH,
              "another root CID");
        CHECK(alx_lat_resolve(ROOT, ROOTN, ROOTCID, alx_lat_layer(0), dir_fetch, 0, S1, sizeof S1,
                              0) == ALX_E_FORM &&
                  alx_lat_resolve(ROOT, ROOTN, ROOTCID, 0x1002000100000001ull, dir_fetch, 0, S1,
                                  sizeof S1, &bad) == ALX_E_DOMAIN,
              "refusals");
    }
    /* image refusals */
    {
        alx_img_t j;
        uint8_t save[48];
        CHECK(alx_img_open(&j, img, 63) == ALX_E_FORM, "a short image");
        img[3] = 'X';
        CHECK(alx_img_open(&j, img, in) == ALX_E_FORM, "magic");
        img[3] = 'I';
        if (im.n > 2) { /* two entries swapped: not in CID order */
            memcpy(save, img + 64, 48);
            memcpy(img + 64, img + 112, 48);
            memcpy(img + 112, save, 48);
            CHECK(alx_img_open(&j, img, in) == ALX_E_FORM, "a table out of order");
            memcpy(img + 112, img + 64, 48);
            memcpy(img + 64, save, 48);
        }
        const uint8_t *e = img + 64u + (uint64_t) (im.n - 1u) * ALX_IMG_ENTRY;
        uint64_t end =
            ((uint64_t) e[36] << 24 | (uint64_t) e[37] << 16 | (uint64_t) e[38] << 8 | e[39]) *
                ALX_IMG_SECTOR +
            ((uint64_t) e[40] << 24 | (uint64_t) e[41] << 16 | (uint64_t) e[42] << 8 | e[43]);
        CHECK(alx_img_open(&j, img, end) == ALX_OK && alx_img_open(&j, img, end - 1u) == ALX_E_FORM,
              "a block past the end");
        CHECK(alx_img_open(&j, img, in) == ALX_OK, "the image is whole again");
    }
    printf("lattice: %u layers (%u empty), %u booklets, %u atoms sampled: block store and image "
           "agree\n",
           layers, empty, booklets, atoms);
    for (uint32_t i = 0; i < CACHE; i++) free(C[i].b);
    free(ROOT);
    free(img);
    printf("%u checks, %u failures\n", checks, fails);
    return fails != 0;
}
