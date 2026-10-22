/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Graft's model against zxvd's real dispatcher (LINK): a folder of the
 * test's scratch Hoard committed twice (backup.run, its history file),
 * the history read back, a file's versions with their commits
 * (backup.versions), the first version brought back beside the file and
 * never over one (backup.restore), every snapshot still kept
 * (backup.list), a diff, three-way merges that are clean, the same, held
 * BOTH for the person and untouched, and the limits. */
#include "graft_model.h"

static graft_t GF, GF2;

static bool gf_write(const char *path, const char *text)
{
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", path);
    zxm_p_str(&LINK, "text", text);
    return zxm_call(&LINK) == ZXM_OK;
}

static bool gf_mkdir(const char *path)
{
    zxm_begin(&LINK, "files.mkdir");
    zxm_p_str(&LINK, "path", path);
    return zxm_call(&LINK) == ZXM_OK;
}

/* Is the file at path exactly `text`? */
static bool gf_is(const char *path, const char *text)
{
    static char got[4096];
    uint32_t n = 0;
    uint64_t size = 0;
    if (zxf_read(path, 0, (uint8_t *) got, sizeof got, false, &n, &size) != 0) return false;
    return n == strlen(text) && size == n && !memcmp(got, text, n);
}

/* A big file written straight into the Hoard: `lines` lines "L<i>\n",
 * the one at `odd` (if any) written "M<i>\n". */
static bool gf_big(const char *path, uint32_t lines, uint32_t odd)
{
    static char buf[ZXF_READ_MAX];
    uint32_t at = 0;
    uint64_t size = 0;
    bool first = true;
    for (uint32_t i = 0; i < lines; i++) {
        if (at + 16u > sizeof buf) {
            if (zxf_write(path, (const uint8_t *) buf, at, first ? ZXF_W_BEGIN : ZXF_W_ADD, false,
                          &size) != 0)
                return false;
            first = false;
            at = 0;
        }
        at += (uint32_t) snprintf(buf + at, sizeof buf - at, "%c%u\n", i == odd ? 'M' : 'L', i);
    }
    if (first) return zxf_write(path, (const uint8_t *) buf, at, ZXF_W_REPLACE, false, &size) == 0;
    return zxf_write(path, (const uint8_t *) buf, at, ZXF_W_END, false, &size) == 0;
}

APPS_TEST(graft)
{
    char dest[256];
    int64_t id = 0;
    snprintf(dest, sizeof dest, "%s/zxv-graft-XXXXXX",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(dest) != NULL, "graft: a backup destination");

    /* nothing asked before a folder and a destination */
    graft_init(&GF, &LINK);
    int before = sent;
    lpres_state_t st = LPRES_STATE_FALSE;
    CHECK(graft_commit(&GF, 1, 1700000000, "x", &id) == ZXM_E_STATE &&
              graft_versions(&GF, "GraftProj/a") == ZXM_E_STATE &&
              graft_restore(&GF, 1, "GraftProj/a", NULL) == ZXM_E_STATE &&
              graft_snapshots(&GF, &st) == ZXM_E_STATE && graft_open(&GF, "") == ZXM_E_ARG &&
              graft_open(&GF, "../up") == ZXM_E_ARG && graft_open(&GF, "/abs") == ZXM_E_ARG &&
              sent == before,
          "graft: nothing asked before a folder and a destination");
    CHECK(graft_set_dest(&GF, "relative") == ZXM_E_ARG && graft_set_dest(&GF, "") == ZXM_E_ARG &&
              graft_set_dest(&GF, NULL) == ZXM_E_ARG && graft_set_dest(&GF, dest) == ZXM_OK,
          "graft: the destination");

    /* GM1: a folder with no history yet */
    CHECK(gf_mkdir("GraftProj") && gf_write("GraftProj/notes.txt", "one\ntwo\nthree\n"),
          "graft: a folder in the Hoard");
    CHECK(graft_open(&GF, "GraftNope") == ZXM_E_REMOTE &&
              graft_open(&GF, "GraftProj/notes.txt") == ZXM_E_ARG && !GF.opened,
          "graft: no such folder, and a file is no folder");
    CHECK(graft_open(&GF, "GraftProj") == ZXM_OK && GF.opened && GF.n == 0 && GF.broken == 0,
          "graft: opened, no history yet: %s", LINK.err);

    /* GM2: refused before anything is sent */
    static char longm[GRAFT_MSG_MAX + 2];
    memset(longm, 'm', GRAFT_MSG_MAX + 1);
    before = sent;
    CHECK(graft_commit(&GF, 0, 1700000000, "first", &id) == ZXM_E_ARG &&
              graft_commit(&GF, GRAFT_AUTHOR + 1, 1700000000, "first", &id) == ZXM_E_ARG &&
              graft_commit(&GF, 1, -1, "first", &id) == ZXM_E_ARG &&
              graft_commit(&GF, 1, GRAFT_STAMP + 1, "first", &id) == ZXM_E_ARG &&
              graft_commit(&GF, 1, 1700000000, "", &id) == ZXM_E_ARG &&
              graft_commit(&GF, 1, 1700000000, "a\tb", &id) == ZXM_E_ARG &&
              graft_commit(&GF, 1, 1700000000, "a\nb", &id) == ZXM_E_ARG &&
              graft_commit(&GF, 1, 1700000000, longm, &id) == ZXM_E_ARG &&
              graft_commit(&GF, 1, 1700000000, NULL, &id) == ZXM_E_ARG &&
              graft_commit(&GF, 1, 1700000000, "first", NULL) == ZXM_E_ARG && sent == before &&
              GF.n == 0,
          "graft: bad commits refused before anything is sent");

    /* two commits, linked */
    CHECK(graft_commit(&GF, 7, 1700000000, "first words", &id) == ZXM_OK && id == 1 && GF.n == 1 &&
              GF.c[0].parent == 0 && GF.c[0].snapshot >= 1 && GF.broken == 1 &&
              GF.c[0].msglen == 11,
          "graft: the first commit: %s", LINK.err);
    CHECK(gf_write("GraftProj/notes.txt", "one\nTWO\nthree\nfour\n"), "graft: the notes changed");
    CHECK(graft_commit(&GF, 7, 1700000100, "second words", &id) == ZXM_OK && id == 2 && GF.n == 2 &&
              GF.c[1].parent == 1 && GF.c[1].snapshot > GF.c[0].snapshot && GF.broken == 2,
          "graft: the second commit, linked to the first: %s", LINK.err);

    /* GM1: another model reads the same history */
    graft_init(&GF2, &LINK);
    CHECK(graft_open(&GF2, "GraftProj") == ZXM_OK && GF2.n == 2 && GF2.broken == 2 &&
              GF2.c[1].commit == 2 && GF2.c[1].parent == 1 && GF2.c[1].author == 7 &&
              GF2.c[1].stamp == 1700000100 && GF2.c[1].snapshot == GF.c[1].snapshot &&
              !strcmp(GF2.c[1].msg, "second words") && graft_find(&GF2, 1) == &GF2.c[0] &&
              graft_find(&GF2, 3) == NULL,
          "graft: the history read back");

    /* versions, each with its commit */
    CHECK(graft_versions(&GF, "GraftProj/notes.txt") == ZXM_OK && GF.nvers == 2 &&
              GF.ver[0].commit == 2 && GF.ver[1].commit == 1 && GF.ver[0].size == 19 &&
              GF.ver[1].size == 14 && strlen(GF.ver[0].hash) == 64,
          "graft: two versions: %u %s", GF.nvers, LINK.err);
    before = sent;
    CHECK(graft_versions(&GF, "Elsewhere/notes.txt") == ZXM_E_ARG &&
              graft_versions(&GF, "GraftProj") == ZXM_E_ARG &&
              graft_versions(&GF, "GraftProj/") == ZXM_E_ARG && sent == before,
          "graft: only files inside the folder");

    /* GM3: the first version back beside the file, never over one */
    CHECK(graft_restore(&GF, 1, "GraftProj/notes.txt", NULL) == ZXM_OK &&
              !strcmp(GF.landed, "GraftProj/notes (restored).txt") && GF.restored == 1 &&
              gf_is("GraftProj/notes (restored).txt", "one\ntwo\nthree\n") &&
              gf_is("GraftProj/notes.txt", "one\nTWO\nthree\nfour\n"),
          "graft: brought back beside it: %s", GF.landed);
    CHECK(graft_restore(&GF, 1, "GraftProj/notes.txt", "GraftProj/notes.txt") == ZXM_E_REMOTE &&
              gf_is("GraftProj/notes.txt", "one\nTWO\nthree\nfour\n"),
          "graft: never over what is there");
    CHECK(graft_restore(&GF, 9, "GraftProj/notes.txt", NULL) == ZXM_E_ARG &&
              graft_restore(&GF, 1, "Elsewhere/x", NULL) == ZXM_E_ARG,
          "graft: no such commit, nothing outside the folder");

    /* GM7: every snapshot kept */
    CHECK(graft_snapshots(&GF, &st) == ZXM_OK && st == LPRES_STATE_TRUE &&
              graft_snapshots(&GF, NULL) == ZXM_E_ARG,
          "graft: every commit's snapshot kept");
    GF.c[0].snapshot = 9999; /* a snapshot pruned away */
    CHECK(graft_snapshots(&GF, &st) == ZXM_OK && st == LPRES_STATE_FALSE, "graft: one gone: FALSE");
    GF.c[0].snapshot = GF2.c[0].snapshot;

    /* GM5: a diff */
    CHECK(graft_diff(&GF, "GraftProj/notes (restored).txt", "GraftProj/notes.txt") == ZXM_OK &&
              GF.kept == 2 && GF.added == 2 && GF.removed == 1,
          "graft: two added, one removed, two kept: %lld %lld %lld", (long long) GF.added,
          (long long) GF.removed, (long long) GF.kept);
    CHECK(graft_diff(&GF, "GraftProj/notes.txt", "GraftProj/notes.txt") == ZXM_OK && GF.kept == 4 &&
              GF.added == 0 && GF.removed == 0,
          "graft: a file against itself");
    CHECK(graft_diff(&GF, "GraftProj", "GraftProj/notes.txt") == ZXM_E_ARG &&
              graft_diff(&GF, "GraftProj/none.txt", "GraftProj/notes.txt") == ZXM_E_REMOTE,
          "graft: a folder is no file, nor a file that is not there");

    /* GM6: merges */
    CHECK(gf_write("GraftProj/base.txt", "a\nb\nc\nd\n") &&
              gf_write("GraftProj/ours.txt", "a\nB\nc\nd\n") &&
              gf_write("GraftProj/theirs.txt", "a\nb\nc\nD\n") &&
              gf_write("GraftProj/clash.txt", "a\nX\nc\nd\n") &&
              gf_write("GraftProj/same.txt", "a\nB\nc\nd\nE"),
          "graft: a base and its changed copies");
    CHECK(graft_merge(&GF, "GraftProj/base.txt", "GraftProj/ours.txt", "GraftProj/theirs.txt",
                      "GraftProj/m1.txt") == ZXM_OK &&
              GF.merged == LPRES_STATE_TRUE && GF.units == 5 && GF.ours_only == 1 &&
              GF.theirs_only == 1 && GF.clashes == 0 && GF.untouched == 3 &&
              gf_is("GraftProj/m1.txt", "a\nB\nc\nD\n"),
          "graft: a clean merge, TRUE: %s", LINK.err);
    CHECK(graft_merge(&GF, "GraftProj/base.txt", "GraftProj/ours.txt", "GraftProj/clash.txt",
                      "GraftProj/m2.txt") == ZXM_OK &&
              GF.merged == LPRES_STATE_BOTH && GF.clashes == 1 &&
              gf_is("GraftProj/m2.txt", "a\n<<<<<<< ours\nB\n=======\nX\n>>>>>>> theirs\nc\nd\n"),
          "graft: a line changed apart is BOTH, both kept for the person");
    CHECK(graft_merge(&GF, "GraftProj/base.txt", "GraftProj/ours.txt", "GraftProj/same.txt",
                      "GraftProj/m3.txt") == ZXM_OK &&
              GF.merged == LPRES_STATE_TRUE && GF.same == 1 && GF.theirs_only == 1 &&
              GF.clashes == 0 && gf_is("GraftProj/m3.txt", "a\nB\nc\nd\nE"),
          "graft: the same change once, and a last line with no newline");
    CHECK(graft_merge(&GF, "GraftProj/base.txt", "GraftProj/base.txt", "GraftProj/base.txt",
                      "GraftProj/m4.txt") == ZXM_OK &&
              GF.merged == LPRES_STATE_NEITHER && GF.untouched == 5 &&
              gf_is("GraftProj/m4.txt", "a\nb\nc\nd\n"),
          "graft: nothing changed: NEITHER, the base kept");
    CHECK(graft_merge(&GF, "GraftProj/base.txt", "GraftProj/ours.txt", "GraftProj/theirs.txt",
                      "GraftProj/m2.txt") == ZXM_E_REMOTE &&
              GF.merged == LPRES_STATE_NEITHER &&
              gf_is("GraftProj/m2.txt", "a\n<<<<<<< ours\nB\n=======\nX\n>>>>>>> theirs\nc\nd\n"),
          "graft: a merge never over what is there");
    CHECK(graft_merge(&GF, "GraftProj/base.txt", "GraftProj/ours.txt", "GraftProj/theirs.txt",
                      "GraftProj/ours.txt") == ZXM_E_ARG,
          "graft: never into one of its own files");

    /* GM4: the limits, 65536 lines and 16 MiB, read across many pieces */
    CHECK(gf_big("GraftProj/big.txt", GRAFT_LINES_MAX, UINT32_MAX) &&
              gf_big("GraftProj/big2.txt", GRAFT_LINES_MAX, 40000u) &&
              gf_big("GraftProj/over.txt", GRAFT_LINES_MAX + 1u, UINT32_MAX),
          "graft: big files");
    CHECK(graft_diff(&GF, "GraftProj/big.txt", "GraftProj/big2.txt") == ZXM_OK &&
              GF.kept == GRAFT_LINES_MAX - 1 && GF.added == 1 && GF.removed == 1,
          "graft: 65536 lines, one changed: %lld %lld", (long long) GF.added,
          (long long) GF.removed);
    CHECK(graft_diff(&GF, "GraftProj/big.txt", "GraftProj/over.txt") == ZXM_E_TOOBIG &&
              strstr(GF.notice, "65536"),
          "graft: 65537 lines refused: %s", GF.notice);
    {
        /* past 16 MiB: refused by its size before a byte is read */
        static uint8_t mib[1u << 20];
        uint64_t size = 0;
        memset(mib, 'x', sizeof mib);
        bool ok = zxf_write("GraftProj/huge.txt", mib, sizeof mib, ZXF_W_BEGIN, false, &size) == 0;
        for (uint32_t i = 1; i < 16u && ok; i++)
            ok = zxf_write("GraftProj/huge.txt", mib, sizeof mib, ZXF_W_ADD, false, &size) == 0;
        ok = ok && zxf_write("GraftProj/huge.txt", mib, 1, ZXF_W_END, false, &size) == 0 &&
             size == (uint64_t) GRAFT_BYTES_MAX + 1u;
        before = sent;
        CHECK(ok && graft_diff(&GF, "GraftProj/huge.txt", "GraftProj/base.txt") == ZXM_E_TOOBIG &&
                  sent == before + 1 && strstr(GF.notice, "16 MiB"),
              "graft: 16 MiB and one byte refused: %s", GF.notice);
    }
    {
        /* GM5: a middle too long for the table is one replaced block */
        static char a1[8192], b1[8192];
        uint32_t x = 0, y = 0;
        for (uint32_t i = 0; i < 600u; i++) {
            x += (uint32_t) snprintf(a1 + x, sizeof a1 - x, "%u\n", i);
            y += (uint32_t) snprintf(b1 + y, sizeof b1 - y, "%u\n", (i + 1u) % 600u);
        }
        uint64_t size = 0;
        CHECK(zxf_write("GraftProj/rot1.txt", (const uint8_t *) a1, x, ZXF_W_REPLACE, false,
                        &size) == 0 &&
                  zxf_write("GraftProj/rot2.txt", (const uint8_t *) b1, y, ZXF_W_REPLACE, false,
                            &size) == 0 &&
                  graft_diff(&GF, "GraftProj/rot1.txt", "GraftProj/rot2.txt") == ZXM_OK &&
                  GF.kept == 0 && GF.added == 600 && GF.removed == 600,
              "graft: a long middle, one block: %lld kept", (long long) GF.kept);
    }

    /* GM1: a broken history is shown, never added to */
    CHECK(gf_write("GraftProj/graft-history.txt",
                   "1\t0\t7\t1700000000\t1\t5\tfirst\n2\t5\t7\t1700000100\t2\t6\tsecond\n"),
          "graft: a history whose second commit points elsewhere");
    CHECK(graft_open(&GF2, "GraftProj") == ZXM_OK && GF2.n == 2 && GF2.broken == 1 &&
              strstr(GF2.notice, "broken"),
          "graft: the broken history loaded and shown");
    before = sent;
    CHECK(graft_set_dest(&GF2, dest) == ZXM_OK &&
              graft_commit(&GF2, 7, 1700000200, "third", &id) == ZXM_E_STATE && sent == before &&
              GF2.n == 2,
          "graft: no commit added to a broken history");
    CHECK(gf_write("GraftProj/graft-history.txt", "1\t0\t7\t1700000000\t1\t9\tfirst\n") &&
              graft_open(&GF2, "GraftProj") == ZXM_E_PROTOCOL && !GF2.opened && GF2.n == 0,
          "graft: a history that does not read is not loaded");
    CHECK(graft_set_space(&GF, "home") == ZXM_OK && !GF.opened && GF.n == 0 && !GF.space[0],
          "graft: a space closes the folder");
}
