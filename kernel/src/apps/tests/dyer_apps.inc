/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Dyer's model against zxvd's real dispatcher (LINK): colours by number and
 * hex text, contrast against the pass marks, and palettes kept in the
 * Hoard (files.list, files.read, files.mkdir, files.write), written only on
 * the owner's word. */
#include "dyer_model.h"

static dyer_t DY, DY2;

static bool dy_file_is(const char *path, const char *want)
{
    static uint8_t buf[DYER_TEXT_MAX];
    uint32_t got = 0;
    uint64_t size = 0;
    if (zxf_read(path, 0, buf, sizeof buf - 1u, true, &got, &size) != 0) return false;
    buf[got] = 0;
    return size == got && !strcmp((const char *) buf, want);
}

static bool dy_absent(const char *path)
{
    zxf_entry_t e;
    return zxf_stat(path, &e) != 0;
}

APPS_TEST(dyer)
{
    /* colours, hex text (DM1) */
    int64_t col = -1, r = -1, g = -1, b = -1;
    char hex[DYER_HEX_LEN + 1];
    CHECK(dyer_hex_parse("#1a2B3c", &col) == ZXM_OK && col == 0x1a2b3c &&
              dyer_hex_parse("ffffff", &col) == ZXM_OK && col == DYER_COLOR_MAX &&
              dyer_hex_parse("#0f8", &col) == ZXM_OK && col == 0x00ff88 &&
              dyer_hex_parse("#000", &col) == ZXM_OK && col == 0,
          "hex: six digits, three, either case, the '#' optional");
    col = 7;
    CHECK(dyer_hex_parse("#12345", &col) == ZXM_E_ARG &&
              dyer_hex_parse("#1234567", &col) == ZXM_E_ARG &&
              dyer_hex_parse("#12345g", &col) == ZXM_E_ARG &&
              dyer_hex_parse("", &col) == ZXM_E_ARG && dyer_hex_parse("##123", &col) == ZXM_E_ARG &&
              dyer_hex_parse(NULL, &col) == ZXM_E_ARG && col == 7,
          "hex: anything else refused, the colour untouched");
    CHECK(dyer_hex_text(0x1a2b3c, hex) == ZXM_OK && !strcmp(hex, "#1a2b3c") &&
              dyer_hex_text(0, hex) == ZXM_OK && !strcmp(hex, "#000000") &&
              dyer_hex_text(DYER_COLOR_MAX, hex) == ZXM_OK && !strcmp(hex, "#ffffff") &&
              dyer_hex_text(DYER_COLOR_MAX + 1, hex) == ZXM_E_ARG &&
              dyer_hex_text(-1, hex) == ZXM_E_ARG,
          "hex text: lower case, refused outside 0..0xFFFFFF");
    CHECK(dyer_join(255, 0, 128) == 0xff0080 && dyer_join(256, 0, 0) == DYER_REFUSED &&
              dyer_join(0, -1, 0) == DYER_REFUSED && dyer_split(0xff0080, &r, &g, &b) == ZXM_OK &&
              r == 255 && g == 0 && b == 128 &&
              dyer_split(DYER_COLOR_MAX + 1, &r, &g, &b) == ZXM_E_ARG,
          "join and split");

    /* HSL both ways (DY1, DY2) */
    dyer_hsl_t hsl = {0, 0, 0};
    CHECK(dyer_to_hsl(0xff0000, &hsl) == ZXM_OK && hsl.h == 0 && hsl.s == 1000 && hsl.l == 500,
          "red: 0, 1000, 500");
    CHECK(dyer_to_hsl(0x0a141e, &hsl) == ZXM_OK && hsl.h == 210 && hsl.s == 500 && hsl.l == 78,
          "#0a141e: %lld %lld %lld", (long long) hsl.h, (long long) hsl.s, (long long) hsl.l);
    CHECK(dyer_to_hsl(0x808080, &hsl) == ZXM_OK && hsl.h == 0 && hsl.s == 0 && hsl.l == 502 &&
              dyer_to_hsl(-1, &hsl) == ZXM_E_ARG,
          "a grey has no hue and no saturation");
    dyer_hsl_t blue = {240, 1000, 500}, bad = {360, 0, 0};
    CHECK(dyer_from_hsl(&blue, &col) == ZXM_OK && col == 0x0000ff &&
              dyer_from_hsl(&bad, &col) == ZXM_E_ARG,
          "HSL to a colour, a hue of 360 refused");
    /* there and back: within 2 a channel (a hue in whole degrees) */
    bool near = true;
    for (int64_t c = 0; c <= DYER_COLOR_MAX && near; c += 99991) {
        int64_t back = -1, r1, g1, b1, r2, g2, b2;
        near = dyer_to_hsl(c, &hsl) == ZXM_OK && dyer_from_hsl(&hsl, &back) == ZXM_OK &&
               dyer_split(c, &r1, &g1, &b1) == ZXM_OK &&
               dyer_split(back, &r2, &g2, &b2) == ZXM_OK && r1 - r2 <= 2 && r2 - r1 <= 2 &&
               g1 - g2 <= 2 && g2 - g1 <= 2 && b1 - b2 <= 2 && b2 - b1 <= 2;
    }
    CHECK(near, "there and back again, within 2");

    /* contrast and its marks (DM6) */
    dyer_contrast_t ct;
    CHECK(dyer_contrast(0x000000, 0xffffff, &ct) == ZXM_OK && ct.fore_lum == 0 &&
              ct.back_lum == DYER_LUM_MAX && ct.ratio == 2100 && ct.grade == 2,
          "black on white: 21 to 1, body text");
    CHECK(dyer_contrast(0x767676, 0xffffff, &ct) == ZXM_OK && ct.ratio == 454 && ct.grade == 2,
          "#767676 on white: 454, body text");
    CHECK(dyer_contrast(0x777777, 0xffffff, &ct) == ZXM_OK && ct.ratio == 447 && ct.grade == 1,
          "#777777 on white: 447, large text only");
    CHECK(dyer_contrast(0x959595, 0xffffff, &ct) == ZXM_OK && ct.ratio == 299 && ct.grade == 0 &&
              dyer_contrast(0x949494, 0xffffff, &ct) == ZXM_OK && ct.ratio == 303 && ct.grade == 1,
          "the 300 mark: %lld", (long long) ct.ratio);
    CHECK(dyer_contrast(0x123456, 0x123456, &ct) == ZXM_OK && ct.ratio == 100 && ct.grade == 0 &&
              dyer_contrast(0, DYER_COLOR_MAX + 1, &ct) == ZXM_E_ARG,
          "one colour on itself: 100");
    CHECK(dyer_name_ok("warm") && dyer_name_ok("Sea_glass-2") && !dyer_name_ok("") &&
              !dyer_name_ok("a/b") && !dyer_name_ok("..") && !dyer_name_ok("two words") &&
              !dyer_name_ok("abcdefghijklmnopqrstuvwxyz0123456") &&
              dyer_name_ok("abcdefghijklmnopqrstuvwxyz012345"),
          "palette names");

    /* nothing changes before a palette is open (DM3) */
    CHECK(zxf_use_space(NULL) == 0, "home");
    dyer_init(&DY, &LINK);
    int before = sent;
    CHECK(dyer_add(&DY, 0xff0000) == ZXM_E_STATE && dyer_remove(&DY, 0) == ZXM_E_STATE &&
              dyer_save(&DY, true) == ZXM_E_STATE && sent == before,
          "not open: nothing changes, nothing is sent");
    CHECK(dyer_open(&DY, "no/such") == ZXM_E_ARG && dyer_add(&DY, -1) == ZXM_E_ARG &&
              sent == before,
          "bad arguments are refused before anything is asked");

    /* a palette not made yet is empty; no folder yet, no palettes (DM5) */
    CHECK(dyer_list(&DY) == ZXM_OK && DY.nnames == 0, "no palettes yet: %s", LINK.err);
    CHECK(dyer_open(&DY, "warm") == ZXM_OK && DY.loaded && DY.n == 0 && !strcmp(DY.name, "warm"),
          "a new palette: %s", LINK.err);
    CHECK(dyer_add(&DY, 0xff0000) == ZXM_OK && dyer_add(&DY, 0xff8800) == ZXM_OK &&
              dyer_add(&DY, 0x112233) == ZXM_OK && dyer_add(&DY, DYER_COLOR_MAX + 1) == ZXM_E_ARG &&
              DY.n == 3 && DY.dirty,
          "three swatches");

    /* DM4: written only on the owner's word */
    before = sent;
    CHECK(dyer_save(&DY, false) == ZXM_E_STATE && sent == before && DY.dirty &&
              dy_absent("Palettes/warm.palette") && dy_absent("Palettes"),
          "no word from the owner: nothing sent, nothing written");
    CHECK(dyer_save(&DY, true) == ZXM_OK && !DY.dirty, "saved: %s", LINK.err);
    CHECK(dy_file_is("Palettes/warm.palette", "dyer 1\n#ff0000\n#ff8800\n#112233\n"),
          "the palette in the Hoard (DM2)");
    CHECK(dyer_remove(&DY, 3) == ZXM_E_ARG && dyer_remove(&DY, 1) == ZXM_OK && DY.n == 2 &&
              DY.color[1] == 0x112233 && dyer_save(&DY, true) == ZXM_OK &&
              dy_file_is("Palettes/warm.palette", "dyer 1\n#ff0000\n#112233\n"),
          "a swatch removed, saved again (the folder is there now): %s", LINK.err);

    /* a second model reads it back; a second palette; the list */
    dyer_init(&DY2, &LINK);
    CHECK(dyer_open(&DY2, "warm") == ZXM_OK && DY2.n == 2 && DY2.color[0] == 0xff0000 &&
              DY2.color[1] == 0x112233 && !DY2.dirty,
          "read back: %s", LINK.err);
    CHECK(dyer_open(&DY2, "cool") == ZXM_OK && DY2.n == 0 && dyer_add(&DY2, 0x0000ff) == ZXM_OK &&
              dyer_save(&DY2, true) == ZXM_OK,
          "a second palette: %s", LINK.err);
    uint64_t wrote = 0;
    CHECK(zxf_write("Palettes/notes.txt", (const uint8_t *) "x", 1, ZXF_W_REPLACE, false, &wrote) ==
                  0 &&
              zxf_mkdir("Palettes/old.palette") == 0,
          "a file and a folder that are not palettes");
    CHECK(dyer_list(&DY2) == ZXM_OK && DY2.nnames == 2 &&
              ((!strcmp(DY2.names[0], "cool") && !strcmp(DY2.names[1], "warm")) ||
               (!strcmp(DY2.names[0], "warm") && !strcmp(DY2.names[1], "cool"))),
          "the palettes, and nothing else: %u", DY2.nnames);

    /* a full palette refuses more */
    bool ok = true;
    while (ok && DY.n < DYER_PLACES) ok = dyer_add(&DY, (int64_t) DY.n * 0x010101) == ZXM_OK;
    CHECK(ok && DY.n == DYER_PLACES && dyer_add(&DY, 0) == ZXM_E_TOOBIG &&
              dyer_save(&DY, true) == ZXM_OK,
          "64 swatches: %s", LINK.err);
    CHECK(dyer_open(&DY2, "warm") == ZXM_OK && DY2.n == DYER_PLACES &&
              !memcmp(DY2.color, DY.color, sizeof DY.color),
          "a full palette read back: %s", LINK.err);

    /* a file that is not a palette is never written over (DM3) */
    static const char *const notpal[] = {"notes\n",           "dyer 1\n#ff0000",
                                         "dyer 1\n#FF0000\n", "dyer 1\n#ff000\n",
                                         "dyer 1\nff0000\n",  "dyer 2\n#ff0000\n"};
    for (uint32_t i = 0; i < sizeof notpal / sizeof notpal[0]; i++) {
        uint64_t size = 0;
        CHECK(zxf_write("Palettes/odd.palette", (const uint8_t *) notpal[i],
                        (uint32_t) strlen(notpal[i]), ZXF_W_REPLACE, false, &size) == 0 &&
                  dyer_open(&DY2, "odd") == ZXM_E_PROTOCOL && !DY2.loaded,
              "not a palette: %u", i);
    }
    before = sent;
    CHECK(dyer_add(&DY2, 1) == ZXM_E_STATE && dyer_save(&DY2, true) == ZXM_E_STATE &&
              sent == before && dy_file_is("Palettes/odd.palette", "dyer 2\n#ff0000\n"),
          "refused, left as it was");
    CHECK(zxf_write("Palettes/odd.palette", (const uint8_t *) "dyer 1\n", 7, ZXF_W_REPLACE, false,
                    &wrote) == 0 &&
              dyer_open(&DY2, "odd") == ZXM_OK && DY2.loaded && DY2.n == 0,
          "a palette with no swatches");
}
