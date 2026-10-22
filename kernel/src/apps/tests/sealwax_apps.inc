/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Sealwax's model against zxvd's real dispatcher: a form built and filled
 * (SM1-SM3), saved and opened again in the scratch Hoard (SM4), written as
 * a PDF and printed on the fake printer only on the owner's word (SM5),
 * and a signed copy checked with web4.verify (SM6). web4.verify is not
 * linked in this in-process zxvd (zxvd_web4.c is one of zxvd's groups,
 * outside test_apps.sh's link line), so it is answered on a link of its
 * own (SW_LINK) as zxvd's schema gives it: valid for the one envelope the
 * owner signed, "signature" for any other. Every other request goes to the
 * real dispatcher. */
#include "sealwax_model.h"

static sealwax_t SW, SW2;
static zxm_link_t SW_LINK;
static int SW_VERIFIED;    /* web4.verify requests seen */
static int64_t SW_PAYLOAD; /* the bytes the owner signed */
static const char SW_GOOD[] = "5745455601"
                              "00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff";

static int32_t sw_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    if (!strstr(req, "\"web4.verify\"")) return inproc(NULL, req, len, resp, cap);
    SW_VERIFIED++;
    char want[sizeof SW_GOOD + 16];
    snprintf(want, sizeof want, "\"envelope\":\"%s\"", SW_GOOD);
    if (!strstr(req, want))
        return snprintf(resp, cap,
                        "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"valid\":false,"
                        "\"reason\":\"signature\"}}",
                        l->seq);
    return snprintf(resp, cap,
                    "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"valid\":true,\"kind\":"
                    "\"REQUEST\",\"from\":\"%s\",\"to\":\"%s\",\"seq\":\"1\",\"ts\":1790000000,"
                    "\"req_id\":\"1\",\"chunk\":0,\"tool\":\"sealwax\",\"final\":false,"
                    "\"payload_bytes\":%lld,\"consent\":false}}",
                    l->seq, SW_GOOD + 10, SW_GOOD + 10, (long long) SW_PAYLOAD);
}

APPS_TEST(sealwax)
{
    sealwax_t *s = &SW;
    sealwax_init(s, &LINK);
    CHECK(!s->ready && s->nfields == 0 && strstr(s->notice, "No fields yet.") &&
              strstr(s->notice, "never signs, prints or sends it by itself"),
          "sealwax: an empty form is not ready, and says it never signs: %s", s->notice);

    /* SM1: the form's fields */
    CHECK(sealwax_set_title(s, "Club membership renewal") == ZXM_OK &&
              sealwax_set_form(s, 7) == ZXM_OK &&
              sealwax_add_field(s, "full_name", SEALWAX_TEXT, 1, 2, 60) == ZXM_OK &&
              sealwax_add_field(s, "member_no", SEALWAX_NUMBER, 1, 1, 99999) == ZXM_OK &&
              sealwax_add_field(s, "dues", SEALWAX_AMOUNT, 1, 1000, 50000) == ZXM_OK &&
              sealwax_add_field(s, "gift", SEALWAX_AMOUNT, 0, 0, 100000) == ZXM_OK &&
              sealwax_add_field(s, "note", SEALWAX_TEXT, 0, 0, 120) == ZXM_OK && s->nfields == 5,
          "sealwax: five fields");
    CHECK(sealwax_add_field(s, "dues", SEALWAX_TEXT, 0, 0, 1) == ZXM_E_ARG &&
              sealwax_add_field(s, "Bad Name", SEALWAX_TEXT, 0, 0, 1) == ZXM_E_ARG &&
              sealwax_add_field(s, "x", 4, 0, 0, 1) == ZXM_E_ARG &&
              sealwax_add_field(s, "x", SEALWAX_TEXT, 2, 0, 1) == ZXM_E_ARG &&
              sealwax_add_field(s, "x", SEALWAX_TEXT, 0, 0, 121) == ZXM_E_ARG &&
              sealwax_add_field(s, "x", SEALWAX_AMOUNT, 0, -1, 5) == ZXM_E_ARG &&
              sealwax_add_field(s, "x", SEALWAX_NUMBER, 0, 5, 4) == ZXM_E_ARG &&
              sealwax_add_field(s, "x", SEALWAX_NUMBER, 0, 0, SEALWAX_BOUND_MAX + 1) == ZXM_E_ARG &&
              sealwax_set_form(s, 1000000000) == ZXM_E_ARG && s->nfields == 5,
          "sealwax: a duplicate, a bad name, kind, flag or bounds: not added");

    /* SM3: nothing filled: required first in the fill order */
    CHECK(!s->ready && s->missing == 3 && s->outside == 0 && s->norder == 5 && s->order[0] == 1 &&
              s->order[1] == 2 && s->order[2] == 3 && s->order[3] == 4 && s->order[4] == 5 &&
              strstr(s->notice, "Not ready: 3 required empty, 0 outside bounds; next: full_name."),
          "sealwax: %s", s->notice);

    /* SM2: filling */
    CHECK(sealwax_fill(s, 0, "Ada Brightwater") == ZXM_OK && sealwax_fill(s, 3, "12.5") == ZXM_OK &&
              s->field[3].num == 1250 && s->missing == 2 && s->order[0] == 2 && s->order[1] == 3 &&
              s->order[2] == 5 && s->norder == 3,
          "sealwax: name and gift in: %s", s->notice);
    CHECK(sealwax_fill(s, 2, "-5") == ZXM_E_ARG && sealwax_fill(s, 2, "5.123") == ZXM_E_ARG &&
              sealwax_fill(s, 2, "5.") == ZXM_E_ARG && sealwax_fill(s, 2, "1,000") == ZXM_E_ARG &&
              sealwax_fill(s, 2, "10000000000000") == ZXM_E_ARG &&
              sealwax_fill(s, 1, "12a") == ZXM_E_ARG &&
              sealwax_fill(s, 1, "1234567890123456") == ZXM_E_ARG &&
              sealwax_fill(s, 4, "line\nbreak") == ZXM_E_ARG &&
              sealwax_fill(s, 4, "\xff") == ZXM_E_ARG && sealwax_fill(s, 9, "x") == ZXM_E_ARG &&
              !s->field[2].filled && s->field[3].num == 1250,
          "sealwax: values that cannot be read are refused, nothing changed");
    {
        char longv[SEALWAX_VALUE_MAX + 2];
        memset(longv, 'a', sizeof longv - 1);
        longv[sizeof longv - 1] = 0;
        CHECK(sealwax_fill(s, 4, longv) == ZXM_E_ARG, "sealwax: 121 characters refused");
        longv[SEALWAX_VALUE_MAX] = 0;
        CHECK(sealwax_fill(s, 4, longv) == ZXM_OK && s->field[4].chars == SEALWAX_VALUE_MAX &&
                  sealwax_fill(s, 4, "Caf\xc3\xa9 (front desk)") == ZXM_OK &&
                  s->field[4].chars == 17 && s->field[4].bytes == 18,
              "sealwax: 120 characters taken; characters counted, not bytes");
    }
    CHECK(sealwax_fill(s, 2, "9.99") == ZXM_OK && s->field[2].num == 999 && s->field[2].outside &&
              sealwax_fill(s, 1, "100000") == ZXM_OK && s->field[1].outside && s->outside == 2 &&
              s->missing == 0 && !s->ready && s->order[0] == 2 && s->order[1] == 3 &&
              strstr(s->notice, "0 required empty, 2 outside bounds; next: member_no."),
          "sealwax: values outside their bounds are kept and counted: %s", s->notice);
    CHECK(sealwax_fill(s, 2, "120") == ZXM_OK && sealwax_fill(s, 1, "4521") == ZXM_OK && s->ready &&
              s->norder == 1 && s->order[0] == 0 && s->total_ok && s->total == 13250 &&
              strstr(s->notice, "Ready to sign: 5 fields in bounds, amounts total 132.50.") &&
              strstr(s->notice, "Advice only"),
          "sealwax: ready: %s", s->notice);
    CHECK(sealwax_fill(s, 3, "") == ZXM_OK && !s->field[3].filled && s->ready &&
              s->total == 12000 && s->norder == 1 && s->order[0] == 4,
          "sealwax: an optional field emptied: still ready, and it is left to fill");
    CHECK(sealwax_record(s, 0) && sealwax_record(s, 4) && !sealwax_record(s, 5),
          "sealwax: its field records fit ^ZXSEALWAX");

    /* SM4: saved, then opened again */
    CHECK(sealwax_save(s, "", "sealwax-renewal.form") == ZXM_OK && !s->dirty && s->saved_len > 0,
          "sealwax: saved: %s", LINK.err);
    {
        char t[1024];
        int32_t n = sealwax_text(s, t, sizeof t);
        CHECK(n == (int32_t) s->saved_len &&
                  !strcmp(t, "sealwax 1\ntitle Club membership renewal\n"
                             "field full_name text 1 2 60 Ada Brightwater\n"
                             "field member_no number 1 1 99999 4521\n"
                             "field dues amount 1 1000 50000 120.00\n"
                             "field gift amount 0 0 100000\n"
                             "field note text 0 0 120 Caf\xc3\xa9 (front desk)\n") &&
                  sealwax_text(s, t, 40) == -1,
              "sealwax: the file's text: %s", t);
    }
    sealwax_t *r = &SW2;
    sealwax_init(r, &LINK);
    CHECK(sealwax_open(r, "", "sealwax-renewal.form") == ZXM_OK && r->nfields == 5 && r->ready &&
              !r->dirty && r->saved_len == s->saved_len &&
              !strcmp(r->title, "Club membership renewal") && r->field[2].num == 12000 &&
              r->field[1].num == 4521 && !strcmp(r->field[4].value, "Caf\xc3\xa9 (front desk)") &&
              !r->field[3].filled && !strcmp(r->path, "sealwax-renewal.form"),
          "sealwax: opened as it was saved: %s", LINK.err);
    {
        static const char *const BAD[] = {
            "sealwax 2\ntitle x\n",
            "sealwax 1\n",
            "sealwax 1\ntitle x\nfield a text 1 0 5",
            "sealwax 1\ntitle x\nfield a text 1 0 5 \n",
            "sealwax 1\ntitle x\nfield a words 1 0 5\n",
            "sealwax 1\ntitle x\nfield a text 1 0 5 toolong\n",
            "sealwax 1\ntitle x\nfield a amount 1 0 5 1.234\n",
            "sealwax 1\ntitle x\nfield a text 1 0 5\nfield a text 1 0 5\n",
            "sealwax 1\ntitle x\nnote here\n",
        };
        bool kept = true;
        for (uint32_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
            char p[256];
            snprintf(p, sizeof p, "{\"path\":\"sealwax-bad.form\",\"text\":\"");
            uint32_t k = (uint32_t) strlen(p);
            int32_t e = zxm_escape(BAD[i], (uint32_t) strlen(BAD[i]), p + k, sizeof p - k - 3);
            if (e < 0) continue;
            memcpy(p + k + (uint32_t) e, "\"}", 3);
            int w = raw_call("files.write", p);
            int o = sealwax_open(r, "", "sealwax-bad.form");
            /* "toolong" is 7 characters, past the field's 5: kept and counted, not refused */
            if (w != ZXM_OK || (i == 5 ? o != ZXM_OK || r->outside != 1 : o != ZXM_E_ARG)) {
                kept = false;
                printf("    bad form %u: write %d open %d\n", i, w, o);
            }
            if (i == 5) sealwax_open(r, "", "sealwax-renewal.form");
        }
        CHECK(kept && r->nfields == 5 && !strcmp(r->title, "Club membership renewal"),
              "sealwax: files that hold no form are refused, the form kept");
    }
    CHECK(sealwax_open(r, "", "sealwax-none.form") == ZXM_E_REMOTE &&
              sealwax_open(r, "", "") == ZXM_E_ARG && r->nfields == 5,
          "sealwax: no such file, or no name");

    /* SM5: the PDF, printed on the owner's word only */
    CHECK(sealwax_export_pdf(s, "sealwax-renewal.txt") == ZXM_E_ARG &&
              sealwax_export_pdf(s, "sealwax-renewal.pdf") == ZXM_OK,
          "sealwax: the PDF written: %s", LINK.err);
    {
        uint32_t plen;
        const char *pdf = sealwax_pdf_text(&plen);
        CHECK(plen > 0 && !memcmp(pdf, "%PDF-1.4\n", 9) && strstr(pdf, "/BaseFont /Courier") &&
                  strstr(pdf, "(Club membership renewal) Tj T*") &&
                  strstr(pdf, "(dues: 120.00) Tj T*") &&
                  strstr(pdf, "(note: Caf? \\(front desk\\)) Tj T*") &&
                  strstr(pdf, "(Ready to sign.) Tj T*") &&
                  strstr(pdf, "(Total of amounts: 120.00)") && strstr(pdf, "%%EOF\n"),
              "sealwax: one page of Courier: %.300s", pdf);
        const char *x = strstr(pdf, "\nxref\n");
        const char *o5 = strstr(pdf, "\n5 0 obj");
        char want[32];
        snprintf(want, sizeof want, "%010u 00000 n \ntrailer", (unsigned) (o5 ? o5 + 1 - pdf : 0));
        CHECK(x && o5 && strstr(pdf, want), "sealwax: the xref points at each object");
    }
    char uri[96];
    CHECK(fake_start(), "sealwax: the fake printer");
    snprintf(uri, sizeof uri, "ipp://127.0.0.1:%d/ipp/print", FAKE_PORT);
    int before = sent;
    CHECK(sealwax_print(s, uri, "sealwax-renewal.pdf", 1, false) == ZXM_E_STATE &&
              sealwax_print(s, uri, "sealwax-renewal.pdf", 1000, true) == ZXM_E_ARG &&
              sealwax_print(s, uri, "sealwax-renewal.form", 1, true) == ZXM_E_ARG &&
              sealwax_print(s, "", "sealwax-renewal.pdf", 1, true) == ZXM_E_ARG && s->job == 0 &&
              sent == before,
          "sealwax: without the owner's word, or with bad arguments, nothing is sent");
    CHECK(sealwax_print(s, uri, "sealwax-renewal.pdf", 2, true) == ZXM_OK && s->job > 0 &&
              s->printer_known && s->accepting && s->takes_pdf && s->job_state[0],
          "sealwax: printed on the owner's word: job %lld %s (%s)", (long long) s->job,
          s->job_state, LINK.err);
    CHECK(strstr(fake_get("/fake/last"), "fmt=application/pdf") &&
              strstr(fake_get("/fake/last"), "copies=2"),
          "sealwax: what the printer got: %s", fake_get("/fake/last"));
    fake_stop();

    /* SM6: a signed copy, the hex of an envelope the owner signed elsewhere
     * (the model never signs), kept beside the form */
    {
        char p[256];
        snprintf(p, sizeof p, "{\"path\":\"sealwax-renewal.sig\",\"text\":\"%s\\n\"}", SW_GOOD);
        CHECK(raw_call("files.write", p) == ZXM_OK &&
                  raw_call("files.write", "{\"path\":\"sealwax-forged.sig\",\"text\":"
                                          "\"5745455601ff\"}") == ZXM_OK &&
                  raw_call("files.write", "{\"path\":\"sealwax-junk.sig\",\"text\":\"not hex\"}") ==
                      ZXM_OK &&
                  raw_call("files.write", "{\"path\":\"sealwax-odd.sig\",\"text\":\"574\"}") ==
                      ZXM_OK,
              "sealwax: a signed, a forged, a junk and an odd copy kept: %s", LINK.err);
    }
    zxm_link_init(&SW_LINK, sw_send, &SW_LINK);
    s->link = &SW_LINK;
    SW_PAYLOAD = s->saved_len;
    CHECK(sealwax_check_signed(s, "sealwax-renewal.sig") == ZXM_OK && s->sig.checked &&
              s->sig.valid && s->sig.bound && !strcmp(s->sig.kind, "REQUEST") &&
              s->sig.payload_bytes == s->saved_len && strlen(s->sig.from) == 64 &&
              s->sig.ts == 1790000000 && strstr(s->notice, "The signed copy holds") &&
              strstr(s->notice, "matched by size only") && strstr(s->notice, "Advice only") &&
              SW_VERIFIED == 1,
          "sealwax: the signed copy holds: %s (%s)", s->notice, SW_LINK.err);
    SW_PAYLOAD = s->saved_len + 1;
    CHECK(sealwax_check_signed(s, "sealwax-renewal.sig") == ZXM_OK && s->sig.valid &&
              !s->sig.bound && strstr(s->notice, "not this form as saved") &&
              strstr(s->notice, " in the form."),
          "sealwax: a signature over another size is not this form: %s", s->notice);
    SW_PAYLOAD = s->saved_len;
    CHECK(sealwax_fill(s, 4, "Cafe") == ZXM_OK && s->dirty &&
              sealwax_check_signed(s, "sealwax-renewal.sig") == ZXM_OK && s->sig.valid &&
              !s->sig.bound && strstr(s->notice, "not this form as saved") &&
              strstr(s->notice, "changed since"),
          "sealwax: the form changed since: the copy is not bound to it: %s", s->notice);
    CHECK(sealwax_check_signed(s, "sealwax-forged.sig") == ZXM_OK && s->sig.checked &&
              !s->sig.valid && !s->sig.bound && !strcmp(s->sig.reason, "signature") &&
              strstr(s->notice, "does not hold: signature."),
          "sealwax: a forged copy: %s", s->notice);
    int verified = SW_VERIFIED;
    CHECK(sealwax_check_signed(s, "sealwax-junk.sig") == ZXM_E_ARG &&
              sealwax_check_signed(s, "sealwax-odd.sig") == ZXM_E_ARG &&
              sealwax_check_signed(s, "") == ZXM_E_ARG && SW_VERIFIED == verified && !s->sig.valid,
          "sealwax: a copy that is not whole hex bytes is never sent to web4.verify");
    s->link = &LINK;

    /* made up replies: nothing out of bounds is taken */
    sealwax_t *f = &SW2;
    zxm_link_init(&FAKE, canned, &FAKE);
    f->link = &FAKE;
    f->saved_len = 10;
    f->dirty = false;
    /* one canned reply answers both the read and the verify */
#define SW_FILE "\"path\":\"x\",\"size\":4,\"offset\":0,\"next\":4,\"eof\":true,\"text\":\"abcd\","
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{" SW_FILE
             "\"valid\":false,\"reason\":\"signature\"}}";
    CHECK(sealwax_check_signed(f, "x.sig") == ZXM_OK && f->sig.checked && !f->sig.valid &&
              !strcmp(f->sig.reason, "signature"),
          "sealwax: a verdict that does not hold, as given");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{" SW_FILE
             "\"valid\":true,\"kind\":\"REQUEST\",\"from\":\"ab\",\"payload_bytes\":10}}";
#undef SW_FILE
    CHECK(sealwax_check_signed(f, "x.sig") == ZXM_E_PROTOCOL,
          "sealwax: a reply that is not a verdict");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"accepting\":true}}";
    CHECK(sealwax_printer(f, uri) == ZXM_E_PROTOCOL && !f->printer_known,
          "sealwax: a printer that does not say its formats");
    CANNED = NULL;
    f->link = &LINK;
}
