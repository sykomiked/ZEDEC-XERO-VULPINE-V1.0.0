/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Cutwater's model against zxvd's real dispatcher (LINK): IVF videos written
 * with files.write into the test's scratch Hoard, added as sources and read
 * with files.read, their cut points found with stream.plan, an edit list of
 * clips built and refused, its timecode, the list saved with files.write
 * (only on the owner's word) and opened again with files.read. */
#include "cutwater_model.h"

#define CUTW_VIDEO_MAX 4000u

static uint8_t CUTW_BYTES[CUTW_VIDEO_MAX];
static char CUTW_B64[4u * (CUTW_VIDEO_MAX / 3u + 1u) + 1u];
static cutwater_t CUTW, CUTW2;

static void cutw_le(uint8_t *p, uint64_t v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t) (v >> (8u * i));
}

/* a VP8 IVF file of n frames of 40 bytes at rate / scale, keyframes where
 * key[f] is 1, at ticks 0, 1, 2 ...; its length */
static uint32_t cutw_ivf(uint32_t rate, uint32_t scale, uint32_t n, const uint8_t *key)
{
    uint32_t at = KINETOSCOPE_IVF_HDR;
    memset(CUTW_BYTES, 0, KINETOSCOPE_IVF_HDR);
    memcpy(CUTW_BYTES, "DKIF", 4);
    cutw_le(CUTW_BYTES + 6, KINETOSCOPE_IVF_HDR, 2);
    memcpy(CUTW_BYTES + 8, "VP80", 4);
    cutw_le(CUTW_BYTES + 12, 640, 2);
    cutw_le(CUTW_BYTES + 14, 360, 2);
    cutw_le(CUTW_BYTES + 16, rate, 4);
    cutw_le(CUTW_BYTES + 20, scale, 4);
    cutw_le(CUTW_BYTES + 24, n, 4);
    for (uint32_t f = 0; f < n; f++) {
        cutw_le(CUTW_BYTES + at, 40, 4);
        cutw_le(CUTW_BYTES + at + 4, f, 8);
        at += KINETOSCOPE_FRAME_HDR;
        CUTW_BYTES[at] = key[f] ? 0x10 : 0x11; /* VP8: bit 0 clear on a keyframe */
        for (uint32_t j = 1; j < 40u; j++) CUTW_BYTES[at + j] = (uint8_t) (f * 5u + j);
        at += 40u;
    }
    return at;
}

static bool cutw_write(const char *path, uint32_t n)
{
    static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t o = 0;
    for (uint32_t i = 0; i < n; i += 3u) {
        uint32_t v = (uint32_t) CUTW_BYTES[i] << 16;
        if (i + 1u < n) v |= (uint32_t) CUTW_BYTES[i + 1u] << 8;
        if (i + 2u < n) v |= CUTW_BYTES[i + 2u];
        CUTW_B64[o++] = A[v >> 18 & 63u];
        CUTW_B64[o++] = A[v >> 12 & 63u];
        CUTW_B64[o++] = i + 1u < n ? A[v >> 6 & 63u] : '=';
        CUTW_B64[o++] = i + 2u < n ? A[v & 63u] : '=';
    }
    CUTW_B64[o] = 0;
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", path);
    zxm_p_str(&LINK, "base64", CUTW_B64);
    return zxm_call(&LINK) == ZXM_OK;
}

static bool cutw_write_text(const char *path, const char *text)
{
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", path);
    zxm_p_str(&LINK, "text", text);
    return zxm_call(&LINK) == ZXM_OK;
}

/* the text of a file of the Hoard into out, or "" */
static void cutw_read_text(const char *path, char *out, uint32_t cap)
{
    out[0] = 0;
    zxm_begin(&LINK, "files.read");
    zxm_p_str(&LINK, "path", path);
    if (zxm_call(&LINK) != ZXM_OK || zxm_get_str(&LINK, LINK.result, "text", out, cap)) out[0] = 0;
}

static bool cutw_same_list(const cutwater_t *x, const cutwater_t *y)
{
    if (x->nsources != y->nsources || x->nclips != y->nclips || x->fnum != y->fnum ||
        x->fden != y->fden)
        return false;
    for (uint32_t i = 0; i < x->nsources; i++)
        if (strcmp(x->src[i].path, y->src[i].path) || x->src[i].frames != y->src[i].frames ||
            x->src[i].cut_points != y->src[i].cut_points)
            return false;
    for (uint32_t i = 0; i < x->nclips; i++)
        if (memcmp(&x->clip[i], &y->clip[i], sizeof x->clip[i])) return false;
    return true;
}

APPS_TEST(cutwater)
{
    static const uint8_t harbour[10] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0};
    static const uint8_t dock[6] = {1, 0, 0, 1, 0, 0};
    static const uint8_t fast[4] = {1, 0, 1, 0};
    static char text[CUTWATER_TEXT_MAX + 1u];
    char tc[CUTWATER_TC_TEXT];
    int64_t id = 0;
    int before;

    /* CM1: nothing applies before a source */
    cutwater_init(&CUTW, &LINK);
    CHECK(cutwater_clip_add(&CUTW, 1, 1, 0, 5, 0) == ZXM_E_ARG &&
              cutwater_scan(&CUTW, 1) == ZXM_E_ARG &&
              cutwater_save(&CUTW, "Cuts/none.cut", true) == ZXM_E_STATE &&
              cutwater_timecode_text(&CUTW, 0, tc, sizeof tc) == ZXM_E_STATE &&
              cutwater_playhead_frame(&CUTW, 0) == CUTWATER_REFUSED &&
              cutwater_source_add(&CUTW, "", &id) == ZXM_E_ARG &&
              cutwater_source_add(&CUTW, NULL, &id) == ZXM_E_ARG && cutwater_length(&CUTW) == 0,
          "cutwater: before a source, nothing applies");

    zxm_begin(&LINK, "files.mkdir");
    zxm_p_str(&LINK, "path", "Cuts");
    CHECK(zxm_call(&LINK) == ZXM_OK, "cutwater: a Cuts folder: %s", LINK.err);
    CHECK(cutw_write("Cuts/Harbour.ivf", cutw_ivf(30, 1, 10, harbour)) &&
              cutw_write("Cuts/Dock.ivf", cutw_ivf(30, 1, 6, dock)) &&
              cutw_write("Cuts/Fast.ivf", cutw_ivf(25, 1, 4, fast)) &&
              cutw_write_text("Cuts/notes.txt", "not a video"),
          "cutwater: three videos and a note written: %s", LINK.err);

    /* CM1: sources, one rate */
    CHECK(cutwater_source_add(&CUTW, "Cuts/Harbour.ivf", &id) == ZXM_OK && id == 1 &&
              CUTW.nsources == 1 && CUTW.fnum == 30 && CUTW.fden == 1 && CUTW.src[0].frames == 10 &&
              CUTW.src[0].width == 640 && !CUTW.src[0].scanned,
          "cutwater: the first source sets the list's rate (%s)", LINK.err);
    CHECK(cutwater_source_add(&CUTW, "Cuts/Harbour.ivf", &id) == ZXM_E_STATE &&
              strstr(CUTW.notice, "already") != NULL &&
              cutwater_source_add(&CUTW, "Cuts/notes.txt", &id) == ZXM_E_STATE &&
              strstr(CUTW.notice, "Not a video") != NULL &&
              cutwater_source_add(&CUTW, "Cuts/Fast.ivf", &id) == ZXM_E_STATE &&
              strstr(CUTW.notice, "frame rate") != NULL &&
              cutwater_source_add(&CUTW, "Cuts/None.ivf", &id) == ZXM_E_REMOTE &&
              CUTW.nsources == 1,
          "cutwater: a source twice, a note, another rate and a missing file are refused");
    CHECK(cutwater_source_add(&CUTW, "Cuts/Dock.ivf", &id) == ZXM_OK && id == 2 &&
              CUTW.src[1].frames == 6,
          "cutwater: a second source at the same rate (%s)", LINK.err);

    /* CM2: cut points from stream.plan */
    CHECK(cutwater_clip_add(&CUTW, 1, 1, 0, 5, 0) == ZXM_E_STATE &&
              strstr(CUTW.notice, "Scan") != NULL && CUTW.nclips == 0,
          "cutwater: no clip before its source's cut points are known");
    before = sent;
    CHECK(cutwater_scan(&CUTW, 1) == ZXM_OK && sent == before + 12 && CUTW.src[0].scanned &&
              CUTW.src[0].scanned_frames == 10 && CUTW.src[0].cut_points == 2 &&
              cutwater_is_cut_point(&CUTW, 1, 0) && cutwater_is_cut_point(&CUTW, 1, 5) &&
              !cutwater_is_cut_point(&CUTW, 1, 3) && !cutwater_is_cut_point(&CUTW, 1, 10) &&
              !cutwater_is_cut_point(&CUTW, 1, -1),
          "cutwater: the header, ten frames and one plan: cut points 0 and 5 (%s, %d)", LINK.err,
          sent - before);
    CHECK(cutwater_scan(&CUTW, 2) == ZXM_OK && CUTW.src[1].cut_points == 2 &&
              cutwater_is_cut_point(&CUTW, 2, 3) && !cutwater_is_cut_point(&CUTW, 2, 1) &&
              cutwater_scan(&CUTW, 3) == ZXM_E_ARG && cutwater_scan(&CUTW, 0) == ZXM_E_ARG,
          "cutwater: the second source's cut points 0 and 3");

    /* CM3: the edit list, each refusal leaving it as it was */
    CHECK(cutwater_clip_add(&CUTW, 1, 1, 0, 5, 0) == ZXM_OK && CUTW.nclips == 1,
          "cutwater: the first clip");
    CHECK(cutwater_clip_add(&CUTW, 1, 1, 3, 8, 20) == ZXM_E_ARG &&
              strstr(CUTW.notice, "cut point") != NULL &&
              cutwater_clip_add(&CUTW, 1, 1, 5, 5, 20) == ZXM_E_ARG &&
              cutwater_clip_add(&CUTW, 1, 1, 5, 4, 20) == ZXM_E_ARG &&
              cutwater_clip_add(&CUTW, 1, 1, 5, 11, 20) == ZXM_E_ARG &&
              strstr(CUTW.notice, "valid") != NULL &&
              cutwater_clip_add(&CUTW, 1, 1, 5, 10, 4) == ZXM_E_ARG &&
              strstr(CUTW.notice, "overlaps") != NULL &&
              cutwater_clip_add(&CUTW, 0, 1, 0, 5, 0) == ZXM_E_ARG &&
              cutwater_clip_add(&CUTW, 9, 1, 0, 5, 0) == ZXM_E_ARG &&
              cutwater_clip_add(&CUTW, 1, 3, 0, 5, 0) == ZXM_E_ARG &&
              cutwater_clip_add(&CUTW, 1, 1, 0, 5, -1) == ZXM_E_ARG &&
              cutwater_clip_add(&CUTW, 1, 1, 0, 5, CUTWATER_FRAME_MAX - 4) == ZXM_E_ARG &&
              CUTW.nclips == 1,
          "cutwater: off a cut point, in at or after out, past its source, over the clip "
          "before, a bad track or source, and past the timeline's end are refused");
    CHECK(cutwater_clip_add(&CUTW, 1, 1, 5, 10, 5) == ZXM_OK &&
              cutwater_clip_add(&CUTW, 1, 2, 3, 6, CUTWATER_FRAME_MAX - 3) == ZXM_OK &&
              CUTW.nclips == 3,
          "cutwater: a clip flush after the first, and one flush with the timeline's end");
    CHECK(cutwater_clip_remove(&CUTW, 2) == ZXM_OK && CUTW.nclips == 2 &&
              cutwater_clip_remove(&CUTW, 2) == ZXM_E_ARG,
          "cutwater: a clip removed");
    CHECK(cutwater_clip_add(&CUTW, 3, 2, 0, 6, 100) == ZXM_OK &&
              cutwater_clip_add(&CUTW, 3, 1, 0, 5, 96) == ZXM_E_ARG &&
              strstr(CUTW.notice, "next") != NULL &&
              cutwater_clip_add(&CUTW, 3, 1, 0, 5, 95) == ZXM_OK &&
              cutwater_clip_add(&CUTW, 2, 2, 3, 6, 0) == ZXM_OK && CUTW.nclips == 5,
          "cutwater: a clip running into the next is refused, one flush with it is not");
    CHECK(CUTW.clip[0].track == 1 && CUTW.clip[0].place == 0 && CUTW.clip[1].place == 5 &&
              CUTW.clip[2].track == 2 && CUTW.clip[3].track == 3 && CUTW.clip[3].place == 95 &&
              CUTW.clip[4].place == 100 && cutwater_length(&CUTW) == 106,
          "cutwater: kept in order of track then place; the list is 106 frames");

    /* CM4: timecode and the playhead */
    CHECK(cutwater_timecode_text(&CUTW, 0, tc, sizeof tc) == ZXM_OK && !strcmp(tc, "0:00:00:00") &&
              cutwater_timecode_text(&CUTW, 1799, tc, sizeof tc) == ZXM_OK &&
              !strcmp(tc, "0:00:59:29") &&
              cutwater_timecode_text(&CUTW, 108030, tc, sizeof tc) == ZXM_OK &&
              !strcmp(tc, "1:00:01:00") &&
              cutwater_timecode_text(&CUTW, -1, tc, sizeof tc) == ZXM_E_ARG &&
              cutwater_timecode_text(&CUTW, 1799, tc, 4) == ZXM_E_TOOBIG,
          "cutwater: frames as timecode (%s)", tc);
    CHECK(cutwater_playhead_frame(&CUTW, 1000) == 30 && cutwater_playhead_frame(&CUTW, 33) == 0 &&
              cutwater_playhead_frame(&CUTW, 34) == 1 && cutwater_playhead_frame(&CUTW, -1) < 0,
          "cutwater: a playhead in ms is a frame of the list");

    /* CM5: saved only on the owner's word */
    before = sent;
    CHECK(cutwater_save(&CUTW, "Cuts/Evening.cut", false) == ZXM_E_STATE && sent == before &&
              strstr(CUTW.notice, "owner") != NULL,
          "cutwater: without the owner's word nothing is sent");
    zxm_begin(&LINK, "files.stat");
    zxm_p_str(&LINK, "path", "Cuts/Evening.cut");
    CHECK(zxm_call(&LINK) == ZXM_E_REMOTE, "cutwater: and nothing was written");
    CHECK(cutwater_save(&CUTW, "", true) == ZXM_E_ARG &&
              cutwater_save(&CUTW, "Cuts/Evening.cut", true) == ZXM_OK,
          "cutwater: the list saved (%s)", LINK.err);
    cutw_read_text("Cuts/Evening.cut", text, sizeof text);
    CHECK(!strcmp(text, "cutwater 1\nrate 30 1\nsource Cuts/Harbour.ivf\nsource Cuts/Dock.ivf\n"
                        "clip 1 1 0 5 0\nclip 1 1 5 10 5\nclip 2 2 3 6 0\nclip 3 1 0 5 95\n"
                        "clip 3 2 0 6 100\n"),
          "cutwater: the saved text, line by line:\n%s", text);

    /* CM6: opened again, and refusals that leave the open list as it was */
    cutwater_init(&CUTW2, &LINK);
    CHECK(cutwater_open(&CUTW2, "Cuts/Evening.cut") == ZXM_OK && cutw_same_list(&CUTW, &CUTW2) &&
              CUTW2.src[0].scanned && CUTW2.src[1].scanned,
          "cutwater: the saved list opens to the same list (%s, %s)", LINK.err, CUTW2.notice);
    CHECK(
        cutw_write_text("Cuts/Bad1.cut", "cutwater 1\nrate 30 1\nclip 1 1 0 5 0\n") &&
            cutw_write_text("Cuts/Bad2.cut", "cutwater 1\nrate 25 1\nsource Cuts/Harbour.ivf\n") &&
            cutw_write_text("Cuts/Bad3.cut", "cutwater 1\nrate 30 1\nsource Cuts/Harbour.ivf\n"
                                             "clip 1 1 0 5 0\nclip 1 1 0 5 3\n") &&
            cutw_write_text("Cuts/Bad4.cut", "an edit list\n") &&
            cutw_write_text("Cuts/Bad5.cut", "cutwater 1\nrate 30 1\n") &&
            cutw_write_text("Cuts/Bad6.cut", "cutwater 1\nrate 30 1\nsource Cuts/Harbour.ivf\n"
                                             "clip 1 1 0 x 0\n"),
        "cutwater: six lists that do not open written");
    CHECK(cutwater_open(&CUTW2, "Cuts/Bad1.cut") == ZXM_E_ARG &&
              cutwater_open(&CUTW2, "Cuts/Bad2.cut") == ZXM_E_STATE &&
              strstr(CUTW2.notice, "rate") != NULL &&
              cutwater_open(&CUTW2, "Cuts/Bad3.cut") == ZXM_E_ARG &&
              strstr(CUTW2.notice, "overlaps") != NULL &&
              cutwater_open(&CUTW2, "Cuts/Bad4.cut") == ZXM_E_STATE &&
              cutwater_open(&CUTW2, "Cuts/Bad5.cut") == ZXM_E_STATE &&
              cutwater_open(&CUTW2, "Cuts/Bad6.cut") == ZXM_E_ARG &&
              cutwater_open(&CUTW2, "Cuts/Missing.cut") == ZXM_E_REMOTE &&
              cutw_same_list(&CUTW, &CUTW2),
          "cutwater: a clip before its source, another rate, an overlap, no header, no source "
          "and a bad number are refused; the open list is kept");
}
