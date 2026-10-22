/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Cartulary's model against zxvd's real dispatcher (LINK): records filed
 * from the test's scratch Hoard once Keepsake has backed them up
 * (backup.run, backup.versions), versions, the schedule and holds, prune
 * advice (backup.list), search.query, the measures, a proof against a
 * journal on disk (journal.query, journal.verify), and record 7 version 3
 * from two sources: BOTH until the owner rules. */
#include "cartulary_model.h"

static cartulary_t CT, CT2;

static bool ct_write(const char *path, const char *text)
{
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", path);
    zxm_p_str(&LINK, "text", text);
    return zxm_call(&LINK) == ZXM_OK;
}

static bool ct_mkdir(const char *path)
{
    zxm_begin(&LINK, "files.mkdir");
    zxm_p_str(&LINK, "path", path);
    return zxm_call(&LINK) == ZXM_OK;
}

static bool ct_backup(const char *dest)
{
    char p[400];
    snprintf(p, sizeof p, "{\"dest\":\"%s\"}", dest);
    return raw_call("backup.run", p) == ZXM_OK;
}

APPS_TEST(cartulary)
{
    const int64_t DAY = 20000; /* 2024-10-04 */
    static const char *LEASE = "Lease of the north field, 7 years.";
    static const char *LEASE2 = "Lease of the north field, 7 years, renewed for 7 more.";
    char dest[256], jdir[256];
    int64_t rec = 0, rec2 = 0, left = 0, v = 0, w = 0;

    /* the rules, and every schedule row inside the limits (CR1, CR4) */
    CHECK(cartulary_left(DAY, 7, 0, DAY, &left) && left == 2556 && cartulary_may_go(0, 0, 1) &&
              !cartulary_may_go(0, 1, 1) && !cartulary_may_go(-1, 0, 0) &&
              cartulary_retention_ok(36525, 1) && !cartulary_retention_ok(36526, 0) &&
              cartulary_year(DAY) == 2024 && cartulary_year(0) == 1970 &&
              cartulary_year(-1) == 1969 && cartulary_year(19722) == 2023 &&
              cartulary_year(19723) == 2024,
          "cartulary: the rules");
    for (int64_t k = 1; k <= CARTULARY_CLASSES; k++) {
        int64_t y = 0, m = 0;
        CHECK(cartulary_schedule(k, &y, &m) && cartulary_retention_ok(cartulary_span(y, m), 0),
              "cartulary: class %lld's schedule holds CR4", (long long) k);
    }
    CHECK(!cartulary_schedule(0, &v, &w) && !cartulary_schedule(CARTULARY_CLASSES + 1, &v, &w),
          "cartulary: no class 0 or 6");

    snprintf(dest, sizeof dest, "%s/zxv-cart-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    snprintf(jdir, sizeof jdir, "%s/zxv-cartj-XXXXXX",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(dest) != NULL && mkdtemp(jdir) != NULL && zjr_open(jdir) == 0,
          "cartulary: a backup destination and a journal");

    /* nothing before a destination and a load */
    cartulary_init(&CT, &LINK);
    int before = sent;
    CHECK(cartulary_file(&CT, 3, "Records/lease.txt", DAY, &rec) == ZXM_E_STATE &&
              cartulary_search(&CT, "lease") == ZXM_E_STATE &&
              cartulary_arrive(&CT, 1, 1, 5, DAY) == ZXM_E_STATE &&
              cartulary_rule(&CT, 1, 1, 1, true) == ZXM_E_STATE &&
              cartulary_hold(&CT, 1, 1, true) == ZXM_E_STATE &&
              cartulary_prune_advice(&CT, 1, &v) == ZXM_E_STATE && sent == before,
          "cartulary: nothing asked before a destination and a load");
    CHECK(cartulary_set_dest(&CT, "relative") == ZXM_E_ARG &&
              cartulary_set_dest(&CT, "") == ZXM_E_ARG &&
              cartulary_set_dest(&CT, NULL) == ZXM_E_ARG && cartulary_set_dest(&CT, dest) == ZXM_OK,
          "cartulary: the destination");
    CHECK(cartulary_load(&CT) == ZXM_OK && CT.loaded && CT.n == 0 && CT.nheld == 0,
          "cartulary: no index yet is an empty one: %s", LINK.err);
    before = sent;
    CHECK(cartulary_file(&CT, 9, "Records/lease.txt", DAY, &rec) == ZXM_E_ARG &&
              cartulary_file(&CT, 3, "../lease.txt", DAY, &rec) == ZXM_E_ARG &&
              cartulary_file(&CT, 3, "/etc/passwd", DAY, &rec) == ZXM_E_ARG &&
              cartulary_file(&CT, 3, "Cartulary/index.txt", DAY, &rec) == ZXM_E_ARG &&
              cartulary_file(&CT, 3, "Records/a\tb.txt", DAY, &rec) == ZXM_E_ARG &&
              cartulary_file(&CT, 3, "Records/lease.txt", -1, &rec) == ZXM_E_ARG &&
              cartulary_file(&CT, 3, "Records/lease.txt", 1000000, &rec) == ZXM_E_ARG &&
              cartulary_file(&CT, 3, "Records/lease.txt", DAY, NULL) == ZXM_E_ARG && sent == before,
          "cartulary: refused before anything is sent");

    /* CM3: filing, once Keepsake has the file */
    CHECK(ct_mkdir("Records") && ct_write("Records/lease.txt", LEASE) &&
              ct_write("Records/letter.txt", "A letter about the lease."),
          "cartulary: two files in the Hoard");
    CHECK(cartulary_file(&CT, 3, "Records/lease.txt", DAY, &rec) != ZXM_OK && CT.n == 0,
          "cartulary: never backed up: refused (%s)", CT.notice);
    CHECK(ct_backup(dest), "cartulary: a backup: %s", LINK.err);
    CHECK(cartulary_file(&CT, 3, "Records/lease.txt", DAY, &rec) == ZXM_OK && rec == 1 &&
              CT.n == 1 && CT.row[0].version == 1 && CT.row[0].content > 0 && CT.row[0].seq > 0 &&
              CT.row[0].prev == 0 && CT.row[0].size == (int64_t) strlen(LEASE) &&
              CT.row[0].source == 1 && CT.row[0].filed == DAY,
          "cartulary: the lease filed: %s", LINK.err);
    CHECK(cartulary_file(&CT, 3, "Records/lease.txt", DAY, &rec) == ZXM_OK && rec == 1 && CT.n == 1,
          "cartulary: the same content files nothing");
    CHECK(cartulary_file(&CT, 2, "Records/lease.txt", DAY, &rec) == ZXM_E_ARG && CT.n == 1,
          "cartulary: a record keeps its class");
    CHECK(cartulary_file(&CT, 2, "Records/letter.txt", DAY, &rec2) == ZXM_OK && rec2 == 2 &&
              CT.n == 2,
          "cartulary: the letter filed: %s", LINK.err);
    CHECK(ct_write("Records/lease.txt", LEASE2) && ct_backup(dest), "cartulary: the lease renewed");
    CHECK(cartulary_file(&CT, 3, "Records/lease.txt", DAY + 30, &rec) == ZXM_OK && rec == 1 &&
              CT.n == 3 && CT.row[2].version == 2 && CT.row[2].prev == CT.row[0].seq &&
              CT.row[2].seq > CT.row[0].seq && CT.row[2].content != CT.row[0].content &&
              CT.row[2].size == (int64_t) strlen(LEASE2),
          "cartulary: its second version, linked to the first: %s", LINK.err);

    /* CM1: another model reads the same index */
    cartulary_init(&CT2, &LINK);
    CHECK(cartulary_load(&CT2) == ZXM_OK && CT2.n == 3 && CT2.row[2].record == 1 &&
              CT2.row[2].version == 2 && CT2.row[2].seq == CT.row[2].seq &&
              CT2.row[2].content == CT.row[2].content &&
              !strcmp(CT2.row[1].path, "Records/letter.txt"),
          "cartulary: the index read back");

    /* CM2, CM5: the schedule (class 3: 7 years, 2556 days), holds, advice */
    const int64_t LATER = DAY + 30 + 2556 + 1;
    CHECK(cartulary_days_left(&CT, 0, DAY, &left) == ZXM_OK && left == 2556 &&
              cartulary_days_left(&CT, 0, LATER, &left) == ZXM_OK && left == -31 &&
              cartulary_days_left(&CT, 9, DAY, &left) == ZXM_E_ARG,
          "cartulary: days left");
    CHECK(cartulary_prunable(&CT, 0, DAY) == LPRES_STATE_FALSE &&
              cartulary_prunable(&CT, 0, LATER) == LPRES_STATE_TRUE &&
              cartulary_prunable(&CT, 2, LATER) == LPRES_STATE_FALSE &&
              cartulary_prunable(&CT, 1, LATER) == LPRES_STATE_FALSE &&
              cartulary_due(&CT, LATER) == 1,
          "cartulary: the first version may go, the newest of each record never");
    CHECK(cartulary_hold(&CT, 1, 1, false) == ZXM_E_STATE &&
              cartulary_hold(&CT, 1, 2, true) == ZXM_E_ARG &&
              cartulary_hold(&CT, 99, 1, true) == ZXM_E_ARG && !cartulary_held(&CT, 1),
          "cartulary: a hold needs the owner's word");
    CHECK(cartulary_hold(&CT, 1, 1, true) == ZXM_OK && cartulary_held(&CT, 1) &&
              cartulary_prunable(&CT, 0, LATER) == LPRES_STATE_FALSE &&
              cartulary_due(&CT, LATER) == 0,
          "cartulary: a hold always wins: %s", LINK.err);
    CHECK(cartulary_load(&CT2) == ZXM_OK && cartulary_held(&CT2, 1) && CT2.nheld == 1,
          "cartulary: the hold kept in the Hoard");
    CHECK(cartulary_hold(&CT, 1, 0, true) == ZXM_OK && !cartulary_held(&CT, 1) &&
              cartulary_due(&CT, LATER) == 1,
          "cartulary: the hold lifted");
    CHECK(cartulary_prune_advice(&CT, 0, &v) == ZXM_E_ARG &&
              cartulary_prune_advice(&CT, 1, &v) == ZXM_OK && v == 1 &&
              cartulary_prune_advice(&CT, 5, &v) == ZXM_OK && v == 0,
          "cartulary: what a prune keeping 1 would take of 2 snapshots, never all: %s", LINK.err);

    /* CM6: found, records only, best first */
    CHECK(cartulary_search(&CT, "   ") == ZXM_E_ARG && cartulary_search(&CT, NULL) == ZXM_E_ARG,
          "cartulary: an empty search");
    CHECK(cartulary_search(&CT, "lease") == ZXM_OK && CT.nhits == 2 &&
              CT.row[CT.hit[0]].record == 1 && CT.row[CT.hit[1]].record == 2 &&
              CT.score[0] > CT.score[1],
          "cartulary: the lease by name first, the letter by its text (%u hits): %s", CT.nhits,
          LINK.err);

    /* CM7: measures */
    int64_t s1 = (int64_t) strlen(LEASE), s2 = (int64_t) strlen(LEASE2), m[2] = {s1, s1 + s2};
    CHECK(cartulary_class_bytes(&CT, 3, &v) == ZXM_OK && v == s1 + s2 &&
              cartulary_class_bytes(&CT, 1, &v) == ZXM_OK && v == 0 &&
              cartulary_class_bytes(&CT, 9, &v) == ZXM_E_ARG,
          "cartulary: a class's bytes (banker.total)");
    CHECK(cartulary_grid(&CT, 0) == 2 && cartulary_grid(&CT, 1) == 1 &&
              cartulary_grid(&CT, 2) == 2 && cartulary_grid(&CT, 3) == 0,
          "cartulary: the index grid");
    CHECK(cartulary_projected(m, 2, 12, &w) && w == s1 + s2 + 12 * s2 &&
              cartulary_growth(&CT, 3, DAY + 30, 2, 12, &v) == ZXM_OK && v == w &&
              cartulary_growth(&CT, 3, DAY + 30, 1, 12, &v) == ZXM_E_ARG &&
              cartulary_growth(&CT, 3, DAY + 30, CARTULARY_MONTHS + 1u, 12, &v) == ZXM_E_ARG &&
              cartulary_growth(&CT, 3, DAY + 30, 2, CARTULARY_AHEAD_MAX + 1, &v) == ZXM_E_ARG,
          "cartulary: October then November, a year ahead: %lld", (long long) v);

    /* CM8: proven against the journal; a broken history is held */
    CHECK(cartulary_prove(&CT, 1) == LPRES_STATE_TRUE &&
              cartulary_prove(&CT, 99) == LPRES_STATE_NEITHER,
          "cartulary: proven: %s", CT.notice);
    int64_t keep_prev = CT.row[2].prev, keep_seq = CT.row[2].seq;
    CT.row[2].prev = CT.row[0].seq - 1;
    CHECK(cartulary_prove(&CT, 1) == LPRES_STATE_BOTH,
          "cartulary: a history the journal does not bear out: held");
    CT.row[2].prev = keep_prev;
    CT.row[2].seq = INT64_C(999999999999);
    CHECK(cartulary_prove(&CT, 1) == LPRES_STATE_BOTH, "cartulary: a seq past the journal: held");
    CT.row[2].prev = CT.row[0].seq - 1;
    CHECK(cartulary_prove(&CT, 1) == LPRES_STATE_FALSE, "cartulary: both broken: FALSE");
    CT.row[2].prev = keep_prev, CT.row[2].seq = keep_seq;
    zjr_close();
    CHECK(cartulary_prove(&CT, 1) == LPRES_STATE_TRUE && strstr(CT.notice, "history holds"),
          "cartulary: no journal says nothing, the history alone stands");

    /* CM4, the BOTH test: record 7, version 3 from two sources */
    CHECK(ct_write(CARTULARY_INDEX, "1\t7\t1\t20000\t10\t9\t1\t0\t0\t0\tRecords/minutes.txt\n"
                                    "1\t7\t2\t20000\t10\t10\t1\t0\t0\t0\tRecords/minutes.txt\n"
                                    "1\t7\t3\t20000\t10\t11\t1\t0\t0\t0\tRecords/minutes.txt\n"
                                    "1\t7\t4\t20000\t10\t13\t1\t0\t0\t0\tRecords/minutes.txt\n") &&
              cartulary_load(&CT) == ZXM_OK && CT.n == 4 &&
              cartulary_state(&CT, 7, 3) == LPRES_STATE_TRUE,
          "cartulary: record 7, four versions: %s", LINK.err);
    const int64_t FAR = DAY + 1000; /* class 1: 6 months, 182 days */
    CHECK(cartulary_prunable(&CT, 2, FAR) == LPRES_STATE_TRUE && cartulary_due(&CT, FAR) == 3,
          "cartulary: one source: version 3 may go");
    CHECK(cartulary_arrive(&CT, 7, 3, 12, DAY) == ZXM_OK && CT.n == 5 &&
              cartulary_find(&CT, 7, 3, 1) == 2 && cartulary_find(&CT, 7, 3, 2) == 4 &&
              CT.row[2].content == 11 && CT.row[4].content == 12,
          "cartulary: content 12 arrives beside content 11: %s", LINK.err);
    lpres_state_t st = cartulary_state(&CT, 7, 3);
    CHECK(st == LPRES_STATE_BOTH && (int) st == 3, "cartulary: no ruling: BOTH (3)");
    CHECK(cartulary_prunable(&CT, 2, FAR) == LPRES_STATE_BOTH &&
              cartulary_prunable(&CT, 4, FAR) == LPRES_STATE_BOTH && cartulary_due(&CT, FAR) == 2,
          "cartulary: the prune truth refuses both while it is BOTH");
    CHECK(cartulary_arrive(&CT, 7, 3, 12, DAY) == ZXM_E_ARG &&
              cartulary_arrive(&CT, 7, 9, 5, DAY) == ZXM_E_ARG &&
              cartulary_arrive(&CT, 99, 1, 5, DAY) == ZXM_E_ARG &&
              cartulary_arrive(&CT, 7, 4, 0, DAY) == ZXM_E_ARG && CT.n == 5,
          "cartulary: a second copy, a version out of reach, no record, no content: refused");
    CHECK(cartulary_rule(&CT, 7, 3, 1, false) == ZXM_E_STATE &&
              cartulary_rule(&CT, 7, 3, 3, true) == ZXM_E_ARG &&
              cartulary_rule(&CT, 7, 2, 1, true) == ZXM_E_ARG &&
              cartulary_state(&CT, 7, 3) == LPRES_STATE_BOTH,
          "cartulary: only the owner's ruling, 1 or 2");
    CHECK(cartulary_rule(&CT, 7, 3, 2, true) == ZXM_OK &&
              cartulary_state(&CT, 7, 3) == LPRES_STATE_TRUE && CT.row[4].ruled == 1 &&
              CT.row[2].ruled == 2 && cartulary_prunable(&CT, 2, FAR) == LPRES_STATE_TRUE &&
              cartulary_due(&CT, FAR) == 4,
          "cartulary: ruled 2: TRUE, and it may go: %s", LINK.err);
    CHECK(cartulary_load(&CT2) == ZXM_OK && CT2.n == 5 &&
              cartulary_state(&CT2, 7, 3) == LPRES_STATE_TRUE && CT2.row[4].ruled == 1,
          "cartulary: the ruling kept in the index");
    CHECK(cartulary_arrive(&CT, 7, 4, 13, DAY) == ZXM_OK &&
              cartulary_state(&CT, 7, 4) == LPRES_STATE_TRUE,
          "cartulary: the same content from both sources agrees");

    /* CM1: a line that does not fit is refused on load */
    CHECK(ct_write(CARTULARY_INDEX, "1\t7\t1\t20000\t10\t0\t1\t0\t0\t0\tRecords/minutes.txt\n") &&
              cartulary_load(&CT2) == ZXM_E_PROTOCOL && !CT2.loaded && CT2.n == 0,
          "cartulary: a content of 0 is refused");
    CHECK(
        ct_write(CARTULARY_INDEX, "10000\t7\t1\t20000\t10\t5\t1\t0\t0\t0\tRecords/minutes.txt\n") &&
            cartulary_load(&CT2) == ZXM_E_PROTOCOL,
        "cartulary: a class past 4 digits is refused");
    CHECK(ct_write(CARTULARY_INDEX, "1\t7\t1\t20000\t10\t5\t1\t0\t0\t0\tRecords/minutes.txt") &&
              cartulary_load(&CT2) == ZXM_E_PROTOCOL,
          "cartulary: a line with no end is refused");
    CHECK(rm_tree(dest) == 0 && rm_tree(jdir) == 0, "cartulary: cleanup");
}
