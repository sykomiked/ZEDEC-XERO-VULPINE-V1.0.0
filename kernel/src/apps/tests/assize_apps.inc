/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Assize's model against zxvd's real dispatcher (LINK): a case opened and
 * kept in the test's scratch Hoard (files.read, files.mkdir, files.write)
 * under a journal on disk (journal.query), filings and evidence lodged
 * (files.stat, a PDF's head and tail by files.read) only when accepted and
 * on the owner's word, two witnesses who place one event at two sites held
 * BOTH and in escrow, never merged, until the arbiter's ruling is
 * recorded, a hearing and an annotation, the trail (journal.verify) and
 * the measures. */
#include "assize_model.h"

static assize_t AZ, AZ2;

static bool az_write(const char *path, const char *text)
{
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", path);
    zxm_p_str(&LINK, "text", text);
    return zxm_call(&LINK) == ZXM_OK;
}

static bool az_mkdir(const char *path)
{
    zxm_begin(&LINK, "files.mkdir");
    zxm_p_str(&LINK, "path", path);
    return zxm_call(&LINK) == ZXM_OK;
}

APPS_TEST(assize)
{
    const int64_t DAY = 20000;      /* 2024-10-04 */
    const int64_t DUE = DAY + 30;   /* the respondent's deadline */
    const int64_t CASE = 178;       /* the case number */
    const int64_t D = CASE * 10000; /* its docket numbers */
    static const char *PDF = "%PDF-1.4\n1 0 obj\n<< /Type /Catalog >>\nendobj\nxref\n0 2\n"
                             "0000000000 65535 f \n0000000009 00000 n \ntrailer\n<< /Size 2 >>\n"
                             "startxref\n46\n%%EOF\n";
    char jdir[256];
    int64_t no = 0, left = 0;

    /* the rules, and AR6's join */
    static const int64_t sites[2] = {11, 12};
    CHECK(assize_filing_accepted(1, 0, 1, 0) && !assize_filing_accepted(1, -1, 1, 0) &&
              !assize_filing_accepted(0, 5, 1, 0) && assize_docket_number(CASE, 2) == D + 2 &&
              assize_next_seq(41) == 42 && assize_says(11, 0) == ASSIZE_SILENT &&
              assize_sites_state(11, sites, 2) == LPRES_STATE_BOTH,
          "assize: the rules");

    snprintf(jdir, sizeof jdir, "%s/zxv-assizej-XXXXXX",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    CHECK(mkdtemp(jdir) != NULL && zjr_open(jdir) == 0, "assize: a journal");

    /* nothing before a load */
    assize_init(&AZ, &LINK);
    int before = sent;
    CHECK(assize_lodge(&AZ, ASSIZE_FILING, 1, DAY, 3, 0, 4, 11, "Filings/complaint.txt", true,
                       &no) == ZXM_E_STATE &&
              assize_rule(&AZ, 4, ASSIZE_UPHELD, DAY, "Rulings/ruling.txt", true) == ZXM_E_STATE &&
              assize_hearing(&AZ, DAY + 9, DAY, true) == ZXM_E_STATE &&
              assize_annotate(&AZ, 2, 1, 0, 0, 1, DAY, true) == ZXM_E_STATE &&
              assize_respond_left(&AZ, DAY, &left) == ZXM_E_STATE && sent == before,
          "assize: nothing asked before a case is open");
    CHECK(assize_load(&AZ, CASE) == ZXM_E_STATE && !AZ.loaded && strstr(AZ.notice, "No such"),
          "assize: no case yet: %s", AZ.notice);
    CHECK(assize_load(&AZ, 0) == ZXM_E_ARG && assize_load(&AZ, 1000000) == ZXM_E_ARG,
          "assize: a case number outside 1..999999");

    /* AM2: opening, refused before anything is sent */
    before = sent;
    CHECK(assize_open(&AZ, CASE, 1, 1, DAY, DUE, true) == ZXM_E_ARG &&
              assize_open(&AZ, CASE, 1, 2, DAY, DAY - 1, true) == ZXM_E_ARG &&
              assize_open(&AZ, 0, 1, 2, DAY, DUE, true) == ZXM_E_ARG &&
              assize_open(&AZ, 1000000, 1, 2, DAY, DUE, true) == ZXM_E_ARG &&
              assize_open(&AZ, CASE, 0, 2, DAY, DUE, true) == ZXM_E_ARG &&
              assize_open(&AZ, CASE, 1, 100, DAY, DUE, true) == ZXM_E_ARG &&
              assize_open(&AZ, CASE, 1, 2, DAY, DAY + ASSIZE_LEFT_MAX + 1, true) == ZXM_E_ARG &&
              sent == before && !AZ.loaded,
          "assize: an opening form not ready is refused before anything is sent");
    CHECK(assize_open(&AZ, CASE, 1, 2, DAY, DUE, false) == ZXM_E_STATE && sent == before &&
              !AZ.loaded,
          "assize: without the owner's word nothing is sent and nothing kept");
    CHECK(assize_open(&AZ, CASE, 1, 2, DAY, DUE, true) == ZXM_OK && AZ.loaded && AZ.n == 1 &&
              AZ.line[0].kind == ASSIZE_OPENED && AZ.line[0].party == 1 && AZ.line[0].other == 2 &&
              AZ.line[0].due == DUE && AZ.line[0].jseq > 0 &&
              !strcmp(AZ.file, "Assize/case-000178.txt"),
          "assize: case 178 opened: %s", LINK.err);
    CHECK(assize_open(&AZ, CASE, 3, 4, DAY, DUE, true) == ZXM_E_STATE &&
              strstr(AZ.notice, "already open") && AZ.n == 1 && AZ.line[0].party == 1,
          "assize: a case number is never reused: %s", AZ.notice);

    /* the documents in the Hoard */
    CHECK(az_mkdir("Filings") && az_mkdir("Evidence") && az_mkdir("Rulings") &&
              az_write("Filings/complaint.txt", "The goods were delivered at the north gate.") &&
              az_write("Filings/answer.pdf", PDF) &&
              az_write("Filings/broken.pdf", "%PDF-1.4\nno trailer here\n") &&
              az_write("Evidence/w1.txt", "I saw the cart at the north gate.") &&
              az_write("Evidence/w2.txt", "The cart stood at the mill all day.") &&
              az_write("Evidence/w3.txt", "A third account.") &&
              az_write("Rulings/ruling.txt", "Ruling of the arbiter on claim 4."),
          "assize: the documents");

    /* AM3: lodging, refused before anything is sent */
    before = sent;
    CHECK(assize_lodge(&AZ, ASSIZE_FILING, 3, DAY, 3, 0, 4, 11, "Filings/complaint.txt", true,
                       &no) == ZXM_E_STATE &&
              strstr(AZ.notice, "not lodged by a party") && sent == before,
          "assize: a stranger to the case is not accepted: %s", AZ.notice);
    CHECK(assize_lodge(&AZ, ASSIZE_FILING, 1, DUE + 1, 3, 0, 4, 11, "Filings/complaint.txt", true,
                       &no) == ZXM_E_STATE &&
              strstr(AZ.notice, "deadline has passed") &&
              assize_lodge(&AZ, ASSIZE_FILING, 1, DAY, ASSIZE_PAGES_MAX + 1, 0, 4, 11,
                           "Filings/complaint.txt", true, &no) == ZXM_E_STATE &&
              assize_lodge(&AZ, ASSIZE_FILING, 1, DAY, 3, ASSIZE_EXHIBITS_MAX + 1, 4, 11,
                           "Filings/complaint.txt", true, &no) == ZXM_E_STATE &&
              sent == before && AZ.n == 1,
          "assize: late, too long or too many exhibits: not accepted, nothing sent");
    CHECK(assize_lodge(&AZ, ASSIZE_FILING, 1, DAY, 3, 0, 4, 11, "../complaint.txt", true, &no) ==
                  ZXM_E_ARG &&
              assize_lodge(&AZ, ASSIZE_FILING, 1, DAY, 3, 0, 4, 11, "Assize/case-000178.txt", true,
                           &no) == ZXM_E_ARG &&
              assize_lodge(&AZ, ASSIZE_FILING, 1, DAY, 3, 0, 4, 11, NULL, true, &no) == ZXM_E_ARG &&
              assize_lodge(&AZ, ASSIZE_FILING, 1, DAY, 3, 0, 4, 11, "", true, &no) == ZXM_E_ARG &&
              assize_lodge(&AZ, ASSIZE_FILING, 1, DAY, 3, 0, 4, 0, "Filings/complaint.txt", true,
                           &no) == ZXM_E_ARG &&
              assize_lodge(&AZ, ASSIZE_FILING, 1, DAY, 3, 0, 0, 11, "Filings/complaint.txt", true,
                           &no) == ZXM_E_ARG &&
              assize_lodge(&AZ, ASSIZE_FILING, 1, DAY - 1, 3, 0, 4, 11, "Filings/complaint.txt",
                           true, &no) == ZXM_E_ARG &&
              assize_lodge(&AZ, ASSIZE_EVIDENCE, 1, DAY, 3, 0, 4, 11, "Evidence/w1.txt", true,
                           &no) == ZXM_E_ARG &&
              assize_lodge(&AZ, ASSIZE_HEARING, 1, DAY, 3, 0, 4, 11, "Filings/complaint.txt", true,
                           &no) == ZXM_E_ARG &&
              assize_lodge(&AZ, ASSIZE_FILING, 1, DAY, 3, 0, 4, 11, "Filings/complaint.txt", true,
                           NULL) == ZXM_E_ARG &&
              sent == before && AZ.n == 1,
          "assize: a form not ready, or evidence on no asserted claim, is refused before anything "
          "is sent");
    CHECK(assize_lodge(&AZ, ASSIZE_FILING, 1, DAY, 3, 0, 4, 11, "Filings/complaint.txt", false,
                       &no) == ZXM_E_STATE &&
              sent == before && AZ.n == 1,
          "assize: without the owner's word nothing is sent and nothing kept");

    /* AM3: lodged */
    CHECK(assize_lodge(&AZ, ASSIZE_FILING, 1, DAY, 3, 0, 4, 11, "Filings/complaint.txt", true,
                       &no) == ZXM_OK &&
              no == D + 2 && AZ.n == 2 && AZ.line[1].seq == 2 && AZ.line[1].prev == 1 &&
              AZ.line[1].jseq >= AZ.line[0].jseq && AZ.line[1].site == 11,
          "assize: the complaint lodged as %lld: %s", (long long) no, LINK.err);
    CHECK(assize_lodge(&AZ, ASSIZE_FILING, 2, DUE, 1, 2, 5, 21, "Filings/answer.pdf", true, &no) ==
                  ZXM_OK &&
              no == D + 3 && AZ.n == 3,
          "assize: the answer, a well-formed PDF, on the last day: %s", LINK.err);
    CHECK(assize_lodge(&AZ, ASSIZE_FILING, 2, DAY, 1, 0, 6, 21, "Filings/broken.pdf", true, &no) ==
                  ZXM_E_ARG &&
              AZ.n == 3 && strstr(AZ.notice, "well-formed PDF"),
          "assize: a PDF with no startxref is not lodged: %s", AZ.notice);
    CHECK(
        assize_lodge(&AZ, ASSIZE_FILING, 2, DAY, 1, 0, 6, 21, "Filings", true, &no) == ZXM_E_ARG &&
            assize_lodge(&AZ, ASSIZE_FILING, 2, DAY, 1, 0, 6, 21, "Filings/none.txt", true, &no) ==
                ZXM_E_ARG &&
            AZ.n == 3,
        "assize: a folder or a missing file is no document");
    CHECK(assize_lodge(&AZ, ASSIZE_FILING, 1, DAY, 3, 0, 7, 11, "Filings/complaint.txt", true,
                       &no) == ZXM_E_ARG &&
              assize_lodge(&AZ, ASSIZE_FILING, 1, DAY, 3, 0, 4, 12, "Evidence/w3.txt", true, &no) ==
                  ZXM_E_ARG &&
              AZ.n == 3,
          "assize: one document once, and one claim asserted once");

    /* AM4: the evidence matrix, and the BOTH test */
    CHECK(assize_claim(&AZ, 4) == LPRES_STATE_NEITHER && !assize_escrowed(&AZ, 4),
          "assize: no evidence yet: NEITHER");
    CHECK(assize_lodge(&AZ, ASSIZE_EVIDENCE, 1, DAY + 1, 1, 0, 4, 11, "Evidence/w1.txt", true,
                       &no) == ZXM_OK &&
              no == D + 4 && assize_claim(&AZ, 4) == LPRES_STATE_TRUE && !assize_escrowed(&AZ, 4),
          "assize: witness 1 places the event at the asserted site: TRUE: %s", LINK.err);
    CHECK(assize_lodge(&AZ, ASSIZE_EVIDENCE, 2, DAY + 2, 1, 0, 4, 12, "Evidence/w2.txt", true,
                       &no) == ZXM_OK &&
              no == D + 5 && strstr(AZ.notice, "escrow"),
          "assize: witness 2 places it at another site: %s", AZ.notice);
    lpres_state_t st = assize_claim(&AZ, 4);
    CHECK(st == LPRES_STATE_BOTH && (int) st == 3 && assize_escrowed(&AZ, 4) &&
              AZ.line[3].site == 11 && AZ.line[4].site == 12 && AZ.line[3].claim == 4 &&
              AZ.line[4].claim == 4,
          "assize: one claim, two sites: BOTH (3), held in escrow, both kept, never merged");
    CHECK(assize_claim(&AZ, 5) == LPRES_STATE_NEITHER &&
              assize_claim(&AZ, 0) == LPRES_STATE_NEITHER &&
              assize_claims_of(&AZ, LPRES_STATE_BOTH) == 1 &&
              assize_claims_of(&AZ, LPRES_STATE_NEITHER) == 1 &&
              assize_claims_of(&AZ, LPRES_STATE_TRUE) == 0,
          "assize: claims by value: one BOTH, one NEITHER");
    CHECK(assize_count(&AZ, ASSIZE_FILING) == 2 && assize_count(&AZ, ASSIZE_EVIDENCE) == 2 &&
              assize_count(&AZ, ASSIZE_RULING) == 0,
          "assize: lines by kind");
    CHECK(assize_respond_left(&AZ, DAY, &left) == ZXM_OK && left == 30 &&
              assize_respond_left(&AZ, DUE + 1, &left) == ZXM_OK && left == -1 &&
              assize_respond_left(&AZ, -1, &left) == ZXM_E_ARG,
          "assize: days left to respond");

    /* AM6: a hearing and an annotation */
    before = sent;
    CHECK(assize_hearing(&AZ, DAY + 1, DAY + 2, true) == ZXM_E_ARG &&
              assize_hearing(&AZ, DAY + 9, DAY - 1, true) == ZXM_E_ARG &&
              assize_hearing(&AZ, DAY + 9, DAY + 2, false) == ZXM_E_STATE && sent == before,
          "assize: a hearing in the past, or without the owner's word, is refused");
    CHECK(assize_hearing(&AZ, DAY + 9, DAY + 2, true) == ZXM_OK && AZ.n == 6 &&
              AZ.line[5].kind == ASSIZE_HEARING && AZ.line[5].due == DAY + 9,
          "assize: the hearing set: %s", LINK.err);
    before = sent;
    CHECK(assize_annotate(&AZ, 2, 1, 0, 0, 1, DAY + 3, true) == ZXM_E_ARG &&
              assize_annotate(&AZ, 4, 2, 0, 0, 1, DAY + 3, true) == ZXM_E_ARG &&
              assize_annotate(&AZ, 4, 1, 0, 0, 4, DAY + 3, true) == ZXM_E_ARG &&
              assize_annotate(&AZ, 4, 1, 100000, 0, 1, DAY + 3, true) == ZXM_E_ARG &&
              assize_annotate(&AZ, 4, 1, 0, 0, 1, DAY + 3, false) == ZXM_E_STATE && sent == before,
          "assize: an annotation off the evidence or its page is refused");
    CHECK(assize_annotate(&AZ, 4, 1, 120, 700, BROADSHEET_HIGHLIGHT, DAY + 3, true) == ZXM_OK &&
              AZ.n == 7 && AZ.line[6].kind == ASSIZE_NOTE && AZ.line[6].other == 4 &&
              AZ.line[6].claim == 4 && AZ.line[6].x == 120,
          "assize: a highlight on witness 1's statement: %s", LINK.err);

    /* AM5: the arbiter's ruling */
    before = sent;
    CHECK(assize_rule(&AZ, 4, 3, DAY + 9, "Rulings/ruling.txt", true) == ZXM_E_ARG &&
              assize_rule(&AZ, 9, ASSIZE_UPHELD, DAY + 9, "Rulings/ruling.txt", true) ==
                  ZXM_E_ARG &&
              assize_rule(&AZ, 4, ASSIZE_UPHELD, DAY + 9, "", true) == ZXM_E_ARG &&
              assize_rule(&AZ, 4, ASSIZE_UPHELD, DAY + 9, "Evidence/w1.txt", true) == ZXM_E_ARG &&
              assize_rule(&AZ, 4, ASSIZE_UPHELD, DAY + 9, "Rulings/ruling.txt", false) ==
                  ZXM_E_STATE &&
              sent == before && assize_escrowed(&AZ, 4),
          "assize: a ruling not ready, or without the owner's word: nothing sent, still held");
    CHECK(assize_finding_follows(&AZ, 4, ASSIZE_UPHELD) == 1 &&
              assize_finding_follows(&AZ, 4, ASSIZE_DISMISSED) == 0,
          "assize: from BOTH, upheld follows and dismissed does not (advice)");
    CHECK(assize_rule(&AZ, 4, ASSIZE_DISMISSED, DAY + 9, "Rulings/ruling.txt", true) == ZXM_OK &&
              AZ.n == 8 && AZ.line[7].kind == ASSIZE_RULING && AZ.line[7].finding == 2 &&
              !assize_escrowed(&AZ, 4) && assize_claim(&AZ, 4) == LPRES_STATE_BOTH &&
              strstr(AZ.notice, "does not follow") && strstr(AZ.notice, "arbiter's"),
          "assize: the arbiter's dismissal recorded, the escrow released, the evidence still "
          "BOTH: %s",
          AZ.notice);
    CHECK(assize_rule(&AZ, 4, ASSIZE_UPHELD, DAY + 9, "Evidence/w3.txt", true) == ZXM_E_STATE &&
              assize_lodge(&AZ, ASSIZE_EVIDENCE, 1, DAY + 9, 1, 0, 4, 11, "Evidence/w3.txt", true,
                           &no) == ZXM_E_STATE &&
              AZ.n == 8 && assize_count(&AZ, ASSIZE_RULING) == 1,
          "assize: a claim is ruled once, and nothing more is lodged on it");

    /* AM1: another model reads the same case */
    assize_init(&AZ2, &LINK);
    CHECK(assize_load(&AZ2, CASE) == ZXM_OK && AZ2.n == 8 && AZ2.line[4].site == 12 &&
              !strcmp(AZ2.line[2].path, "Filings/answer.pdf") &&
              AZ2.line[3].jseq == AZ.line[3].jseq && assize_claim(&AZ2, 4) == LPRES_STATE_BOTH &&
              !assize_escrowed(&AZ2, 4),
          "assize: the case read back: %s", LINK.err);

    /* AM7: the trail */
    CHECK(assize_trail(&AZ) == LPRES_STATE_TRUE, "assize: the trail holds: %s", AZ.notice);
    int64_t keep = AZ.line[4].prev;
    AZ.line[4].prev = 1;
    CHECK(assize_trail(&AZ) == LPRES_STATE_BOTH,
          "assize: a line out of its chain, the journal whole: held");
    AZ.line[4].prev = keep;
    int64_t kj = AZ.line[4].jseq;
    AZ.line[4].jseq = INT64_C(999999999999);
    CHECK(assize_trail(&AZ) == LPRES_STATE_BOTH, "assize: a stamp past the journal: held");
    AZ.line[4].prev = 1;
    CHECK(assize_trail(&AZ) == LPRES_STATE_FALSE, "assize: both broken: FALSE");
    AZ.line[4].prev = keep, AZ.line[4].jseq = kj;
    zjr_close();
    CHECK(assize_trail(&AZ) == LPRES_STATE_TRUE && strstr(AZ.notice, "holds"),
          "assize: no journal says nothing, the docket alone stands");

    /* made up replies: nothing out of bounds is taken */
    zxm_link_init(&FAKE, canned, &FAKE);
    AZ.link = &FAKE;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"dir\":false,\"size\":-4}}";
    CHECK(assize_lodge(&AZ, ASSIZE_FILING, 1, DAY, 3, 0, 8, 11, "Filings/x.txt", true, &no) ==
                  ZXM_E_PROTOCOL &&
              AZ.n == 8,
          "assize: a size below zero is no size");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"entries\":7}}";
    CHECK(assize_hearing(&AZ, DAY + 20, DAY + 10, true) == ZXM_E_PROTOCOL && AZ.n == 8,
          "assize: a journal answer that is no list");
    CANNED = NULL;
    AZ.link = &LINK;

    /* AM1: a line that does not fit is refused on load */
    CHECK(az_write("Assize/case-000179.txt",
                   "1\t0\t1\t1\t2\t20000\t20030\t0\t0\t0\t0\t0\t0\t0\t0\t0"
                   "\t0\t-\n") &&
              assize_load(&AZ2, 179) == ZXM_OK && AZ2.n == 1,
          "assize: an opening line read");
    CHECK(az_write("Assize/case-000179.txt", "1\t0\t1\t100\t2\t20000\t20030\t0\t0\t0\t0\t0\t0\t0\t0"
                                             "\t0\t0\t-\n") &&
              assize_load(&AZ2, 179) == ZXM_E_PROTOCOL && !AZ2.loaded && AZ2.n == 0,
          "assize: a party past 2 digits is refused");
    CHECK(az_write("Assize/case-000179.txt",
                   "1\t0\t2\t1\t2\t20000\t20030\t0\t0\t0\t0\t0\t0\t0\t0\t0"
                   "\t0\t-\n") &&
              assize_load(&AZ2, 179) == ZXM_E_PROTOCOL,
          "assize: a docket that does not begin with its opening is refused");
    CHECK(az_write("Assize/case-000179.txt",
                   "1\t0\t7\t1\t2\t20000\t20030\t0\t0\t0\t0\t0\t0\t0\t0\t0"
                   "\t0\t-\n") &&
              assize_load(&AZ2, 179) == ZXM_E_PROTOCOL,
          "assize: a kind past 6 is refused");
    CHECK(az_write("Assize/case-000179.txt",
                   "1\t0\t1\t1\t2\t20000\t20030\t0\t0\t0\t0\t0\t0\t0\t0\t0"
                   "\t0\t-") &&
              assize_load(&AZ2, 179) == ZXM_E_PROTOCOL,
          "assize: a line with no end is refused");
    CHECK(rm_tree(jdir) == 0, "assize: cleanup");
}
