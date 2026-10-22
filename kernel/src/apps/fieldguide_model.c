/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fieldguide_model.c — Fieldguide (see fieldguide_model.h). */
#include <string.h>

#include "fieldguide_model.h"

#define FIELDGUIDE_TAXON_MAX INT64_C(999999999) /* FR4: taxon's 9 digits */
#define FIELDGUIDE_PLACE_MAX INT64_C(9999)      /* FR4: article's 4 digits */
#define FIELDGUIDE_LINE_MAX  320u               /* one header or key line */

void fieldguide_init(fieldguide_t *g, zxm_link_t *link)
{
    memset(g, 0, sizeof *g);
    g->link = link;
    g->caution_level = FIELDGUIDE_CAUTION_UNKNOWN;
    g->caution = LPRES_STATE_NEITHER;
}

/* ---- text, bounded ---- */

static uint32_t len_of(const char *s, uint32_t cap)
{
    uint32_t n = 0;
    if (!s) return 0;
    while (n < cap && s[n]) n++;
    return n;
}

/* n bytes of well-formed UTF-8 with no control byte */
static bool text_ok(const char *s, uint32_t n)
{
    const unsigned char *p = (const unsigned char *) s;
    uint32_t i = 0;
    while (i < n) {
        unsigned char c = p[i];
        uint32_t more;
        if (c < 0x20u || c == 0x7fu) return false;
        if (c < 0x80u) {
            i++;
            continue;
        }
        if (c >= 0xc2u && c <= 0xdfu)
            more = 1;
        else if (c >= 0xe0u && c <= 0xefu)
            more = 2;
        else if (c >= 0xf0u && c <= 0xf4u)
            more = 3;
        else
            return false;
        if (more > n - 1u - i) return false;
        for (uint32_t k = 1; k <= more; k++)
            if ((p[i + k] & 0xc0u) != 0x80u) return false;
        if (c == 0xe0u && p[i + 1] < 0xa0u) return false;
        if (c == 0xedu && p[i + 1] > 0x9fu) return false;
        if (c == 0xf0u && p[i + 1] < 0x90u) return false;
        if (c == 0xf4u && p[i + 1] > 0x8fu) return false;
        i += more + 1u;
    }
    return true;
}

static char fold(char c)
{
    return c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
}

/* s[0..n) is word, ASCII case folded */
static bool is_word(const char *s, uint32_t n, const char *word)
{
    uint32_t k = 0;
    for (; k < n && word[k]; k++)
        if (fold(s[k]) != word[k]) return false;
    return k == n && !word[k];
}

static bool ends_with(const char *s, uint32_t n, const char *tail)
{
    uint32_t t = len_of(tail, 16u);
    return n >= t && !memcmp(s + n - t, tail, t);
}

/* s[0..n) without spaces at either end */
static void trim(const char **s, uint32_t *n)
{
    while (*n && ((*s)[0] == ' ' || (*s)[0] == '\t')) (*s)++, (*n)--;
    while (*n && ((*s)[*n - 1u] == ' ' || (*s)[*n - 1u] == '\t' || (*s)[*n - 1u] == '\r')) (*n)--;
}

/* a whole number 1..max from s[0..n), or -1 */
static int64_t number_of(const char *s, uint32_t n, int64_t max)
{
    int64_t v = 0;
    if (!n || n > 18u) return -1;
    for (uint32_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        v = v * 10 + (s[i] - '0');
    }
    return v >= 1 && v <= max ? v : -1;
}

/* copy s[0..n) into out (cap with its NUL), cut on a character boundary */
static void copy_text(char *out, uint32_t cap, const char *s, uint32_t n)
{
    if (n > cap - 1u) n = zxm_utf8_cut(s, cap - 1u);
    memcpy(out, s, n);
    out[n] = 0;
}

static void put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    uint32_t n = len_of(s, cap);
    if (*at + n >= cap) n = cap - 1u - *at;
    memcpy(out + *at, s, n);
    *at += n;
    out[*at] = 0;
}

/* ---- FM1, FM2: an article's head ---- */

bool fieldguide_parse_head(const char *text, uint32_t n, fieldguide_article_t *a)
{
    int toxic = -1, dangerous = -1; /* 1 yes, 0 no, -1 not said */
    bool any = false;
    uint32_t i = 0;
    if (!text || !a) return false;
    a->name[0] = a->common[0] = 0;
    a->taxon = 0;
    a->kind = FIELDGUIDE_KIND_OTHER;
    while (i < n) {
        uint32_t e = i;
        while (e < n && text[e] != '\n') e++;
        const char *line = text + i;
        uint32_t ln = e - i;
        i = e + 1u;
        trim(&line, &ln);
        if (!ln) break; /* the head ends at the first empty line */
        uint32_t colon = 0;
        while (colon < ln && line[colon] != ':') colon++;
        if (colon == ln || ln > FIELDGUIDE_LINE_MAX) continue; /* not a header line */
        const char *key = line, *val = line + colon + 1u;
        uint32_t kn = colon, vn = ln - colon - 1u;
        trim(&key, &kn);
        trim(&val, &vn);
        if (!text_ok(val, vn)) continue;
        if (is_word(key, kn, "name")) {
            copy_text(a->name, sizeof a->name, val, vn);
            any = true;
        } else if (is_word(key, kn, "common")) {
            copy_text(a->common, sizeof a->common, val, vn);
            any = true;
        } else if (is_word(key, kn, "taxon")) {
            int64_t t = number_of(val, vn, FIELDGUIDE_TAXON_MAX);
            a->taxon = t > 0 ? t : 0;
            any = true;
        } else if (is_word(key, kn, "kind")) {
            a->kind = is_word(val, vn, "plant")    ? FIELDGUIDE_KIND_PLANT
                      : is_word(val, vn, "fungus") ? FIELDGUIDE_KIND_FUNGUS
                      : is_word(val, vn, "animal") ? FIELDGUIDE_KIND_ANIMAL
                                                   : FIELDGUIDE_KIND_OTHER;
            any = true;
        } else if (is_word(key, kn, "toxic") || is_word(key, kn, "dangerous")) {
            int said = is_word(val, vn, "yes") ? 1 : is_word(val, vn, "no") ? 0 : -1;
            if (is_word(key, kn, "toxic"))
                toxic = said;
            else
                dangerous = said;
            any = true;
        }
    }
    if (toxic == 1 || dangerous == 1)
        a->mark = FIELDGUIDE_MARK_TOXIC;
    else if (toxic == 0 || dangerous == 0)
        a->mark = FIELDGUIDE_MARK_NONE;
    else
        a->mark = FIELDGUIDE_MARK_UNKNOWN;
    return any;
}

/* ---- FM5: the caution ---- */

void fieldguide_caution_notice(int64_t level, lpres_state_t state, char *out, uint32_t cap)
{
    uint32_t at = 0;
    if (!out || !cap) return;
    out[0] = 0;
    if (level == FIELDGUIDE_CAUTION_MARKED)
        put(out, cap, &at,
            state == LPRES_STATE_BOTH
                ? "Caution: an article here marks a species toxic or dangerous, and another "
                  "says it is not; treat it as toxic. "
                : "Caution: an article here marks a species toxic or dangerous. ");
    else if (level == FIELDGUIDE_CAUTION_NONE)
        put(out, cap, &at,
            "No article here marks a species toxic or dangerous; that does not make it "
            "harmless. ");
    else
        put(out, cap, &at, "Toxicity unknown: an article here carries no toxicity information. ");
    put(out, cap, &at, FIELDGUIDE_NEVER_SAFE);
}

static void caution_of(fieldguide_t *g)
{
    int64_t mark[FIELDGUIDE_HITS];
    lpres_state_t s = LPRES_STATE_NEITHER;
    for (uint32_t i = 0; i < g->n; i++) {
        mark[i] = g->match[i].mark;
        if (mark[i] == FIELDGUIDE_MARK_TOXIC)
            s = lpres_kjoin(s, LPRES_STATE_TRUE);
        else if (mark[i] == FIELDGUIDE_MARK_NONE)
            s = lpres_kjoin(s, LPRES_STATE_FALSE);
    }
    g->caution_level = fieldguide_caution(mark, g->n);
    g->caution = s;
    fieldguide_caution_notice(g->caution_level, s, g->notice, sizeof g->notice);
}

/* ---- FM4: ranking ---- */

int fieldguide_rank(fieldguide_t *g)
{
    static fieldguide_article_t sorted[FIELDGUIDE_HITS];
    int64_t key[FIELDGUIDE_HITS];
    if (!g) return ZXM_E_ARG;
    for (uint32_t i = 0; i < g->n; i++) {
        int64_t keyed = g->reached && g->match[i].taxon == g->reached;
        key[i] = fieldguide_match_key(g->match[i].score, keyed);
        if (key[i] == FIELDGUIDE_REFUSED) return ZXM_E_STATE;
    }
    for (uint32_t i = 0; i < g->n; i++) {
        g->match[i].key = key[i];
        g->match[i].rank = assay_score_rank(key, g->n, i);
    }
    uint32_t k = 0; /* best first; ties keep the search's order */
    for (int64_t r = 1; r <= (int64_t) g->n; r++)
        for (uint32_t i = 0; i < g->n; i++)
            if (g->match[i].rank == r) sorted[k++] = g->match[i];
    memcpy(g->match, sorted, k * sizeof sorted[0]);
    return ZXM_OK;
}

/* ---- FM3: refresh and search ---- */

int fieldguide_refresh(fieldguide_t *g)
{
    int64_t files = 0;
    if (!g || !g->link) return ZXM_E_ARG;
    zxm_begin(g->link, "search.refresh");
    int rc = zxm_call(g->link);
    if (rc) return rc;
    if (zxm_get_i64(g->link, g->link->result, "text_files", &files) || files < 0)
        return ZXM_E_PROTOCOL;
    g->indexed = files;
    g->refreshed = true;
    return ZXM_OK;
}

/* an article's path: FIELDGUIDE_DIR/..., .txt, not under the keys */
static bool article_path(const char *p, uint32_t n)
{
    uint32_t d = sizeof FIELDGUIDE_DIR - 1u, k = sizeof FIELDGUIDE_KEYS - 1u;
    if (n <= d + 5u || memcmp(p, FIELDGUIDE_DIR "/", d + 1u) || !ends_with(p, n, ".txt"))
        return false;
    return !(n > k && !memcmp(p, FIELDGUIDE_KEYS "/", k + 1u));
}

/* the head of one article (files.read), parsed */
static int read_head(zxm_link_t *l, fieldguide_article_t *a)
{
    static char text[FIELDGUIDE_HEAD + 1u];
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", a->path);
    zxm_p_u64(l, "max", FIELDGUIDE_HEAD);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_str(l, l->result, "text", text, sizeof text)) return ZXM_E_PROTOCOL;
    return fieldguide_parse_head(text, len_of(text, FIELDGUIDE_HEAD), a) ? ZXM_OK : ZXM_E_STATE;
}

int fieldguide_search(fieldguide_t *g, const char *query)
{
    static fieldguide_article_t got[FIELDGUIDE_HITS];
    uint32_t n = len_of(query, FIELDGUIDE_QUERY_MAX + 1u), kept = 0;
    bool word = false;
    if (!g || !g->link || !query || !n || n > FIELDGUIDE_QUERY_MAX || !text_ok(query, n))
        return ZXM_E_ARG;
    for (uint32_t i = 0; i < n && !word; i++) word = query[i] != ' ';
    if (!word) return ZXM_E_ARG;
    zxm_link_t *l = g->link;
    zxm_begin(l, "search.query");
    zxm_p_str(l, "q", query);
    zxm_p_u64(l, "max", FIELDGUIDE_HITS);
    zxm_p_bool(l, "refresh", !g->refreshed);
    int rc = zxm_call(l);
    if (rc) return rc;
    int32_t arr = zxm_get(l, l->result, "hits");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || zxm_len(l, arr) > FIELDGUIDE_HITS)
        return ZXM_E_PROTOCOL;
    uint32_t hits = zxm_len(l, arr);
    for (uint32_t i = 0; i < hits; i++) {
        fieldguide_article_t *a = &got[kept];
        int32_t o = zxm_at(l, arr, i);
        memset(a, 0, sizeof *a);
        if (o < 0 || l->tok[o].type != W4_J_OBJ || zxm_get_i64(l, o, "score", &a->score) ||
            zxm_get_i64(l, o, "mtime", &a->mtime) || a->score < 0 ||
            a->score > FIELDGUIDE_SCORE_MAX)
            return ZXM_E_PROTOCOL;
        if (zxm_get_str(l, o, "path", a->path, sizeof a->path)) continue; /* too long a path */
        if (!article_path(a->path, len_of(a->path, sizeof a->path))) continue;
        kept++;
    }
    /* every hit's path and score are copied out before the link is used again */
    uint32_t keep = 0;
    for (uint32_t i = 0; i < kept; i++) {
        fieldguide_article_t *a = &got[i];
        rc = read_head(l, a);
        if (rc == ZXM_E_STATE || rc == ZXM_E_REMOTE) continue; /* no header, or not text */
        if (rc) return rc;
        if (!fieldguide_article_row_ok((int64_t) keep + 1, a->taxon, a->kind, a->mark, a->mtime) ||
            (int64_t) keep + 1 > FIELDGUIDE_PLACE_MAX)
            continue; /* FR4 */
        got[keep++] = *a;
    }
    memcpy(g->match, got, keep * sizeof got[0]);
    g->n = keep;
    rc = fieldguide_rank(g);
    if (rc) return rc;
    caution_of(g);
    return ZXM_OK;
}

/* ---- FM6: the identification key ---- */

/* a lead: "c<n>" couplet n (1..ncouplets), "t<taxon>" -taxon, "-" 0; else
 * FIELDGUIDE_REFUSED */
static int64_t lead_of(const char *s, uint32_t n, uint32_t ncouplets)
{
    if (n == 1u && s[0] == '-') return 0;
    if (n < 2u) return FIELDGUIDE_REFUSED;
    if (s[0] == 'c') {
        int64_t c = number_of(s + 1, n - 1u, (int64_t) ncouplets);
        return c > 0 ? c : FIELDGUIDE_REFUSED;
    }
    if (s[0] == 't') {
        int64_t t = number_of(s + 1, n - 1u, FIELDGUIDE_TAXON_MAX);
        return t > 0 ? -t : FIELDGUIDE_REFUSED;
    }
    return FIELDGUIDE_REFUSED;
}

/* split line[0..n) at '|' into four fields */
static bool fields_of(const char *line, uint32_t n, const char *f[4], uint32_t fn[4])
{
    uint32_t k = 0, start = 0;
    for (uint32_t i = 0; i <= n; i++) {
        if (i < n && line[i] != '|') continue;
        if (k == 4u) return false;
        f[k] = line + start;
        fn[k] = i - start;
        trim(&f[k], &fn[k]);
        k++;
        start = i + 1u;
    }
    return k == 4u;
}

static int parse_key(fieldguide_t *g, const char *text, uint32_t n)
{
    static fieldguide_couplet_t c[FIELDGUIDE_COUPLETS];
    const char *f[4];
    uint32_t fn[4], count = 0, i = 0;
    /* first pass: count the couplets, so a lead may name a later one */
    while (i < n) {
        uint32_t e = i;
        while (e < n && text[e] != '\n') e++;
        const char *line = text + i;
        uint32_t ln = e - i;
        i = e + 1u;
        trim(&line, &ln);
        if (!ln || line[0] == '#') continue;
        if (count == FIELDGUIDE_COUPLETS) return ZXM_E_STATE;
        count++;
    }
    if (!count) return ZXM_E_STATE;
    uint32_t k = 0;
    i = 0;
    while (i < n) {
        uint32_t e = i;
        while (e < n && text[e] != '\n') e++;
        const char *line = text + i;
        uint32_t ln = e - i;
        i = e + 1u;
        trim(&line, &ln);
        if (!ln || line[0] == '#') continue;
        if (!fields_of(line, ln, f, fn) || number_of(f[0], fn[0], count) != (int64_t) k + 1 ||
            !fn[1] || fn[1] >= FIELDGUIDE_QUESTION || !text_ok(f[1], fn[1]))
            return ZXM_E_STATE;
        c[k].yes = lead_of(f[2], fn[2], count);
        c[k].no = lead_of(f[3], fn[3], count);
        if (c[k].yes == FIELDGUIDE_REFUSED || c[k].no == FIELDGUIDE_REFUSED) return ZXM_E_STATE;
        memcpy(c[k].question, f[1], fn[1]);
        c[k].question[fn[1]] = 0;
        k++;
    }
    memcpy(g->couplet, c, k * sizeof c[0]);
    g->ncouplets = k;
    g->walk = FIELDGUIDE_WALK_ASKING;
    g->at = 1;
    g->steps = 0;
    g->reached = 0;
    return ZXM_OK;
}

int fieldguide_key_load(fieldguide_t *g, const char *path)
{
    static char text[FIELDGUIDE_KEY_READ + 1u];
    uint32_t pn = len_of(path, FIELDGUIDE_PATH_MAX);
    uint32_t k = sizeof FIELDGUIDE_KEYS - 1u;
    bool eof = false;
    if (!g || !g->link || !path || pn >= FIELDGUIDE_PATH_MAX || pn <= k + 5u ||
        memcmp(path, FIELDGUIDE_KEYS "/", k + 1u) || !ends_with(path, pn, ".key") ||
        !text_ok(path, pn))
        return ZXM_E_ARG;
    zxm_link_t *l = g->link;
    zxm_begin(l, "files.read");
    zxm_p_str(l, "path", path);
    zxm_p_u64(l, "max", FIELDGUIDE_KEY_READ);
    int rc = zxm_call(l);
    if (rc) return rc;
    if (zxm_get_bool(l, l->result, "eof", &eof) ||
        zxm_get_str(l, l->result, "text", text, sizeof text))
        return ZXM_E_PROTOCOL;
    if (!eof) return ZXM_E_TOOBIG; /* a key longer than one read */
    return parse_key(g, text, len_of(text, FIELDGUIDE_KEY_READ));
}

const char *fieldguide_key_question(const fieldguide_t *g)
{
    if (!g || g->walk != FIELDGUIDE_WALK_ASKING || !g->at || g->at > g->ncouplets) return NULL;
    return g->couplet[g->at - 1u].question;
}

int fieldguide_key_answer(fieldguide_t *g, int64_t answer)
{
    if (!g) return ZXM_E_ARG;
    if (g->walk != FIELDGUIDE_WALK_ASKING) return ZXM_E_STATE;
    const fieldguide_couplet_t *c = &g->couplet[g->at - 1u];
    int64_t lead = fieldguide_key_next(answer, c->yes, c->no); /* FR2 */
    if (lead == FIELDGUIDE_REFUSED) return ZXM_E_ARG;
    if (lead > 0 && g->steps + 1u >= g->ncouplets) return ZXM_E_STATE; /* a loop */
    g->trail[g->steps++] = g->at;
    if (lead > 0) {
        g->at = (uint32_t) lead;
        return ZXM_OK;
    }
    if (lead < 0) {
        g->walk = FIELDGUIDE_WALK_REACHED;
        g->reached = -lead;
        return fieldguide_rank(g);
    }
    g->walk = FIELDGUIDE_WALK_STOPPED;
    return ZXM_OK;
}

int fieldguide_key_back(fieldguide_t *g)
{
    if (!g) return ZXM_E_ARG;
    if (g->walk == FIELDGUIDE_WALK_NONE || !g->steps) return ZXM_E_STATE;
    g->at = g->trail[--g->steps];
    g->walk = FIELDGUIDE_WALK_ASKING;
    if (!g->reached) return ZXM_OK;
    g->reached = 0;
    return fieldguide_rank(g);
}
