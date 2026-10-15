/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Hollow's model against zxvd's real dispatcher (LINK): the desk is a file
 * in the Hoard (files.read, files.write), the wallpaper an image there;
 * spaces.list and spaces.get on zxvd's spaces; screen.tick on screen time
 * in memory; notes.list and notes.mark_read through host ops that hold the
 * notes below; ui.get through people ops, where they are set. */
#include "hollow_model.h"

static const char *HW_NOTES = "{\"unread\":0,\"notes\":[]}";
static uint32_t hw_marked;
static int64_t hw_now = 1700000000;

static void hw_notes(zxvd_buf_t *o)
{
    zxvd_buf_raw(o, HW_NOTES, strlen(HW_NOTES));
}

static uint32_t hw_read(void)
{
    return hw_marked;
}

static int64_t hw_clock(void)
{
    return hw_now;
}

static int hw_people(const char *method, const char *params, size_t len, zxvd_buf_t *out, char *msg,
                     size_t msg_cap)
{
    (void) params;
    (void) len;
    if (strcmp(method, "ui.get")) {
        snprintf(msg, msg_cap, "not here");
        return -32601;
    }
    zxvd_buf_raw(out, "{\"simple_mode\":true}", 20);
    return 0;
}

static bool hw_desk_is(const char *want)
{
    static uint8_t buf[HOLLOW_TEXT_MAX];
    uint32_t got = 0;
    uint64_t size = 0;
    if (zxf_read(HOLLOW_DESK, 0, buf, sizeof buf - 1u, true, &got, &size) != 0) return false;
    buf[got] = 0;
    return size == got && !strcmp((const char *) buf, want);
}

static bool hw_put(const char *path, const void *d, uint32_t n)
{
    uint64_t size;
    return zxf_write(path, (const uint8_t *) d, n, ZXF_W_REPLACE, false, &size) == 0;
}

#define HWN(seq, pri, read, title)                                                                 \
    "{\"seq\":" #seq ",\"kind\":\"info\",\"pri\":" #pri ",\"read\":" #read                         \
    ",\"count\":1,\"source\":\"burrow\",\"title\":\"" title "\",\"body\":\"\",\"action\":\"\"}"

APPS_TEST(hollow)
{
    static hollow_t H1, H2;
    static zxvd_host_ops_t ops;
    static const zxvd_people_ops_t people = {hw_people};
    hollow_grid_t g;

    /* the rules (HR1-HR5), as the model puts them together */
    CHECK(hollow_grid(4, 1920, 1080, &g) == ZXM_OK && g.cols == 2 && g.rows == 2 &&
              g.cell_w == 960 && g.cell_h == 540,
          "grid: four windows on 1920 by 1080");
    CHECK(hollow_grid(5, 1920, 1080, &g) == ZXM_OK && g.cols == 3 && g.rows == 2 &&
              g.cell_w == 640 && g.cell_h == 540,
          "grid: five windows, a row of three and a row of two");
    CHECK(hollow_grid(64, HOLLOW_PX_MAX, 1, &g) == ZXM_OK && g.cols == 8 && g.rows == 8 &&
              g.cell_w == 2048 && g.cell_h == 0,
          "grid: the most windows, a side of one pixel");
    CHECK(hollow_grid(0, 1920, 1080, &g) == ZXM_OK && !g.cols && !g.rows && !g.cell_w && !g.cell_h,
          "grid: none");
    CHECK(hollow_grid(65, 1920, 1080, &g) == ZXM_E_ARG &&
              hollow_grid(-1, 1920, 1080, &g) == ZXM_E_ARG &&
              hollow_grid(4, 0, 1080, &g) == ZXM_E_ARG &&
              hollow_grid(4, 1920, HOLLOW_PX_MAX + 1, &g) == ZXM_E_ARG,
          "grid: refused outside its rules");
    CHECK(hollow_window_ok(0, 0, 800, 600, 1920, 1080) &&
              hollow_window_ok(-768, 0, 800, 600, 1920, 1080) &&
              !hollow_window_ok(-769, 0, 800, 600, 1920, 1080) &&
              hollow_window_ok(1888, 1048, 800, 600, 1920, 1080) &&
              !hollow_window_ok(1889, 0, 800, 600, 1920, 1080) &&
              !hollow_window_ok(0, 1049, 800, 600, 1920, 1080) &&
              !hollow_window_ok(0, 0, 159, 600, 1920, 1080) &&
              !hollow_window_ok(0, 0, 800, 99, 1920, 1080) &&
              hollow_window_ok(0, 0, 160, 100, 1920, 1080) &&
              !hollow_window_ok(0, 0, 800, 600, 0, 1080),
          "window limits: 32 pixels on screen, 160 by 100 at least");
    static const uint8_t png[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 13, 'I'};
    CHECK(hollow_image_kind(png, sizeof png) == HOLLOW_IMG_PNG &&
              hollow_image_kind(png, 7) == HOLLOW_IMG_NONE &&
              hollow_image_kind((const uint8_t *) "\xff\xd8\xff\xe0", 4) == HOLLOW_IMG_JPEG &&
              hollow_image_kind((const uint8_t *) "GIF89a", 6) == HOLLOW_IMG_GIF &&
              hollow_image_kind((const uint8_t *) "BM", 2) == HOLLOW_IMG_BMP &&
              hollow_image_kind((const uint8_t *) "RIFF\0\0\0\0WEBP", 12) == HOLLOW_IMG_WEBP &&
              hollow_image_kind((const uint8_t *) "hello", 5) == HOLLOW_IMG_NONE &&
              hollow_image_kind(NULL, 4) == HOLLOW_IMG_NONE,
          "image kinds by their first bytes");

    /* nothing is written before the desk is read */
    CHECK(zxf_use_space(NULL) == 0, "home");
    hollow_init(&H1, &LINK);
    int before = sent;
    CHECK(hollow_pin(&H1, 5, hw_now) == ZXM_E_STATE && hollow_unpin(&H1, 0) == ZXM_E_STATE &&
              hollow_place(&H1, 5, 0, 0, 800, 600, 1920, 1080) == ZXM_E_STATE && sent == before,
          "not loaded: nothing written");
    CHECK(hollow_pin(&H1, 0, hw_now) == ZXM_E_ARG && hollow_pin(&H1, 145, hw_now) == ZXM_E_ARG &&
              hollow_pin(&H1, 5, -1) == ZXM_E_ARG &&
              hollow_pin(&H1, 5, 1000000000000) == ZXM_E_ARG &&
              hollow_place(&H1, 5, 1900, 0, 800, 600, 1920, 1080) == ZXM_E_ARG && sent == before,
          "bad pins and places are refused before anything is asked");

    /* a desk not made yet is empty */
    CHECK(hollow_load(&H1) == ZXM_OK && H1.loaded && !H1.ndock && !H1.nwin && !H1.wallpaper[0],
          "no desk yet: %s", LINK.err);

    /* the dock (HD3) */
    CHECK(hollow_pin(&H1, 37, hw_now) == ZXM_OK && hollow_pin(&H1, 1, hw_now + 1) == ZXM_OK &&
              hollow_pin(&H1, 144, hw_now + 2) == ZXM_OK && H1.ndock == 3,
          "three pins: %s", LINK.err);
    CHECK(hollow_pin(&H1, 37, hw_now) == ZXM_E_STATE && H1.ndock == 3, "an app is pinned once");
    CHECK(hw_desk_is("hollow 1\npin 37 1700000000\npin 1 1700000001\npin 144 1700000002\n"),
          "the desk in the Hoard (HD1, HD3)");
    CHECK(hollow_move(&H1, 2, 0) == ZXM_OK && H1.dock[0].app == 144 && H1.dock[1].app == 37 &&
              H1.dock[2].app == 1,
          "the last pin first");
    CHECK(hollow_move(&H1, 0, 2) == ZXM_OK && H1.dock[0].app == 37 && H1.dock[2].app == 144 &&
              hollow_move(&H1, 1, 1) == ZXM_OK && hollow_move(&H1, 0, 3) == ZXM_E_ARG,
          "and back");
    CHECK(hollow_unpin(&H1, 1) == ZXM_OK && H1.ndock == 2 && H1.dock[1].app == 144 &&
              hollow_unpin(&H1, 2) == ZXM_E_ARG,
          "unpin the middle one");

    /* window places (HD4), and tiling */
    CHECK(hollow_place(&H1, 37, -100, 40, 800, 600, 1920, 1080) == ZXM_OK && H1.nwin == 1 &&
              hollow_place(&H1, 37, 100, 40, 800, 600, 1920, 1080) == ZXM_OK && H1.nwin == 1 &&
              H1.win[0].x == 100,
          "a window placed, then moved: %s", LINK.err);
    CHECK(hollow_place(&H1, 1, 0, 1049, 800, 600, 1920, 1080) == ZXM_E_ARG && H1.nwin == 1,
          "31 pixels on screen is too few");
    static const int32_t four[4] = {1, 37, 89, 144}, twice[2] = {1, 1};
    CHECK(hollow_tile(&H1, four, 4, 1920, 1080) == ZXM_OK && H1.nwin == 4 && H1.win[0].app == 37 &&
              H1.win[0].x == 960 && H1.win[0].y == 0 && H1.win[0].width == 960 &&
              H1.win[0].height == 540 && H1.win[1].app == 1 && H1.win[1].x == 0 &&
              H1.win[1].y == 0 && H1.win[2].app == 89 && H1.win[2].x == 0 && H1.win[2].y == 540 &&
              H1.win[3].x == 960 && H1.win[3].y == 540,
          "four windows tiled row by row in a 2 by 2 grid: %s", LINK.err);
    static int32_t many[64];
    for (uint32_t i = 0; i < 62; i++) many[i] = (int32_t) i + 1;
    many[62] = 89, many[63] = 144; /* every app already placed, so 64 places in all */
    before = sent;
    CHECK(hollow_tile(&H1, many, 64, 800, 600) == ZXM_E_ARG &&
              hollow_tile(&H1, twice, 2, 1920, 1080) == ZXM_E_ARG && H1.nwin == 4 && sent == before,
          "cells under the limits, or one app twice: refused, nothing asked");
    CHECK(hollow_tile(&H1, many, 64, 3840, 2160) == ZXM_OK && H1.nwin == 64 &&
              H1.win[3].app == 144 && H1.win[3].x == 7 * 480 && H1.win[3].y == 7 * 270 &&
              H1.win[63].app == 62 && H1.win[63].x == 5 * 480 && H1.win[63].y == 7 * 270,
          "the most windows on a large screen: %s", LINK.err);
    CHECK(hollow_tile(&H1, NULL, 0, 1920, 1080) == ZXM_OK && H1.nwin == 64,
          "tiling nothing changes nothing");

    /* the wallpaper */
    CHECK(hw_put("wall.png", png, sizeof png) && hw_put("notes.txt", "hello", 5), "two files");
    CHECK(hollow_set_wallpaper(&H1, "notes.txt") == ZXM_E_ARG && !H1.wallpaper[0],
          "a file that is not an image");
    CHECK(hollow_set_wallpaper(&H1, "none.png") == ZXM_E_REMOTE &&
              LINK.err_code == HOLLOW_E_FILES && !H1.wallpaper[0],
          "a file that is not there");
    CHECK(hollow_set_wallpaper(&H1, "a\nb") == ZXM_E_ARG, "a path with a control byte");
    CHECK(hollow_set_wallpaper(&H1, "wall.png") == ZXM_OK && !strcmp(H1.wallpaper, "wall.png") &&
              H1.wallpaper_kind == HOLLOW_IMG_PNG,
          "the wallpaper: %s", LINK.err);
    uint8_t img[64];
    uint32_t got = 0;
    CHECK(hollow_wallpaper(&H1, img, sizeof img, &got) == ZXM_OK && got == sizeof png &&
              !memcmp(img, png, sizeof png),
          "its bytes read back: %s", LINK.err);
    CHECK(hollow_wallpaper(&H1, img, 4, &got) == ZXM_E_TOOBIG && !got, "a buffer too small");

    /* a second model reads it all */
    hollow_init(&H2, &LINK);
    CHECK(hollow_load(&H2) == ZXM_OK && H2.ndock == H1.ndock &&
              !memcmp(H2.dock, H1.dock, H1.ndock * sizeof H1.dock[0]) && H2.nwin == H1.nwin &&
              !memcmp(H2.win, H1.win, H1.nwin * sizeof H1.win[0]) &&
              !strcmp(H2.wallpaper, "wall.png"),
          "read back: %s", LINK.err);
    CHECK(hollow_set_wallpaper(&H1, "") == ZXM_OK && !H1.wallpaper[0] &&
              hollow_wallpaper(&H1, img, sizeof img, &got) == ZXM_E_STATE,
          "the wallpaper cleared");

    /* a file that is not a desk is never overwritten (HD5) */
    CHECK(hw_put(HOLLOW_DESK, "hollow 1\npin 07 1\n", 18), "a leading zero");
    CHECK(hollow_load(&H2) == ZXM_E_PROTOCOL && !H2.loaded &&
              hollow_pin(&H2, 3, 1) == ZXM_E_STATE && hw_desk_is("hollow 1\npin 07 1\n"),
          "not a desk: refused, left as it was");
    static const char *const bad[] = {
        "desk\n",
        "hollow 1\npin 1 1",
        "hollow 1\npin 0 1\n",
        "hollow 1\npin 145 1\n",
        "hollow 1\npin 1 1\npin 1 2\n",
        "hollow 1\npin 1 -1\n",
        "hollow 1\nwin 1 0 0 800 600\npin 2 1\n",
        "hollow 1\npin 1 1\nwallpaper a.png\n",
        "hollow 1\nwallpaper \n",
        "hollow 1\nwin 1 0 0 159 600\n",
        "hollow 1\nwin 1 -0 0 800 600\n",
        "hollow 1\nwin 1 -769 0 800 600\n",
        "hollow 1\nwin 1 0 0 800 600\nwin 1 0 0 800 600\n",
        "hollow 1\npin 1 1000000000000\n",
    };
    for (uint32_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK(hw_put(HOLLOW_DESK, bad[i], (uint32_t) strlen(bad[i])) &&
                  hollow_load(&H2) == ZXM_E_PROTOCOL,
              "not a desk: %u", i);
    static const char good[] =
        "hollow 1\nwallpaper Pics/sea.png\npin 2 0\nwin 2 -768 16352 800 600\n";
    CHECK(hw_put(HOLLOW_DESK, good, sizeof good - 1u) && hollow_load(&H2) == ZXM_OK && H2.loaded &&
              H2.ndock == 1 && H2.nwin == 1 && H2.win[0].x == -768 &&
              !strcmp(H2.wallpaper, "Pics/sea.png"),
          "a desk at its edges");

    /* spaces, notices and simple mode */
    ops = OPS;
    ops.notes = hw_notes;
    ops.notes_read = hw_read;
    zxvd_rpc_set_ops(&ops);
    HW_NOTES = "{\"unread\":2,\"notes\":[" HWN(7, 1, false, "Saved") "," HWN(
        6, 3, false, "Low") "," HWN(5, 0, true, "Old") "]}";
    zxm_begin(&LINK, "spaces.create");
    zxm_p_str(&LINK, "kind", "office");
    zxm_p_str(&LINK, "name", "Hollow Desk");
    char id[ZXM_SPACE_MAX + 1] = "";
    CHECK(zxm_call(&LINK) == ZXM_OK &&
              zxm_get_str(&LINK, LINK.result, "id", id, sizeof id) == ZXM_OK && id[0],
          "a space to be in: %s", LINK.err);
    zxvd_rpc_set_people_ops(NULL);
    CHECK(hollow_refresh(&H1) == ZXM_OK && H1.nspaces >= 2 && !strcmp(H1.space[0].id, "home") &&
              H1.unread == 2 && H1.nnotes == 3 && H1.note[0].seq == 7 && H1.note[1].pri == 3 &&
              H1.note[2].read && !strcmp(H1.note[0].title, "Saved") && !H1.simple &&
              !H1.simple_known,
          "refresh, ui.get not served here: %s", LINK.err);
    bool listed = false;
    for (uint32_t i = 0; i < H1.nspaces; i++)
        listed =
            listed || (!strcmp(H1.space[i].id, id) && !strcmp(H1.space[i].name, "Hollow Desk") &&
                       !strcmp(H1.space[i].kind, "office") && H1.space[i].write);
    CHECK(listed, "the new space is listed");
    zxvd_rpc_set_people_ops(&people);
    CHECK(hollow_refresh(&H1) == ZXM_OK && H1.simple && H1.simple_known, "simple mode on: %s",
          LINK.err);
    HW_NOTES = "{\"unread\":1,\"notes\":{}}";
    CHECK(hollow_refresh(&H1) == ZXM_E_PROTOCOL && H1.unread == 2 && H1.nnotes == 3 && H1.simple,
          "a reply out of bounds changes nothing");
    zxvd_rpc_set_people_ops(NULL);
    CHECK(hollow_enter(&H1, id) == ZXM_OK && !strcmp(H1.here.id, id) &&
              !strcmp(H1.here.name, "Hollow Desk"),
          "into the space: %s", LINK.err);
    CHECK(hollow_enter(&H1, "nowhere") == ZXM_E_REMOTE && !strcmp(H1.here.id, id) &&
              hollow_enter(&H1, "") == ZXM_E_ARG,
          "a missing space leaves the desk where it was");
    CHECK(hollow_enter(&H1, "home") == ZXM_OK && !strcmp(H1.here.kind, "home"), "home again");
    hw_marked = 2;
    uint32_t marked = 0;
    CHECK(hollow_mark_read(&H1, &marked) == ZXM_OK && marked == 2 && !H1.unread &&
              H1.note[0].read && H1.note[1].read,
          "every notice read: %s", LINK.err);

    /* the app in front, once a minute */
    CHECK(zsc_open(NULL) == 0, "screen time in memory");
    zsc_set_clock(hw_clock);
    CHECK(hollow_tick(&H1, "quillon") == ZXM_OK && !strcmp(H1.front.app, "quillon") &&
              H1.front.allowed && H1.front.counted && H1.front.used == 1 && H1.front.left == -1,
          "a minute counted: %s", LINK.err);
    hw_now += 30;
    CHECK(hollow_tick(&H1, "quillon") == ZXM_OK && !H1.front.counted && H1.front.used == 1,
          "not twice within 45 seconds");
    hw_now += 60;
    CHECK(hollow_tick(&H1, "quillon") == ZXM_OK && H1.front.counted && H1.front.used == 2,
          "the next minute");
    before = sent;
    CHECK(hollow_tick(&H1, "") == ZXM_E_ARG && hollow_tick(&H1, "two words") == ZXM_E_ARG &&
              hollow_tick(&H1, "abcdefghijklmnopqrstuvwxyz0123456") == ZXM_E_ARG &&
              sent == before && H1.front.used == 2,
          "names screen.tick does not take are not sent");
    zsc_set_clock(NULL);
    zsc_close();
    zxvd_rpc_set_ops(&OPS);
}
