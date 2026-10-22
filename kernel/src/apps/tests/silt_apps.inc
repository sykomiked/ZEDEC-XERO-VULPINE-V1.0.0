/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Silt's model against zxvd's real dispatcher (LINK): records received
 * from the test's scratch Hoard (files.list, files.stat, a PDF's head and
 * tail by files.read), stamped and registered (files.mkdir, files.write)
 * under a journal on disk (journal.query), two records under one
 * reference with different dates held BOTH and never merged, a legal hold
 * and a disposal to the Trash only on the owner's word (files.trash), a
 * record's versions (backup.run, backup.versions), the trail
 * (journal.verify), the measures and the backlog forecast. web4.verify is
 * not linked in this in-process zxvd (zxvd_web4.c is one of zxvd's groups,
 * outside test_apps.sh's link line), so it is answered on a link of its
 * own (SL_LINK) as zxvd's schema gives it; every other request goes to the
 * real dispatcher. */
#include "silt_model.h"

static silt_t SL, SL2;
static zxm_link_t SL_LINK;
static int SL_VERIFIED; /* web4.verify requests seen */
static const char SL_GOOD[] = "5745455601"
                              "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";

static int32_t sl_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    if (!strstr(req, "\"web4.verify\"")) return inproc(NULL, req, len, resp, cap);
    SL_VERIFIED++;
    char want[sizeof SL_GOOD + 16];
    snprintf(want, sizeof want, "\"envelope\":\"%s\"", SL_GOOD);
    if (!strstr(req, want))
        return snprintf(resp, cap,
                        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"valid\":false,"
                        "\"reason\":\"signature\"}}",
                        l->seq);
    return snprintf(resp, cap,
                    "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"valid\":true,\"kind\":"
                    "\"REQUEST\",\"from\":\"%s\",\"to\":\"%s\",\"seq\":\"1\",\"ts\":1790000000,"
                    "\"req_id\":\"1\",\"chunk\":0,\"tool\":\"silt\",\"final\":false,"
                    "\"payload_bytes\":321,\"consent\":false}}",
                    l->seq, SL_GOOD + 10, SL_GOOD + 10);
}

static bool sl_write(const char *path, const char *text)
{
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", path);
    zxm_p_str(&LINK, "text", text);
    return zxm_call(&LINK) == ZXM_OK;
}

static bool sl_exists(const char *path)
{
    zxm_begin(&LINK, "files.stat");
    zxm_p_str(&LINK, "path", path);
    return zxm_call(&LINK) == ZXM_OK;
}

APPS_TEST(silt)
{
    const int64_t DAY = 20000;        /* 2024-10-04 */
    const int64_t LATER = DAY + 400;  /* a year and a month on */
    const int64_t Y = 2024 * 1000000; /* the register numbers of 2024 */
    static const char *DEED = "Deed of the north field, registered.";
    static const char *PDF = "%PDF-1.4\n1 0 obj\n<< /Type /Catalog >>\nendobj\nxref\n0 2\n"
                             "0000000000 65535 f \n0000000009 00000 n \ntrailer\n<< /Size 2 >>\n"
                             "startxref\n46\n%%EOF\n";
    char dest[256], jdir[256];
    int64_t no = 0, no2 = 0, no3 = 0, left = 0, v = 0;
    uint32_t w = 0;

    /* the rules, and SR5's join */
    static const int64_t d2[2] = {DAY - 30, DAY - 10};
    CHECK(silt_year(DAY) == 2024 && silt_year(0) == 1970 && silt_year(-1) == 1969 &&
              silt_register_number(2024, 1) == Y + 1 && silt_next_seq(41) == 42 &&
              silt_may_dispose(0, 0, 1) && !silt_may_dispose(0, 1, 1) &&
              !silt_may_dispose(-1, 0, 0) && silt_reference_state(d2, 2) == LPRES_STATE_BOTH,
          "silt: the rules");

    snprintf(dest, sizeof dest, "%s/zxv-silt-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    snprintf(jdir, sizeof jdir, "%s/zxv-siltj-XXXXXX",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(dest) != NULL && mkdtemp(jdir) != NULL && zjr_open(jdir) == 0,
          "silt: a backup destination and a journal");

    /* nothing before a load */
    silt_init(&SL, &LINK);
    int before = sent;
    CHECK(silt_receive(&SL, "Inbox/deed.txt", 3, 7, "LND-2024-17", DAY - 30, DAY, true, &no) ==
                  ZXM_E_STATE &&
              silt_dispose(&SL, Y + 1, 1, 0, LATER, 1, true) == ZXM_E_STATE &&
              silt_hold(&SL, Y + 1, 1, true) == ZXM_E_STATE &&
              silt_inbox(&SL, "Inbox", &w) == ZXM_E_STATE &&
              silt_versions(&SL, Y + 1, &w) == ZXM_E_STATE && sent == before,
          "silt: nothing asked before a load");
    CHECK(silt_load(&SL) == ZXM_OK && SL.loaded && SL.n == 0,
          "silt: no register yet is an empty one: %s", LINK.err);

    /* SM7: the inbox */
    zxm_begin(&LINK, "files.mkdir");
    zxm_p_str(&LINK, "path", "Inbox");
    CHECK(zxm_call(&LINK) == ZXM_OK && sl_write("Inbox/deed.txt", DEED) &&
              sl_write("Inbox/deed-copy.txt", DEED) && sl_write("Inbox/minutes.pdf", PDF) &&
              sl_write("Inbox/broken.pdf", "%PDF-1.4\nno trailer here\n"),
          "silt: four records waiting in the inbox");
    CHECK(silt_inbox(&SL, "Inbox", &w) == ZXM_OK && w == 4 &&
              silt_inbox(&SL, "../x", &w) == ZXM_E_ARG,
          "silt: four waiting: %s", LINK.err);

    /* SM2: refused before anything is sent */
    before = sent;
    CHECK(silt_receive(&SL, "Inbox/deed.txt", 0, 7, "LND-2024-17", DAY - 30, DAY, true, &no) ==
                  ZXM_E_ARG &&
              silt_receive(&SL, "Inbox/deed.txt", 10000, 7, "LND-2024-17", DAY - 30, DAY, true,
                           &no) == ZXM_E_ARG &&
              silt_receive(&SL, "../deed.txt", 3, 7, "LND-2024-17", DAY - 30, DAY, true, &no) ==
                  ZXM_E_ARG &&
              silt_receive(&SL, "Silt/register.txt", 3, 7, "LND", DAY - 30, DAY, true, &no) ==
                  ZXM_E_ARG &&
              silt_receive(&SL, "Inbox/deed.txt", 3, 7, "", DAY - 30, DAY, true, &no) ==
                  ZXM_E_ARG &&
              silt_receive(&SL, NULL, 3, 7, "LND", DAY - 30, DAY, true, &no) == ZXM_E_ARG &&
              silt_receive(&SL, "Inbox/deed.txt", 3, 7, "LND 17", DAY - 30, DAY, true, &no) ==
                  ZXM_E_ARG &&
              silt_receive(&SL, "Inbox/deed.txt", 3, 7, "LND-2024-17", DAY + 1, DAY, true, &no) ==
                  ZXM_E_ARG &&
              silt_receive(&SL, "Inbox/deed.txt", 3, 0, "LND-2024-17", DAY - 30, DAY, true, &no) ==
                  ZXM_E_ARG &&
              silt_receive(&SL, "Inbox/deed.txt", 3, 7, "LND-2024-17", DAY - 30, 1000000, true,
                           &no) == ZXM_E_ARG &&
              silt_receive(&SL, "Inbox/deed.txt", 3, 7, "LND-2024-17", DAY - 30, DAY, true, NULL) ==
                  ZXM_E_ARG &&
              sent == before && SL.n == 0,
          "silt: a receipt not ready to stamp is refused before anything is sent");
    CHECK(silt_receive(&SL, "Inbox/deed.txt", 3, 7, "LND-2024-17", DAY - 30, DAY, false, &no) ==
                  ZXM_E_STATE &&
              sent == before && SL.n == 0,
          "silt: without the owner's word nothing is sent and nothing kept");

    /* SM2: received and stamped */
    CHECK(silt_receive(&SL, "Inbox/deed.txt", 3, 7, "LND-2024-17", DAY - 30, DAY, true, &no) ==
                  ZXM_OK &&
              no == Y + 1 && SL.n == 1 && SL.row[0].doc == 1 && SL.row[0].seq == 1 &&
              SL.row[0].prev == 0 && SL.row[0].jseq > 0 && SL.row[0].kind == SILT_FILED &&
              SL.row[0].size == (int64_t) strlen(DEED) && SL.row[0].received == DAY,
          "silt: the deed stamped %lld: %s", (long long) no, LINK.err);
    CHECK(silt_receive(&SL, "Inbox/deed.txt", 3, 8, "LND-2024-18", DAY - 30, DAY, true, &no) ==
                  ZXM_E_ARG &&
              SL.n == 1,
          "silt: the same record twice is refused");
    CHECK(silt_receive(&SL, "Inbox/minutes.pdf", 3, 7, "MIN-2024-03", DAY, DAY, true, &no2) ==
                  ZXM_OK &&
              no2 == Y + 2 && SL.n == 2 && SL.row[1].doc == 2 && SL.row[1].seq == 2 &&
              SL.row[1].prev == 1 && SL.row[1].jseq >= SL.row[0].jseq,
          "silt: a well-formed PDF, the case's second document: %s", LINK.err);
    CHECK(silt_receive(&SL, "Inbox/broken.pdf", 3, 7, "MIN-2024-04", DAY, DAY, true, &v) ==
                  ZXM_E_ARG &&
              SL.n == 2 && strstr(SL.notice, "not a well-formed PDF"),
          "silt: a PDF with no startxref is not received: %s", SL.notice);
    CHECK(silt_receive(&SL, "Inbox", 3, 7, "DIR-1", DAY, DAY, true, &v) == ZXM_E_ARG && SL.n == 2,
          "silt: a folder is no record");

    /* SM3, the BOTH test: a second record under LND-2024-17, dated 20 days later */
    CHECK(silt_receive(&SL, "Inbox/deed-copy.txt", 3, 7, "LND-2024-17", DAY - 10, DAY, true,
                       &no3) == ZXM_OK &&
              no3 == Y + 3 && SL.n == 3 && strstr(SL.notice, "flagged, never merged"),
          "silt: the second record under the reference is filed too: %s", SL.notice);
    lpres_state_t st = silt_reference(&SL, "LND-2024-17");
    CHECK(st == LPRES_STATE_BOTH && (int) st == 3 && silt_find(&SL, no) == 0 &&
              silt_find(&SL, no3) == 2 && SL.row[0].dated == DAY - 30 &&
              SL.row[2].dated == DAY - 10 && strcmp(SL.row[0].path, SL.row[2].path),
          "silt: one reference, two dates: BOTH (3), both filed, never merged");
    CHECK(silt_reference(&SL, "MIN-2024-03") == LPRES_STATE_TRUE &&
              silt_reference(&SL, "NONE-1") == LPRES_STATE_NEITHER,
          "silt: one date TRUE, no record NEITHER");
    CHECK(silt_inbox(&SL, "Inbox", &w) == ZXM_OK && w == 1, "silt: one left in the inbox");

    /* SM1: another model reads the same register */
    silt_init(&SL2, &LINK);
    CHECK(silt_load(&SL2) == ZXM_OK && SL2.n == 3 && SL2.row[2].regno == no3 &&
              !strcmp(SL2.row[1].ref, "MIN-2024-03") &&
              !strcmp(SL2.row[1].path, "Inbox/minutes.pdf") && SL2.row[1].jseq == SL.row[1].jseq &&
              silt_reference(&SL2, "LND-2024-17") == LPRES_STATE_BOTH,
          "silt: the register read back");

    /* SM4, SM5: retention (a year: 365 days) and disposal */
    CHECK(silt_days_left(&SL, 1, 1, 0, DAY, &left) == ZXM_OK && left == 365 &&
              silt_days_left(&SL, 1, 1, 0, LATER, &left) == ZXM_OK && left == -35 &&
              silt_days_left(&SL, 1, 0, 0, DAY, &left) == ZXM_E_ARG &&
              silt_days_left(&SL, 1, 101, 0, DAY, &left) == ZXM_E_ARG &&
              silt_days_left(&SL, 9, 1, 0, DAY, &left) == ZXM_E_ARG,
          "silt: days left");
    CHECK(silt_disposable(&SL, 1, 1, 0, DAY, 1) == LPRES_STATE_FALSE &&
              silt_disposable(&SL, 1, 1, 0, LATER, 0) == LPRES_STATE_FALSE &&
              silt_disposable(&SL, 1, 1, 0, LATER, 1) == LPRES_STATE_TRUE &&
              silt_disposable(&SL, 0, 1, 0, LATER, 1) == LPRES_STATE_BOTH &&
              silt_disposable(&SL, 2, 1, 0, LATER, 1) == LPRES_STATE_BOTH,
          "silt: the minutes may go once approved; the two deeds are held while they disagree");
    before = sent;
    CHECK(silt_hold(&SL, no2, 1, false) == ZXM_E_STATE &&
              silt_hold(&SL, no2, 2, true) == ZXM_E_ARG &&
              silt_hold(&SL, Y + 99, 1, true) == ZXM_E_ARG && SL.row[1].hold == 0 && sent == before,
          "silt: a legal hold needs the owner's word");
    CHECK(silt_hold(&SL, no2, 1, true) == ZXM_OK && SL.row[1].hold == 1 &&
              silt_disposable(&SL, 1, 1, 0, LATER, 1) == LPRES_STATE_FALSE &&
              silt_load(&SL2) == ZXM_OK && SL2.row[1].hold == 1,
          "silt: a legal hold always wins, and is kept: %s", LINK.err);
    CHECK(silt_dispose(&SL, no2, 1, 0, LATER, 1, true) == ZXM_E_STATE &&
              sl_exists("Inbox/minutes.pdf"),
          "silt: held: not disposed of");
    CHECK(silt_hold(&SL, no2, 0, true) == ZXM_OK && SL.row[1].hold == 0, "silt: the hold lifted");
    before = sent;
    CHECK(silt_dispose(&SL, no2, 1, 0, LATER, 1, false) == ZXM_E_STATE && sent == before &&
              SL.row[1].kind == SILT_FILED && sl_exists("Inbox/minutes.pdf"),
          "silt: without the owner's word nothing is sent and the record stays");
    before = sent;
    CHECK(silt_dispose(&SL, no2, 1, 0, LATER, 2, true) == ZXM_E_ARG &&
              silt_dispose(&SL, Y + 99, 1, 0, LATER, 1, true) == ZXM_E_ARG &&
              silt_dispose(&SL, no2, 0, 0, LATER, 1, true) == ZXM_E_ARG && sent == before,
          "silt: a disposal's arguments are checked first");
    before = sent;
    CHECK(silt_dispose(&SL, no2, 1, 0, DAY, 1, true) == ZXM_E_STATE &&
              silt_dispose(&SL, no2, 1, 0, LATER, 0, true) == ZXM_E_STATE &&
              silt_dispose(&SL, no, 1, 0, LATER, 1, true) == ZXM_E_STATE &&
              strstr(SL.notice, "disagree") && sent == before && sl_exists("Inbox/deed.txt"),
          "silt: not over, not approved, or BOTH: nothing is sent: %s", SL.notice);
    CHECK(silt_dispose(&SL, no2, 1, 0, LATER, 1, true) == ZXM_OK &&
              SL.row[1].kind == SILT_DISPOSED && !sl_exists("Inbox/minutes.pdf") &&
              strstr(SL.notice, "Trash"),
          "silt: the minutes moved to the Trash: %s", LINK.err);
    {
        zxm_begin(&LINK, "files.list");
        zxm_p_str(&LINK, "path", ".Trash");
        bool found = false;
        if (zxm_call(&LINK) == ZXM_OK) {
            char name[256];
            int32_t arr = zxm_get(&LINK, LINK.result, "entries");
            for (uint32_t k = 0; arr >= 0 && k < zxm_len(&LINK, arr); k++)
                if (!zxm_get_str(&LINK, zxm_at(&LINK, arr, k), "name", name, sizeof name) &&
                    strstr(name, "minutes"))
                    found = true;
        }
        CHECK(found, "silt: kept in the Trash, never erased");
    }
    CHECK(silt_dispose(&SL, no2, 1, 0, LATER, 1, true) == ZXM_E_STATE &&
              silt_disposable(&SL, 1, 1, 0, LATER, 1) == LPRES_STATE_FALSE &&
              silt_load(&SL2) == ZXM_OK && SL2.row[1].kind == SILT_DISPOSED,
          "silt: disposed of once, and so registered");

    /* SM7: versions, measures and the backlog */
    CHECK(silt_versions(&SL, no, &w) == ZXM_E_STATE &&
              silt_set_dest(&SL, "relative") == ZXM_E_ARG &&
              silt_set_dest(&SL, NULL) == ZXM_E_ARG && silt_set_dest(&SL, dest) == ZXM_OK,
          "silt: the backup's destination");
    {
        char p[400];
        snprintf(p, sizeof p, "{\"dest\":\"%s\"}", dest);
        CHECK(raw_call("backup.run", p) == ZXM_OK && silt_versions(&SL, no, &w) == ZXM_OK &&
                  w == 1 && silt_versions(&SL, Y + 99, &w) == ZXM_E_ARG,
              "silt: the deed's versions: %s", LINK.err);
    }
    int64_t deed = (int64_t) strlen(DEED), pdf = (int64_t) strlen(PDF);
    CHECK(silt_series_bytes(&SL, 3, &v) == ZXM_OK && v == 2 * deed + pdf &&
              silt_series_bytes(&SL, 4, &v) == ZXM_OK && v == 0 &&
              silt_series_bytes(&SL, 10000, &v) == ZXM_E_ARG &&
              silt_series_share(&SL, 3) == 10000 && silt_series_share(&SL, 4) == 0 &&
              silt_count(&SL, SILT_FILED) == 2 && silt_count(&SL, SILT_DISPOSED) == 1 &&
              silt_grid(&SL, 0) == 3 && silt_grid(&SL, 3) == 0,
          "silt: the series' bytes and share, the counts by kind, the filing grid");
    {
        static const int64_t in[3] = {3, 4, 5}, out[3] = {6, 6, 6}, flat[3] = {3, 4, 5};
        rat_t days;
        CHECK(silt_backlog(&SL, in, out, 3, 12, &days) == ZXM_OK && days.num == 6 &&
                  days.den == 1 && strstr(SL.notice, "6") &&
                  silt_backlog(&SL, in, flat, 3, 12, &days) == ZXM_E_ARG &&
                  silt_backlog(&SL, in, out, 0, 12, &days) == ZXM_E_ARG,
              "silt: the backlog clears in 6 days; one that does not fall is refused");
    }

    /* SM6: the trail */
    CHECK(silt_trail(&SL) == LPRES_STATE_TRUE, "silt: the trail holds: %s", SL.notice);
    int64_t keep = SL.row[2].prev;
    SL.row[2].prev = 0;
    CHECK(silt_trail(&SL) == LPRES_STATE_BOTH,
          "silt: a line out of its chain, the journal whole: held");
    SL.row[2].prev = keep;
    int64_t kj = SL.row[2].jseq;
    SL.row[2].jseq = INT64_C(999999999999);
    CHECK(silt_trail(&SL) == LPRES_STATE_BOTH, "silt: a stamp past the journal: held");
    SL.row[2].prev = 0;
    CHECK(silt_trail(&SL) == LPRES_STATE_FALSE, "silt: both broken: FALSE");
    SL.row[2].prev = keep, SL.row[2].jseq = kj;
    zjr_close();
    CHECK(silt_trail(&SL) == LPRES_STATE_TRUE && strstr(SL.notice, "holds"),
          "silt: no journal says nothing, the register alone stands");

    /* SM8: a signed copy, on its own link */
    char sig[sizeof SL_GOOD + 2];
    snprintf(sig, sizeof sig, "%s\n", SL_GOOD);
    CHECK(sl_write("silt-good.sig", sig) && sl_write("silt-forged.sig", "574545560100ff\n") &&
              sl_write("silt-junk.sig", "not hex at all"),
          "silt: three copies");
    zxm_link_init(&SL_LINK, sl_send, &SL_LINK);
    SL.link = &SL_LINK;
    CHECK(silt_check_signed(&SL, "silt-good.sig") == ZXM_OK && SL.sig_checked && SL.sig_valid &&
              SL.sig_bytes == 321 && strstr(SL.notice, "holds") && strstr(SL.notice, "Advice only"),
          "silt: the signed copy holds: %s", SL.notice);
    CHECK(silt_check_signed(&SL, "silt-forged.sig") == ZXM_OK && !SL.sig_valid &&
              strstr(SL.notice, "does not hold: signature"),
          "silt: a forged copy: %s", SL.notice);
    int verified = SL_VERIFIED;
    CHECK(silt_check_signed(&SL, "silt-junk.sig") == ZXM_E_ARG &&
              silt_check_signed(&SL, "missing.sig") == ZXM_E_ARG &&
              silt_check_signed(&SL, "") == ZXM_E_ARG && SL_VERIFIED == verified,
          "silt: a copy that is not whole hex bytes is never sent to web4.verify");
    SL.link = &LINK;

    /* made up replies: nothing out of bounds is taken */
    zxm_link_init(&FAKE, canned, &FAKE);
    SL.link = &FAKE;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"path\":\"x\",\"size\":4,\"offset\":0,"
             "\"next\":4,\"eof\":true,\"text\":\"abcd\",\"valid\":true,\"kind\":\"REQUEST\"}}";
    CHECK(silt_check_signed(&SL, "x.sig") == ZXM_E_PROTOCOL, "silt: a verdict with no size");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"entries\":7}}";
    CHECK(silt_inbox(&SL, "Inbox", &w) == ZXM_E_PROTOCOL, "silt: a listing that is no list");
    CANNED = NULL;
    SL.link = &LINK;

    /* SM1: a line that does not fit is refused on load */
    CHECK(sl_write(SILT_REGISTER, "2024000001\t3\t7\t1\t19970\t20000\t36\t1\t0\t0\t0\t1\tLND-1\t"
                                  "Inbox/deed.txt\n") &&
              silt_load(&SL2) == ZXM_OK && SL2.n == 1,
          "silt: a register line read");
    CHECK(sl_write(SILT_REGISTER,
                   "2024000001\t10000\t7\t1\t19970\t20000\t36\t1\t0\t0\t0\t1\tLND-1\t"
                   "Inbox/deed.txt\n") &&
              silt_load(&SL2) == ZXM_E_PROTOCOL && !SL2.loaded && SL2.n == 0,
          "silt: a series past 4 digits is refused");
    CHECK(sl_write(SILT_REGISTER, "2024000000\t3\t7\t1\t19970\t20000\t36\t1\t0\t0\t0\t1\tLND-1\t"
                                  "Inbox/deed.txt\n") &&
              silt_load(&SL2) == ZXM_E_PROTOCOL,
          "silt: register number 0 of a year is refused");
    CHECK(sl_write(SILT_REGISTER, "2024000001\t3\t7\t1\t19970\t20000\t36\t1\t0\t0\t0\t3\tLND-1\t"
                                  "Inbox/deed.txt\n") &&
              silt_load(&SL2) == ZXM_E_PROTOCOL,
          "silt: a kind that is neither filed nor disposed is refused");
    CHECK(sl_write(SILT_REGISTER, "2024000001\t3\t7\t1\t19970\t20000\t36\t1\t0\t0\t0\t1\tLND-1") &&
              silt_load(&SL2) == ZXM_E_PROTOCOL,
          "silt: a line with no end is refused");
    CHECK(rm_tree(dest) == 0 && rm_tree(jdir) == 0, "silt: cleanup");
}
