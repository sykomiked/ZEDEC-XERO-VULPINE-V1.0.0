/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_kit.c — the toolkit as a whole under each colour scheme
 * (zui_palette.h C6, zui_widget.h W13) and the widgets of W8..W14:
 *
 *   - dark through the scheme is pixel for pixel the token screen, for the
 *     widgets, the window manager (dock and strip), the lock screen, the
 *     notices and the on-screen keyboard;
 *   - light and high contrast change every surface, leave no dark-only
 *     token colour behind, and paint the pairs the scheme promises;
 *   - toggles, check boxes, radios, sliders, tabs, dialogs, tooltips,
 *     scroll indicators, wrapped rows, empty states, the list's lead
 *     column and detail width, the two-tone focus ring, cap-height labels
 *     and the right-to-left caret, by pointer, keyboard and screen reader.
 *
 * Text is drawn with the repository's ASCW Golden Latin face (run from
 * kernel/, as test_zui_toolkit.sh does; ZXV_KIT_FONT names another); the
 * checks that read text ink are skipped when no face loads.
 * ZXV_KIT_SHOTS=<dir> also writes PPM pictures of each surface in each
 * scheme; they are not checked. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zui_caret.h"
#include "zui_dock.h"
#include "zui_keymap.h"
#include "zui_lock.h"
#include "zui_motion.h"
#include "zui_notice.h"
#include "zui_osk.h"
#include "zui_palette.h"
#include "zui_tokens.h"
#include "zui_widget.h"
#include "zui_wm.h"
#include "../font/ttf_fixture.inc" /* MINI_TTF[] */

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

#define W 1280
#define H 800
static uint32_t PX[W * H];
static zui_surface_t S;
static zui_ui_t UI;
static ttf_font_t MINI, SHOTFONT;
static zui_glyph_cache_t CACHE[4];
static bool have_shotfont;
static const char *SHOTS;

static const zui_scheme_t SCHEMES[3] = {ZUI_SCHEME_DARK, ZUI_SCHEME_LIGHT,
                                        ZUI_SCHEME_HIGH_CONTRAST};
static const char *SNAME[3] = {"dark", "light", "hc"};

static uint32_t at(int32_t x, int32_t y)
{
    return S.px[y * S.stride + x] & 0xFFFFFFu;
}

static uint32_t hash(void)
{
    uint32_t h = 2166136261u;
    for (int32_t y = 0; y < S.h; y++)
        for (int32_t x = 0; x < S.w; x++) {
            h ^= at(x, y);
            h *= 16777619u;
        }
    return h;
}

static void shot(const char *name)
{
    if (!SHOTS) return;
    char p[512];
    snprintf(p, sizeof p, "%s/%s.ppm", SHOTS, name);
    FILE *f = fopen(p, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", S.w, S.h);
    for (int32_t y = 0; y < S.h; y++)
        for (int32_t x = 0; x < S.w; x++) {
            uint32_t c = at(x, y);
            unsigned char b[3] = {(unsigned char) (c >> 16), (unsigned char) (c >> 8),
                                  (unsigned char) c};
            fwrite(b, 1, 3, f);
        }
    fclose(f);
}

/* Pixels exactly equal to a dark token whose role has another value in
 * set: a colour that did not go through the scheme. */
static zui_rect_t SKIP; /* the launch screen: the boot sequence's own palette (zui_wm.h WM8) */
static uint32_t leaks(const zui_colors_t *set)
{
    const zui_colors_t *dark = zui_scheme_colors(ZUI_SCHEME_DARK);
    uint32_t n = 0;
    for (int32_t y = 0; y < S.h; y++)
        for (int32_t x = 0; x < S.w; x++) {
            uint32_t c = at(x, y);
            if (x >= SKIP.x && y >= SKIP.y && x < SKIP.x + SKIP.w && y < SKIP.y + SKIP.h) continue;
            for (int r = 0; r < ZUI_C_COUNT; r++) {
                if (dark->c[r] != c) continue;
                bool kept = false; /* the same value in this set under any role */
                for (int k = 0; k < ZUI_C_COUNT; k++) kept |= set->c[k] == c;
                if (!kept) {
                    n++;
                }
                break;
            }
        }
    return n;
}

static bool any(zui_rect_t r, uint32_t rgb)
{
    for (int32_t y = r.y; y < r.y + r.h; y++)
        for (int32_t x = r.x; x < r.x + r.w; x++)
            if (x >= 0 && y >= 0 && x < S.w && y < S.h && at(x, y) == rgb) return true;
    return false;
}

static uint32_t now = 1000;
static int n_act;
static zui_id_t last_w;
static zui_action_t last_act;
static int32_t last_arg;

static void on(zui_ui_t *ui, zui_id_t w, zui_action_t a, int32_t arg, void *ctx)
{
    (void) ui;
    (void) ctx;
    n_act++;
    last_w = w;
    last_act = a;
    last_arg = arg;
}

static void reset_act(void)
{
    n_act = 0;
    last_w = ZUI_NONE;
    last_act = ZUI_ACT_NONE;
    last_arg = -99;
}

static zui_event_t ev(uint8_t type, int32_t x, int32_t y)
{
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.x = x;
    e.y = y;
    e.ms = ++now;
    if (type == ZUI_EV_PTR_DOWN || type == ZUI_EV_PTR_UP) e.button = ZUI_BTN_LEFT;
    return e;
}

static void send(zui_event_t e)
{
    zui_ui_event(&UI, &e);
}

static void key(uint16_t k, uint8_t mods)
{
    zui_event_t e = ev(ZUI_EV_KEY_DOWN, 0, 0);
    e.key = k;
    e.mods = mods;
    send(e);
}

static void click(int32_t x, int32_t y)
{
    now += 1000;
    send(ev(ZUI_EV_PTR_DOWN, x, y));
    send(ev(ZUI_EV_PTR_UP, x, y));
}

static void tick(uint32_t ms)
{
    now += ms;
    zui_event_t e = ev(ZUI_EV_TICK, 0, 0);
    e.ms = now;
    send(e);
}

static void center(zui_id_t id, int32_t *x, int32_t *y)
{
    zui_widget_t *w = zui_w(&UI, id);
    *x = w->r.x + w->r.w / 2;
    *y = w->r.y + w->r.h / 2;
}

static bool a11y_has(const char *s)
{
    static char out[16384];
    zui_a11y_text(&UI, out, sizeof out);
    if (strstr(out, s)) return true;
    if (getenv("ZXV_KIT_A11Y")) printf("--- no '%s' in:\n%s\n", s, out);
    return false;
}

static zui_text_t face(uint32_t size, int i)
{
    zui_text_t t;
    memset(&t, 0, sizeof t);
    t.font = have_shotfont ? &SHOTFONT : &MINI;
    t.cache = &CACHE[i];
    t.size_px = size;
    return t;
}

static zui_theme_t font_theme(void)
{
    zui_theme_t th;
    th.body = face(16, 0);
    th.label = face(12, 1);
    th.title = face(20, 2);
    th.mono = face(16, 3);
    return th;
}

static zui_theme_t mono_theme(void)
{
    zui_theme_t t;
    memset(&t, 0, sizeof t);
    t.body.size_px = 16; /* no font: a fixed advance, deterministic */
    t.label.size_px = 12;
    t.title.size_px = 20;
    t.mono.size_px = 16;
    return t;
}

/* ---- a gallery of every widget ---------------------------------------------- */

static const char *ROWS[] = {"Albums", "Art", "Audio", "Archive"};
static void row(const zui_ui_t *ui, zui_id_t l, int32_t r, zui_row_t *o, void *ctx)
{
    (void) ui;
    (void) l;
    (void) ctx;
    snprintf(o->text, sizeof o->text, "%s", ROWS[r % 4]);
    snprintf(o->detail, sizeof o->detail, "%d KB", 10 * (r + 1));
    o->lead_rgb = r == 2 ? 0 : (r & 1 ? ZUI_INFO : ZUI_OK);
    o->muted = r == 3;
}

typedef struct {
    zui_id_t root, title, save, list, toggle, check, radio[3], slider, tabs, seg, field, empty,
        dialog, ok, cancel;
} gallery_t;
static gallery_t G;
static char fbuf[64] = "Hello world";

static void gallery(const zui_screen_t *sc, const zui_theme_t *th)
{
    zui_ui_init(&UI, sc, th);
    zui_ui_on(&UI, on, 0);
    zui_surface_init(&S, PX, (int32_t) sc->px_w, (int32_t) sc->px_h, (int32_t) sc->px_w);
    G.root = zui_box(&UI, ZUI_NONE, ZUI_COL);
    zui_set_pad(&UI, G.root, 16, 8);
    G.title = zui_label(&UI, G.root, "A Gallery");
    zui_set_font(&UI, G.title, ZUI_TEXT_TITLE, 0);
    zui_id_t r = zui_box(&UI, G.root, ZUI_ROW);
    zui_set_pad(&UI, r, 0, 8);
    G.save = zui_button(&UI, r, "Save All", ZUI_BTN_PRIMARY);
    zui_set_tooltip(&UI, G.save, "Save the file");
    zui_button(&UI, r, "Cancel", ZUI_BTN_SECONDARY);
    zui_button(&UI, r, "Delete", ZUI_BTN_DANGER);
    zui_id_t off = zui_button(&UI, r, "Off", ZUI_BTN_SECONDARY);
    zui_set_disabled(&UI, off, true);
    zui_separator(&UI, G.root);
    zui_id_t p = zui_panel(&UI, G.root, ZUI_COL);
    zui_set_size(&UI, p, ZUI_SIZE_FIXED, 160);
    G.list = zui_list(&UI, p, row, 0);
    zui_list_set_rows(&UI, G.list, 30);
    zui_list_select(&UI, G.list, 1);
    zui_id_t c = zui_box(&UI, G.root, ZUI_ROW);
    zui_set_pad(&UI, c, 0, 16);
    G.toggle = zui_toggle(&UI, c, "Wi-Fi", true);
    G.check = zui_check(&UI, c, "Remember", false);
    zui_id_t rg = zui_box(&UI, G.root, ZUI_ROW);
    zui_set_pad(&UI, rg, 0, 16);
    G.radio[0] = zui_radio(&UI, rg, "Small", false);
    G.radio[1] = zui_radio(&UI, rg, "Medium", true);
    G.radio[2] = zui_radio(&UI, rg, "Large", false);
    G.slider = zui_slider(&UI, G.root, 0, 100, 5, 50);
    static const char *const tabs[] = {"General", "Display", "Sound"};
    G.tabs = zui_tabs(&UI, G.root, tabs, 3, 0, false);
    G.seg = zui_tabs(&UI, G.root, tabs, 3, 1, true);
    G.field = zui_field(&UI, G.root, fbuf, sizeof fbuf, false);
    zui_field_set(&UI, G.field, (uint32_t) strlen(fbuf));
    zui_id_t pr = zui_progress(&UI, G.root);
    zui_progress_set(&UI, pr, 100, ZUI_BAR_GATHER);
    zui_id_t eb = zui_box(&UI, G.root, ZUI_COL);
    zui_set_size(&UI, eb, ZUI_SIZE_FIXED, 140);
    G.empty = zui_empty(&UI, eb, ZUI_ICON_FOLDER, "No files here yet", "New file");
    G.dialog = zui_dialog(&UI, "Discard changes?");
    zui_label(&UI, G.dialog, "Your edits will be lost.");
    zui_id_t dr = zui_box(&UI, G.dialog, ZUI_ROW);
    zui_set_pad(&UI, dr, 0, 8);
    G.cancel = zui_button(&UI, dr, "Keep", ZUI_BTN_SECONDARY);
    G.ok = zui_button(&UI, dr, "Discard", ZUI_BTN_DANGER);
    reset_act();
}

static void repaint(void)
{
    zui_damage_all(&UI.damage, S.w, S.h);
    zui_ui_paint(&UI, &S);
}

/* lay out (and paint) on a surface the screen's size */
static void lay(void)
{
    zui_relayout(&UI);
    zui_surface_init(&S, PX, (int32_t) UI.screen.px_w, (int32_t) UI.screen.px_h,
                     (int32_t) UI.screen.px_w);
    repaint();
}

/* ---- #4: every surface through the scheme ------------------------------------ */

static uint32_t render_gallery(const zui_colors_t *set, const char *name)
{
    zui_screen_t sc = {640, 900, 256, ZUI_INPUT_POINTER};
    zui_theme_t th = font_theme();
    gallery(&sc, &th);
    zui_ui_set_colors(&UI, set);
    zui_focus(&UI, G.slider, true);
    repaint();
    uint32_t h = hash();
    if (name) shot(name);
    return h;
}

static void paintc(void *ctx, zui_surface_t *s, zui_rect_t c, uint32_t t)
{
    (void) ctx;
    (void) t;
    zui_fill(s, c, 0x123456u);
}

static zui_wm_t WM;
static uint32_t render_wm(const zui_colors_t *set, bool dock, const char *name)
{
    zui_wm_init(&WM, W, H, false);
    zui_wm_use_dock(&WM, dock);
    static zui_text_t t;
    t = face(15, 0);
    WM.text = &t;
    WM.colors = set;
    zui_wm_client_t cl = {paintc, 0, 0, 0};
    uint32_t a = zui_wm_open(&WM, "burrow", "Burrow", 0, &cl);
    zui_wm_progress(&WM, a, 256);
    zui_wm_tick(&WM, 1000);
    uint32_t b = zui_wm_open(&WM, "glint", "Glint", 0, &cl);
    zui_wm_progress(&WM, b, 256);
    zui_wm_tick(&WM, 1000);
    uint32_t q = zui_wm_open(&WM, "quillon", "Quillon", 0, &cl);
    SKIP = zui_wm_content(&WM, zui_wm_get(&WM, q));
    zui_surface_init(&S, PX, W, H, W);
    WM.switching = true;
    WM.pick = 1;
    if (dock) zui_dock_bloom(&WM.dock, true, true), zui_dock_tick(&WM.dock, 2000);
    zui_wm_draw(&WM, &S, 7);
    if (name) shot(name);
    return hash();
}

static zui_lock_t LK;
static uint32_t render_lock(const zui_colors_t *set, const char *name)
{
    static zui_text_t big, tx;
    big = face(40, 2);
    tx = face(15, 0);
    zui_lock_init(&LK, 0, 0);
    zui_lock_account(&LK, 1, "Michael");
    LK.big = &big;
    LK.text = &tx;
    LK.colors = set;
    zui_lock_now(&LK);
    zui_lock_notify(&LK, "Chat", "Hi there");
    zui_lock_notify(&LK, "Mail", "Invoice");
    zui_event_t e = ev(ZUI_EV_TEXT, 0, 0);
    strcpy(e.text, "wrong");
    zui_lock_event(&LK, &e);
    zui_lock_submit(&LK);
    zui_surface_init(&S, PX, W, H, W);
    zui_lock_draw(&LK, &S, (zui_rect_t){0, 0, W, H}, 1760000000, 0, 0);
    if (name) shot(name);
    return hash();
}

static zui_notice_center_t NC;
static uint32_t render_notices(const zui_colors_t *set, const char *name)
{
    static zui_text_t tx;
    tx = face(15, 0);
    zui_notice_init(&NC);
    NC.colors = set;
    zui_notice_action_t acts[2] = {{"Reply", "reply"}, {"Mark read", "read"}};
    zui_notice_spec_t a = {"Chat", "ana", "Ana", "See you at noon", ZUI_NPRI_NORMAL, 100,
                           acts,   2,     0};
    zui_notice_spec_t b = {"Mail", "", "Invoice", "Your invoice", ZUI_NPRI_HIGH,
                           200,    0,  0,         "companion"};
    zui_notice_post(&NC, &a, 100);
    zui_notice_post(&NC, &b, 200);
    zui_surface_init(&S, PX, W, H, W);
    zui_fill(&S, (zui_rect_t){0, 0, W, H}, zui_color(set, ZUI_C_VOID));
    zui_notice_draw_center(&NC, &S, (zui_rect_t){800, 0, 480, 800}, &tx);
    zui_notice_draw_banner(&NC, &S, (zui_rect_t){20, 20, 400, 120}, &tx);
    if (name) shot(name);
    return hash();
}

static zui_keymap_t KM;
static zui_osk_t OSK;
static uint32_t render_osk(const zui_colors_t *set, const char *name)
{
    static zui_text_t tx;
    tx = face(15, 0);
    zui_keymap_load(&KM, "us");
    zui_osk_init(&OSK, &KM);
    OSK.colors = set;
    zui_surface_init(&S, PX, 390, 300, 390);
    zui_fill(&S, (zui_rect_t){0, 0, 390, 300}, zui_color(set, ZUI_C_VOID));
    zui_osk_layout(&OSK, (zui_rect_t){0, 0, 390, 300}, 256);
    zui_osk_paint(&OSK, &S, &tx);
    if (name) shot(name);
    return hash();
}

typedef uint32_t (*render_fn)(const zui_colors_t *set, const char *name);
static uint32_t r_dock(const zui_colors_t *s, const char *n)
{
    return render_wm(s, true, n);
}
static uint32_t r_strip(const zui_colors_t *s, const char *n)
{
    return render_wm(s, false, n);
}

static void test_schemes(void)
{
    static const struct {
        const char *name;
        render_fn fn;
    } SURF[] = {{"widgets", render_gallery}, {"wm-dock", r_dock},         {"wm-strip", r_strip},
                {"lock", render_lock},       {"notices", render_notices}, {"osk", render_osk}};
    for (unsigned i = 0; i < sizeof SURF / sizeof SURF[0]; i++) {
        SKIP = (zui_rect_t){0, 0, 0, 0};
        uint32_t h0 = SURF[i].fn(0, 0); /* NULL: the tokens */
        char n[64];
        snprintf(n, sizeof n, "kit-%s-dark", SURF[i].name);
        uint32_t hd = SURF[i].fn(zui_scheme_colors(ZUI_SCHEME_DARK), n);
        CHECK(h0 == hd, "C6/W13: %s: dark through the scheme is the token screen (%08x %08x)",
              SURF[i].name, h0, hd);
        uint32_t hs[3] = {hd, 0, 0};
        for (int k = 1; k < 3; k++) {
            const zui_colors_t *set = zui_scheme_colors(SCHEMES[k]);
            snprintf(n, sizeof n, "kit-%s-%s", SURF[i].name, SNAME[k]);
            hs[k] = SURF[i].fn(set, n);
            CHECK(hs[k] != hd, "%s: %s differs from dark", SURF[i].name, SNAME[k]);
            uint32_t lk = leaks(set);
            CHECK(lk == 0, "%s: %s leaves no dark-only colour (%u px)", SURF[i].name, SNAME[k], lk);
        }
        CHECK(hs[1] != hs[2], "%s: light and high contrast differ", SURF[i].name);
    }

    /* the pairs the scheme promises, read off the screen */
    for (int k = 0; k < 3; k++) {
        const zui_colors_t *set = zui_scheme_colors(SCHEMES[k]);
        uint32_t need = SCHEMES[k] == ZUI_SCHEME_HIGH_CONTRAST ? ZUI_CONTRAST_AAA : ZUI_CONTRAST_AA;
        render_gallery(set, 0);
        uint32_t ground = at(2, 2);
        CHECK(ground == set->c[ZUI_C_VOID], "%s: the ground is the set's void", SNAME[k]);
        zui_widget_t *t = zui_w(&UI, G.title);
        CHECK(!have_shotfont || any(t->r, set->c[ZUI_C_TEXT]), "%s: the title in the set's text",
              SNAME[k]);
        CHECK(zui_contrast_q8(set->c[ZUI_C_TEXT], ground) >= need, "%s: text on the ground",
              SNAME[k]);
        zui_widget_t *b = zui_w(&UI, G.save);
        uint32_t fill = at(b->r.x + 3, b->r.y + b->r.h / 2);
        CHECK(fill == set->c[ZUI_C_GOLD], "%s: the primary button is the set's gold", SNAME[k]);
        CHECK((!have_shotfont || any(b->r, set->c[ZUI_C_VOID])) &&
                  zui_contrast_q8(set->c[ZUI_C_VOID], fill) >= need,
              "%s: its label on it", SNAME[k]);
        zui_widget_t *l = zui_w(&UI, G.list);
        CHECK(any(l->r, set->c[ZUI_C_RAISED]) && any(l->r, set->c[ZUI_C_MUTED]),
              "%s: the selected row raised, the details muted", SNAME[k]);
        CHECK(zui_contrast_q8(set->c[ZUI_C_MUTED], set->c[ZUI_C_RAISED]) >= need,
              "%s: muted on the raised row", SNAME[k]);
        zui_widget_t *sl = zui_w(&UI, G.slider);
        zui_rect_t ring = {sl->r.x - 5, sl->r.y - 5, sl->r.w + 10, sl->r.h + 10};
        CHECK(any(ring, set->c[ZUI_C_GOLD]), "%s: the focus ring in the set's gold", SNAME[k]);
        render_lock(set, 0);
        CHECK(at(2, 2) == set->c[ZUI_C_VOID], "%s: the lock screen's ground", SNAME[k]);
        render_wm(set, false, 0);
        zui_rect_t st = zui_wm_strip_rect(&WM);
        CHECK(at(st.x + 2, st.y + st.h - 2) == set->c[ZUI_C_PANEL], "%s: the strip is the panel",
              SNAME[k]);
        CHECK(any(st, set->c[ZUI_C_GOLD]), "%s: the front window's gold mark", SNAME[k]);
    }

    /* a stored token colour follows the scheme (zui_color_remap) */
    const zui_colors_t *lt = zui_scheme_colors(ZUI_SCHEME_LIGHT);
    CHECK(zui_color_remap(lt, ZUI_MUTED) == lt->c[ZUI_C_MUTED] &&
              zui_color_remap(lt, 0x123456u) == 0x123456u &&
              zui_color_remap(0, ZUI_GOLD) == ZUI_GOLD,
          "C6: tokens remap, other colours stay");
    CHECK(zui_color(0, ZUI_C_TEXT) == ZUI_TEXT &&
              zui_color(lt, (zui_color_role_t) 99) == lt->c[ZUI_C_TEXT],
          "C6: NULL is the tokens; a bad role is text");
}

/* ---- W8: choices ------------------------------------------------------------- */

static void mono_gallery(const zui_screen_t *sc)
{
    zui_theme_t th = mono_theme();
    gallery(sc, &th);
    lay();
    repaint();
}

static zui_screen_t LAPTOP = {1280, 800, 256, ZUI_INPUT_POINTER};

static void test_toggle_check(void)
{
    mono_gallery(&LAPTOP);
    int32_t x, y;
    center(G.toggle, &x, &y);
    CHECK(zui_checked(&UI, G.toggle) == 1 && a11y_has("switch \"Wi-Fi\" on"), "W8: a switch, on");
    click(x, y);
    CHECK(zui_checked(&UI, G.toggle) == 0 && last_w == G.toggle && last_act == ZUI_ACT_CHANGE &&
              last_arg == 0,
          "W8: a click turns it off");
    CHECK(zui_hit(&UI, x, y) == G.toggle, "W8: the toggle is hit");
    zui_focus(&UI, G.toggle, true);
    key(ZUI_KEY_SPACE, 0);
    CHECK(zui_checked(&UI, G.toggle) == 1 && last_arg == 1, "W8: Space turns it on");
    key(ZUI_KEY_ENTER, 0);
    CHECK(zui_checked(&UI, G.toggle) == 0, "W8: Enter flips it");
    zui_focus(&UI, G.check, true);
    key(ZUI_KEY_SPACE, 0);
    CHECK(zui_checked(&UI, G.check) == 1 && last_w == G.check && last_arg == 1,
          "W8: a check box checks");
    zui_set_checked(&UI, G.check, 2);
    CHECK(a11y_has("checkbox") && a11y_has("mixed"), "W8: a mixed check box says so");
    center(G.check, &x, &y);
    click(x, y);
    CHECK(zui_checked(&UI, G.check) == 1, "W8: mixed goes to checked");
    zui_set_disabled(&UI, G.check, true);
    reset_act();
    click(x, y);
    CHECK(n_act == 0 && zui_checked(&UI, G.check) == 1, "W8: disabled does nothing");
}

static void test_radio_tabs(void)
{
    mono_gallery(&LAPTOP);
    /* roving focus: only the chosen radio and tab are in the Tab order */
    zui_focus(&UI, G.slider, true);
    zui_focus(&UI, G.check, true);
    zui_id_t next = zui_focus_next(&UI, false);
    CHECK(next == G.radio[1], "W8: Tab reaches the chosen radio, not the first (%d)", next);
    key(ZUI_KEY_DOWN, 0);
    CHECK(UI.focus == G.radio[2] && zui_checked(&UI, G.radio[2]) == 1 &&
              zui_checked(&UI, G.radio[1]) == 0 && last_act == ZUI_ACT_SELECT && last_arg == 2,
          "W8: an arrow moves the choice and the focus");
    key(ZUI_KEY_DOWN, 0);
    CHECK(UI.focus == G.radio[0] && zui_choice_index(&UI, G.radio[0]) == 0, "W8: and wraps");
    CHECK(a11y_has("radio") && a11y_has("radio \"Small\" selected"), "W8: radios say their place");
    int32_t x, y;
    center(G.radio[1], &x, &y);
    click(x, y);
    CHECK(zui_checked(&UI, G.radio[1]) == 1 && zui_checked(&UI, G.radio[0]) == 0 && last_arg == 1,
          "W8: a click picks one");
    next = zui_focus_next(&UI, false);
    CHECK(next != G.radio[2] && next != G.radio[0], "W8: Tab leaves the group");
    /* tabs */
    zui_widget_t *tl = zui_w(&UI, G.tabs);
    zui_id_t t0 = tl->first, t1 = UI.w[t0].next, t2 = UI.w[t1].next;
    zui_focus(&UI, t0, true);
    key(ZUI_KEY_RIGHT, 0);
    CHECK(UI.focus == t1 && tl->u.tabs.sel == 1 && last_w == G.tabs && last_act == ZUI_ACT_SELECT &&
              last_arg == 1,
          "W8: Right picks the next tab");
    center(t2, &x, &y);
    click(x, y);
    CHECK(tl->u.tabs.sel == 2 && zui_choice_index(&UI, t2) == 2, "W8: a click picks a tab");
    zui_tabs_select(&UI, G.tabs, 0);
    CHECK(tl->u.tabs.sel == 0, "W8: zui_tabs_select");
    CHECK(a11y_has("tablist") && a11y_has("tab \"General\" selected"), "W8: tabs read as tabs");
    zui_focus(&UI, t0, true);
    next = zui_focus_next(&UI, false);
    CHECK(next != t1 && next != t2, "W8: one tab in the Tab order");
}

static void test_slider(void)
{
    mono_gallery(&LAPTOP);
    zui_focus(&UI, G.slider, true);
    key(ZUI_KEY_RIGHT, 0);
    CHECK(zui_slider_value(&UI, G.slider) == 55 && last_act == ZUI_ACT_CHANGE && last_arg == 55,
          "W8: an arrow steps");
    key(ZUI_KEY_PAGE_UP, 0);
    CHECK(zui_slider_value(&UI, G.slider) == 100, "W8: Page Up, clamped at the end");
    key(ZUI_KEY_HOME, 0);
    CHECK(zui_slider_value(&UI, G.slider) == 0, "W8: Home");
    key(ZUI_KEY_END, 0);
    CHECK(zui_slider_value(&UI, G.slider) == 100, "W8: End");
    key(ZUI_KEY_PAGE_DOWN, 0);
    CHECK(zui_slider_value(&UI, G.slider) == 50, "W8: Page Down, ten steps");
    zui_slider_set(&UI, G.slider, 37);
    CHECK(zui_slider_value(&UI, G.slider) == 35 || zui_slider_value(&UI, G.slider) == 40,
          "W8: set lands on a step");
    zui_slider_set(&UI, G.slider, 500);
    CHECK(zui_slider_value(&UI, G.slider) == 100, "W8: set is clamped");
    zui_widget_t *w = zui_w(&UI, G.slider);
    click(w->r.x + 1, w->r.y + w->r.h / 2);
    CHECK(zui_slider_value(&UI, G.slider) == 0, "W8: a press at the left end");
    now += 1000;
    send(ev(ZUI_EV_PTR_DOWN, w->r.x + 1, w->r.y + w->r.h / 2));
    zui_event_t m = ev(ZUI_EV_PTR_MOVE, w->r.x + w->r.w - 1, w->r.y + w->r.h / 2);
    m.button = ZUI_BTN_LEFT;
    send(m);
    send(ev(ZUI_EV_PTR_UP, w->r.x + w->r.w - 1, w->r.y + w->r.h / 2));
    CHECK(zui_slider_value(&UI, G.slider) == 100, "W8: a drag to the right end");
    zui_event_t sc = ev(ZUI_EV_SCROLL, w->r.x + w->r.w / 2, w->r.y + w->r.h / 2);
    sc.dy = 40; /* down is less */
    send(sc);
    int32_t v = zui_slider_value(&UI, G.slider);
    CHECK(v < 100 && v % 5 == 0, "W8: the wheel steps (%d)", v);
    CHECK(a11y_has("slider") && a11y_has("of 0..100"), "W8: the slider reads its value");
}

/* ---- W9: dialogs ----------------------------------------------------------- */

static void test_dialog(void)
{
    mono_gallery(&LAPTOP);
    zui_focus(&UI, G.save, true);
    zui_dialog_open(&UI, G.dialog);
    lay();
    repaint();
    zui_widget_t *d = zui_w(&UI, G.dialog);
    CHECK(UI.modal == G.dialog && !d->hidden && d->r.w > 0, "W9: open");
    CHECK(UI.focus == G.cancel || UI.focus == G.ok, "W9: focus moves into it");
    int32_t x, y;
    center(G.save, &x, &y);
    CHECK(zui_hit(&UI, x, y) == G.dialog, "W9: the scrim takes what is behind");
    reset_act();
    click(x, y);
    CHECK(n_act == 0 || last_w != G.save, "W9: nothing behind can be pressed");
    for (int i = 0; i < 6; i++) {
        zui_id_t f = zui_focus_next(&UI, false);
        CHECK(f == G.cancel || f == G.ok, "W9: Tab stays inside (%d)", f);
    }
    CHECK(a11y_has("dialog") && a11y_has("modal"), "W9: read as a modal dialog");
    zui_widget_t *sv = zui_w(&UI, G.save);
    CHECK(any(d->r, ZUI_PANEL) && at(sv->r.x + 3, sv->r.y + sv->r.h / 2) != ZUI_GOLD,
          "W9: a panel over a scrim that dims what is behind");
    key(ZUI_KEY_ESCAPE, 0);
    CHECK(UI.modal == ZUI_NONE && zui_w(&UI, G.dialog)->hidden && last_w == G.dialog &&
              last_act == ZUI_ACT_DISMISS,
          "W9: Escape closes it with DISMISS");
    CHECK(UI.focus == G.save, "W9: focus goes back");
    /* a sheet on a phone */
    zui_screen_t phone = {390, 800, 256, ZUI_INPUT_TOUCH};
    mono_gallery(&phone);
    zui_dialog_open(&UI, G.dialog);
    lay();
    d = zui_w(&UI, G.dialog);
    if (getenv("ZXV_KIT_DBG")) printf("sheet %d %d %d %d\n", d->r.x, d->r.y, d->r.w, d->r.h);
    CHECK(d->u.dialog.sheet && d->r.y + d->r.h == 800 && d->r.x == 0 && d->r.w == 390,
          "W9: a bottom sheet on a compact screen");
    center(G.ok, &x, &y);
    reset_act();
    click(x, y);
    CHECK(last_w == G.ok && last_act == ZUI_ACT_PRESS, "W9: its buttons work");
    zui_dialog_close(&UI, G.dialog);
    CHECK(UI.modal == ZUI_NONE, "W9: closed by the app");
}

/* ---- W10: tooltips ----------------------------------------------------------- */

static void test_tooltip(void)
{
    mono_gallery(&LAPTOP);
    int32_t x, y;
    center(G.save, &x, &y);
    send(ev(ZUI_EV_PTR_MOVE, x, y));
    zui_rect_t r;
    CHECK(!zui_tooltip_shown(&UI, &r) && r.w == 0, "W10: not at once");
    tick(ZUI_TIP_MS - 10);
    CHECK(!zui_tooltip_shown(&UI, 0), "W10: not before ZUI_TIP_MS");
    tick(20);
    const char *t = zui_tooltip_shown(&UI, &r);
    zui_widget_t *b = zui_w(&UI, G.save);
    CHECK(t && !strcmp(t, "Save the file") && r.y >= b->r.y + b->r.h, "W10: then, under it");
    repaint();
    uint32_t c = at(r.x + r.w / 2, r.y + 1);
    CHECK(c != ZUI_PANEL && c != ZUI_VOID, "W10: fading in (not yet the panel)");
    tick(ZUI_MOTION_MS);
    repaint();
    CHECK(any(r, ZUI_PANEL), "W10: fully in after ZUI_MOTION_MS");
    CHECK(a11y_has("Save the file"), "W10: read as the description");
    key('x', 0);
    CHECK(!zui_tooltip_shown(&UI, 0), "W10: a key hides it");
    /* keyboard focus arms it; reduce motion shows it at once, opaque */
    UI.reduce_motion = true;
    zui_focus(&UI, G.list, true);
    zui_focus(&UI, G.save, true);
    tick(ZUI_TIP_MS);
    CHECK(zui_tooltip_shown(&UI, &r) != 0, "W10: keyboard focus shows it");
    repaint();
    CHECK(any(r, ZUI_PANEL), "W10: no fade under reduce motion");
    click(5, 5);
    CHECK(!zui_tooltip_shown(&UI, 0), "W10: a press hides it");
}

/* ---- W11: overflow, W12 empty states, the wrap, the list ------------------- */

static void test_overflow(void)
{
    mono_gallery(&LAPTOP);
    zui_rect_t tr = zui_scroll_track(&UI, G.list);
    zui_widget_t *l = zui_w(&UI, G.list);
    CHECK(tr.w > 0 && tr.h > 0 && tr.x + tr.w <= l->r.x + l->r.w && tr.x > l->r.x + l->r.w / 2,
          "W11: a track along the list's right edge");
    CHECK(any(tr, ZUI_MUTED), "W11: a muted thumb");
    CHECK(a11y_has("rows below"), "W11: says how much is hidden");
    int32_t top = l->u.list.top;
    click(tr.x + tr.w / 2, tr.y + tr.h - 2);
    CHECK(l->u.list.top > top, "W11: a press on the track pages down (%d)", l->u.list.top);
    CHECK(a11y_has("rows above"), "W11: and now above too");
    zui_list_set_rows(&UI, G.list, 2);
    lay();
    tr = zui_scroll_track(&UI, G.list);
    CHECK(tr.w == 0 && tr.h == 0, "W11: no track when it all fits");
}

static void test_empty_wrap(void)
{
    mono_gallery(&LAPTOP);
    zui_widget_t *e = zui_w(&UI, G.empty);
    CHECK(e->kind == ZUI_W_EMPTY && a11y_has("No files here yet"), "W12: an empty state");
    zui_id_t act = e->last;
    int32_t x, y;
    center(act, &x, &y);
    reset_act();
    click(x, y);
    CHECK(last_w == G.empty && last_act == ZUI_ACT_PRESS, "W12: its action reaches the app");
    zui_widget_t *box = zui_w(&UI, e->parent);
    CHECK(e->r.x >= box->r.x && e->r.x + e->r.w <= box->r.x + box->r.w, "W12: inside its box");
    repaint();
    zui_rect_t icon = {e->r.x, e->r.y, e->r.w, zui_w(&UI, e->first)->r.y - e->r.y};
    CHECK(icon.h > 0 && any(icon, ZUI_MUTED), "W12: an engraved icon above the line");

    /* a row of buttons wraps on a phone */
    zui_screen_t phone = {390, 800, 256, ZUI_INPUT_TOUCH};
    zui_theme_t th = mono_theme();
    zui_ui_init(&UI, &phone, &th);
    zui_id_t root = zui_box(&UI, ZUI_NONE, ZUI_COL);
    zui_set_pad(&UI, root, 16, 8);
    zui_id_t r = zui_box(&UI, root, ZUI_ROW);
    zui_set_pad(&UI, r, 0, 8);
    zui_set_wrap(&UI, r, true);
    zui_id_t b[6];
    static const char *names[] = {"Open", "Rename", "Share", "Duplicate", "Delete", "Info"};
    for (int i = 0; i < 6; i++) b[i] = zui_button(&UI, r, names[i], ZUI_BTN_SECONDARY);
    lay();
    bool inside = true, two = false;
    for (int i = 0; i < 6; i++) {
        zui_widget_t *w = zui_w(&UI, b[i]);
        inside &= w->r.x >= 0 && w->r.x + w->r.w <= 390 && w->r.w > 0;
        two |= w->r.y != zui_w(&UI, b[0])->r.y;
    }
    if (getenv("ZXV_KIT_DBG"))
        for (int i = 0; i < 6; i++)
            printf("b%d %d %d %d %d\n", i, zui_w(&UI, b[i])->r.x, zui_w(&UI, b[i])->r.y,
                   zui_w(&UI, b[i])->r.w, zui_w(&UI, b[i])->r.h);
    CHECK(inside && two, "W: the row wraps onto two lines, all on screen");
    bool overlap = false;
    for (int i = 0; i < 6; i++)
        for (int j = i + 1; j < 6; j++) {
            zui_rect_t p = zui_w(&UI, b[i])->r, q = zui_w(&UI, b[j])->r;
            overlap |= p.x < q.x + q.w && q.x < p.x + p.w && p.y < q.y + q.h && q.y < p.y + p.h;
        }
    CHECK(!overlap, "W: wrapped buttons do not overlap");
    zui_id_t last = b[5];
    CHECK(zui_w(&UI, r)->r.h >= zui_w(&UI, last)->r.y + zui_w(&UI, last)->r.h - zui_w(&UI, r)->r.y,
          "W: the row is as tall as its lines");
}

static bool leads = true;
static void long_row(const zui_ui_t *ui, zui_id_t l, int32_t r, zui_row_t *o, void *ctx)
{
    (void) ui;
    (void) l;
    (void) ctx;
    snprintf(o->text, sizeof o->text, "Alpha %d", r);
    snprintf(o->detail, sizeof o->detail, "%s",
             r == 1 ? "A very long detail that would run across the whole row and over its name"
                    : "1 KB");
    o->lead_rgb = leads && r == 0 ? ZUI_OK : 0;
}

/* the first column at or after x0 with ink (not the ground) in row r */
static int32_t ink_from(zui_rect_t lr, int32_t row_h, int r, int32_t x0)
{
    for (int32_t x = x0; x < lr.x + lr.w; x++)
        for (int32_t y = lr.y + r * row_h; y < lr.y + (r + 1) * row_h; y++)
            if (at(x, y) != ZUI_VOID) return x;
    return -1;
}

static zui_rect_t one_list(int32_t *row_h)
{
    zui_screen_t sc = {640, 400, 256, ZUI_INPUT_POINTER};
    zui_theme_t th = font_theme();
    zui_ui_init(&UI, &sc, &th);
    zui_id_t root = zui_box(&UI, ZUI_NONE, ZUI_COL);
    zui_id_t l = zui_list(&UI, root, long_row, 0);
    zui_list_set_rows(&UI, l, 3);
    zui_list_select(&UI, l, -1);
    lay();
    zui_widget_t *w = zui_w(&UI, l);
    *row_h = 0;
    for (int32_t y = w->r.y; y < w->r.y + w->r.h && !*row_h; y++)
        if (zui_list_row_at(&UI, l, w->r.x + 5, y) == 1) *row_h = y - w->r.y;
    return w->r;
}

static void test_list_columns(void)
{
    if (!have_shotfont) return; /* reads text ink */
    int32_t rh;
    leads = false;
    zui_rect_t r = one_list(&rh);
    int32_t bare = ink_from(r, rh, 1, r.x);
    leads = true;
    r = one_list(&rh);
    int32_t t1 = ink_from(r, rh, 1, r.x), t2 = ink_from(r, rh, 2, r.x);
    int32_t dot = ink_from(r, rh, 0, r.x);
    CHECK(rh > 0 && bare > 0 && t1 > bare && t1 == t2,
          "W: one row's lead dot moves every row's name over (%d, %d %d)", bare, t1, t2);
    CHECK(dot >= 0 && dot < t1 && any((zui_rect_t){r.x, r.y, t1 - r.x, rh}, ZUI_OK) &&
              ink_from(r, rh, 0, dot + 12) <= t1 + 1,
          "W: the dot in the lead column, the name in line with the others");
    /* the detail column: no further left than 3/5 of the row */
    int32_t x = t1, gap = 0, detail = -1;
    for (; x < r.x + r.w && detail < 0; x++) {
        bool ink = false;
        for (int32_t y = r.y + rh; y < r.y + 2 * rh; y++) ink |= at(x, y) != ZUI_VOID;
        if (ink && gap >= 16) detail = x;
        gap = ink ? 0 : gap + 1;
    }
    CHECK(detail >= r.x + r.w * 3 / 5 - 2, "W: the detail keeps to its column (%d of %d)",
          detail - r.x, r.w);
}

/* ---- the focus ring, cap height ---------------------------------------------- */

static void test_focus_cap(void)
{
    mono_gallery(&LAPTOP);
    zui_focus(&UI, G.save, true);
    repaint();
    zui_widget_t *b = zui_w(&UI, G.save);
    int32_t y = b->r.y + b->r.h / 2;
    CHECK(at(b->r.x - 1, y) == ZUI_VOID && at(b->r.x - 3, y) == ZUI_GOLD,
          "W: two-tone focus ring: a ground gap, then gold");
    zui_ui_set_colors(&UI, zui_scheme_colors(ZUI_SCHEME_LIGHT));
    repaint();
    const zui_colors_t *lt = zui_scheme_colors(ZUI_SCHEME_LIGHT);
    CHECK(at(b->r.x - 1, y) == lt->c[ZUI_C_VOID] && at(b->r.x - 3, y) == lt->c[ZUI_C_GOLD],
          "W: the ring follows the scheme");

    /* capitals centre in the button (when the real face is here to measure) */
    if (!have_shotfont) return;
    zui_screen_t sc = {640, 400, 256, ZUI_INPUT_POINTER};
    zui_theme_t th = font_theme();
    zui_ui_init(&UI, &sc, &th);
    zui_surface_init(&S, PX, 640, 400, 640);
    zui_id_t root = zui_box(&UI, ZUI_NONE, ZUI_COL);
    zui_set_pad(&UI, root, 16, 8);
    zui_id_t hb = zui_button(&UI, root, "HHHH", ZUI_BTN_SECONDARY);
    repaint();
    zui_widget_t *w = zui_w(&UI, hb);
    int32_t top = -1, bot = -1;
    for (int32_t yy = w->r.y + 2; yy < w->r.y + w->r.h - 2; yy++)
        for (int32_t x = w->r.x + 4; x < w->r.x + w->r.w - 4; x++)
            if (at(x, yy) == ZUI_TEXT) {
                if (top < 0) top = yy;
                bot = yy;
                break;
            }
    int32_t above = top - w->r.y, below = w->r.y + w->r.h - 1 - bot;
    CHECK(top > 0 && (above - below <= 1 && below - above <= 1),
          "W: capitals centred (%d above, %d below)", above, below);
}

/* ---- W14: the caret in right-to-left text ------------------------------------ */

static char rbuf[128];
static void test_bidi_caret(void)
{
    zui_theme_t th = mono_theme();
    zui_ui_init(&UI, &LAPTOP, &th);
    zui_ui_on(&UI, on, 0);
    zui_surface_init(&S, PX, W, H, W);
    zui_id_t root = zui_box(&UI, ZUI_NONE, ZUI_COL);
    zui_set_pad(&UI, root, 16, 8);
    zui_id_t f = zui_field(&UI, root, rbuf, sizeof rbuf, false);
    strcpy(rbuf, "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D"); /* shalom: 4 letters, 8 bytes */
    zui_field_set(&UI, f, 8);
    lay();
    zui_rect_t c0, c8, cl;
    zui_field_select(&UI, f, 0, 0);
    zui_field_caret_rect(&UI, f, &c0);
    zui_field_select(&UI, f, 8, 8);
    zui_field_caret_rect(&UI, f, &c8);
    CHECK(c8.x > c0.x, "W14: without the hook, left to right (%d %d)", c0.x, c8.x);
    zui_ui_use_bidi(&UI, true);
    CHECK(UI.caret == zui_caret_bidi, "W14: the hook installed");
    zui_field_select(&UI, f, 0, 0);
    zui_field_caret_rect(&UI, f, &c0);
    zui_field_select(&UI, f, 8, 8);
    zui_field_caret_rect(&UI, f, &c8);
    zui_widget_t *w = zui_w(&UI, f);
    CHECK(c0.x > c8.x, "W14: right to left: the start is on the right (%d %d)", c0.x, c8.x);
    CHECK(c0.x > w->r.x + w->r.w / 2, "W14: the paragraph aligns right");
    zui_focus(&UI, f, true);
    zui_field_select(&UI, f, 0, 0);
    key(ZUI_KEY_LEFT, 0);
    uint32_t a, b;
    zui_field_selection(&UI, f, &a, &b);
    CHECK(a == 2 && b == 2, "W14: Left moves forward in a right-to-left word (%u)", a);
    zui_field_caret_rect(&UI, f, &cl);
    CHECK(cl.x < c0.x && cl.x > c8.x, "W14: and the caret moves left");
    key(ZUI_KEY_RIGHT, 0);
    zui_field_selection(&UI, f, &a, &b);
    CHECK(a == 0, "W14: Right goes back");
    click(c8.x - 20, c8.y + c8.h / 2);
    zui_field_selection(&UI, f, &a, &b);
    CHECK(a == 8, "W14: a click left of the text is its logical end (%u)", a);
    /* mixed: Latin then Hebrew */
    strcpy(rbuf, "ab \xD7\x90\xD7\x91");
    zui_field_set(&UI, f, (uint32_t) strlen(rbuf));
    zui_rect_t m3, m5, m7;
    zui_field_select(&UI, f, 3, 3);
    zui_field_caret_rect(&UI, f, &m3);
    zui_field_select(&UI, f, 5, 5);
    zui_field_caret_rect(&UI, f, &m5);
    zui_field_select(&UI, f, 7, 7);
    zui_field_caret_rect(&UI, f, &m7);
    CHECK(m5.x > m7.x && m5.x > m3.x, "W14: mixed text: after alef is right of bet (%d %d %d)",
          m3.x, m5.x, m7.x);
    zui_field_select(&UI, f, 7, 7);
    zui_event_t te = ev(ZUI_EV_TEXT, 0, 0);
    strcpy(te.text, "\xD7\x92");
    send(te);
    CHECK(!strcmp(rbuf, "ab \xD7\x90\xD7\x91\xD7\x92"), "W14: edits stay logical");
    zui_ui_use_bidi(&UI, false);
    CHECK(UI.caret == 0, "W14: and off again");
}

/* ---- NC11: previews off --------------------------------------------------------- */

static void test_notice_summary(void)
{
    zui_notice_init(&NC);
    zui_notice_spec_t a = {"Chat", "ana", "Ana", "See you at noon", ZUI_NPRI_NORMAL, 100, 0, 0, 0};
    zui_notice_spec_t b = {"Chat", "bo", "Bo", "Lunch?", ZUI_NPRI_NORMAL, 110, 0, 0, 0};
    zui_notice_spec_t c = {"Mail", "", "Invoice", "Your invoice", ZUI_NPRI_NORMAL, 120, 0, 0, 0};
    zui_notice_post(&NC, &a, 100);
    zui_notice_post(&NC, &b, 110);
    zui_notice_post(&NC, &c, 120);
    char s[64];
    zui_notice_summary(&NC, "Chat", s, sizeof s);
    CHECK(!strcmp(s, "2 notifications"), "NC11: '%s'", s);
    zui_notice_summary(&NC, "Mail", s, sizeof s);
    CHECK(!strcmp(s, "1 notification"), "NC11: '%s'", s);
    zui_surface_init(&S, PX, W, H, W);
    int32_t shown = zui_notice_draw_center(&NC, &S, (zui_rect_t){800, 0, 480, 800}, 0);
    NC.hide_previews = true;
    int32_t hidden = zui_notice_draw_center(&NC, &S, (zui_rect_t){800, 0, 480, 800}, 0);
    CHECK(hidden > 0 && hidden < shown, "NC11: one card per app when previews are off (%d < %d)",
          hidden, shown);
}

/* ---- WM: the doors under reduce motion; the strip on the panel's edge -------- */

static void test_wm_motion(void)
{
    zui_motion_reset();
    zui_wm_init(&WM, W, H, false);
    uint32_t id = zui_wm_open(&WM, "a", "A", 0, 0);
    zui_wm_progress(&WM, id, 256);
    zui_wm_tick(&WM, ZUI_MOTION_MS);
    CHECK(zui_wm_get(&WM, id)->phase == ZUI_WIN_OPENING && ZUI_WM_OPEN_MS > ZUI_MOTION_MS,
          "WM2: the doors are a sequence, longer than ZUI_MOTION_MS");
    zui_wm_tick(&WM, ZUI_WM_OPEN_MS);
    CHECK(zui_wm_get(&WM, id)->phase == ZUI_WIN_OPEN, "WM2: then open");
    zui_motion_set_flag(ZUI_PREF_REDUCE_MOTION, true);
    uint32_t id2 = zui_wm_open(&WM, "b", "B", 0, 0);
    zui_wm_progress(&WM, id2, 256);
    zui_wm_tick(&WM, ZUI_MOTION_MS);
    CHECK(zui_wm_get(&WM, id2)->phase == ZUI_WIN_OPEN,
          "WM2: under reduce motion they are done within ZUI_MOTION_MS");
    zui_motion_reset();
    zui_wm_use_dock(&WM, false);
    zui_wm_strip_place(&WM, 1, 64);
    zui_rect_t s = zui_wm_strip_rect(&WM), a = zui_wm_work(&WM);
    CHECK(s.x == 0 && s.w == 64 && s.h == H && a.x == 64 && a.w == W - 64, "WM6: the left edge");
    zui_rect_t sl = zui_wm_strip_slot(&WM, 0, 2), sl2 = zui_wm_strip_slot(&WM, 1, 2);
    CHECK(sl.x >= 0 && sl.x + sl.w <= 64 && sl2.y > sl.y, "WM6: slots run down the side");
    uint32_t hid;
    CHECK(zui_wm_hit(&WM, sl.x + 2, sl.y + 2, &hid, 0) == 6 && hid, "WM6: and are hit there");
    zui_wm_strip_place(&WM, 3, 48);
    CHECK(zui_wm_strip_rect(&WM).y == 0 && zui_wm_work(&WM).y == 48, "WM6: the top edge");
    zui_wm_strip_place(&WM, 0, 0);
    CHECK(zui_wm_work(&WM).h == H, "WM6: size 0 hides it");
}

int main(void)
{
    ttf_parse(&MINI, MINI_TTF, sizeof MINI_TTF);
    for (int i = 0; i < 4; i++) zui_glyph_cache_init(&CACHE[i]);
    SHOTS = getenv("ZXV_KIT_SHOTS");
    const char *fp = getenv("ZXV_KIT_FONT"); /* the kit's own face by default */
    if (!fp) fp = "../assets/fonts/ascw-golden/ASCW-Golden-Latin2.ttf";
    static uint8_t fdata[4u << 20];
    FILE *f = fp ? fopen(fp, "rb") : 0;
    if (f) {
        size_t n = fread(fdata, 1, sizeof fdata, f);
        fclose(f);
        have_shotfont = ttf_parse(&SHOTFONT, fdata, (uint32_t) n);
    }
    test_schemes();
    test_toggle_check();
    test_radio_tabs();
    test_slider();
    test_dialog();
    test_tooltip();
    test_overflow();
    test_empty_wrap();
    test_list_columns();
    test_focus_cap();
    test_bidi_caret();
    test_notice_summary();
    test_wm_motion();
    printf("%s test_zui_kit: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails, checks);
    return fails ? 1 : 0;
}
