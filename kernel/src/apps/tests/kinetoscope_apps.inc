/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Kinetoscope's model against zxvd's real dispatcher (LINK): IVF videos
 * written with files.write into the test's scratch Hoard, opened and
 * indexed with files.read, cut with stream.plan, a segment described with
 * stream.segment, fitted to windows, a position's frame, and the resume
 * table. hall.* is served in the daemon by zxvd_pspace.c, which this test
 * does not link (the dispatcher answers -32601), so the hall's answers are
 * made up in hall.status's documented shape (FAKE). */
#include "kinetoscope_model.h"

#define KINE_VIDEO_MAX 12000u
#define KINE_HALL      "bafkreihallforkinetoscope"
#define KINE_MEDIA     "bafkreimovienight"
#define KINE_OK        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":"

static uint8_t KINE_BYTES[KINE_VIDEO_MAX];
static char KINE_B64[4u * (KINE_VIDEO_MAX / 3u + 1u) + 1u];
static kinetoscope_t KINE;

static void kine_le(uint8_t *p, uint64_t v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t) (v >> (8u * i));
}

/* an IVF file: codec, size, time base rate / scale, frames of the given
 * lengths whose first bytes are first[], at ticks 0, 1, 2 ...; its length */
static uint32_t kine_ivf(const char *codec, uint32_t w, uint32_t h, uint32_t rate, uint32_t scale,
                         uint32_t n, const uint32_t *len, const uint8_t *first)
{
    uint32_t at = KINETOSCOPE_IVF_HDR;
    memset(KINE_BYTES, 0, KINETOSCOPE_IVF_HDR);
    memcpy(KINE_BYTES, "DKIF", 4);
    kine_le(KINE_BYTES + 6, KINETOSCOPE_IVF_HDR, 2);
    memcpy(KINE_BYTES + 8, codec, 4);
    kine_le(KINE_BYTES + 12, w, 2);
    kine_le(KINE_BYTES + 14, h, 2);
    kine_le(KINE_BYTES + 16, rate, 4);
    kine_le(KINE_BYTES + 20, scale, 4);
    kine_le(KINE_BYTES + 24, n, 4);
    for (uint32_t f = 0; f < n; f++) {
        kine_le(KINE_BYTES + at, len[f], 4);
        kine_le(KINE_BYTES + at + 4, f, 8);
        at += KINETOSCOPE_FRAME_HDR;
        KINE_BYTES[at] = first[f];
        for (uint32_t j = 1; j < len[f]; j++) KINE_BYTES[at + j] = (uint8_t) (f * 7u + j);
        at += len[f];
    }
    return at;
}

/* n bytes of KINE_BYTES as padded base64 into KINE_B64 */
static void kine_b64(uint32_t n)
{
    static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t o = 0;
    for (uint32_t i = 0; i < n; i += 3u) {
        uint32_t v = (uint32_t) KINE_BYTES[i] << 16;
        if (i + 1u < n) v |= (uint32_t) KINE_BYTES[i + 1u] << 8;
        if (i + 2u < n) v |= KINE_BYTES[i + 2u];
        KINE_B64[o++] = A[v >> 18 & 63u];
        KINE_B64[o++] = A[v >> 12 & 63u];
        KINE_B64[o++] = i + 1u < n ? A[v >> 6 & 63u] : '=';
        KINE_B64[o++] = i + 2u < n ? A[v & 63u] : '=';
    }
    KINE_B64[o] = 0;
}

static bool kine_write(const char *path, uint32_t n)
{
    kine_b64(n);
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", path);
    zxm_p_str(&LINK, "base64", KINE_B64);
    return zxm_call(&LINK) == ZXM_OK;
}

static bool kine_write_text(const char *path, const char *text)
{
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", path);
    zxm_p_str(&LINK, "text", text);
    return zxm_call(&LINK) == ZXM_OK;
}

APPS_TEST(kinetoscope)
{
    /* ten VP8 frames at 30 a second, keyframes at 0 and 5 */
    static const uint32_t vlen[10] = {400, 120, 130, 140, 150, 380, 160, 170, 180, 190};
    static const uint8_t vkey[10] = {0x10, 0x11, 0x11, 0x11, 0x11, 0x10, 0x11, 0x11, 0x11, 0x11};
    /* five VP9 frames at 24000/1001: key, inter, shown again, profile 3 key, inter */
    static const uint32_t nlen[5] = {50, 40, 30, 60, 20};
    static const uint8_t nkey[5] = {0x80, 0x84, 0x88, 0xB0, 0xB2};
    kinetoscope_rect_t r;
    char text[24];
    int64_t pos = 0;
    int before;

    /* KM1: nothing applies before a video is open */
    kinetoscope_init(&KINE, &LINK);
    CHECK(!KINE.open && kinetoscope_index(&KINE) == ZXM_E_STATE &&
              kinetoscope_plan(&KINE, 100000, 1000000) == ZXM_E_STATE &&
              kinetoscope_fit(&KINE, 100, 100, &r) == ZXM_E_STATE &&
              kinetoscope_frame(&KINE, 0) == KINETOSCOPE_REFUSED &&
              kinetoscope_resume_keep(&KINE, 0, 1) == ZXM_E_STATE &&
              kinetoscope_open(&KINE, "") == ZXM_E_ARG &&
              kinetoscope_open(&KINE, NULL) == ZXM_E_ARG,
          "kinetoscope: before a video is open, nothing applies");

    zxm_begin(&LINK, "files.mkdir");
    zxm_p_str(&LINK, "path", "Videos");
    CHECK(zxm_call(&LINK) == ZXM_OK, "kinetoscope: a Videos folder: %s", LINK.err);
    uint32_t n9 = kine_ivf("VP90", 1080, 1920, 24000, 1001, 5, nlen, nkey);
    CHECK(n9 == KINETOSCOPE_IVF_HDR + 5u * KINETOSCOPE_FRAME_HDR + 200u &&
              kine_write("Videos/Portrait.ivf", n9),
          "kinetoscope: a VP9 video written: %s", LINK.err);
    uint32_t n8 = kine_ivf("VP80", 640, 360, 30, 1, 10, vlen, vkey);
    CHECK(kine_write("Videos/Harbour.ivf", n8) &&
              kine_write_text("Videos/notes.txt", "not a video"),
          "kinetoscope: a VP8 video and a note written: %s", LINK.err);

    /* KM1: the VP8 video's header */
    CHECK(kinetoscope_open(&KINE, "Videos/Harbour.ivf") == ZXM_OK && KINE.open &&
              KINE.codec == KINETOSCOPE_VP8 && KINE.width == 640 && KINE.height == 360 &&
              KINE.fnum == 30 && KINE.fden == 1 && KINE.frames == 10 && KINE.length_ms == 333 &&
              KINE.size == (int64_t) n8 && !KINE.at_end,
          "kinetoscope: the IVF header read (%s)", LINK.err);
    CHECK(kinetoscope_open(&KINE, "Videos/notes.txt") == ZXM_E_STATE && KINE.open &&
              KINE.codec == KINETOSCOPE_VP8 && !strcmp(KINE.path, "Videos/Harbour.ivf") &&
              strstr(KINE.notice, "Not a video") != NULL,
          "kinetoscope: a note is no video; the open video is kept");
    CHECK(kinetoscope_open(&KINE, "Videos/None.ivf") == ZXM_E_REMOTE && KINE.open,
          "kinetoscope: a missing file is zxvd's refusal, shown as is");

    /* KM2: the frames, one files.read each, keyframes from their first bytes */
    before = sent;
    CHECK(kinetoscope_index(&KINE) == ZXM_OK && KINE.nunits == 10 && sent == before + 10 &&
              KINE.at_end && KINE.first_frame == 0 && KINE.read_frames == 10,
          "kinetoscope: ten frames indexed with ten reads: %s (%u)", LINK.err, KINE.nunits);
    {
        bool ok = true;
        int64_t off = KINETOSCOPE_IVF_HDR + KINETOSCOPE_FRAME_HDR;
        for (uint32_t i = 0; i < 10; i++) {
            ok = ok && KINE.unit[i].len == vlen[i] && KINE.unit[i].file_off == off &&
                 KINE.unit[i].pts_us == (int64_t) i * 1000000 / 30 &&
                 KINE.unit[i].key == (i % 5 == 0);
            off += vlen[i] + KINETOSCOPE_FRAME_HDR;
        }
        CHECK(ok, "kinetoscope: every frame's place, length, time and keyframe (KM2)");
    }
    CHECK(kinetoscope_index(&KINE) == ZXM_E_STATE && KINE.nunits == 10 &&
              strstr(KINE.notice, "end") != NULL,
          "kinetoscope: past the last frame is the end, the window kept");

    /* KM3: the plan, zxvd's own cut, and its seek points (witness.kind_count) */
    CHECK(kinetoscope_plan(&KINE, 0, 1000000) == ZXM_E_ARG &&
              kinetoscope_plan(&KINE, 200000, 100000) == ZXM_E_ARG &&
              kinetoscope_plan(&KINE, 1, INT64_C(4294967296)) == ZXM_E_ARG,
          "kinetoscope: a plan's bad times are never sent");
    CHECK(kinetoscope_plan(&KINE, 100000, 1000000) == ZXM_OK && KINE.nsegs == 2 &&
              KINE.seg[0].first_unit == 0 && KINE.seg[0].nunits == 5 &&
              KINE.seg[1].first_unit == 5 && KINE.seg[1].nunits == 5 && KINE.seg[0].len == 940 &&
              KINE.seg[1].len == 1080 && KINE.seg[1].offset == 940 &&
              KINE.seg[0].dur_us == 166666 && KINE.seg[1].t_start_us == 166666 &&
              KINE.seg[1].dur_us == 166667 && KINE.seg[0].key_start && KINE.seg[1].key_start &&
              KINE.seek_points == 2,
          "kinetoscope: cut before each keyframe: two segments, two seek points (%s)", LINK.err);
    CHECK(kinetoscope_plan(&KINE, 1, 50000) == ZXM_OK && KINE.nsegs == 6 && KINE.seek_points == 2 &&
              KINE.seg[1].first_unit == 2 && !KINE.seg[1].key_start &&
              KINE.seg[3].first_unit == 5 && KINE.seg[3].key_start,
          "kinetoscope: at most 50 ms a segment: six, two of them seek points (%s, %u)", LINK.err,
          KINE.nsegs);

    /* KM4: a segment described by stream.segment */
    CHECK(kinetoscope_segment(&KINE, 6, 0, 0) == ZXM_E_ARG, "kinetoscope: no seventh segment");
    CHECK(kinetoscope_plan(&KINE, 100000, 1000000) == ZXM_OK &&
              kinetoscope_segment(&KINE, 1, 7, 1) == ZXM_OK && KINE.seg[1].described &&
              !strncmp(KINE.seg[1].cid, "bafkrei", 7) && KINE.seg[1].nfreights >= 1 &&
              !KINE.seg[0].described,
          "kinetoscope: segment 1's CID and freights (%s, %s)", LINK.err, KINE.seg[1].cid);
    {
        char cid[KINETOSCOPE_SEGCID_MAX + 1u];
        memcpy(cid, KINE.seg[1].cid, sizeof cid);
        CHECK(kinetoscope_segment(&KINE, 1, 7, 1) == ZXM_OK && !strcmp(cid, KINE.seg[1].cid) &&
                  kinetoscope_segment(&KINE, 0, 7, 0) == ZXM_OK && strcmp(cid, KINE.seg[0].cid),
              "kinetoscope: the same bytes, the same CID; other bytes, another");
    }

    /* KM5: the fit, letterboxed or pillarboxed, the bars half before */
    CHECK(kinetoscope_fit(&KINE, 1920, 1080, &r) == ZXM_OK && r.x == 0 && r.y == 0 && r.w == 1920 &&
              r.h == 1080,
          "kinetoscope: 16:9 in 16:9 fills the window");
    CHECK(kinetoscope_fit(&KINE, 1280, 1024, &r) == ZXM_OK && r.x == 0 && r.y == 152 &&
              r.w == 1280 && r.h == 720,
          "kinetoscope: letterboxed in 5:4, 152 pixels above");
    CHECK(kinetoscope_fit(&KINE, 802, 300, &r) == ZXM_OK && r.w == 533 && r.h == 300 && r.x == 134,
          "kinetoscope: pillarboxed, the odd pixel after the picture (%lld)", (long long) r.x);
    CHECK(kinetoscope_fit(&KINE, 70000, 300, &r) == ZXM_E_ARG &&
              kinetoscope_fit(&KINE, 100, 100, NULL) == ZXM_E_ARG,
          "kinetoscope: a window past 65535 is refused");

    /* KM6: the frame at a position */
    CHECK(kinetoscope_frame(&KINE, 0) == 0 && kinetoscope_frame(&KINE, 33) == 0 &&
              kinetoscope_frame(&KINE, 34) == 1 && kinetoscope_frame(&KINE, 333) == 9 &&
              kinetoscope_frame(&KINE, -1) == KINETOSCOPE_REFUSED &&
              kinetoscope_hall_frame(&KINE) == KINETOSCOPE_REFUSED,
          "kinetoscope: the frame shown at 0, 33, 34 and 333 ms");
    CHECK(kinetoscope_time_text(KINE.length_ms, text, sizeof text) == ZXM_OK &&
              !strcmp(text, "0:00:00") &&
              kinetoscope_time_text(5025000, text, sizeof text) == ZXM_OK &&
              !strcmp(text, "1:23:45") &&
              kinetoscope_time_text(-1, text, sizeof text) == ZXM_E_ARG &&
              kinetoscope_time_text(5025000, text, 4) == ZXM_E_TOOBIG,
          "kinetoscope: times shown through halloo.hms (%s)", text);

    /* KM8: resume, inside the video's length */
    CHECK(kinetoscope_resume_find(&KINE, &pos) == ZXM_E_STATE &&
              kinetoscope_resume_keep(&KINE, 334, 1760000000) == ZXM_E_ARG &&
              kinetoscope_resume_keep(&KINE, -1, 1760000000) == ZXM_E_ARG &&
              kinetoscope_resume_keep(&KINE, 200, KINETOSCOPE_STAMP_MAX + 1) == ZXM_E_ARG &&
              kinetoscope_resume_find(&KINE, &pos) == ZXM_E_STATE,
          "kinetoscope: a place outside the video is never kept");
    CHECK(kinetoscope_resume_keep(&KINE, 200, 1760000000) == ZXM_OK &&
              kinetoscope_resume_find(&KINE, &pos) == ZXM_OK && pos == 200 &&
              kinetoscope_resume_keep(&KINE, 250, 1760000060) == ZXM_OK &&
              kinetoscope_resume_find(&KINE, &pos) == ZXM_OK && pos == 250 &&
              KINE.resume[1].used == false,
          "kinetoscope: the place kept and moved in one record");

    /* KM1, KM2 on VP9: 23.976 frames a second and its keyframe bits */
    CHECK(kinetoscope_open(&KINE, "Videos/Portrait.ivf") == ZXM_OK &&
              KINE.codec == KINETOSCOPE_VP9 && KINE.fnum == 24000 && KINE.fden == 1001 &&
              KINE.length_ms == 208 && KINE.nunits == 0 &&
              kinetoscope_resume_find(&KINE, &pos) == ZXM_E_STATE,
          "kinetoscope: the VP9 video, its own resume place (none yet)");
    CHECK(kinetoscope_index(&KINE) == ZXM_OK && KINE.nunits == 5 && KINE.unit[0].key &&
              !KINE.unit[1].key && !KINE.unit[2].key && KINE.unit[3].key && !KINE.unit[4].key &&
              KINE.unit[1].pts_us == 41708 && KINE.unit[4].pts_us == 166833,
          "kinetoscope: VP9 keyframes, shown-again frames and profile 3 (%s)", LINK.err);
    CHECK(kinetoscope_fit(&KINE, 1366, 768, &r) == ZXM_OK && r.w == 432 && r.h == 768 &&
              r.x == 467 && r.y == 0 && kinetoscope_frame(&KINE, 1001) == 24,
          "kinetoscope: a portrait video pillarboxed; frame 24 at 1001 ms");
    CHECK(kinetoscope_open(&KINE, "Videos/Harbour.ivf") == ZXM_OK &&
              kinetoscope_resume_find(&KINE, &pos) == ZXM_OK && pos == 250 &&
              kinetoscope_resume_forget(&KINE) == ZXM_OK &&
              kinetoscope_resume_find(&KINE, &pos) == ZXM_E_STATE &&
              kinetoscope_resume_forget(&KINE) == ZXM_E_STATE,
          "kinetoscope: back to the first video, where it was left; then forgotten");

    /* KM2: a video cut short, and one of another codec */
    {
        uint32_t n = kine_ivf("VP80", 640, 360, 30, 1, 10, vlen, vkey);
        CHECK(kine_write("Videos/Cut.ivf", n - 100u), "kinetoscope: a cut file written");
        CHECK(kinetoscope_open(&KINE, "Videos/Cut.ivf") == ZXM_OK &&
                  kinetoscope_index(&KINE) == ZXM_E_STATE && KINE.nunits == 0 &&
                  KINE.read_frames == 0 && strstr(KINE.notice, "cut short") != NULL,
              "kinetoscope: a frame cut short refuses the window, nothing kept");
        n = kine_ivf("AV01", 640, 360, 30, 1, 1, vlen, vkey);
        CHECK(kine_write("Videos/Other.ivf", n) &&
                  kinetoscope_open(&KINE, "Videos/Other.ivf") == ZXM_E_STATE &&
                  !strcmp(KINE.path, "Videos/Cut.ivf"),
              "kinetoscope: another codec is refused");
        n = kine_ivf("VP80", 640, 360, 30, 1, 0, vlen, vkey);
        bool all = true;
        for (uint32_t i = 0; i < KINETOSCOPE_RESUME && all; i++) {
            char p[32] = "Videos/Empty00.ivf";
            p[12] = (char) ('0' + i / 10u);
            p[13] = (char) ('0' + i % 10u);
            all = kine_write(p, n) && kinetoscope_open(&KINE, p) == ZXM_OK && KINE.at_end &&
                  kinetoscope_resume_keep(&KINE, 0, 1) == ZXM_OK;
        }
        CHECK(all && kine_write("Videos/Empty99.ivf", n) &&
                  kinetoscope_open(&KINE, "Videos/Empty99.ivf") == ZXM_OK &&
                  kinetoscope_resume_keep(&KINE, 0, 1) == ZXM_E_STATE &&
                  strstr(KINE.notice, "full") != NULL,
              "kinetoscope: the resume table holds %u videos, then says it is full",
              KINETOSCOPE_RESUME);
    }

    /* KM7: the hall; the owner's word first, then not linked (-32601), then FAKE */
    CHECK(kinetoscope_open(&KINE, "Videos/Harbour.ivf") == ZXM_OK &&
              kinetoscope_hall_status(&KINE) == ZXM_E_STATE &&
              kinetoscope_hall_set(&KINE, "Bad") == ZXM_E_ARG &&
              kinetoscope_hall_set(&KINE, "b1") == ZXM_E_ARG &&
              kinetoscope_hall_set(&KINE, KINE_HALL) == ZXM_OK,
          "kinetoscope: no hall yet; a CID that is not base32 is refused");
    before = sent;
    CHECK(kinetoscope_hall_load(&KINE, KINE_MEDIA, 0, false) == ZXM_E_STATE &&
              kinetoscope_hall_play(&KINE, -1, 0, false) == ZXM_E_STATE &&
              kinetoscope_hall_pause(&KINE, -1, false) == ZXM_E_STATE &&
              kinetoscope_hall_seek(&KINE, 100, false) == ZXM_E_STATE && sent == before &&
              !KINE.hall.set && strstr(KINE.notice, "owner's confirmed word") != NULL,
          "kinetoscope: without the owner's word nothing is sent, the hall unchanged");
    CHECK(kinetoscope_hall_play(&KINE, 0, 0, true) == ZXM_E_REMOTE && LINK.err_code == -32601 &&
              sent == before + 1 && !KINE.hall.set,
          "kinetoscope: hall.play is not linked here: -32601 (%s)", LINK.err);
    KINE.link = &FAKE;
    CANNED = KINE_OK "{\"set\":true,\"state\":\"stop\",\"media\":\"" KINE_MEDIA "\",\"pos_ms\":0,"
                     "\"base_pos_ms\":0,\"at_ms\":5,\"rate_q16\":65536,\"now_ms\":5,\"viewers\":4,"
                     "\"may_control\":true}}";
    CHECK(kinetoscope_hall_load(&KINE, KINE_MEDIA, -1, true) == ZXM_OK && KINE.hall.set &&
              KINE.hall.state == KINETOSCOPE_HALL_STOP && !strcmp(KINE.hall.media, KINE_MEDIA) &&
              KINE.hall.viewers == 4 && KINE.hall.may_control &&
              strstr(last_req, "\"method\":\"hall.load\"") &&
              strstr(last_req, "\"media\":\"" KINE_MEDIA) && !strstr(last_req, "pos_ms"),
          "kinetoscope: hall.load, the media in the hall (%s)", FAKE.err);
    CANNED = KINE_OK "{\"set\":true,\"state\":\"play\",\"media\":\"" KINE_MEDIA "\",\"pos_ms\":200,"
                     "\"base_pos_ms\":200,\"at_ms\":9,\"rate_q16\":262144,\"now_ms\":9,"
                     "\"viewers\":4,\"may_control\":true}}";
    CHECK(kinetoscope_hall_play(&KINE, 200, KINETOSCOPE_RATE_MAX, true) == ZXM_OK &&
              KINE.hall.state == KINETOSCOPE_HALL_PLAY &&
              KINE.hall.rate_q16 == KINETOSCOPE_RATE_MAX &&
              strstr(last_req, "\"rate_q16\":262144") && strstr(last_req, "\"pos_ms\":200") &&
              kinetoscope_hall_frame(&KINE) == 6,
          "kinetoscope: hall.play at four times; every viewer on frame 6 (%s)", FAKE.err);
    last_req[0] = 0;
    CHECK(kinetoscope_hall_play(&KINE, 0, KINETOSCOPE_RATE_MAX + 1, true) == ZXM_E_ARG &&
              kinetoscope_hall_play(&KINE, 0, -1, true) == ZXM_E_ARG &&
              kinetoscope_hall_play(&KINE, -2, 0, true) == ZXM_E_ARG &&
              kinetoscope_hall_seek(&KINE, -1, true) == ZXM_E_ARG &&
              kinetoscope_hall_load(&KINE, "Not a CID", 0, true) == ZXM_E_ARG && !last_req[0] &&
              KINE.hall.state == KINETOSCOPE_HALL_PLAY,
          "kinetoscope: a rate past four times, a bad position or media is never sent");
    CANNED = KINE_OK "{\"set\":true,\"state\":\"play\",\"media\":\"" KINE_MEDIA "\",\"pos_ms\":300,"
                     "\"base_pos_ms\":300,\"at_ms\":12,\"rate_q16\":262144,\"now_ms\":12,"
                     "\"viewers\":4,\"may_control\":true}}";
    CHECK(kinetoscope_hall_seek(&KINE, 300, true) == ZXM_OK && KINE.hall.pos_ms == 300 &&
              strstr(last_req, "\"method\":\"hall.seek\"") && kinetoscope_hall_frame(&KINE) == 9,
          "kinetoscope: hall.seek keeps the state and the rate (%s)", FAKE.err);
    CANNED =
        KINE_OK "{\"set\":true,\"state\":\"pause\",\"media\":\"" KINE_MEDIA "\",\"pos_ms\":320,"
                "\"base_pos_ms\":320,\"at_ms\":14,\"rate_q16\":262144,\"now_ms\":14,"
                "\"viewers\":3,\"may_control\":true}}";
    CHECK(kinetoscope_hall_pause(&KINE, -1, true) == ZXM_OK &&
              KINE.hall.state == KINETOSCOPE_HALL_PAUSE && KINE.hall.viewers == 3 &&
              !strstr(last_req, "pos_ms") && kinetoscope_hall_status(&KINE) == ZXM_OK &&
              strstr(last_req, "\"method\":\"hall.status\""),
          "kinetoscope: hall.pause where every viewer is; hall.status (%s)", FAKE.err);
    CANNED = KINE_OK "{\"set\":true,\"state\":\"play\",\"media\":\"x\",\"pos_ms\":1,"
                     "\"rate_q16\":262145}}";
    CHECK(kinetoscope_hall_status(&KINE) == ZXM_E_PROTOCOL &&
              KINE.hall.state == KINETOSCOPE_HALL_PAUSE,
          "kinetoscope: a rate zxvd never keeps is refused, the hall kept");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32032,\"message\":\"only the "
             "hall's owner controls playback\"}}";
    CHECK(kinetoscope_hall_play(&KINE, -1, 0, true) == ZXM_E_REMOTE && FAKE.err_code == -32032 &&
              KINE.hall.state == KINETOSCOPE_HALL_PAUSE,
          "kinetoscope: the owner's hall refuses a viewer, shown as is");
    CANNED = NULL;
    KINE.link = &LINK;
}
