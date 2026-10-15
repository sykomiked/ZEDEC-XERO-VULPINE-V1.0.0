/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_print.c — the IPP and eSCL codecs (ipp.h, escl.h): requests byte by
 * byte, replies a printer or scanner sends, and damaged input. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "escl.h"
#include "ipp.h"

/* Appends to b at *k, never past cap - 1: *k stops at the end when the text
 * does not fit. */
static void catf(char *b, size_t cap, int *k, const char *fmt, ...)
{
    if (*k < 0 || (size_t) *k + 1u >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(b + *k, cap - (size_t) *k, fmt, ap);
    va_end(ap);
    if (r > 0) *k = (size_t) r < cap - (size_t) *k ? *k + r : (int) cap - 1;
}

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL %s:%d ", __FILE__, __LINE__);                                           \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static uint8_t buf[4096];

/* a reply as a printer would build it, with the same writer */
static uint32_t printer_reply(uint8_t *out, uint32_t cap)
{
    ipp_w_t w;
    ipp_w_init(&w, out, cap);
    ipp_w_head(&w, 0x0000, 7);
    ipp_w_group(&w, IPP_G_OPERATION);
    ipp_w_str(&w, IPP_T_CHARSET, "attributes-charset", "utf-8");
    ipp_w_str(&w, IPP_T_LANGUAGE, "attributes-natural-language", "en");
    ipp_w_group(&w, IPP_G_PRINTER);
    ipp_w_str(&w, IPP_T_NAME, "printer-name", "Hallway");
    ipp_w_str(&w, IPP_T_TEXT, "printer-info", "Upstairs\nhall");
    ipp_w_str(&w, IPP_T_TEXT, "printer-make-and-model", "Example LaserJet 400");
    ipp_w_str(&w, IPP_T_TEXT, "printer-location", "Hall");
    ipp_w_int(&w, IPP_T_ENUM, "printer-state", IPP_PRINTER_PROCESSING);
    ipp_w_str(&w, IPP_T_KEYWORD, "printer-state-reasons", "media-low-report");
    ipp_w_str(&w, IPP_T_KEYWORD, NULL, "toner-low-warning");
    ipp_w_bool(&w, "printer-is-accepting-jobs", true);
    ipp_w_bool(&w, "color-supported", false);
    ipp_w_int(&w, IPP_T_INTEGER, "queued-job-count", 2);
    ipp_w_str(&w, IPP_T_MIME, "document-format-supported", "application/pdf");
    ipp_w_str(&w, IPP_T_MIME, NULL, "image/jpeg");
    ipp_w_str(&w, IPP_T_MIME, NULL, "image/pwg-raster");
    /* a collection the summary skips whole */
    {
        uint8_t b[] = {IPP_T_BEGIN_COLLECTION,
                       0,
                       13,
                       'm',
                       'e',
                       'd',
                       'i',
                       'a',
                       '-',
                       'c',
                       'o',
                       'l',
                       '-',
                       'd',
                       'f',
                       't',
                       0,
                       0,
                       IPP_T_MEMBER_NAME,
                       0,
                       0,
                       0,
                       10,
                       'm',
                       'e',
                       'd',
                       'i',
                       'a',
                       '-',
                       's',
                       'i',
                       'z',
                       'e',
                       IPP_T_BEGIN_COLLECTION,
                       0,
                       0,
                       0,
                       0,
                       IPP_T_MEMBER_NAME,
                       0,
                       0,
                       0,
                       11,
                       'x',
                       '-',
                       'd',
                       'i',
                       'm',
                       'e',
                       'n',
                       's',
                       'i',
                       'o',
                       'n',
                       IPP_T_INTEGER,
                       0,
                       0,
                       0,
                       4,
                       0,
                       0,
                       0x52,
                       0x08,
                       IPP_T_END_COLLECTION,
                       0,
                       0,
                       0,
                       0,
                       IPP_T_END_COLLECTION,
                       0,
                       0,
                       0,
                       0};
        memcpy(w.p + w.len, b, sizeof b);
        w.len += sizeof b;
    }
    ipp_w_str(&w, IPP_T_KEYWORD, "media-supported", "iso_a4_210x297mm");
    ipp_w_str(&w, IPP_T_KEYWORD, NULL, "na_letter_8.5x11in");
    ipp_w_str(&w, IPP_T_KEYWORD, "media-default", "iso_a4_210x297mm");
    ipp_w_str(&w, IPP_T_KEYWORD, "sides-supported", "one-sided");
    ipp_w_str(&w, IPP_T_KEYWORD, NULL, "two-sided-long-edge");
    {
        uint8_t b[] = {IPP_T_RANGE, 0,   16,  'c', 'o', 'p', 'i', 'e', 's', '-',
                       's',         'u', 'p', 'p', 'o', 'r', 't', 'e', 'd', 0,
                       8,           0,   0,   0,   1,   0,   0,   0,   99};
        memcpy(w.p + w.len, b, sizeof b);
        w.len += sizeof b;
    }
    ipp_w_group(&w, IPP_G_END);
    return w.over ? 0 : w.len;
}

static void test_write(void)
{
    ipp_w_t w;
    ipp_target_t t = {"ipp://10.0.0.5:631/ipp/print", "ada"};
    ipp_w_init(&w, buf, sizeof buf);
    CHECK(ipp_req_printer(&w, 0x01020304, &t) == IPP_OK && !w.over, "printer request");
    static const uint8_t head[] = {2, 0, 0, 0x0B, 1, 2, 3, 4, IPP_G_OPERATION, IPP_T_CHARSET,
                                   0, 18};
    CHECK(!memcmp(buf, head, sizeof head) && !memcmp(buf + 12, "attributes-charset", 18) &&
              buf[30] == 0 && buf[31] == 5 && !memcmp(buf + 32, "utf-8", 5),
          "the head, byte by byte");
    ipp_r_t r;
    ipp_attr_t a;
    CHECK(ipp_r_init(&r, buf, w.len) == IPP_OK && r.major == 2 && r.minor == 0 &&
              r.status == IPP_OP_GET_PRINTER_ATTRIBUTES && r.request_id == 0x01020304,
          "read back the head");
    int n = 0, rc, wanted = 0;
    char v[64];
    while ((rc = ipp_next(&r, &a)) == 1) {
        n++;
        if (ipp_name_is(&a, "printer-uri")) {
            ipp_str(&a, v, sizeof v);
            CHECK(!strcmp(v, "ipp://10.0.0.5:631/ipp/print") && a.tag == IPP_T_URI, "uri %s", v);
        }
        if (ipp_name_is(&a, "requesting-user-name")) {
            ipp_str(&a, v, sizeof v);
            CHECK(!strcmp(v, "ada") && a.tag == IPP_T_NAME, "user");
        }
        if (a.tag == IPP_T_KEYWORD) wanted++;
    }
    CHECK(rc == 0 && n == 4 + 14 && wanted == 14 && r.done && ipp_r_end(&r) == w.len,
          "every attribute: %d, %d keywords", n, wanted);
    /* the user defaults to zxv */
    ipp_target_t anon = {"ipp://h/p", NULL};
    ipp_w_init(&w, buf, sizeof buf);
    CHECK(ipp_req_job(&w, IPP_OP_GET_JOB_ATTRIBUTES, 9, &anon, 42) == IPP_OK, "job request");
    ipp_r_init(&r, buf, w.len);
    int32_t id = 0;
    bool zxv = false;
    while (ipp_next(&r, &a) == 1) {
        if (ipp_name_is(&a, "job-id")) ipp_int(&a, &id);
        if (ipp_name_is(&a, "requesting-user-name")) {
            ipp_str(&a, v, sizeof v);
            zxv = !strcmp(v, "zxv");
        }
    }
    CHECK(id == 42 && zxv && r.status == IPP_OP_GET_JOB_ATTRIBUTES, "job id %d", id);
    anon.user = "";
    ipp_w_init(&w, buf, sizeof buf);
    ipp_req_job(&w, IPP_OP_CANCEL_JOB, 9, &anon, -1);
    ipp_r_init(&r, buf, w.len);
    while (ipp_next(&r, &a) == 1)
        if (ipp_name_is(&a, "job-id")) ipp_int(&a, &id);
    CHECK(id == -1 && r.status == IPP_OP_CANCEL_JOB, "negative ids survive");

    /* a print job: operation attributes, then the job template */
    ipp_job_opts_t o = {"Plan.pdf",   "application/pdf", 3, "two-sided-long-edge",
                        "monochrome", "iso_a4_210x297mm"};
    ipp_w_init(&w, buf, sizeof buf);
    CHECK(ipp_req_print_job(&w, 1, &t, &o) == IPP_OK, "print job");
    ipp_r_init(&r, buf, w.len);
    int seen = 0;
    while (ipp_next(&r, &a) == 1) {
        int32_t x;
        ipp_str(&a, v, sizeof v);
        if (ipp_name_is(&a, "job-name") && a.group == IPP_G_OPERATION && !strcmp(v, "Plan.pdf"))
            seen |= 1;
        if (ipp_name_is(&a, "document-format") && a.tag == IPP_T_MIME &&
            !strcmp(v, "application/pdf"))
            seen |= 2;
        if (ipp_name_is(&a, "copies") && a.group == IPP_G_JOB && ipp_int(&a, &x) && x == 3)
            seen |= 4;
        if (ipp_name_is(&a, "sides") && !strcmp(v, "two-sided-long-edge")) seen |= 8;
        if (ipp_name_is(&a, "print-color-mode") && !strcmp(v, "monochrome")) seen |= 16;
        if (ipp_name_is(&a, "media") && !strcmp(v, "iso_a4_210x297mm")) seen |= 32;
    }
    CHECK(seen == 63 && r.status == IPP_OP_PRINT_JOB, "job attributes: %d", seen);
    /* defaults: no job group at all */
    ipp_job_opts_t d = {NULL, NULL, 0, NULL, NULL, NULL};
    ipp_w_init(&w, buf, sizeof buf);
    CHECK(ipp_req_validate_job(&w, 1, &t, &d) == IPP_OK, "validate");
    ipp_r_init(&r, buf, w.len);
    bool job_group = false;
    n = 0;
    while (ipp_next(&r, &a) == 1) {
        n++;
        job_group = job_group || a.group == IPP_G_JOB;
    }
    CHECK(!job_group && n == 4 && r.status == IPP_OP_VALIDATE_JOB, "no template: %d", n);
    ipp_job_opts_t one = {"", "", 1, NULL, NULL, NULL};
    ipp_w_init(&w, buf, sizeof buf);
    ipp_req_validate_job(&w, 1, &t, &one);
    ipp_r_init(&r, buf, w.len);
    n = 0;
    while (ipp_next(&r, &a) == 1) {
        n++;
        job_group = job_group || a.group == IPP_G_JOB;
    }
    CHECK(job_group && n == 5, "copies alone: %d", n);

    /* no room: every size up to the full one fails, and nothing runs past */
    ipp_w_init(&w, buf, sizeof buf);
    ipp_req_print_job(&w, 1, &t, &o);
    uint32_t full = w.len;
    static uint8_t small[512];
    for (uint32_t cap = 0; cap < full; cap++) {
        memset(small, 0xEE, sizeof small);
        ipp_w_init(&w, small, cap);
        CHECK(ipp_req_print_job(&w, 1, &t, &o) == IPP_E_ROOM && w.len <= cap && small[cap] == 0xEE,
              "cap %u", cap);
    }
    ipp_w_init(&w, small, full);
    CHECK(ipp_req_print_job(&w, 1, &t, &o) == IPP_OK && w.len == full, "exactly enough");
    /* names and values past 65535 bytes cannot be written */
    static char huge[70000];
    memset(huge, 'x', sizeof huge - 1);
    static uint8_t big[140000];
    ipp_w_init(&w, big, sizeof big);
    ipp_w_str(&w, IPP_T_TEXT, "n", huge);
    CHECK(w.over && w.len == 0, "value too long");
    ipp_w_init(&w, big, sizeof big);
    ipp_w_str(&w, IPP_T_TEXT, huge, "v");
    CHECK(w.over && w.len == 0, "name too long");
    huge[65535] = 0;
    ipp_w_init(&w, big, sizeof big);
    ipp_w_str(&w, IPP_T_TEXT, "n", huge);
    CHECK(!w.over && w.len == 1 + 2 + 1 + 2 + 65535, "65535 bytes fit");
}

static void test_read(void)
{
    uint32_t n = printer_reply(buf, sizeof buf);
    CHECK(n > 0, "reply built");
    ipp_printer_t p;
    CHECK(ipp_parse_printer(buf, n, &p) == IPP_OK && p.status == 0 && IPP_STATUS_OK(p.status),
          "parsed");
    CHECK(!strcmp(p.name, "Hallway") && !strcmp(p.info, "Upstairs hall") &&
              !strcmp(p.make, "Example LaserJet 400") && !strcmp(p.where, "Hall"),
          "names: [%s] [%s]", p.name, p.info);
    CHECK(p.state == IPP_PRINTER_PROCESSING &&
              !strcmp(p.reasons, "media-low-report,toner-low-warning") && p.accepting && !p.color &&
              p.queued == 2,
          "state: %d [%s]", p.state, p.reasons);
    CHECK(p.nformats == 3 && !strcmp(p.formats[2], "image/pwg-raster") && p.nmedia == 2 &&
              !strcmp(p.media[1], "na_letter_8.5x11in") &&
              !strcmp(p.media_default, "iso_a4_210x297mm") && p.nsides == 2 &&
              !strcmp(p.sides[1], "two-sided-long-edge") && p.copies_max == 99 && p.more == 0,
          "lists: %u formats, %u media, copies %d", p.nformats, p.nmedia, p.copies_max);
    CHECK(ipp_takes(&p, "application/pdf") && ipp_takes(&p, "image/jpeg") &&
              !ipp_takes(&p, "text/plain"),
          "formats taken");
    CHECK(!strcmp(ipp_printer_state_name(p.state), "printing") &&
              !strcmp(ipp_printer_state_name(IPP_PRINTER_IDLE), "idle") &&
              !strcmp(ipp_printer_state_name(IPP_PRINTER_STOPPED), "stopped") &&
              !strcmp(ipp_printer_state_name(0), "unknown") &&
              !strcmp(ipp_printer_state_name(6), "unknown"),
          "printer state names");
    CHECK(!strcmp(ipp_job_state_name(IPP_JOB_PENDING), "pending") &&
              !strcmp(ipp_job_state_name(IPP_JOB_COMPLETED), "completed") &&
              !strcmp(ipp_job_state_name(IPP_JOB_ABORTED), "aborted") &&
              !strcmp(ipp_job_state_name(2), "unknown") &&
              !strcmp(ipp_job_state_name(10), "unknown"),
          "job state names");

    /* lists past what is kept are counted; a sniffing printer takes anything */
    {
        ipp_w_t w;
        static uint8_t m[8192];
        ipp_w_init(&w, m, sizeof m);
        ipp_w_head(&w, 0, 1);
        ipp_w_group(&w, IPP_G_PRINTER);
        ipp_w_str(&w, IPP_T_MIME, "document-format-supported", "application/octet-stream");
        for (int i = 0; i < 20; i++) ipp_w_str(&w, IPP_T_MIME, NULL, "image/x");
        ipp_w_int(&w, IPP_T_INTEGER, "printer-state",
                  3); /* INTEGER where ENUM is usual: read the same */
        char nm[160];
        memset(nm, 'x', 124);
        memcpy(nm + 124, "\xc3\xa9\xc3\xa9", 5); /* the second one does not fit in 127 */
        ipp_w_str(&w, IPP_T_TEXT, "printer-name", nm);
        ipp_w_group(&w, IPP_G_END);
        CHECK(ipp_parse_printer(m, w.len, &p) == IPP_OK && p.nformats == IPP_LIST && p.more == 5 &&
                  ipp_takes(&p, "text/plain") && p.state == 3 && p.queued == -1,
              "kept %u, %u more", p.nformats, p.more);
        CHECK(strlen(p.name) == 126 && !memcmp(p.name + 124, "\xc3\xa9", 2),
              "cut on a character: %zu", strlen(p.name));
    }

    /* a job reply */
    {
        ipp_w_t w;
        ipp_w_init(&w, buf, sizeof buf);
        ipp_w_head(&w, 0x0001, 3); /* successful-ok-ignored-or-substituted-attributes */
        ipp_w_group(&w, IPP_G_OPERATION);
        ipp_w_str(&w, IPP_T_TEXT, "status-message", "copies changed");
        ipp_w_group(&w, IPP_G_JOB);
        ipp_w_int(&w, IPP_T_INTEGER, "job-id", 512);
        ipp_w_int(&w, IPP_T_ENUM, "job-state", IPP_JOB_PROCESSING);
        ipp_w_str(&w, IPP_T_KEYWORD, "job-state-reasons", "job-printing");
        ipp_w_int(&w, IPP_T_INTEGER, "job-impressions-completed", 2);
        ipp_w_group(&w, IPP_G_UNSUPPORTED);
        ipp_w_int(&w, IPP_T_INTEGER, "copies", 300);
        ipp_w_group(&w, IPP_G_END);
        ipp_job_t j;
        CHECK(ipp_parse_job(buf, w.len, &j) == IPP_OK && j.status == 1 && IPP_STATUS_OK(j.status) &&
                  j.id == 512 && j.state == IPP_JOB_PROCESSING &&
                  !strcmp(j.reasons, "job-printing") && j.pages == 2 &&
                  !strcmp(j.message, "copies changed"),
              "job: %d %d [%s] [%s]", j.id, j.state, j.reasons, j.message);
        /* an error status, with the job's own message preferred */
        ipp_w_init(&w, buf, sizeof buf);
        ipp_w_head(&w, 0x040A, 3); /* client-error-document-format-not-supported */
        ipp_w_group(&w, IPP_G_OPERATION);
        ipp_w_str(&w, IPP_T_TEXT, "status-message", "unsupported format");
        ipp_w_group(&w, IPP_G_JOB);
        ipp_w_str(&w, IPP_T_TEXT, "job-state-message", "own words");
        ipp_w_group(&w, IPP_G_END);
        CHECK(ipp_parse_job(buf, w.len, &j) == IPP_OK && j.status == 0x040A &&
                  !IPP_STATUS_OK(j.status) && j.id == 0 && j.state == 0 && j.pages == -1 &&
                  !strcmp(j.message, "own words"),
              "error: %x [%s]", j.status, j.message);
    }
}

static void test_damage(void)
{
    uint32_t n = printer_reply(buf, sizeof buf);
    ipp_printer_t p;
    ipp_job_t j;
    /* every cut short of the end is refused, never read past */
    uint32_t refused = 0;
    for (uint32_t cut = 0; cut < n; cut++) {
        uint8_t *c = malloc(cut ? cut : 1);
        memcpy(c, buf, cut);
        if (ipp_parse_printer(c, cut, &p) == IPP_E_FORMAT &&
            ipp_parse_job(c, cut, &j) == IPP_E_FORMAT)
            refused++;
        free(c);
    }
    CHECK(refused == n, "%u of %u cuts refused", refused, n);
    /* a version that is not IPP */
    static uint8_t m[64];
    memcpy(m, buf, 32);
    m[0] = 0;
    CHECK(ipp_parse_printer(m, 32, &p) == IPP_E_FORMAT, "version 0");
    m[0] = 3;
    CHECK(ipp_parse_printer(m, 32, &p) == IPP_E_FORMAT, "version 3");
    m[0] = 1;
    ipp_r_t r;
    CHECK(ipp_r_init(&r, m, 8) == IPP_OK && r.major == 1, "1.1 is read");
    /* an attribute before any group */
    static const uint8_t nogroup[] = {2,          0, 0, 0,   0, 0, 0,        1,
                                      IPP_T_TEXT, 0, 1, 'a', 0, 0, IPP_G_END};
    CHECK(ipp_parse_printer(nogroup, sizeof nogroup, &p) == IPP_E_FORMAT, "no group");
    /* a delimiter 0 */
    static const uint8_t zero[] = {2, 0, 0, 0, 0, 0, 0, 1, 0, IPP_G_END};
    CHECK(ipp_parse_printer(zero, sizeof zero, &p) == IPP_E_FORMAT, "tag 0");
    /* another value with nothing before it */
    static const uint8_t orphan[] = {2,          0, 0, 0, 0, 0,   0,        1, IPP_G_PRINTER,
                                     IPP_T_TEXT, 0, 0, 0, 1, 'a', IPP_G_END};
    CHECK(ipp_parse_printer(orphan, sizeof orphan, &p) == IPP_E_FORMAT &&
              ipp_parse_job(orphan, sizeof orphan, &j) == IPP_E_FORMAT,
          "orphan value");
    /* a stray end of collection, a member name outside one, an open collection */
    static const uint8_t stray[] = {
        2, 0, 0, 0, 0, 0, 0, 1, IPP_G_PRINTER, IPP_T_END_COLLECTION, 0, 0, 0, 0, IPP_G_END};
    static const uint8_t member[] = {
        2, 0, 0, 0, 0, 0, 0, 1, IPP_G_PRINTER, IPP_T_MEMBER_NAME, 0, 0, 0, 1, 'a', IPP_G_END};
    static const uint8_t open_[] = {
        2, 0, 0, 0, 0, 0, 0, 1, IPP_G_PRINTER, IPP_T_BEGIN_COLLECTION, 0, 1, 'c', 0, 0, IPP_G_END};
    CHECK(ipp_parse_printer(stray, sizeof stray, &p) == IPP_E_FORMAT &&
              ipp_parse_printer(member, sizeof member, &p) == IPP_E_FORMAT &&
              ipp_parse_printer(open_, sizeof open_, &p) == IPP_E_FORMAT,
          "collections that do not close");
    /* wrong sizes are not numbers */
    ipp_attr_t a = {IPP_G_PRINTER, IPP_T_INTEGER, "x", 1, (const uint8_t *) "\0\0\0", 3};
    int32_t v = 77;
    CHECK(!ipp_int(&a, &v) && v == 77, "3-byte integer");
    a.tag = IPP_T_BOOLEAN;
    a.val_len = 2;
    CHECK(!ipp_int(&a, &v), "2-byte boolean");
    a.tag = IPP_T_TEXT;
    a.val_len = 1;
    CHECK(!ipp_int(&a, &v), "text");
    a.tag = IPP_T_ENUM;
    a.val = (const uint8_t *) "\xff\xff\xff\xfe";
    a.val_len = 4;
    CHECK(ipp_int(&a, &v) && v == -2, "enum");
    char s[4] = "zz";
    ipp_str(&a, s, 0);
    CHECK(!strcmp(s, "zz"), "no room, untouched");
    /* a status with only a header and an end */
    static const uint8_t bare[] = {2, 0, 0x04, 0x00, 0, 0, 0, 1, IPP_G_END};
    CHECK(ipp_parse_printer(bare, sizeof bare, &p) == IPP_OK && p.status == 0x0400 &&
              !IPP_STATUS_OK(p.status),
          "bare error");
    /* no end tag */
    CHECK(ipp_parse_printer(bare, sizeof bare - 1, &p) == IPP_E_FORMAT, "no end");
    /* a range with the wrong size is ignored */
    static const uint8_t range[] = {
        2,   0,   0,   0,   0,   0,   0,   1,   IPP_G_PRINTER, IPP_T_RANGE, 0,   16,  'c',
        'o', 'p', 'i', 'e', 's', '-', 's', 'u', 'p',           'p',         'o', 'r', 't',
        'e', 'd', 0,   4,   0,   0,   0,   9,   IPP_G_END};
    CHECK(ipp_parse_printer(range, sizeof range, &p) == IPP_OK && p.copies_max == 0, "short range");
}

static void test_escl(void)
{
    static char x[4096];
    uint32_t n;
    escl_settings_t s = {ESCL_SRC_PLATEN, ESCL_COLOR_RGB, 300, 0, 0, "image/jpeg"};
    CHECK(escl_settings(&s, x, sizeof x, &n) == ESCL_OK && n == strlen(x), "settings");
    CHECK(
        strstr(x, "<pwg:Width>2480</pwg:Width><pwg:Height>3508</pwg:Height>") &&
            strstr(x, "<pwg:InputSource>Platen</pwg:InputSource>") &&
            strstr(x, "<scan:ColorMode>RGB24</scan:ColorMode>") &&
            strstr(x, "<scan:XResolution>300</scan:XResolution><scan:YResolution>300</"
                      "scan:YResolution>") &&
            strstr(x, "<pwg:DocumentFormat>image/jpeg</pwg:DocumentFormat>") &&
            strstr(x,
                   "<pwg:ContentRegionUnits>escl:ThreeHundredthsOfInches</pwg:ContentRegionUnits>"),
        "%s", x);
    escl_settings_t f = {ESCL_SRC_FEEDER, ESCL_COLOR_BW, 1200, 2550, 3300, "application/pdf"};
    CHECK(escl_settings(&f, x, sizeof x, &n) == ESCL_OK && strstr(x, ">Feeder<") &&
              strstr(x, ">BlackAndWhite1<") && strstr(x, "<pwg:Width>2550<") &&
              strstr(x, ">1200</scan:YResolution>"),
          "feeder");
    f.color = ESCL_COLOR_GRAY;
    f.dpi = 75;
    f.width = 10200;
    f.height = 10200;
    f.format = "image/png";
    CHECK(escl_settings(&f, x, sizeof x, &n) == ESCL_OK && strstr(x, ">Grayscale8<") &&
              strstr(x, ">75</scan"),
          "edges");
    escl_settings_t bad[] = {
        {ESCL_SRC_PLATEN, ESCL_COLOR_RGB, 74, 0, 0, "image/jpeg"},
        {ESCL_SRC_PLATEN, ESCL_COLOR_RGB, 1201, 0, 0, "image/jpeg"},
        {2, ESCL_COLOR_RGB, 300, 0, 0, "image/jpeg"},
        {ESCL_SRC_PLATEN, 3, 300, 0, 0, "image/jpeg"},
        {ESCL_SRC_PLATEN, ESCL_COLOR_RGB, 300, 10201, 0, "image/jpeg"},
        {ESCL_SRC_PLATEN, ESCL_COLOR_RGB, 300, 0, 10201, "image/jpeg"},
        {ESCL_SRC_PLATEN, ESCL_COLOR_RGB, 300, 0, 0, "image/tiff"},
        {ESCL_SRC_PLATEN, ESCL_COLOR_RGB, 300, 0, 0, NULL},
    };
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++)
        CHECK(escl_settings(&bad[i], x, sizeof x, &n) == ESCL_E_ARG, "bad settings %zu", i);
    CHECK(escl_settings(&s, x, 100, &n) == ESCL_E_ROOM, "no room");

    static const char caps[] =
        "<?xml version=\"1.0\"?>\n<scan:ScannerCapabilities xmlns:scan=\"x\" xmlns:pwg=\"y\">"
        "<pwg:Version>2.63</pwg:Version><pwg:MakeAndModel>Example  Scan &amp; "
        "Copy</pwg:MakeAndModel>"
        "<scan:Platen><scan:PlatenInputCaps><scan:MaxWidth>2550</scan:MaxWidth>"
        "<scan:MaxHeight>3508</scan:MaxHeight><scan:SettingProfiles><scan:SettingProfile>"
        "<scan:ColorModes><scan:ColorMode>RGB24</scan:ColorMode><scan:ColorMode>Grayscale8</"
        "scan:ColorMode>"
        "</scan:ColorModes><scan:DocumentFormats><pwg:DocumentFormat>image/jpeg</"
        "pwg:DocumentFormat>"
        "<scan:DocumentFormatExt>image/jpeg</scan:DocumentFormatExt>"
        "<scan:DocumentFormatExt>application/pdf</scan:DocumentFormatExt></scan:DocumentFormats>"
        "<scan:SupportedResolutions><scan:DiscreteResolutions>"
        "<scan:DiscreteResolution><scan:XResolution>300</scan:XResolution><scan:YResolution>300</"
        "scan:YResolution></scan:DiscreteResolution>"
        "<scan:DiscreteResolution><scan:XResolution> 600 "
        "</scan:XResolution></scan:DiscreteResolution>"
        "<scan:DiscreteResolution><scan:XResolution>x</scan:XResolution></scan:DiscreteResolution>"
        "</scan:DiscreteResolutions></scan:SupportedResolutions></scan:SettingProfile></"
        "scan:SettingProfiles>"
        "</scan:PlatenInputCaps></scan:Platen>"
        "<scan:Adf><scan:AdfSimplexInputCaps><scan:SettingProfiles><scan:SettingProfile>"
        "<scan:ColorModes><scan:ColorMode>RGB24</scan:ColorMode></scan:ColorModes>"
        "<scan:SupportedResolutions><scan:DiscreteResolutions><scan:DiscreteResolution>"
        "<scan:XResolution>300</scan:XResolution></scan:DiscreteResolution></"
        "scan:DiscreteResolutions>"
        "</scan:SupportedResolutions></scan:SettingProfile></scan:SettingProfiles></"
        "scan:AdfSimplexInputCaps></scan:Adf>"
        "</scan:ScannerCapabilities>";
    escl_caps_t c;
    CHECK(escl_parse_caps(caps, sizeof caps - 1, &c) == ESCL_OK, "caps");
    CHECK(!strcmp(c.make, "Example  Scan & Copy") && c.platen && c.feeder && c.ndpi == 2 &&
              c.dpi[0] == 300 && c.dpi[1] == 600 && c.ncolors == 2 &&
              !strcmp(c.colors[1], "Grayscale8") && c.nformats == 2 &&
              !strcmp(c.formats[1], "application/pdf") && c.max_width == 2550 &&
              c.max_height == 3508,
          "caps: [%s] %u dpi %u colors %u formats", c.make, c.ndpi, c.ncolors, c.nformats);
    static const char caps2[] =
        "<ScannerCapabilities><DocumentFormat>image/jpeg</DocumentFormat>"
        "<MaxWidth>wide</MaxWidth><MaxHeight>99999999999</MaxHeight></ScannerCapabilities>";
    CHECK(escl_parse_caps(caps2, sizeof caps2 - 1, &c) == ESCL_OK && c.nformats == 1 &&
              !strcmp(c.formats[0], "image/jpeg") && !c.platen && !c.feeder && c.max_width == 0 &&
              c.max_height == 0 && !c.make[0] && c.ndpi == 0,
          "no prefixes, only DocumentFormat, bad sizes");
    CHECK(escl_parse_caps("<ScannerStatus/>", 16, &c) == ESCL_E_FORMAT, "not capabilities");
    {
        static char many[4096];
        int k = snprintf(many, sizeof many, "<ScannerCapabilities>");
        for (int i = 0; i < 12; i++)
            catf(many, sizeof many, &k, "<XResolution>%d</XResolution><ColorMode>m%d</ColorMode>",
                 100 + i, i);
        catf(many, sizeof many, &k, "</ScannerCapabilities>");
        CHECK(escl_parse_caps(many, (uint32_t) k, &c) == ESCL_OK && c.ndpi == ESCL_LIST &&
                  c.ncolors == ESCL_LIST && c.dpi[7] == 107,
              "lists stop at %u", ESCL_LIST);
    }

    static const char st[] =
        "<?xml version=\"1.0\"?><scan:ScannerStatus><pwg:Version>2.0</pwg:Version>"
        "<pwg:State>Processing</pwg:State><scan:AdfState>ScannerAdfLoaded</scan:AdfState>"
        "<scan:Jobs><scan:JobInfo><pwg:JobUri>/eSCL/ScanJobs/7</pwg:JobUri>"
        "<pwg:JobState>Processing</pwg:JobState></scan:JobInfo>"
        "<scan:JobInfo><pwg:JobState>Completed</pwg:JobState></scan:JobInfo></scan:Jobs>"
        "</scan:ScannerStatus>";
    escl_status_t ss;
    CHECK(escl_parse_status(st, sizeof st - 1, &ss) == ESCL_OK && !strcmp(ss.state, "Processing") &&
              !strcmp(ss.adf_state, "ScannerAdfLoaded") && ss.jobs == 2,
          "status [%s] [%s] %u", ss.state, ss.adf_state, ss.jobs);
    static const char st2[] = "<ScannerStatus><State>Idle</State></ScannerStatus>";
    CHECK(escl_parse_status(st2, sizeof st2 - 1, &ss) == ESCL_OK && !strcmp(ss.state, "Idle") &&
              !ss.adf_state[0] && ss.jobs == 0,
          "plain");
    CHECK(escl_parse_status("<ScannerStatus></ScannerStatus>", 31, &ss) == ESCL_E_FORMAT,
          "no state");
    CHECK(escl_parse_status("<State>Idle</State>", 19, &ss) == ESCL_E_FORMAT, "not a status");

    /* element text: entities, white space, empty elements, cuts */
    char v[8];
    uint32_t at = 0;
    static const char t1[] =
        "<a:Name/><a:Name>  &lt;&gt;&quot;&apos;&bogus; </a:Name><Name>caf\xc3\xa9</Name>";
    CHECK(escl_text(t1, sizeof t1 - 1, &at, "Name", v, sizeof v) && !v[0] && at == 9,
          "empty element, at %u", at);
    char w[32];
    CHECK(escl_text(t1, sizeof t1 - 1, &at, "Name", w, sizeof w) && !strcmp(w, "<>\"'&bogus;"),
          "entities [%s]", w);
    CHECK(escl_text(t1, sizeof t1 - 1, &at, "Name", v, 5) && !strcmp(v, "caf"), "cut before é [%s]",
          v);
    CHECK(!escl_text(t1, sizeof t1 - 1, &at, "Name", v, sizeof v), "no more");
    at = 0;
    CHECK(!escl_text("<Names>x</Names><Nam>y</Nam>", 28, &at, "Name", v, sizeof v),
          "names must match whole");
    at = 0;
    CHECK(!escl_text("<Name", 5, &at, "Name", v, sizeof v), "unclosed tag");
    at = 0;
    CHECK(escl_text("<Name>unclosed text", 19, &at, "Name", v, sizeof v) && !strcmp(v, "unclose"),
          "text to the end");
    at = 0;
    CHECK(escl_text("<Name>\xc3\xa9\xc3\xa9</Name>", 15, &at, "Name", v, 5) &&
              !strcmp(v, "\xc3\xa9\xc3\xa9"),
          "exactly two é fit");
    at = 0;
    CHECK(escl_text("<Name>x</Name>", 14, &at, "Name", v, 0), "no room: found, nothing copied");
    at = 0;
    CHECK(!escl_text("<", 1, &at, "Name", v, sizeof v) &&
              !escl_text("</Name><?Name?><!Name>", 22, &at, "Name", v, sizeof v),
          "closing tags and declarations are not elements");

    char path[40];
    CHECK(escl_job_path("http://10.0.0.9:8080/eSCL/ScanJobs/42", path, sizeof path) &&
              !strcmp(path, "/eSCL/ScanJobs/42"),
          "absolute: %s", path);
    CHECK(escl_job_path("https://s/eSCL/ScanJobs/a-b_C", path, sizeof path) &&
              !strcmp(path, "/eSCL/ScanJobs/a-b_C"),
          "https");
    CHECK(escl_job_path("/scanner/ScanJobs/7", path, sizeof path) &&
              !strcmp(path, "/scanner/ScanJobs/7"),
          "another root");
    CHECK(!escl_job_path("http://host", path, sizeof path) &&
              !escl_job_path("eSCL/ScanJobs/1", path, sizeof path) &&
              !escl_job_path("/eSCL/ScanJobs/", path, sizeof path) &&
              !escl_job_path("/eSCL/Jobs/1", path, sizeof path) &&
              !escl_job_path("/eSCL/ScanJobs/1?x", path, sizeof path) &&
              !escl_job_path("/e SCL/ScanJobs/1", path, sizeof path) &&
              !escl_job_path("/eSCL/ScanJobs/1", path, 16) &&
              escl_job_path("/eSCL/ScanJobs/1", path, 17),
          "refused");
}

int main(void)
{
    test_write();
    test_read();
    test_damage();
    test_escl();
    if (fails) {
        printf("[FAIL] test_print: %d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("%d checks\n", checks);
    return 0;
}
