/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Warble's model against zxvd's real dispatcher (LINK): a Music folder
 * written with files.write into the test's scratch Hoard, listed, a WAV's
 * length read from its header, one track looked at again with files.stat,
 * tracks found with search.query, then a playlist, its length, a shuffle
 * and the volume under the owner's cap. hall.* is served in the daemon by
 * zxvd_pspace.c, which this test does not link (the dispatcher answers
 * -32601), so the hall's answers are made up in hall.status's documented
 * shape (FAKE). */
#include "warble_model.h"

/* a file written straight through zxvd, as another app would */
static bool wb_write(const char *path, const char *key, const char *val)
{
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", path);
    zxm_p_str(&LINK, key, val);
    return zxm_call(&LINK) == ZXM_OK;
}

static bool wb_mkdir(const char *path)
{
    zxm_begin(&LINK, "files.mkdir");
    zxm_p_str(&LINK, "path", path);
    return zxm_call(&LINK) == ZXM_OK;
}

/* 2 s of 8 kHz mono 16-bit, 3 min of 44.1 kHz stereo, a header that is not RIFF */
#define WB_TWO_S   "UklGRiR9AABXQVZFZm10IBAAAAABAAEAQB8AAIA+AAACABAAZGF0YQB9AAA="
#define WB_THREE_M "UklGRmR/5AFXQVZFZm10IBAAAAABAAIARKwAABCxAgAEABAAZGF0YUB/5AE="
#define WB_RIFX    "UklGWCR9AABXQVZFZm10IBAAAAABAAEAQB8AAIA+AAACABAAZGF0YQB9AAA="
#define WB_HALL    "bafkreihallforwarble"
#define WB_MEDIA   "bafkreimorningsong"
#define WB_OK      "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":"

static warble_t WB;

APPS_TEST(warble)
{
    char text[24];
    int64_t ms = 0;
    uint32_t unknown = 0;

    /* WM1: no Music folder yet is an empty library; nothing else applies */
    warble_init(&WB, &LINK);
    CHECK(WB.volume == WARBLE_VOL_START && WB.cap == WARBLE_CAP_START && !WB.loaded &&
              warble_queue(&WB, 0) == ZXM_E_STATE && warble_search(&WB, "song") == ZXM_E_STATE &&
              warble_restat(&WB, 0) == ZXM_E_STATE,
          "warble: before the load, nothing applies");
    CHECK(warble_load(&WB) == ZXM_OK && WB.loaded && WB.n == 0 && WB.skipped == 0,
          "warble: no Music folder: an empty library: %s", LINK.err);

    /* WM1, WM2: four tracks, a note and a folder */
    CHECK(wb_mkdir("Music") && wb_mkdir("Music/Albums") &&
              wb_write("Music/Morning song.wav", "base64", WB_TWO_S) &&
              wb_write("Music/Long walk.WAV", "base64", WB_THREE_M) &&
              wb_write("Music/Odd.wav", "base64", WB_RIFX) &&
              wb_write("Music/Tide.mp3", "text", "ID3 not decoded") &&
              wb_write("Music/lyrics.txt", "text", "la la la"),
          "warble: the Music folder written: %s", LINK.err);
    int before = sent;
    CHECK(warble_load(&WB) == ZXM_OK && WB.n == 4 && WB.skipped == 2 && sent == before + 4,
          "warble: four tracks, a folder and a note skipped, three headers read: %s (%u, %u)",
          LINK.err, WB.n, WB.skipped);
    int32_t morning = warble_find(&WB, "Morning song.wav"),
            walk = warble_find(&WB, "Long walk.WAV"), odd = warble_find(&WB, "Odd.wav"),
            tide = warble_find(&WB, "Tide.mp3");
    CHECK(morning >= 0 && walk >= 0 && odd >= 0 && tide >= 0 && warble_find(&WB, "lyrics.txt") < 0,
          "warble: every track found by its name");
    CHECK(WB.track[morning].ms == 2000 && WB.track[walk].ms == 180000 && WB.track[odd].ms == 0 &&
              WB.track[tide].ms == 0 && WB.track[morning].bytes == 44 && WB.track[tide].mtime > 0,
          "warble: lengths from the WAV headers, the rest not known (WM2)");

    /* WM2: one track looked at again with files.stat */
    CHECK(wb_write("Music/Tide.mp3", "text", "ID3 not decoded, longer now") &&
              warble_restat(&WB, (uint32_t) tide) == ZXM_OK && WB.track[tide].bytes == 27 &&
              warble_restat(&WB, WB.n) == ZXM_E_ARG,
          "warble: files.stat: the new size: %s", LINK.err);

    /* WM3: found by name with search.query */
    CHECK(warble_search(&WB, "morning") == ZXM_OK && WB.nhits == 1 &&
              WB.hit[0] == (uint32_t) morning,
          "warble: search.query finds the track: %s (%u hits)", LINK.err, WB.nhits);
    CHECK(warble_search(&WB, "lyrics") == ZXM_OK && WB.nhits == 0 &&
              warble_search(&WB, "   ") == ZXM_E_ARG && warble_search(&WB, "") == ZXM_E_ARG &&
              warble_search(&WB, NULL) == ZXM_E_ARG,
          "warble: a note is no track; an empty search is never sent");

    /* WM4: a playlist and its exact length, shown through halloo.hms */
    CHECK(warble_length(&WB, &ms, &unknown) == ZXM_OK && ms == 0 && unknown == 0 &&
              warble_length_text(&WB, text, sizeof text) == ZXM_OK && !strcmp(text, "0:00:00"),
          "warble: an empty playlist lasts 0:00:00");
    CHECK(warble_queue(&WB, (uint32_t) morning) == ZXM_OK &&
              warble_queue(&WB, (uint32_t) walk) == ZXM_OK &&
              warble_queue(&WB, (uint32_t) tide) == ZXM_OK &&
              warble_queue(&WB, (uint32_t) walk) == ZXM_OK &&
              warble_queue(&WB, WB.n) == ZXM_E_ARG && WB.nlist == 4,
          "warble: four places in the playlist, one track twice");
    CHECK(warble_length(&WB, &ms, &unknown) == ZXM_OK && ms == 362000 && unknown == 1 &&
              warble_length_text(&WB, text, sizeof text) == ZXM_OK && !strcmp(text, "0:06:02") &&
              warble_length_text(&WB, text, 4) == ZXM_E_TOOBIG,
          "warble: 2 s + 3 min + ? + 3 min = 0:06:02, one length not known (%lld, %s)",
          (long long) ms, text);
    {
        static warble_t FULL;
        FULL = WB;
        uint32_t k = 0;
        while (warble_queue(&FULL, (uint32_t) walk) == ZXM_OK) k++;
        CHECK(FULL.nlist == WARBLE_PLAYLIST_MAX && k == WARBLE_PLAYLIST_MAX - 4u &&
                  warble_length(&FULL, &ms, &unknown) == ZXM_OK && ms == 2000 + 98 * 180000 &&
                  warble_length_text(&FULL, text, sizeof text) == ZXM_OK &&
                  !strcmp(text, "4:54:02"),
              "warble: a full playlist, its length still shown (%s)", text);
    }

    /* WM5: the shuffle, every place once, then back to the playlist's order */
    CHECK(warble_at(&WB, 0) == morning && warble_at(&WB, 3) == walk && warble_at(&WB, 4) == -1,
          "warble: in order without a seed");
    CHECK(warble_shuffle(&WB, -1) == ZXM_E_ARG &&
              warble_shuffle(&WB, WARBLE_SEED_MAX + 1) == ZXM_E_ARG &&
              warble_shuffle(&WB, 42) == ZXM_OK && WB.shuffled,
          "warble: a shuffle from seed 42");
    {
        uint32_t seen = 0;
        bool ok = true;
        for (uint32_t i = 0; i < 4; i++) {
            int64_t p = warble_shuffle_at(42, 4, i);
            ok = ok && p >= 0 && warble_at(&WB, i) == (int32_t) WB.list[p];
            seen |= 1u << p;
        }
        CHECK(ok && seen == 15u, "warble: the play order is shuffle_at's, every place once");
        int32_t second = warble_at(&WB, 1);
        CHECK(warble_next(&WB) == second && WB.place == 1 && warble_next(&WB) >= 0 &&
                  warble_next(&WB) >= 0 && warble_next(&WB) == -1,
              "warble: next through the shuffle to its end");
    }
    CHECK(warble_unqueue(&WB, 2) == ZXM_OK && !WB.shuffled && WB.nlist == 3 &&
              warble_unqueue(&WB, 3) == ZXM_E_ARG && warble_length(&WB, &ms, &unknown) == ZXM_OK &&
              ms == 362000 && unknown == 0,
          "warble: a place taken out leaves the shuffle (WM5)");
    warble_unshuffle(&WB);
    CHECK(warble_at(&WB, 0) == morning && WB.place == 0, "warble: back to the playlist's order");

    /* WM6: the volume under the owner's cap */
    CHECK(warble_set_volume(&WB, 80) == ZXM_OK && WB.volume == 80 &&
              warble_set_volume(&WB, 86) == ZXM_E_ARG && WB.volume == 80 &&
              warble_set_volume(&WB, -1) == ZXM_E_ARG,
          "warble: a volume past the cap is refused, never turned down");
    CHECK(warble_set_cap(&WB, 60, false) == ZXM_E_STATE && WB.cap == WARBLE_CAP_START &&
              warble_set_cap(&WB, 101, true) == ZXM_E_ARG &&
              warble_set_cap(&WB, 60, true) == ZXM_OK && WB.cap == 60 && WB.volume == 60 &&
              strstr(WB.notice, "brought down") != NULL,
          "warble: only the owner's confirmed word moves the cap, and says so");

    /* WM7: the hall; not linked here (-32601), then answered by FAKE */
    CHECK(warble_hall_status(&WB) == ZXM_E_STATE && warble_hall_set(&WB, "Bad") == ZXM_E_ARG &&
              warble_hall_set(&WB, "b1") == ZXM_E_ARG && warble_hall_set(&WB, WB_HALL) == ZXM_OK,
          "warble: no hall yet; a CID that is not base32 is refused");
    CHECK(warble_hall_status(&WB) == ZXM_E_REMOTE && LINK.err_code == -32601 && !WB.hall.set,
          "warble: hall.status is not linked here: -32601 (%s)", LINK.err);
    WB.link = &FAKE;
    CANNED = WB_OK "{\"set\":true,\"state\":\"stop\",\"media\":\"" WB_MEDIA "\",\"pos_ms\":0,"
                   "\"base_pos_ms\":0,\"at_ms\":5,\"rate_q16\":65536,\"now_ms\":5,\"viewers\":3,"
                   "\"may_control\":true}}";
    CHECK(warble_hall_load(&WB, WB_MEDIA, -1, true) == ZXM_OK && WB.hall.set &&
              WB.hall.state == WARBLE_HALL_STOP && !strcmp(WB.hall.media, WB_MEDIA) &&
              WB.hall.viewers == 3 && WB.hall.may_control &&
              strstr(last_req, "\"method\":\"hall.load\"") &&
              strstr(last_req, "\"media\":\"" WB_MEDIA) && !strstr(last_req, "pos_ms"),
          "warble: hall.load, the track's media in the hall (%s)", FAKE.err);
    CANNED =
        WB_OK "{\"set\":true,\"state\":\"play\",\"media\":\"" WB_MEDIA "\",\"pos_ms\":1500,"
              "\"base_pos_ms\":1500,\"at_ms\":9,\"rate_q16\":131072,\"now_ms\":9,\"viewers\":3,"
              "\"may_control\":true}}";
    CHECK(warble_hall_play(&WB, 1500, 131072, true) == ZXM_OK && WB.hall.state == WARBLE_HALL_PLAY &&
              WB.hall.pos_ms == 1500 && WB.hall.rate_q16 == 131072 &&
              strstr(last_req, "\"pos_ms\":1500") && strstr(last_req, "\"rate_q16\":131072"),
          "warble: hall.play at twice the speed (%s)", FAKE.err);
    CANNED = WB_OK "{\"set\":true,\"state\":\"pause\",\"media\":\"" WB_MEDIA "\",\"pos_ms\":4000,"
                   "\"base_pos_ms\":4000,\"at_ms\":12,\"rate_q16\":131072,\"now_ms\":12,"
                   "\"viewers\":2,\"may_control\":true}}";
    CHECK(warble_hall_pause(&WB, -1, true) == ZXM_OK && WB.hall.state == WARBLE_HALL_PAUSE &&
              WB.hall.pos_ms == 4000 && WB.hall.viewers == 2 &&
              strstr(last_req, "\"method\":\"hall.pause\"") && !strstr(last_req, "pos_ms"),
          "warble: hall.pause where every listener is (%s)", FAKE.err);
    CANNED =
        WB_OK "{\"set\":true,\"state\":\"dance\",\"media\":\"x\",\"pos_ms\":1,\"rate_q16\":65536}}";
    CHECK(warble_hall_status(&WB) == ZXM_E_PROTOCOL && WB.hall.state == WARBLE_HALL_PAUSE,
          "warble: a state zxvd never says is refused, the hall kept");
    last_req[0] = 0;
    CHECK(warble_hall_load(&WB, WB_MEDIA, 0, false) == ZXM_E_STATE &&
              warble_hall_play(&WB, 0, 0, false) == ZXM_E_STATE &&
              warble_hall_pause(&WB, -1, false) == ZXM_E_STATE && !last_req[0] &&
              WB.hall.state == WARBLE_HALL_PAUSE && WB.hall.pos_ms == 4000 &&
              strstr(WB.notice, "owner's confirmed word") != NULL,
          "warble: without the owner's word no hall call is sent and the hall is kept");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32032,\"message\":\"only the "
             "hall's owner controls playback\"}}";
    CHECK(warble_hall_play(&WB, -1, 0, true) == ZXM_E_REMOTE && FAKE.err_code == -32032 &&
              WB.hall.state == WARBLE_HALL_PAUSE,
          "warble: the owner's hall refuses a listener, shown as is");
    CHECK(warble_hall_load(&WB, "Not a CID", 0, true) == ZXM_E_ARG &&
              warble_hall_play(&WB, -2, 0, true) == ZXM_E_ARG &&
              warble_hall_play(&WB, 0, WARBLE_RATE_MAX + 1, true) == ZXM_E_ARG,
          "warble: a bad media, position or rate is never sent");
    CANNED = NULL;
    WB.link = &LINK;
}
