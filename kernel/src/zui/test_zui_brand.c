/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_brand.c — the fox is vector and draws at any size (BR1..BR2). */
#include <stdio.h>
#include <string.h>

#include "zui_brand.h"
#include "zui_progress.h"
#include "zui_tokens.h"

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("FAIL %s:%d ", __FILE__, __LINE__);                                             \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static zui_vec_work_t W;
static uint32_t PX[512 * 512];

/* Count pixels close to gold and pixels left at the ground in a box. */
static void census(const zui_surface_t *s, zui_rect_t r, uint32_t *gold, uint32_t *ground)
{
    *gold = *ground = 0;
    for (int32_t y = r.y; y < r.y + r.h; y++)
        for (int32_t x = r.x; x < r.x + r.w; x++) {
            uint32_t p = s->px[y * s->stride + x] & 0xFFFFFFu;
            if (p == (ZUI_VOID & 0xFFFFFFu)) (*ground)++;
            int dr = (int) ((p >> 16) & 255) - 0xF2, dg = (int) ((p >> 8) & 255) - 0xC2,
                db = (int) (p & 255) - 0x30;
            if (dr * dr + dg * dg + db * db < 40 * 40) (*gold)++;
        }
}

static void fill(zui_surface_t *s)
{
    for (int32_t i = 0; i < s->stride * s->h; i++) s->px[i] = ZUI_VOID;
}

int main(void)
{
    uint32_t n = 0;
    const uint8_t *fox = zui_brand_fox(&n);
    CHECK(fox && n > 64 && !memcmp(fox, "ZXVG", 4), "the fox is a ZXVG blob");
    CHECK(zui_zxvg_check(fox, n) == 0, "and well formed");
    CHECK(zui_brand_fox(NULL) == fox, "len may be NULL");

    zui_surface_t s;
    zui_surface_init(&s, PX, 512, 512, 512);
    uint32_t prev_gold = 0;
    int32_t sizes[] = {32, 64, 128, 256, 512};
    for (uint32_t i = 0; i < 5; i++) {
        fill(&s);
        zui_rect_t box = {0, 0, sizes[i], sizes[i]};
        CHECK(zui_vec_draw_zxvg(&s, &W, fox, n, box, NULL) == 0, "draws at %d", sizes[i]);
        uint32_t g, gr, og, ogr;
        census(&s, box, &g, &gr);
        CHECK(g > 0 && gr > 0, "gold fox on the ground at %d (%u gold)", sizes[i], g);
        zui_rect_t all = {0, 0, 512, 512};
        census(&s, all, &og, &ogr);
        CHECK(og == g, "nothing outside the box at %d", sizes[i]);
        if (i)
            CHECK(g > prev_gold * 3 && g < prev_gold * 7, "area grows ~4x per doubling (%u -> %u)",
                  prev_gold, g);
        prev_gold = g;
    }

    /* BR2: on a launch screen, dim until lit. */
    zui_vec_mark_t m;
    zui_brand_fox_mark(&m, &W, 64);
    CHECK(m.data == fox && m.len == n && m.work == &W && m.dim == 64 && !m.ov && !m.nov,
          "mark context");
    uint32_t lit_gold, dim_gold, x;
    zui_rect_t box = {128, 128, 256, 256};
    fill(&s);
    zui_vec_mark(&s, box, 256, &m);
    census(&s, box, &lit_gold, &x);
    fill(&s);
    zui_vec_mark(&s, box, 0, &m);
    census(&s, box, &dim_gold, &x);
    CHECK(lit_gold > 1000 && dim_gold < lit_gold / 4, "lit %u vs dim %u", lit_gold, dim_gold);

    zui_launch_t L;
    memset(&L, 0, sizeof L);
    L.mark = zui_vec_mark;
    L.ctx[0] = &m;
    fill(&s);
    zui_rect_t area = {0, 0, 512, 512};
    zui_launch_state_t st = {256, 256, 256, 0};
    zui_launch_draw(&s, area, 256, &L, st, 0);
    uint32_t g2, gr2;
    census(&s, area, &g2, &gr2);
    CHECK(g2 > 100, "the launch screen draws the vector fox (%u)", g2);

    printf("[%s] test_zui_brand: %d/%d checks\n", fails ? "FAIL" : "PASS", checks - fails, checks);
    return fails != 0;
}
