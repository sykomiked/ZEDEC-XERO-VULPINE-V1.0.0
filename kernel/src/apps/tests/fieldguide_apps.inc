/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Fieldguide's model against zxvd's real dispatcher (LINK): small articles
 * and identification keys written here with files.write into the test's
 * scratch Hoard (made up for the test, no pack data), found with
 * search.refresh and search.query, read with files.read, ranked, cautioned
 * and walked one question at a time. */
#include "fieldguide_model.h"

static bool fgd_write(const char *path, const char *text)
{
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", path);
    zxm_p_str(&LINK, "text", text);
    return zxm_call(&LINK) == ZXM_OK;
}

static bool fgd_mkdir(const char *path)
{
    zxm_begin(&LINK, "files.mkdir");
    zxm_p_str(&LINK, "path", path);
    int rc = zxm_call(&LINK);
    return rc == ZXM_OK || (rc == ZXM_E_REMOTE && strstr(LINK.err, "already exists"));
}

/* the match whose taxon is `taxon`, or -1 */
static int32_t fgd_match(const fieldguide_t *g, int64_t taxon)
{
    for (uint32_t i = 0; i < g->n; i++)
        if (g->match[i].taxon == taxon) return (int32_t) i;
    return -1;
}

/* the notice says nothing is safe: "safe" only inside FIELDGUIDE_NEVER_SAFE */
static bool fgd_never_safe(const char *notice)
{
    const char *tail = strstr(notice, FIELDGUIDE_NEVER_SAFE);
    if (!tail || strlen(tail) != strlen(FIELDGUIDE_NEVER_SAFE)) return false;
    const char *s = strstr(notice, "safe");
    return s && s > tail;
}

APPS_TEST(fieldguide)
{
    static fieldguide_t G;
    static char longq[FIELDGUIDE_QUERY_MAX + 2];
    memset(longq, 'q', FIELDGUIDE_QUERY_MAX + 1);

    /* FM1, FM2: an article's head */
    {
        fieldguide_article_t a;
        static const char h1[] = "Name: Amanita phalloides\ncommon: Death cap\ntaxon: 5240046\n"
                                 "kind: Fungus\ntoxic: yes\nedible: yes\n\ntoxic: no\n";
        static const char h2[] = "name: Agaricus campestris\nkind: fungus\ntoxic: no\n"
                                 "dangerous: no\n";
        static const char h3[] = "name: Urtica dioica\nkind: plant\ndangerous: yes\ntoxic: no\n";
        static const char h4[] = "name: Macrolepiota procera\nkind: fungus\ntoxic: maybe\n"
                                 "taxon: 1000000000\n";
        static const char h5[] = "Just some words about a walk in the woods.\n";
        CHECK(fieldguide_parse_head(h1, sizeof h1 - 1u, &a) &&
                  !strcmp(a.name, "Amanita phalloides") && !strcmp(a.common, "Death cap") &&
                  a.taxon == 5240046 && a.kind == FIELDGUIDE_KIND_FUNGUS &&
                  a.mark == FIELDGUIDE_MARK_TOXIC,
              "fieldguide: a head, case folded, ending at the first empty line");
        CHECK(fieldguide_parse_head(h2, sizeof h2 - 1u, &a) && a.mark == FIELDGUIDE_MARK_NONE &&
                  a.taxon == 0 && fieldguide_parse_head(h3, sizeof h3 - 1u, &a) &&
                  a.kind == FIELDGUIDE_KIND_PLANT && a.mark == FIELDGUIDE_MARK_TOXIC &&
                  fieldguide_parse_head(h4, sizeof h4 - 1u, &a) &&
                  a.mark == FIELDGUIDE_MARK_UNKNOWN && a.taxon == 0 &&
                  !fieldguide_parse_head(h5, sizeof h5 - 1u, &a) &&
                  a.mark == FIELDGUIDE_MARK_UNKNOWN && !fieldguide_parse_head(NULL, 0, &a),
              "fieldguide: marks (FM2): no, dangerous over toxic, maybe is unknown; no header");
    }

    /* FM5: every notice ends with the warning and says nothing is safe */
    {
        static char out[FIELDGUIDE_NOTICE];
        bool ok = true;
        for (int64_t lv = 0; lv <= 2; lv++)
            for (int s = 0; s < 4; s++) {
                fieldguide_caution_notice(lv, (lpres_state_t) s, out, sizeof out);
                ok = ok && fgd_never_safe(out);
            }
        fieldguide_caution_notice(FIELDGUIDE_CAUTION_UNKNOWN, LPRES_STATE_NEITHER, out, sizeof out);
        CHECK(ok && strstr(out, "Toxicity unknown") != NULL,
              "fieldguide: every caution ends with the warning, none says safe");
    }

    /* nothing searched or loaded: refused, and nothing sent */
    fieldguide_init(&G, &LINK);
    int before = sent;
    CHECK(fieldguide_search(&G, "") == ZXM_E_ARG && fieldguide_search(&G, "   ") == ZXM_E_ARG &&
              fieldguide_search(&G, longq) == ZXM_E_ARG &&
              fieldguide_search(&G, "a\nb") == ZXM_E_ARG &&
              fieldguide_search(&G, "bad\xc3") == ZXM_E_ARG &&
              fieldguide_search(&G, NULL) == ZXM_E_ARG &&
              fieldguide_key_load(&G, "Notes/caps.key") == ZXM_E_ARG &&
              fieldguide_key_load(&G, FIELDGUIDE_KEYS "/caps.txt") == ZXM_E_ARG &&
              fieldguide_key_load(&G, NULL) == ZXM_E_ARG &&
              fieldguide_key_answer(&G, 1) == ZXM_E_STATE &&
              fieldguide_key_back(&G) == ZXM_E_STATE && fieldguide_key_question(&G) == NULL &&
              G.caution_level == FIELDGUIDE_CAUTION_UNKNOWN && sent == before,
          "fieldguide: refused before anything is sent");

    /* FM1, FM3: articles in the Hoard, written as fixtures */
    CHECK(fgd_mkdir(FIELDGUIDE_DIR) && fgd_mkdir(FIELDGUIDE_KEYS) && fgd_mkdir("FgNotes") &&
              fgd_write(FIELDGUIDE_DIR "/deathcap.txt",
                        "name: Amanita phalloides\ncommon: Death cap\ntaxon: 5240046\n"
                        "kind: fungus\ntoxic: yes\n\nA pale toadstool of oak woods; its cap is "
                        "olive and its gills white.\n") &&
              fgd_write(FIELDGUIDE_DIR "/fieldmushroom.txt",
                        "name: Agaricus campestris\ncommon: Field mushroom\ntaxon: 2601111\n"
                        "kind: fungus\ntoxic: no\n\nA toadstool of meadows, toadstool gills pink "
                        "then brown, pink toadstool pink.\n") &&
              fgd_write(FIELDGUIDE_DIR "/parasol.txt",
                        "name: Macrolepiota procera\ncommon: Parasol\ntaxon: 2602222\n"
                        "kind: fungus\n\nA tall toadstool with a scaly cap.\n") &&
              fgd_write(FIELDGUIDE_DIR "/walk.txt",
                        "A toadstool seen on a walk, no header here.\n") &&
              fgd_write("FgNotes/toadstool.txt", "name: Not an article\ntoxic: no\n\ntoadstool\n"),
          "fieldguide: fixtures: %s", LINK.err);

    /* FM3, FM5: three matches, one marked toxic, one not, one silent: caution, held BOTH */
    CHECK(fieldguide_search(&G, "toadstool") == ZXM_OK && G.n == 3 && fgd_match(&G, 5240046) >= 0 &&
              fgd_match(&G, 2601111) >= 0 && fgd_match(&G, 2602222) >= 0 &&
              G.caution_level == FIELDGUIDE_CAUTION_MARKED && G.caution == LPRES_STATE_BOTH &&
              strstr(G.notice, "treat it as toxic") != NULL && fgd_never_safe(G.notice),
          "fieldguide: toadstool: %u matches, caution %lld: %s / %s", G.n,
          (long long) G.caution_level, G.notice, LINK.err);
    /* FM4: best first by the search's score, ranks from 1 */
    {
        bool ordered = G.n == 3 && G.match[0].rank == 1;
        for (uint32_t i = 1; i < G.n; i++)
            ordered = ordered && G.match[i].score <= G.match[i - 1u].score &&
                      G.match[i].rank >= G.match[i - 1u].rank && G.match[i].key == G.match[i].score;
        CHECK(ordered && G.match[0].taxon == 2601111,
              "fieldguide: ranked by the search's score, the most mentions first (%lld %lld %lld)",
              (long long) G.match[0].score, (long long) G.match[1].score,
              (long long) G.match[2].score);
    }
    /* none marked: says so, and still never that it is safe */
    CHECK(fieldguide_search(&G, "meadows") == ZXM_OK && G.n == 1 &&
              G.caution_level == FIELDGUIDE_CAUTION_NONE && G.caution == LPRES_STATE_FALSE &&
              strstr(G.notice, "does not make it harmless") != NULL && fgd_never_safe(G.notice),
          "fieldguide: meadows: none marked: %s", G.notice);
    /* no toxicity information at all: unknown, never safe */
    CHECK(fieldguide_search(&G, "scaly") == ZXM_OK && G.n == 1 &&
              G.caution_level == FIELDGUIDE_CAUTION_UNKNOWN && G.caution == LPRES_STATE_NEITHER &&
              strstr(G.notice, "Toxicity unknown") != NULL && fgd_never_safe(G.notice),
          "fieldguide: scaly: unknown: %s", G.notice);
    /* nothing found: unknown */
    CHECK(fieldguide_search(&G, "zebrawood") == ZXM_OK && G.n == 0 &&
              G.caution_level == FIELDGUIDE_CAUTION_UNKNOWN && fgd_never_safe(G.notice),
          "fieldguide: nothing found: unknown");
    /* the index brought up to date once, then searched as it is */
    CHECK(fieldguide_refresh(&G) == ZXM_OK && G.refreshed && G.indexed >= 4 &&
              fieldguide_search(&G, "olive") == ZXM_OK && G.n == 1 &&
              G.caution_level == FIELDGUIDE_CAUTION_MARKED && G.caution == LPRES_STATE_TRUE,
          "fieldguide: refresh, then search: %lld indexed: %s", (long long) G.indexed, LINK.err);

    /* FM6: an identification key, one question at a time */
    CHECK(fgd_write(FIELDGUIDE_KEYS "/caps.key",
                    "# three couplets\n"
                    "1|Is the cap olive or white, with a cup at the stem's base?|c2|c3\n"
                    "2|Are the gills white?|t5240046|-\n"
                    "3|Is the cap scaly?|t2602222|t2601111\n") &&
              fgd_write(FIELDGUIDE_KEYS "/loop.key", "1|First?|c2|-\n2|Second?|c1|-\n") &&
              fgd_write(FIELDGUIDE_KEYS "/far.key", "1|First?|c2|-\n2|Second?|c9|-\n") &&
              fgd_write(FIELDGUIDE_KEYS "/skip.key", "1|First?|c2|-\n3|Second?|-|-\n") &&
              fgd_write(FIELDGUIDE_KEYS "/three.key", "1|First?|c2\n2|Second?|-|-\n"),
          "fieldguide: keys: %s", LINK.err);
    CHECK(fieldguide_search(&G, "toadstool") == ZXM_OK && G.n == 3 && G.match[0].taxon == 2601111,
          "fieldguide: the matches before the key");
    CHECK(fieldguide_key_load(&G, FIELDGUIDE_KEYS "/missing.key") == ZXM_E_REMOTE &&
              fieldguide_key_load(&G, FIELDGUIDE_KEYS "/far.key") == ZXM_E_STATE &&
              fieldguide_key_load(&G, FIELDGUIDE_KEYS "/skip.key") == ZXM_E_STATE &&
              fieldguide_key_load(&G, FIELDGUIDE_KEYS "/three.key") == ZXM_E_STATE &&
              G.walk == FIELDGUIDE_WALK_NONE,
          "fieldguide: a missing key, a lead to no couplet, a gap, a short line: refused");
    CHECK(fieldguide_key_load(&G, FIELDGUIDE_KEYS "/caps.key") == ZXM_OK && G.ncouplets == 3 &&
              G.walk == FIELDGUIDE_WALK_ASKING && G.at == 1 && fieldguide_key_question(&G) &&
              !strncmp(fieldguide_key_question(&G), "Is the cap olive", 16),
          "fieldguide: the key loads at its first question: %s", LINK.err);
    before = sent;
    CHECK(fieldguide_key_answer(&G, 2) == ZXM_E_ARG && fieldguide_key_answer(&G, -2) == ZXM_E_ARG &&
              G.at == 1 && G.steps == 0 && G.walk == FIELDGUIDE_WALK_ASKING && sent == before,
          "fieldguide: an answer that is not yes, no or not sure: refused, nothing moves");
    CHECK(fieldguide_key_answer(&G, FIELDGUIDE_ANSWER_YES) == ZXM_OK && G.at == 2 &&
              !strcmp(fieldguide_key_question(&G), "Are the gills white?") &&
              fieldguide_key_back(&G) == ZXM_OK && G.at == 1 && G.steps == 0 &&
              fieldguide_key_answer(&G, FIELDGUIDE_ANSWER_YES) == ZXM_OK &&
              fieldguide_key_answer(&G, FIELDGUIDE_ANSWER_YES) == ZXM_OK &&
              G.walk == FIELDGUIDE_WALK_REACHED && G.reached == 5240046 &&
              fieldguide_key_question(&G) == NULL,
          "fieldguide: yes, back, yes, yes: the death cap reached");
    /* FM4: the species the key reached ranks first, the rest in the search's order */
    CHECK(G.match[0].taxon == 5240046 && G.match[0].rank == 1 &&
              G.match[0].key == FIELDGUIDE_KEYED + G.match[0].score &&
              G.match[1].taxon == 2601111 && G.match[1].rank == 2 && G.match[2].rank == 3 &&
              fieldguide_key_answer(&G, 1) == ZXM_E_STATE && sent == before,
          "fieldguide: the keyed species first, nothing sent to rank it");
    CHECK(fieldguide_key_back(&G) == ZXM_OK && G.walk == FIELDGUIDE_WALK_ASKING && G.at == 2 &&
              G.reached == 0 && G.match[0].taxon == 2601111 &&
              fieldguide_key_answer(&G, FIELDGUIDE_ANSWER_UNSURE) == ZXM_OK &&
              G.walk == FIELDGUIDE_WALK_STOPPED && G.reached == 0 &&
              fieldguide_key_back(&G) == ZXM_OK &&
              fieldguide_key_answer(&G, FIELDGUIDE_ANSWER_NO) == ZXM_OK &&
              G.walk == FIELDGUIDE_WALK_STOPPED && G.reached == 0,
          "fieldguide: back takes the species away; not sure and a dead end stop, no guess");
    CHECK(fieldguide_key_load(&G, FIELDGUIDE_KEYS "/caps.key") == ZXM_OK &&
              fieldguide_key_answer(&G, FIELDGUIDE_ANSWER_NO) == ZXM_OK && G.at == 3 &&
              fieldguide_key_answer(&G, FIELDGUIDE_ANSWER_NO) == ZXM_OK && G.reached == 2601111 &&
              G.match[0].taxon == 2601111,
          "fieldguide: no, no: the field mushroom");
    CHECK(fieldguide_key_load(&G, FIELDGUIDE_KEYS "/loop.key") == ZXM_OK &&
              fieldguide_key_answer(&G, FIELDGUIDE_ANSWER_YES) == ZXM_OK && G.at == 2 &&
              fieldguide_key_answer(&G, FIELDGUIDE_ANSWER_YES) == ZXM_E_STATE && G.at == 2 &&
              G.steps == 1 && G.walk == FIELDGUIDE_WALK_ASKING,
          "fieldguide: a key that loops back: refused, the walk where it was");
}
