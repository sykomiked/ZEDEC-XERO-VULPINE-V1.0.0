/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Glimpse's model against zxvd's real dispatcher (LINK): screenshots
 * written with files.write into the test's scratch Hoard under the first
 * number free in files.list's listing, the room from sys.disk and
 * recordings cut with stream.plan. A disk with little room is answered on a
 * link of its own (GLM_LINK), which passes every other request to the real
 * dispatcher. zxvd has no capture method and nothing is captured here: the
 * pictures are small hand-made PNG byte strings, handed in as the view
 * would. */
#include "glimpse_model.h"

static glimpse_t GLM;
static zxm_link_t GLM_LINK;
static const char *GLM_DISK; /* sys.disk's result, when set */

static int32_t glm_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    if (GLM_DISK && strstr(req, "\"sys.disk\""))
        return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq,
                        GLM_DISK);
    return inproc(NULL, req, len, resp, cap);
}

/* a PNG-shaped picture of n bytes: signature, IHDR of w by h, a fill, IEND */
static void glm_png(uint8_t *p, uint32_t n, uint32_t w, uint32_t h, uint8_t seed)
{
    static const uint8_t head[16] = {0x89u, 'P', 'N', 'G', 0x0Du, 0x0Au, 0x1Au, 0x0Au,
                                     0,     0,   0,   13,  'I',   'H',   'D',   'R'};
    static const uint8_t tail[12] = {0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xAEu, 0x42u, 0x60u, 0x82u};
    memcpy(p, head, sizeof head);
    for (uint32_t k = 0; k < 4u; k++) {
        p[16 + k] = (uint8_t) (w >> (24u - 8u * k));
        p[20 + k] = (uint8_t) (h >> (24u - 8u * k));
    }
    for (uint32_t i = 24; i + 12u < n; i++) p[i] = (uint8_t) (seed + i * 5u);
    memcpy(p + n - 12u, tail, sizeof tail);
}

/* the file at path holds exactly n bytes of want */
static bool glm_file_is(const char *path, const uint8_t *want, uint32_t n)
{
    static uint8_t buf[65536];
    uint32_t got = 0, at = 0;
    uint64_t size = 0;
    if (n > sizeof buf) return false;
    while (at < sizeof buf) {
        uint32_t cap = sizeof buf - at < 49152u ? sizeof buf - at : 49152u;
        if (zxf_read(path, at, buf + at, cap, false, &got, &size) != 0) return false;
        at += got;
        if (!got || at >= size) break;
    }
    return size == n && at == n && !memcmp(buf, want, n);
}

APPS_TEST(glimpse)
{
    static uint8_t small[600], big[30001], odd[600], probe[16];
    uint32_t pgot = 0;
    uint64_t size = 0;
    glm_png(small, sizeof small, 800, 600, 3);
    glm_png(big, sizeof big, 800, 600, 11);
    glm_png(odd, sizeof odd, 801, 600, 5); /* not the region's size */

    /* the rules (GR1-GR5) */
    CHECK(glimpse_capture_origin(-20, 1920) == 0 && glimpse_capture_origin(2000, 1920) == 1920 &&
              glimpse_capture_extent(-20, 100, 1920) == 80 &&
              glimpse_recording_bytes(800, 600, 4, 10, 30, 576000000) == 576000000 &&
              glimpse_recording_bytes(800, 600, 4, 10, 30, 575999999) == GLIMPSE_REFUSED &&
              glimpse_shot_fits(600, 1600, 1000) && !glimpse_shot_fits(601, 1600, 1000),
          "glimpse: the clamp, the size and the room");

    /* GM1, GM2: the screen and a region clamped to it */
    zxm_link_init(&GLM_LINK, glm_send, &GLM_LINK);
    glimpse_init(&GLM, &GLM_LINK);
    int before = sent;
    CHECK(glimpse_select(&GLM, 0, 0, 10, 10) == ZXM_E_STATE &&
              glimpse_select_screen(&GLM) == ZXM_E_STATE &&
              glimpse_set_screen(&GLM, 0, 1080) == ZXM_E_ARG &&
              glimpse_set_screen(&GLM, 1920, GLIMPSE_SIDE_MAX + 1) == ZXM_E_ARG && !GLM.screen_w &&
              glimpse_set_screen(&GLM, 1920, 1080) == ZXM_OK,
          "glimpse: no screen, no region; a screen of no size is refused");
    CHECK(glimpse_select(&GLM, -100, -50, 900, 650) == ZXM_OK && GLM.region && GLM.x == 0 &&
              GLM.y == 0 && GLM.w == 800 && GLM.h == 600,
          "glimpse: a region past the top left corner is clamped: %lld %lld %lld %lld",
          (long long) GLM.x, (long long) GLM.y, (long long) GLM.w, (long long) GLM.h);
    CHECK(glimpse_select(&GLM, 1800, 1000, 500, 500) == ZXM_OK && GLM.x == 1800 && GLM.y == 1000 &&
              GLM.w == 120 && GLM.h == 80,
          "glimpse: past the bottom right: clamped to 120 by 80");
    CHECK(glimpse_select(&GLM, 1920, 0, 10, 10) == ZXM_E_ARG && strstr(GLM.notice, "Nothing") &&
              glimpse_select(&GLM, 0, 0, -1, 10) == ZXM_E_ARG &&
              glimpse_select(&GLM, GLIMPSE_POS_MAX + 1, 0, 10, 10) == ZXM_E_ARG && GLM.x == 1800 &&
              GLM.w == 120 && sent == before,
          "glimpse: a region off the screen or no region is refused, the one kept stays");
    CHECK(glimpse_select_screen(&GLM) == ZXM_OK && GLM.w == 1920 && GLM.h == 1080,
          "glimpse: the whole screen");
    CHECK(glimpse_select(&GLM, 560, 240, 800, 600) == ZXM_OK && GLM.w == 800 && GLM.h == 600,
          "glimpse: a window of 800 by 600");

    /* GM2: the picture must be a PNG of the region's size */
    CHECK(glimpse_take(&GLM, odd, sizeof odd) == ZXM_E_ARG && strstr(GLM.notice, "size") &&
              glimpse_take(&GLM, small, 40) == ZXM_E_ARG &&
              glimpse_take(&GLM, NULL, 600) == ZXM_E_ARG &&
              glimpse_take(&GLM, small + 1, sizeof small - 1u) == ZXM_E_ARG &&
              glimpse_take(&GLM, small, GLIMPSE_SHOT_MAX + 1u) == ZXM_E_ARG && GLM.shot_len == 0 &&
              sent == before,
          "glimpse: a picture that is no PNG or not the region's size is refused");
    CHECK(glimpse_save(&GLM, true) == ZXM_E_STATE && sent == before,
          "glimpse: nothing taken, nothing saved");
    CHECK(glimpse_take(&GLM, small, sizeof small) == ZXM_OK && GLM.shot_len == sizeof small,
          "glimpse: a PNG of the region");

    /* GM3: without the owner's confirmation nothing is sent and nothing changes */
    CHECK(glimpse_save(&GLM, false) == ZXM_E_STATE && sent == before &&
              GLM.shot_len == sizeof small && GLM.last_n == 0 && strstr(GLM.notice, "confirmed") &&
              zxf_read(GLIMPSE_DIR "/Screenshot 1.png", 0, probe, sizeof probe, false, &pgot,
                       &size) != 0,
          "glimpse: not confirmed: nothing sent, nothing saved");

    /* GM3: room first, from sys.disk */
    CHECK(glimpse_room(&GLM) == ZXM_OK && GLM.total > 0 && GLM.avail >= 0,
          "glimpse: the real disk: %s", GLM_LINK.err);
    GLM_DISK = "{\"path\":\"/h\",\"total\":100000,\"free\":2000,\"avail\":1599}";
    CHECK(glimpse_set_floor(&GLM, -1) == ZXM_E_ARG &&
              glimpse_set_floor(&GLM, GLIMPSE_ROOM_MAX + 1) == ZXM_E_ARG &&
              glimpse_set_floor(&GLM, 1000) == ZXM_OK && glimpse_save(&GLM, true) == ZXM_E_TOOBIG &&
              GLM.last_n == 0 && strstr(GLM.notice, "is 1 bytes more than the room left") &&
              GLM.shot_len == sizeof small,
          "glimpse: one byte short with the floor kept: not saved (%s)", GLM.notice);
    GLM_DISK = "{\"path\":\"/h\",\"total\":100000,\"free\":2000,\"avail\":1600}";
    CHECK(glimpse_save(&GLM, true) == ZXM_OK && GLM.last_n == 1 &&
              !strcmp(GLM.last_path, "Screenshots/Screenshot 1.png") && GLM.shot_len == 0 &&
              GLM.folder,
          "glimpse: exactly fits: Screenshot 1: %s", GLM_LINK.err);
    CHECK(glm_file_is("Screenshots/Screenshot 1.png", small, sizeof small),
          "glimpse: the screenshot in the Hoard, byte for byte");
    GLM_DISK = NULL;
    CHECK(glimpse_set_floor(&GLM, 0) == ZXM_OK, "glimpse: no floor");

    /* GM3, GR4: the first free number, gaps filled, other names left alone */
    CHECK(zxf_write("Screenshots/Screenshot 3.png", small, sizeof small, ZXF_W_REPLACE, false,
                    &size) == 0 &&
              zxf_write("Screenshots/Screenshot 02.png", small, sizeof small, ZXF_W_REPLACE, false,
                        &size) == 0 &&
              zxf_write("Screenshots/Screenshot 2.jpg", small, sizeof small, ZXF_W_REPLACE, false,
                        &size) == 0 &&
              zxf_mkdir("Screenshots/Screenshot 2.png") == 0,
          "glimpse: a folder with a gap and names that are not screenshots");
    int64_t n = 0;
    CHECK(glimpse_next_number(&GLM, &n) == ZXM_OK && n == 2 && GLM.nused == 2 &&
              glimpse_next_number(&GLM, NULL) == ZXM_E_ARG,
          "glimpse: 1 and 3 used: 2 is free (%lld) %s", (long long) n, GLM_LINK.err);
    CHECK(glimpse_take(&GLM, big, sizeof big) == ZXM_OK && glimpse_save(&GLM, true) == ZXM_E_REMOTE,
          "glimpse: a folder named Screenshot 2.png is in the way: refused, never overwritten");
    CHECK(GLM.last_n == 1 && GLM.shot_len == sizeof big, "glimpse: nothing changed");
    zxm_begin(&LINK, "files.trash");
    zxm_p_str(&LINK, "path", "Screenshots/Screenshot 2.png");
    CHECK(zxm_call(&LINK) == ZXM_OK, "glimpse: the folder in the way is moved to the Trash: %s",
          LINK.err);
    CHECK(glimpse_save(&GLM, true) == ZXM_OK && GLM.last_n == 2 &&
              glm_file_is("Screenshots/Screenshot 2.png", big, sizeof big) &&
              glm_file_is("Screenshots/Screenshot 1.png", small, sizeof small),
          "glimpse: the gap filled, in several parts, byte for byte: %s", GLM_LINK.err);
    CHECK(glimpse_take(&GLM, small, sizeof small) == ZXM_OK && glimpse_save(&GLM, true) == ZXM_OK &&
              GLM.last_n == 4 && !strcmp(GLM.last_path, "Screenshots/Screenshot 4.png"),
          "glimpse: 1, 2 and 3 used: 4");

    /* GM4: a recording starts only confirmed and only when it fits; it always shows */
    before = sent;
    CHECK(glimpse_record_start(&GLM, 4, 61, 10, true) == ZXM_E_ARG &&
              glimpse_record_start(&GLM, 4, 30, 7201, true) == ZXM_E_ARG &&
              glimpse_record_start(&GLM, 0, 30, 10, true) == ZXM_E_ARG &&
              glimpse_record_start(&GLM, 4, 30, 10, false) == ZXM_E_STATE && !GLM.recording &&
              !GLM.indicator[0] && sent == before,
          "glimpse: past the limits or not confirmed: not recording, nothing sent");
    /* 800 * 600 * 4 * 10 * 30 = 576000000 bytes */
    GLM_DISK = "{\"path\":\"/h\",\"total\":900000000,\"free\":600000000,\"avail\":576000999}";
    CHECK(glimpse_set_floor(&GLM, 1000) == ZXM_OK &&
              glimpse_record_start(&GLM, 4, 30, 10, true) == ZXM_E_TOOBIG && !GLM.recording &&
              !GLM.indicator[0] && strstr(GLM.notice, "is 1 bytes more than the room left"),
          "glimpse: one byte past the room: not recording (%s)", GLM.notice);
    GLM_DISK = "{\"path\":\"/h\",\"total\":900000000,\"free\":600000000,\"avail\":999}";
    CHECK(glimpse_record_start(&GLM, 4, 30, 10, true) == ZXM_E_TOOBIG && !GLM.recording,
          "glimpse: less room than the floor: not recording");
    GLM_DISK = "{\"path\":\"/h\",\"total\":900000000,\"free\":600000000,\"avail\":576001000}";
    CHECK(glimpse_record_start(&GLM, 4, 30, 10, true) == ZXM_OK && GLM.recording &&
              !strcmp(GLM.indicator, GLIMPSE_REC_TEXT) && GLM.rec_bytes == 576000000 &&
              GLM.frames_max == 300,
          "glimpse: exactly fits: recording, and shown as recording");
    GLM_DISK = NULL;
    CHECK(glimpse_record_start(&GLM, 4, 30, 10, true) == ZXM_E_STATE &&
              glimpse_select(&GLM, 0, 0, 10, 10) == ZXM_E_STATE &&
              glimpse_set_screen(&GLM, 640, 480) == ZXM_E_STATE && GLM.w == 800,
          "glimpse: while recording the region and screen stay");
    bool shown = true;
    for (int i = 0; i < 299; i++)
        shown = glimpse_record_frame(&GLM) == ZXM_OK && GLM.recording &&
                !strcmp(GLM.indicator, GLIMPSE_REC_TEXT) && shown;
    CHECK(shown && GLM.frames == 299, "glimpse: shown as recording on every frame");
    CHECK(glimpse_record_frame(&GLM) == ZXM_OK && !GLM.recording && !GLM.indicator[0] &&
              GLM.frames == 300 && glimpse_record_frame(&GLM) == ZXM_E_STATE,
          "glimpse: the last frame stops it, and it shows that it stopped");

    /* GM5: the recording cut with stream.plan */
    static const glimpse_unit_t units[4] = {
        {100, 0, true}, {50, 33000, false}, {70, 1000000, true}, {20, 1500000, false}};
    static const glimpse_unit_t back[2] = {{10, 5, true}, {10, 4, true}};
    before = sent;
    CHECK(glimpse_cut(&GLM, units, 0, 1000000, 2000000) == ZXM_E_ARG &&
              glimpse_cut(&GLM, units, GLIMPSE_UNITS + 1u, 1000000, 2000000) == ZXM_E_ARG &&
              glimpse_cut(&GLM, units, 4, 0, 2000000) == ZXM_E_ARG &&
              glimpse_cut(&GLM, units, 4, 1000000, 999999) == ZXM_E_ARG &&
              glimpse_cut(&GLM, back, 2, 1, 1) == ZXM_E_ARG && sent == before,
          "glimpse: a cut with no units, units going back or a bad target is refused");
    CHECK(glimpse_cut(&GLM, units, 4, 1000000, 2000000) == ZXM_OK && GLM.nsegs == 2 &&
              GLM.seg[0].first_unit == 0 && GLM.seg[0].nunits == 2 && GLM.seg[0].len == 150 &&
              GLM.seg[0].dur_us == 1000000 && GLM.seg[1].first_unit == 2 &&
              GLM.seg[1].offset == 150 && GLM.seg[1].len == 90 &&
              GLM.seg[1].t_start_us == 1000000 && GLM.seg[1].key_start,
          "glimpse: cut at the keyframe past the target: %s", GLM_LINK.err);
    GLM_DISK = "{\"path\":\"/h\",\"total\":900000000,\"free\":600000000,\"avail\":576001000}";
    CHECK(glimpse_record_start(&GLM, 4, 30, 10, true) == ZXM_OK &&
              glimpse_cut(&GLM, units, 4, 1000000, 2000000) == ZXM_E_STATE && GLM.nsegs == 0,
          "glimpse: no cut while recording");
    GLM_DISK = NULL;
    glimpse_record_stop(&GLM);
    CHECK(!GLM.recording && !GLM.indicator[0], "glimpse: stopped, shown as stopped");
}
