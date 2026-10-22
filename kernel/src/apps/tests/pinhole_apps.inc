/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Pinhole's model against zxvd's real dispatcher (LINK): photos written
 * with files.write into the test's scratch Hoard and read back, the room
 * from sys.disk and the place tag from place.ask under the owner's answer
 * (zpl_permit, as Waymark sets it). A disk with little room is answered on
 * a link of its own (PH_LINK), which passes every other request to the
 * real dispatcher. zxvd has no capture method, so the frames are small
 * hand-made JPEG byte strings, handed in as the view would. */
#include "pinhole_model.h"

static pinhole_t PH, PH2;
static zxm_link_t PH_LINK;
static const char *PH_DISK; /* sys.disk's result, when set */

static int32_t ph_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    if (PH_DISK && strstr(req, "\"sys.disk\""))
        return snprintf(resp, cap, "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":%s}", l->seq,
                        PH_DISK);
    return inproc(NULL, req, len, resp, cap);
}

/* the file at path holds exactly n bytes of want */
static bool ph_file_is(const char *path, const uint8_t *want, uint32_t n)
{
    static uint8_t buf[PINHOLE_FRAME_MAX];
    uint32_t got = 0, at = 0;
    uint64_t size = 0;
    while (at < PINHOLE_FRAME_MAX) {
        uint32_t cap = PINHOLE_FRAME_MAX - at < 49152u ? PINHOLE_FRAME_MAX - at : 49152u;
        if (zxf_read(path, at, buf + at, cap, false, &got, &size) != 0) return false;
        at += got;
        if (!got || at >= size) break;
    }
    return size == n && at == n && !memcmp(buf, want, n);
}

/* a JPEG-shaped frame of n bytes: SOI, a fill that varies, EOI */
static void ph_frame(uint8_t *f, uint32_t n, uint8_t seed)
{
    f[0] = 0xFF, f[1] = 0xD8;
    for (uint32_t i = 2; i + 2 < n; i++) f[i] = (uint8_t) (seed + i * 7u);
    f[n - 2] = 0xFF, f[n - 1] = 0xD9;
}

APPS_TEST(pinhole)
{
    static uint8_t small[600], big[30001], bad[16];
    ph_frame(small, sizeof small, 3);
    ph_frame(big, sizeof big, 11);
    memcpy(bad, small, sizeof bad); /* no EOI at its end */

    /* the rules (PH1-PH7) */
    CHECK(pinhole_camera_ready(1, 1, 1) && !pinhole_camera_ready(1, 1, 0) &&
              pinhole_camera_ready_n(1, 1, 1) == 1 && pinhole_camera_ready_n(0, 1, 1) == 0 &&
              pinhole_camera_ready_n(1, 1, 2) == PINHOLE_REFUSED &&
              pinhole_exposure_ok(100, 8000, 0) && !pinhole_exposure_ok(100, 8000, 10),
          "pinhole: ready and exposure rules");
    CHECK(pinhole_photo_fits(600, 1600, 1000) && !pinhole_photo_fits(601, 1600, 1000) &&
              pinhole_place_ok(1, 1, 0) && !pinhole_place_ok(1, 0, 0) &&
              pinhole_record_ok(1, 0, 600, 4, 3, 1, 1792627200000) &&
              !pinhole_record_ok(1, 0, 600, 4, 3, 10, 0),
          "pinhole: the used rules and the record");

    /* PM1: off until ready, shown as on whenever on */
    zxm_link_init(&PH_LINK, ph_send, &PH_LINK);
    pinhole_init(&PH, &PH_LINK);
    int before = sent;
    CHECK(
        !PH.on && !PH.indicator[0] && pinhole_take(&PH, small, sizeof small, 4, 3) == ZXM_E_STATE &&
            pinhole_camera_on(&PH, 0, 1) == ZXM_E_STATE && !PH.on &&
            strstr(PH.notice, "No camera sensor") && pinhole_camera_on(&PH, 1, 0) == ZXM_E_STATE &&
            strstr(PH.notice, "mode") && pinhole_camera_on(&PH, 2, 1) == ZXM_E_ARG && !PH.on &&
            !PH.indicator[0] && sent == before,
        "pinhole: no sensor, no mode: the camera stays off, nothing sent");
    CHECK(pinhole_camera_on(&PH, 1, 1) == ZXM_OK && PH.on && !strcmp(PH.indicator, PINHOLE_ON_TEXT),
          "pinhole: on, and shown as on");
    pinhole_camera_off(&PH);
    CHECK(!PH.on && !PH.indicator[0] && pinhole_take(&PH, small, sizeof small, 4, 3) == ZXM_E_STATE,
          "pinhole: off, shown as off, nothing taken");
    CHECK(pinhole_camera_on(&PH, 1, 1) == ZXM_OK && PH.on, "pinhole: on again");

    /* PM3: exposure inside its limits only */
    CHECK(pinhole_set_exposure(&PH, 24, 8000, 0) == ZXM_E_ARG &&
              pinhole_set_exposure(&PH, 100, 30000001, 0) == ZXM_E_ARG &&
              pinhole_set_exposure(&PH, 100, 8000, -10) == ZXM_E_ARG && PH.iso == 100 &&
              PH.shutter == 8000 && PH.comp == 0 &&
              pinhole_set_exposure(&PH, 3200, 31, -9) == ZXM_OK && PH.iso == 3200 &&
              PH.shutter == 31 && PH.comp == -9,
          "pinhole: an exposure past its limits is refused, one inside is kept");

    /* PM2: a frame that is no JPEG, no size or a bad side is refused */
    CHECK(pinhole_take(&PH, bad, sizeof bad, 4, 3) == ZXM_E_ARG &&
              pinhole_take(&PH, small, 3, 4, 3) == ZXM_E_ARG &&
              pinhole_take(&PH, NULL, 600, 4, 3) == ZXM_E_ARG &&
              pinhole_take(&PH, small, PINHOLE_FRAME_MAX + 1u, 4, 3) == ZXM_E_ARG &&
              pinhole_take(&PH, small, sizeof small, 0, 3) == ZXM_E_ARG &&
              pinhole_take(&PH, small, sizeof small, 4, PINHOLE_SIDE_MAX + 1) == ZXM_E_ARG &&
              PH.frame_len == 0 && sent == before,
          "pinhole: bad frames refused, nothing sent");

    /* PM6: nothing saved before the index is read */
    CHECK(pinhole_take(&PH, small, sizeof small, 4, 3) == ZXM_OK && PH.frame_len == sizeof small &&
              pinhole_save(&PH, 1, 1792627200000, false) == ZXM_E_STATE &&
              pinhole_save(&PH, -1, 1, false) == ZXM_E_ARG &&
              pinhole_save(&PH, PINHOLE_ALBUM_MAX + 1, 1, false) == ZXM_E_ARG &&
              pinhole_save(&PH, 1, -1, false) == ZXM_E_ARG && sent == before,
          "pinhole: not loaded, or a bad album or stamp: nothing sent");
    CHECK(pinhole_load(&PH) == ZXM_OK && PH.loaded && PH.n == 0 && !PH.folder,
          "pinhole: no index yet: no photos: %s", PH_LINK.err);

    /* PM4: room first, from sys.disk */
    CHECK(pinhole_room(&PH) == ZXM_OK && PH.total > 0 && PH.avail >= 0 && PH.free >= 0,
          "pinhole: the real disk: %s", PH_LINK.err);
    PH_DISK = "{\"path\":\"/h\",\"total\":100000,\"free\":2000,\"avail\":1599}";
    CHECK(pinhole_set_floor(&PH, -1) == ZXM_E_ARG &&
              pinhole_set_floor(&PH, LIFEBOAT_BYTES_MAX + 1) == ZXM_E_ARG &&
              pinhole_set_floor(&PH, 1000) == ZXM_OK &&
              pinhole_save(&PH, 1, 1792627200000, false) == ZXM_E_TOOBIG && PH.n == 0 &&
              PH.avail == 1599 && strstr(PH.notice, "is 1 bytes more than the room left") &&
              !ph_file_is(PINHOLE_DIR "/a1-p1.jpg", small, sizeof small),
          "pinhole: one byte short with the floor kept: not saved (%s)", PH.notice);
    PH_DISK = "{\"path\":\"/h\",\"total\":100000,\"free\":2000,\"avail\":1600}";
    CHECK(pinhole_save(&PH, 1, 1792627200000, false) == ZXM_OK && PH.n == 1 &&
              PH.photo[0].photo == 1 && PH.photo[0].album == 1 && PH.photo[0].bytes == 600 &&
              PH.photo[0].width == 4 && PH.photo[0].height == 3 && PH.photo[0].placed == 0 &&
              !strcmp(PH.photo[0].path, "Pinhole/a1-p1.jpg") && PH.frame_len == 0 && PH.folder,
          "pinhole: exactly fits: saved: %s", PH_LINK.err);
    CHECK(ph_file_is("Pinhole/a1-p1.jpg", small, sizeof small),
          "pinhole: the photo in the Hoard, byte for byte");
    CHECK(pinhole_save(&PH, 1, 1792627200000, false) == ZXM_E_STATE,
          "pinhole: the frame is saved once");
    PH_DISK = NULL;
    CHECK(pinhole_set_floor(&PH, 0) == ZXM_OK, "pinhole: no floor");

    /* PM5: a place tag only with the owner's permit */
    CHECK(zpl_open(NULL) == 0, "pinhole: places in memory");
    CHECK(pinhole_take(&PH, big, sizeof big, 640, 480) == ZXM_OK &&
              pinhole_save(&PH, 2, 1792627300000, true) == ZXM_OK && PH.n == 2 &&
              !strcmp(PH.answer, "pending") && PH.photo[1].placed == 0 && PH.photo[1].lat == 0 &&
              strstr(PH.notice, "not answered"),
          "pinhole: no answer yet: saved without a place (%s) %s", PH.notice, PH_LINK.err);
    CHECK(ph_file_is("Pinhole/a2-p2.jpg", big, sizeof big),
          "pinhole: a frame in several parts, byte for byte");
    CHECK(zpl_permit(PINHOLE_APP, ZPL_NEVER) == 0 &&
              pinhole_take(&PH, small, sizeof small, 4, 3) == ZXM_OK &&
              pinhole_save(&PH, 2, 1792627400000, true) == ZXM_OK && !strcmp(PH.answer, "never") &&
              PH.photo[2].placed == 0 && strstr(PH.notice, "never"),
          "pinhole: the owner said never: no place (%s)", PH.notice);
    zpl_forget_here();
    CHECK(zpl_permit(PINHOLE_APP, ZPL_ALWAYS) == 0 &&
              pinhole_take(&PH, small, sizeof small, 4, 3) == ZXM_OK &&
              pinhole_save(&PH, 1, 1792627500000, true) == ZXM_OK &&
              !strcmp(PH.answer, "unknown") && PH.photo[3].placed == 0 &&
              strstr(PH.notice, "no location"),
          "pinhole: allowed but no location set: no place (%s)", PH.notice);
    CHECK(zpl_set_here((zpl_point_t){59913900, 10752200}, 50, "owner") == 0 &&
              pinhole_take(&PH, small, sizeof small, 4, 3) == ZXM_OK &&
              pinhole_save(&PH, 1, 1792627600000, true) == ZXM_OK &&
              !strcmp(PH.answer, "allowed") && PH.photo[4].placed == 1 &&
              PH.photo[4].lat == 59913900 && PH.photo[4].lon == 10752200 && !PH.notice[0],
          "pinhole: allowed: the place is added: %s", PH_LINK.err);
    CHECK(pinhole_take(&PH, small, sizeof small, 4, 3) == ZXM_OK &&
              pinhole_save(&PH, 3, 1792627700000, false) == ZXM_OK && PH.photo[5].placed == 0 &&
              !strcmp(PH.answer, "allowed"),
          "pinhole: no place asked for: none added, even when allowed");

    /* PH5: bytes per album over the index */
    int64_t t = -1;
    CHECK(pinhole_album_bytes(&PH, 1, &t) == 0 && t == 600 * 3 &&
              pinhole_album_bytes(&PH, 2, &t) == 0 && t == (int64_t) sizeof big + 600 &&
              pinhole_album_bytes(&PH, 3, &t) == 0 && t == 600 &&
              pinhole_album_bytes(&PH, 9, &t) == 0 && t == 0 &&
              pinhole_album_bytes(&PH, 1, NULL) == PINHOLE_REFUSED,
          "pinhole: bytes per album");

    /* PM6: the index, read back by another model */
    pinhole_init(&PH2, &LINK);
    CHECK(pinhole_load(&PH2) == ZXM_OK && PH2.n == 6 && PH2.last == 6 && PH2.folder &&
              PH2.photo[4].placed == 1 && PH2.photo[4].lat == 59913900 &&
              PH2.photo[4].lon == 10752200 && PH2.photo[4].stamp == 1792627600000 &&
              PH2.photo[1].bytes == (int64_t) sizeof big && PH2.photo[1].width == 640 &&
              !strcmp(PH2.photo[5].path, "Pinhole/a3-p6.jpg"),
          "pinhole: the index read back: %d photos %s", (int) PH2.n, LINK.err);
    CHECK(pinhole_camera_on(&PH2, 1, 1) == ZXM_OK &&
              pinhole_take(&PH2, small, sizeof small, 4, 3) == ZXM_OK &&
              pinhole_save(&PH2, 1, 1792627800000, false) == ZXM_OK && PH2.n == 7 &&
              PH2.photo[6].photo == 7 && ph_file_is("Pinhole/a1-p7.jpg", small, sizeof small),
          "pinhole: the folder kept, the numbering goes on: %s", LINK.err);

    /* an index that is not one is refused, and the photos kept stay */
    uint64_t size = 0;
    CHECK(zxf_write(PINHOLE_INDEX, (const uint8_t *) "1 0 600 4 3 2 0 0 1\n", 20, ZXF_W_REPLACE,
                    false, &size) == 0 &&
              pinhole_load(&PH2) == ZXM_E_PROTOCOL && PH2.n == 7,
          "pinhole: a placed of 2 is no record");
    CHECK(zxf_write(PINHOLE_INDEX, (const uint8_t *) "2 0 600 4 3 0 0 0 1\n1 0 600 4 3 0 0 0 1\n",
                    40, ZXF_W_REPLACE, false, &size) == 0 &&
              pinhole_load(&PH2) == ZXM_E_PROTOCOL,
          "pinhole: photos out of order are no index");
    pinhole_camera_off(&PH);
    pinhole_camera_off(&PH2);
    CHECK(!PH.on && !PH2.on, "pinhole: both off");
}
