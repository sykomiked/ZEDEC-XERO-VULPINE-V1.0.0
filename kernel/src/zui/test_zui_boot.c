/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_boot.c — the OS boot screen: real pace, its sounds with it, the
 * vault only when ready. */
#include <stdio.h>
#include <string.h>

#include "zui_boot.h"
#include "zui_tokens.h"

#include "brand/fox_zxvg.inc" /* a well-formed ZXVG to stand in for a logo */

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL (%s:%d) ", __FILE__, __LINE__);                                         \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

#define W 640
#define H 400
static uint32_t PX[W * H], FACE[W * H], BEHIND[W * H], PX2[W * H];
static zui_surface_t S, F, B, S2;
static zui_boot_t BOOT, BOOT2;
static int16_t AUD[48000 * 2 * 2];

static void surfaces(void)
{
    zui_surface_init(&S, PX, W, H, W);
    zui_surface_init(&F, FACE, W, H, W);
    zui_surface_init(&B, BEHIND, W, H, W);
    zui_surface_init(&S2, PX2, W, H, W);
    for (uint32_t i = 0; i < W * H; i++) BEHIND[i] = 0x123456u;
}

static int32_t audio_peak(zui_boot_t *b, uint32_t frames)
{
    int32_t m = 0;
    zui_boot_audio(b, AUD, frames);
    for (uint32_t i = 0; i < 2 * frames; i++) {
        int32_t v = AUD[i] < 0 ? -AUD[i] : AUD[i];
        if (v > m) m = v;
    }
    return m;
}

static void frame(zui_boot_t *b, zui_surface_t *s)
{
    zui_boot_draw(b, s, &F, &B);
    zui_boot_tick(b, 16);
}

static void test_pace(void)
{
    surfaces();
    zui_boot_init(&BOOT, 0, false);
    for (uint32_t i = 0; i < ZUI_BOOT_LOGOS; i++)
        CHECK(zui_boot_logo(&BOOT, i, zxv_fox, sizeof zxv_fox) == 0, "B1: logo %u handed over", i);
    CHECK(zui_boot_phase(&BOOT) == ZUI_BOOT_LOADING && zui_boot_progress(&BOOT) == 0,
          "starts loading at 0");
    CHECK(audio_peak(&BOOT, 4800) == 0, "B3: nothing sounds before the screen shows");
    frame(&BOOT, &S);
    CHECK(audio_peak(&BOOT, 9600) > 500, "B3: the chime as the screen first shows");
    CHECK(PX[5 * W + 5] == ZUI_VOID, "B1: the ground is #002911");

    /* B2: the total not known yet: the heads breathe, nothing moves */
    CHECK(zui_boot_load(&BOOT, 0, 0) == 0 && zui_boot_progress(&BOOT) == 0,
          "B2: unknown total (%u)", zui_boot_progress(&BOOT));
    CHECK(zui_boot_load(&BOOT, 5, 0) == -1, "B2: work done of an unknown total is refused");

    /* B2: real pace; the tiles light outer pair first, the crest last */
    uint32_t last = 0;
    bool rising = true, paired = true, crest_last = true;
    for (uint64_t d = 0; d <= 1000000; d += 50000) {
        CHECK(zui_boot_load(&BOOT, d, 1000000) == 0, "B2: %llu of 1000000", (unsigned long long) d);
        for (int k = 0; k < 3; k++) {
            frame(&BOOT, &S);
            uint32_t lit = BOOT.launch.lit;
            if ((lit & 1u) != (lit >> 4 & 1u) || (lit >> 1 & 1u) != (lit >> 3 & 1u)) paired = false;
            if ((lit >> 2 & 1u) && (lit & 0x1Bu) != 0x1Bu) crest_last = false;
        }
        uint32_t p = zui_boot_progress(&BOOT);
        if (p < last) rising = false;
        last = p;
    }
    CHECK(rising && last < 256u, "B2: the screen only rises, never to the end on its own (%u)",
          last);
    CHECK(BOOT.launch.lit == 0x1Fu && crest_last, "B2: every tile lit, the crest last");
    CHECK(paired, "B2: the tiles light in pairs from the outside in");
    CHECK(zui_boot_load(&BOOT, 900000, 1000000) == -1, "B2: going backwards is refused");
    CHECK(zui_boot_load(&BOOT, 1000000, 2000000) == -1, "B2: a changed total is refused");
    CHECK(zui_boot_load(&BOOT, 1000001, 1000000) == -1, "B2: past the total is refused");

    /* B3: the coins rang as tiles lit; once they die away, quiet until ready */
    audio_peak(&BOOT, 48000 * 2);
    audio_peak(&BOOT, 48000 * 2);
    CHECK(audio_peak(&BOOT, 4800) < 64, "B3: no gong before ready");
    CHECK(zui_boot_phase(&BOOT) == ZUI_BOOT_LOADING, "still loading");

    /* B4: ready: the gong, the doors */
    zui_boot_ready(&BOOT);
    CHECK(zui_boot_progress(&BOOT) == 256u && zui_boot_phase(&BOOT) == ZUI_BOOT_OPENING,
          "B4: ready fills the bar and opens");
    CHECK(audio_peak(&BOOT, 9600) > 1000, "B4: the gong sounds");
    CHECK(zui_boot_load(&BOOT, 1000000, 1000000) == -1, "B4: no more load after ready");
    frame(&BOOT, &S);
    for (uint32_t t = 0; t < ZUI_BOOT_OPEN_MS / 2; t += 16) zui_boot_tick(&BOOT, 16);
    zui_boot_draw(&BOOT, &S, &F, &B);
    CHECK(
        (PX[(H / 2) * W + W / 2] & 0xFFu) > 0x20u && PX[(H / 2) * W + 2] != 0x123456u,
        "B4: half way, the seam has parted onto what is behind; the edges still the doors (%x %x)",
        PX[(H / 2) * W + W / 2], PX[(H / 2) * W + 2]);
    for (uint32_t t = 0; t < ZUI_BOOT_OPEN_MS; t += 16) zui_boot_tick(&BOOT, 16);
    zui_boot_draw(&BOOT, &S, &F, &B);
    bool all = true;
    for (uint32_t i = 0; i < W * H; i++)
        if (PX[i] != 0x123456u) all = false;
    CHECK(zui_boot_phase(&BOOT) == ZUI_BOOT_DONE && all, "B4: open, only what is behind shows");
}

static void test_fail_and_motion(void)
{
    surfaces();
    zui_boot_init(&BOOT, 0, false);
    frame(&BOOT, &S);
    zui_boot_load(&BOOT, 300, 1000);
    for (int k = 0; k < 4; k++) frame(&BOOT, &S);
    uint32_t p = zui_boot_progress(&BOOT);
    zui_boot_fail(&BOOT);
    zui_boot_ready(&BOOT);
    CHECK(zui_boot_phase(&BOOT) == ZUI_BOOT_FAILED && zui_boot_progress(&BOOT) == p,
          "B5: failed stays where the load stopped; ready is refused");
    audio_peak(&BOOT, 48000 * 2);
    audio_peak(&BOOT, 48000 * 2);
    CHECK(audio_peak(&BOOT, 48000) < 64, "B5: no gong after a failure");
    CHECK(zui_boot_load(&BOOT, 400, 1000) == -1, "B5: and no more load");

    /* reduced motion: the doors open at once */
    zui_boot_init(&BOOT, 0, true);
    frame(&BOOT, &S);
    zui_boot_ready(&BOOT);
    CHECK(zui_boot_phase(&BOOT) == ZUI_BOOT_DONE, "B4: reduced motion opens at once");

    /* sound off: nothing at all, the screen the same */
    zxl_prefs_t quiet = zxl_prefs_default();
    quiet.sounds = 0;
    zui_boot_init(&BOOT, &quiet, false);
    frame(&BOOT, &S);
    zui_boot_load(&BOOT, 1000, 1000);
    for (int k = 0; k < 8; k++) frame(&BOOT, &S);
    zui_boot_ready(&BOOT);
    CHECK(audio_peak(&BOOT, 48000) == 0, "B3: with sounds off nothing sounds");
}

static void test_logos(void)
{
    surfaces();
    static uint8_t bad[64];
    zui_boot_init(&BOOT, 0, false);
    CHECK(zui_boot_logo(&BOOT, 5, zxv_fox, sizeof zxv_fox) == -1 &&
              zui_boot_logo(&BOOT, 0, bad, sizeof bad) == -1 &&
              zui_boot_logo(0, 0, zxv_fox, 9) == -1,
          "B1: a bad index or a malformed file is refused");
    /* an empty tile against one with its logo, everything else equal */
    zui_boot_init(&BOOT2, 0, false);
    zui_boot_logo(&BOOT2, ZUI_BOOT_ASCW, zxv_fox, sizeof zxv_fox);
    zui_boot_draw(&BOOT, &S, &F, &B);
    zui_boot_draw(&BOOT2, &S2, &F, &B);
    CHECK(memcmp(PX, PX2, sizeof PX) != 0, "B1: a handed-over logo shows on its tile");
    uint32_t diff_l = 0, diff_c = 0;
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++)
            if (PX[y * W + x] != PX2[y * W + x]) {
                if (x < W / 3) diff_l++;
                if (x > W * 2 / 5 && x < W * 3 / 5) diff_c++;
            }
    CHECK(diff_c > 0 && diff_l == 0, "B1: only the crest's tile changed (%u, %u)", diff_c, diff_l);
    /* the same calls, the same pixels */
    zui_boot_init(&BOOT, 0, false);
    zui_boot_logo(&BOOT, ZUI_BOOT_ASCW, zxv_fox, sizeof zxv_fox);
    zui_boot_draw(&BOOT, &S, &F, &B);
    CHECK(memcmp(PX, PX2, sizeof PX) == 0, "deterministic");
}

int main(void)
{
    printf("=== zui_boot: the OS boot screen at the pace of the real load ===\n");
    test_pace();
    test_fail_and_motion();
    test_logos();
    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
