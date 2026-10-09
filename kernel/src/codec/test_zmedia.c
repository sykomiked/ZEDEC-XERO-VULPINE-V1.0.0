/* test_zmedia.c — Tri-Space media codec tests.
 *
 * The headline test is the thesis: decode S+ and you get a LOSSY image; apply
 * S- and you get the ORIGINAL BACK, byte for byte, with the restoration PROVEN
 * by the inverse-witness verifier rather than asserted.
 *
 * Build/run:
 *   gcc -std=c11 -Wall -Wextra -Werror -Isrc/codec src/codec/test_zmedia.c \
 *       src/codec/zmedia.c src/invproof/invproof.c src/robin_debanks/sha256.c \
 *       -o /tmp/test_zm && /tmp/test_zm
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 */
#include <stdio.h>
#include <string.h>
#include "zmedia.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  [FAIL] %s\n", msg); failures++; } \
    else         { printf("  [PASS] %s\n", msg); } } while (0)

#define W 64u
#define H 64u
#define N (W * H)

static uint8_t orig[N], lossy[N], scratch[N];
static uint8_t sp[N * 4], sm[N * 4], s0[256];

/* a picture with both smooth gradients and hard edges, so the quantiser has
 * something real to lose */
static void make_image(void) {
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++) {
            uint32_t v = (x * 4 + y * 2) & 0xFF;
            if (((x / 8) + (y / 8)) & 1) v = (v + 96) & 0xFF;
            if (x > 40 && x < 48 && y > 20 && y < 44) v = 250;
            orig[y * W + x] = (uint8_t)v;
        }
}

static uint32_t maxerr(const uint8_t *a, const uint8_t *b, uint32_t n) {
    uint32_t m = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t d = a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
        if (d > m) m = d;
    }
    return m;
}

int main(void) {
    printf("Tri-Space media codec\n");
    make_image();

    printf("S+ : the lossy transform\n");
    int spn = zm_encode_positive(orig, W, H, 50, sp, sizeof(sp));
    CHECK(spn > 0, "encodes");
    CHECK((uint32_t)spn < N, "S+ is SMALLER than the raw image (real compression)");
    CHECK(zm_decode_positive(sp, (uint32_t)spn, lossy, W, H) == ZM_OK, "decodes");
    uint32_t err = maxerr(orig, lossy, N);
    CHECK(err > 0, "the reconstruction is genuinely LOSSY (information was dropped)");
    CHECK(err < 120, "yet it is recognisably the same image, not noise");
    printf("       (S+ %d bytes vs %u raw, max pixel error %u)\n", spn, N, err);

    printf("S- : the discarded remainder, as an inverse witness\n");
    int smn = zm_build_negative(orig, lossy, N, sm, sizeof(sm));
    CHECK(smn > 0, "S- builds");
    CHECK(zm_restore_exact(lossy, N, sm, (uint32_t)smn, scratch, N) == ZM_OK,
          "S- restores the original, and the restoration VERIFIES");
    CHECK(memcmp(lossy, orig, N) == 0,
          "the result is BYTE-EXACT: lossy + S- == lossless");

    printf("the S- must be about THIS image\n");
    {   uint8_t other[N];
        for (uint32_t i = 0; i < N; i++) other[i] = (uint8_t)(i * 7);
        CHECK(zm_restore_exact(other, N, sm, (uint32_t)smn, scratch, N)
                  == ZM_ERR_NOT_EXACT,
              "an S- lifted from another image is REFUSED, not applied"); }

    printf("quality governs how much lands in S-\n");
    {   uint8_t hi_sp[N * 4], hi_sm[N * 4], hi_px[N];
        int a = zm_encode_positive(orig, W, H, 95, hi_sp, sizeof(hi_sp));
        zm_decode_positive(hi_sp, (uint32_t)a, hi_px, W, H);
        int b = zm_build_negative(orig, hi_px, N, hi_sm, sizeof(hi_sm));
        CHECK(a > spn, "higher quality => a LARGER S+ (more detail retained)");
        /* NOT "higher quality => smaller S-". Measured: at q95 the residual is
         * a few isolated +/-1 errors SCATTERED across the frame, and a sparse
         * run encoding pays a run header per island, so a smaller total error
         * can cost MORE bytes than the clustered residual at q50. What is
         * conserved is the RECONSTRUCTION, not the size. */
        CHECK(b > 0, "S- builds at high quality too (scattered residual)");
        printf("       (q50: S+ %d S- %d   |   q95: S+ %d S- %d)\n", spn, smn, a, b);
        printf("       (note: S- is cheap for CLUSTERED error, dear for SCATTERED)\n");
        /* the conservation the whole design rests on */
        CHECK(zm_restore_exact(hi_px, N, hi_sm, (uint32_t)b, scratch, N) == ZM_OK &&
              memcmp(hi_px, orig, N) == 0,
              "at EVERY quality the triad reconstructs the original exactly"); }

    printf("S0 : what was not decided\n");
    {   int n = zm_build_neutral(W, H, 50, s0, sizeof(s0));
        CHECK(n > 0, "S0 builds");
        s0[n] = 0;
        CHECK(strstr((char *)s0, "unresolved: colour-space") != NULL,
              "S0 declares colour space UNRESOLVED rather than assuming one");
        CHECK(strstr((char *)s0, "resolved-by: signed-policy") != NULL,
              "S0 names what would resolve it"); }

    printf("format discipline\n");
    {   uint8_t bad[64]; memset(bad, 0xAB, sizeof(bad));
        CHECK(zm_decode_positive(bad, sizeof(bad), lossy, W, H) == ZM_ERR_FORMAT,
              "foreign data is refused, not decoded as garbage");
        CHECK(zm_decode_positive(sp, (uint32_t)spn / 2, lossy, W, H) != ZM_OK,
              "a truncated stream is refused");
        CHECK(zm_encode_positive(orig, 63, 64, 50, sp, sizeof(sp)) == ZM_ERR_SIZE,
              "non-8-aligned dimensions are refused (no silent cropping)"); }

    printf("\n%s zmedia: %d failure(s)\n", failures ? "FAILED" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
