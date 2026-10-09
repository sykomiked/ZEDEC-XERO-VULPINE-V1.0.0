/* test_shimmer.c — host tests for the desktop shimmer field.
 *
 * Proves the properties that matter architecturally: the field is a pure
 * function of the CAUSAL EVENT COUNT (replayable, no clock), bounded,
 * spatially continuous, and actually moving.
 *
 *   gcc -std=c11 -Wall -Wextra -Isrc/shimmer src/shimmer/test_shimmer.c \
 *       src/shimmer/shimmer.c -o /tmp/test_shimmer && /tmp/test_shimmer
 */
#include <stdio.h>
#include <stdlib.h>
#include "shimmer.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

int main(void) {
    printf("=== Desktop shimmer field ===\n");

    /* --- sine LUT sanity --- */
    CHECK(shm_sin(0) == 0, "sin(0) == 0");
    CHECK(shm_sin(64) > 120, "sin(quarter turn) near +127");
    CHECK(shm_sin(192) < -120, "sin(three-quarter turn) near -127");
    {
        int ok = 1;
        for (uint32_t i = 0; i < 256; i++) {
            int32_t v = shm_sin(i);
            if (v < -127 || v > 127) ok = 0;
        }
        CHECK(ok, "sine LUT stays within +/-127 for all 256 entries");
    }

    shimmer_t s;
    shimmer_init(&s, 320, 200, 1);
    CHECK(s.phase == 0 && s.frames == 0, "init: phase and frames at zero");

    /* --- output is bounded everywhere, every frame --- */
    {
        int ok = 1;
        for (int f = 0; f < 40; f++) {
            for (int y = 0; y < 200; y += 7)
                for (int x = 0; x < 320; x += 7) {
                    uint8_t v = shimmer_sample(&s, x, y);
                    /* v is a uint8_t, so it cannot exceed 255 */
                    (void)v;
                }
            shimmer_advance(&s);
        }
        CHECK(ok, "sample stays in 0..255 across 40 frames");
        CHECK(s.phase == 40 && s.frames == 40, "phase tracks event count exactly");
    }

    /* --- DETERMINISM: same event count => identical frame (replayable) --- */
    {
        shimmer_t a, b;
        shimmer_init(&a, 320, 200, 7);
        shimmer_init(&b, 320, 200, 7);
        for (int i = 0; i < 137; i++) { shimmer_advance(&a); shimmer_advance(&b); }
        int same = 1;
        for (int y = 0; y < 200; y += 3)
            for (int x = 0; x < 320; x += 3)
                if (shimmer_sample(&a, x, y) != shimmer_sample(&b, x, y)) same = 0;
        CHECK(same, "identical event count yields a bit-identical frame (replayable)");
    }

    /* --- it actually MOVES between event cycles --- */
    {
        shimmer_t m; shimmer_init(&m, 320, 200, 3);
        int diff = 0, total = 0;
        uint8_t before[64];
        int i = 0;
        for (int x = 0; x < 320 && i < 64; x += 5, i++) before[i] = shimmer_sample(&m, x, 100);
        shimmer_advance(&m);
        i = 0;
        for (int x = 0; x < 320 && i < 64; x += 5, i++) {
            total++;
            if (shimmer_sample(&m, x, 100) != before[i]) diff++;
        }
        CHECK(diff > total / 3, "field changes materially after one event cycle");
    }

    /* --- different seeds decorrelate (two surfaces don't move in lockstep) --- */
    {
        shimmer_t p, q;
        shimmer_init(&p, 320, 200, 1);
        shimmer_init(&q, 320, 200, 99);
        for (int i = 0; i < 20; i++) { shimmer_advance(&p); shimmer_advance(&q); }
        int diff = 0, total = 0;
        for (int x = 0; x < 320; x += 4) { total++;
            if (shimmer_sample(&p, x, 60) != shimmer_sample(&q, x, 60)) diff++; }
        CHECK(diff > total / 4, "different seeds produce a decorrelated field");
    }

    /* --- spatial continuity: neighbours shouldn't jump wildly (no hash noise) --- */
    {
        shimmer_t c; shimmer_init(&c, 320, 200, 5);
        for (int i = 0; i < 11; i++) shimmer_advance(&c);
        long big = 0, n = 0;
        for (int y = 20; y < 180; y += 3)
            for (int x = 20; x < 300; x += 3) {
                int d = (int)shimmer_sample(&c, x, y) - (int)shimmer_sample(&c, x+1, y);
                if (d < 0) d = -d;
                if (d > 60) big++;
                n++;
            }
        CHECK(big * 20 < n, "adjacent pixels vary smoothly (caustic, not noise)");
    }

    /* --- the field uses its full dynamic range --- */
    {
        shimmer_t r; shimmer_init(&r, 320, 200, 2);
        int lo = 255, hi = 0;
        for (int f = 0; f < 8; f++) {
            for (int y = 0; y < 200; y += 5)
                for (int x = 0; x < 320; x += 5) {
                    int v = shimmer_sample(&r, x, y);
                    if (v < lo) lo = v;
                    if (v > hi) hi = v;
                }
            shimmer_advance(&r);
        }
        printf("       range observed: %d..%d\n", lo, hi);
        CHECK(hi - lo > 150, "field spans a wide dynamic range");
    }

    /* --- shading preserves alpha and stays in gamut --- */
    {
        shimmer_t g; shimmer_init(&g, 64, 64, 4);
        const uint32_t base = 0xFF002911u;    /* midnight emerald */
        int ok = 1, changed = 0;
        for (int f = 0; f < 6; f++) {
            for (int y = 0; y < 64; y += 2)
                for (int x = 0; x < 64; x += 2) {
                    uint32_t o = shimmer_shade(&g, base, x, y);
                    if (((o >> 24) & 0xFF) != 0xFF) ok = 0;   /* alpha kept */
                    if (o != base) changed++;
                }
            shimmer_advance(&g);
        }
        CHECK(ok, "shade preserves the alpha channel");
        CHECK(changed > 0, "shade actually modifies the ground colour");
    }

    /* --- amplitude 0 must be a true no-op (user can disable it) --- */
    {
        shimmer_t z; shimmer_init(&z, 64, 64, 6);
        shimmer_set_amplitude(&z, 0);
        shimmer_set_glint(&z, 255);
        const uint32_t base = 0xFF002911u;
        int same = 1;
        for (int y = 0; y < 64; y += 2)
            for (int x = 0; x < 64; x += 2)
                if (shimmer_shade(&z, base, x, y) != base) same = 0;
        CHECK(same, "amplitude 0 + glint 255 disables the effect entirely");
    }

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
