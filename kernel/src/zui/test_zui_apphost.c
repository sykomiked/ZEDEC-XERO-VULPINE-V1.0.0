/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zui_apphost.c — the desktop's app opener: the launch screen follows
 * the real load, from a recipe through zxvd, and only "done" opens it. */
#include <stdio.h>
#include <string.h>

#include "zui_apphost.h"

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

static zui_wm_t WM;
static zui_apphost_t AH;
static zxm_link_t LINK;

/* ---- a scripted zxvd: start, steps of one block, then done -------------- */
static struct {
    uint32_t total, done, starts, steps, cancels;
    bool refuse_start, fail_at_step, other_target;
    uint32_t fail_step;
} Z;

static int32_t zxvd(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    char r[ZXM_REQ_MAX + 1];
    memcpy(r, req, len);
    r[len] = 0;
    if (strstr(r, "fab.receive.start")) {
        Z.starts++;
        Z.done = 0;
        if (Z.refuse_start)
            return snprintf(resp, cap,
                            "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32085,"
                            "\"message\":\"TRUST: the sender is not on the list\"}}",
                            l->seq);
        return snprintf(resp, cap,
                        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"job\":9,\"cid\":\"bafkapp\","
                        "\"len\":10,\"mode\":\"public\",\"blocks_done\":0,\"blocks_total\":%u}}",
                        l->seq, Z.total);
    }
    if (strstr(r, "fab.receive.step")) {
        Z.steps++;
        if (Z.fail_at_step && Z.steps == Z.fail_step)
            return snprintf(resp, cap,
                            "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32085,"
                            "\"message\":\"BLOCK: a block is missing\"}}",
                            l->seq);
        if (Z.done < Z.total) {
            Z.done++;
            return snprintf(resp, cap,
                            "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"job\":9,\"phase\":"
                            "\"fetch\",\"blocks_done\":%u,\"blocks_total\":%u}}",
                            l->seq, Z.done, Z.total);
        }
        return snprintf(resp, cap,
                        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"job\":9,\"phase\":\"done\","
                        "\"blocks_done\":%u,\"blocks_total\":%u,\"cid\":\"%s\",\"len\":10}}",
                        l->seq, Z.total, Z.total, Z.other_target ? "bafkother" : "bafkapp");
    }
    if (strstr(r, "fab.receive.cancel")) {
        Z.cancels++;
        return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"ok\":true}}",
                        l->seq);
    }
    return -1;
}

static void reset_zxvd(uint32_t total)
{
    memset(&Z, 0, sizeof Z);
    Z.total = total;
}

/* ---- the shell: counts the sounds ---------------------------------------- */
static uint32_t begins, ticks, ends;

static void sh_play(void *ctx, uint32_t window, const zxl_theme_t *theme, const zxl_event_t *ev)
{
    (void) ctx, (void) window, (void) theme;
    if (ev->kind == ZXL_EV_BEGIN) begins++;
    if (ev->kind == ZXL_EV_TICK) ticks++;
    if (ev->kind == ZXL_EV_END) ends++;
}

/* ---- what the desktop is told -------------------------------------------- */
static struct {
    uint32_t n, window;
    bool ok;
    char out[64], why[ZXM_ERR_MAX];
} D;

static void on_done(void *ctx, uint32_t window, bool ok, const char *out, const char *why)
{
    (void) ctx;
    D.n++;
    D.window = window;
    D.ok = ok;
    snprintf(D.out, sizeof D.out, "%s", out ? out : "");
    snprintf(D.why, sizeof D.why, "%s", why ? why : "");
}

/* a hook that was there before the opener: still told */
static uint32_t prev_open, prev_close;
static void prev_on_window(zui_wm_t *wm, uint32_t id, bool open, void *ctx)
{
    (void) wm, (void) id, (void) ctx;
    if (open)
        prev_open++;
    else
        prev_close++;
}

static void settle(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 16) zui_wm_tick(&WM, 16);
}

static uint32_t progress(uint32_t id)
{
    zui_wm_window_t *w = zui_wm_get(&WM, id);
    return w ? w->progress : 9999u;
}

static void setup(void)
{
    zui_wm_init(&WM, 1280, 800, false);
    WM.on_window = prev_on_window;
    zxm_link_init(&LINK, zxvd, &LINK);
    zui_lt_shell_t sh = {0, 0, sh_play, 0};
    zui_ah_init(&AH, &WM, &sh, 0, &LINK, on_done, 0);
    memset(&D, 0, sizeof D);
    begins = ticks = ends = 0;
}

static void test_open(void)
{
    setup();
    reset_zxvd(4);
    uint32_t id =
        zui_ah_open(&AH, "glint", "Glint", 0, true, 0, "glint.fab", "/apps/glint", 0, 0, false, 1);
    CHECK(id && Z.starts == 1 && zui_ah_loading(&AH, id) && progress(id) == 0,
          "AH2: the window opens on its launch screen at 0 and the receive starts");
    CHECK(prev_open == 1, "AH1: the hook that was there is still told");
    CHECK(zui_launch_theme_of(zui_ah_theme(&AH), id) != 0, "AH1: the window has its app's theme");
    settle(400);
    CHECK(begins == 1 && ends == 0, "the begin sound plays as the window opens, no end");

    /* AH3: one step per frame; the bar follows the verified blocks only */
    uint32_t last = 0, frames = 0;
    bool rising = true, early = false;
    while (zui_ah_pump(&AH) && frames < 32) {
        frames++;
        uint32_t p = progress(id);
        if (p < last) rising = false;
        if (p >= 256u) early = true;
        last = p;
        settle(32);
    }
    CHECK(frames == 4 && Z.steps == 5, "AH3: one step a frame, four blocks then done (%u, %u)",
          frames, Z.steps);
    CHECK(rising && !early, "AH3: the bar rises with the blocks and never reaches the end early");
    CHECK(last > 0, "AH3: the bar moved with the blocks (%u)", last);
    CHECK(progress(id) == 256u && !zui_ah_loading(&AH, id),
          "AH4: done, the bar is full and nothing is loading");
    CHECK(D.n == 1 && D.window == id && D.ok && !strcmp(D.out, "/apps/glint") && !D.why[0],
          "AH4: the desktop is told the app is ready, and where");
    settle(2000);
    CHECK(ends == 1 && zui_wm_get(&WM, id), "AH4: the end sound plays once and the window stays");
    CHECK(zui_ah_pump(&AH) == 0 && D.n == 1, "AH3: nothing more to pump, nothing told twice");
    CHECK(Z.cancels == 0, "no cancel for an open that finished");
}

static void test_fail(void)
{
    /* AH5: zxvd's error mid-load: told in its words, window closed, no end */
    setup();
    reset_zxvd(4);
    Z.fail_at_step = true;
    Z.fail_step = 3;
    uint32_t id =
        zui_ah_open(&AH, "glint", "Glint", 0, true, 0, "glint.fab", "/apps/glint", 0, 0, false, 1);
    settle(400);
    uint32_t frames = 0;
    while (zui_ah_pump(&AH) && frames < 32) frames++;
    CHECK(D.n == 1 && D.window == id && !D.ok && strstr(D.why, "BLOCK: a block is missing") &&
              !D.out[0],
          "AH5: told why, in zxvd's words (%s)", D.why);
    CHECK(!zui_wm_get(&WM, id) && !zui_ah_loading(&AH, id), "AH5: the window is closed");
    settle(2000);
    CHECK(ends == 0, "AH5: a failed load never plays its end");

    /* AH5: the built app is not the one the recipe named */
    setup();
    reset_zxvd(2);
    Z.other_target = true;
    id = zui_ah_open(&AH, "glint", "Glint", 0, true, 0, "glint.fab", "/apps/glint", 0, 0, false, 1);
    frames = 0;
    while (zui_ah_pump(&AH) && frames < 32) frames++;
    settle(2000);
    CHECK(D.n == 1 && !D.ok && !zui_wm_get(&WM, id) && ends == 0,
          "AH5: another target is a failure, never run (%s)", D.why);

    /* AH5: a start zxvd refuses */
    setup();
    reset_zxvd(2);
    Z.refuse_start = true;
    id = zui_ah_open(&AH, "glint", "Glint", 0, true, 0, "glint.fab", "/apps/glint", 0, 0, false, 1);
    CHECK(id && D.n == 1 && D.window == id && !D.ok && strstr(D.why, "TRUST") &&
              !zui_wm_get(&WM, id) && !zui_ah_loading(&AH, id),
          "AH5: a refused start: told, window closed (%s)", D.why);
    CHECK(zui_ah_pump(&AH) == 0 && D.n == 1, "AH5: nothing left to pump");
}

static void test_close_and_args(void)
{
    /* AH6: closing a window that is loading cancels the receive */
    setup();
    reset_zxvd(8);
    uint32_t id =
        zui_ah_open(&AH, "glint", "Glint", 0, true, 0, "glint.fab", "/apps/glint", 0, 0, false, 1);
    zui_ah_pump(&AH);
    zui_ah_pump(&AH);
    uint32_t pc = prev_close;
    CHECK(zui_wm_close(&WM, id), "AH6: the person closes the window");
    CHECK(Z.cancels == 1 && D.n == 1 && !D.ok && strstr(D.why, "closed") &&
              !zui_ah_loading(&AH, id) && prev_close == pc + 1,
          "AH6: the receive is cancelled, the desktop told, the old hook still told");
    CHECK(zui_ah_pump(&AH) == 0 && Z.steps == 2, "AH6: nothing more is asked of zxvd");

    /* two apps at once, each its own screen */
    setup();
    reset_zxvd(3);
    uint32_t a =
        zui_ah_open(&AH, "glint", "Glint", 0, true, 0, "glint.fab", "/apps/glint", 0, 0, false, 1);
    uint32_t b = zui_ah_open(&AH, "spindle", "Spindle", 0, false, 0, "spindle.fab", "/apps/spindle",
                             0, 0, false, 1);
    CHECK(a && b && a != b && zui_ah_pump(&AH) == 2, "AH3: two opens, each stepped");

    /* bad arguments: nothing opens, nothing is told */
    setup();
    reset_zxvd(2);
    uint32_t before = Z.starts;
    CHECK(!zui_ah_open(&AH, "glint", "Glint", 0, true, 0, "", "/o", 0, 0, false, 1) &&
              !zui_ah_open(&AH, "glint", "Glint", 0, true, 0, "r.fab", 0, 0, 0, false, 1) &&
              !zui_ah_open(&AH, "glint", "Glint", 0, true, 0, "r.fab", "/o", 0, 1, false, 1) &&
              !zui_ah_open(0, "glint", "Glint", 0, true, 0, "r.fab", "/o", 0, 0, false, 1),
          "bad arguments open nothing");
    CHECK(Z.starts == before && D.n == 0 && zui_ah_pump(&AH) == 0 && !zui_wm_front(&WM),
          "and ask nothing, tell nothing, leave no window");

    /* no link: only warm opens */
    zui_lt_shell_t sh = {0, 0, sh_play, 0};
    zui_wm_init(&WM, 1280, 800, false);
    zui_ah_init(&AH, &WM, &sh, 0, 0, on_done, 0);
    CHECK(!zui_ah_open(&AH, "glint", "Glint", 0, true, 0, "r.fab", "/o", 0, 0, false, 1),
          "no link, no open from a recipe");

    /* AH7: warm */
    begins = ends = 0;
    uint32_t w = zui_ah_open_warm(&AH, "glint", "Glint", 0, true, 0);
    CHECK(w && progress(w) == 256u && !zui_ah_loading(&AH, w), "AH7: warm is loaded at once");
    settle(2000);
    CHECK(begins == 0 && ends == 1, "AH7: a warm open plays only its end");
}

int main(void)
{
    printf("=== zui_apphost: the launch screen follows the real load ===\n");
    test_open();
    test_fail();
    test_close_and_args();
    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
