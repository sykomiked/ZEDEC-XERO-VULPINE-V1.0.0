/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Broadsheet's model against zxvd's real dispatcher: PDFs built here byte by
 * byte in the scratch Hoard, opened and checked (BM1..BM3), fitted (BM4),
 * annotated in a notes file beside them (BM5) and printed on the fake
 * printer (BM6); the document itself is never written. */
#include "broadsheet_model.h"

#define BS_CAP 40000u

static broadsheet_t BS;

/* A PDF of three objects into d: hdr its first line, box the /MediaBox
 * ("" for none), pad bytes of comment lines before the cross-reference
 * table (to cross read windows), entry the in-use offset of object 2 written
 * into the table (-1: its true one), sx startxref (-1: its true one), eof
 * whether "%%EOF" ends it. Its length. */
static uint32_t bs_pdf(char *d, const char *hdr, const char *box, uint32_t pad, int64_t entry,
                       int64_t sx, bool eof)
{
    uint32_t n = 0, at[4];
    n += (uint32_t) snprintf(d + n, BS_CAP - n, "%s\n", hdr);
    at[1] = n;
    n += (uint32_t) snprintf(d + n, BS_CAP - n,
                             "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
    at[2] = n;
    n += (uint32_t) snprintf(
        d + n, BS_CAP - n, "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 %s >>\nendobj\n", box);
    for (uint32_t i = 0; i < pad / 64u; i++)
        n += (uint32_t) snprintf(d + n, BS_CAP - n, "%%%062u\n", i);
    at[3] = n;
    n += (uint32_t) snprintf(d + n, BS_CAP - n,
                             "3 0 obj\n<< /Type /Page /Parent 2 0 R >>\nendobj\n");
    uint32_t xref = n;
    n += (uint32_t) snprintf(d + n, BS_CAP - n, "xref\n0 4\n0000000000 65535 f \n");
    for (uint32_t k = 1; k <= 3; k++)
        n += (uint32_t) snprintf(d + n, BS_CAP - n, "%010lld 00000 n \n",
                                 (long long) (k == 2 && entry >= 0 ? entry : at[k]));
    n += (uint32_t) snprintf(d + n, BS_CAP - n,
                             "trailer\n<< /Size 4 /Root 1 0 R >>\nstartxref\n%lld\n%s",
                             (long long) (sx >= 0 ? sx : xref), eof ? "%%EOF\n" : "");
    return n;
}

static bool bs_put(const char *rel, const char *d, uint32_t n)
{
    uint64_t size = 0;
    return zxf_write(rel, (const uint8_t *) d, n, ZXF_W_REPLACE, false, &size) == 0 && size == n;
}

/* The file at rel is exactly d[0..n). */
static bool bs_same(const char *rel, const char *d, uint32_t n)
{
    static uint8_t buf[BS_CAP];
    uint32_t got = 0, at = 0;
    uint64_t size = 0;
    while (at < n) {
        if (zxf_read(rel, at, buf + at, BS_CAP - at, false, &got, &size) != 0 || !got) return false;
        at += got;
    }
    return size == n && !memcmp(buf, d, n);
}

APPS_TEST(broadsheet)
{
    static char doc[BS_CAP], other[BS_CAP];
    broadsheet_t *b = &BS;
    int64_t note = 0;
    broadsheet_init(b, &LINK);
    CHECK(zxf_mkdir("papers") == 0, "papers");

    /* nothing open: nothing to fit, note, save or print, nothing asked */
    int before = sent;
    CHECK(broadsheet_fit(b, 800, 600) == ZXM_E_STATE &&
              broadsheet_note_add(b, 1, 0, 0, 1, 0, "", &note) == ZXM_E_STATE &&
              broadsheet_notes_save(b) == ZXM_E_STATE &&
              broadsheet_print(b, "ipp://127.0.0.1/ipp/print", 1) == ZXM_E_STATE &&
              broadsheet_open(b, NULL) == ZXM_E_ARG && broadsheet_open(b, "") == ZXM_E_ARG &&
              broadsheet_open(b, "papers/a.txt") == ZXM_E_ARG && sent == before,
          "broadsheet: nothing open, or no PDF named: refused, nothing sent");
    CHECK(broadsheet_open(b, "papers/none.pdf") == ZXM_E_REMOTE &&
              LINK.err_code == BROADSHEET_E_FILES && !b->path[0],
          "broadsheet: a document that is not there: zxvd says so");

    /* BM1..BM3: a well formed A4 document; its page from /MediaBox */
    uint32_t dn = bs_pdf(doc, "%PDF-1.4", "/MediaBox [0 0 595.28 841.89]", 0, -1, -1, true);
    CHECK(bs_put("papers/a.pdf", doc, dn), "a.pdf");
    CHECK(broadsheet_open(b, "papers/a.PDF") == ZXM_E_REMOTE, "names are exact");
    CHECK(broadsheet_open(b, "papers/a.pdf") == ZXM_OK && b->accepted == 1 &&
              b->fault == BROADSHEET_F_NONE && b->header == 1 && b->size == dn && b->nxref == 4 &&
              b->tables == 1 && b->xref[1] == 9 && b->box_found && b->page_w == 595 &&
              b->page_h == 841 && b->notes_loaded && !b->notes_found && b->nnotes == 0 &&
              !strcmp(b->notes_path, "papers/a.pdf.notes"),
          "broadsheet: opened, startxref and three entries inside, A4: %d %lld %s", b->fault,
          (long long) b->fault_at, LINK.err);

    /* BM4: a point is 4/3 of a pixel, A4 is 793 x 1121 pixels */
    CHECK(broadsheet_fit(b, 793, 1121) == ZXM_OK && b->zoom == 10000, "broadsheet: A4 at its size");
    CHECK(broadsheet_fit(b, 1600, 900) == ZXM_OK && b->zoom == 8028 &&
              b->zoom == broadsheet_fit_zoom(793, 1121, 1600, 900),
          "broadsheet: a wide view, the height fits: %lld", (long long) b->zoom);
    CHECK(broadsheet_fit(b, 100000, 900) == ZXM_E_ARG && broadsheet_fit(b, -1, 900) == ZXM_E_ARG &&
              b->zoom == 8028,
          "broadsheet: a view past BR3: refused, the zoom kept");

    /* BM5: notes, kept beside the document */
    CHECK(broadsheet_note_add(b, 1, 72, 700, BROADSHEET_HIGHLIGHT, INT64_C(1700000000000),
                              "Check the total", &note) == ZXM_OK &&
              note == 1 &&
              broadsheet_note_add(b, 1, 300, 120, BROADSHEET_NOTE, INT64_C(1700000000500),
                                  "Ask Ana \"why\"", &note) == ZXM_OK &&
              note == 2 && b->nnotes == 2 && b->notes_dirty,
          "broadsheet: two notes");
    CHECK(broadsheet_note_add(b, 1, 0, 0, 4, 0, "", &note) == ZXM_E_ARG &&
              broadsheet_note_add(b, 0, 0, 0, 1, 0, "", &note) == ZXM_E_ARG &&
              broadsheet_note_add(b, 1, -1, 0, 1, 0, "", &note) == ZXM_E_ARG &&
              broadsheet_note_add(b, 1, 0, 0, 1, 0, "two\nlines", &note) == ZXM_E_ARG &&
              b->nnotes == 2,
          "broadsheet: a kind, page or place BR4 refuses, a control character: refused");
    {
        char long_text[BROADSHEET_TEXT + 2];
        memset(long_text, 'a', sizeof long_text - 1u);
        long_text[BROADSHEET_TEXT + 1] = 0;
        CHECK(broadsheet_note_add(b, 1, 0, 0, 1, 0, long_text, &note) == ZXM_E_ARG &&
                  b->nnotes == 2,
              "broadsheet: a text past %u bytes", BROADSHEET_TEXT);
    }
    CHECK(broadsheet_notes_save(b) == ZXM_OK && b->notes_found && !b->notes_dirty,
          "broadsheet: saved: %s", LINK.err);
    {
        static const char want[] = "broadsheet notes 1\n"
                                   "1 1 72 700 1 1700000000000 Check the total\n"
                                   "2 1 300 120 2 1700000000500 Ask Ana \"why\"\n";
        CHECK(bs_same("papers/a.pdf.notes", want, sizeof want - 1u),
              "broadsheet: the notes file beside it");
        CHECK(bs_same("papers/a.pdf", doc, dn), "broadsheet: the document is untouched");
    }
    /* opened again: the notes come back; a removed number is never reused */
    CHECK(broadsheet_open(b, "papers/a.pdf") == ZXM_OK && b->nnotes == 2 && b->notes_found &&
              b->note[1].note == 2 && b->note[1].stamp == INT64_C(1700000000500) &&
              !strcmp(b->note[1].text, "Ask Ana \"why\"") && b->zoom == 0,
          "broadsheet: the notes read back: %s", LINK.err);
    CHECK(broadsheet_note_remove(b, 7) == ZXM_E_ARG && broadsheet_note_remove(b, 1) == ZXM_OK &&
              b->nnotes == 1 &&
              broadsheet_note_add(b, 2, 10, 10, BROADSHEET_UNDERLINE, 5, "", &note) == ZXM_OK &&
              note == 3 && broadsheet_notes_save(b) == ZXM_OK,
          "broadsheet: one removed, the next is 3: %s", LINK.err);
    {
        static const char want[] = "broadsheet notes 1\n"
                                   "2 1 300 120 2 1700000000500 Ask Ana \"why\"\n"
                                   "3 2 10 10 3 5 \n";
        CHECK(bs_same("papers/a.pdf.notes", want, sizeof want - 1u) &&
                  bs_same("papers/a.pdf", doc, dn),
              "broadsheet: the notes file replaced, the document untouched");
    }

    /* BM5: a notes file made after opening is never replaced */
    CHECK(bs_put("papers/b.pdf", doc, dn) && broadsheet_open(b, "papers/b.pdf") == ZXM_OK &&
              !b->notes_found && bs_put("papers/b.pdf.notes", "mine\n", 5) &&
              broadsheet_note_add(b, 1, 1, 1, 1, 1, "x", &note) == ZXM_OK &&
              broadsheet_notes_save(b) == ZXM_E_REMOTE &&
              bs_same("papers/b.pdf.notes", "mine\n", 5),
          "broadsheet: the save is exclusive when there was no notes file: %s", LINK.err);
    /* ... and one that does not parse is refused whole, never overwritten */
    CHECK(broadsheet_open(b, "papers/b.pdf") == ZXM_E_ARG && b->accepted == 1 &&
              b->fault == BROADSHEET_F_NONE && !b->notes_loaded && b->nnotes == 0,
          "broadsheet: a notes file that does not parse");
    before = sent;
    CHECK(broadsheet_notes_save(b) == ZXM_E_STATE &&
              broadsheet_note_add(b, 1, 1, 1, 1, 1, "", &note) == ZXM_E_STATE && sent == before &&
              bs_same("papers/b.pdf.notes", "mine\n", 5),
          "broadsheet: nothing saved over it");
    {
        static const char *bad[] = {
            "broadsheet notes 1\n1 1 0 0 4 0 x\n",                /* a kind BR4 refuses */
            "broadsheet notes 1\n1 1 0 0 1 0 x\n1 1 0 0 1 0 y\n", /* one number twice */
            "broadsheet notes 1\n1 1 0 0 1 0 x",                  /* no end of line */
            "broadsheet notes 1\n1 1 0 0 1 x\n",                  /* a number missing */
            "broadsheet notes 2\n",                               /* another form */
        };
        for (uint32_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
            CHECK(bs_put("papers/b.pdf.notes", bad[i], (uint32_t) strlen(bad[i])) &&
                      broadsheet_open(b, "papers/b.pdf") == ZXM_E_ARG && !b->notes_loaded,
                  "broadsheet: bad notes file %u refused", i);
        static const char good[] = "broadsheet notes 1\n9999 999999 99999 99999 3 "
                                   "999999999999999 \xC3\xA9t\xC3\xA9\n";
        CHECK(bs_put("papers/b.pdf.notes", good, sizeof good - 1u) &&
                  broadsheet_open(b, "papers/b.pdf") == ZXM_OK && b->nnotes == 1 &&
                  b->note[0].note == 9999 && !strcmp(b->note[0].text, "\xC3\xA9t\xC3\xA9") &&
                  broadsheet_note_add(b, 1, 1, 1, 1, 1, "", &note) == ZXM_E_ARG,
              "broadsheet: the widest note read back; note 10000 is past BR4");
    }

    /* BM2: what is not well formed is refused, and nothing more is read */
    {
        uint32_t n = bs_pdf(other, "%XDF-1.4", "", 0, -1, -1, true);
        CHECK(bs_put("papers/c.pdf", other, n) && broadsheet_open(b, "papers/c.pdf") == ZXM_E_ARG &&
                  b->accepted == 0 && b->fault == BROADSHEET_F_HEADER && b->nxref == 0 &&
                  !strcmp(b->path, "papers/c.pdf"),
              "broadsheet: no header (BR2 1)");
        before = sent;
        CHECK(broadsheet_fit(b, 800, 600) == ZXM_E_STATE &&
                  broadsheet_print(b, "ipp://127.0.0.1/ipp/print", 1) == ZXM_E_STATE &&
                  broadsheet_notes_save(b) == ZXM_E_STATE && sent == before,
              "broadsheet: a refused document is not fitted, saved or printed");
        n = bs_pdf(other, "%PDF-1.7", "", 0, 99999, -1, true);
        CHECK(bs_put("papers/d.pdf", other, n) && broadsheet_open(b, "papers/d.pdf") == ZXM_E_ARG &&
                  b->fault == BROADSHEET_F_BOUNDS && b->fault_at == 99999 && b->nxref == 2,
              "broadsheet: an entry past the end (BR2 2): %d", b->fault);
        /* startxref 0 rewritten as the file's own size: "<size>\n%%EOF\n" */
        n = bs_pdf(other, "%PDF-1.7", "", 0, -1, 0, true) - 8u;
        uint32_t size = n + 7u + 1u;
        while ((uint32_t) snprintf(NULL, 0, "%u", size) != size - n - 7u) size++;
        n += (uint32_t) snprintf(other + n, BS_CAP - n, "%u\n%%%%EOF\n", size);
        CHECK(n == size && bs_put("papers/e.pdf", other, n) &&
                  broadsheet_open(b, "papers/e.pdf") == ZXM_E_ARG &&
                  b->fault == BROADSHEET_F_BOUNDS && b->nxref == 0,
              "broadsheet: startxref at the size, one past the last byte: %d", b->fault);
        n = bs_pdf(other, "%PDF-1.7", "", 0, -1, 9, true);
        CHECK(bs_put("papers/f.pdf", other, n) && broadsheet_open(b, "papers/f.pdf") == ZXM_E_ARG &&
                  b->fault == BROADSHEET_F_STREAM && b->fault_at == 9,
              "broadsheet: startxref at an object: a cross-reference stream, refused: %d",
              b->fault);
        n = bs_pdf(other, "%PDF-1.7", "", 0, -1, 1, true);
        CHECK(bs_put("papers/g.pdf", other, n) && broadsheet_open(b, "papers/g.pdf") == ZXM_E_ARG &&
                  b->fault == BROADSHEET_F_FORM,
              "broadsheet: startxref inside the file at no table");
        n = bs_pdf(other, "%PDF-1.7", "", 0, -1, -1, false);
        CHECK(bs_put("papers/h.pdf", other, n) && broadsheet_open(b, "papers/h.pdf") == ZXM_E_ARG &&
                  b->fault == BROADSHEET_F_FORM && b->nxref == 0,
              "broadsheet: no %%%%EOF after startxref");
        CHECK(bs_put("papers/i.pdf", "%PDF-1.4\n", 9) &&
                  broadsheet_open(b, "papers/i.pdf") == ZXM_E_ARG && b->fault == BROADSHEET_F_FORM,
              "broadsheet: a header and nothing else");
        CHECK(bs_put("papers/j.pdf", "", 0) && broadsheet_open(b, "papers/j.pdf") == ZXM_E_ARG &&
                  b->fault == BROADSHEET_F_HEADER,
              "broadsheet: an empty file");
    }

    /* BM2: an incremental update, its /Prev followed; a /Prev loop refused */
    {
        uint32_t n = bs_pdf(other, "%PDF-1.4", "", 0, -1, -1, true);
        uint32_t first = 0;
        for (uint32_t i = 1; i + 4 < n; i++)
            if (!memcmp(other + i, "xref", 4) && other[i - 1] == '\n') first = i;
        uint32_t second = n;
        n += (uint32_t) snprintf(other + n, BS_CAP - n,
                                 "xref\n3 1\n%010u 00000 n\r\ntrailer\n<< /Size 4 /Prev %u >>\n"
                                 "startxref\n%u\n%%%%EOF\n",
                                 first, first, second);
        CHECK(bs_put("papers/k.pdf", other, n) && broadsheet_open(b, "papers/k.pdf") == ZXM_OK &&
                  b->tables == 2 && b->nxref == 6 && b->xref[0] == second && b->xref[2] == first &&
                  !b->box_found && b->page_w == BROADSHEET_LETTER_W &&
                  b->page_h == BROADSHEET_LETTER_H,
              "broadsheet: two tables by /Prev, every offset inside; no box, Letter: %d %u %u",
              b->fault, b->tables, b->nxref);
        uint32_t m = (uint32_t) snprintf(other + second, BS_CAP - second,
                                         "xref\n3 1\n%010u 00000 n\r\ntrailer\n<< /Size 4 /Prev "
                                         "%u >>\nstartxref\n%u\n%%%%EOF\n",
                                         first, second, second);
        CHECK(bs_put("papers/l.pdf", other, second + m) &&
                  broadsheet_open(b, "papers/l.pdf") == ZXM_E_ARG &&
                  b->fault == BROADSHEET_F_FORM && b->fault_at == second,
              "broadsheet: a /Prev to itself is a loop");
    }

    /* BM2 across read windows: a document longer than three windows */
    {
        uint32_t n =
            bs_pdf(other, "%PDF-2.0", "/MediaBox [ -10 -10.5 602 782 ]", 30000, -1, -1, true);
        CHECK(n > 3u * BROADSHEET_WIN && bs_put("papers/m.pdf", other, n) &&
                  broadsheet_open(b, "papers/m.pdf") == ZXM_OK && b->nxref == 4 &&
                  b->xref[3] > 3 * (int64_t) BROADSHEET_WIN && b->page_w == 612 && b->page_h == 792,
              "broadsheet: %u bytes read in windows, the box's fractions dropped: %d %s", n,
              b->fault, LINK.err);
    }

    /* BM6: only an accepted document is printed, as it is */
    char uri[96];
    CHECK(fake_start(), "broadsheet: the fake printer");
    snprintf(uri, sizeof uri, "ipp://127.0.0.1:%d/ipp/print", FAKE_PORT);
    CHECK(broadsheet_open(b, "papers/a.pdf") == ZXM_OK, "a.pdf again");
    before = sent;
    CHECK(broadsheet_print(b, "", 1) == ZXM_E_ARG && broadsheet_print(b, uri, 1000) == ZXM_E_ARG &&
              b->job == 0 && sent == before,
          "broadsheet: no printer, too many copies: nothing asked");
    CHECK(broadsheet_print(b, uri, 2) == ZXM_OK && b->job == 77 && b->printer_known &&
              b->accepting && b->takes_pdf && !strcmp(b->job_state, "pending") &&
              strstr(LINK.req, "\"path\":\"papers/a.pdf\"") && strstr(LINK.req, "\"copies\":2"),
          "broadsheet: printed: job %lld %s (%s)", (long long) b->job, b->job_state, LINK.err);
    {
        char want[64];
        snprintf(want, sizeof want, "doc=%u:%%PDF-1.4", (unsigned) dn);
        CHECK(strstr(fake_get("/fake/last"), "fmt=application/pdf") &&
                  strstr(fake_get("/fake/last"), "copies=2") &&
                  strstr(fake_get("/fake/last"), want),
              "broadsheet: the printer got the document as it is: %s", fake_get("/fake/last"));
    }
    fake_stop();
}
