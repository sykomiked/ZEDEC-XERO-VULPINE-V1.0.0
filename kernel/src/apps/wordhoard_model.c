/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* wordhoard_model.c — Wordhoard's model (wordhoard_model.h). */
#include <string.h>

#include "wordhoard_model.h"

#define WORDHOARD_E_FILES  (-32014) /* zxvd's files refusal (zxvd_rpc.h ZXVD_E_FILES) */
#define WORDHOARD_CHUNK    16384u   /* bytes asked of one files.read */
#define WORDHOARD_LINE_MAX 1024u    /* bytes of one list line */
#define WORDHOARD_SMALL    512u     /* bytes of PACK.tsv */
#define WORDHOARD_NAME_MAX 256u     /* one listed name (zxv_files.h ZXF_NAME_MAX + 1) */
#define WORDHOARD_ASK      100u     /* hits asked of search.query */
#define WORDHOARD_READS    4096u    /* reads of one list, a bound on its size */
#define WORDHOARD_SLOT     "46"

const zxm_ai_action_t WORDHOARD_AI[WORDHOARD_AI_COUNT] = {
    {"Define", "Writer", 200u},
};

/* ---- small text helpers, bounded ---- */

static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    size_t n = strlen(s);
    if (*at + n + 1u > cap) return false;
    memcpy(out + *at, s, n + 1u);
    *at += (uint32_t) n;
    return true;
}

static void say(wordhoard_t *w, const char *a, const char *b)
{
    uint32_t at = 0;
    w->notice[0] = 0;
    if (put(w->notice, sizeof w->notice, &at, a) && b) put(w->notice, sizeof w->notice, &at, b);
}

static uint32_t len_of(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    while (s && n < cap && s[n]) n++;
    return n;
}

static char fold(char c)
{
    return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
}

/* a and b the same bytes, ASCII case folded */
static bool same_fold(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (fold(*a) != fold(*b)) return false;
    return !*a && !*b;
}

/* word begins with prefix, ASCII case folded */
static bool starts_fold(const char *word, const char *prefix)
{
    for (; *prefix; word++, prefix++)
        if (!*word || fold(*word) != fold(*prefix)) return false;
    return true;
}

int64_t wordhoard_code_points(const char *s, uint32_t n, uint32_t *cp, uint32_t cap)
{
    uint32_t k = 0;
    if (!s && n) return WORDHOARD_REFUSED;
    for (uint32_t i = 0; i < n;) {
        unsigned char c = (unsigned char) s[i];
        uint32_t more, v;
        if (c < 0x80u)
            more = 0, v = c;
        else if (c >= 0xC2u && c <= 0xDFu)
            more = 1u, v = c & 0x1Fu;
        else if ((c & 0xF0u) == 0xE0u)
            more = 2u, v = c & 0x0Fu;
        else if (c >= 0xF0u && c <= 0xF4u)
            more = 3u, v = c & 0x07u;
        else
            return WORDHOARD_REFUSED;
        if (n - i <= more) return WORDHOARD_REFUSED;
        for (uint32_t j = 1; j <= more; j++) {
            unsigned char b = (unsigned char) s[i + j];
            if ((b & 0xC0u) != 0x80u) return WORDHOARD_REFUSED;
            v = (v << 6) | (b & 0x3Fu);
        }
        if ((more == 2u && v < 0x800u) || (more == 3u && (v < 0x10000u || v > 0x10FFFFu)) ||
            !wordhoard_cp_ok(v))
            return WORDHOARD_REFUSED;
        if (k >= cap) return WORDHOARD_REFUSED;
        if (cp) cp[k] = v;
        k++;
        i += more + 1u;
    }
    return k;
}

/* n bytes of s are UTF-8 with no control byte (tab and newline included) */
static bool text_ok(const char *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if ((unsigned char) s[i] < 0x20u || s[i] == 0x7F) return false;
    return wordhoard_code_points(s, n, NULL, UINT32_MAX) >= 0;
}

/* a typed word: 1..WORDHOARD_WORD_BYTES bytes of text_ok */
static bool word_ok(const char *s)
{
    uint32_t n = len_of(s, WORDHOARD_WORD_BYTES + 1u);
    return s && n >= 1u && n <= WORDHOARD_WORD_BYTES && text_ok(s, n);
}

/* ---- zxvd ---- */

static bool no_such(const zxm_link_t *l, int rc)
{
    return rc == ZXM_E_REMOTE && l->err_code == WORDHOARD_E_FILES && strstr(l->err, "no such");
}

/* files.read of a whole small file into text (cap bytes); its length, or a
 * negative ZXM_ error: ZXM_E_STATE when it does not exist, ZXM_E_TOOBIG
 * when it does not fit. */
static int32_t read_small(wordhoard_t *w, const char *path, char *text, uint32_t cap)
{
    zxm_link_t *l = w->link;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", path);
    zxm_p_u64(l, "max", cap - 1u);
    int rc = zxm_call(l);
    if (no_such(l, rc)) return ZXM_E_STATE;
    if (rc) return rc;
    bool eof = false;
    int32_t t = zxm_get(l, l->result, "text");
    if (t < 0 || zxm_get_bool(l, l->result, "eof", &eof)) return ZXM_E_PROTOCOL;
    if (!eof) return ZXM_E_TOOBIG;
    int32_t len = w4_json_str(l->resp, &l->tok[t], text, cap);
    return len < 0 ? ZXM_E_TOOBIG : len;
}

/* WM6: a tag's place in xlate.languages, 0 when not there */
static int64_t lang_place(const wordhoard_t *w, const char *tag)
{
    for (uint32_t i = 0; i < w->nlangs; i++)
        if (same_fold(w->langs[i], tag)) return (int64_t) i + 1;
    return 0;
}

/* ---- WM2: one line of a list ---- */

/* the next field of line[*at..n) up to a tab or the end, as a span */
static void field(const char *line, uint32_t n, uint32_t *at, uint32_t *start, uint32_t *len)
{
    uint32_t i = *at;
    *start = i;
    while (i < n && line[i] != '\t') i++;
    *len = i - *start;
    *at = i < n ? i + 1u : n;
}

static bool tag_ok(const char *s, uint32_t n)
{
    if (n > WORDHOARD_TAG_MAX) return false;
    for (uint32_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '-'))
            return false;
    }
    return true;
}

/* the senses of s[0..n): synset numbers, comma-separated, a repeat dropped
 * (WH4); their count 1..WORDHOARD_ENTRY_SENSES, or 0 when they do not hold */
static uint32_t senses(const char *s, uint32_t n, int64_t *out)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < n;) {
        int64_t v = 0;
        uint32_t digits = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9') {
            if (v > (WORDHOARD_SENSE_MAX - (s[i] - '0')) / 10) return 0;
            v = v * 10 + (s[i] - '0');
            i++;
            digits++;
        }
        if (!digits || v < 1) return 0;
        if (i < n) {
            if (s[i] != ',' || i + 1u >= n) return 0;
            i++;
        }
        bool seen = false;
        for (uint32_t j = 0; j < k; j++) seen = seen || out[j] == v;
        if (seen) continue;
        if (k >= WORDHOARD_ENTRY_SENSES) return 0;
        out[k++] = v;
    }
    return k;
}

/* WM2: a line into a new entry; false (and skipped) when it does not hold */
static void take_line(wordhoard_t *w, const char *line, uint32_t n, int64_t list, int64_t stamp)
{
    uint32_t at = 0, ws, wl, ls, ll, ss, sl, gs, gl;
    if (!n || line[0] == '#') return;
    field(line, n, &at, &ws, &wl);
    field(line, n, &at, &ls, &ll);
    field(line, n, &at, &ss, &sl);
    field(line, n, &at, &gs, &gl);
    if (at < n || !sl || !wl || wl > WORDHOARD_WORD_BYTES || !text_ok(line + ws, wl) ||
        !tag_ok(line + ls, ll) || !text_ok(line + gs, gl)) {
        w->skipped++;
        return;
    }
    if (w->n >= WORDHOARD_WORDS) {
        w->dropped++;
        return;
    }
    wordhoard_entry_t *e = &w->entry[w->n];
    memset(e, 0, sizeof *e);
    e->nsense = senses(line + ss, sl, e->sense);
    if (!e->nsense) {
        w->skipped++;
        return;
    }
    memcpy(e->word, line + ws, wl);
    if (ll)
        memcpy(e->lang, line + ls, ll);
    else
        memcpy(e->lang, "und", 4u);
    uint32_t g = gl > WORDHOARD_GLOSS_BYTES ? zxm_utf8_cut(line + gs, WORDHOARD_GLOSS_BYTES) : gl;
    memcpy(e->gloss, line + gs, g);
    int64_t ncp = wordhoard_code_points(e->word, wl, e->cp, WORDHOARD_WORD_CPS);
    e->comparable = ncp >= 0;
    e->ncp = ncp >= 0 ? (uint32_t) ncp : 0u;
    e->code = kindred_name_code(e->word, wl, NULL);
    e->langno = lang_place(w, e->lang);
    e->list = list;
    e->stamp = stamp;
    for (uint32_t i = 0; i < e->nsense; i++)
        if (!wordhoard_record_ok(e->code, e->sense[i], e->langno, list, stamp)) {
            w->skipped++;
            return;
        }
    w->n++;
}

/* WM2: a whole list, read a chunk at a time; a missing file is empty */
static int read_list(wordhoard_t *w, const char *path, int64_t list, int64_t stamp)
{
    static char chunk[WORDHOARD_CHUNK + 1u];
    static char line[WORDHOARD_LINE_MAX];
    zxm_link_t *l = w->link;
    uint32_t len = 0;
    bool over = false, eof = false;
    uint64_t offset = 0;
    for (uint32_t reads = 0; !eof; reads++) {
        if (reads >= WORDHOARD_READS) return ZXM_E_TOOBIG;
        zxm_begin(l, "files.read");
        zxm_p_str(l, "path", path);
        zxm_p_u64(l, "offset", offset);
        zxm_p_u64(l, "max", WORDHOARD_CHUNK);
        int rc = zxm_call(l);
        if (no_such(l, rc) && offset == 0u) return ZXM_OK;
        if (rc) return rc;
        uint64_t next = 0;
        int32_t t = zxm_get(l, l->result, "text");
        if (t < 0 || zxm_get_bool(l, l->result, "eof", &eof) ||
            zxm_get_u64(l, l->result, "next", &next))
            return ZXM_E_PROTOCOL;
        int32_t got = w4_json_str(l->resp, &l->tok[t], chunk, sizeof chunk);
        if (got < 0 || (!eof && next <= offset)) return ZXM_E_PROTOCOL;
        offset = next;
        for (int32_t i = 0; i < got; i++) {
            if (chunk[i] == '\n') {
                if (over)
                    w->skipped++;
                else
                    take_line(w, line, len > 0u && line[len - 1u] == '\r' ? len - 1u : len, list,
                              stamp);
                len = 0;
                over = false;
            } else if (len < WORDHOARD_LINE_MAX) {
                line[len++] = chunk[i];
            } else {
                over = true;
            }
        }
    }
    if (over)
        w->skipped++;
    else if (len)
        take_line(w, line, len, list, stamp);
    return ZXM_OK;
}

/* the value of `key` in a PACK.tsv text (key TAB value lines) is `want` */
static bool pack_says(const char *text, uint32_t n, const char *key, const char *want)
{
    uint32_t kn = len_of(key, 32u), wn = len_of(want, 32u);
    for (uint32_t at = 0; at < n;) {
        uint32_t end = at;
        while (end < n && text[end] != '\n') end++;
        if (end - at == kn + 1u + wn && !strncmp(text + at, key, kn) && text[at + kn] == '\t' &&
            !strncmp(text + at + kn + 1u, want, wn))
            return true;
        at = end + 1u;
    }
    return false;
}

/* a folder's listing: the mtime of `name` in it (-1 when it is not
 * there); ZXM_E_STATE when the folder does not exist */
static int stamp_of(wordhoard_t *w, const char *dir, const char *name, int64_t *stamp)
{
    zxm_link_t *l = w->link;
    *stamp = -1;
    zxm_begin(l, "files.list");
    zxm_p_str(l, "path", dir);
    int rc = zxm_call(l);
    if (no_such(l, rc)) return ZXM_E_STATE;
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "entries");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < zxm_len(l, arr); i++) {
        char got[WORDHOARD_NAME_MAX];
        int64_t mtime = 0;
        int32_t o = zxm_at(l, arr, i);
        if (o < 0 || zxm_get_str(l, o, "name", got, sizeof got) ||
            zxm_get_i64(l, o, "mtime", &mtime))
            return ZXM_E_PROTOCOL;
        if (!strcmp(got, name)) *stamp = mtime;
    }
    return ZXM_OK;
}

/* WM3: one installed pack, read only when its PACK.tsv holds */
static int load_pack(wordhoard_t *w, int64_t kind)
{
    static char text[WORDHOARD_SMALL];
    const char *dir = kind == WORDHOARD_PACK_CORE ? WORDHOARD_CORE : WORDHOARD_OPTIONAL;
    const char *pack = kind == WORDHOARD_PACK_CORE ? "core" : "optional";
    const char *state = kind == WORDHOARD_PACK_CORE ? "TRUE" : "BOTH";
    char path[WORDHOARD_PATH_MAX];
    uint32_t at = 0;
    int64_t stamp = -1;
    int rc = stamp_of(w, dir, "words.tsv", &stamp);
    if (rc == ZXM_E_STATE) return ZXM_OK; /* not installed */
    if (rc) return rc;
    if (stamp < 0 || stamp > WORDHOARD_STAMP_MAX) {
        say(w, "a pack without its word list: ", pack);
        return ZXM_OK;
    }
    if (!put(path, sizeof path, &at, dir) || !put(path, sizeof path, &at, "/PACK.tsv"))
        return ZXM_E_ARG;
    int32_t n = read_small(w, path, text, sizeof text);
    if (n == ZXM_E_STATE || n == ZXM_E_TOOBIG ||
        (n >= 0 && !(pack_says(text, (uint32_t) n, "app", "wordhoard") &&
                     pack_says(text, (uint32_t) n, "slot", WORDHOARD_SLOT) &&
                     pack_says(text, (uint32_t) n, "pack", pack) &&
                     pack_says(text, (uint32_t) n, "state", state)))) {
        say(w, "a pack that does not hold its licence rule, not read: ", pack);
        return ZXM_OK;
    }
    if (n < 0) return n;
    at = 0;
    if (!put(path, sizeof path, &at, dir) || !put(path, sizeof path, &at, "/ATTRIBUTION.txt"))
        return ZXM_E_ARG;
    n = read_small(w, path, w->attribution[kind], sizeof w->attribution[kind]);
    if (n == ZXM_E_STATE || n == ZXM_E_TOOBIG || n == 0) {
        w->attribution[kind][0] = 0;
        say(w, "a pack whose attribution cannot be shown, not read: ", pack);
        return ZXM_OK;
    }
    if (n < 0) return n;
    at = 0;
    if (!put(path, sizeof path, &at, dir) || !put(path, sizeof path, &at, "/words.tsv"))
        return ZXM_E_ARG;
    rc = read_list(w, path, kind, stamp);
    if (rc) return rc;
    w->pack[kind] = true;
    return ZXM_OK;
}

/* WM1: the owner's own lists, Wordhoard/lists/<name>.tsv */
static int load_own(wordhoard_t *w)
{
    static char names[WORDHOARD_LIST_FILES][WORDHOARD_NAME_MAX];
    static int64_t stamps[WORDHOARD_LIST_FILES];
    zxm_link_t *l = w->link;
    uint32_t k = 0;
    zxm_begin(l, "files.list");
    zxm_p_str(l, "path", WORDHOARD_LISTS);
    int rc = zxm_call(l);
    if (no_such(l, rc)) return ZXM_OK;
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "entries");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < zxm_len(l, arr); i++) {
        char got[WORDHOARD_NAME_MAX];
        bool dir = false;
        int64_t mtime = 0;
        int32_t o = zxm_at(l, arr, i);
        if (o < 0 || zxm_get_str(l, o, "name", got, sizeof got) ||
            zxm_get_bool(l, o, "dir", &dir) || zxm_get_i64(l, o, "mtime", &mtime))
            return ZXM_E_PROTOCOL;
        uint32_t n = len_of(got, sizeof got);
        if (dir || n < 5u || strcmp(got + n - 4u, ".tsv") || mtime < 0 ||
            mtime > WORDHOARD_STAMP_MAX)
            continue;
        if (k >= WORDHOARD_LIST_FILES) {
            say(w, "more word lists than are read; the first are", NULL);
            break;
        }
        memcpy(names[k], got, n + 1u);
        stamps[k++] = mtime;
    }
    for (uint32_t i = 0; i < k; i++) {
        char path[WORDHOARD_PATH_MAX];
        uint32_t at = 0;
        if (!put(path, sizeof path, &at, WORDHOARD_LISTS "/") ||
            !put(path, sizeof path, &at, names[i]))
            return ZXM_E_ARG;
        rc = read_list(w, path, WORDHOARD_OWN, stamps[i]);
        if (rc) return rc;
    }
    return ZXM_OK;
}

/* WM4: a before b in the index */
static bool before(const wordhoard_entry_t *a, const wordhoard_entry_t *b)
{
    if (a->code != b->code) return a->code < b->code;
    int c = strcmp(a->word, b->word);
    if (c) return c < 0;
    return a->list < b->list;
}

static void index_words(wordhoard_t *w)
{
    for (uint32_t i = 0; i < w->n; i++) {
        uint32_t j = i;
        while (j > 0u && before(&w->entry[i], &w->entry[w->order[j - 1u]])) {
            w->order[j] = w->order[j - 1u];
            j--;
        }
        w->order[j] = i;
    }
}

/* ---- the actions ---- */

void wordhoard_init(wordhoard_t *w, zxm_link_t *link)
{
    memset(w, 0, sizeof *w);
    w->link = link;
    memcpy(w->typed_lang, "und", 4u);
}

int wordhoard_load(wordhoard_t *w)
{
    if (!w || !w->link) return ZXM_E_ARG;
    w->loaded = false;
    w->n = w->skipped = w->dropped = 0;
    w->nhits = w->nsuggest = w->nsyn = 0;
    for (uint32_t k = 0; k < 3u; k++) {
        w->pack[k] = false;
        w->attribution[k][0] = 0;
    }
    w->notice[0] = 0;
    int rc = load_pack(w, WORDHOARD_PACK_CORE);
    if (!rc) rc = load_pack(w, WORDHOARD_PACK_OPTIONAL);
    if (!rc) rc = load_own(w);
    if (rc) {
        w->n = 0;
        return rc;
    }
    index_words(w);
    w->loaded = true;
    return ZXM_OK;
}

int wordhoard_languages(wordhoard_t *w)
{
    if (!w || !w->link) return ZXM_E_ARG;
    zxm_link_t *l = w->link;
    zxm_begin(l, "xlate.languages");
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "languages");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t k = 0;
    for (uint32_t i = 0; i < zxm_len(l, arr) && k < WORDHOARD_LANGS; i++) {
        int32_t o = zxm_at(l, arr, i);
        if (o < 0 || zxm_get_str(l, o, "tag", w->langs[k], sizeof w->langs[k])) continue;
        if (tag_ok(w->langs[k], len_of(w->langs[k], sizeof w->langs[k])) && w->langs[k][0]) k++;
    }
    w->nlangs = k;
    for (uint32_t i = 0; i < w->n; i++) w->entry[i].langno = lang_place(w, w->entry[i].lang);
    return ZXM_OK;
}

int wordhoard_prefix(wordhoard_t *w, const char *typed)
{
    if (!w || !word_ok(typed)) return ZXM_E_ARG;
    if (!w->loaded) return ZXM_E_STATE;
    int64_t len = 0;
    uint32_t n = len_of(typed, WORDHOARD_WORD_BYTES);
    int64_t code = kindred_name_code(typed, n, &len);
    memcpy(w->typed, typed, n + 1u);
    w->nhits = w->nsuggest = w->nsyn = 0;
    for (uint32_t k = 0; k < w->n && w->nhits < WORDHOARD_HITS; k++) {
        uint32_t i = w->order[k];
        if (kindred_prefix_hit(w->entry[i].code, code, len) == 1 &&
            starts_fold(w->entry[i].word, typed))
            w->hit[w->nhits++] = i;
    }
    return ZXM_OK;
}

/* WM5: xlate.detect's tag for the word; "und" when zxvd refuses */
static int detect(wordhoard_t *w, const char *word)
{
    zxm_link_t *l = w->link;
    memcpy(w->typed_lang, "und", 4u);
    zxm_begin(l, "xlate.detect");
    zxm_p_str(l, "text", word);
    int rc = zxm_call(l);
    if (rc == ZXM_E_REMOTE) {
        say(w, "the language is not known: ", l->err);
        return ZXM_OK;
    }
    if (rc) return rc;
    char tag[WORDHOARD_TAG_MAX + 1];
    if (!zxm_get_str(l, l->result, "language", tag, sizeof tag) &&
        tag_ok(tag, len_of(tag, sizeof tag)) && tag[0])
        memcpy(w->typed_lang, tag, len_of(tag, sizeof tag) + 1u);
    return ZXM_OK;
}

/* WH4, WH5: the synonyms of the hits */
static void synonyms(wordhoard_t *w)
{
    static int64_t head[WORDHOARD_SENSES], shared[WORDHOARD_WORDS];
    static uint32_t cand[WORDHOARD_WORDS];
    uint32_t m = 0, nc = 0;
    for (uint32_t h = 0; h < w->nhits; h++) {
        const wordhoard_entry_t *e = &w->entry[w->hit[h]];
        for (uint32_t s = 0; s < e->nsense && m < WORDHOARD_SENSES; s++) {
            bool seen = false;
            for (uint32_t j = 0; j < m; j++) seen = seen || head[j] == e->sense[s];
            if (!seen) head[m++] = e->sense[s];
        }
    }
    for (uint32_t k = 0; k < w->n; k++) {
        uint32_t i = w->order[k];
        const wordhoard_entry_t *e = &w->entry[i];
        if (same_fold(e->word, w->typed)) continue;
        int64_t s = wordhoard_shared_senses(head, m, e->sense, e->nsense);
        if (s < 1) continue;
        bool merged = false;
        for (uint32_t c = 0; c < nc && !merged; c++)
            if (same_fold(w->entry[cand[c]].word, e->word)) {
                if (s > shared[c]) shared[c] = s;
                merged = true;
            }
        if (merged) continue;
        cand[nc] = i;
        shared[nc++] = s;
    }
    /* by place (assay.score_rank), the index's order within a place */
    static int64_t place[WORDHOARD_WORDS];
    for (uint32_t c = 0; c < nc; c++) place[c] = assay_score_rank(shared, nc, c);
    for (int64_t p = 1; p <= (int64_t) nc && w->nsyn < WORDHOARD_SYNONYMS; p++)
        for (uint32_t c = 0; c < nc && w->nsyn < WORDHOARD_SYNONYMS; c++) {
            if (place[c] != p) continue;
            w->syn[w->nsyn] = cand[c];
            w->syn_shared[w->nsyn] = shared[c];
            w->syn_place[w->nsyn++] = p;
        }
}

/* WH3: the offers for a word that is not there */
static void offers(wordhoard_t *w)
{
    uint32_t cp[WORDHOARD_WORD_CPS];
    int64_t n = wordhoard_code_points(w->typed, len_of(w->typed, WORDHOARD_WORD_BYTES), cp,
                                      WORDHOARD_WORD_CPS);
    if (n < 1) return;
    int64_t limit = wordhoard_suggest_limit((uint32_t) n);
    for (uint32_t k = 0; k < w->n; k++) {
        uint32_t i = w->order[k];
        const wordhoard_entry_t *e = &w->entry[i];
        if (!e->comparable) continue;
        int64_t d = wordhoard_distance(cp, (uint32_t) n, e->cp, e->ncp);
        if (d < 1 || d > limit) continue;
        bool dup = false;
        for (uint32_t j = 0; j < w->nsuggest && !dup; j++)
            dup = same_fold(w->entry[w->suggest[j]].word, e->word);
        if (dup) continue;
        uint32_t at = w->nsuggest;
        while (at > 0u && w->suggest_distance[at - 1u] > d) at--;
        if (at >= WORDHOARD_SUGGEST) continue;
        uint32_t last = w->nsuggest < WORDHOARD_SUGGEST ? w->nsuggest : WORDHOARD_SUGGEST - 1u;
        for (uint32_t j = last; j > at; j--) {
            w->suggest[j] = w->suggest[j - 1u];
            w->suggest_distance[j] = w->suggest_distance[j - 1u];
        }
        w->suggest[at] = i;
        w->suggest_distance[at] = d;
        if (w->nsuggest < WORDHOARD_SUGGEST) w->nsuggest++;
    }
}

int wordhoard_lookup(wordhoard_t *w, const char *word)
{
    if (!w || !word_ok(word)) return ZXM_E_ARG;
    if (!w->loaded) return ZXM_E_STATE;
    uint32_t n = len_of(word, WORDHOARD_WORD_BYTES);
    memcpy(w->typed, word, n + 1u);
    w->nhits = w->nsuggest = w->nsyn = 0;
    w->notice[0] = 0;
    int rc = detect(w, word);
    if (rc) return rc;
    for (uint32_t k = 0; k < w->n && w->nhits < WORDHOARD_HITS; k++)
        if (same_fold(w->entry[w->order[k]].word, word)) w->hit[w->nhits++] = w->order[k];
    if (w->nhits)
        synonyms(w);
    else
        offers(w);
    return ZXM_OK;
}

int wordhoard_find_lists(wordhoard_t *w, const char *word)
{
    if (!w || !w->link || !word_ok(word)) return ZXM_E_ARG;
    zxm_link_t *l = w->link;
    w->nfound = 0;
    zxm_begin(l, "search.query");
    zxm_p_str(l, "q", word);
    zxm_p_u64(l, "max", WORDHOARD_ASK);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "hits");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_len(l, arr) > WORDHOARD_ASK)
        return ZXM_E_PROTOCOL;
    for (uint32_t i = 0; i < zxm_len(l, arr) && w->nfound < WORDHOARD_FOUND; i++) {
        char *path = w->found[w->nfound];
        int32_t o = zxm_at(l, arr, i);
        if (o < 0 || l->tok[o].type != W4_J_OBJ) return ZXM_E_PROTOCOL;
        if (zxm_get_str(l, o, "path", path, WORDHOARD_PATH_MAX)) continue;
        uint32_t pn = len_of(path, WORDHOARD_PATH_MAX);
        if (pn < 15u || strncmp(path, WORDHOARD_DIR "/", 10u) || strcmp(path + pn - 4u, ".tsv"))
            continue;
        w->nfound++;
    }
    return ZXM_OK;
}

int wordhoard_define(wordhoard_t *w, const char *word)
{
    if (!w || !w->link || !word_ok(word)) return ZXM_E_ARG;
    uint32_t n = len_of(word, WORDHOARD_WORD_BYTES);
    int rc = zxm_ai_ask(w->link, &WORDHOARD_AI[WORDHOARD_AI_DEFINE], WORDHOARD_AI_DEFINE,
                        "Define the word below in one or two plain sentences, as a dictionary "
                        "would.",
                        word, n, &w->held);
    if (rc) return rc;
    memcpy(w->held_word, word, n + 1u);
    return ZXM_OK;
}

int wordhoard_accept(wordhoard_t *w, int64_t owner, int64_t approved)
{
    if (!w) return ZXM_E_ARG;
    if (w->held.state != ZXM_HELD_PENDING) return ZXM_E_STATE;
    if (!warren_approval_ok(owner, approved)) return ZXM_E_ARG; /* still held */
    return zxm_held_accept(&w->held);
}

int wordhoard_reject(wordhoard_t *w)
{
    if (!w) return ZXM_E_ARG;
    int rc = zxm_held_reject(&w->held);
    if (!rc) w->held_word[0] = 0;
    return rc;
}
