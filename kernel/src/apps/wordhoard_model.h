/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* wordhoard_model.h — Wordhoard, the dictionary and thesaurus, as a model
 * (zxm.h): word lists kept in the Hoard, looked up by prefix, by the word
 * itself with its synonyms, or by a "did you mean" when it is not there;
 * the language of a word from zxvd; a definition from the swarm held until
 * the owner accepts it. No drawing, no allocation, integers only. The rules
 * it runs are wordhoard_rules.h (WH1-WH8).
 *
 * zxvd methods used: files.list, files.read, search.query, xlate.detect,
 * xlate.languages and swarm.ask.
 *
 *   WM1 WHERE THE WORDS ARE. The owner's own lists are
 *       Wordhoard/lists/<name>.tsv. A data pack (docs/DATAPACKS.md) is
 *       installed as Wordhoard/core (wordhoard-core.tar) or
 *       Wordhoard/optional (wordhoard-optional.tar): its PACK.tsv, its
 *       ATTRIBUTION.txt and its word list words.tsv. A missing folder or
 *       file is an empty one.
 *   WM2 A WORD LIST is UTF-8 text, one headword a line, tab-separated:
 *       the headword (1..WORDHOARD_WORD_BYTES bytes, no control byte), its
 *       language (a BCP 47 tag of 1..WORDHOARD_TAG_MAX letters, digits and
 *       '-', or empty for not known), its senses (synset numbers, WH4,
 *       comma-separated, 1..WORDHOARD_ENTRY_SENSES of them) and its gloss
 *       (0..WORDHOARD_GLOSS_BYTES bytes, cut on a character). A line
 *       starting '#' is a comment; any other line that does not hold this,
 *       or whose record does not fit WH6, is passed over and counted
 *       (skipped). At most WORDHOARD_WORDS headwords are kept (dropped
 *       counts the rest).
 *   WM3 A PACK is read only when its PACK.tsv says app wordhoard, slot 46,
 *       its own pack and the state that pack must hold (core TRUE,
 *       optional BOTH, the owner's licence rule in the kernel's four
 *       values); a pack that says anything else is not read (FALSE and
 *       NEITHER are never used). Its ATTRIBUTION.txt is kept to be shown
 *       whenever its words are. An entry of the optional pack keeps list 2
 *       so the view marks it share-alike.
 *   WM4 THE INDEX. order[] lists the headwords by name code (KR2), then by
 *       their bytes, then by list: the walk of ^ZXWORDHOARD (WH6).
 *       wordhoard_prefix walks it with kindred.name_prefix_hit and holds
 *       the bytes to the typed prefix (WH7).
 *   WM5 LOOKING A WORD UP. wordhoard_lookup asks xlate.detect for the
 *       word's language (advice: a refusal leaves it "und"), keeps the
 *       headwords equal to it (ASCII case folded), and then either their
 *       synonyms, placed by WH5, or, when none is equal, the WH3 offers.
 *   WM6 THE LANGUAGES. wordhoard_languages reads xlate.languages; a
 *       headword's lang (WH6) is its tag's place in that list, 0 when the
 *       list is not read or does not hold it.
 *   WM7 FINDING LISTS. wordhoard_find_lists asks search.query for a word
 *       and keeps the hits that are word lists under Wordhoard/, in zxvd's
 *       rank.
 *   WM8 A DEFINITION. wordhoard_define asks the swarm (swarm.ask) for a
 *       short definition of a word; the answer is held (zxm.h M3) and
 *       shown as the swarm's, until wordhoard_accept is given the owner's
 *       session and approval and warren.approval_ok (WH8) says yes;
 *       wordhoard_reject drops it. */
#ifndef WORDHOARD_MODEL_H
#define WORDHOARD_MODEL_H

#include "wordhoard_rules.h"
#include "zxm.h"

#define WORDHOARD_DIR          "Wordhoard"
#define WORDHOARD_LISTS        "Wordhoard/lists"
#define WORDHOARD_CORE         "Wordhoard/core"
#define WORDHOARD_OPTIONAL     "Wordhoard/optional"
#define WORDHOARD_WORDS        256u /* headwords kept from one load */
#define WORDHOARD_WORD_BYTES   64u
#define WORDHOARD_TAG_MAX      16u /* bytes of a BCP 47 tag kept */
#define WORDHOARD_ENTRY_SENSES 8u
#define WORDHOARD_GLOSS_BYTES  200u
#define WORDHOARD_LIST_FILES   8u /* the owner's lists read */
#define WORDHOARD_HITS         32u
#define WORDHOARD_SYNONYMS     16u
#define WORDHOARD_FOUND        20u
#define WORDHOARD_LANGS        96u
#define WORDHOARD_ATTR_MAX     1024u
#define WORDHOARD_PATH_MAX     320u
#define WORDHOARD_NOTICE       200u

enum { WORDHOARD_OWN = 0, WORDHOARD_PACK_CORE = 1, WORDHOARD_PACK_OPTIONAL = 2 };

typedef struct {
    char word[WORDHOARD_WORD_BYTES + 1];
    uint32_t cp[WORDHOARD_WORD_CPS]; /* its code points (WH1), when it has at most 32 */
    uint32_t ncp;
    bool comparable; /* at most WORDHOARD_WORD_CPS code points (WH2) */
    int64_t code;    /* KR2 */
    char lang[WORDHOARD_TAG_MAX + 1];
    int64_t langno; /* WM6 */
    int64_t sense[WORDHOARD_ENTRY_SENSES];
    uint32_t nsense;
    char gloss[WORDHOARD_GLOSS_BYTES + 1];
    int64_t list;  /* WORDHOARD_OWN, _PACK_CORE or _PACK_OPTIONAL */
    int64_t stamp; /* its list's mtime, seconds since 1970 */
} wordhoard_entry_t;

typedef struct {
    zxm_link_t *link;
    bool loaded;
    bool pack[3]; /* [1] core, [2] optional: installed and sound (WM3) */
    char attribution[3][WORDHOARD_ATTR_MAX + 1];
    wordhoard_entry_t entry[WORDHOARD_WORDS];
    uint32_t n;
    uint32_t skipped, dropped;       /* WM2 */
    uint32_t order[WORDHOARD_WORDS]; /* WM4 */
    char langs[WORDHOARD_LANGS][WORDHOARD_TAG_MAX + 1];
    uint32_t nlangs;
    /* the last lookup (WM4, WM5) */
    char typed[WORDHOARD_WORD_BYTES + 1];
    char typed_lang[WORDHOARD_TAG_MAX + 1];
    uint32_t hit[WORDHOARD_HITS];
    uint32_t nhits;
    uint32_t suggest[WORDHOARD_SUGGEST];
    int64_t suggest_distance[WORDHOARD_SUGGEST];
    uint32_t nsuggest;
    uint32_t syn[WORDHOARD_SYNONYMS];
    int64_t syn_shared[WORDHOARD_SYNONYMS];
    int64_t syn_place[WORDHOARD_SYNONYMS];
    uint32_t nsyn;
    /* WM7 */
    char found[WORDHOARD_FOUND][WORDHOARD_PATH_MAX];
    uint32_t nfound;
    /* WM8 */
    zxm_held_t held;
    char held_word[WORDHOARD_WORD_BYTES + 1];
    char notice[WORDHOARD_NOTICE];
} wordhoard_t;

enum { WORDHOARD_AI_DEFINE, WORDHOARD_AI_COUNT };
extern const zxm_ai_action_t WORDHOARD_AI[WORDHOARD_AI_COUNT];

void wordhoard_init(wordhoard_t *w, zxm_link_t *link);
/* WM1-WM4 */
int wordhoard_load(wordhoard_t *w);
/* WM6 */
int wordhoard_languages(wordhoard_t *w);
/* WM4: headwords that begin with typed */
int wordhoard_prefix(wordhoard_t *w, const char *typed);
/* WM5 */
int wordhoard_lookup(wordhoard_t *w, const char *word);
/* WM7 */
int wordhoard_find_lists(wordhoard_t *w, const char *word);
/* WM8 */
int wordhoard_define(wordhoard_t *w, const char *word);
int wordhoard_accept(wordhoard_t *w, int64_t owner, int64_t approved);
int wordhoard_reject(wordhoard_t *w);
/* The code points of n bytes of UTF-8 into cp (up to cap); their count, or
 * WORDHOARD_REFUSED when the bytes are not UTF-8 or hold more than cap. */
int64_t wordhoard_code_points(const char *s, uint32_t n, uint32_t *cp, uint32_t cap);

#endif
