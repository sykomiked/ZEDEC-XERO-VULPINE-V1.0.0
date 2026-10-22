/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Tintype's model against zxvd's real dispatcher (LINK): albums filed in a
 * tree, a photo and a screen grab taken as the view would hand them (small
 * hand-made JPEG and PNG byte strings: zxvd captures and decodes nothing),
 * kept in the test's scratch Hoard under the first number free in their
 * album (files.list, files.write, files.stat) within the room sys.disk
 * reports and the album's declared bytes, read back and checked against
 * their records (files.read), and the catalogue saved and loaded. The BOTH
 * test: a photo the camera reported as 4000 x 3000 that decodes to 3990 x
 * 3000 is BOTH, flagged possibly altered, and so is a kept photo whose file
 * is later changed. A disk with little room is answered on a link of its
 * own (TT_LINK), which passes every other request to the real dispatcher. */
#include "tintype_model.h"

static tintype_t TT, TT2;
static zxm_link_t TT_LINK;
static const char *TT_DISK; /* sys.disk's result, when set */

static int32_t tt_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    if (TT_DISK && strstr(req, "\"sys.disk\""))
        return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq,
                        TT_DISK);
    return inproc(NULL, req, len, resp, cap);
}

/* A JPEG-shaped picture of n bytes (n >= 80): SOI, an EXIF segment with the
 * camera's time when `when` is given, a baseline SOF of w by h, a scan of
 * filler and EOI. */
static uint32_t tt_jpeg(uint8_t *p, uint32_t n, uint32_t w, uint32_t h, const char *when,
                        uint8_t seed)
{
    static const uint8_t sos[10] = {0xFFu, 0xDAu, 0, 8, 1, 1, 0, 0, 0x3Fu, 0};
    uint32_t at = 0;
    p[at++] = 0xFFu, p[at++] = 0xD8u;
    if (when) {
        p[at++] = 0xFFu, p[at++] = 0xE1u, p[at++] = 0, p[at++] = 32;
        memcpy(p + at, "Exif\0\0MM\0*", 10);
        at += 10;
        memcpy(p + at, when, 19);
        at += 19;
        p[at++] = 0;
    }
    static const uint8_t sof[4] = {0xFFu, 0xC0u, 0, 17};
    memcpy(p + at, sof, 4);
    at += 4;
    p[at++] = 8;
    p[at++] = (uint8_t) (h >> 8), p[at++] = (uint8_t) h;
    p[at++] = (uint8_t) (w >> 8), p[at++] = (uint8_t) w;
    p[at++] = 3;
    for (uint32_t k = 0; k < 9u; k++) p[at++] = (uint8_t) (k + 1u);
    memcpy(p + at, sos, sizeof sos);
    at += sizeof sos;
    for (; at + 2u < n; at++) p[at] = (uint8_t) ((seed + at * 7u) % 250u); /* never 0xFF */
    p[at++] = 0xFFu, p[at++] = 0xD9u;
    return at;
}

/* a PNG-shaped picture of n bytes: signature, IHDR of w by h, a fill, IEND */
static void tt_png(uint8_t *p, uint32_t n, uint32_t w, uint32_t h, uint8_t seed)
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
static bool tt_file_is(const char *path, const uint8_t *want, uint32_t n)
{
    static uint8_t buf[8192];
    uint32_t got = 0;
    uint64_t size = 0;
    if (n > sizeof buf || zxf_read(path, 0, buf, sizeof buf, false, &got, &size) != 0) return false;
    return size == n && got == n && !memcmp(buf, want, n);
}

APPS_TEST(tintype)
{
    static uint8_t ph1[600], ph2[600], odd[600], png[500], big[1500];
    static const char *const WHEN = "2026:10:22 12:00:00"; /* 1792670400 s since 1970 */
    const int64_t at_when = INT64_C(1792670400);
    char path[TINTYPE_PATH_MAX];
    int64_t id = 0;
    tt_jpeg(ph1, sizeof ph1, 4000, 3000, WHEN, 3);
    tt_jpeg(ph2, sizeof ph2, 4000, 3000, NULL, 9);
    tt_jpeg(odd, sizeof odd, 3990, 3000, WHEN, 5);
    tt_jpeg(big, sizeof big, 4000, 3000, WHEN, 1);
    tt_png(png, sizeof png, 800, 600, 7);

    /* the rules the model runs */
    CHECK(tintype_ev_thirds(160, 8000, 100) == 45 &&
              tintype_ev_thirds(28, 16666, 400) == 9 + 18 - 6 &&
              tintype_rights_ok(1, 7, TINTYPE_LICENCE_BY, 1) &&
              !tintype_rights_ok(1, 7, TINTYPE_LICENCE_MAX + 1, 1) &&
              tintype_album_room(1000, 400, 600) == 0 && tintype_storage_ok(0, 1000) &&
              !tintype_storage_ok(-1, 1000) && tintype_decode_match(0, 0, 1, 1) &&
              !tintype_decode_match(10, 0, 0, 1),
          "tintype: the exposure value, the rights row, the album's limit, the decode match");

    /* TM4: develop and its histogram, no file touched */
    {
        static const int64_t lift[TINTYPE_KNOTS] = {0, 80, 140, 200, 255},
                             bad[TINTYPE_KNOTS] = {0, 80, 140, 200, 256};
        uint8_t in[256], out[256];
        int64_t bins[TINTYPE_BINS_MAX], sum = 0;
        uint32_t nb = 0;
        for (uint32_t i = 0; i < 256u; i++) in[i] = (uint8_t) i;
        CHECK(tintype_develop(in, 256, lift, 0, 10000, out) == ZXM_OK && out[0] == 0 &&
                  out[64] == 80 && out[128] == 140 && out[255] == 255 && out[32] == 40,
              "tintype: the curve alone lifts the shadows: %u %u %u", out[32], out[64], out[128]);
        CHECK(tintype_develop(in, 256, lift, 20, 20000, out) == ZXM_OK && out[0] == 0 &&
                  out[255] == 255 && out[64] == 72 && out[128] == 192,
              "tintype: then gesso's brightness and contrast: %u %u", out[64], out[128]);
        CHECK(tintype_develop(in, 256, bad, 0, 10000, out) == ZXM_E_ARG &&
                  tintype_develop(in, 256, lift, 256, 10000, out) == ZXM_E_ARG &&
                  tintype_develop(in, 256, lift, 0, GESSO_GAIN_MAX + 1, out) == ZXM_E_ARG,
              "tintype: a knot, a delta or a gain outside its domain is refused");
        CHECK(tintype_histogram(in, 256, 32, bins, &nb) == ZXM_OK && nb == 8u && bins[0] == 32 &&
                  bins[7] == 32 && tintype_histogram(in, 256, 0, bins, &nb) == ZXM_E_ARG &&
                  tintype_histogram(in, 256, 100, bins, &nb) == ZXM_OK && nb == 3u && bins[2] == 56,
              "tintype: the histogram in bins of 32 and of 100 levels");
        for (uint32_t b = 0; b < nb; b++) sum += bins[b];
        CHECK(sum == 256, "tintype: every value in one bin");
    }

    /* TM1, TM2, TM3: refused before anything is sent */
    zxm_link_init(&TT_LINK, tt_send, &TT_LINK);
    tintype_init(&TT, &TT_LINK);
    int before = sent;
    CHECK(tintype_album_add(&TT, 0, 0, 1000) == ZXM_E_ARG &&
              tintype_album_add(&TT, 1, 1, 1000) == ZXM_E_ARG &&
              tintype_album_add(&TT, 1, 0, 0) == ZXM_E_ARG &&
              tintype_album_add(&TT, 2, 1, 1000) == ZXM_E_STATE &&
              tintype_take_photo(&TT, 0, 1, ph1, sizeof ph1, 4000, 3000, 160, 8000, 100, at_when) ==
                  ZXM_E_STATE &&
              tintype_take_photo(&TT, 1, 1, ph1, TINTYPE_SHOT_MAX + 1u, 4000, 3000, 160, 8000, 100,
                                 at_when) == ZXM_E_STATE &&
              tintype_take_photo(&TT, 1, 1, ph1, sizeof ph1, 4000, 3000, 9, 8000, 100, at_when) ==
                  ZXM_E_ARG &&
              tintype_take_photo(&TT, 1, 1, ph1, sizeof ph1, 4000, 3000, 160, 8000, 24, at_when) ==
                  ZXM_E_ARG &&
              tintype_take_photo(&TT, 1, 1, png, sizeof png, 4000, 3000, 160, 8000, 100, at_when) ==
                  ZXM_E_ARG &&
              tintype_take_screen(&TT, 1920, 1080, 1920, 0, 10, 10, png, sizeof png, at_when) ==
                  ZXM_E_ARG &&
              tintype_take_screen(&TT, 0, 1080, 0, 0, 10, 10, png, sizeof png, at_when) ==
                  ZXM_E_ARG &&
              tintype_keep(&TT, 1, 7, 2, 1, true, &id) == ZXM_E_STATE && TT.nalbums == 0 &&
              TT.shot_len == 0 && sent == before,
          "tintype: refused before anything is sent: %s", TT.notice);

    /* TM1: the album tree */
    CHECK(tintype_album_add(&TT, 1, 0, 100000) == ZXM_OK &&
              tintype_album_add(&TT, 2, 1, 1000) == ZXM_OK &&
              tintype_album_add(&TT, 2, 0, 1000) == ZXM_E_STATE &&
              tintype_album_add(&TT, 3, 9, 1000) == ZXM_E_STATE && TT.nalbums == 2 &&
              tintype_album(&TT, 2)->parent == 1 && sent == before,
          "tintype: albums 1 and 2 under it; one under no album is refused");

    /* TM2: a photo as reported, decoded the same: TRUE */
    CHECK(tintype_take_photo(&TT, 1, 1, ph1, sizeof ph1, 4000, 3000, 160, 8000, 100, at_when) ==
                  ZXM_OK &&
              TT.shot_len == sizeof ph1 && TT.shot_ev == 45 && TT.shot_tknown &&
              TT.shot_said == LPRES_STATE_TRUE && TT.notice[0] == 0,
          "tintype: f/16 1/125 s ISO 100, as reported: EV 15, TRUE");

    /* TM6: without the owner's confirmation nothing is sent and nothing changes */
    CHECK(tintype_keep(&TT, 2, 7, TINTYPE_LICENCE_BY, 1, false, &id) == ZXM_E_STATE &&
              sent == before && TT.nphotos == 0 && TT.shot_len == sizeof ph1 &&
              strstr(TT.notice, "confirmed") && tintype_album(&TT, 2)->used == 0,
          "tintype: not confirmed: nothing sent, nothing kept");
    CHECK(tintype_keep(&TT, 2, 0, TINTYPE_LICENCE_BY, 1, true, &id) == ZXM_E_ARG &&
              tintype_keep(&TT, 2, 7, 8, 1, true, &id) == ZXM_E_ARG &&
              tintype_keep(&TT, 2, 7, 2, 2, true, &id) == ZXM_E_ARG &&
              tintype_keep(&TT, 5, 7, 2, 1, true, &id) == ZXM_E_STATE && sent == before,
          "tintype: a rights row that does not fit, or no such album: refused");

    /* TM5: room first, from sys.disk, the owner's floor kept */
    TT_DISK = "{\"path\":\"/h\",\"total\":100000,\"free\":2000,\"avail\":1599}";
    CHECK(tintype_set_floor(&TT, -1) == ZXM_E_ARG && tintype_set_floor(&TT, 1000) == ZXM_OK &&
              tintype_keep(&TT, 2, 7, TINTYPE_LICENCE_BY, 1, true, &id) == ZXM_E_TOOBIG &&
              TT.nphotos == 0 && strstr(TT.notice, "is 1 bytes more than the room left"),
          "tintype: one byte short with the floor kept: not kept (%s)", TT.notice);
    TT_DISK = "{\"path\":\"/h\",\"total\":100000,\"free\":2000,\"avail\":1600}";
    CHECK(tintype_keep(&TT, 2, 7, TINTYPE_LICENCE_BY, 1, true, &id) == ZXM_OK && id == 1 &&
              TT.nphotos == 1 && TT.shot_len == 0 && tintype_album(&TT, 2)->used == 600 &&
              tintype_album(&TT, 2)->photos == 1 &&
              tintype_path(tintype_find(&TT, 1), path, sizeof path) &&
              !strcmp(path, "Tintype/Album 2/Photo 1.jpg"),
          "tintype: exactly fits: %s (%s)", path, TT_LINK.err);
    TT_DISK = NULL;
    CHECK(tintype_set_floor(&TT, 0) == ZXM_OK &&
              tt_file_is("Tintype/Album 2/Photo 1.jpg", ph1, sizeof ph1),
          "tintype: the photo in the Hoard, byte for byte");
    {
        tintype_photo_t *p = tintype_find(&TT, 1);
        char hex[TINTYPE_CID_HEX + 1];
        uint8_t md[SHA256_DIGEST_LEN];
        sha256(ph1, sizeof ph1, md);
        for (uint32_t i = 0; i < SHA256_DIGEST_LEN; i++) snprintf(hex + 2 * i, 3, "%02x", md[i]);
        CHECK(p && p->width == 4000 && p->height == 3000 && p->stamp == at_when && p->tknown &&
                  p->ev == 45 && p->owner == 7 && p->licence == TINTYPE_LICENCE_BY &&
                  p->release == 1 && !strcmp(p->cid, hex) && p->said == LPRES_STATE_TRUE,
              "tintype: its record, rights row and content id");
    }

    /* TR4: the album's declared bytes: 600 of 1000 held, 600 more is past */
    before = sent;
    CHECK(tintype_take_photo(&TT, 1, 1, ph2, sizeof ph2, 4000, 3000, 28, 16666, 400, at_when) ==
                  ZXM_OK &&
              !TT.shot_tknown && TT.shot_ev == 21 &&
              tintype_keep(&TT, 2, 7, TINTYPE_LICENCE_RESERVED, 0, true, &id) == ZXM_E_TOOBIG &&
              strstr(TT.notice, "200 bytes past") && sent == before && TT.nphotos == 1,
          "tintype: the album would pass its declared bytes: nothing sent (%s)", TT.notice);

    /* TM6, GR4: the first free number in an album, a gap filled */
    CHECK(zxf_mkdir("Tintype/Album 1") == 0 &&
              zxf_write("Tintype/Album 1/Photo 1.jpg", ph2, sizeof ph2, ZXF_W_REPLACE, false,
                        &(uint64_t){0}) == 0 &&
              zxf_write("Tintype/Album 1/Photo 3.png", png, sizeof png, ZXF_W_REPLACE, false,
                        &(uint64_t){0}) == 0 &&
              zxf_write("Tintype/Album 1/Photo 02.jpg", ph2, sizeof ph2, ZXF_W_REPLACE, false,
                        &(uint64_t){0}) == 0,
          "tintype: album 1 holds photos 1 and 3, and a name that is no number");
    CHECK(tintype_keep(&TT, 1, 7, TINTYPE_LICENCE_RESERVED, 0, true, &id) == ZXM_OK && id == 2 &&
              tintype_path(tintype_find(&TT, 2), path, sizeof path) &&
              !strcmp(path, "Tintype/Album 1/Photo 2.jpg") && tt_file_is(path, ph2, sizeof ph2),
          "tintype: kept as the first free number: %s (%s)", path, TT_LINK.err);

    /* TM3: a screen grab, its region clamped to the screen */
    CHECK(tintype_take_screen(&TT, 1920, 1080, 1120, 480, 900, 650, png, sizeof png, at_when) ==
                  ZXM_OK &&
              TT.shot_width == 800 && TT.shot_height == 600 && TT.shot_said == LPRES_STATE_TRUE &&
              !TT.shot_tknown && TT.shot_ev == TINTYPE_EV_REFUSED &&
              tintype_keep(&TT, 1, 7, TINTYPE_LICENCE_PUBLIC, 0, true, &id) == ZXM_OK && id == 3 &&
              tintype_path(tintype_find(&TT, 3), path, sizeof path) &&
              !strcmp(path, "Tintype/Album 1/Photo 4.png"),
          "tintype: a grab past the bottom right, 800 x 600, kept as %s", path);

    /* TM5: a recording's size against the room */
    {
        int64_t bytes = 0;
        TT_DISK = "{\"path\":\"/h\",\"total\":9000000000,\"free\":3000000000,\"avail\":2488320000}";
        CHECK(tintype_recording_fits(&TT, 1920, 1080, 4, 10, 30, &bytes) == ZXM_OK &&
                  bytes == 2488320000 &&
                  tintype_recording_fits(&TT, 1920, 1080, 4, 11, 30, &bytes) == ZXM_E_TOOBIG &&
                  strstr(TT.notice, "248832000 bytes more") &&
                  tintype_recording_fits(&TT, 1920, 1080, 4, 10, 61, &bytes) == ZXM_E_ARG,
              "tintype: ten seconds of 1080p fit exactly, eleven do not (%s)", TT.notice);
        TT_DISK = NULL;
    }

    /* TM7: read back as kept: TRUE */
    CHECK(tintype_verify(&TT, 1) == ZXM_OK && tintype_find(&TT, 1)->said == LPRES_STATE_TRUE &&
              tintype_verify(&TT, 3) == ZXM_OK && tintype_find(&TT, 3)->said == LPRES_STATE_TRUE &&
              tintype_verify(&TT, 99) == ZXM_E_STATE,
          "tintype: read back, each decodes to its record: %s", TT_LINK.err);

    /* TR2, the BOTH test: the camera reported 4000 x 3000, the frame decodes to 3990 x 3000 */
    CHECK(tintype_take_photo(&TT, 1, 1, odd, sizeof odd, 4000, 3000, 160, 8000, 100, at_when) ==
                  ZXM_OK &&
              TT.shot_said == LPRES_STATE_BOTH && (int) TT.shot_said == 3 &&
              strstr(TT.notice, "Possibly altered"),
          "tintype: 4000 x 3000 reported, 3990 x 3000 decoded: BOTH (%s)", TT.notice);
    CHECK(tintype_keep(&TT, 1, 7, TINTYPE_LICENCE_RESERVED, 0, true, &id) == ZXM_OK && id == 4 &&
              tintype_find(&TT, 4)->said == LPRES_STATE_BOTH &&
              strstr(TT.notice, "possibly altered"),
          "tintype: kept with both claims, flagged: %s", TT.notice);

    /* TM7: a kept photo whose file was changed since: BOTH */
    CHECK(zxf_write("Tintype/Album 2/Photo 1.jpg", odd, sizeof odd, ZXF_W_REPLACE, false,
                    &(uint64_t){0}) == 0 &&
              tintype_verify(&TT, 1) == ZXM_OK && tintype_find(&TT, 1)->said == LPRES_STATE_BOTH &&
              strstr(TT.notice, "Possibly altered: photo 1"),
          "tintype: photo 1 now decodes to 3990 x 3000: BOTH (%s)", TT.notice);
    CHECK(zxf_write("Tintype/Album 2/Photo 1.jpg", big, sizeof big, ZXF_W_REPLACE, false,
                    &(uint64_t){0}) == 0 &&
              tintype_verify(&TT, 1) == ZXM_OK && tintype_find(&TT, 1)->said == LPRES_STATE_BOTH,
          "tintype: the same size and time, other content: a new content id, BOTH");
    CHECK(zxf_write("Tintype/Album 2/Photo 1.jpg", ph1, sizeof ph1, ZXF_W_REPLACE, false,
                    &(uint64_t){0}) == 0 &&
              tintype_verify(&TT, 1) == ZXM_OK && tintype_find(&TT, 1)->said == LPRES_STATE_TRUE,
          "tintype: put back as kept: TRUE again");

    /* TM8: the catalogue, saved only on the owner's word and read back whole */
    before = sent;
    CHECK(tintype_save(&TT, false) == ZXM_E_STATE && sent == before && TT.dirty,
          "tintype: not saved without the owner's word, nothing sent");
    CHECK(tintype_save(&TT, true) == ZXM_OK && !TT.dirty, "tintype: saved: %s", TT_LINK.err);
    tintype_init(&TT2, &TT_LINK);
    CHECK(tintype_load(&TT2) == ZXM_OK && TT2.nalbums == 2 && TT2.nphotos == 4 && TT2.last == 4 &&
              tintype_album(&TT2, 1)->used == TT.album[0].used &&
              tintype_find(&TT2, 4)->said == LPRES_STATE_BOTH &&
              !strcmp(tintype_find(&TT2, 1)->cid, tintype_find(&TT, 1)->cid) &&
              tintype_find(&TT2, 3)->kind == TINTYPE_KIND_SCREEN &&
              tintype_find(&TT2, 1)->licence == TINTYPE_LICENCE_BY,
          "tintype: the catalogue read back: %s", TT_LINK.err);
    CHECK(tintype_verify(&TT2, 1) == ZXM_OK && tintype_find(&TT2, 1)->said == LPRES_STATE_TRUE,
          "tintype: a photo checked from the catalogue read back");
    {
        static const char junk[] = "tintype 1\nA 1 0 1000 2000 1\n"; /* more held than declared */
        CHECK(zxf_write(TINTYPE_CATALOGUE, (const uint8_t *) junk, sizeof junk - 1u, ZXF_W_REPLACE,
                        false, &(uint64_t){0}) == 0 &&
                  tintype_load(&TT2) == ZXM_E_PROTOCOL && TT2.nalbums == 2 && TT2.nphotos == 4,
              "tintype: a catalogue past its limits leaves what is open as it was");
    }
}
