/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_views.c — the five built-in apps' native screens (zv.h) on their
 * models, against zxvd's real dispatcher in this process (as test_apps.c):
 * files.* on a scratch ZXV folder, swarm.state and swarm.ask stubbed.
 * Clicks and keys go through zui's event path, so the wiring from a pixel
 * to a model call is what is tested. Murmuration also runs its KRON logic
 * and Eve logic (the swarm_shares block apps/murmuration.zxapp uses) and is checked against its C
 * model on random swarms. Then the Settings, Terminal and Playground screens
 * (settings.h, terminal.h, playground.h) on their own. Run by test_views.sh. */
#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700 /* nftw */
#define _DARWIN_C_SOURCE
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "burrow.h"
#include "glint.h"
#include "autoview.h"
#include "circuit.h"
#include "murmuration.h"
#include "quillon.h"
#include "playground.h"
#include "settings.h"
#include "spindle.h"
#include "terminal.h"
#include "zui_keymap.h"
#include "zui_motion.h"
#include "zui_tokens.h"
#include "zxapp_eve.h"
#include "zxapp_kron.h"
#include "zxv_files.h"
#include "zxvd_rpc.h"

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

static int rm_one(const char *p, const struct stat *st, int flag, struct FTW *f)
{
    (void) st, (void) flag, (void) f;
    return remove(p);
}

/* ---------------- zxvd in this process ---------------- */

#define E_NEUTRAL "\xf0\x9f\x98\x90" /* U+1F610 */
#define E_JOY     "\xf0\x9f\x98\x84" /* U+1F604 */
#define E_CURIOUS "\xf0\x9f\xa4\x94" /* U+1F914 */
#define E_SAD     "\xf0\x9f\x98\xa2" /* U+1F622 */

/* three agents: 450 + 300 + 250 = 1000 tokens this cycle */
static const char STATE[] =
    "{\"version\":\"1.0.0\",\"host\":\"hive\",\"cores\":8,\"levels\":3,\"tokens_per_cycle\":1000,"
    "\"tick\":4242,\"cycle\":17,\"mood\":\"" E_JOY " 3\",\"ledger_postings\":96,\"ledger_halt\":0,"
    "\"audit\":\"active\",\"audit_records\":12,\"agents\":["
    "{\"id\":1,\"name\":\"Companion\",\"level\":0,\"emoji\":\"" E_NEUTRAL "\",\"intensity\":0,"
    "\"re\":400,\"mk\":50,\"im\":0,\"witness\":1,\"witness_by\":\"Coder-2\"},"
    "{\"id\":2,\"name\":\"Coder-2\",\"level\":1,\"emoji\":\"" E_SAD "\",\"intensity\":4,"
    "\"re\":200,\"mk\":0,\"im\":100,\"witness\":2,\"witness_by\":\"Critic-3\"},"
    "{\"id\":3,\"name\":\"Critic-3\",\"level\":2,\"emoji\":\"" E_CURIOUS "\",\"intensity\":2,"
    "\"re\":150,\"mk\":75,\"im\":25,\"witness\":0,\"witness_by\":\"Companion\"}],"
    "\"model\":{\"loaded\":true,\"can_generate\":false,\"name\":\"tiny.gguf\",\"arch\":\"llama\","
    "\"status\":\"loaded\"}}";

static int asked;
static void h_ask(const char *q, zxvd_buf_t *o)
{
    (void) q;
    asked++;
    zxvd_buf_json_str(o, "A calm, busy swarm.");
}
static void h_state(zxvd_buf_t *o)
{
    zxvd_buf_raw(o, STATE, strlen(STATE));
}
static const zxvd_host_ops_t OPS = {.state = h_state, .ask = h_ask, .app_version = "test"};

static zxvd_buf_t OUT;
static int32_t inproc(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    (void) ctx;
    if (!zxvd_rpc_handle_line(req, len, &OUT)) return -1;
    memcpy(resp, OUT.p, OUT.len < cap ? OUT.len : cap);
    return (int32_t) OUT.len;
}
static int32_t dead(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    (void) ctx, (void) req, (void) len, (void) resp, (void) cap;
    return -1;
}
static zxm_link_t LINK, DEAD;

/* ---------------- one screen, driven by events ---------------- */

static zui_ui_t UI;
static uint32_t PX[1280 * 800];
static zui_surface_t SURF;
static uint32_t now = 1000;
static zui_screen_t LAPTOP = {1280, 800, 256, ZUI_INPUT_POINTER};
static zui_screen_t PHONE = {390, 800, 256, ZUI_INPUT_TOUCH};

typedef bool (*act_fn)(void *v, zui_id_t w, zui_action_t a, int32_t arg);
static act_fn ACT;
static int unhandled;

static void on(zui_ui_t *ui, zui_id_t w, zui_action_t a, int32_t arg, void *ctx)
{
    (void) ui;
    if (!ACT(ctx, w, a, arg) && a != ZUI_ACT_CHANGE && a != ZUI_ACT_DISMISS) unhandled++;
}

/* Pictures of each screen, for people (not checked): ZXV_VIEWS_SHOTS names
 * a folder for PPM files and ZXV_VIEWS_FONT, ZXV_VIEWS_MONO TrueType files
 * to draw the text with. The checks themselves run with no font. */
static const char *SHOTS;
static ttf_font_t FONT, MONO;
static bool have_font, have_mono;
static zui_glyph_cache_t CACHE[4];

static bool load_font(const char *env, ttf_font_t *f)
{
    const char *p = getenv(env);
    FILE *fp = p ? fopen(p, "rb") : NULL;
    if (!fp) return false;
    static uint8_t data[2][4u << 20];
    static int k;
    uint8_t *d = data[k++ & 1];
    size_t n = fread(d, 1, sizeof data[0], fp);
    fclose(fp);
    return ttf_parse(f, d, (uint32_t) n);
}

static void paint(void);

static void shot(const char *name)
{
    if (!SHOTS) return;
    paint();
    char p[512];
    snprintf(p, sizeof p, "%s/%s.ppm", SHOTS, name);
    FILE *f = fopen(p, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", SURF.w, SURF.h);
    for (int32_t i = 0; i < SURF.w * SURF.h; i++) {
        unsigned char rgb[3] = {(unsigned char) (PX[i] >> 16), (unsigned char) (PX[i] >> 8),
                                (unsigned char) PX[i]};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static zui_theme_t mono_theme(void)
{
    zui_theme_t t;
    memset(&t, 0, sizeof t);
    t.body.size_px = 16; /* no font: fixed advance, deterministic */
    t.label.size_px = 12;
    t.title.size_px = 20;
    t.mono.size_px = 16;
    if (have_font) {
        zui_text_t *x[4] = {&t.body, &t.label, &t.title, &t.mono};
        for (int i = 0; i < 4; i++) {
            zui_glyph_cache_init(&CACHE[i]);
            x[i]->font = i == 3 && have_mono ? &MONO : &FONT;
            x[i]->cache = &CACHE[i];
        }
        t.label.size_px = 13;
        t.mono.size_px = 14;
        t.mono.tabular = true;
    }
    return t;
}

static void screen(const zui_screen_t *sc, act_fn act, void *view)
{
    zui_theme_t th = mono_theme();
    zui_ui_init(&UI, sc, &th);
    zui_ui_on(&UI, on, view);
    ACT = act;
    unhandled = 0;
    zui_surface_init(&SURF, PX, sc->px_w, sc->px_h, sc->px_w);
}

static void paint(void)
{
    zui_damage_all(&UI.damage, SURF.w, SURF.h);
    zui_ui_paint(&UI, &SURF);
}

static void send(zui_event_t e)
{
    zui_ui_event(&UI, &e);
    paint();
}

static zui_event_t ev(uint8_t type, int32_t x, int32_t y)
{
    zui_event_t e;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.x = x;
    e.y = y;
    e.button = ZUI_BTN_LEFT;
    e.ms = now;
    return e;
}

static bool shown(zui_id_t id)
{
    for (zui_widget_t *w = zui_w(&UI, id); w; w = zui_w(&UI, w->parent))
        if (w->hidden) return false;
    return true;
}

/* A click in the middle of a widget, as a person would make it. */
static bool click(zui_id_t id)
{
    paint();
    zui_widget_t *w = zui_w(&UI, id);
    if (!w || !shown(id) || w->r.w <= 0 || w->r.h <= 0) return false;
    int32_t x = w->r.x + w->r.w / 2, y = w->r.y + w->r.h / 2;
    now += 1000; /* never a double click by accident */
    send(ev(ZUI_EV_PTR_DOWN, x, y));
    now += 20;
    send(ev(ZUI_EV_PTR_UP, x, y));
    return true;
}

static void key(uint16_t k, uint8_t mods)
{
    zui_event_t e = ev(ZUI_EV_KEY_DOWN, 0, 0);
    e.key = k;
    e.mods = mods;
    send(e);
}

static void type(const char *s)
{
    zui_event_t e = ev(ZUI_EV_TEXT, 0, 0);
    snprintf(e.text, sizeof e.text, "%s", s);
    send(e);
}

static const char *text_of(zui_id_t id)
{
    return zui_w(&UI, id)->label;
}

static bool has(const char *hay, const char *needle)
{
    return hay && strstr(hay, needle) != NULL;
}

/* V4: the ground is the void green and nothing on it is a browser white. */
static void check_ground(const char *who)
{
    paint();
    uint32_t white = 0;
    for (int32_t i = 0; i < SURF.w * SURF.h; i++) white += (PX[i] & 0xFFFFFFu) == 0xFFFFFFu;
    CHECK((PX[SURF.w * (SURF.h - 2) + SURF.w / 2] & 0xFFFFFFu) != 0xFFFFFFu && white == 0,
          "%s: no white pixels (%u)", who, white);
    CHECK((PX[0] & 0xFFFFFFu) == ZUI_VOID, "%s: the corner is the ground, %06x", who, PX[0]);
}

/* ---------------- The Banker ---------------- */

static burrow_t B;
static burrow_view_t BV;
static char opened[256];
static void on_open(void *ctx, uint8_t kind, const char *space, const char *path)
{
    (void) ctx, (void) kind;
    snprintf(opened, sizeof opened, "%s:%s", space, path);
}
static bool burrow_act(void *v, zui_id_t w, zui_action_t a, int32_t arg)
{
    return burrow_view_action(v, w, a, arg);
}

static int32_t row_named(const burrow_t *b, const char *name)
{
    for (uint32_t i = 0; i < b->n; i++)
        if (!strcmp(b->e[i].name, name)) return (int32_t) i;
    return -1;
}

static void test_burrow(void)
{
    CHECK(burrow_init(&B, &LINK) == ZXM_OK, "burrow_init");
    screen(&LAPTOP, burrow_act, &BV);
    burrow_view_build(&BV, &UI, &B);
    BV.open_fn = on_open;
    check_ground("burrow");
    CHECK(zui_w(&UI, BV.list)->u.list.rows == (int32_t) B.n, "rows follow the model");
    CHECK(zui_w(&UI, BV.up)->disabled, "Up is off at the top");

    /* New folder: the button, a name, Return */
    CHECK(click(BV.new_dir) && shown(BV.name_field) && UI.focus == BV.name_field,
          "New folder asks for a name and takes the keys");
    type("Notes");
    key(ZUI_KEY_ENTER, 0);
    CHECK(row_named(&B, "Notes") >= 0 && !shown(BV.name_field), "the folder exists: %s",
          zui_w(&UI, BV.sh.error)->label);
    CHECK(has(text_of(BV.sh.status), "item"), "status counts items");

    /* a text file through the same field */
    CHECK(click(BV.new_text), "New text");
    type("todo.txt");
    key(ZUI_KEY_ENTER, 0);
    int32_t t = row_named(&B, "todo.txt");
    CHECK(t >= 0, "the file exists");

    /* select it with a click on the list, then the detail pane follows */
    burrow_view_action(&BV, BV.list, ZUI_ACT_SELECT, t);
    CHECK(B.sel == t && !strcmp(text_of(BV.d_name), "todo.txt") && shown(BV.trash),
          "selected: detail shows it");
    CHECK(click(BV.open) && has(opened, "todo.txt"), "Open hands the path to the host: %s", opened);
    shot("burrow");
    CHECK(click(BV.trash) && row_named(&B, "todo.txt") < 0, "Move to Trash");
    CHECK(click(BV.trash_view) && burrow_in_trash(&B) && shown(BV.empty), "the Trash");
    CHECK(shown(BV.none) == (B.n == 0) && shown(BV.list) == (B.n != 0),
          "W12: Burrow's empty state stands in for an empty list");
    if (B.n == 0)
        CHECK(!strcmp(text_of(zui_w(&UI, BV.none)->first), "The Trash is empty") &&
                  !shown(zui_w(&UI, BV.none)->last),
              "W12: the Trash says so, with no New folder");
    CHECK(click(BV.empty) && B.n == 0, "Empty Trash");
    CHECK(click(BV.trash_view) && !burrow_in_trash(&B), "back to files");

    /* a refused call shows zxvd's words, not a guess */
    B.link = &DEAD;
    burrow_view_action(&BV, BV.list, ZUI_ACT_OPEN, row_named(&B, "Notes"));
    CHECK(text_of(BV.sh.error)[0] != 0 && shown(BV.sh.error), "a failed call says so");
    B.link = &LINK;
    CHECK(burrow_view_action(&BV, BV.list, ZUI_ACT_OPEN, row_named(&B, "Notes")) &&
              !strcmp(B.dir, "Notes") && !zui_w(&UI, BV.up)->disabled,
          "opening a folder enters it");
    CHECK(text_of(BV.sh.error)[0] == 0, "success clears the error line");
    CHECK(click(BV.up) && B.dir[0] == 0, "Up");

    /* AI from the palette is held, and accept does not touch files */
    int before = asked;
    paint();
    key('k', ZUI_MOD_CMD);
    CHECK(UI.palette.open, "Cmd-K opens the Ask palette");
    key(ZUI_KEY_ENTER, 0);
    CHECK(asked == before + 1 && B.held.state == ZXM_HELD_PENDING && shown(BV.sh.held),
          "a pick asks, and the answer is held");
    burrow_view_action(&BV, BV.sh.held, ZUI_ACT_REJECT, 0);
    CHECK(B.held.state == ZXM_HELD_REJECTED, "reject");

    /* V3: on a phone the list and the detail take turns */
    screen(&PHONE, burrow_act, &BV);
    burrow_view_build(&BV, &UI, &B);
    paint();
    CHECK(zv_compact(&BV.sh) && shown(BV.list) && !shown(BV.detail), "compact: the list");
    burrow_view_action(&BV, BV.list, ZUI_ACT_SELECT, 0);
    CHECK(!shown(BV.list) && shown(BV.detail) && shown(BV.back), "compact: the detail");
    CHECK(click(BV.back) && shown(BV.list), "Back");
    CHECK(unhandled == 0, "every action reached the view (%d not)", unhandled);
}

/* ---------------- Quillon ---------------- */

static quillon_t Q;
static quillon_view_t QV;
static bool quillon_act(void *v, zui_id_t w, zui_action_t a, int32_t arg)
{
    return quillon_view_action(v, w, a, arg);
}

static void test_quillon(const char *root)
{
    quillon_new(&Q, &LINK);
    screen(&LAPTOP, quillon_act, &QV);
    quillon_view_build(&QV, &UI, &Q);
    check_ground("quillon");
    CHECK(has(text_of(QV.where), "Not saved"), "a new page");
    CHECK(click(QV.text) && UI.focus == QV.text, "the page takes focus");
    type("Hello ");
    type("swarm");
    CHECK(Q.len == 11 && !memcmp(Q.text, "Hello swarm", 11), "typing goes through the model: %.*s",
          (int) Q.len, Q.text);
    CHECK(has(text_of(QV.where), "edited") && has(text_of(QV.counts), "2 words"),
          "dirty mark and counts: %s", text_of(QV.counts));
    CHECK(click(QV.save_as) && shown(QV.as_field), "Save as asks for a path");
    type("hello.txt");
    key(ZUI_KEY_ENTER, 0);
    char p[512];
    snprintf(p, sizeof p, "%s/hello.txt", root);
    FILE *f = fopen(p, "rb");
    char got[32] = "";
    if (f) {
        size_t n = fread(got, 1, sizeof got - 1, f);
        got[n] = 0;
        fclose(f);
    }
    CHECK(!strcmp(got, "Hello swarm") && !quillon_dirty(&Q), "saved to disk: '%s'", got);

    /* AI on the selection: held, then accepted into the page, selected */
    zui_field_select(&UI, QV.text, 6, 11);
    int before = asked;
    quillon_view_action(&QV, QV.sh.root, ZUI_ACT_PICK, QUILLON_AI_REWRITE);
    CHECK(asked == before + 1 && Q.held.state == ZXM_HELD_PENDING &&
              !memcmp(Q.text, "Hello swarm", 11),
          "held: the page is unchanged until accepted");
    quillon_view_action(&QV, QV.sh.held, ZUI_ACT_ACCEPT, 0);
    uint32_t s0, s1;
    zui_field_selection(&UI, QV.text, &s0, &s1);
    CHECK(has(Q.text, "A calm, busy swarm.") && s1 > s0 && Q.held.state != ZXM_HELD_PENDING,
          "accepted: the new text is in and selected (%u..%u)", s0, s1);
    CHECK(click(QV.save) && !quillon_dirty(&Q), "Save");
    shot("quillon");
    CHECK(unhandled == 0, "every action reached the view (%d not)", unhandled);
}

/* ---------------- Glint ---------------- */

static glint_t G;
static glint_view_t GV;
static uint8_t GBUF[4096];
static bool glint_act(void *v, zui_id_t w, zui_action_t a, int32_t arg)
{
    return glint_view_action(v, w, a, arg);
}
static int decoded;
static bool decode(void *ctx, const uint8_t *data, uint32_t len, zui_surface_t *dst, zui_rect_t box)
{
    (void) ctx;
    decoded++;
    zui_fill_rrect(dst, box, 0, len && data[0] == 'P' ? ZUI_INFO : ZUI_ALERT, 255);
    return true;
}

static void test_glint(const char *root)
{
    const char *names[] = {"a.png", "b.jpg", "c.mp3"};
    for (int i = 0; i < 3; i++) {
        char p[512];
        snprintf(p, sizeof p, "%s/%s", root, names[i]);
        FILE *f = fopen(p, "wb");
        if (f) {
            fputs("PIXELS", f);
            fclose(f);
        }
    }
    {
        static glint_t none;
        screen(&LAPTOP, glint_act, &GV);
        glint_view_build(&GV, &UI, &none, GBUF, sizeof GBUF);
        CHECK(shown(GV.none) && !shown(GV.items) && !shown(GV.pane) &&
                  zui_w(&UI, GV.none)->kind == ZUI_W_EMPTY,
              "W12: Glint's empty state");
        shot("glint-empty");
    }
    CHECK(burrow_open(&B, "") == ZXM_OK, "reread the folder");
    glint_scan(&G, &B);
    screen(&LAPTOP, glint_act, &GV);
    glint_view_build(&GV, &UI, &G, GBUF, sizeof GBUF);
    GV.decode = decode;
    check_ground("glint");
    CHECK(G.n == 3 && zui_w(&UI, GV.items)->u.list.rows == 3, "three media items (%u)", G.n);
    CHECK(!shown(GV.none) && shown(GV.items), "W12: no empty state with items");
    CHECK(zui_w(&UI, GV.prev)->disabled && zui_w(&UI, GV.next)->disabled, "nothing viewed yet");
    glint_view_action(&GV, GV.items, ZUI_ACT_SELECT, 0);
    CHECK(G.cur == 0 && has(text_of(GV.caption), "a.png"), "viewing the first");
    CHECK(click(GV.next) && G.cur == 1 && !GV.loaded, "Next");
    CHECK(click(GV.load) && GV.loaded && GV.len == 6, "Show loads the bytes");
    paint();
    CHECK(decoded > 0, "the decoder drew the stage");
    shot("glint");
    CHECK(click(GV.prev) && G.cur == 0, "Previous");
    CHECK(unhandled == 0, "every action reached the view (%d not)", unhandled);
}

/* ---------------- Spindle ---------------- */

static spindle_t S;
static spindle_view_t SV;
static bool spindle_act(void *v, zui_id_t w, zui_action_t a, int32_t arg)
{
    return spindle_view_action(v, w, a, arg);
}

static void test_spindle(void)
{
    spindle_init(&S, &LINK);
    screen(&LAPTOP, spindle_act, &SV);
    spindle_view_build(&SV, &UI, &S);
    check_ground("spindle");
    CHECK(zui_w(&UI, SV.methods)->u.list.rows == 0, "no methods before discovery");
    CHECK(shown(SV.none) && !shown(SV.history) && !shown(SV.reply) &&
              zui_w(&UI, SV.none)->kind == ZUI_W_EMPTY,
          "W12: the empty state before the first call");
    CHECK(spindle_discover(&S) == ZXM_OK && S.n_methods > 0, "discover");
    spindle_view_sync(&SV);
    CHECK(zui_w(&UI, SV.methods)->u.list.rows == (int32_t) S.n_methods, "the methods are listed");
    spindle_view_action(&SV, SV.methods, ZUI_ACT_SELECT, 0);
    CHECK(!strcmp(SV.method_buf, S.methods[0]), "a picked method fills the field: %s",
          SV.method_buf);
    CHECK(click(SV.method) && UI.focus == SV.method, "the method field");
    zui_field_select(&UI, SV.method, 0, (uint32_t) strlen(SV.method_buf)); /* typing replaces it */
    type("swarm.state");
    CHECK(click(SV.call) && S.n_hist == 1, "Call");
    CHECK(zui_w(&UI, SV.history)->u.list.rows == 1, "history row");
    CHECK(!shown(SV.none) && shown(SV.history), "W12: the history replaces the empty state");
    CHECK(has(zui_w(&UI, SV.reply)->u.field.buf, "Companion"), "the reply is shown");
    CHECK(zui_w(&UI, SV.reply)->u.field.readonly, "the reply cannot be edited");
    shot("spindle");
    CHECK(unhandled == 0, "every action reached the view (%d not)", unhandled);
}

/* ---------------- Murmuration ---------------- */

static murmuration_t M;
static murm_view_t MV;
static zxapp_t APP;
static zxapp_host_t HOST;
static zxapp_t BLOCK;
static zxapp_registry_t REG;
static char MANIFEST[4096], BLOCK_SRC[4096];
static bool murm_act(void *v, zui_id_t w, zui_action_t a, int32_t arg)
{
    return murm_view_action(v, w, a, arg);
}

/* the manifest's paths are under kernel/src */
static int32_t read_src(void *ctx, const char *path, char *buf, uint32_t cap)
{
    char p[512];
    snprintf(p, sizeof p, "%s/%s", (const char *) ctx, path);
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return (int32_t) n;
}

static uint64_t rng = 0x9E3779B97F4A7C15u;
static uint64_t next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}

/* U1: KRON and C give the same numbers on any swarm the model can hold. */
static void cross_check(void)
{
    int bad_share = 0, bad_top = 0, kron = 0, agreed = 0, eve = 0, bad_over = 0, some_over = 0;
    for (int round = 0; round < 300; round++) {
        murm_swarm_t *sw = &M.sw;
        sw->n = sw->n_total = 1u + (uint32_t) (next() % MURM_AGENTS);
        sw->truncated = false;
        uint64_t sum = 0, scale = round % 3 == 0 ? 4u : round % 3 == 1 ? 1000u : 1000000000u;
        for (uint32_t i = 0; i < sw->n; i++) {
            murm_agent_t *a = &sw->agent[i];
            a->re = next() % scale;
            a->mk = round % 5 ? next() % scale : 0;
            a->im = next() % (scale / 2 + 1);
            a->intensity = (uint8_t) (next() % 6u);
            if (round % 7 == 0) a->re = a->mk = a->im = 0; /* nothing allotted */
            sum += a->re + a->mk + a->im;
        }
        sw->allotted = sum;
        M.sel = -1;
        murm_view_sync(&MV);
        kron += MV.kron;
        agreed += MV.shares_lp == ZXAPP_TRUE && MV.limits_lp == ZXAPP_TRUE;
        eve += MV.eve;
        for (uint32_t i = 0; i < sw->n; i++) {
            bool want = murm_cycle_share(&M, i) > MURM_VIEW_SHARE_CAP;
            bad_over += MV.over[i] != want;
            some_over += want;
        }
        for (uint32_t i = 0; i < sw->n; i++) bad_share += MV.share[i] != murm_cycle_share(&M, i);
        /* the busiest: largest totals, the earlier agent first on a tie */
        uint32_t want = sw->n < MURM_VIEW_BUSIEST ? sw->n : MURM_VIEW_BUSIEST;
        bad_top += MV.ntop != want;
        for (uint32_t k = 0; k < MV.ntop && k < want; k++) {
            uint32_t at = (uint32_t) MV.top[k];
            const murm_agent_t *x = &sw->agent[at];
            uint64_t tx = x->re + x->mk + x->im;
            uint32_t above = 0;
            for (uint32_t j = 0; j < sw->n; j++) {
                const murm_agent_t *y = &sw->agent[j];
                uint64_t ty = y->re + y->mk + y->im;
                above += ty > tx || (ty == tx && j < at);
            }
            bad_top += above != k;
        }
    }
    CHECK(kron == 300 && agreed == 300, "KRON ran and C witnessed it every round (%d, %d)", kron,
          agreed);
    CHECK(bad_share == 0, "KRON's shares equal the C model's (%d differ)", bad_share);
    CHECK(bad_top == 0, "KRON's busiest are the largest totals, stable (%d differ)", bad_top);
    CHECK(eve == 300 && bad_over == 0 && some_over > 0,
          "Eve's limits agree with the C cap (%d rounds, %d differ, %d over)", eve, bad_over,
          some_over);
}

static void test_murmuration(const char *src)
{
    murm_init(&M, &LINK);
    screen(&LAPTOP, murm_act, &MV);
    murm_view_build(&MV, &UI, &M);
    check_ground("murmuration");
    CHECK(zui_w(&UI, MV.agents)->u.list.rows == 0 && text_of(MV.sh.status)[0] == 0,
          "nothing before the first refresh");
    CHECK(zui_w(&UI, MV.refresh)->style == ZUI_BTN_PRIMARY, "U3: Refresh is the gold");
    CHECK(click(MV.refresh) && M.refreshes == 1, "Refresh");
    CHECK(zui_w(&UI, MV.agents)->u.list.rows == 3 && !MV.kron, "three agents, shares in C");
    CHECK(MV.share[0] == 450 && MV.share[1] == 300 && MV.share[2] == 250, "per mille: %u %u %u",
          MV.share[0], MV.share[1], MV.share[2]);
    CHECK(MV.ntop == 3 && MV.top[0] == 0 && MV.top[1] == 1 && MV.top[2] == 2, "busiest, in C");
    CHECK(has(text_of(MV.busiest), "Companion, Coder-2, Critic-3"), "%s", text_of(MV.busiest));
    CHECK(has(text_of(MV.sh.status), "3 agents") && has(text_of(MV.sh.status), "shares: C") &&
              MV.shares_lp == ZXAPP_NEITHER,
          "%s", text_of(MV.sh.status));
    CHECK(has(text_of(MV.model), "tiny.gguf"), "model: %s", text_of(MV.model));
    CHECK(has(text_of(MV.feeling), "Mood") && has(text_of(MV.witness), "Ledger running"),
          "the swarm's line with nothing selected");

    /* rows: emoji, name, share, and the witness colour */
    zui_row_t r;
    memset(&r, 0, sizeof r);
    zui_w(&UI, MV.agents)->u.list.row(&UI, MV.agents, 1, &r, &MV);
    CHECK(!strcmp(r.text, "Coder-2") && has(r.detail, "sad") && has(r.detail, "30.0%") &&
              r.lead_rgb == ZUI_INFO,
          "row: %s / %s", r.text, r.detail);
    memset(&r, 0, sizeof r);
    zui_w(&UI, MV.agents)->u.list.row(&UI, MV.agents, 2, &r, &MV);
    CHECK(r.lead_rgb == ZUI_ALERT, "a FALSE witness is the alert colour");

    /* selecting: the detail follows the agent */
    murm_view_action(&MV, MV.agents, ZUI_ACT_SELECT, 1);
    CHECK(M.sel == 1 && has(text_of(MV.who), "Coder-2") && has(text_of(MV.who), "level 1"), "%s",
          text_of(MV.who));
    CHECK(has(text_of(MV.allot), "200 real + 0 market + 100i") && has(text_of(MV.allot), "30.0%"),
          "%s", text_of(MV.allot));
    CHECK(has(text_of(MV.witness), "Contested by Critic-3"), "%s", text_of(MV.witness));
    CHECK(has(text_of(MV.feeling), "intensity 4"), "%s", text_of(MV.feeling));
    murm_view_action(&MV, MV.agents, ZUI_ACT_SELECT, 0);
    CHECK(has(text_of(MV.who), "companion"), "level 0 is the companion: %s", text_of(MV.who));
    murm_view_action(&MV, MV.agents, ZUI_ACT_SELECT, 2);
    CHECK(has(text_of(MV.witness), "Does not add up, says Companion"), "%s", text_of(MV.witness));
    paint(); /* the face, with the FAILING ring */

    /* the app's KRON logic, loaded from its manifest */
    char p[512];
    snprintf(p, sizeof p, "%s/apps/blocks/swarm_shares.zxblock", src);
    FILE *f = fopen(p, "rb");
    size_t n = f ? fread(BLOCK_SRC, 1, sizeof BLOCK_SRC - 1, f) : 0;
    if (f) fclose(f);
    zxapp_registry_init(&REG);
    CHECK(zxapp_parse(&BLOCK, BLOCK_SRC, (uint32_t) n) == ZXAPP_OK && BLOCK.block &&
              zxapp_registry_add(&REG, &BLOCK) == ZXAPP_OK,
          "the swarm_shares block registers: %s %s", BLOCK.err, REG.err);
    snprintf(p, sizeof p, "%s/apps/murmuration.zxapp", src);
    f = fopen(p, "rb");
    n = f ? fread(MANIFEST, 1, sizeof MANIFEST - 1, f) : 0;
    if (f) fclose(f);
    CHECK(zxapp_parse(&APP, MANIFEST, (uint32_t) n) == ZXAPP_OK &&
              zxapp_compose(&APP, &REG) == ZXAPP_OK && APP.n == 3,
          "the manifest parses and composes the block: %s", APP.err);
    zxapp_host_init(&HOST);
    HOST.read_file = read_src;
    HOST.read_ctx = (void *) src;
    murm_view_logic(&MV, &APP, &HOST);
    CHECK(!MV.kron, "no runtime yet: C");
    CHECK(zxapp_kron_register(&HOST) == ZXAPP_OK && zxapp_load(&APP, &HOST) == 2,
          "both KRON entries load, Eve waits for its runtime: %s", APP.err);
    murm_view_sync(&MV);
    CHECK(MV.kron && !MV.eve && has(text_of(MV.sh.status), "limits: C") &&
              MV.shares_lp == ZXAPP_TRUE && MV.limits_lp == ZXAPP_NEITHER,
          "KRON witnessed by C; limits in C alone");
    CHECK(zxapp_eve_register(&HOST) == ZXAPP_OK && zxapp_load(&APP, &HOST) == 3,
          "three entries in two languages: %s", APP.err);
    murm_view_sync(&MV);
    CHECK(MV.eve && !MV.over[0] && !MV.over[1] && !MV.over[2], "U4: everyone inside the limits");
    CHECK(MV.kron && MV.share[0] == 450 && MV.share[1] == 300 && MV.share[2] == 250 &&
              MV.ntop == 3 && MV.top[0] == 0,
          "U1: KRON gives the same numbers");
    CHECK(has(text_of(MV.sh.status), "shares: KRON = C") &&
              has(text_of(MV.sh.status), "limits: Eve = C") && MV.limits_lp == ZXAPP_TRUE,
          "U5: both witnessed: %s", text_of(MV.sh.status));
    /* one agent over half a cycle is marked, in the row and the detail */
    murm_swarm_t keep = M.sw;
    M.sw.agent[0].re = 700;
    M.sw.allotted = 700 + 50 + 300 + 250;
    murm_view_sync(&MV);
    CHECK(MV.eve && MV.over[0] && !MV.over[1] && MV.share[0] == 576, "U4: Eve marks 57.6%%");
    zui_row_t lr;
    memset(&lr, 0, sizeof lr);
    zui_w(&UI, MV.agents)->u.list.row(&UI, MV.agents, 0, &lr, &MV);
    CHECK(has(lr.detail, "over limit"), "the row says so: %s", lr.detail);
    murm_view_action(&MV, MV.agents, ZUI_ACT_SELECT, 0);
    CHECK(has(text_of(MV.limits), "Over the swarm's limits"), "%s", text_of(MV.limits));
    murm_view_action(&MV, MV.agents, ZUI_ACT_SELECT, -1);
    CHECK(has(text_of(MV.limits), "1 agent over"), "%s", text_of(MV.limits));
    M.sw = keep;
    M.sw.truncated = true;
    murm_view_sync(&MV);
    CHECK(!MV.kron, "a truncated snapshot uses C");
    M.sw.truncated = false;

    /* U5: a KRON that disagrees with C is held, never shown as the answer */
    static zxapp_t BAD, BAD_BLOCK;
    static zxapp_registry_t BAD_REG;
    static const char BAD_BLOCK_SRC[] = "zxblock 1\nid swarm_shares\nversion 1.0.1\n"
                                        "provides cycle_share num t\n"
                                        "logic kron cycle_share <<\n_999*t%1|+/t\n.\n"
                                        "provides busiest num t\n"
                                        "logic kron busiest <<\n(3&#t)#>t\n.\n"
                                        "provides within_limits truth share intensity\n"
                                        "logic kron within_limits <<\nshare+0*intensity\n.\n";
    static const char BAD_SRC[] = "zxapp 1\nid murmuration\nuse swarm_shares 1\n";
    zxapp_registry_init(&BAD_REG);
    CHECK(zxapp_parse(&BAD_BLOCK, BAD_BLOCK_SRC, (uint32_t) strlen(BAD_BLOCK_SRC)) == ZXAPP_OK &&
              zxapp_registry_add(&BAD_REG, &BAD_BLOCK) == ZXAPP_OK &&
              zxapp_parse(&BAD, BAD_SRC, (uint32_t) strlen(BAD_SRC)) == ZXAPP_OK &&
              zxapp_compose(&BAD, &BAD_REG) == ZXAPP_OK && zxapp_load(&BAD, &HOST) == 3,
          "a wrong KRON block loads: %s %s", BAD.err, BAD_REG.err);
    murm_view_logic(&MV, &BAD, &HOST);
    CHECK(MV.shares_lp == ZXAPP_BOTH && has(text_of(MV.sh.error), "Held: KRON and C") &&
              shown(MV.sh.error),
          "U5: BOTH, said in words: %s", text_of(MV.sh.error));
    memset(&r, 0, sizeof r);
    zui_w(&UI, MV.agents)->u.list.row(&UI, MV.agents, 0, &r, &MV);
    CHECK(has(r.detail, "held") && !has(r.detail, "%"), "rows hold the numbers: %s", r.detail);
    CHECK(!has(text_of(MV.busiest), "Companion"), "no busiest while held");
    CHECK(MV.limits_lp == ZXAPP_BOTH,
          "a KRON answer where Eve's truths belong breaks the contract: %s", BAD.err);
    CHECK(has(BAD.err, "provides a truth"), "the contract says why: %s", BAD.err);
    zxapp_unload(&BAD, &HOST);
    murm_view_logic(&MV, &APP, &HOST);
    CHECK(MV.shares_lp == ZXAPP_TRUE && !has(text_of(MV.sh.error), "Held"),
          "the right logic clears the hold");

    /* AI: why is this agent unhappy, held until read */
    int before = asked;
    murm_view_action(&MV, MV.agents, ZUI_ACT_SELECT, 1);
    murm_view_action(&MV, MV.sh.root, ZUI_ACT_PICK, MURM_AI_WHY_UNHAPPY);
    CHECK(asked == before + 1 && M.held.state == ZXM_HELD_PENDING && shown(MV.sh.held),
          "the answer is held");
    shot("murmuration");
    key(ZUI_KEY_ENTER, ZUI_MOD_CMD);
    CHECK(M.held.state == ZXM_HELD_ACCEPTED, "Cmd-Enter accepts (%d)", M.held.state);

    /* a refresh that fails keeps the snapshot and says so */
    M.link = &DEAD;
    CHECK(click(MV.refresh) && M.refreshes == 1 && text_of(MV.sh.error)[0], "failed refresh");
    CHECK(zui_w(&UI, MV.agents)->u.list.rows == 3, "the snapshot is kept");
    M.link = &LINK;

    /* V3 on a phone: the list, then the agent */
    screen(&PHONE, murm_act, &MV);
    murm_view_build(&MV, &UI, &M);
    murm_view_logic(&MV, &APP, &HOST);
    paint();
    CHECK(shown(MV.agents) && !shown(MV.detail), "compact: the list");
    murm_view_action(&MV, MV.agents, ZUI_ACT_SELECT, 0);
    CHECK(!shown(MV.agents) && shown(MV.detail), "compact: the agent");
    shot("murmuration-phone");
    CHECK(click(MV.back) && shown(MV.agents), "Back");
    CHECK(unhandled == 0, "every action reached the view (%d not)", unhandled);

    screen(&LAPTOP, murm_act, &MV);
    murm_view_build(&MV, &UI, &M);
    murm_view_logic(&MV, &APP, &HOST);
    cross_check();
    zxapp_unload(&APP, &HOST);
}

/* ---------------- An app with no C: the screen from its contracts ---------------- */

static av_view_t AV;
static zxapp_t TALLY;
static bool av_act(void *v, zui_id_t w, zui_action_t a, int32_t arg)
{
    return av_view_action(v, w, a, arg);
}

/* replace what a field holds, as a person would */
static void fill(zui_id_t f, const char *s)
{
    click(f);
    zui_widget_t *w = zui_w(&UI, f);
    zui_field_select(&UI, f, 0, w->u.field.len);
    if (w->u.field.len) key(ZUI_KEY_BACKSPACE, 0);
    type(s);
}

static void test_autoview(void)
{
    int64_t v[8];
    CHECK(av_parse_ints("1 2, 3", v, 8) == 3 && v[2] == 3, "AV2: spaces and commas");
    CHECK(av_parse_ints("-7", v, 8) == 1 && v[0] == -7, "negative");
    CHECK(av_parse_ints("", v, 8) == 0 && av_parse_ints("  ", v, 8) == 0, "empty");
    CHECK(av_parse_ints("1.5", v, 8) == -1 && av_parse_ints("x", v, 8) == -1 &&
              av_parse_ints("1x", v, 8) == -1 && av_parse_ints("-", v, 8) == -1,
          "whole numbers only");
    CHECK(av_parse_ints("1 2 3", v, 2) == -1, "no more than fit");
    CHECK(av_parse_ints("9223372036854775807", v, 1) == 1 && v[0] == INT64_MAX &&
              av_parse_ints("9223372036854775808", v, 1) == -1,
          "64-bit");

    static const char SRC[] = "zxapp 1\nid tally\nname Tally\nauthor Test\nview auto\n"
                              "use swarm_shares 1\n"
                              "logic kron double <<\n2*x\n.\nprovides double num x\n"
                              "logic kron hidden <<\n1\n.\n";
    CHECK(zxapp_parse(&TALLY, SRC, (uint32_t) strlen(SRC)) == ZXAPP_OK &&
              !strcmp(TALLY.view, "auto") && zxapp_compose(&TALLY, &REG) == ZXAPP_OK &&
              zxapp_load(&TALLY, &HOST) == 5,
          "an app with no C: blocks and KRON: %s", TALLY.err);
    screen(&LAPTOP, av_act, &AV);
    av_view_build(&AV, &UI, &TALLY, &HOST);
    check_ground("autoview");
    CHECK(AV.n == 4 && !strcmp(AV.entry[0], "double") &&
              !strcmp(AV.entry[1], "swarm_shares.cycle_share") &&
              !strcmp(AV.entry[3], "swarm_shares.within_limits"),
          "AV1: every entry with a contract, none without (%u)", AV.n);
    CHECK(!shown(AV.run) && has(text_of(AV.name), "Choose"), "nothing chosen: no Run");
    zui_row_t r;
    memset(&r, 0, sizeof r);
    zui_w(&UI, AV.entries)->u.list.row(&UI, AV.entries, 3, &r, &AV);
    CHECK(has(r.detail, "truth"), "a row says what it answers: %s", r.detail);
    av_view_action(&AV, AV.entries, ZUI_ACT_SELECT, 0);
    CHECK(shown(AV.run) && shown(AV.field[0]) && !shown(AV.field[1]) &&
              !strcmp(text_of(AV.label[0]), "x"),
          "one field, named as the contract names it");
    fill(AV.field[0], "1 2 3");
    CHECK(click(AV.run) && !strcmp(AV.answer_text, "2 4 6"), "Run: %s", AV.answer_text);
    CHECK(has(text_of(AV.sh.status), "One language"), "status: %s", text_of(AV.sh.status));
    fill(AV.field[0], "1.5");
    CHECK(click(AV.run) && !AV.answer_text[0] && shown(AV.sh.error) &&
              has(text_of(AV.sh.error), "whole numbers"),
          "AV2 in words: %s", text_of(AV.sh.error));
    fill(AV.field[0], "7");
    key(ZUI_KEY_ENTER, 0);
    CHECK(!strcmp(AV.answer_text, "14") && !shown(AV.sh.error), "Enter runs too, error cleared");

    av_view_action(&AV, AV.entries, ZUI_ACT_SELECT, 3);
    CHECK(shown(AV.field[1]) && !strcmp(text_of(AV.label[0]), "share") &&
              !strcmp(text_of(AV.label[1]), "intensity") && !AV.answer_text[0],
          "a block's entry, two inputs");
    fill(AV.field[0], "100 600");
    fill(AV.field[1], "1 2");
    CHECK(click(AV.run) && !strcmp(AV.answer_text, "TRUE FALSE"), "Eve answers in words: %s",
          AV.answer_text);
    av_view_action(&AV, AV.entries, ZUI_ACT_SELECT, 1);
    fill(AV.field[0], "1 1 1");
    CHECK(click(AV.run) && !strcmp(AV.answer_text, "333 333 333"), "KRON shares: %s",
          AV.answer_text);
    shot("autoview");
    fill(AV.field[0], "");
    CHECK(click(AV.run) && has(text_of(AV.sh.error), "t:"), "an empty input: %s",
          text_of(AV.sh.error));
    CHECK(unhandled == 0, "every action reached the view (%d not)", unhandled);
    zxapp_unload(&TALLY, &HOST);
}

/* ---------------- The Circuit board: apps wired like a circuit ---------------- */

static cb_view_t CV;
static zxboard_t CBOARD;
static zxapp_t FEES;
static zxapp_registry_t CREG;
static bool cb_act(void *v, zui_id_t w, zui_action_t a, int32_t arg)
{
    return cb_view_action(v, w, a, arg);
}

static void ptr(uint8_t type, int32_t x, int32_t y)
{
    zui_event_t e = ev(type, x, y);
    if (!cb_view_pointer(&CV, &e)) zui_ui_event(&UI, &e);
    paint();
}

static void drag(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    now += 1000;
    ptr(ZUI_EV_PTR_DOWN, x0, y0);
    ptr(ZUI_EV_PTR_MOVE, (x0 + x1) / 2, (y0 + y1) / 2);
    ptr(ZUI_EV_PTR_MOVE, x1, y1);
    ptr(ZUI_EV_PTR_UP, x1, y1);
}

static int32_t shelf_row_of(const char *ref)
{
    zxboard_item_t it;
    for (uint32_t i = 0; zxboard_shelf(&CBOARD, i, &it); i++)
        if (!strcmp(it.ref, ref)) return (int32_t) i;
    return -1;
}

static uint32_t px_at(int32_t x, int32_t y)
{
    return PX[y * SURF.w + x] & 0xFFFFFFu;
}

static void test_circuit(void)
{
    static const char SRC[] = "zxblock 1\nid fees\nname Fees\nversion 1.2.0\n"
                              "provides fee num amount rate\n"
                              "logic kron fee <<\n50|_amount*rate%10000\n.\n"
                              "provides fee_ok truth amount fee\n"
                              "logic kron fee_ok <<\n(amount>0)&fee<2501\n.\n"
                              "provides total num amount fee\n"
                              "logic kron total <<\n+/amount+fee\n.\n";
    zxapp_registry_init(&CREG);
    CHECK(zxapp_parse(&FEES, SRC, (uint32_t) strlen(SRC)) == ZXAPP_OK &&
              zxapp_registry_add(&CREG, &BLOCK) == ZXAPP_OK &&
              zxapp_registry_add(&CREG, &FEES) == ZXAPP_OK,
          "a shelf: swarm_shares and fees");
    zxboard_t *b = &CBOARD;
    CHECK(zxboard_init(b, &CREG, "checkout", "Checkout") == ZXAPP_OK &&
              zxboard_input_add(b, "amount") == 0 && zxboard_input_add(b, "rate") == 1,
          "a board with two inputs");
    screen(&LAPTOP, cb_act, &CV);
    cb_view_build(&CV, &UI, b);
    paint();
    check_ground("circuit");
    CHECK(zui_w(&UI, CV.shelf)->u.list.rows == 6, "CB1: the shelf lists all six entries");
    CHECK(has(text_of(CV.sh.status), "NEITHER"), "nothing wired: %s", text_of(CV.sh.status));

    /* CV1: place three parts */
    cb_view_action(&CV, CV.shelf, ZUI_ACT_OPEN, shelf_row_of("fees.fee"));
    cb_view_action(&CV, CV.shelf, ZUI_ACT_OPEN, shelf_row_of("fees.fee_ok"));
    cb_view_action(&CV, CV.shelf, ZUI_ACT_OPEN, shelf_row_of("fees.total"));
    CHECK(b->c.nparts == 3 && CV.node == ZXBOARD_NODE_PART && CV.sel == 2,
          "placed, the newest picked");
    CHECK(b->c.part[0].x != b->c.part[1].x || b->c.part[0].y != b->c.part[1].y,
          "each in its own place");
    paint();
    zui_rect_t r0 = cb_node_rect(&CV, ZXBOARD_NODE_PART, 0), br = zui_w(&UI, CV.board)->r;
    CHECK(r0.w > 0 && r0.x >= br.x && r0.x + r0.w <= br.x + br.w, "a tile inside the board");
    CHECK(px_at(r0.x + r0.w / 2, r0.y + r0.h - 4) == ZUI_PANEL, "a carved tile");

    /* CV2 by pointer: amount -> fee.amount, by dragging between the dots */
    int32_t ax, ay, px, py;
    cb_answer_at(&CV, ZXBOARD_NODE_INPUT, 0, &ax, &ay);
    cb_port_at(&CV, 0, 0, &px, &py);
    drag(ax, ay, px, py);
    CHECK(b->c.part[0].src[0].kind == ZXWIRE_SRC_INPUT && b->c.part[0].src[0].index == 0,
          "CV2: dragged from amount's dot to fee.amount");
    CHECK(CV.node == ZXBOARD_NODE_PART && CV.sel == 0 && CV.port == 0,
          "the input just wired is picked");
    /* CV2 by lists: fee.rate <- rate */
    cb_view_action(&CV, CV.ports, ZUI_ACT_SELECT, 1);
    CHECK(shown(CV.sources) && shown(CV.setting), "picking an input offers its sources");
    cb_view_action(&CV, CV.sources, ZUI_ACT_SELECT, 1);
    CHECK(b->c.part[0].src[1].kind == ZXWIRE_SRC_INPUT && b->c.part[0].src[1].index == 1,
          "fee.rate <- rate, without a pointer");
    /* fee_ok, picked by clicking its tile */
    zui_rect_t r1 = cb_node_rect(&CV, ZXBOARD_NODE_PART, 1);
    now += 1000;
    ptr(ZUI_EV_PTR_DOWN, r1.x + r1.w / 2, r1.y + 8);
    ptr(ZUI_EV_PTR_UP, r1.x + r1.w / 2, r1.y + 8);
    CHECK(CV.node == ZXBOARD_NODE_PART && CV.sel == 1 && has(text_of(CV.part_name), "fee_ok"),
          "a click picks a tile");
    cb_view_action(&CV, CV.ports, ZUI_ACT_SELECT, 0);
    cb_view_action(&CV, CV.sources, ZUI_ACT_SELECT, 0);
    cb_answer_at(&CV, ZXBOARD_NODE_PART, 0, &ax, &ay);
    cb_port_at(&CV, 1, 1, &px, &py);
    drag(ax, ay, px, py);
    CHECK(b->c.part[1].src[1].kind == ZXWIRE_SRC_PART && b->c.part[1].src[1].index == 0,
          "fee_ok.fee <- fee, dragged from fee's answer");
    /* total: amount and a setting */
    cb_view_action(&CV, CV.shelf, ZUI_ACT_SELECT, 0);
    CV.node = ZXBOARD_NODE_PART, CV.sel = 2;
    cb_view_action(&CV, CV.ports, ZUI_ACT_SELECT, 0);
    cb_view_action(&CV, CV.sources, ZUI_ACT_SELECT, 0);
    cb_view_action(&CV, CV.ports, ZUI_ACT_SELECT, 1);
    fill(CV.setting, "1,x");
    key(ZUI_KEY_ENTER, 0);
    CHECK(shown(CV.sh.error) && has(text_of(CV.sh.error), "whole numbers"),
          "a bad setting is refused in words");
    fill(CV.setting, "100");
    key(ZUI_KEY_ENTER, 0);
    CHECK(b->c.part[2].src[1].kind == ZXWIRE_SRC_CONST && b->c.part[2].src[1].konst[0] == 100 &&
              !shown(CV.sh.error),
          "total.fee set to 100");
    CHECK(click(CV.save) && CV.saved_len < 0 && has(text_of(CV.sh.error), "not saved") &&
              has(text_of(CV.sh.error), "answer"),
          "CV4: the open answer is not saved: %s", text_of(CV.sh.error));
    /* the answer: fee_ok, a truth, on an app that answers numbers -> held */
    int32_t ox, oy;
    zui_rect_t ro = cb_node_rect(&CV, ZXBOARD_NODE_OUT, 0);
    ox = ro.x, oy = ro.y + ro.h / 2;
    cb_answer_at(&CV, ZXBOARD_NODE_PART, 1, &ax, &ay);
    drag(ax, ay, ox, oy);
    CHECK(b->c.out.kind == ZXWIRE_SRC_PART && b->c.out.index == 1 && CV.node == ZXBOARD_NODE_OUT,
          "the answer <- fee_ok");
    CHECK(CV.chk.out == ZXWIRE_HELD && has(text_of(CV.sh.status), "BOTH, held") &&
              has(text_of(CV.sh.status), "a truth"),
          "CV3: held, said in words: %s", text_of(CV.sh.status));
    CHECK(shown(CV.kind) && has(text_of(CV.kind), "make it a truth"), "the answer's kind button");
    CHECK(click(CV.kind) && b->c.kind == ZXAPP_KIND_TRUTH && CV.chk.lp == ZXAPP_TRUE &&
              has(text_of(CV.sh.status), "TRUE"),
          "the person makes it a truth: TRUE: %s", text_of(CV.sh.status));
    CHECK(click(CV.save) && CV.saved_len > 0 && !strncmp(CV.cid, "sha256:", 7) &&
              has(text_of(CV.sh.status), "Saved sha256:"),
          "CV4: saved, with its content id: %s", text_of(CV.sh.status));
    CHECK(has(CV.saved, "part fee_ok fees.fee_ok amount=amount fee=fee\n") &&
              has(CV.saved, "fee=100") && has(CV.saved, "use fees 1.2\n"),
          "the manifest:\n%s", CV.saved);
    /* move a tile by dragging its title */
    r0 = cb_node_rect(&CV, ZXBOARD_NODE_PART, 2);
    int32_t was_x = b->c.part[2].x;
    drag(r0.x + 60, r0.y + 10, r0.x + 100, r0.y + 30);
    CHECK(b->c.part[2].x > was_x && !CV.cid[0], "dragging a tile moves it, and it needs saving");
    shot("circuit");
    /* a truth into a number input: held where it is drawn */
    CV.node = ZXBOARD_NODE_PART, CV.sel = 2;
    cb_view_action(&CV, CV.ports, ZUI_ACT_SELECT, 1);
    cb_view_action(&CV, CV.sources, ZUI_ACT_SELECT, 2 + 1);
    CHECK(CV.chk.port[2][1] == ZXWIRE_HELD && has(text_of(CV.sh.status), "total.fee takes whole"),
          "CV3: fee_ok into total.fee is held: %s", text_of(CV.sh.status));
    int32_t hx, hy;
    cb_port_at(&CV, 2, 1, &hx, &hy);
    paint();
    CHECK(px_at(hx, hy) == ZUI_GOLD, "the held dot is gold");
    shot("circuit-held");
    cb_view_action(&CV, CV.sources, ZUI_ACT_SELECT, 2 + 2);
    CHECK(has(text_of(CV.sh.error), "own answer"), "a part cannot take itself: %s",
          text_of(CV.sh.error));
    CHECK(click(CV.remove) && b->c.nparts == 2 && CV.node == -1, "Remove takes the part away");
    CHECK(unhandled == 0, "every action reached the view (%d not)", unhandled);

    /* a phone still draws it */
    screen(&PHONE, cb_act, &CV);
    cb_view_build(&CV, &UI, b);
    paint();
    check_ground("circuit on a phone");
}

/* ---------------- each scheme ---------------- */

/* the screen in dark, light and high contrast, for people (C6) */
static void shot3(const char *name)
{
    if (!SHOTS) return;
    static const char *const NAME[3] = {"dark", "light", "hc"};
    const zui_colors_t *was = UI.colors;
    for (int k = 0; k < 3; k++) {
        char n[96];
        snprintf(n, sizeof n, "%s-%s", name, NAME[k]);
        zui_ui_set_colors(&UI, zui_scheme_colors((zui_scheme_t) k));
        shot(n);
    }
    zui_ui_set_colors(&UI, was);
    paint();
}

/* ---------------- Settings ---------------- */

static zui_settings_t SETS;
static zui_workspace_t WSV;
static zui_wm_t WMV;
static settings_view_t STV;
static char layout_got[32];
static bool settings_act(void *v, zui_id_t w, zui_action_t a, int32_t arg)
{
    return settings_view_action(v, w, a, arg);
}
static void on_layout(void *ctx, const char *id)
{
    (void) ctx;
    snprintf(layout_got, sizeof layout_got, "%s", id);
}

static zui_id_t nth_child(zui_id_t parent, int n)
{
    zui_id_t c = zui_w(&UI, parent)->first;
    while (n-- > 0 && c != ZUI_NONE) c = zui_w(&UI, c)->next;
    return c;
}

static void test_settings(void)
{
    zui_motion_reset();
    zui_set_init(&SETS);
    uint8_t seat = zui_set_seat(&SETS, "seat0"), user = zui_set_user(&SETS, "ana");
    CHECK(zui_ws_init(&WSV, &SETS, (zui_set_view_t){seat, user, 0}) == 0, "a workspace");
    zui_wm_init(&WMV, 1280, 800, false);
    screen(&LAPTOP, settings_act, &STV);
    settings_view_build(&STV, &UI, &WSV, &WMV);
    STV.on_layout = on_layout;
    check_ground("settings");
    CHECK(UI.colors == &STV.colors && WMV.colors == &STV.colors && WMV.use_dock,
          "ST3: the view and the windows paint with the workspace's set");
    CHECK(shown(STV.pane[SET_APPEARANCE]) && !shown(STV.pane[SET_ACCESS]) && !shown(STV.back),
          "ST1: the first section beside the list");
    shot3("settings-appearance");
    zui_ws_theme_t t;
    CHECK(click(nth_child(STV.scheme, 1)), "the Light segment");
    zui_ws_theme(&WSV, &t);
    paint();
    const zui_colors_t *lt = zui_scheme_colors(ZUI_SCHEME_LIGHT);
    CHECK(t.scheme == ZUI_SCHEME_LIGHT && (PX[0] & 0xFFFFFFu) == lt->c[ZUI_C_VOID],
          "ST3: light at once (%06x)", PX[0] & 0xFFFFFFu);
    CHECK(click(nth_child(STV.scheme, 2)), "the High contrast segment");
    zui_ws_theme(&WSV, &t);
    CHECK(t.scheme == ZUI_SCHEME_HIGH_CONTRAST &&
              STV.colors.c[ZUI_C_VOID] == zui_scheme_color(ZUI_SCHEME_HIGH_CONTRAST, ZUI_C_VOID),
          "high contrast");
    CHECK(click(nth_child(STV.scheme, 0)), "back to Dark");
    zui_ws_theme(&WSV, &t);
    CHECK(t.scheme == ZUI_SCHEME_DARK && (PX[0] & 0xFFFFFFu) == ZUI_VOID, "dark again");
    zui_focus(&UI, STV.text_size, true);
    key(ZUI_KEY_RIGHT, 0);
    zui_ws_theme(&WSV, &t);
    CHECK(t.text_scale == 110 && zui_slider_value(&UI, STV.text_size) == 110,
          "the text size slider writes theme.text-scale");
    CHECK(click(nth_child(STV.density, 2)), "Roomy");
    zui_ws_theme(&WSV, &t);
    CHECK(t.density == ZUI_DENSITY_ROOMY, "density");

    settings_view_action(&STV, STV.sections, ZUI_ACT_SELECT, SET_ACCESS);
    paint();
    CHECK(shown(STV.pane[SET_ACCESS]) && !shown(STV.pane[SET_APPEARANCE]), "Accessibility");
    CHECK(click(STV.a11y[0]) && zui_motion_get()->increase_contrast &&
              STV.colors.c[ZUI_C_TEXT] == zui_scheme_color(ZUI_SCHEME_HIGH_CONTRAST, ZUI_C_TEXT),
          "ST3: increase contrast paints the high-contrast set");
    shot("settings-access");
    CHECK(click(STV.a11y[0]) && !zui_motion_get()->increase_contrast &&
              STV.colors.c[ZUI_C_VOID] == ZUI_VOID,
          "and off");
    CHECK(click(STV.a11y[3]) && zui_motion_get()->flash_alert, "flash for alerts");

    settings_view_action(&STV, STV.sections, ZUI_ACT_SELECT, SET_KEYBOARD);
    CHECK(shown(STV.layouts) && zui_w(&UI, STV.layouts)->u.list.rows == (int32_t) STV.nlayout &&
              STV.nlayout == zui_keymap_builtin_count() && zui_w(&UI, STV.layouts)->u.list.sel == 0,
          "the built-in layouts, us in use");
    settings_view_action(&STV, STV.layouts, ZUI_ACT_SELECT, 3);
    char got[32];
    zui_set_str(&SETS, WSV.view, "keyboard.layout", got, sizeof got);
    CHECK(!strcmp(got, zui_keymap_builtin_id(3)) && !strcmp(layout_got, got) &&
              zui_w(&UI, STV.layouts)->u.list.sel == 3,
          "ST3: a layout is saved and handed to the host (%s)", got);
    shot("settings-keyboard");

    settings_view_action(&STV, STV.sections, ZUI_ACT_SELECT, SET_PANEL);
    paint();
    CHECK(zui_w(&UI, STV.edge)->disabled, "the strip's settings rest while the dock is used");
    CHECK(click(STV.dock) && !WMV.use_dock && !zui_set_bool(&SETS, WSV.view, "panel.dock") &&
              !zui_w(&UI, STV.edge)->disabled,
          "the strip instead of the dock");
    zui_set_lock(&SETS, ZUI_LAYER_DEVICE, (zui_set_view_t){0, 0, 0}, "panel.position", true);
    CHECK(click(nth_child(STV.edge, 1)) && shown(STV.sh.error) &&
              zui_w(&UI, STV.edge)->u.tabs.sel == 0,
          "ST2: a locked key is refused and the control goes back");
    zui_set_lock(&SETS, ZUI_LAYER_DEVICE, (zui_set_view_t){0, 0, 0}, "panel.position", false);
    CHECK(click(nth_child(STV.edge, 1)) && !shown(STV.sh.error), "the Left segment");
    zui_rect_t sr = zui_wm_strip_rect(&WMV);
    CHECK(sr.x == 0 && sr.y == 0 && sr.h == 800 && sr.w == ZUI_WM_STRIP,
          "ST3: the strip moves to the left edge (%d %d %d %d)", sr.x, sr.y, sr.w, sr.h);
    zui_focus(&UI, STV.size, true);
    key(ZUI_KEY_RIGHT, 0);
    CHECK(zui_wm_strip_rect(&WMV).w == ZUI_WM_STRIP + 4, "the size slider, a step of 4");
    shot("settings-panel");

    settings_view_action(&STV, STV.sections, ZUI_ACT_SELECT, SET_MOTION);
    paint();
    CHECK(click(STV.reduce) && zui_motion_get()->reduce_motion && UI.reduce_motion,
          "reduce motion, for the view too");
    zui_ws_theme(&WSV, &t);
    CHECK(t.reduced_motion, "and in the workspace");
    zui_focus(&UI, STV.crossfade, true);
    key(ZUI_KEY_HOME, 0);
    CHECK(zui_motion_get()->crossfade_ms == 0, "a cut instead of a crossfade");
    shot3("settings-motion");
    CHECK(unhandled == 0, "every action reached the view (%d not)", unhandled);

    /* V3: a phone */
    screen(&PHONE, settings_act, &STV);
    settings_view_build(&STV, &UI, &WSV, 0);
    paint();
    CHECK(shown(STV.sections) && !shown(STV.pane[SET_APPEARANCE]), "phone: the list first");
    settings_view_action(&STV, STV.sections, ZUI_ACT_SELECT, SET_ACCESS);
    paint();
    CHECK(!shown(STV.sections) && shown(STV.pane[SET_ACCESS]) && shown(STV.back),
          "phone: then the section, with Back");
    shot("settings-phone");
    CHECK(click(STV.back) && shown(STV.sections), "phone: Back");
    zui_motion_reset();
}

/* ---------------- Terminal ---------------- */

static term_view_t TV;
static int shell_calls;
static bool term_act(void *v, zui_id_t w, zui_action_t a, int32_t arg)
{
    return term_view_action(v, w, a, arg);
}
static void shell(void *ctx, term_view_t *t, const char *line)
{
    (void) ctx;
    shell_calls++;
    if (!strcmp(line, "ls")) {
        term_puts(t, "\x1B[1;34mdocs\x1B[0m  \x1B[32mrun.sh\x1B[0m  notes.txt\r\n");
    } else if (!strcmp(line, "status")) {
        term_puts(t, "\x1B[33mwarning\x1B[0m: 2 updates   \x1B[7m ok \x1B[27m\r\n");
    } else {
        term_puts(t, "\x1B[31m");
        term_puts(t, line);
        term_puts(t, ": not found\x1B[0m\r\n");
    }
}

static int32_t find_line(const char *s)
{
    char buf[TERM_COLS * 4 + 1];
    for (uint32_t i = 0; i < term_lines(&TV); i++) {
        term_line_text(&TV, i, buf, sizeof buf);
        if (strstr(buf, s)) return (int32_t) i;
    }
    return -1;
}

static void test_terminal(void)
{
    screen(&LAPTOP, term_act, &TV);
    term_view_build(&TV, &UI, shell, 0);
    check_ground("terminal");
    CHECK(UI.focus == TV.input && term_lines(&TV) == 1, "the input line has the keyboard");
    type("ls");
    key(ZUI_KEY_ENTER, 0);
    CHECK(shell_calls == 1 && TV.in_buf[0] == 0, "T1: Enter hands the line to the shell");
    int32_t l = find_line("$ ls"), o = find_line("docs  run.sh  notes.txt");
    CHECK(l >= 0 && o == l + 1, "T1: the line echoed, the output under it (%d %d)", l, o);
    uint32_t d = term_cell(&TV, (uint32_t) o, 0), r = term_cell(&TV, (uint32_t) o, 6);
    CHECK(TERM_CP(d) == 'd' && term_role(d) == ZUI_C_INFO && (d & TERM_BOLD),
          "T2, T3: bold blue is bold INFO");
    CHECK(TERM_CP(r) == 'r' && term_role(r) == ZUI_C_OK, "T3: green is OK");
    CHECK(term_role(term_cell(&TV, (uint32_t) o, 14)) == ZUI_C_TEXT, "SGR 0 resets");
    CHECK(term_role(term_cell(&TV, (uint32_t) l, 0)) == ZUI_C_MUTED, "the prompt is muted");
    type("status");
    key(ZUI_KEY_ENTER, 0);
    o = find_line("warning: 2 updates");
    CHECK(o >= 0 && term_role(term_cell(&TV, (uint32_t) o, 0)) == ZUI_C_GOLD &&
              (term_cell(&TV, (uint32_t) o, 22) & TERM_INVERSE),
          "T3: yellow is gold; inverse is kept");
    type("frob");
    key(ZUI_KEY_ENTER, 0);
    o = find_line("frob: not found");
    CHECK(o >= 0 && term_role(term_cell(&TV, (uint32_t) o, 0)) == ZUI_C_ALERT, "red is ALERT");
    shot3("terminal");
    /* the parser */
    term_puts(&TV, "abc\rX\n");
    CHECK(find_line("Xbc") >= 0, "T2: CR returns to the column 0");
    term_puts(&TV, "\x1B]0;a title\x07visible\r\n");
    CHECK(find_line("visible") >= 0 && find_line("a title") < 0, "T2: OSC is dropped");
    term_write(&TV, "\x1B[3", 3);
    term_write(&TV, "5mM\xC3", 4);
    term_write(&TV, "\xA9\x1B[0m\r\n", 7);
    o = find_line("M\xC3\xA9");
    CHECK(o >= 0 && term_role(term_cell(&TV, (uint32_t) o, 0)) == ZUI_C_GOLD_HI &&
              TERM_CP(term_cell(&TV, (uint32_t) o, 1)) == 0xE9,
          "T2: sequences and UTF-8 split across writes");
    term_puts(&TV, "1\t2\bZ\r\n");
    o = find_line("1       Z");
    CHECK(o >= 0, "T2: TAB to column 8, BS back");
    term_puts(&TV, "gone\x1B[2K\rkept\r\n");
    CHECK(find_line("gone") < 0 && find_line("kept") >= 0, "T2: erase in line");
    term_puts(&TV, "\x1B[5;10HQ");
    uint32_t top = term_lines(&TV) > TERM_ROWS ? term_lines(&TV) - TERM_ROWS : 0;
    CHECK(TERM_CP(term_cell(&TV, top + 4, 9)) == 'Q', "T2: cursor position on the screen");
    term_puts(&TV, "\x1B[2A\x1B[3DW");
    CHECK(TERM_CP(term_cell(&TV, top + 2, 7)) == 'W', "T2: cursor up and back");
    term_puts(&TV, "\xFF\r\n\x1B[?25lnohide\r\n");
    CHECK(find_line("nohide") >= 0 && find_line("?25") < 0, "T2: private modes dropped");
    char wide[200];
    memset(wide, 'w', 140);
    wide[140] = 0;
    term_puts(&TV, "\r\n");
    term_puts(&TV, wide);
    o = find_line("wwww");
    char buf[TERM_COLS * 4 + 1];
    CHECK(o >= 0 && term_line_text(&TV, (uint32_t) o, buf, sizeof buf) == TERM_COLS &&
              term_line_text(&TV, (uint32_t) o + 1u, buf, sizeof buf) == 8,
          "T2: a long line wraps at %u columns", TERM_COLS);
    for (int i = 0; i < 300; i++) {
        char n[32];
        snprintf(n, sizeof n, "\r\nline %d", i);
        term_puts(&TV, n);
    }
    CHECK(term_lines(&TV) == TERM_LINES && TV.dropped > 0 && find_line("line 299") >= 0 &&
              find_line("line 10 ") < 0,
          "T4: the scrollback keeps the last %u lines", TERM_LINES);
    paint();
    zui_widget_t *sc = zui_w(&UI, TV.scroll);
    CHECK(sc->u.scroll.offset > 0 && sc->u.scroll.offset + sc->r.h >= sc->u.scroll.content_h - 2,
          "T4: the newest line in view");
    CHECK(zui_scroll_track(&UI, TV.scroll).h > 0, "W11: a scroll indicator over the scrollback");
    CHECK(click(TV.clear) && term_lines(&TV) == 1 && find_line("line") < 0, "Clear");
    CHECK(unhandled == 0, "every action reached the view (%d not)", unhandled);
    /* no shell */
    screen(&LAPTOP, term_act, &TV);
    term_view_build(&TV, &UI, 0, 0);
    type("ls");
    key(ZUI_KEY_ENTER, 0);
    CHECK(find_line("No shell is connected") >= 0, "T1: no shell, said so");
}

/* ---------------- Playground ---------------- */

static playground_view_t PV;
static int evals;
static bool pg_act(void *v, zui_id_t w, zui_action_t a, int32_t arg)
{
    return playground_view_action(v, w, a, arg);
}
static int eval(void *ctx, const char *src, uint32_t len, char *out, uint32_t cap,
                uint32_t *out_len)
{
    (void) ctx;
    evals++;
    if (strstr(src, "fail")) {
        int n = snprintf(out, cap, "line 1: boom");
        *out_len = n > 0 ? (uint32_t) n : 0;
        return 3;
    }
    int n = snprintf(out, cap, "%u bytes: %s", len, src);
    *out_len = n > 0 ? (uint32_t) n : 0;
    return 0;
}

static void test_playground(void)
{
    screen(&LAPTOP, pg_act, &PV);
    playground_view_build(&PV, &UI, eval, 0, "Scheme");
    check_ground("playground");
    CHECK(shown(PV.none) && !shown(PV.out) && zui_w(&UI, PV.run)->disabled,
          "P2: the empty state; nothing to run yet");
    shot3("playground-empty");
    CHECK(click(PV.pad), "the pad");
    type("(+ 1 2)");
    CHECK(!zui_w(&UI, PV.run)->disabled, "Run once there is a snippet");
    CHECK(click(PV.run) && evals == 1 && shown(PV.out) && !shown(PV.none),
          "P1: Run hands the pad to the evaluator");
    CHECK(!strcmp(PV.out_buf, "7 bytes: (+ 1 2)") && !strcmp(PV.src, "(+ 1 2)") &&
              has(text_of(PV.sh.status), "Done") && zui_w(&UI, PV.out)->u.field.readonly,
          "P2, P3: the output beside, the pad as it was");
    shot3("playground");
    CHECK(click(PV.pad), "back to the pad");
    key(ZUI_KEY_ENTER, ZUI_MOD_CMD);
    CHECK(evals == 2 && PV.runs == 2, "P1: Cmd-Enter runs it");
    type(" fail");
    CHECK(click(PV.run) && PV.last_rc == 3 && shown(PV.sh.error) &&
              !strcmp(PV.out_buf, "line 1: boom") && zui_w(&UI, PV.use)->disabled,
          "P2: an error, in the error line");
    CHECK(click(PV.pad), "the pad again");
    zui_field_select(&UI, PV.pad, 0, (uint32_t) strlen(PV.src));
    type("ok");
    CHECK(click(PV.run) && PV.last_rc == 0 && !shown(PV.sh.error), "the error clears");
    CHECK(click(PV.use) && !strcmp(PV.src, "2 bytes: ok"), "P3: Use output, on a press only");
    CHECK(click(PV.clear) && PV.src[0] == 0 && zui_w(&UI, PV.run)->disabled, "Clear");
    CHECK(unhandled == 0, "every action reached the view (%d not)", unhandled);
    screen(&LAPTOP, pg_act, &PV);
    playground_view_build(&PV, &UI, 0, 0, "");
    CHECK(has(text_of(PV.sh.status), "No evaluator") && zui_w(&UI, PV.run)->disabled,
          "P1: no evaluator, said so");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: test_views KERNEL_SRC\n");
        return 2;
    }
    char root[256];
    snprintf(root, sizeof root, "%s/zxv-views-XXXXXX",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    if (!mkdtemp(root) || zxf_set_root(root) != 0) {
        printf("[FAIL] test_views: no scratch folder\n");
        return 1;
    }
    SHOTS = getenv("ZXV_VIEWS_SHOTS");
    if (SHOTS) {
        have_font = load_font("ZXV_VIEWS_FONT", &FONT);
        have_mono = have_font && load_font("ZXV_VIEWS_MONO", &MONO);
    }
    zxvd_rpc_set_ops(&OPS);
    zxm_link_init(&LINK, inproc, NULL);
    zxm_link_init(&DEAD, dead, NULL);
    test_burrow();
    test_quillon(root);
    test_glint(root);
    test_spindle();
    test_murmuration(argv[1]);
    test_autoview();
    test_circuit();
    test_settings();
    test_terminal();
    test_playground();
    free(OUT.p);
    nftw(root, rm_one, 16, FTW_DEPTH | FTW_PHYS);
    printf("%s test_views: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails, checks);
    return fails ? 1 : 0;
}
