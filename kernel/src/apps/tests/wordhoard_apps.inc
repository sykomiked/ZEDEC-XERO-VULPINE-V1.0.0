/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Wordhoard's model against zxvd's real dispatcher (LINK): a core pack, an
 * optional pack and the owner's own lists written with files.write into
 * the test's scratch Hoard as fixtures (made up here, no pack data), then
 * loaded, looked up by prefix and by word, searched with search.query, and
 * a definition asked of the swarm stub and held for the owner.
 * zxvd_people.c, which serves xlate.* in the daemon, is not linked into
 * this test, so xlate.detect and xlate.languages are answered here by a
 * small host in their own shapes (api/zxvd.openrpc.json), set as zxvd's
 * people ops as test_zxvd_rpc.c does; the requests still go through
 * zxvd_rpc_handle_line. */
#include "wordhoard_model.h"

static uint32_t WH_DETECTS;

static int wh_people(const char *method, const char *params, size_t len, zxvd_buf_t *out, char *msg,
                     size_t msg_cap)
{
    char ps[1024];
    if (len >= sizeof ps) len = sizeof ps - 1u;
    memcpy(ps, params, len);
    ps[len] = 0;
    if (!strcmp(method, "xlate.languages")) {
        zxvd_buf_printf(out, "{\"count\":3,\"languages\":[{\"tag\":\"en\",\"name\":\"English\"},"
                             "{\"tag\":\"de\",\"name\":\"German\"},"
                             "{\"tag\":\"fr\",\"name\":\"French\"}]}");
        return 0;
    }
    if (!strcmp(method, "xlate.detect")) {
        WH_DETECTS++;
        const char *tag = strstr(ps, "\xc3\xb6") || strstr(ps, "buch") ? "de" : "en";
        zxvd_buf_printf(out,
                        "{\"language\":\"%s\",\"name\":\"%s\",\"confidence\":80,\"letters\":4,"
                        "\"reliable\":true}",
                        tag, !strcmp(tag, "de") ? "German" : "English");
        return 0;
    }
    snprintf(msg, msg_cap, "not here");
    return -32601;
}

static bool wh_mkdir(const char *path)
{
    zxm_begin(&LINK, "files.mkdir");
    zxm_p_str(&LINK, "path", path);
    int rc = zxm_call(&LINK);
    return rc == ZXM_OK || (rc == ZXM_E_REMOTE && strstr(LINK.err, "already exists"));
}

/* a file written through zxvd as another app would, in parts of at most
 * 8000 bytes (files.write's begin, add and end) so a long one fits zxvd's
 * request line */
static bool wh_write(const char *path, const char *text)
{
    size_t n = strlen(text);
    if (n <= 8000u) {
        zxm_begin(&LINK, "files.write");
        zxm_p_str(&LINK, "path", path);
        zxm_p_str(&LINK, "text", text);
        return zxm_call(&LINK) == ZXM_OK;
    }
    for (size_t at = 0; at < n; at += 8000u) {
        size_t part = n - at < 8000u ? n - at : 8000u;
        zxm_begin(&LINK, "files.write");
        zxm_p_str(&LINK, "path", path);
        zxm_p_strn(&LINK, "text", text + at, (uint32_t) part);
        zxm_p_str(&LINK, "mode", at == 0 ? "begin" : at + part >= n ? "end" : "add");
        if (zxm_call(&LINK) != ZXM_OK) return false;
    }
    return true;
}

/* the words of the last hits, in order, joined by spaces */
static bool wh_hits(const wordhoard_t *w, const char *want)
{
    char got[512];
    uint32_t at = 0;
    got[0] = 0;
    for (uint32_t i = 0; i < w->nhits; i++)
        at += (uint32_t) snprintf(got + at, sizeof got - at, "%s%s", i ? " " : "",
                                  w->entry[w->hit[i]].word);
    return !strcmp(got, want);
}

static int32_t wh_find(const wordhoard_t *w, const char *word)
{
    for (uint32_t i = 0; i < w->n; i++)
        if (!strcmp(w->entry[i].word, word)) return (int32_t) i;
    return -1;
}

static wordhoard_t WH, WH2;

APPS_TEST(wordhoard)
{
    static const zxvd_people_ops_t people = {wh_people};
    static char big[24000];

    /* the rules as the model puts them together (WH1-WH8) */
    {
        uint32_t cp[4];
        CHECK(wordhoard_code_points("W\xc3\xb6rt", 5, cp, 4) == 4 && cp[1] == 0xF6 &&
                  wordhoard_code_points("\xed\xa0\x80", 3, cp, 4) == WORDHOARD_REFUSED &&
                  wordhoard_code_points("\xc3", 1, cp, 4) == WORDHOARD_REFUSED &&
                  wordhoard_code_points("abcde", 5, cp, 4) == WORDHOARD_REFUSED &&
                  wordhoard_suggest_limit(4) == 1 && warren_approval_ok(1, 1) &&
                  !warren_approval_ok(1, 2),
              "wordhoard: the rules");
    }

    /* nothing before the load, and a bad word never sent */
    wordhoard_init(&WH, &LINK);
    int before = sent;
    CHECK(
        wordhoard_lookup(&WH, "dog") == ZXM_E_STATE && wordhoard_prefix(&WH, "do") == ZXM_E_STATE &&
            wordhoard_lookup(&WH, "") == ZXM_E_ARG && wordhoard_lookup(&WH, "a\tb") == ZXM_E_ARG &&
            wordhoard_lookup(&WH, "\xc3") == ZXM_E_ARG &&
            wordhoard_prefix(&WH, NULL) == ZXM_E_ARG &&
            wordhoard_define(&WH, "a\nb") == ZXM_E_ARG &&
            wordhoard_find_lists(&WH, "") == ZXM_E_ARG &&
            wordhoard_accept(&WH, 1, 1) == ZXM_E_STATE && wordhoard_reject(&WH) == ZXM_E_STATE &&
            sent == before,
        "wordhoard: not loaded, or a bad word: nothing sent");
    CHECK(wordhoard_load(&WH) == ZXM_OK && WH.loaded && WH.n == 0 && !WH.pack[1] && !WH.pack[2],
          "wordhoard: nothing installed: no words: %s", LINK.err);

    /* the languages (WM6): zxvd answers through the people ops */
    zxvd_rpc_set_people_ops(NULL);
    CHECK(wordhoard_languages(&WH) == ZXM_E_REMOTE && WH.nlangs == 0,
          "wordhoard: xlate.languages not served: refused");
    zxvd_rpc_set_people_ops(&people);
    CHECK(wordhoard_languages(&WH) == ZXM_OK && WH.nlangs == 3 && !strcmp(WH.langs[1], "de"),
          "wordhoard: xlate.languages: %s", LINK.err);

    /* WM1-WM3: two packs and the owner's list, as fixtures */
    CHECK(wh_mkdir(WORDHOARD_DIR) && wh_mkdir(WORDHOARD_CORE) && wh_mkdir(WORDHOARD_OPTIONAL) &&
              wh_mkdir(WORDHOARD_LISTS),
          "wordhoard: folders: %s", LINK.err);
    CHECK(wh_write(WORDHOARD_CORE "/PACK.tsv",
                   "app\twordhoard\nslot\t46\npack\tcore\nstate\tTRUE\n") &&
              wh_write(WORDHOARD_CORE "/ATTRIBUTION.txt", "Test fixture core pack, CC BY 4.0\n") &&
              wh_write(WORDHOARD_CORE "/words.tsv",
                       "# a fixture: headword, language, senses, gloss\n"
                       "dog\ten\t20840710,101234561\ta tame animal kept by people\n"
                       "hound\ten\t20840710\ta dog bred for the chase\n"
                       "canine\ten\t101234561,20840710,20840710\tof a dog\n"
                       "cat\ten\t21211460\ta small tame animal\n"
                       "dogma\ten\t60000011\ta belief held fast\n"
                       "domestic\ten\t\tno senses: passed over\n"
                       "no tabs at all\n"
                       "dot\ten\t0\ta sense of 0: passed over\n"
                       "dash\txx yy\t70000014\ta bad tag: passed over\r\n"
                       "kitten\t\t21211461\ta young cat"),
          "wordhoard: the core pack: %s", LINK.err);
    CHECK(wh_write(WORDHOARD_OPTIONAL "/PACK.tsv",
                   "app\twordhoard\nslot\t46\npack\toptional\nstate\tBOTH\n") &&
              wh_write(WORDHOARD_OPTIONAL "/ATTRIBUTION.txt",
                       "Test fixture optional pack, CC BY-SA 4.0\n") &&
              wh_write(WORDHOARD_OPTIONAL "/words.tsv", "doggo\ten\t20840710\ta dog, fondly\n"),
          "wordhoard: the optional pack: %s", LINK.err);
    CHECK(wh_write(WORDHOARD_LISTS "/mine.tsv",
                   "W\xc3\xb6rterbuch\tde\t60000041\tein Buch der W\xc3\xb6rter\n"
                   "caf\xc3\xa9\tfr\t80000011\tun petit restaurant\n") &&
              wh_write(WORDHOARD_LISTS "/notes.txt", "not a list\n"),
          "wordhoard: the owner's list: %s", LINK.err);
    CHECK(wordhoard_load(&WH) == ZXM_OK && WH.pack[1] && WH.pack[2] && WH.n == 9 &&
              WH.skipped == 4 && WH.dropped == 0 &&
              !strcmp(WH.attribution[1], "Test fixture core pack, CC BY 4.0\n") &&
              strstr(WH.attribution[2], "CC BY-SA") != NULL,
          "wordhoard: loaded: %s (%u words, %u passed over)", LINK.err, WH.n, WH.skipped);
    {
        int32_t dog = wh_find(&WH, "dog"), can = wh_find(&WH, "canine"),
                wb = wh_find(&WH, "W\xc3\xb6rterbuch"), kit = wh_find(&WH, "kitten"),
                go = wh_find(&WH, "doggo");
        CHECK(dog >= 0 && can >= 0 && wb >= 0 && kit >= 0 && go >= 0 && WH.entry[dog].langno == 1 &&
                  WH.entry[wb].langno == 2 && WH.entry[dog].list == WORDHOARD_PACK_CORE &&
                  WH.entry[go].list == WORDHOARD_PACK_OPTIONAL &&
                  WH.entry[wb].list == WORDHOARD_OWN && WH.entry[can].nsense == 2 &&
                  !strcmp(WH.entry[kit].lang, "und") && WH.entry[kit].langno == 0 &&
                  WH.entry[wb].ncp == 10 && WH.entry[wb].comparable &&
                  WH.entry[dog].code == 41507000000000000,
              "wordhoard: entries: languages, lists, a repeat dropped, code points (WM2, WM6)");
        bool sorted = true;
        for (uint32_t k = 1; k < WH.n; k++)
            sorted = sorted && WH.entry[WH.order[k - 1u]].code <= WH.entry[WH.order[k]].code;
        CHECK(sorted, "wordhoard: the index in name-code order (WM4)");
    }

    /* WM4: prefixes */
    CHECK(wordhoard_prefix(&WH, "do") == ZXM_OK && wh_hits(&WH, "dog doggo dogma") &&
              wordhoard_prefix(&WH, "DOG") == ZXM_OK && wh_hits(&WH, "dog doggo dogma") &&
              wordhoard_prefix(&WH, "dogm") == ZXM_OK && wh_hits(&WH, "dogma") &&
              wordhoard_prefix(&WH, "caf\xc3\xa9") == ZXM_OK && wh_hits(&WH, "caf\xc3\xa9") &&
              wordhoard_prefix(&WH, "cafe") == ZXM_OK && WH.nhits == 0 &&
              wordhoard_prefix(&WH, "z") == ZXM_OK && WH.nhits == 0,
          "wordhoard: prefixes, the bytes held after the walk (WH7)");

    /* WM5: a word, its language and its synonyms by shared senses */
    uint32_t detects = WH_DETECTS;
    CHECK(wordhoard_lookup(&WH, "Dog") == ZXM_OK && wh_hits(&WH, "dog") &&
              !strcmp(WH.typed_lang, "en") && WH_DETECTS == detects + 1u && WH.nsuggest == 0 &&
              WH.nsyn == 3 && !strcmp(WH.entry[WH.syn[0]].word, "canine") &&
              WH.syn_shared[0] == 2 && WH.syn_place[0] == 1 &&
              !strcmp(WH.entry[WH.syn[1]].word, "doggo") && WH.syn_place[1] == 2 &&
              !strcmp(WH.entry[WH.syn[2]].word, "hound") && WH.syn_place[2] == 2,
          "wordhoard: dog: canine first (2 senses), then doggo and hound (1) (WH5)");
    CHECK(wordhoard_lookup(&WH, "cat") == ZXM_OK && wh_hits(&WH, "cat") && WH.nsyn == 0,
          "wordhoard: cat shares no sense");

    /* WH3: did you mean */
    CHECK(wordhoard_lookup(&WH, "hund") == ZXM_OK && WH.nhits == 0 && WH.nsuggest == 1 &&
              !strcmp(WH.entry[WH.suggest[0]].word, "hound") && WH.suggest_distance[0] == 1,
          "wordhoard: hund: did you mean hound");
    CHECK(wordhoard_lookup(&WH, "Worterbuch") == ZXM_OK && WH.nhits == 0 &&
              !strcmp(WH.typed_lang, "de") && WH.nsuggest == 1 &&
              !strcmp(WH.entry[WH.suggest[0]].word, "W\xc3\xb6rterbuch") &&
              WH.suggest_distance[0] == 1,
          "wordhoard: Worterbuch: one code point away, in German");
    CHECK(wordhoard_lookup(&WH, "dgo") == ZXM_OK && WH.nhits == 0 && WH.nsuggest == 0,
          "wordhoard: dgo: two edits from dog, past the limit of 1 for three code points");
    CHECK(wordhoard_lookup(&WH, "doggma") == ZXM_OK && WH.nsuggest == 2 &&
              !strcmp(WH.entry[WH.suggest[0]].word, "dogma") && WH.suggest_distance[0] == 1 &&
              !strcmp(WH.entry[WH.suggest[1]].word, "doggo") && WH.suggest_distance[1] == 2,
          "wordhoard: doggma: dogma (1) before doggo (2), nearest first");

    /* WM7: the lists that hold a word */
    CHECK(wordhoard_find_lists(&WH, "hound") == ZXM_OK && WH.nfound == 1 &&
              !strcmp(WH.found[0], WORDHOARD_CORE "/words.tsv") &&
              wordhoard_find_lists(&WH, "restaurant") == ZXM_OK && WH.nfound == 1 &&
              !strcmp(WH.found[0], WORDHOARD_LISTS "/mine.tsv") &&
              wordhoard_find_lists(&WH, "zebra") == ZXM_OK && WH.nfound == 0,
          "wordhoard: search.query finds the lists: %s", LINK.err);

    /* WM8: a definition held until the owner's word */
    ANSWER = "A tame animal kept by people.";
    int asked_before = asked;
    CHECK(wordhoard_define(&WH, "dog") == ZXM_OK && asked == asked_before + 1 &&
              WH.held.state == ZXM_HELD_PENDING && !strcmp(WH.held.text, ANSWER) &&
              !strcmp(WH.held_word, "dog") &&
              strstr(last_prompt, "dictionary would.\n\ndog") != NULL &&
              !strcmp(WH.held.cost.agent, "Writer"),
          "wordhoard: a definition asked and held: %s", LINK.err);
    CHECK(wordhoard_accept(&WH, 1, 0) == ZXM_E_ARG && wordhoard_accept(&WH, 0, 1) == ZXM_E_ARG &&
              wordhoard_accept(&WH, 1, 2) == ZXM_E_ARG && WH.held.state == ZXM_HELD_PENDING,
          "wordhoard: not the owner's word: still held");
    CHECK(wordhoard_accept(&WH, 1, 1) == ZXM_OK && WH.held.state == ZXM_HELD_ACCEPTED &&
              wordhoard_accept(&WH, 1, 1) == ZXM_E_STATE,
          "wordhoard: the owner accepts it");
    CHECK(wordhoard_define(&WH, "cat") == ZXM_OK && WH.held.state == ZXM_HELD_PENDING &&
              wordhoard_reject(&WH) == ZXM_OK && WH.held.state == ZXM_HELD_REJECTED &&
              !WH.held_word[0] && wordhoard_accept(&WH, 1, 1) == ZXM_E_STATE,
          "wordhoard: a definition rejected is dropped");

    /* WM2: a list past one read, a line past its bytes, and more words than are kept */
    {
        uint32_t at = 0;
        for (uint32_t i = 0; i < 200u; i++)
            at += (uint32_t) snprintf(big + at, sizeof big - at,
                                      "# %03u a comment line long enough to fill a read of the "
                                      "list well past sixteen kilobytes\n",
                                      i);
        at += (uint32_t) snprintf(big + at, sizeof big - at, "zebra\ten\t90000011\tstriped\n");
        memset(big + at, 'x', 1100u);
        at += 1100u;
        at += (uint32_t) snprintf(big + at, sizeof big - at, "\nzebu\ten\t90000021\thumped");
        CHECK(at > 16384u + 1000u && wh_write(WORDHOARD_LISTS "/big.tsv", big),
              "wordhoard: a long list written: %s (%u bytes)", LINK.err, at);
    }
    CHECK(wordhoard_load(&WH) == ZXM_OK && WH.n == 11 && WH.skipped == 5 &&
              wh_find(&WH, "zebra") >= 0 && wh_find(&WH, "zebu") >= 0,
          "wordhoard: read in parts, a line past 1024 bytes passed over: %s (%u, %u)", LINK.err,
          WH.n, WH.skipped);
    {
        uint32_t at = 0;
        for (uint32_t i = 0; i < 300u; i++)
            at += (uint32_t) snprintf(big + at, sizeof big - at, "w%03u\ten\t%u\tfiller\n", i,
                                      1000u + i);
        CHECK(wh_write(WORDHOARD_LISTS "/many.tsv", big) && wordhoard_load(&WH) == ZXM_OK &&
                  WH.n == WORDHOARD_WORDS && WH.dropped == 11u + 300u - WORDHOARD_WORDS,
              "wordhoard: %u words kept, %u dropped", WH.n, WH.dropped);
    }

    /* WM3: a pack whose PACK.tsv does not hold its licence rule is not read */
    CHECK(wh_write(WORDHOARD_CORE "/PACK.tsv",
                   "app\twordhoard\nslot\t46\npack\tcore\nstate\tFALSE\n") &&
              wh_write(WORDHOARD_OPTIONAL "/ATTRIBUTION.txt", ""),
          "wordhoard: a pack spoiled: %s", LINK.err);
    wordhoard_init(&WH2, &LINK);
    CHECK(wordhoard_load(&WH2) == ZXM_OK && !WH2.pack[1] && !WH2.pack[2] &&
              wh_find(&WH2, "hound") < 0 && wh_find(&WH2, "doggo") < 0 &&
              wh_find(&WH2, "zebra") >= 0 && !WH2.attribution[1][0] && WH2.notice[0],
          "wordhoard: a core pack saying FALSE and an optional pack with no attribution: not read");
    zxvd_rpc_set_people_ops(NULL);
}
