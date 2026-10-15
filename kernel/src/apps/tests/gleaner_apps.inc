/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Gleaner's model against zxvd's real dispatcher: files.list, files.read,
 * files.write and search.query on the scratch Hoard, web4.cid as zxvd runs
 * it, and aipi.put first with no blockstore (as here) and then through a
 * blockstore below that keeps what it was given. */
#include "gleaner_model.h"

static uint8_t GL_PUT[GLEANER_BYTES];
static uint32_t GL_PUT_LEN;
static bool GL_PRIV;
static int GL_PUTS;

static const char *gl_put(const uint8_t *data, uint32_t len, const char *path, bool priv,
                          zxvd_buf_t *out)
{
    if (path || len > sizeof GL_PUT) return "only bytes, at most 8000";
    memcpy(GL_PUT, data, len);
    GL_PUT_LEN = len;
    GL_PRIV = priv;
    GL_PUTS++;
    zxvd_buf_printf(out, "{\"cid\":\"zGleanerBlock%u\",\"bytes\":%u,\"private\":%s}", len, len,
                    priv ? "true" : "false");
    return NULL;
}

/* The ten rows: three classes, two sources, two personal rows, one line
 * ending in CR LF and no LF after the last. */
#define GL_HEAD "id,label,source,date,pii,note"
#define GL_R0   "1,cat,camera,20260103,0,tabby"
#define GL_R1   "2,dog,camera,20260110,1,owner Ann"
#define GL_R2   "3,cat,phone,20260201,0,grey"
#define GL_R3   "4,bird,phone,20260215,0,robin"
#define GL_R4   "5,dog,camera,20260301,0,beagle"
#define GL_R5   "6,cat,camera,20260302,1,owner Bo"
#define GL_R6   "7,bird,camera,20260410,0,wren"
#define GL_R7   "8,dog,phone,20260411,0,collie"
#define GL_R8   "9,cat,phone,20260520,0,black"
#define GL_R9   "10,bird,phone,20260601,0,finch"
static const char GL_CSV[] = GL_HEAD "\n" GL_R0 "\n" GL_R1 "\n" GL_R2 "\n" GL_R3 "\r\n" GL_R4
                                     "\n" GL_R5 "\n" GL_R6 "\n" GL_R7 "\n" GL_R8 "\n" GL_R9;

static bool gl_file_is(const char *rel, const char *want)
{
    static uint8_t buf[GLEANER_BYTES + 2];
    uint32_t got = 0;
    uint64_t size = 0;
    if (zxf_read(rel, 0, buf, sizeof buf, true, &got, &size) != 0) return false;
    return got == strlen(want) && size == got && !memcmp(buf, want, got);
}

static bool gl_refused(gleaner_t *g, const char *text, int32_t fault, uint32_t line)
{
    return gleaner_parse(g, "t.csv", text, (uint32_t) strlen(text)) == ZXM_E_ARG &&
           g->fault == fault && g->fault_line == line;
}

APPS_TEST(gleaner)
{
    static gleaner_t G;
    static char big[GLEANER_BYTES + 64], sel[GLEANER_BYTES + 2], first_cid[GLEANER_CID_MAX + 1];
    static const zxvd_aipi_ops_t AIPI = {gl_put, NULL, NULL, NULL, NULL};
    uint64_t size;
    gleaner_init(&G, &LINK);

    /* GM1: what is not a dataset, refused before anything is kept */
    CHECK(gl_refused(&G, "id,label,date\n1,a,20260101", GLEANER_F_HEADER, 1) &&
              gl_refused(&G, "label,source,date,label\na,b,20260101,c", GLEANER_F_HEADER, 1) &&
              gl_refused(&G, "label,,source,date\na,x,b,20260101", GLEANER_F_HEADER, 1),
          "a header without source, with a column twice, with an empty name");
    CHECK(gl_refused(&G, "label,source,date\na,b,20260101\na,b", GLEANER_F_RAGGED, 3) &&
              gl_refused(&G, "label,source,date\n\"a\",b,20260101", GLEANER_F_FIELD, 2) &&
              gl_refused(&G, "label,source,date\n,b,20260101", GLEANER_F_FIELD, 2) &&
              gl_refused(&G, "label,source,date\na,b\t,20260101", GLEANER_F_FIELD, 2) &&
              gl_refused(&G, "label,source,date\na,b,2026011", GLEANER_F_DATE, 2) &&
              gl_refused(&G, "label,source,date\na,b,20261301", GLEANER_F_DATE, 2) &&
              gl_refused(&G, "label,source,date\na,b,20260100", GLEANER_F_DATE, 2) &&
              gl_refused(&G, "label,source,date,pii\na,b,20260101,2", GLEANER_F_PII, 2) &&
              gl_refused(&G, "label,source,date\nabcdefghijabcdefghijabcdefghijab,b,20260101",
                         GLEANER_F_FIELD, 2),
          "ragged, quoted, empty, a control byte, dates that are none, a pii of 2, a label of 32");
    CHECK(gl_refused(&G, "label,source,date\n", GLEANER_F_SIZE, 1) &&
              gleaner_parse(&G, "t.csv", "", 0) == ZXM_E_ARG && G.fault == GLEANER_F_SIZE &&
              G.nrows == 0,
          "no rows (GR3)");
    {
        uint32_t o = 0;
        memcpy(big, "label,source,date", 17), o = 17;
        for (uint32_t c = 0; c < 30; c++) o += (uint32_t) snprintf(big + o, 8, ",c%u", c);
        CHECK(gl_refused(&G, big, GLEANER_F_SIZE, 1), "33 columns (GR3)");
        o = (uint32_t) snprintf(big, sizeof big, "label,source,date\n");
        for (uint32_t r = 0; r < 65; r++)
            o += (uint32_t) snprintf(big + o, sizeof big - o, "k%u,s,20260101\n", r);
        CHECK(gl_refused(&G, big, GLEANER_F_CLASSES, 66), "a 65th label");
        o = (uint32_t) snprintf(big, sizeof big, "label,source,date\n");
        while (o + 13u <= GLEANER_BYTES) o += (uint32_t) snprintf(big + o, 14, "a,s,20260101\n");
        CHECK(gleaner_parse(&G, "t.csv", big, o) == ZXM_OK && G.nrows == (o - 18u) / 13u &&
                  G.nlabels == 1 && G.count[0] == G.nrows && G.personal == G.nrows && !G.has_pii,
              "every byte but a few: taken, every row personal with no pii column (GM2)");
        memcpy(big + o, "a,s,20260101\n", 14);
        CHECK(gl_refused(&G, big, GLEANER_F_SIZE, 0) && G.nrows == (o - 18u) / 13u,
              "8001 bytes and more refused (GR3); the dataset open stays");
    }

    /* files.list and search.query find the CSV files */
    CHECK(zxf_mkdir("gleaner") == 0 && zxf_mkdir("gleaner/old.csv") == 0 &&
              zxf_write("gleaner/pets.csv", (const uint8_t *) GL_CSV, sizeof GL_CSV - 1u,
                        ZXF_W_REPLACE, false, &size) == 0 &&
              zxf_write("gleaner/pets notes.txt", (const uint8_t *) "pets", 4, ZXF_W_REPLACE, false,
                        &size) == 0,
          "files");
    CHECK(gleaner_list(&G, "gleaner") == ZXM_OK && G.nfiles == 1 &&
              !strcmp(G.file[0].path, "gleaner/pets.csv") && G.file[0].size == sizeof GL_CSV - 1u,
          "listed: the CSV file, not the folder or the note: %s", LINK.err);
    CHECK(gleaner_find(&G, "pets") == ZXM_OK && G.nfiles == 1 &&
              !strcmp(G.file[0].path, "gleaner/pets.csv"),
          "found: %u %s", G.nfiles, LINK.err);
    int before = sent;
    CHECK(gleaner_find(&G, " \t") == ZXM_E_ARG && gleaner_list(&G, NULL) == ZXM_E_ARG &&
              gleaner_open(&G, "") == ZXM_E_ARG && sent == before,
          "nothing asked for a blank search, no folder or no path");
    CHECK(gleaner_open(&G, "gleaner/none.csv") == ZXM_E_REMOTE && LINK.err_code == ZXVD_E_FILES,
          "a file that is not there: zxvd says so");

    /* files.read: the dataset */
    CHECK(gleaner_open(&G, "gleaner/pets.csv") == ZXM_OK && G.nrows == 10 && G.cols == 6 &&
              G.has_pii && G.nlabels == 3 && G.nsources == 2 && G.nkept == 10 && G.share == -1,
          "opened: %s", LINK.err);
    int64_t cat = gleaner_label_of(&G, "cat"), dog = gleaner_label_of(&G, "dog"),
            bird = gleaner_label_of(&G, "bird"), cam = gleaner_source_of(&G, "camera"),
            phone = gleaner_source_of(&G, "phone");
    CHECK(cat == 0 && dog == 1 && bird == 2 && cam == 0 && phone == 1 &&
              gleaner_label_of(&G, "fish") == -1 && gleaner_source_of(&G, NULL) == -1,
          "labels and sources in order of first appearance");
    CHECK(G.count[0] == 4 && G.count[1] == 3 && G.count[2] == 3 && G.personal == 2 &&
              G.row[3].len == strlen(GL_R3) && G.row[1].pii && !G.row[0].pii &&
              G.row[9].date == 20260601,
          "the rows of each class (witness.kind_count), two personal, the CR dropped");

    /* GM4: the split by row index, 20 % */
    CHECK(gleaner_split(&G, 10001) == ZXM_E_ARG && gleaner_split(&G, -1) == ZXM_E_ARG &&
              gleaner_split(&G, 2000) == ZXM_OK && G.share == 2000 && G.nkept_test == 2 &&
              G.row[4].test && G.row[9].test && !G.row[0].test && G.count_test[0] == 0 &&
              G.count_test[1] == 1 && G.count_test[2] == 1,
          "split 20 %%: rows 5 and 10 are the test set");
    CHECK(gleaner_save(&G, "gleaner/train.csv", "gleaner/train.csv") == ZXM_E_ARG &&
              gleaner_save(&G, "gleaner/train.csv", "gleaner/test.csv") == ZXM_OK &&
              gl_file_is("gleaner/train.csv",
                         GL_HEAD "\n" GL_R0 "\n" GL_R1 "\n" GL_R2 "\n" GL_R3 "\n" GL_R5 "\n" GL_R6
                                 "\n" GL_R7 "\n" GL_R8 "\n") &&
              gl_file_is("gleaner/test.csv", GL_HEAD "\n" GL_R4 "\n" GL_R9 "\n"),
          "saved: every row once, in two files: %s", LINK.err);
    CHECK(gleaner_save(&G, "gleaner/train2.csv", "gleaner/test.csv") == ZXM_E_REMOTE &&
              LINK.err_code == ZXVD_E_FILES &&
              gl_file_is("gleaner/test.csv", GL_HEAD "\n" GL_R4 "\n" GL_R9 "\n"),
          "never over a file that is there: %s", LINK.err);

    /* GM3: filters (GR2) */
    CHECK(gleaner_filter(&G, -1, cam, 20260101, 20261231) == ZXM_OK && G.nkept == 5 &&
              G.count[0] == 2 && G.count[1] == 2 && G.count[2] == 1 && G.nkept_test == 1 &&
              G.personal == 2,
          "from the camera");
    CHECK(gleaner_filter(&G, cat, -1, 20260201, 20260531) == ZXM_OK && G.nkept == 3 &&
              G.count[0] == 3 && G.count[1] == 0 && G.personal == 1 && G.nkept_test == 0,
          "cats from February to May");

    /* GM5: naming keeps it in; leaving needs the owner's word */
    CHECK(gleaner_name(&G) == ZXM_OK && !strncmp(G.cid, "bafkrei", 7), "named: %s %s", G.cid,
          LINK.err);
    memcpy(first_cid, G.cid, sizeof first_cid);
    before = sent;
    CHECK(gleaner_publish(&G) == ZXM_E_STATE && sent == before && !G.block[0],
          "a personal row without consent: nothing leaves, nothing sent");
    gleaner_consent(&G, true);
    CHECK(G.consent && gleaner_publish(&G) == ZXM_E_REMOTE && LINK.err_code == -32601,
          "with consent, but no blockstore here: zxvd says so: %s", LINK.err);
    zxvd_rpc_set_aipi_ops(&AIPI);
    int32_t n = gleaner_selection(&G, sel, sizeof sel);
    CHECK(n == (int32_t) strlen(GL_HEAD "\n" GL_R2 "\n" GL_R5 "\n" GL_R8 "\n") &&
              !memcmp(sel, GL_HEAD "\n" GL_R2 "\n" GL_R5 "\n" GL_R8 "\n", (size_t) n) &&
              gleaner_publish(&G) == ZXM_OK && GL_PUTS == 1 && GL_PRIV && G.sealed &&
              GL_PUT_LEN == (uint32_t) n && !memcmp(GL_PUT, sel, (size_t) n) &&
              !strncmp(G.block, "zGleanerBlock", 13),
          "published with consent, sealed at rest: %s", LINK.err);
    CHECK(gleaner_filter(&G, -1, phone, 0, 99999999) == ZXM_OK && !G.consent && !G.cid[0] &&
              !G.block[0] && G.personal == 0 && G.nkept == 5,
          "a new selection takes the consent back");
    CHECK(gleaner_name(&G) == ZXM_OK && strcmp(G.cid, first_cid) && gleaner_publish(&G) == ZXM_OK &&
              GL_PUTS == 2 && !GL_PRIV && !G.sealed &&
              GL_PUT_LEN ==
                  strlen(GL_HEAD "\n" GL_R2 "\n" GL_R3 "\n" GL_R7 "\n" GL_R8 "\n" GL_R9 "\n"),
          "nothing personal: it may go without consent, not sealed: %s", LINK.err);
    CHECK(gleaner_filter(&G, cat, cam, 20260101, 20260131) == ZXM_OK && G.nkept == 1 &&
              G.personal == 0 && gleaner_filter(&G, dog, cam, 20260101, 20260131) == ZXM_OK &&
              G.personal == 1 && gleaner_publish(&G) == ZXM_E_STATE && GL_PUTS == 2,
          "Ann's row alone: held back");
    gleaner_consent(&G, false);
    CHECK(!G.consent && gleaner_publish(&G) == ZXM_E_STATE && GL_PUTS == 2, "the owner said no");

    /* a dataset with no pii column: every row is personal (GM2) */
    CHECK(zxf_write("gleaner/plain.csv", (const uint8_t *) "label,source,date\nx,y,20260101\n", 31,
                    ZXF_W_REPLACE, false, &size) == 0 &&
              gleaner_open(&G, "gleaner/plain.csv") == ZXM_OK && G.personal == 1 &&
              gleaner_publish(&G) == ZXM_E_STATE && GL_PUTS == 2,
          "no pii column: held back until the owner says yes");
    gleaner_consent(&G, true);
    CHECK(gleaner_publish(&G) == ZXM_OK && GL_PUTS == 3 && GL_PRIV, "then sealed");
    zxvd_rpc_set_aipi_ops(NULL);

    /* a long set goes in parts (begin, add, end) */
    {
        uint32_t o = (uint32_t) snprintf(big, sizeof big, "label,source,date,pii\n");
        for (uint32_t r = 0; o + 24u <= GLEANER_BYTES; r++)
            o += (uint32_t) snprintf(big + o, 25, "k%02u,src,2026%02u%02u,0\n", r % 50u,
                                     1u + r % 12u, 1u + r % 28u);
        CHECK(zxf_write("gleaner/long.csv", (const uint8_t *) big, o, ZXF_W_REPLACE, false,
                        &size) == 0 &&
                  gleaner_open(&G, "gleaner/long.csv") == ZXM_OK && G.nlabels == 50 &&
                  gleaner_split(&G, 0) == ZXM_OK && G.nkept_test == 0 &&
                  gleaner_save(&G, "gleaner/long-train.csv", "gleaner/long-test.csv") == ZXM_OK,
              "a long dataset: %s", LINK.err);
        big[o] = 0;
        CHECK(gl_file_is("gleaner/long-train.csv", big) &&
                  gl_file_is("gleaner/long-test.csv", "label,source,date,pii\n"),
              "written whole in parts; an empty test set keeps its header");
        memset(big + o, 'x', GLEANER_BYTES + 1u - o); /* one more row, a byte too long */
        int rc = zxf_write("gleaner/huge.csv", (const uint8_t *) big, GLEANER_BYTES + 1u,
                           ZXF_W_REPLACE, false, &size);
        CHECK(rc == 0 && (rc = gleaner_open(&G, "gleaner/huge.csv")) == ZXM_E_TOOBIG &&
                  G.fault == GLEANER_F_SIZE && G.nlabels == 50,
              "a file past 8000 bytes is not read in (GR3): %d %s", rc, LINK.err);
    }
    gleaner_init(&G, &LINK);
    before = sent;
    CHECK(gleaner_split(&G, 10) == ZXM_E_STATE && gleaner_filter(&G, 0, 0, 0, 0) == ZXM_E_STATE &&
              gleaner_save(&G, "a.csv", "b.csv") == ZXM_E_STATE &&
              gleaner_name(&G) == ZXM_E_STATE && gleaner_publish(&G) == ZXM_E_STATE &&
              gleaner_selection(&G, sel, sizeof sel) == -1 && sent == before,
          "no dataset: nothing to do, nothing sent");
}
