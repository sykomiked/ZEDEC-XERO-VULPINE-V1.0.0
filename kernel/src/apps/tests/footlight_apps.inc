/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Footlight's model against zxvd's real dispatcher (LINK): a deck read
 * and refused (FM1, FM2), fitted to windows (FM3), rehearsed (FM4),
 * stepped (FM5), saved and opened again in the scratch Hoard, a handout
 * written and printed on the fake printer (FM6). pspace.* and hall.* are
 * served in the daemon by zxvd_pspace.c, which this test does not link (the
 * dispatcher answers -32601), so the hall's answers are made up in their
 * documented shape (FAKE, FM7). */
#include "footlight_model.h"

#define FT_HALL  "bafkreihallforfootlight"
#define FT_MEDIA "bafkreithedeckitself"
#define FT_OK    "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":"

static footlight_t FT, FT2;

static const char FT_DECK[] = "# Welcome\n"
                              "@ 30\n"
                              "Hello, everyone\n"
                              "\n"
                              "# Plan\n"
                              "First\n"
                              "Second\n"
                              "@ 45\n"
                              "# Thanks\n";

/* a file read straight through zxvd, as another app would */
static const char *ft_read(const char *path)
{
    static char text[FOOTLIGHT_TEXT_MAX + 1];
    zxm_begin(&LINK, "files.read");
    zxm_p_str(&LINK, "path", path);
    if (zxm_call(&LINK) != ZXM_OK) return "";
    int32_t t = zxm_get(&LINK, LINK.result, "text");
    if (t < 0 || w4_json_str(LINK.resp, &LINK.tok[t], text, sizeof text) < 0) return "";
    return text;
}

APPS_TEST(footlight)
{
    footlight_t *f = &FT;
    static char out[FOOTLIGHT_TEXT_MAX + 1];
    char clock[24];
    int64_t over = 0;

    footlight_init(f, &LINK);
    CHECK(f->n == 0 && f->cur == 0 && f->mode == FOOTLIGHT_ADVANCE && f->deck == 1 &&
              footlight_step(f) == ZXM_E_STATE && footlight_rehearsal(f, &over) == ZXM_E_STATE &&
              footlight_handout(f, "h.pdf") == ZXM_E_STATE,
          "footlight: no deck: nothing to step, time or hand out");

    /* FM1, FM2: a deck read, each slide's layout and record */
    CHECK(footlight_set_text(f, FT_DECK, sizeof FT_DECK - 1u) == ZXM_OK && f->n == 3 &&
              f->cur == 1 && !strcmp(f->slide[0].title, "Welcome") && f->slide[0].secs == 30 &&
              f->slide[0].layout == FOOTLIGHT_LAYOUT_BOTH && f->slide[0].body_len == 17 &&
              !strncmp(f->body + f->slide[0].body_at, "Hello, everyone\n\n", 17) &&
              !strcmp(f->slide[1].title, "Plan") && f->slide[1].secs == 45 &&
              !strncmp(f->body + f->slide[1].body_at, "First\nSecond\n", 13) &&
              f->slide[2].secs == 0 && f->slide[2].layout == FOOTLIGHT_LAYOUT_TITLE,
          "footlight: three slides, their titles, text and rehearsed seconds");
    CHECK(f->start_ms[0] == 0 && f->start_ms[1] == 30000 && f->start_ms[2] == 75000,
          "footlight: each slide's place in the hall's run, an unrehearsed one a second (FM7)");
    CHECK(footlight_record_fits(f, 1, INT64_C(1767225600000)) && !footlight_record_fits(f, 0, 0) &&
              !footlight_record_fits(f, 4, 0) &&
              !footlight_record_fits(f, 1, INT64_C(1000000000000000)) &&
              footlight_set_deck(f, 0) == ZXM_E_ARG &&
              footlight_set_deck(f, INT64_C(1000000000)) == ZXM_E_ARG &&
              footlight_set_deck(f, 999999999) == ZXM_OK && footlight_record_fits(f, 3, 1) &&
              footlight_set_deck(f, 7) == ZXM_OK,
          "footlight: a slide's record fits ^ZXFOOTLIGHT, a stamp past 15 digits does not (FL7)");
    {
        static char many[FOOTLIGHT_SLIDES * 4u + 8u], title[FOOTLIGHT_TITLE + 8u];
        uint32_t n = 0;
        for (uint32_t i = 0; i <= FOOTLIGHT_SLIDES; i++) {
            memcpy(many + n, "# s\n", 4);
            n += 4;
        }
        memcpy(title, "# ", 2);
        memset(title + 2, 'x', FOOTLIGHT_TITLE + 1u);
        static const char nul[] = "# a\0b\n";
        CHECK(footlight_set_text(f, "@ 0\n", 4) == ZXM_E_ARG &&
                  footlight_set_text(f, "# a\n@ 3601\n", 11) == ZXM_E_ARG &&
                  footlight_set_text(f, "# a\n@ 1x\n", 9) == ZXM_E_ARG &&
                  footlight_set_text(f, "# a\n@ \n", 7) == ZXM_E_ARG &&
                  footlight_set_text(f, title, FOOTLIGHT_TITLE + 3u) == ZXM_E_ARG &&
                  footlight_set_text(f, nul, sizeof nul - 1u) == ZXM_E_ARG &&
                  footlight_set_text(f, many, n) == ZXM_E_TOOBIG &&
                  footlight_set_text(f, many, FOOTLIGHT_TEXT_MAX + 1u) == ZXM_E_TOOBIG &&
                  footlight_set_text(f, NULL, 3) == ZXM_E_ARG && f->n == 3 &&
                  !strcmp(f->slide[1].title, "Plan") && f->slide[1].secs == 45,
              "footlight: no seconds, an hour and one, not a number, a long title, a NUL, "
              "65 slides, too long: refused, the deck kept");
        CHECK(footlight_set_text(f, title, FOOTLIGHT_TITLE + 2u) == ZXM_OK && f->n == 1 &&
                  strlen(f->slide[0].title) == FOOTLIGHT_TITLE &&
                  footlight_set_text(f, many, n - 4u) == ZXM_OK && f->n == FOOTLIGHT_SLIDES,
              "footlight: the longest title, and 64 slides, are taken");
    }
    CHECK(footlight_set_text(&FT2, "\n\nIntro text\n# A\n# \n#nohead\n# B\n\n", 33) == ZXM_OK &&
              FT2.n == 4 && FT2.slide[0].title[0] == 0 &&
              FT2.slide[0].layout == FOOTLIGHT_LAYOUT_TEXT &&
              FT2.slide[1].layout == FOOTLIGHT_LAYOUT_TITLE &&
              FT2.slide[2].layout == FOOTLIGHT_LAYOUT_TEXT && FT2.slide[2].title[0] == 0 &&
              FT2.slide[3].layout == FOOTLIGHT_LAYOUT_TITLE &&
              footlight_set_text(&FT2, "# \n\n", 4) == ZXM_OK &&
              FT2.slide[0].layout == FOOTLIGHT_LAYOUT_BLANK &&
              footlight_set_text(&FT2, "", 0) == ZXM_OK && FT2.n == 0 && FT2.cur == 0,
          "footlight: text before a title, '#' with no space is text, a blank slide, no slides");

    /* FM1: the deck as text, saved, opened again */
    CHECK(footlight_set_text(f, FT_DECK, sizeof FT_DECK - 1u) == ZXM_OK,
          "footlight: the deck again");
    CHECK(footlight_text(f, out, sizeof out) == 66 &&
              !strcmp(out, "# Welcome\n@ 30\nHello, everyone\n\n# Plan\n@ 45\nFirst\nSecond\n"
                           "# Thanks\n") &&
              footlight_text(f, out, 66) == -1,
          "footlight: the deck written back, each slide's seconds under its title: %s", out);
    CHECK(footlight_save(f, "") == ZXM_E_ARG && footlight_save(f, "talk.deck") == ZXM_OK &&
              !strcmp(f->path, "talk.deck") && footlight_text(f, out, sizeof out) == 66 &&
              !strcmp(ft_read("talk.deck"), out),
          "footlight: saved in the Hoard: %s", LINK.err);
    footlight_init(&FT2, &LINK);
    CHECK(footlight_open(&FT2, "talk.deck") == ZXM_OK && FT2.n == 3 && FT2.slide[1].secs == 45 &&
              !strcmp(FT2.path, "talk.deck") &&
              footlight_text(&FT2, out + 100, sizeof out - 100) == 66,
          "footlight: opened again, the same deck: %s", LINK.err);
    CHECK(footlight_open(&FT2, "none.deck") == ZXM_E_REMOTE && FT2.n == 3 &&
              footlight_open(&FT2, "") == ZXM_E_ARG,
          "footlight: a deck that is not there: zxvd's refusal, the deck kept (%s)", LINK.err);

    /* FM3: the fit */
    CHECK(footlight_fit_window(f, 1920, 1200) == ZXM_OK && f->fit.w == 1920 && f->fit.h == 1080 &&
              f->fit.left == 0 && f->fit.top == 60 &&
              footlight_fit_window(f, 2560, 1080) == ZXM_OK && f->fit.w == 1920 &&
              f->fit.h == 1080 && f->fit.left == 320 && f->fit.top == 0 &&
              footlight_fit_window(f, 1366, 769) == ZXM_OK && f->fit.w == 1366 && f->fit.h == 768 &&
              f->fit.top == 0,
          "footlight: letterboxed, pillarboxed, and a pixel left after the slide");
    CHECK(footlight_fit_window(f, -1, 100) == ZXM_E_ARG &&
              footlight_fit_window(f, 100, FOOTLIGHT_SIDE_MAX + 1) == ZXM_E_ARG &&
              f->fit.winw == 1366 && f->fit.w == 1366,
          "footlight: a window outside its sides: refused, the fit kept");

    /* FM4: rehearsal */
    CHECK(footlight_rehearsal(f, &over) == ZXM_E_STATE && footlight_set_talk(f, 0) == ZXM_E_ARG &&
              footlight_set_talk(f, FOOTLIGHT_TALK_MAX + 1) == ZXM_E_ARG &&
              footlight_set_talk(f, 120) == ZXM_OK &&
              footlight_rehearsal(f, &over) == ZXM_E_STATE &&
              footlight_rehearsal(f, NULL) == ZXM_E_ARG,
          "footlight: no talk length, then a slide not rehearsed: no total");
    CHECK(footlight_set_secs(f, 3, FOOTLIGHT_SECS_MAX + 1) == ZXM_E_ARG &&
              footlight_set_secs(f, 4, 30) == ZXM_E_ARG && footlight_set_secs(f, 3, 30) == ZXM_OK &&
              f->start_ms[2] == 75000 && footlight_rehearsal(f, &over) == ZXM_OK && over == -15 &&
              footlight_clock(over, clock, sizeof clock) == 8 && !strcmp(clock, "-0:00:15"),
          "footlight: 105 seconds rehearsed for a talk of 120: 15 under (%s)", clock);
    CHECK(footlight_set_secs(f, 1, 3600) == ZXM_OK && f->start_ms[1] == 3600000 &&
              footlight_rehearsal(f, &over) == ZXM_OK && over == 3555 &&
              footlight_clock(over, clock, sizeof clock) == 7 && !strcmp(clock, "0:59:15") &&
              footlight_clock(90061, clock, sizeof clock) == 8 && !strcmp(clock, "25:01:01") &&
              footlight_clock(1, clock, 8) == 7 && footlight_clock(1, clock, 7) == -1 &&
              footlight_set_secs(f, 1, 30) == ZXM_OK,
          "footlight: an hour on the first slide: 59:15 over, and longer than a day");
    CHECK(footlight_set_secs(f, 3, 0) == ZXM_OK && f->slide[2].secs == 0 &&
              footlight_rehearsal(f, &over) == ZXM_E_STATE,
          "footlight: a slide's seconds taken away: no total again");

    /* FM5: run order */
    CHECK(footlight_go(f, 0) == ZXM_E_ARG && footlight_go(f, 4) == ZXM_E_ARG &&
              footlight_go(f, 2) == ZXM_OK && footlight_step(f) == ZXM_OK && f->cur == 3 &&
              footlight_step(f) == ZXM_OK && f->cur == FOOTLIGHT_END &&
              footlight_step(f) == ZXM_E_STATE,
          "footlight: advance to the end, and no further");
    CHECK(footlight_set_mode(f, 3) == ZXM_E_ARG && footlight_set_mode(f, -1) == ZXM_E_ARG &&
              footlight_set_mode(f, FOOTLIGHT_LOOP) == ZXM_OK && footlight_go(f, 3) == ZXM_OK &&
              footlight_step(f) == ZXM_OK && f->cur == 1 &&
              footlight_set_mode(f, FOOTLIGHT_HOLD) == ZXM_OK && footlight_step(f) == ZXM_OK &&
              f->cur == 1 && footlight_set_mode(f, FOOTLIGHT_ADVANCE) == ZXM_OK &&
              footlight_step(f) == ZXM_OK && f->cur == 2,
          "footlight: loop back to the first, hold, advance");

    /* FM6: the handout's PDF, printed on the fake printer */
    uint32_t plen = 0;
    CHECK(footlight_handout(f, "talk.txt") == ZXM_E_ARG &&
              footlight_handout(f, "talk-handout.pdf") == ZXM_OK,
          "footlight: the handout is a PDF: %s", LINK.err);
    const char *pdf = footlight_handout_pdf(&plen);
    uint32_t talk_len = plen;
    CHECK(plen > 0 && !strncmp(pdf, "%PDF-1.4\n", 9) && strstr(pdf, "/Count 3 >>") &&
              strstr(pdf, "/F1 14 Tf\n1 0 0 1 72 720 Tm\n(1. Welcome [0:00:30]) Tj\n") &&
              strstr(pdf, "/F1 11 Tf\n1 0 0 1 72 692 Tm\n(Hello, everyone) Tj\n") &&
              strstr(pdf, "(2. Plan [0:00:45]) Tj") &&
              strstr(pdf, "1 0 0 1 72 678 Tm\n(Second) Tj") && strstr(pdf, "(3. Thanks) Tj") &&
              !strncmp(pdf + plen - 6, "%%EOF\n", 6),
          "footlight: three pages, each slide's heading and text: %u bytes", plen);
    {
        /* every object where the xref says */
        const char *sx = strstr(pdf, "startxref\n");
        uint32_t xref = 0, good = 0;
        for (const char *d = sx ? sx + 10 : "x"; *d >= '0' && *d <= '9'; d++)
            xref = xref * 10u + (uint32_t) (*d - '0');
        const char *x = xref < plen ? pdf + xref : "";
        CHECK(!strncmp(x, "xref\n0 10\n0000000000 65535 f \n", 30), "footlight: the xref: %.20s",
              x);
        for (uint32_t k = 1; *x && k <= 9; k++) {
            const char *e = x + 30 + 20 * (k - 1u);
            uint32_t off = 0;
            for (uint32_t j = 0; j < 10; j++) off = off * 10u + (uint32_t) (e[j] - '0');
            char want[16];
            snprintf(want, sizeof want, "%u 0 obj", (unsigned) k);
            good += off < plen && !strncmp(pdf + off, want, strlen(want));
        }
        CHECK(good == 9, "footlight: every object where the xref says (%u of 9)", good);
        const char *back = ft_read("talk-handout.pdf");
        CHECK(strlen(back) == plen && !strncmp(back, pdf, plen),
              "footlight: the handout in the Hoard, as composed");
    }
    {
        static char deck[256];
        static const char head[] = "# a (b) \\c \xc3\xa9\n";
        uint32_t n = sizeof head - 1u;
        memcpy(deck, head, n);
        memset(deck + n, 'x', 75);
        n += 75;
        deck[n++] = '\n';
        for (uint32_t i = 0; i < 50; i++) {
            deck[n++] = 'y';
            deck[n++] = '\n';
        }
        footlight_init(&FT2, &LINK);
        CHECK(footlight_set_text(&FT2, deck, n) == ZXM_OK &&
                  footlight_handout(&FT2, "long.pdf") == ZXM_OK,
              "footlight: a long slide's handout: %s", LINK.err);
        pdf = footlight_handout_pdf(&plen);
        CHECK(strstr(pdf, "/Count 2 >>") && strstr(pdf, "(1. a \\(b\\) \\\\c ?) Tj") &&
                  strstr(pdf, "(xxxxx) Tj") && strstr(pdf, "1 0 0 1 72 720 Tm\n(y) Tj"),
              "footlight: ( ) \\ escaped, a character past US-ASCII as '?', a line cut at 70, "
              "and the slide going on over a second page");
    }
    char uri[96];
    CHECK(fake_start(), "footlight: the fake printer");
    snprintf(uri, sizeof uri, "ipp://127.0.0.1:%d/ipp/print", FAKE_PORT);
    int before = sent;
    CHECK(footlight_print(f, "", "talk-handout.pdf", 1) == ZXM_E_ARG &&
              footlight_print(f, uri, "talk-handout.pdf", 1000) == ZXM_E_ARG &&
              footlight_print(f, uri, "talk.deck", 1) == ZXM_E_ARG && sent == before && f->job == 0,
          "footlight: no printer, too many copies, not the handout: nothing asked");
    CHECK(footlight_print(f, uri, "talk-handout.pdf", 2) == ZXM_OK && f->job > 0 &&
              f->job_state[0] && strstr(LINK.req, "\"copies\":2"),
          "footlight: printed: job %lld %s (%s)", (long long) f->job, f->job_state, LINK.err);
    {
        char want[64];
        plen = talk_len;
        snprintf(want, sizeof want, "doc=%u:%%PDF-1.4", (unsigned) plen);
        CHECK(strstr(fake_get("/fake/last"), "fmt=application/pdf") &&
                  strstr(fake_get("/fake/last"), "copies=2") &&
                  strstr(fake_get("/fake/last"), want),
              "footlight: what the printer got: %s", fake_get("/fake/last"));
    }
    fake_stop();

    /* FM7: the hall; not linked here (-32601), then answered by FAKE */
    before = sent;
    CHECK(footlight_hall_show(f) == ZXM_E_STATE && footlight_hall_follow(f) == ZXM_E_STATE &&
              footlight_hall_load(f, FT_MEDIA) == ZXM_E_STATE &&
              footlight_hall_create(f, "Talk", false) == ZXM_E_STATE &&
              footlight_hall_create(f, "", true) == ZXM_E_ARG &&
              footlight_hall_set(f, "Bad") == ZXM_E_ARG && sent == before,
          "footlight: no hall yet, and none made without the owner's word: nothing asked");
    CHECK(footlight_hall_create(f, "Talk", true) == ZXM_E_REMOTE && LINK.err_code == -32601 &&
              !f->hall_cid[0],
          "footlight: pspace.create is not linked here: -32601 (%s)", LINK.err);
    f->link = &FAKE;
    CANNED = FT_OK "{\"cid\":\"" FT_HALL "\"}}";
    CHECK(footlight_hall_create(f, "Talk", true) == ZXM_OK && !strcmp(f->hall_cid, FT_HALL) &&
              strstr(last_req, "\"method\":\"pspace.create\"") &&
              strstr(last_req, "\"kind\":\"hall\"") && strstr(last_req, "\"gov\":\"owner\""),
          "footlight: an owner's hall made (%s)", FAKE.err);
    CANNED = FT_OK "{\"cid\":\"Not a CID\"}}";
    CHECK(footlight_hall_create(f, "Talk", true) == ZXM_E_PROTOCOL && !strcmp(f->hall_cid, FT_HALL),
          "footlight: a CID zxvd never gives is refused, the hall kept");
    CANNED = FT_OK "{\"set\":true,\"state\":\"stop\",\"media\":\"" FT_MEDIA "\",\"pos_ms\":30000,"
                   "\"base_pos_ms\":30000,\"at_ms\":5,\"rate_q16\":65536,\"now_ms\":5,"
                   "\"viewers\":4,\"may_control\":true}}";
    CHECK(footlight_hall_load(f, "Not a CID") == ZXM_E_ARG &&
              footlight_hall_load(f, FT_MEDIA) == ZXM_OK && f->hall.set &&
              f->hall.state == FOOTLIGHT_HALL_STOP && f->hall.viewers == 4 &&
              strstr(last_req, "\"method\":\"hall.load\"") &&
              strstr(last_req, "\"media\":\"" FT_MEDIA) && strstr(last_req, "\"pos_ms\":30000"),
          "footlight: the deck loaded at the slide shown, slide 2 at 30 s (%s)", FAKE.err);
    CANNED = FT_OK "{\"set\":true,\"state\":\"stop\",\"media\":\"" FT_MEDIA "\",\"pos_ms\":75000,"
                   "\"viewers\":4,\"may_control\":true}}";
    CHECK(footlight_step(f) == ZXM_OK && f->cur == 3 && footlight_hall_show(f) == ZXM_OK &&
              strstr(last_req, "\"method\":\"hall.seek\"") &&
              strstr(last_req, "\"pos_ms\":75000") && f->hall.pos_ms == 75000,
          "footlight: the next slide shown to every viewer (%s)", FAKE.err);
    CANNED = FT_OK "{\"set\":true,\"state\":\"play\",\"media\":\"" FT_MEDIA "\",\"pos_ms\":29999,"
                   "\"viewers\":4,\"may_control\":false}}";
    CHECK(footlight_hall_follow(f) == ZXM_OK && f->cur == 1 && !f->hall.may_control &&
              strstr(last_req, "\"method\":\"hall.status\"") && !strstr(last_req, "pos_ms"),
          "footlight: a viewer follows the presenter, 29.999 s is still slide 1");
    CANNED = FT_OK "{\"set\":true,\"state\":\"play\",\"media\":\"" FT_MEDIA "\",\"pos_ms\":"
                   "99000000,\"viewers\":4}}";
    CHECK(footlight_hall_follow(f) == ZXM_OK && f->cur == 3 && footlight_slide_at(f, 30000) == 2 &&
              footlight_slide_at(f, 0) == 1 && footlight_slide_at(&FT2, 0) == 1,
          "footlight: past the run is the last slide");
    CANNED = FT_OK "{\"set\":true,\"state\":\"dance\",\"media\":\"x\",\"pos_ms\":1}}";
    CHECK(footlight_hall_follow(f) == ZXM_E_PROTOCOL && f->cur == 3 &&
              f->hall.state == FOOTLIGHT_HALL_PLAY,
          "footlight: a state zxvd never says is refused, the hall kept");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"error\":{\"code\":-32032,\"message\":\"only the "
             "hall's owner controls playback\"}}";
    CHECK(footlight_hall_show(f) == ZXM_E_REMOTE && FAKE.err_code == -32032 &&
              f->hall.state == FOOTLIGHT_HALL_PLAY,
          "footlight: an owner's hall refuses a viewer, shown as is");
    CANNED = NULL;
    f->link = &LINK;
    CHECK(footlight_hall_set(f, FT_HALL) == ZXM_OK && !f->hall.set,
          "footlight: a hall joined by its CID");
}
