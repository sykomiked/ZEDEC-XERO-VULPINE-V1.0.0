/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Cellar's model against zxvd's real dispatcher (LINK): sys.disk, sys.info
 * and files.* on the test's Hoard, in a folder of its own (Cellar-test),
 * which it puts in the Trash and empties at the end. A misnamed folder and
 * a misdated file are made beside the Hoard's own calls (mkdir, utime), as
 * a crash or another program would leave them. */
#include <utime.h>

#include "cellar_model.h"

static cellar_t CE;

static bool ce_put(const char *path, uint32_t n)
{
    static const uint8_t bytes[512] = {0};
    uint64_t size = 0;
    return zxf_write(path, bytes, n, ZXF_W_REPLACE, false, &size) == 0 && size == n;
}

APPS_TEST(cellar)
{
    /* the rules (cellar_rules.h) */
    CHECK(cellar_name_ok("Photos") && cellar_name_ok("a b.txt") && !cellar_name_ok("") &&
              !cellar_name_ok(".hidden") && !cellar_name_ok("bad:dir") &&
              !cellar_name_ok("back\\slash") && !cellar_name_ok("trail.") &&
              !cellar_name_ok("trail ") && !cellar_name_ok("tab\there"),
          "names the Hoard's paths can hold (F2)");
    CHECK(cellar_repair_safe(1, 0, 1) && cellar_repair_safe(CELLAR_BYTES_MAX, 9500, 1) &&
              !cellar_repair_safe(0, 0, 1) && !cellar_repair_safe(1, 9501, 1) &&
              !cellar_repair_safe(1, -1, 1) && !cellar_repair_safe(1, 0, 0) &&
              !cellar_repair_safe(1, 0, 2) && !cellar_repair_safe(CELLAR_BYTES_MAX + 1, 0, 1),
          "safe to repair: its edges (CR4)");

    /* nothing is asked before it applies */
    CHECK(zxf_use_space(NULL) == 0, "home");
    cellar_init(&CE, &LINK);
    uint32_t removed = 7;
    int before = sent;
    CHECK(cellar_repair_advice(&CE, true) == ZXM_E_STATE &&
              cellar_empty_trash(&CE, false, &removed) == ZXM_E_STATE && removed == 0 &&
              cellar_set_base(&CE, NULL) == ZXM_E_ARG && sent == before,
          "no advice before a measure and a check; no emptying without a yes");

    /* CM1: the volume */
    CHECK(cellar_measure(&CE) == ZXM_OK && CE.measured && CE.total > 0 && CE.free <= CE.total &&
              CE.avail <= CE.total && CE.used_x100 == cellar_used_x100(CE.total, CE.free),
          "the volume: %s", LINK.err);

    /* CM2: space by folder */
    CHECK(zxf_mkdir("Cellar-test") == 0 && zxf_mkdir("Cellar-test/Photos") == 0 &&
              zxf_mkdir("Cellar-test/Music") == 0 && zxf_mkdir("Cellar-test/Music/Live") == 0 &&
              ce_put("Cellar-test/Photos/a.jpg", 100) && ce_put("Cellar-test/Photos/b.jpg", 50) &&
              ce_put("Cellar-test/Music/Live/set.flac", 300) && ce_put("Cellar-test/notes.txt", 7),
          "a folder to survey");
    CHECK(cellar_set_base(&CE, "Cellar-test") == ZXM_OK && cellar_survey(&CE) == ZXM_OK &&
              CE.surveyed && CE.nfolders == 2,
          "survey: %s", LINK.err);
    int32_t mu = cellar_find(&CE, "Music"), ph = cellar_find(&CE, "Photos");
    CHECK(mu == 0 && ph == 1 && cellar_find(&CE, "Live") == -1, "folders first, by name");
    CHECK(mu == 0 && ph == 1 && CE.folder[mu].bytes == 300 && CE.folder[mu].files == 1 &&
              CE.folder[ph].bytes == 150 && CE.folder[ph].files == 2 && CE.loose_bytes == 7 &&
              CE.loose_files == 1 && CE.base_bytes == 457 && CE.base_files == 4,
          "the bytes below each folder, and the loose ones");
    CHECK(mu == 0 && ph == 1 && CE.folder[mu].share_x100 == 6564 &&
              CE.folder[ph].share_x100 == 3282 &&
              CE.folder[mu].disk_x100 == cellar_used_x100(CE.total, CE.total - 300) &&
              CE.folder[ph].disk_x100 == cellar_used_x100(CE.total, CE.total - 150),
          "each folder's share of the Hoard and of the volume");

    /* CM4: a clean check, then a misnamed folder and a misdated file */
    CHECK(cellar_check(&CE) == ZXM_OK && CE.checked && CE.now > 1700000000 && !CE.misnamed &&
              !CE.misdated,
          "a clean check: %s", LINK.err);
    CHECK(cellar_repair_advice(&CE, false) == ZXM_OK &&
              !strcmp(CE.advice, "The Hoard checks clean: nothing to repair."),
          "clean: %s", CE.advice);
    char root[ZXF_PATH_MAX], abs[ZXF_PATH_MAX + 64];
    CHECK(zxf_root(root, sizeof root) == 0, "the test's folder");
    snprintf(abs, sizeof abs, "%s/Cellar-test/bad:dir", root);
    CHECK(mkdir(abs, 0700) == 0, "a folder the Hoard's paths cannot name");
    snprintf(abs, sizeof abs, "%s/Cellar-test/Photos/b.jpg", root);
    struct utimbuf later = {4102444800, 4102444800}; /* 2100-01-01 */
    CHECK(utime(abs, &later) == 0, "a date a clock gone wrong left");
    CHECK(cellar_check(&CE) == ZXM_OK && CE.misnamed == 1 && CE.misdated == 1 &&
              !strcmp(CE.first_misnamed, "Cellar-test/bad:dir") &&
              !strcmp(CE.first_misdated, "Cellar-test/Photos/b.jpg"),
          "found: %u %s, %u %s", CE.misnamed, CE.first_misnamed, CE.misdated, CE.first_misdated);
    CHECK(cellar_survey(&CE) == ZXM_OK && CE.nfolders == 2 && CE.base_bytes == 457,
          "a misnamed folder is not walked into");
    CHECK(cellar_repair_advice(&CE, false) == ZXM_OK && !CE.repair_safe &&
              CE.room == CE.avail - CELLAR_REPAIR_RESERVE &&
              strstr(CE.advice, "\"Cellar-test/bad:dir\" and 0 more have names ZXV cannot open") &&
              strstr(CE.advice, "\"Cellar-test/Photos/b.jpg\" and 0 more are dated after") &&
              strstr(CE.advice, "Not safe to repair yet:") &&
              strstr(CE.advice, "waiting for your confirmation."),
          "advice without a yes: %s", CE.advice);
    bool safe = cellar_repair_safe(CE.avail - CELLAR_REPAIR_RESERVE, CE.used_x100, 1);
    CHECK(cellar_repair_advice(&CE, true) == ZXM_OK && CE.repair_safe == safe &&
              (safe ? strstr(CE.advice, "Safe to repair now.") != NULL
                    : strstr(CE.advice, "Not safe to repair yet:") != NULL &&
                          !strstr(CE.advice, "waiting for your confirmation")),
          "advice with a yes, on this disk: %s", CE.advice);
    snprintf(abs, sizeof abs, "%s/Cellar-test/bad:dir", root);
    CHECK(rmdir(abs) == 0 && cellar_check(&CE) == ZXM_OK && !CE.misnamed && CE.misdated == 1,
          "renamed outside ZXV: one finding left");

    /* CM3: the Trash */
    CHECK(cellar_trash_size(&CE) == ZXM_OK && CE.trash_known, "the Trash: %s", LINK.err);
    int64_t items = CE.trash_items, bytes = CE.trash_bytes;
    char trashed[ZXF_PATH_MAX];
    CHECK(zxf_trash("Cellar-test/Photos", trashed, sizeof trashed) == 0 &&
              cellar_trash_size(&CE) == ZXM_OK && CE.trash_items == items + 1 &&
              CE.trash_bytes == bytes + 150 &&
              CE.trash_x100 == cellar_used_x100(CE.total, CE.total - CE.trash_bytes),
          "a folder in the Trash: %lld items, %lld bytes", (long long) CE.trash_items,
          (long long) CE.trash_bytes);
    CHECK(cellar_survey(&CE) == ZXM_OK && CE.nfolders == 1 && CE.base_bytes == 307 &&
              CE.folder[0].share_x100 == 9771,
          "the survey without it");
    before = sent;
    CHECK(cellar_empty_trash(&CE, false, &removed) == ZXM_E_STATE && sent == before &&
              CE.trash_bytes == bytes + 150,
          "not emptied without the owner's yes");
    CHECK(zxf_trash("Cellar-test", trashed, sizeof trashed) == 0 &&
              cellar_empty_trash(&CE, true, &removed) == ZXM_OK && removed == items + 2 &&
              !CE.trash_items && !CE.trash_bytes && CE.trash_x100 == 0,
          "emptied with a yes: %u removed", removed);
    CHECK(cellar_trash_size(&CE) == ZXM_OK && !CE.trash_items && !CE.trash_bytes,
          "the Trash read again: empty");
    CHECK(cellar_survey(&CE) == ZXM_E_REMOTE, "the folder is gone: %s", LINK.err);

    /* made up replies: a volume that is not one */
    cellar_init(&CE, &FAKE);
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"total\":10,\"free\":11,\"avail\":1}}";
    CHECK(cellar_measure(&CE) == ZXM_E_PROTOCOL && !CE.measured &&
              strstr(last_req, "\"method\":\"sys.disk\""),
          "more free than the volume holds");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"total\":1000000000000000000,"
             "\"free\":0,\"avail\":0}}";
    CHECK(cellar_measure(&CE) == ZXM_E_TOOBIG && CE.fault == CELLAR_F_TOO_BIG && !CE.measured,
          "a volume past 18 digits");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"total\":0,\"free\":0,\"avail\":0}}";
    CHECK(cellar_measure(&CE) == ZXM_OK && CE.used_x100 == CELLAR_REFUSED,
          "a volume of 0 bytes: no share");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"entries\":[{\"name\":\"x\","
             "\"dir\":false,\"size\":1000000000000000000,\"mtime\":0}]}}";
    CHECK(cellar_survey(&CE) == ZXM_E_TOOBIG && CE.fault == CELLAR_F_TOO_BIG && !CE.surveyed,
          "a file past 18 digits");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"entries\":[{\"name\":\"x\","
             "\"dir\":false,\"mtime\":0}]}}";
    CHECK(cellar_survey(&CE) == ZXM_E_PROTOCOL, "a file with no size");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"removed\":-1}}";
    CHECK(cellar_empty_trash(&CE, true, &removed) == ZXM_E_PROTOCOL, "a count that is not one");
    CANNED = NULL;
}
