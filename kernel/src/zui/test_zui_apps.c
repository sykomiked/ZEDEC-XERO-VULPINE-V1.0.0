/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_apps.c — the app catalogue and icon pack (zui_apps.h AP1..AP4).
 * Reads the real pack: argv[1], else assets/icons/apps/zxv-apps.zxap from
 * the repository root or from kernel/. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zui_apps.h"
#include "zui_tokens.h"

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

static zui_vec_work_t W;
static uint8_t *PACK, *BAD;
static uint32_t N;

static uint8_t *load(const char *p, uint32_t *n)
{
    FILE *f = fopen(p, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    rewind(f);
    uint8_t *b = malloc((size_t) len);
    if (!b || fread(b, 1, (size_t) len, f) != (size_t) len) {
        fclose(f);
        free(b);
        return 0;
    }
    fclose(f);
    *n = (uint32_t) len;
    return b;
}

static void w32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
    p[2] = (uint8_t) (v >> 16);
    p[3] = (uint8_t) (v >> 24);
}
static uint32_t r32(const uint8_t *p)
{
    return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

/* a fresh copy of the pack to break one thing in */
static uint8_t *fresh(void)
{
    memcpy(BAD, PACK, N);
    return BAD;
}
static bool refused(uint32_t n)
{
    zui_apps_t a;
    int r = zui_apps_bind(&a, BAD, n);
    zui_app_t app;
    return r == -1 && zui_apps_count(&a) == 0 && !zui_apps_get(&a, 0, &app) &&
           zui_apps_find(&a, "Quillon") == -1;
}

static void good(void)
{
    zui_apps_t a;
    CHECK(zui_apps_bind(&a, PACK, N) == 0, "the real pack binds");
    CHECK(zui_apps_count(&a) == 407, "407 apps (%u)", zui_apps_count(&a));
    CHECK(zui_apps_in_layer(&a, 1) == 144 && zui_apps_in_layer(&a, 2) == 127 &&
              zui_apps_in_layer(&a, 3) == 136 && zui_apps_in_layer(&a, 0) == 407,
          "AP3 layers 144/127/136");
    CHECK(zui_apps_in_layer(&a, 4) == 0, "no layer 4");
    zui_app_t app;
    CHECK(zui_apps_get(&a, 0, &app) && app.number == 1 && app.layer == 1 &&
              !strcmp(app.name, "Banker") && !strcmp(app.category, "System core"),
          "app 1 is the Banker (%s)", app.name);
    CHECK(zui_apps_get(&a, 406, &app) && app.number == 407 && app.layer == 3, "app 407");
    CHECK(!zui_apps_get(&a, 407, &app), "nothing past the end");
    for (uint32_t i = 0; i < 407; i++) {
        zui_apps_get(&a, i, &app);
        if (app.number != i + 1 || !app.name[0] || !app.icon || app.icon_len < 16 ||
            zui_zxvg_check(app.icon, app.icon_len) != 0 ||
            (app.layer == 1) != (i < 144) || (app.layer == 3) != (i >= 271)) {
            CHECK(0, "app %u is malformed", i + 1);
            break;
        }
    }
    CHECK(zui_apps_get(&a, 150, &app) && !strcmp(app.name, "Lectern") &&
              strstr(app.built_from, "Broadsheet") && strstr(app.pillars, "Sutra") &&
              strstr(app.paraconsistent, "BOTH"),
          "a layer 2 app keeps every field");
    CHECK(zui_apps_find(&a, "quillon") == 36 && zui_apps_find(&a, "QUILLON") == 36,
          "find ignores case");
    CHECK(zui_apps_find(&a, "Quill") == -1 && zui_apps_find(&a, "Quillons") == -1,
          "find is whole-name");
    CHECK(zui_apps_find(&a, "GAZETTE") >= 0 && zui_apps_find(&a, "BRAZIER") >= 0,
          "find folds every capital, Z included");
    CHECK(zui_apps_find(&a, 0) == -1 && zui_apps_find(&a, "") == -1, "find nothing");
    CHECK(zui_apps_find(&a, "Banker") == 0 && zui_apps_find(&a, "Burrow") == -1,
          "the file manager is the Banker");

    zui_apps_get(&a, 36, &app);
    CHECK(zui_apps_match(&app, "") && zui_apps_match(&app, 0), "empty matches");
    CHECK(zui_apps_match(&app, "quil") && zui_apps_match(&app, "WRITING") &&
              zui_apps_match(&app, "markdown"),
          "match name, category, what it does");
    CHECK(!zui_apps_match(&app, "zzzz") && !zui_apps_match(&app, "quillonx"), "no match");

    /* AP: the icon draws inside its box only, and differs from the ground */
    static uint32_t px[96 * 96];
    zui_surface_t s;
    zui_surface_init(&s, px, 96, 96, 96);
    zui_fill(&s, (zui_rect_t){0, 0, 96, 96}, ZUI_VOID);
    CHECK(zui_app_icon(&s, &W, &app, (zui_rect_t){16, 16, 64, 64}, 0) == 0, "icon draws");
    uint32_t inked = 0, outside = 0;
    for (int y = 0; y < 96; y++)
        for (int x = 0; x < 96; x++) {
            bool inbox = x >= 16 && x < 80 && y >= 16 && y < 80;
            if (px[y * 96 + x] != ZUI_VOID) {
                if (inbox) inked++;
                else outside++;
            }
        }
    CHECK(inked > 200 && outside == 0, "ink %u inside, %u outside", inked, outside);
    CHECK(zui_app_icon(&s, &W, &app, (zui_rect_t){0, 0, 0, 10}, 0) == -1, "empty box");
    CHECK(zui_app_icon(&s, &W, 0, (zui_rect_t){0, 0, 10, 10}, 0) == -1, "no app");
}

static void bad(void)
{
    BAD = malloc(N);
    zui_apps_t a;
    CHECK(zui_apps_bind(&a, 0, N) == -1 && zui_apps_count(&a) == 0, "null");
    CHECK((fresh(), refused(15)), "shorter than the header");
    CHECK((fresh(), refused(N - 1)), "truncated");
    CHECK((fresh()[0] = 'Y', refused(N)), "magic");
    CHECK((fresh()[4] = 2, refused(N)), "version");
    CHECK((fresh()[6] = 0, fresh()[6] = 0, BAD[7] = 0, refused(N)), "count 0");
    CHECK((w32(fresh() + 12, N + 4), refused(N)), "total size");
    CHECK((w32(fresh() + 8, r32(PACK + 8) + 4), refused(N)), "string area moved");
    uint8_t *e = 0;
    e = fresh() + 16 + 36 * 5;
    e[0] = 9;
    CHECK(refused(N), "numbers out of order");
    e = fresh() + 16 + 36 * 5;
    e[2] = 4;
    CHECK(refused(N), "layer 4");
    e = fresh() + 16 + 36 * 5;
    e[2] = 0;
    CHECK(refused(N), "layer 0");
    e = fresh() + 16 + 36 * 5;
    e[3] = 1;
    CHECK(refused(N), "reserved byte");
    e = fresh() + 16 + 36 * 5;
    w32(e + 4, 0x7FFFFFF0u);
    CHECK(refused(N), "a name past the area");
    e = fresh() + 16 + 36 * 5;
    w32(e + 28, r32(e + 28) + 2);
    CHECK(refused(N), "an icon off its alignment");
    e = fresh() + 16 + 36 * 5;
    w32(e + 32, N);
    CHECK(refused(N), "an icon past the end");
    e = fresh() + 16 + 36 * 5;
    w32(e + 32, N - r32(e + 28) + 4);
    CHECK(refused(N), "an icon running 4 bytes past the end");
    e = fresh() + 16 + 36 * 5;
    w32(e + 4, 0u - r32(PACK + 8) + 20u); /* wraps round to the header */
    CHECK(refused(N), "a string offset that wraps");
    e = fresh() + 16 + 36 * 5;
    w32(e + 32, 0);
    CHECK(refused(N), "an empty icon");
    e = fresh() + 16 + 36 * 5;
    BAD[r32(e + 28)] ^= 0xFFu;
    CHECK(refused(N), "a broken icon");
    /* the last string in the area loses its NUL */
    fresh();
    uint32_t send = N;
    for (uint32_t i = 0; i < 407; i++) {
        uint32_t io = r32(PACK + 16 + 36 * i + 28);
        if (io < send) send = io;
    }
    uint32_t z = send - 1;
    while (z > r32(PACK + 8) && BAD[z] == 0) z--; /* past the padding */
    for (uint32_t k = z + 1; k < send; k++) BAD[k] = 'x';
    CHECK(refused(N), "a string without its end");
    CHECK((fresh(), zui_apps_bind(&a, BAD, N) == 0), "the copy itself binds");
    free(BAD);
}

int main(int argc, char **argv)
{
    const char *try[] = {argc > 1 ? argv[1] : "assets/icons/apps/zxv-apps.zxap",
                         "../assets/icons/apps/zxv-apps.zxap", "assets/icons/apps/zxv-apps.zxap"};
    for (int i = 0; i < 3 && !PACK; i++) PACK = load(try[i], &N);
    if (!PACK) {
        printf("[FAIL] test_zui_apps: no pack (run tools/zui/gen_appicons.py)\n");
        return 1;
    }
    good();
    bad();
    free(PACK);
    if (fails) {
        printf("test_zui_apps: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("[PASS] test_zui_apps: %d/%d checks\n", checks, checks);
    return 0;
}
