/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxv_desktop.c — the hosted desktop (zxv_desktop.h) with no display:
 * the real desktop, its app windows and Spaces, drawn into its own surface
 * (HD5) and driven by pointer events the way a person would click. Checks
 * the ground, the gold mark, the status line, that every app on the rail
 * opens in its own window and finishes launching, Spaces, closing and
 * reopening, a resize and a 2x panel, and the app library (HD6): the Apps
 * tile, search, a card, an app on the rail, the card limit and a missing
 * pack. Writes a PPM of each screen to the
 * folder named on the command line. Run by test_zxv_desktop.sh.
 *
 *   test_zxv_desktop FONT|none SHOTS_DIR [ZXVD_SOCKET]
 *
 * With a socket the desktop must say "zxvd connected" and Burrow must list
 * the folder; without one it must say "zxvd not running" and still open
 * every window. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zui_palette.h"
#include "zui_tokens.h"
#include "zxv_desktop.h"

static int fails, checks;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL %s:%d ", __FILE__, __LINE__);                                           \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static const char *SHOTS;
static bool HAVE_FONT;
static uint32_t clock_ms = 100000;

static void shot(zxv_desktop_t *d, const char *name)
{
    const zui_surface_t *s = zxv_desktop_surface(d);
    char p[1024];
    snprintf(p, sizeof p, "%s/hosted-%s.ppm", SHOTS, name);
    FILE *f = fopen(p, "wb");
    if (!f) {
        CHECK(0, "cannot write %s", p);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", (int) s->w, (int) s->h);
    for (int32_t y = 0; y < s->h; y++)
        for (int32_t x = 0; x < s->w; x++) {
            uint32_t v = s->px[(size_t) y * (size_t) s->stride + (size_t) x];
            unsigned char rgb[3] = {(unsigned char) (v >> 16), (unsigned char) (v >> 8),
                                    (unsigned char) v};
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

static void steps(zxv_desktop_t *d, int n)
{
    for (int i = 0; i < n; i++) CHECK(zxv_desktop_step(d, 0) == 1, "step %d", i);
}

static void ptr(zxv_desktop_t *d, uint8_t type, int32_t x, int32_t y)
{
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.x = x;
    e.y = y;
    e.button = ZUI_BTN_LEFT;
    e.ms = clock_ms;
    zxv_desktop_feed(d, &e);
}

static void click(zxv_desktop_t *d, zui_rect_t r)
{
    clock_ms += 1000; /* never a double click */
    int32_t x = r.x + r.w / 2, y = r.y + r.h / 2;
    ptr(d, ZUI_EV_PTR_MOVE, x, y);
    ptr(d, ZUI_EV_PTR_DOWN, x, y);
    steps(d, 1);
    clock_ms += 40;
    ptr(d, ZUI_EV_PTR_UP, x, y);
    steps(d, 1);
}

/* pixels of one colour inside r */
static uint32_t count(const zxv_desktop_t *d, zui_rect_t r, uint32_t rgb)
{
    const zui_surface_t *s = zxv_desktop_surface(d);
    uint32_t n = 0;
    for (int32_t y = r.y; y < r.y + r.h && y < s->h; y++)
        for (int32_t x = r.x; x < r.x + r.w && x < s->w; x++)
            if (x >= 0 && y >= 0 &&
                (s->px[(size_t) y * (size_t) s->stride + (size_t) x] & 0xFFFFFFu) == rgb)
                n++;
    return n;
}

/* pixels that are text-light (the light green ink), anti-aliasing aside */
static uint32_t inked(const zxv_desktop_t *d, zui_rect_t r)
{
    const zui_surface_t *s = zxv_desktop_surface(d);
    uint32_t n = 0;
    for (int32_t y = r.y; y < r.y + r.h && y < s->h; y++)
        for (int32_t x = r.x; x < r.x + r.w && x < s->w; x++) {
            uint32_t v = s->px[(size_t) y * (size_t) s->stride + (size_t) x];
            if (((v >> 8) & 0xFF) > 0xA0) n++; /* far brighter than any panel */
        }
    return n;
}

static uint32_t white(const zxv_desktop_t *d)
{
    const zui_surface_t *s = zxv_desktop_surface(d);
    return count(d, (zui_rect_t){0, 0, s->w, s->h}, 0xFFFFFFu);
}

static void check_chrome(zxv_desktop_t *d, const char *who, bool font)
{
    zui_desk_t *k = zxv_desktop_desk(d);
    zui_rect_t a = zui_desk_area(k), bar = zui_desk_bar(k);
    CHECK(count(d, a, ZUI_VOID) > 0, "%s: the #002911 ground shows in the window area", who);
    zui_rect_t markbox = {0, 0, zui_desk_pt(k, 48), bar.h};
    CHECK(count(d, markbox, ZUI_GOLD) > 20, "%s: the gold mark (%u gold pixels)", who,
          count(d, markbox, ZUI_GOLD));
    CHECK(white(d) == 0, "%s: no white pixels (%u)", who, white(d));
    if (font) {
        CHECK(inked(d, (zui_rect_t){markbox.w, 0, zui_desk_pt(k, 60), bar.h}) > 10,
              "%s: the ZXV wordmark is drawn in the face", who);
        CHECK(inked(d, zui_desk_rail(k)) > 50, "%s: the rail's marks and names", who);
    }
}

static void test_desktop(const char *font, const char *rpc)
{
    zxv_desktop_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.headless = true;
    cfg.font = font;
    cfg.rpc = rpc ? rpc : "";
    cfg.w_pt = 1280;
    cfg.h_pt = 800;
    const char *why = NULL;
    zxv_desktop_t *d = zxv_desktop_open(&cfg, &why);
    CHECK(d != NULL, "opens headless: %s", why ? why : "");
    if (!d) return;
    zui_desk_t *k = zxv_desktop_desk(d);
    steps(d, 2);
    CHECK(zxv_desktop_apps(d) == 5 && !strcmp(zxv_desktop_app_name(d, 0), "Banker") &&
              !strcmp(zxv_desktop_app_name(d, 4), "Murmuration"),
          "five apps on the rail, Burrow first");
    if (rpc)
        CHECK(zxv_desktop_connected(d) && !strcmp(k->status, "zxvd connected"), "status: '%s'",
              k->status);
    else
        CHECK(!zxv_desktop_connected(d) && !strcmp(k->status, "zxvd not running"), "status: '%s'",
              k->status);
    check_chrome(d, "empty desktop", HAVE_FONT);
    zui_rect_t a = zui_desk_area(k);
    CHECK(count(d, a, ZUI_VOID) > (uint32_t) (a.w * (a.h - ZUI_WM_STRIP)) * 9 / 10,
          "the empty area is the ground");
    shot(d, "empty");

    /* every app's tile opens its window, which finishes launching */
    static const char *const names[] = {"burrow", "quillon", "glint", "spindle", "murmuration"};
    for (uint32_t i = 0; i < zxv_desktop_apps(d); i++) {
        click(d, zui_desk_tile(k, i));
        uint32_t id = k->win_of[i];
        CHECK(id != 0, "%s: the tile opens a window", names[i]);
        zui_wm_window_t *w = id ? zui_wm_get(&k->wm, id) : NULL;
        CHECK(w && w->progress == 256, "%s: the launch got its progress", names[i]);
        steps(d, 40); /* the doors open: 360 ms */
        CHECK(w && w->phase == ZUI_WIN_OPEN, "%s: the window is open (phase %d)", names[i],
              w ? (int) w->phase : -1);
        CHECK(zui_desk_front_app(k) == (int32_t) i, "%s: in front", names[i]);
        check_chrome(d, names[i], HAVE_FONT);
        if (w) {
            zui_rect_t c = zui_wm_content(&k->wm, w);
            c.x += a.x;
            c.y += a.y;
            CHECK(count(d, c, ZUI_VOID) + count(d, c, ZUI_PANEL) + count(d, c, ZUI_RAISED) >
                      (uint32_t) (c.w * c.h) / 2,
                  "%s: the app draws on the green ground", names[i]);
            if (HAVE_FONT)
                CHECK(inked(d, c) > 100, "%s: the app's text is drawn (%u)", names[i], inked(d, c));
        }
        /* the view's own words: what zxvd said, or that it is not there */
        static char a11y[16384];
        zui_ui_t *ui = zxv_desktop_app_ui(d, i);
        CHECK(ui && zui_a11y_text(ui, a11y, sizeof a11y) > 0, "%s: the tree reads", names[i]);
        if (i == 0 && rpc)
            CHECK(strstr(a11y, " 1 row") != NULL, "burrow lists the folder's one file:\n%s", a11y);
        if (i == 0 && !rpc)
            CHECK(strstr(a11y, "zxvd") != NULL, "burrow's error line says zxvd is not there:\n%s",
                  a11y);
        shot(d, names[i]);
    }
    shot(d, "desktop");
    /* the rail tile of a running app raises it rather than opening another */
    uint32_t first = k->win_of[0];
    click(d, zui_desk_tile(k, 0));
    CHECK(k->win_of[0] == first && zui_desk_front_app(k) == 0, "a second press raises Burrow");

    /* Spaces: the switch, a card, back */
    click(d, zui_desk_switch(k, true));
    CHECK(zxv_desktop_in_spaces(d) && k->spaces, "the switch shows Spaces");
    check_chrome(d, "spaces", HAVE_FONT);
    zui_rect_t card = zxv_desktop_space_card(d, 1);
    CHECK(card.w > 0 && zxv_desktop_space_card(d, 4).w > 0, "five cards");
    CHECK(count(d, zxv_desktop_space_card(d, 0), ZUI_PANEL) > 0, "the cards are carved panels");
    shot(d, "spaces");
    click(d, card);
    CHECK(!zxv_desktop_in_spaces(d) && zxv_desktop_space(d) == 1 && strstr(k->status, "Office"),
          "choosing Office goes back to the desktop: '%s'", k->status);
    click(d, zui_desk_switch(k, true));
    click(d, zui_desk_switch(k, false));
    CHECK(!zxv_desktop_in_spaces(d), "Desktop goes back");

    /* closing a window and opening it again */
    uint32_t qid = k->win_of[1];
    CHECK(zui_wm_close(&k->wm, qid) && k->win_of[1] == 0, "close Quillon");
    steps(d, 2);
    click(d, zui_desk_tile(k, 1));
    CHECK(k->win_of[1] != 0 && k->win_of[1] != qid, "Quillon opens again in a new window");
    steps(d, 40);

    /* the window shrinks: the desktop and its windows follow */
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = ZUI_EV_RESIZE;
    e.x = 900;
    e.y = 600;
    zxv_desktop_feed(d, &e);
    steps(d, 3);
    CHECK(zxv_desktop_surface(d)->w == 900 && zxv_desktop_surface(d)->h == 600 && k->w == 900,
          "resized to 900 x 600");
    check_chrome(d, "resized", HAVE_FONT);
    shot(d, "resized");

    /* the window's close button is the person closing the desktop */
    memset(&e, 0, sizeof e);
    e.type = ZUI_EV_CLOSE;
    zxv_desktop_feed(d, &e);
    CHECK(zxv_desktop_step(d, 0) == 0, "a close event ends the loop");
    zxv_desktop_note(d, "Restarting the swarm");
    CHECK(!strcmp(k->status, "Restarting the swarm"), "a note takes the status line");
    zxv_desktop_note(d, NULL);
    CHECK(strstr(k->status, "zxvd") != NULL, "and gives it back: '%s'", k->status);
    zxv_desktop_close(d);
}

static void key(zxv_desktop_t *d, uint16_t k)
{
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = ZUI_EV_KEY_DOWN;
    e.key = k;
    e.ms = clock_ms;
    zxv_desktop_feed(d, &e);
    steps(d, 1);
}

static void type(zxv_desktop_t *d, const char *t)
{
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = ZUI_EV_TEXT;
    snprintf(e.text, sizeof e.text, "%s", t);
    e.ms = clock_ms;
    zxv_desktop_feed(d, &e);
    steps(d, 1);
}

static const char *front_title(zui_desk_t *k)
{
    zui_wm_window_t *w = zui_wm_get(&k->wm, zui_wm_front(&k->wm));
    return w ? w->title : "";
}

/* HD6: the app library */
static void test_library(const char *font)
{
    zxv_desktop_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.headless = true;
    cfg.font = font;
    cfg.rpc = "";
    cfg.w_pt = 1280;
    cfg.h_pt = 800;
    zxv_desktop_t *d = zxv_desktop_open(&cfg, NULL);
    CHECK(d != NULL, "library: opens");
    if (!d) return;
    zui_desk_t *k = zxv_desktop_desk(d);
    zui_applib_t *l = zxv_desktop_library(d);
    CHECK(l && zui_apps_count(l->apps) == 407, "the pack loads: 407 apps");
    if (!l) {
        zxv_desktop_close(d);
        return;
    }
    for (uint32_t i = 0; i < zxv_desktop_apps(d); i++)
        CHECK(k->app[i].icon && k->app[i].icon_len > 16, "%s wears its catalogue icon",
              zxv_desktop_app_name(d, i));
    steps(d, 2);
    /* the rail's icons are gold vectors, not letters */
    CHECK(count(d, zui_desk_tile(k, 0), ZUI_GOLD) > 100, "the Banker's tile is its gold icon (%u)",
          count(d, zui_desk_tile(k, 0), ZUI_GOLD));

    click(d, zui_desk_apps_tile(k));
    CHECK(zxv_desktop_library_open(d) && k->library, "the Apps tile opens the library");
    check_chrome(d, "library", HAVE_FONT);
    zui_rect_t g = zui_applib_grid(l);
    CHECK(l->nshown == 407 && count(d, g, ZUI_GOLD) > 2000, "407 tiles, gold icons drawn (%u)",
          count(d, g, ZUI_GOLD));
    CHECK(count(d, zui_desk_apps_tile(k), ZUI_GOLD) > 50, "the Apps tile is lit while open");
    shot(d, "library");

    /* typing searches; Enter on an app with no window opens its card */
    type(d, "lectern");
    CHECK(l->nshown >= 1 && l->qlen == 7, "typing searches (%u shown)", l->nshown);
    zui_app_t app;
    zui_apps_get(l->apps, l->shown[0], &app);
    CHECK(!strcmp(app.name, "Lectern"), "Lectern first (%s)", app.name);
    shot(d, "library-search");
    key(d, ZUI_KEY_ENTER);
    CHECK(!zxv_desktop_library_open(d), "choosing closes the library");
    CHECK(!strcmp(front_title(k), "Lectern"), "Lectern's card is in front (%s)", front_title(k));
    steps(d, 40);
    zui_wm_window_t *cw = zui_wm_get(&k->wm, zui_wm_front(&k->wm));
    if (cw && HAVE_FONT) {
        zui_rect_t c = zui_wm_content(&k->wm, cw), a = zui_desk_area(k);
        c.x += a.x;
        c.y += a.y;
        CHECK(inked(d, c) > 500 && count(d, c, ZUI_GOLD) > 500, "the card has its icon and words");
    }
    shot(d, "card");

    /* a fresh search each time; an app on the rail opens its window */
    click(d, zui_desk_apps_tile(k));
    CHECK(zxv_desktop_library_open(d) && l->qlen == 0 && l->nshown == 407, "opens fresh");
    type(d, "quillon");
    key(d, ZUI_KEY_ENTER);
    CHECK(k->win_of[1] != 0 && zui_desk_front_app(k) == 1, "Quillon opens its own window");
    steps(d, 40);

    /* Escape: clears the search, then closes */
    click(d, zui_desk_apps_tile(k));
    type(d, "zz");
    key(d, ZUI_KEY_ESCAPE);
    CHECK(zxv_desktop_library_open(d) && l->qlen == 0, "Escape clears the search first");
    key(d, ZUI_KEY_ESCAPE);
    CHECK(!zxv_desktop_library_open(d), "then closes the library");
    /* the tile toggles, and Spaces takes over */
    click(d, zui_desk_apps_tile(k));
    click(d, zui_desk_apps_tile(k));
    CHECK(!zxv_desktop_library_open(d), "a second press on the tile closes it");
    click(d, zui_desk_apps_tile(k));
    click(d, zui_desk_switch(k, true));
    CHECK(!zxv_desktop_library_open(d) && zxv_desktop_in_spaces(d), "Spaces closes the library");
    click(d, zui_desk_switch(k, false));

    /* seven cards: at most six stay open, the oldest gives way */
    uint32_t ids[7];
    for (uint32_t i = 0; i < 7; i++) ids[i] = zxv_desktop_choose(d, 200 + i);
    uint32_t open = 0;
    for (uint32_t i = 0; i < 7; i++) open += ids[i] && zui_wm_get(&k->wm, ids[i]) ? 1u : 0u;
    CHECK(ids[6] && open == 6 && !zui_wm_get(&k->wm, ids[0]), "six cards at most (%u)", open);
    CHECK(zxv_desktop_choose(d, 203) == ids[3], "choosing an open card raises it");
    CHECK(zxv_desktop_choose(d, 407) == 0, "no app 408");
    zxv_desktop_close(d);

    /* HD7: the light scheme reaches the desk, the library, a card and Spaces */
    cfg.scheme = "light";
    d = zxv_desktop_open(&cfg, NULL);
    CHECK(d != NULL && zxv_desktop_scheme(d) == ZUI_SCHEME_LIGHT, "opens light");
    if (d) {
        const zui_colors_t *lt = zui_scheme_colors(ZUI_SCHEME_LIGHT);
        k = zxv_desktop_desk(d);
        steps(d, 2);
        zui_rect_t a = zui_desk_area(k);
        CHECK(count(d, a, lt->c[ZUI_C_VOID]) > (uint32_t) (a.w * a.h) / 2 &&
                  count(d, zui_desk_bar(k), ZUI_PANEL) + count(d, zui_desk_bar(k), ZUI_RAISED) == 0,
              "the light ground, no dark panel left in the bar");
        zxv_desktop_launch(d, 1);
        steps(d, 40);
        zui_wm_window_t *qw = zui_wm_get(&k->wm, k->win_of[1]);
        if (qw) {
            zui_rect_t c = zui_wm_content(&k->wm, qw);
            c.x += a.x;
            c.y += a.y;
            CHECK(count(d, c, ZUI_VOID) + count(d, c, ZUI_PANEL) < (uint32_t) (c.w * c.h) / 20,
                  "Quillon's view is light too");
        }
        click(d, zui_desk_apps_tile(k));
        zui_rect_t lg = zui_applib_grid(zxv_desktop_library(d));
        zui_rect_t head = {a.x, a.y, a.w, lg.y - a.y};
        CHECK(count(d, a, lt->c[ZUI_C_VOID]) > (uint32_t) (a.w * a.h) / 3 &&
                  count(d, head, ZUI_PANEL) + count(d, head, ZUI_RAISED) == 0,
              "the library is light (the dark is the icons' own art)");
        shot(d, "library-light");
        zxv_desktop_set_scheme(d, ZUI_SCHEME_HIGH_CONTRAST);
        steps(d, 1);
        const zui_colors_t *hc = zui_scheme_colors(ZUI_SCHEME_HIGH_CONTRAST);
        CHECK(count(d, zui_applib_grid(zxv_desktop_library(d)), hc->c[ZUI_C_VOID]) > 1000,
              "switching scheme repaints the library");
        click(d, zui_desk_switch(k, true));
        CHECK(count(d, a, hc->c[ZUI_C_VOID]) > (uint32_t) (a.w * a.h) / 3, "and Spaces");
        shot(d, "spaces-contrast");
        zxv_desktop_close(d);
    }
    cfg.scheme = NULL;

    /* no pack: letter marks, and the tile says why */
    cfg.apps = "/nonexistent/zxv-apps.zxap";
    d = zxv_desktop_open(&cfg, NULL);
    CHECK(d != NULL, "opens with no pack");
    if (!d) return;
    k = zxv_desktop_desk(d);
    CHECK(!zxv_desktop_library(d) && !k->app[0].icon, "no pack, no library, letter marks");
    steps(d, 1);
    click(d, zui_desk_apps_tile(k));
    CHECK(!zxv_desktop_library_open(d) && strstr(k->status, "catalogue"),
          "the status says the catalogue is missing: '%s'", k->status);
    zxv_desktop_close(d);
}

/* A Retina panel: the chrome and the apps at 2x. */
static void test_retina(const char *font)
{
    zxv_desktop_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.headless = true;
    cfg.font = font;
    cfg.rpc = "";
    cfg.w_pt = 900;
    cfg.h_pt = 560;
    cfg.scale_q8 = 512;
    zxv_desktop_t *d = zxv_desktop_open(&cfg, NULL);
    CHECK(d != NULL, "opens at 2x");
    if (!d) return;
    CHECK(zxv_desktop_surface(d)->w == 1800, "2x pixels");
    zxv_desktop_launch(d, 0);
    zxv_desktop_launch(d, 4);
    steps(d, 40);
    check_chrome(d, "2x", HAVE_FONT);
    shot(d, "retina");
    zxv_desktop_close(d);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: test_zxv_desktop FONT SHOTS_DIR [ZXVD_SOCKET]\n");
        return 2;
    }
    /* FONT "none": a font file that is not there, so no text is drawn */
    const char *font = argv[1];
    HAVE_FONT = strcmp(font, "none") != 0;
    SHOTS = argv[2];
    test_desktop(font, argc > 3 ? argv[3] : NULL);
    test_retina(font);
    test_library(font);
    printf("%s test_zxv_desktop%s: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]",
           argc > 3 ? " (zxvd running)" : " (no zxvd)", checks - fails, checks);
    return fails ? 1 : 0;
}
