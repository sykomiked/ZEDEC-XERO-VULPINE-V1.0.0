/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Folio's model against zxvd's real dispatcher: the page (FM2), line
 * breaking and the outline (FM3), an amount table (FM4), the text saved
 * and opened again in the scratch Hoard (FM5), and the PDF written and
 * printed on the fake printer (FM6). */
#include "folio_model.h"

static folio_t FO, FO2;

/* row r of f's layout as a string */
static const char *folio_row_text(const folio_t *f, uint32_t r)
{
    static char s[FOLIO_TEXT_MAX + 1];
    uint32_t n = r < f->nrows ? f->row[r].len : 0;
    if (n) memcpy(s, f->text + f->row[r].at, n);
    s[n] = 0;
    return s;
}

APPS_TEST(folio)
{
    folio_t *f = &FO;
    folio_init(f, &LINK);
    CHECK(f->per_page == 54 && f->cols == 78 && f->page.width == 12240 && !f->laid,
          "folio: US Letter by default, 54 lines of 78 (FR1, FR5)");

    /* FM2: the page; a refused one keeps the last */
    folio_page_t bad = {2879, 15840, 1440, 1440, 1440, 1440, 240};
    folio_page_t wide = {2880, 15840, 1440, 1441, 1440, 1440, 240};
    folio_page_t full = {12240, 2880, 1440, 1440, 1440, 1440, 240};
    folio_page_t narrow = {2880, 15840, 0, 481, 0, 0, 240};
    CHECK(folio_set_page(f, &bad) == ZXM_E_ARG && folio_set_page(f, &wide) == ZXM_E_ARG &&
              folio_set_page(f, &full) == ZXM_E_ARG && folio_set_page(f, &narrow) == ZXM_E_ARG &&
              folio_set_page(f, NULL) == ZXM_E_ARG && f->per_page == 54 && f->cols == 78,
          "folio: a page past its limits, margins past it, no line, 19 columns: refused");
    folio_page_t a4 = {11906, 16838, 1440, 1440, 1440, 1440, 240};
    CHECK(folio_set_page(f, &a4) == ZXM_OK && f->per_page == 58 && f->cols == 75,
          "folio: A4 holds 58 lines of 75: %lld %lld", (long long) f->per_page,
          (long long) f->cols);
    folio_page_t small = {2880, 2880, 0, 480, 0, 0, 240};
    CHECK(folio_set_page(f, &small) == ZXM_OK && f->per_page == 12 && f->cols == 20,
          "folio: a two-inch page, 12 lines of 20");

    /* FM3: line breaking, a form feed, headings */
    static const char doc[] = "# Title\n"
                              "The quick brown fox jumps over the lazy dog\n"
                              "\f\n"
                              "## Next\n"
                              "abcdefghijklmnopqrstuvwxyzABCD\n"
                              "\n"
                              "   indented\n";
    CHECK(folio_set_doc(f, 1000000000) == ZXM_E_ARG && folio_set_doc(f, 7) == ZXM_OK &&
              folio_set_text(f, doc, sizeof doc - 1u) == ZXM_OK && f->dirty &&
              folio_set_text(f, doc, FOLIO_TEXT_MAX + 1u) == ZXM_E_TOOBIG,
          "folio: the text, and one too long");
    CHECK(folio_layout(f) == ZXM_OK && f->laid && f->nrows == 17 && f->pages == 2 &&
              f->pages == folio_page_count(f->nrows, f->per_page),
          "folio: laid out: %u lines on %lld pages", f->nrows, (long long) f->pages);
    CHECK(!strcmp(folio_row_text(f, 0), "Title") &&
              !strcmp(folio_row_text(f, 1), "The quick brown fox") &&
              !strcmp(folio_row_text(f, 2), "jumps over the lazy") &&
              !strcmp(folio_row_text(f, 3), "dog") && !f->row[4].len && !f->row[11].len &&
              !strcmp(folio_row_text(f, 12), "Next") &&
              !strcmp(folio_row_text(f, 13), "abcdefghijklmnopqrst") &&
              !strcmp(folio_row_text(f, 14), "uvwxyzABCD") && !f->row[15].len &&
              !strcmp(folio_row_text(f, 16), "   indented"),
          "folio: broken at spaces, a long word cut, a form feed to the page's end");
    CHECK(f->nheads == 2 && f->head[0].level == 1 && f->head[0].page == 1 && f->head[0].line == 1 &&
              !strcmp(f->head[0].title, "Title") && f->head[1].level == 2 && f->head[1].page == 2 &&
              f->head[1].line == 13 && !strcmp(f->head[1].title, "Next") &&
              folio_outline_ok(f->doc, 2, f->head[1].level, f->head[1].page, f->head[1].line),
          "folio: the outline, each entry's record fits ^ZXFOLIO (FR7)");
    {
        /* a form feed at a page's start adds no page; a character is a code point */
        static const char ff[] = "\f\n"
                                 "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9"
                                 "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9"
                                 "\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9\n"
                                 "#nohead\n";
        CHECK(folio_set_text(&FO2, "", 0) == ZXM_OK, "folio: an empty scratch document");
        folio_init(&FO2, &LINK);
        CHECK(folio_set_page(&FO2, &small) == ZXM_OK &&
                  folio_set_text(&FO2, ff, sizeof ff - 1u) == ZXM_OK &&
                  folio_layout(&FO2) == ZXM_OK && FO2.nrows == 2 && FO2.row[0].len == 40 &&
                  FO2.nheads == 0 && !strcmp(folio_row_text(&FO2, 1), "#nohead"),
              "folio: twenty two-byte characters fill one line; '#' with no space is text");
        static char many[FOLIO_OUTLINE * 4u + 8u];
        uint32_t n = 0;
        for (uint32_t i = 0; i <= FOLIO_OUTLINE; i++) {
            memcpy(many + n, "# h\n", 4);
            n += 4;
        }
        uint32_t rows = FO2.nrows;
        CHECK(folio_set_text(&FO2, many, n) == ZXM_OK && folio_layout(&FO2) == ZXM_E_TOOBIG &&
                  FO2.nrows == rows && !FO2.laid,
              "folio: one heading past the outline's room is refused, the last layout kept");
        CHECK(folio_set_text(&FO2, "", 0) == ZXM_OK && folio_layout(&FO2) == ZXM_OK &&
                  FO2.nrows == 0 && FO2.pages == 0 &&
                  folio_export_pdf(&FO2, "empty.pdf") == ZXM_E_STATE,
              "folio: an empty document has no page to print");
    }

    /* FM4: an amount table */
    char m[32];
    CHECK(folio_money(18999, m, sizeof m) == 6 && !strcmp(m, "189.99") &&
              folio_money(-1205, m, sizeof m) == 6 && !strcmp(m, "-12.05") &&
              folio_money(5, m, sizeof m) == 4 && !strcmp(m, "0.05") && folio_money(5, m, 3) == -1,
          "folio: minor units with two decimals");
    CHECK(folio_put_table(f) == ZXM_E_STATE && folio_add_item(f, "", 1000, 1) == ZXM_E_ARG &&
              folio_add_item(f, "a\nb", 1000, 1) == ZXM_E_ARG &&
              folio_add_item(f, "x", -1, 1) == ZXM_E_ARG &&
              folio_add_item(f, "x", 1000, FOLIO_PRICE_MAX + 1) == ZXM_E_ARG &&
              folio_add_item(f, "123456789012345678901234567890123", 1000, 1) == ZXM_E_ARG &&
              f->nitems == 0,
          "folio: no items, or items refused (FR3)");
    CHECK(folio_add_item(f, "Consulting hours", 1500, 12000) == ZXM_OK &&
              folio_add_item(f, "Paper", 2000, 499) == ZXM_OK &&
              folio_add_item(f, "Ink", 1, 500) == ZXM_OK && f->nitems == 3 &&
              f->item[0].amount == 18000 && f->item[1].amount == 998 && f->item[2].amount == 1 &&
              f->total == 18999,
          "folio: three lines at a fixed scale and their total (FR3, FR4): %lld",
          (long long) f->total);
    for (uint32_t i = 0; i < FOLIO_ITEMS - 3u; i++)
        (void) folio_add_item(f, "Max", FOLIO_QTY_MAX, FOLIO_PRICE_MAX);
    CHECK(f->nitems == FOLIO_ITEMS && folio_add_item(f, "one more", 1, 1) == ZXM_E_TOOBIG &&
              f->total == 18999 + (int64_t) (FOLIO_ITEMS - 3u) * INT64_C(999999998000000),
          "folio: the table's room, and its largest total still exact");
    CHECK(folio_clear_items(f) == ZXM_OK && f->nitems == 0 && f->total == 0 &&
              folio_add_item(f, "Consulting hours", 1500, 12000) == ZXM_OK &&
              folio_add_item(f, "Paper", 2000, 499) == ZXM_OK &&
              folio_add_item(f, "Ink", 1, 500) == ZXM_OK,
          "folio: cleared, and the three again");
    {
        char l0[96], l1[96], l3[96], lt[96];
        snprintf(l0, sizeof l0, "%-32s%12s%14s%16s\n", "Item", "Qty", "Price", "Amount");
        snprintf(l1, sizeof l1, "%-32s%12s%14s%16s\n", "Consulting hours", "1.500", "120.00",
                 "180.00");
        snprintf(l3, sizeof l3, "%-32s%12s%14s%16s\n", "Ink", "0.001", "5.00", "0.01");
        snprintf(lt, sizeof lt, "%-32s%42s", "Total", "189.99");
        CHECK(folio_set_page(f, &a4) == ZXM_OK && folio_put_table(f) == ZXM_OK && !f->laid &&
                  strstr(f->text, l0) && strstr(f->text, l1) && strstr(f->text, l3) &&
                  strstr(f->text, lt) && strlen(l1) == 75u,
              "folio: the table in the text: %s", f->text + sizeof doc - 1u);
        /* Title, the paragraph, the form feed to line 58, four lines, the table's five */
        CHECK(folio_layout(f) == ZXM_OK && f->nrows == 58 + 4 + 5 &&
                  !strcmp(folio_row_text(f, 1), "The quick brown fox jumps over the lazy dog") &&
                  !strcmp(folio_row_text(f, 58 + 4 + 4), lt) && f->pages == 2,
              "folio: each table line on one line of A4: %u", f->nrows);
    }

    /* FM5: saved and opened again */
    int before = sent;
    CHECK(folio_save(f, "", "") == ZXM_E_ARG && folio_save(f, NULL, "a.txt") == ZXM_E_ARG &&
              folio_open(&FO2, "", "") == ZXM_E_ARG && sent == before,
          "folio: no name: nothing asked");
    CHECK(folio_save(f, "", "folio-letter.txt") == ZXM_OK && !f->dirty &&
              !strcmp(f->path, "folio-letter.txt"),
          "folio: saved: %s", LINK.err);
    folio_init(&FO2, &LINK);
    CHECK(folio_open(&FO2, "", "folio-none.txt") == ZXM_E_REMOTE && FO2.len == 0,
          "folio: no such file: nothing opened");
    CHECK(folio_open(&FO2, "", "folio-letter.txt") == ZXM_OK && FO2.len == f->len &&
              !memcmp(FO2.text, f->text, f->len) && !FO2.dirty,
          "folio: opened again, the same %u bytes: %s", FO2.len, LINK.err);
    {
        /* a text past three request pieces is written in parts and read back whole */
        static char big[9000];
        for (uint32_t i = 0; i < sizeof big; i++)
            big[i] = i % 61u == 60u ? '\n' : (char) ('a' + i % 26u);
        big[5] = '\f';
        CHECK(folio_set_text(&FO2, big, sizeof big) == ZXM_OK &&
                  folio_save(&FO2, "", "folio-big.txt") == ZXM_OK &&
                  folio_open(&FO2, "", "folio-big.txt") == ZXM_OK && FO2.len == sizeof big &&
                  !memcmp(FO2.text, big, sizeof big),
              "folio: 9000 bytes written in parts and read back: %s", LINK.err);
    }

    /* FM6: the PDF */
    CHECK(folio_set_page(f, &small) == ZXM_OK &&
              folio_set_text(f, doc, sizeof doc - 1u) == ZXM_OK &&
              folio_export_pdf(f, "folio-letter.pdf") == ZXM_E_STATE && folio_layout(f) == ZXM_OK &&
              folio_export_pdf(f, "folio-letter.txt") == ZXM_E_ARG,
          "folio: not laid out, or not a .pdf: refused");
    CHECK(folio_export_pdf(f, "folio-letter.pdf") == ZXM_OK, "folio: the PDF: %s", LINK.err);
    uint32_t plen = 0;
    const char *pdf = folio_pdf_text(&plen);
    CHECK(plen > 0 && !strncmp(pdf, "%PDF-1.4\n", 9) &&
              strstr(pdf, "/Kids [4 0 R 6 0 R] /Count 2") && strstr(pdf, "/BaseFont /Courier") &&
              strstr(pdf, "/MediaBox [0 0 144.00 144.00]") &&
              strstr(pdf, "1 0 0 1 0.00 136.00 Tm\n(Title) Tj\n") &&
              strstr(pdf, "1 0 0 1 0.00 124.00 Tm\n(The quick brown fox) Tj\n") &&
              strstr(pdf, "(Next) Tj") && strstr(pdf, "xref\n0 8\n") &&
              !strcmp(pdf + plen - 6, "%%EOF\n"),
          "folio: two pages of Courier, baselines in points: %.200s", pdf);
    {
        /* the xref's offsets point at their objects */
        const char *x = strstr(pdf, "xref\n0 8\n0000000000 65535 f \n");
        uint32_t good = 0;
        for (uint32_t k = 1; x && k <= 7; k++) {
            const char *e = x + 29 + 20 * (k - 1u);
            uint32_t off = 0;
            for (uint32_t j = 0; j < 10; j++) off = off * 10u + (uint32_t) (e[j] - '0');
            char want[16];
            snprintf(want, sizeof want, "%u 0 obj", (unsigned) k);
            good += off < plen && !strncmp(pdf + off, want, strlen(want));
        }
        CHECK(good == 7, "folio: every object where the xref says (%u of 7)", good);
    }
    {
        folio_init(&FO2, &LINK);
        static const char esc[] = "a (b) \\c \xe2\x82\xac\n";
        CHECK(folio_set_page(&FO2, &small) == ZXM_OK &&
                  folio_set_text(&FO2, esc, sizeof esc - 1u) == ZXM_OK &&
                  folio_layout(&FO2) == ZXM_OK &&
                  folio_export_pdf(&FO2, "folio-esc.pdf") == ZXM_OK &&
                  strstr(folio_pdf_text(NULL), "(a \\(b\\) \\\\c ?) Tj"),
              "folio: ( ) \\ escaped, a character past US-ASCII shown as '?'");
    }
    zxm_begin(&LINK, "files.read");
    zxm_p_str(&LINK, "path", "folio-letter.pdf");
    zxm_p_u64(&LINK, "max", 600);
    CHECK(zxm_call(&LINK) == ZXM_OK, "folio: the PDF read: %s", LINK.err);
    static char head[1024];
    int32_t t = zxm_get(&LINK, LINK.result, "text");
    int32_t hn = t < 0 ? -1 : w4_json_str(LINK.resp, &LINK.tok[t], head, sizeof head);
    CHECK(hn > 0 && !strncmp(head, "%PDF-1.4\n1 0 obj\n", 17), "folio: in the Hoard: %.40s", head);

    /* FM6: printed on the fake printer, then followed */
    char uri[96];
    CHECK(fake_start(), "folio: the fake printer");
    snprintf(uri, sizeof uri, "ipp://127.0.0.1:%d/ipp/print", FAKE_PORT);
    before = sent;
    CHECK(folio_print(f, "", "folio-letter.pdf", 1) == ZXM_E_ARG &&
              folio_print(f, uri, "folio-letter.pdf", 1000) == ZXM_E_ARG &&
              folio_print(f, uri, "folio-letter.txt", 1) == ZXM_E_ARG &&
              folio_follow(f, uri) == ZXM_E_STATE && f->job == 0 && sent == before,
          "folio: no printer, too many copies, not a PDF, no job: nothing asked");
    CHECK(folio_printer(f, uri) == ZXM_OK && f->printer_known && f->accepting && f->takes_pdf,
          "folio: the printer accepts jobs and takes PDF: %s", LINK.err);
    CHECK(folio_print(f, uri, "folio-letter.pdf", 2) == ZXM_OK && f->job == 77 &&
              !strcmp(f->job_state, "pending") && strstr(LINK.req, "\"copies\":2"),
          "folio: printed: job %lld %s (%s)", (long long) f->job, f->job_state, LINK.err);
    {
        char want[64];
        snprintf(want, sizeof want, "doc=%u:%%PDF-1.4", (unsigned) plen);
        CHECK(strstr(fake_get("/fake/last"), "fmt=application/pdf") &&
                  strstr(fake_get("/fake/last"), "copies=2") &&
                  strstr(fake_get("/fake/last"), want),
              "folio: what the printer got: %s", fake_get("/fake/last"));
    }
    CHECK(folio_follow(f, uri) == ZXM_OK && !strcmp(f->job_state, "completed") && f->job_pages == 2,
          "folio: followed: %s, %lld pages", f->job_state, (long long) f->job_pages);
    f->job = 78;
    CHECK(folio_follow(f, uri) == ZXM_E_REMOTE, "folio: a job the printer does not know");
    fake_stop();
}
