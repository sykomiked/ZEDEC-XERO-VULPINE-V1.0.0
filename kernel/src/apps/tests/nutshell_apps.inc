/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Nutshell's model against zxvd's real dispatcher (LINK): each deck is a
 * file in the Banker's Nutshell folder, read with files.read and written
 * with files.write. */
#include "nutshell_model.h"

static nutshell_t NUT, NUT2;

static bool nut_file_is(const char *path, const char *want)
{
    static uint8_t buf[NUTSHELL_TEXT_MAX + 1];
    uint32_t got = 0;
    uint64_t size = 0;
    if (zxf_read(path, 0, buf, NUTSHELL_TEXT_MAX, true, &got, &size) != 0) return false;
    buf[got] = 0;
    return size == got && !strcmp((const char *) buf, want);
}

static bool nut_put(const char *path, const char *text)
{
    uint64_t size;
    return zxf_write(path, (const uint8_t *) text, (uint32_t) strlen(text), ZXF_W_REPLACE, false,
                     &size) == 0;
}

static bool nut_absent(const char *path)
{
    zxf_entry_t e;
    return zxf_stat(path, &e) == ZXF_E_NOTFOUND;
}

/* the same deck, field by field (a struct's padding is not compared) */
static bool nut_same_deck(const nutshell_t *a, const nutshell_t *b)
{
    if (a->n != b->n || a->deck != b->deck || a->made != b->made || a->reviewed != b->reviewed)
        return false;
    for (uint32_t i = 0; i < a->n; i++) {
        const nutshell_card_t *x = &a->c[i], *y = &b->c[i];
        if (x->box != y->box || x->due != y->due || x->right != y->right || x->wrong != y->wrong ||
            strcmp(x->front, y->front) || strcmp(x->back, y->back))
            return false;
    }
    return true;
}

/* 2026-10-19, a Monday, is day 20745 */
#define NUT_MON 20745

APPS_TEST(nutshell)
{
    /* the rules the model keeps (NM2) */
    CHECK(nutshell_card_valid(1, 0, 0, 0) &&
              nutshell_card_valid(NUTSHELL_BOX_MAX, SOLSTICE_DAY_MAX, NUTSHELL_TALLY_MAX,
                                  NUTSHELL_TALLY_MAX) &&
              !nutshell_card_valid(0, 0, 0, 0) && !nutshell_card_valid(6, 0, 0, 0) &&
              !nutshell_card_valid(1, -1, 0, 0) &&
              !nutshell_card_valid(1, SOLSTICE_DAY_MAX + 1, 0, 0) &&
              !nutshell_card_valid(1, 0, -1, 0) &&
              !nutshell_card_valid(1, 0, NUTSHELL_TALLY_MAX + 1, 0) &&
              !nutshell_card_valid(1, 0, 0, NUTSHELL_TALLY_MAX + 1),
          "card: valid at its edges, refused past them");
    static char side[NUTSHELL_SIDE_MAX + 2];
    memset(side, 'a', NUTSHELL_SIDE_MAX);
    CHECK(nutshell_side_ok("cat") && nutshell_side_ok(side) && nutshell_side_ok("caf\xc3\xa9") &&
              nutshell_side_ok("\xf0\x9f\x90\x88") && !nutshell_side_ok("") &&
              !nutshell_side_ok(NULL) && !nutshell_side_ok("a\tb") &&
              !nutshell_side_ok("two\nlines") && !nutshell_side_ok("\xff") &&
              !nutshell_side_ok("caf\xc3") && !nutshell_side_ok("\xed\xa0\x80") &&
              !nutshell_side_ok("\xc0\xaf"),
          "side: 1 to 48 bytes of UTF-8, no control byte");
    side[NUTSHELL_SIDE_MAX] = 'a';
    CHECK(!nutshell_side_ok(side), "side: 49 bytes refused");

    /* nothing is written before a deck is opened */
    CHECK(zxf_use_space(NULL) == 0, "home");
    nutshell_init(&NUT, &LINK);
    int before = sent;
    CHECK(nutshell_add_card(&NUT, "cat", "gato", NUT_MON, true) == ZXM_E_STATE &&
              nutshell_review(&NUT, 0, 1, NUT_MON, true) == ZXM_E_STATE && sent == before &&
              !NUT.loaded,
          "not opened: nothing written");
    CHECK(nutshell_open(&NUT, 0) == ZXM_E_ARG &&
              nutshell_open(&NUT, NUTSHELL_DECK_MAX + 1) == ZXM_E_ARG && sent == before,
          "a deck number outside 1..999999: not asked");

    /* no file yet is a new deck; nothing is sent without the owner's word (NM4) */
    CHECK(nutshell_open(&NUT, 1) == ZXM_OK && NUT.loaded && !NUT.exists && NUT.n == 0 &&
              NUT.deck == 1,
          "no deck yet: %s", LINK.err);
    before = sent;
    CHECK(nutshell_add_card(&NUT, "cat", "gato", NUT_MON, false) == ZXM_E_STATE && sent == before &&
              NUT.n == 0 && !NUT.exists && nut_absent("Nutshell/1.deck"),
          "add without the owner's word: refused, nothing sent, no file");
    CHECK(nutshell_add_card(&NUT, "", "gato", NUT_MON, true) == ZXM_E_ARG &&
              nutshell_add_card(&NUT, "cat", "a\tb", NUT_MON, true) == ZXM_E_ARG &&
              nutshell_add_card(&NUT, "cat", "gato", -1, true) == ZXM_E_ARG &&
              nutshell_add_card(&NUT, "cat", "gato", SOLSTICE_DAY_MAX + 1, true) == ZXM_E_ARG &&
              sent == before,
          "bad cards are refused before anything is asked");

    /* three cards, each in box 1 and due the day it was added */
    CHECK(nutshell_add_card(&NUT, "cat", "gato", NUT_MON, true) == ZXM_OK && NUT.exists &&
              NUT.n == 1 && NUT.made == NUT_MON &&
              nutshell_add_card(&NUT, "dog", "perro", NUT_MON, true) == ZXM_OK &&
              nutshell_add_card(&NUT, "coffee", "caf\xc3\xa9", NUT_MON, true) == ZXM_OK &&
              NUT.n == 3,
          "add three cards: %s", LINK.err);
    CHECK(nut_file_is("Nutshell/1.deck", "nutshell 1\n20745 0\n"
                                         "1 20745 0 0\tcat\tgato\n"
                                         "1 20745 0 0\tdog\tperro\n"
                                         "1 20745 0 0\tcoffee\tcaf\xc3\xa9\n"),
          "the deck is one file in the Banker (NM1)");

    /* answering: up one box on right, back to box 1 on wrong (NM3) */
    before = sent;
    CHECK(nutshell_review(&NUT, 0, 1, NUT_MON, false) == ZXM_E_STATE && sent == before &&
              NUT.c[0].box == 1 && NUT.c[0].due == NUT_MON && NUT.reviewed == 0,
          "answer without the owner's word: refused, nothing sent, the card as it was");
    CHECK(nutshell_review(&NUT, 0, 1, NUT_MON, true) == ZXM_OK && NUT.c[0].box == 2 &&
              NUT.c[0].due == NUT_MON + 2 && NUT.c[0].right == 1 && NUT.reviewed == NUT_MON,
          "cat right on Monday: box 2, due Wednesday: %s", LINK.err);
    CHECK(nutshell_review(&NUT, 1, 0, NUT_MON, true) == ZXM_OK && NUT.c[1].box == 1 &&
              NUT.c[1].due == NUT_MON + 1 && NUT.c[1].wrong == 1,
          "dog wrong on Monday: box 1, due Tuesday: %s", LINK.err);
    CHECK(nutshell_review(&NUT, 3, 1, NUT_MON, true) == ZXM_E_ARG &&
              nutshell_review(&NUT, 0, 2, NUT_MON, true) == ZXM_E_ARG &&
              nutshell_review(&NUT, 0, 1, -1, true) == ZXM_E_ARG,
          "no such card, an answer that is no flag, a day before 1970: refused");

    /* what the deck says */
    uint32_t n = 9, at = 9;
    int64_t boxes[NUTSHELL_BOX_MAX];
    CHECK(nutshell_due_total(&NUT, NUT_MON, &n) == ZXM_OK && n == 1 &&
              nutshell_due_total(&NUT, NUT_MON + 1, &n) == ZXM_OK && n == 2 &&
              nutshell_due_total(&NUT, NUT_MON + 2, &n) == ZXM_OK && n == 3 &&
              nutshell_due_total(&NUT, NUT_MON - 1, &n) == ZXM_OK && n == 0 &&
              nutshell_due_total(&NUT, -1, &n) == ZXM_E_ARG,
          "due: one on Monday, two on Tuesday, all three on Wednesday");
    CHECK(nutshell_next_card(&NUT, NUT_MON + 2, &at) == ZXM_OK && at == 2 &&
              nutshell_next_card(&NUT, NUT_MON - 1, &at) == ZXM_E_STATE,
          "the card due longest is shown first; none before Monday");
    CHECK(nutshell_box_counts(&NUT, boxes) == ZXM_OK && boxes[0] == 2 && boxes[1] == 1 &&
              boxes[2] == 0 && boxes[3] == 0 && boxes[4] == 0,
          "two cards in box 1, one in box 2");
    CHECK(nutshell_review(&NUT, 0, 1, NUT_MON + 2, true) == ZXM_OK && NUT.c[0].box == 3 &&
              NUT.c[0].due == NUT_MON + 6 && NUT.reviewed == NUT_MON + 2,
          "cat right again on Wednesday: box 3, due four days on");
    CHECK(nut_file_is("Nutshell/1.deck", "nutshell 1\n20745 20747\n"
                                         "3 20751 2 0\tcat\tgato\n"
                                         "1 20746 0 1\tdog\tperro\n"
                                         "1 20745 0 0\tcoffee\tcaf\xc3\xa9\n"),
          "each answer is kept in the deck's file");

    /* a second model reads the deck back */
    nutshell_init(&NUT2, &LINK);
    CHECK(nutshell_open(&NUT2, 1) == ZXM_OK && NUT2.exists && nut_same_deck(&NUT2, &NUT),
          "read back: %u cards %s", NUT2.n, LINK.err);

    /* a due day past 9999-12-31 is refused, the card as it was (NM3) */
    nutshell_init(&NUT2, &LINK);
    CHECK(nutshell_open(&NUT2, 3) == ZXM_OK &&
              nutshell_add_card(&NUT2, "last", "day", SOLSTICE_DAY_MAX, true) == ZXM_OK,
          "a card due on 9999-12-31: %s", LINK.err);
    before = sent;
    CHECK(nutshell_review(&NUT2, 0, 1, SOLSTICE_DAY_MAX, true) == ZXM_E_ARG && sent == before &&
              NUT2.c[0].box == 1 && NUT2.c[0].due == SOLSTICE_DAY_MAX,
          "due past 9999-12-31: refused, nothing sent");

    /* a tally that would pass six digits is refused */
    CHECK(nut_put("Nutshell/6.deck", "nutshell 1\n0 0\n2 5 999999 0\tq\ta\n"), "a full tally");
    nutshell_init(&NUT2, &LINK);
    before = sent;
    CHECK(nutshell_open(&NUT2, 6) == ZXM_OK && NUT2.n == 1 &&
              nutshell_review(&NUT2, 0, 1, 10, true) == ZXM_E_TOOBIG && sent == before + 1 &&
              nutshell_review(&NUT2, 0, 0, 10, true) == ZXM_OK && NUT2.c[0].box == 1 &&
              NUT2.c[0].wrong == 1,
          "right past 999999 refused; a wrong answer still kept: %s", LINK.err);

    /* a file that is not a deck is not read, and never written over (NM1) */
    static const char *const BAD[] = {"hello\n",
                                      "nutshell 1\n0 0\n6 5 0 0\tq\ta\n",
                                      "nutshell 1\n0 0\n1 05 0 0\tq\ta\n",
                                      "nutshell 1\n0 0\n1 5 0 0\tq\n",
                                      "nutshell 1\n0 0\n1 5 0 0\t\ta\n",
                                      "nutshell 1\n0 0\n1 5 0 0\tq\ta"};
    bool refused = true;
    for (uint32_t i = 0; i < sizeof BAD / sizeof BAD[0]; i++) {
        CHECK(nut_put("Nutshell/4.deck", BAD[i]), "not a deck");
        nutshell_init(&NUT2, &LINK);
        bool r = nutshell_open(&NUT2, 4) == ZXM_E_STATE && !NUT2.loaded &&
                 nutshell_add_card(&NUT2, "q", "a", 0, true) == ZXM_E_STATE &&
                 nut_file_is("Nutshell/4.deck", BAD[i]);
        if (!r) printf("    bad deck %u\n", i);
        refused = r && refused;
    }
    CHECK(refused, "six files that are not decks: refused, left alone");

    /* a deck made meanwhile is never written over (NM5) */
    nutshell_init(&NUT2, &LINK);
    CHECK(nutshell_open(&NUT2, 5) == ZXM_OK && !NUT2.exists, "deck 5 is new");
    CHECK(nut_put("Nutshell/5.deck", "nutshell 1\n0 0\n"), "taken meanwhile");
    CHECK(nutshell_add_card(&NUT2, "mine", "yours", NUT_MON, true) == ZXM_E_REMOTE &&
              LINK.err_code == NUTSHELL_E_FILES && NUT2.n == 0 && !NUT2.exists &&
              nut_file_is("Nutshell/5.deck", "nutshell 1\n0 0\n"),
          "exclusive: refused, nothing kept: %s", LINK.err);

    /* a full deck takes no more cards */
    nutshell_init(&NUT2, &LINK);
    CHECK(nutshell_open(&NUT2, 7) == ZXM_OK, "deck 7");
    static char quotes[NUTSHELL_SIDE_MAX + 1], slashes[NUTSHELL_SIDE_MAX + 1];
    memset(quotes, '"', NUTSHELL_SIDE_MAX);
    memset(slashes, '\\', NUTSHELL_SIDE_MAX);
    bool filled = true;
    for (uint32_t i = 0; i < NUTSHELL_CARDS && filled; i++)
        filled = nutshell_add_card(&NUT2, quotes, slashes, NUT_MON, true) == ZXM_OK;
    before = sent;
    CHECK(filled && NUT2.n == NUTSHELL_CARDS &&
              nutshell_add_card(&NUT2, "q", "a", NUT_MON, true) == ZXM_E_TOOBIG && sent == before,
          "64 cards of 48 quotes and 48 backslashes fit one request; a 65th is refused: %s",
          LINK.err);
    nutshell_init(&NUT, &LINK);
    CHECK(nutshell_open(&NUT, 7) == ZXM_OK && nut_same_deck(&NUT, &NUT2),
          "the full deck reads back");
}
