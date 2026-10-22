/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Margins' model against zxvd's real dispatcher (LINK): notes written
 * with files.write into the test's scratch Hoard and listed, read, tagged,
 * pinned, renamed, found with search.query, put in the Trash, put back and
 * removed for good, then loaded again by a second model. */
#include "margins_model.h"

/* a file written straight through zxvd, as another app would */
static bool mg_write(const char *path, const char *text)
{
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", path);
    zxm_p_str(&LINK, "text", text);
    return zxm_call(&LINK) == ZXM_OK;
}

static bool mg_mkdir(const char *path)
{
    zxm_begin(&LINK, "files.mkdir");
    zxm_p_str(&LINK, "path", path);
    return zxm_call(&LINK) == ZXM_OK;
}

/* the titles of the last hits, in order */
static bool mg_hits(const margins_t *m, const char *const *want, uint32_t n)
{
    if (m->nhits != n) return false;
    for (uint32_t i = 0; i < n; i++)
        if (strcmp(m->note[m->hit[i]].title, want[i])) return false;
    return true;
}

/* the notes of tag `t` as the model counted them (MR5) */
static int64_t mg_tag_notes(const margins_t *m, const char *t)
{
    for (uint32_t i = 0; i < m->ntags; i++)
        if (!strcmp(m->tag[i], t)) return m->tag_notes[i];
    return -1;
}

APPS_TEST(margins)
{
    static margins_t MG, MG2;
    static char big[MARGINS_BODY_MAX + 2], many[17 * 6 + 1], longt[MARGINS_TITLE_MAX + 2];
    const int64_t NOW = 1760000000;
    memset(big, 'z', MARGINS_BODY_MAX + 1);
    memset(longt, 't', MARGINS_TITLE_MAX + 1);
    for (uint32_t i = 0; i < 17u; i++) {
        many[i * 6u] = '#', many[i * 6u + 1u] = 't', many[i * 6u + 2u] = 'a';
        many[i * 6u + 3u] = 'g', many[i * 6u + 4u] = (char) ('a' + i), many[i * 6u + 5u] = ' ';
    }

    /* the rules (MR1-MR7) */
    CHECK(margins_size_ok(120, 8000, 16) && !margins_size_ok(121, 0, 0) &&
              margins_key(1, 0) > margins_key(0, MARGINS_STAMP_MAX) &&
              margins_tag_record_ok(9999, 9999, 999999999999) &&
              margins_trash_days(NOW, NOW) == 30 && margins_fill_x100(4000) == 5000,
          "margins: the rules");

    /* MM5: nothing before the load, and a bad note never sent */
    margins_init(&MG, &LINK);
    int before = sent;
    CHECK(margins_save(&MG, "Shopping", "milk", 4, true) == ZXM_E_STATE &&
              margins_save(&MG, "", "milk", 4, true) == ZXM_E_ARG &&
              margins_save(&MG, longt, "milk", 4, true) == ZXM_E_ARG &&
              margins_save(&MG, "a/b", "milk", 4, true) == ZXM_E_ARG &&
              margins_save(&MG, "a:b", "milk", 4, true) == ZXM_E_ARG &&
              margins_save(&MG, ".hidden", "milk", 4, true) == ZXM_E_ARG &&
              margins_save(&MG, "trailing.", "milk", 4, true) == ZXM_E_ARG &&
              margins_save(&MG, "trailing ", "milk", 4, true) == ZXM_E_ARG &&
              margins_save(&MG, "A\xc3", "milk", 4, true) == ZXM_E_ARG &&
              margins_save(&MG, "Big", big, MARGINS_BODY_MAX + 1u, true) == ZXM_E_ARG &&
              margins_save(&MG, "Many", many, 17u * 6u, true) == ZXM_E_ARG &&
              margins_save(&MG, "Cr", "a\rb", 3, true) == ZXM_E_ARG &&
              margins_save(&MG, "Nul", "a\0b", 3, true) == ZXM_E_ARG &&
              margins_save(&MG, "Null", NULL, 0, true) == ZXM_E_ARG &&
              margins_pin(&MG, "Shopping", 2) == ZXM_E_ARG &&
              margins_trash(&MG, "Shopping", -1) == ZXM_E_ARG &&
              margins_search(&MG, "   ") == ZXM_E_ARG && margins_tagged(&MG, "a b") == ZXM_E_ARG &&
              margins_open(&MG, "Shopping") == ZXM_E_STATE && sent == before,
          "margins: not loaded, or a note past its limits: nothing sent");
    CHECK(margins_load(&MG) == ZXM_OK && MG.loaded && MG.n == 0 && MG.ntrash == 0,
          "margins: no Notes folder yet: no notes: %s", LINK.err);

    /* MM1, MM3, MM5: three notes, two sharing a tag in two spellings */
    static const char shop[] = "Milk and #groceries\nBread #Home";
    CHECK(margins_save(&MG, "Shopping", shop, sizeof shop - 1u, true) == ZXM_OK && MG.n == 1 &&
              MG.open == 0 && MG.len == sizeof shop - 1u && !strcmp(MG.text, shop),
          "margins: the first note: %s", LINK.err);
    CHECK(margins_save(&MG, "Ideas", "#home renovation\t#work-plan #home", 33, true) == ZXM_OK &&
              margins_save(&MG, "Plain", "no tags here, not#this one", 26, true) == ZXM_OK &&
              MG.n == 3 && MG.ntags == 3,
          "margins: two more: %s (%u tags)", LINK.err, MG.ntags);
    CHECK(mg_tag_notes(&MG, "home") == 2 && mg_tag_notes(&MG, "groceries") == 1 &&
              mg_tag_notes(&MG, "work-plan") == 1 && mg_tag_notes(&MG, "this") == -1,
          "margins: the tag index, each note counted once a tag (MR5)");
    static const char *const both[2] = {"Ideas", "Shopping"};
    CHECK(margins_tagged(&MG, "#HOME") == ZXM_OK && MG.nhits == 2 &&
              (mg_hits(&MG, both, 2) || (!strcmp(MG.note[MG.hit[0]].title, "Shopping") &&
                                         !strcmp(MG.note[MG.hit[1]].title, "Ideas"))) &&
              margins_tagged(&MG, "groceries") == ZXM_OK && MG.nhits == 1 &&
              margins_tagged(&MG, "nothing") == ZXM_OK && MG.nhits == 0,
          "margins: the notes of a tag");
    CHECK(margins_save(&MG, "Ideas", "again", 5, true) == ZXM_E_STATE &&
              margins_save(&MG, "Missing", "edit", 4, false) == ZXM_E_ARG,
          "margins: a new note never over another; an edit only of one that exists");

    /* MM5: an edit replaces the note whole */
    CHECK(margins_save(&MG, "Shopping", "Eggs #work-plan", 15, false) == ZXM_OK && MG.n == 3 &&
              mg_tag_notes(&MG, "home") == 1 && mg_tag_notes(&MG, "work-plan") == 2 &&
              mg_tag_notes(&MG, "groceries") == -1 && margins_fill(&MG) == 15 * 10000 / 8000,
          "margins: an edit, its tags read again: %s", LINK.err);
    CHECK(margins_open(&MG, "Ideas") == ZXM_OK && MG.len == 33 &&
              !strncmp(MG.text, "#home renovation", 16) && margins_fill(&MG) == 41,
          "margins: a note opened: %s", LINK.err);

    /* MM4: a pinned note first (MR2, MR3) */
    CHECK(margins_pin(&MG, "Plain", 1) == ZXM_OK && MG.npinned == 1, "margins: pin: %s", LINK.err);
    {
        int32_t p = margins_find(&MG, "Plain");
        bool rest = true;
        for (uint32_t i = 0; i < MG.n; i++)
            if ((int32_t) i != p && MG.note[i].place < 2) rest = false;
        CHECK(p >= 0 && MG.order[0] == (uint32_t) p && MG.note[p].place == 1 && rest &&
                  MG.note[p].key == MARGINS_PIN_STEP + MG.note[p].mtime,
              "margins: the pinned note first, the others after it");
    }

    /* MM2: what is not a note is passed over; a note too big is listed, not opened */
    static char huge[9001];
    memset(huge, 'h', 9000);
    CHECK(mg_write("Notes/readme.md", "not a note") && mg_write("Notes/Big.txt", huge) &&
              mg_mkdir("Elsewhere") &&
              mg_write("Elsewhere/renovation plans.txt", "renovation elsewhere") &&
              margins_load(&MG) == ZXM_OK && MG.n == 4 && MG.skipped == 1 && MG.npinned == 1,
          "margins: a listing with other files: %u notes, %u skipped: %s", MG.n, MG.skipped,
          LINK.err);
    int32_t bi = margins_find(&MG, "Big");
    CHECK(bi >= 0 && !MG.note[bi].readable && MG.note[bi].ntags == 0 &&
              margins_open(&MG, "Big") == ZXM_E_STATE && MG.notice[0],
          "margins: a note past 8000 bytes is listed, not opened");

    /* MM7: found by its words, only notes kept */
    static const char *const ideas[1] = {"Ideas"};
    CHECK(margins_search(&MG, "renovation") == ZXM_OK && mg_hits(&MG, ideas, 1),
          "margins: search keeps the notes: %u hits %s", MG.nhits, LINK.err);

    /* MM5: a rename keeps the pin */
    CHECK(margins_rename(&MG, "Plain", "Ideas") == ZXM_E_STATE &&
              margins_rename(&MG, "Nope", "Other") == ZXM_E_ARG &&
              margins_rename(&MG, "Plain", "Plain note") == ZXM_OK &&
              margins_find(&MG, "Plain") < 0 && margins_find(&MG, "Plain note") >= 0 &&
              MG.note[margins_find(&MG, "Plain note")].pinned == 1 && MG.npinned == 1,
          "margins: rename, pinned still: %s", LINK.err);

    /* MM6: the Trash, put back and removed only on the owner's word */
    CHECK(margins_trash(&MG, "Plain note", NOW) == ZXM_OK && MG.n == 3 && MG.ntrash == 1 &&
              MG.npinned == 0 && !strcmp(MG.trash[0].title, "Plain note") &&
              !strncmp(MG.trash[0].trashed, ".Trash/", 7) &&
              margins_trash_left(&MG, 0, NOW) == 30 &&
              margins_trash_left(&MG, 0, NOW + 30 * 86400) == 0 &&
              margins_trash_left(&MG, 1, NOW) == MARGINS_REFUSED &&
              margins_trash_due(&MG, NOW) == 0 && margins_trash_due(&MG, NOW + 30 * 86400) == 1,
          "margins: into the Trash, 30 days kept: %s", LINK.err);
    CHECK(margins_restore(&MG, 0, false) == ZXM_E_STATE && MG.ntrash == 1 &&
              margins_restore(&MG, 1, true) == ZXM_E_ARG &&
              margins_restore(&MG, 0, true) == ZXM_OK && MG.ntrash == 0 && MG.n == 4 &&
              margins_find(&MG, "Plain note") >= 0,
          "margins: put back on the owner's word: %s", LINK.err);
    CHECK(margins_trash(&MG, "Ideas", NOW + 5) == ZXM_OK && MG.ntrash == 1 && MG.n == 3 &&
              margins_forget(&MG, 0, false) == ZXM_E_STATE && MG.ntrash == 1 &&
              margins_forget(&MG, 0, true) == ZXM_OK && MG.ntrash == 0,
          "margins: removed for good on the owner's word: %s", LINK.err);
    zxm_begin(&LINK, "files.list");
    zxm_p_str(&LINK, "path", ".Trash");
    CHECK(zxm_call(&LINK) == ZXM_OK && !strstr(LINK.resp, "Ideas.txt") &&
              strstr(LINK.resp, "Shopping") == NULL,
          "margins: the Trash holds it no more");

    /* MM1, MM2: a second model reads what the first kept */
    CHECK(margins_pin(&MG, "Shopping", 1) == ZXM_OK && margins_trash(&MG, "Big", NOW) == ZXM_OK,
          "margins: a pin and a trashed note to read back: %s", LINK.err);
    margins_init(&MG2, &LINK);
    CHECK(margins_load(&MG2) == ZXM_OK && MG2.n == MG.n && MG2.npinned == 1 &&
              MG2.note[margins_find(&MG2, "Shopping")].pinned == 1 && MG2.ntrash == 1 &&
              !strcmp(MG2.trash[0].title, "Big") && MG2.trash[0].stamp == NOW &&
              MG2.ntags == MG.ntags && MG2.open == -1 && margins_fill(&MG2) == MARGINS_REFUSED,
          "margins: loaded again by a second model: %s", LINK.err);

    /* MM2: a load that fails keeps the notes before it */
    uint32_t had = MG2.n;
    CHECK(mg_write("Margins/pins.txt", "bad/title\n") && margins_load(&MG2) == ZXM_E_PROTOCOL &&
              MG2.n == had && MG2.loaded,
          "margins: a broken pin file: refused, nothing lost");
    CHECK(mg_write("Margins/pins.txt", "Shopping\n") && margins_load(&MG2) == ZXM_OK &&
              MG2.npinned == 1,
          "margins: mended, it loads");
}
