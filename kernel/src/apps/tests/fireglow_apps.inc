/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Fireglow's model against zxvd's real dispatcher (LINK): Podcasts and
 * Audiobooks folders written with files.write into the test's scratch
 * Hoard, listed, a show's saved places read (one line kept, two refused),
 * the time left at a speed, a book's chapters, an episode heard on, the
 * listening shares, and the places saved and read back. */
#include "fireglow_model.h"

/* a file written straight through zxvd, as another app would */
static bool fg_write(const char *path, const char *text)
{
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", path);
    zxm_p_str(&LINK, "text", text);
    return zxm_call(&LINK) == ZXM_OK;
}

static bool fg_mkdir(const char *path)
{
    zxm_begin(&LINK, "files.mkdir");
    zxm_p_str(&LINK, "path", path);
    return zxm_call(&LINK) == ZXM_OK;
}

static fireglow_t FGW, FGB;

APPS_TEST(fireglow)
{
    char text[24];
    int64_t ms = 0, share[FIREGLOW_SHOWS];

    /* FM1: no folders yet is an empty library; nothing else applies */
    fireglow_init(&FGW, &LINK);
    CHECK(FGW.rate == FIREGLOW_RATE_UNIT && !FGW.loaded && FGW.chapters_of == -1 &&
              fireglow_heard(&FGW, 0, 0, 0, 0) == ZXM_E_STATE &&
              fireglow_save(&FGW, 0) == ZXM_E_STATE && fireglow_chapters(&FGW, 0) == ZXM_E_STATE,
          "fireglow: before the load, nothing applies");
    CHECK(fireglow_load(&FGW) == ZXM_OK && FGW.loaded && FGW.nshows == 0 && FGW.n == 0 &&
              FGW.skipped == 0 && FGW.unsaved == 0,
          "fireglow: no Podcasts or Audiobooks folder: an empty library: %s", LINK.err);

    /* FM1, FM2: two shows and a book, with notes, a stray folder and saved places */
    CHECK(fg_mkdir("Podcasts") && fg_mkdir("Podcasts/Night Talks") &&
              fg_mkdir("Podcasts/Night Talks/extras") && fg_mkdir("Podcasts/Garden Hour") &&
              fg_mkdir("Audiobooks") && fg_mkdir("Audiobooks/The Long Road") &&
              fg_write("Podcasts/readme.txt", "shows go in folders") &&
              fg_write("Podcasts/Night Talks/ep1.mp3", "ID3 one") &&
              fg_write("Podcasts/Night Talks/ep2.MP3", "ID3 two") &&
              fg_write("Podcasts/Night Talks/notes.txt", "guests") &&
              fg_write("Podcasts/Garden Hour/g1.ogg", "OggS") &&
              fg_write("Audiobooks/The Long Road/part1.m4b", "ftyp") &&
              fg_write("Audiobooks/The Long Road/part1.m4b.chapters", "0\n60000\r\n120000\n") &&
              fg_write("Podcasts/Garden Hour/g1.ogg.chapters", "5\n0\n") &&
              fg_write("Podcasts/Night Talks/fireglow.tsv",
                       "ep1.mp3\t30000\t60000\t90000\nmissing.mp3\t1\t2\t3\n"
                       "ep2.MP3\t70000\t60000\t0\n"),
          "fireglow: the folders written: %s", LINK.err);
    CHECK(fireglow_load(&FGW) == ZXM_OK && FGW.nshows == 3 && FGW.n == 4 && FGW.skipped == 2 &&
              FGW.unsaved == 2,
          "fireglow: three shows, four episodes, a stray file and folder skipped, two saved lines "
          "refused: %s (%u, %u, %u, %u)",
          LINK.err, FGW.nshows, FGW.n, FGW.skipped, FGW.unsaved);
    int32_t night = fireglow_find_show(&FGW, "Night Talks"),
            garden = fireglow_find_show(&FGW, "Garden Hour"),
            road = fireglow_find_show(&FGW, "The Long Road");
    CHECK(night >= 0 && garden >= 0 && road >= 0 && FGW.show[road].kind == FIREGLOW_BOOK &&
              FGW.show[night].kind == FIREGLOW_PODCAST && fireglow_find_show(&FGW, "extras") < 0,
          "fireglow: every show found by its name, the book as a book");
    int32_t ep1 = fireglow_find(&FGW, (uint32_t) night, "ep1.mp3"),
            ep2 = fireglow_find(&FGW, (uint32_t) night, "ep2.MP3"),
            g1 = fireglow_find(&FGW, (uint32_t) garden, "g1.ogg"),
            part1 = fireglow_find(&FGW, (uint32_t) road, "part1.m4b");
    CHECK(ep1 >= 0 && ep2 >= 0 && g1 >= 0 && part1 >= 0 &&
              fireglow_find(&FGW, (uint32_t) night, "notes.txt") < 0 &&
              fireglow_find(&FGW, (uint32_t) garden, "ep1.mp3") < 0,
          "fireglow: every episode found in its own show");
    CHECK(FGW.ep[ep1].pos == 30000 && FGW.ep[ep1].length == 60000 && FGW.ep[ep1].heard == 90000 &&
              FGW.ep[ep2].pos == 0 && FGW.ep[ep2].length == 0,
          "fireglow: the saved place read, a place past its length refused (FM2)");

    /* FM5: the time left at a speed, only inside the limits */
    CHECK(fireglow_left(&FGW, (uint32_t) ep1, &ms) == ZXM_OK && ms == 30000 &&
              fireglow_left(&FGW, (uint32_t) ep2, &ms) == ZXM_E_STATE &&
              fireglow_left(&FGW, FGW.n, &ms) == ZXM_E_ARG,
          "fireglow: 30 s left at the normal speed; a length not known has none");
    CHECK(fireglow_set_speed(&FGW, 131072) == ZXM_OK &&
              fireglow_left(&FGW, (uint32_t) ep1, &ms) == ZXM_OK && ms == 15000 &&
              fireglow_left_text(&FGW, (uint32_t) ep1, text, sizeof text) == ZXM_OK &&
              !strcmp(text, "0:00:15") &&
              fireglow_left_text(&FGW, (uint32_t) ep1, text, 4) == ZXM_E_TOOBIG,
          "fireglow: twice as fast, 15 s left, 0:00:15 (%lld, %s)", (long long) ms, text);
    CHECK(fireglow_set_speed(&FGW, FIREGLOW_RATE_MAX + 1) == ZXM_E_ARG &&
              fireglow_set_speed(&FGW, FIREGLOW_RATE_MIN - 1) == ZXM_E_ARG && FGW.rate == 131072 &&
              FGW.notice[0],
          "fireglow: a speed past the limits is refused, the speed kept (FR4)");

    /* FM4: heard on, checked before anything changes */
    CHECK(fireglow_heard(&FGW, (uint32_t) part1, 90000, 3600000, 180000) == ZXM_OK &&
              FGW.ep[part1].pos == 90000 && FGW.ep[part1].heard == 180000,
          "fireglow: the book heard to 1:30, three minutes listened");
    CHECK(fireglow_heard(&FGW, (uint32_t) part1, 3600001, 3600000, 0) == ZXM_E_ARG &&
              fireglow_heard(&FGW, (uint32_t) part1, 0, FIREGLOW_LEN_MAX + 1, 0) == ZXM_E_ARG &&
              fireglow_heard(&FGW, (uint32_t) part1, 0, 3600000, -1) == ZXM_E_ARG &&
              fireglow_heard(&FGW, (uint32_t) part1, 0, 3600000, FIREGLOW_HEARD_MAX) == ZXM_E_ARG &&
              fireglow_heard(&FGW, FGW.n, 0, 0, 0) == ZXM_E_ARG && FGW.ep[part1].pos == 90000 &&
              FGW.ep[part1].length == 3600000 && FGW.ep[part1].heard == 180000,
          "fireglow: a place past the length, a length of 100 hours, time heard taken back or "
          "past MYRA's digits: refused, nothing changed (FR5)");

    /* FM3: chapters */
    CHECK(fireglow_chapter(&FGW, (uint32_t) part1) == FIREGLOW_REFUSED &&
              fireglow_chapters(&FGW, (uint32_t) part1) == ZXM_OK && FGW.nstarts == 3 &&
              FGW.starts[2] == 120000 && fireglow_chapter(&FGW, (uint32_t) part1) == 2,
          "fireglow: the book's chapters read, 1:30 is in the second: %s", LINK.err);
    CHECK(fireglow_chapters(&FGW, (uint32_t) g1) == ZXM_E_STATE && FGW.chapters_of == part1 &&
              FGW.nstarts == 3 && FGW.notice[0],
          "fireglow: a chapter list not starting at 0 is refused, the one read before kept");
    CHECK(fireglow_chapters(&FGW, (uint32_t) ep1) == ZXM_OK && FGW.nstarts == 0 &&
              fireglow_chapter(&FGW, (uint32_t) ep1) == 0 &&
              fireglow_chapter(&FGW, (uint32_t) part1) == FIREGLOW_REFUSED,
          "fireglow: no chapter file, no chapters");

    /* FM6: the listening shares, through banker.share's rule */
    CHECK(fireglow_shares(&FGW, share, FGW.nshows) == ZXM_OK && share[night] == 2500 &&
              share[garden] == 0 && share[road] == 7500 &&
              fireglow_shares(&FGW, share, FGW.nshows - 1u) == ZXM_E_ARG,
          "fireglow: one minute of four and three of four (%lld, %lld, %lld)",
          (long long) share[night], (long long) share[garden], (long long) share[road]);

    /* FM2: saved and read back by a fresh model */
    int before = sent;
    CHECK(fireglow_save(&FGW, (uint32_t) road) == ZXM_OK &&
              fireglow_save(&FGW, (uint32_t) night) == ZXM_OK && sent == before + 2 &&
              fireglow_save(&FGW, FGW.nshows) == ZXM_E_ARG,
          "fireglow: two shows' places written: %s", LINK.err);
    fireglow_init(&FGB, &LINK);
    CHECK(fireglow_load(&FGB) == ZXM_OK && FGB.n == 4 && FGB.unsaved == 0,
          "fireglow: read back, every saved line kept: %s (%u)", LINK.err, FGB.unsaved);
    int32_t back = fireglow_find(&FGB, (uint32_t) fireglow_find_show(&FGB, "The Long Road"),
                                 "part1.m4b"),
            back1 =
                fireglow_find(&FGB, (uint32_t) fireglow_find_show(&FGB, "Night Talks"), "ep1.mp3");
    CHECK(back >= 0 && back1 >= 0 && FGB.ep[back].pos == 90000 && FGB.ep[back].length == 3600000 &&
              FGB.ep[back].heard == 180000 && FGB.ep[back1].pos == 30000 &&
              FGB.ep[back1].heard == 90000 && FGB.rate == FIREGLOW_RATE_UNIT,
          "fireglow: the places as they were saved");
    CHECK(fireglow_load(&FGW) == ZXM_OK && FGW.n == 4 && FGW.ep[part1].heard == 180000 &&
              FGW.chapters_of == -1,
          "fireglow: a load again keeps the saved places and forgets the chapters read");
}
