/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* gen_ui_strings.c — build-time host tool: the desktop window's string table,
 * made by the i18n module itself (kernel/src/i18n).
 *
 *   gen_ui_strings TABLE.json SAMPLES.json KEY...
 *
 * gen_ui.py compiles this file with the i18n sources (the real catalog in
 * i18n_catalog.c, the CLDR tables in i18n_tables.c) and runs it with every
 * message key zxv_ui.html uses. It writes:
 *
 * TABLE.json, embedded in the page (zxv_ui.html, <script id="zxv-i18n">):
 *   v     i18n_data_version()
 *   keys  the keys, in the order the sets index them
 *   en    the English text of each key (i18n_msg(en, id))
 *   sets  per distinct catalog outcome, the strings that are NOT the English
 *         text: {"index": [text, review]}, review as i18n_review_t. A key
 *         missing from a set is English; it is a fallback (shown with
 *         lang="en" and a notice) when the locale's language is not English.
 *   nums  per distinct CLDR decimal format: digits, separators, grouping and
 *         the affixes, as i18n_fmt_int()/i18n_fmt_decimal() use them
 *   locs  [tag, autonym, rtl, set, num, english_fallback] per locale
 *   map   normalized tag -> locs index. The page normalizes navigator.language
 *         the way i18n_locale_resolve() does, then looks the tag up here,
 *         cutting subtags from the right until one is found, else English.
 *         The map is built so that this lookup returns exactly what
 *         i18n_locale_resolve() returns (aliases, region-implied scripts,
 *         truncation) for every tag in a large domain (every language in the
 *         registry and its aliases, with and without each script, with every
 *         two-letter and registry region); the tool checks that and fails
 *         the build on any difference.
 *
 * SAMPLES.json: C results the page's JavaScript must reproduce (negotiation,
 * number formatting, message substitution); check_ui_i18n.py runs the page's
 * i18n script under node against them.
 *
 * Hosted only (stdio, malloc). Exits non-zero, with a message, on an unknown
 * key or any internal inconsistency. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "i18n_internal.h"

#define POOL(off) (i18n_pool + (off))

static void die(const char *m, const char *a)
{
    fprintf(stderr, "gen_ui_strings: %s%s%s\n", m, a ? " " : "", a ? a : "");
    exit(1);
}

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) die("out of memory", NULL);
    return p;
}

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = xmalloc(n);
    memcpy(p, s, n);
    return p;
}

/* ---------- a growable byte buffer and a JSON string writer ---------- */

typedef struct {
    char *b;
    size_t n, cap;
} buf_t;

static void put(buf_t *o, const char *s, size_t n)
{
    if (o->n + n + 1 > o->cap) {
        size_t c = o->cap ? o->cap * 2 : 4096;
        while (c < o->n + n + 1) c *= 2;
        char *nb = xmalloc(c);
        if (o->n) memcpy(nb, o->b, o->n);
        free(o->b);
        o->b = nb;
        o->cap = c;
    }
    memcpy(o->b + o->n, s, n);
    o->n += n;
    o->b[o->n] = 0;
}

static void puts_(buf_t *o, const char *s)
{
    put(o, s, strlen(s));
}

static void putf(buf_t *o, const char *fmt, long long v)
{
    char t[32];
    snprintf(t, sizeof t, fmt, v);
    puts_(o, t);
}

/* A JSON string. '<', '>' and '&' are escaped too, so the table can sit in
 * an HTML <script> element without ending it. */
static void jstr(buf_t *o, const char *s)
{
    put(o, "\"", 1);
    for (const unsigned char *p = (const unsigned char *) s; *p; p++) {
        char t[8];
        if (*p == '"' || *p == '\\') {
            t[0] = '\\';
            t[1] = (char) *p;
            put(o, t, 2);
        } else if (*p < 0x20 || *p == '<' || *p == '>' || *p == '&') {
            snprintf(t, sizeof t, "\\u%04x", *p);
            put(o, t, 6);
        } else
            put(o, (const char *) p, 1);
    }
    put(o, "\"", 1);
}

static void jcp(buf_t *o, uint32_t cp)
{
    uint8_t b[5] = {0};
    uint32_t n = i18n_utf8_encode(cp, b);
    if (!n) die("bad digit codepoint", NULL);
    b[n] = 0;
    jstr(o, (const char *) b);
}

/* ---------- the tag lookup the page does (see TABLE.json, map) ---------- */

/* Normalize as i18n_locale_resolve() does before it searches: '_' -> '-',
 * at most 47 bytes, cut at the first single-character subtag (extensions,
 * private use) and drop a trailing one; then ASCII lowercase, since every
 * comparison in i18n_locale.c ignores ASCII case. */
static void norm(const char *tag, char out[48])
{
    uint32_t n = (uint32_t) strlen(tag);
    if (n > 47) n = 47;
    for (uint32_t i = 0; i < n; i++) {
        char c = tag[i] == '_' ? '-' : tag[i];
        out[i] = (c >= 'A' && c <= 'Z') ? (char) (c - 'A' + 'a') : c;
    }
    out[n] = 0;
    for (uint32_t i = 0; i + 2 < n; i++)
        if (out[i] == '-' && out[i + 2] == '-') {
            n = i;
            out[n] = 0;
            break;
        }
    if (n >= 2 && out[n - 2] == '-') out[n - 2] = 0;
}

/* the map: open addressing, string keys -> locale index */
#define MAP_CAP (1u << 16)
static char *map_key[MAP_CAP];
static int map_val[MAP_CAP];
static uint32_t map_n;

static uint32_t hash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (uint8_t) *s) * 16777619u;
    return h;
}

static int map_get(const char *k)
{
    for (uint32_t i = hash(k) & (MAP_CAP - 1);; i = (i + 1) & (MAP_CAP - 1)) {
        if (!map_key[i]) return -1;
        if (!strcmp(map_key[i], k)) return map_val[i];
    }
}

static void map_set(const char *k, int v)
{
    if (map_n * 2 >= MAP_CAP) die("locale map full", NULL);
    for (uint32_t i = hash(k) & (MAP_CAP - 1);; i = (i + 1) & (MAP_CAP - 1)) {
        if (!map_key[i]) {
            map_key[i] = xstrdup(k);
            map_val[i] = v;
            map_n++;
            return;
        }
        if (!strcmp(map_key[i], k)) {
            map_val[i] = v;
            return;
        }
    }
}

static int en_index;

/* What the page's lookup returns for a raw tag. */
static int page_lookup(const char *tag)
{
    char t[48];
    norm(tag, t);
    for (;;) {
        int v = t[0] ? map_get(t) : -1;
        if (v >= 0) return v;
        char *dash = strrchr(t, '-');
        if (!dash) return en_index;
        *dash = 0;
    }
}

static int resolve_index(const char *tag)
{
    return (int) (i18n_locale_resolve(tag) - i18n_locale_at(0));
}

/* ---------- the negotiation domain ---------- */

typedef struct {
    char **v;
    size_t n, cap;
} strs_t;

static void strs_add(strs_t *s, const char *x)
{
    if (s->n == s->cap) {
        size_t c = s->cap ? s->cap * 2 : 1024;
        char **nv = xmalloc(c * sizeof *nv);
        if (s->n) memcpy(nv, s->v, s->n * sizeof *nv);
        free(s->v);
        s->v = nv;
        s->cap = c;
    }
    s->v[s->n++] = xstrdup(x);
}

static int strs_has(const strs_t *s, const char *x)
{
    for (size_t i = 0; i < s->n; i++)
        if (!strcmp(s->v[i], x)) return 1;
    return 0;
}

static void lower_copy(char *d, const char *s, size_t n)
{
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = (s[i] >= 'A' && s[i] <= 'Z') ? (char) (s[i] + 32) : s[i];
    d[i] = 0;
}

/* subtag k (0-based) of a tag, lowercased, or "" */
static void subtag(const char *tag, int k, char out[16])
{
    const char *p = tag;
    for (int i = 0; i < k; i++) {
        p = strpbrk(p, "-_");
        if (!p) {
            out[0] = 0;
            return;
        }
        p++;
    }
    size_t n = strcspn(p, "-_");
    if (n > 15) n = 15;
    lower_copy(out, p, n);
}

static int dashes(const char *s)
{
    int n = 0;
    for (; *s; s++) n += *s == '-';
    return n;
}

static int by_dashes(const void *a, const void *b)
{
    const char *x = *(const char *const *) a, *y = *(const char *const *) b;
    int d = dashes(x) - dashes(y);
    return d ? d : strcmp(x, y);
}

static int is_alpha_n(const char *s, size_t n)
{
    if (strlen(s) != n) return 0;
    for (size_t i = 0; i < n; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z'))) return 0;
    return 1;
}

static int is_digit3(const char *s)
{
    return strlen(s) == 3 && s[0] >= '0' && s[0] <= '9' && s[1] >= '0' && s[1] <= '9' &&
           s[2] >= '0' && s[2] <= '9';
}

/* Builds the map and checks it. The domain: every registry tag and hint key;
 * every language of the registry or the alias table alone, with every
 * script of the registry, and with every region that appears in a registry
 * tag or a hint key (and p-Script-REGION where p-Script resolves to another
 * locale than p; registry p-Script-REGION tags are in the domain anyway);
 * the languages of the hint keys also with every two-letter region AA..ZZ.
 * Any other region can only reach the truncation step of
 * i18n_locale_resolve(), which the page's lookup does the same way. */
static void build_map(strs_t *domain)
{
    uint32_t nl = i18n_locale_count();
    strs_t langs = {0}, scripts = {0}, regions = {0}, all_regions = {0}, hinted = {0};
    char st[16], t[64];
    for (uint32_t i = 0; i < nl; i++) {
        const char *tag = i18n_locale_tag(i18n_locale_at(i));
        lower_copy(t, tag, 63);
        strs_add(domain, t);
        subtag(tag, 0, st);
        if (!strs_has(&langs, st)) strs_add(&langs, st);
        for (int k = 1; k < 4; k++) {
            subtag(tag, k, st);
            if (is_alpha_n(st, 4) && !strs_has(&scripts, st)) strs_add(&scripts, st);
            if ((is_digit3(st) || is_alpha_n(st, 2)) && !strs_has(&regions, st))
                strs_add(&regions, st);
        }
    }
    for (uint32_t i = 0; i < i18n_alias_count; i++) {
        lower_copy(st, i18n_aliases[i].from, 4);
        if (!strs_has(&langs, st)) strs_add(&langs, st);
    }
    for (uint32_t i = 0; i < i18n_hint_count; i++) {
        lower_copy(t, i18n_hints[i].key, 12);
        strs_add(domain, t);
        subtag(t, 0, st);
        if (!strs_has(&hinted, st)) strs_add(&hinted, st);
        for (int k = 1; k < 3; k++) {
            subtag(t, k, st);
            if ((is_digit3(st) || is_alpha_n(st, 2)) && !strs_has(&regions, st))
                strs_add(&regions, st);
        }
    }
    for (size_t i = 0; i < regions.n; i++) strs_add(&all_regions, regions.v[i]);
    for (char a = 'a'; a <= 'z'; a++)
        for (char b = 'a'; b <= 'z'; b++) {
            char r[3] = {a, b, 0};
            if (!strs_has(&regions, r)) strs_add(&all_regions, r);
        }
    for (size_t li = 0; li < langs.n; li++) {
        const char *p = langs.v[li];
        const strs_t *rs = strs_has(&hinted, p) ? &all_regions : &regions;
        strs_add(domain, p);
        for (size_t ri = 0; ri < rs->n; ri++) {
            snprintf(t, sizeof t, "%s-%s", p, rs->v[ri]);
            strs_add(domain, t);
        }
        const i18n_locale_t *base = i18n_locale_resolve(p);
        for (size_t si = 0; si < scripts.n; si++) {
            snprintf(t, sizeof t, "%s-%s", p, scripts.v[si]);
            strs_add(domain, t);
            if (i18n_locale_resolve(t) == base) continue;
            for (size_t ri = 0; ri < rs->n; ri++) {
                snprintf(t, sizeof t, "%s-%s-%s", p, scripts.v[si], rs->v[ri]);
                strs_add(domain, t);
            }
        }
    }
    qsort(domain->v, domain->n, sizeof *domain->v, by_dashes);
    int *want = xmalloc(sizeof(int) * domain->n);
    /* shorter tags first: an entry only changes the lookup of longer tags */
    for (size_t i = 0; i < domain->n; i++) {
        want[i] = resolve_index(domain->v[i]);
        if (page_lookup(domain->v[i]) != want[i]) {
            char n[48];
            norm(domain->v[i], n);
            map_set(n, want[i]);
        }
    }
    for (size_t i = 0; i < domain->n; i++)
        if (page_lookup(domain->v[i]) != want[i])
            die("page lookup differs from i18n_locale_resolve for", domain->v[i]);
    free(want);
    fprintf(stderr, "gen_ui_strings: negotiation checked on %zu tags, map %u entries\n", domain->n,
            map_n);
}

/* ---------- strings ---------- */

static int lang_is_en(const i18n_locale_t *l)
{
    char st[16];
    subtag(i18n_locale_tag(l), 0, st);
    return !strcmp(st, "en");
}

/* ---------- number formats ---------- */

static void affix(buf_t *o, const i18n_numfmt_t *nf, const char *a)
{
    buf_t t = {0};
    puts_(&t, "");
    for (; *a; a++) switch ((uint8_t) *a) {
        case I18N_AFX_CURRENCY:
            break;
        case I18N_AFX_MINUS:
            puts_(&t, POOL(nf->minus));
            break;
        case I18N_AFX_PLUS:
            puts_(&t, POOL(nf->plus));
            break;
        case I18N_AFX_PERCENT:
            puts_(&t, POOL(nf->percent));
            break;
        case I18N_AFX_PERMILLE:
            puts_(&t, "\xE2\x80\xB0");
            break;
        default:
            put(&t, a, 1);
        }
    jstr(o, t.b);
    free(t.b);
}

int main(int argc, char **argv)
{
    if (argc < 4) die("usage: gen_ui_strings TABLE.json SAMPLES.json KEY...", NULL);
    int nk = argc - 3;
    char **keys = argv + 3;
    int *ids = xmalloc(sizeof(int) * (size_t) nk);
    for (int k = 0; k < nk; k++) {
        ids[k] = i18n_msg_id(keys[k]);
        if (ids[k] < 0) die("zxv_ui.html uses a key that is not in catalog/en.msg:", keys[k]);
        for (int j = 0; j < k; j++)
            if (!strcmp(keys[j], keys[k])) die("key listed twice:", keys[k]);
    }
    uint32_t nl = i18n_locale_count();
    const i18n_locale_t *en = i18n_locale_default();
    en_index = (int) (en - i18n_locale_at(0));

    /* per locale: its sparse set of non-English strings */
    char **sets = xmalloc(sizeof(char *) * nl);
    int *loc_set = xmalloc(sizeof(int) * nl), nsets = 0;
    for (uint32_t i = 0; i < nl; i++) {
        const i18n_locale_t *l = i18n_locale_at(i);
        buf_t s = {0};
        puts_(&s, "{");
        int first = 1;
        for (int k = 0; k < nk; k++) {
            i18n_msg_info_t info;
            const char *txt = i18n_msg(l, (i18n_msgid_t) ids[k], &info);
            const char *base = i18n_msg(en, (i18n_msgid_t) ids[k], NULL);
            int from_en = lang_is_en(info.from);
            if (from_en) {
                if (strcmp(txt, base) || info.review != I18N_REVIEW_SOURCE)
                    die("an English variant catalog changes a window string:", keys[k]);
                continue;
            }
            if (!first) puts_(&s, ",");
            first = 0;
            putf(&s, "\"%lld\":[", k);
            jstr(&s, txt);
            putf(&s, ",%lld]", (long long) info.review);
        }
        puts_(&s, "}");
        int found = -1;
        for (int j = 0; j < nsets; j++)
            if (!strcmp(sets[j], s.b)) found = j;
        if (found < 0) sets[found = nsets++] = s.b;
        loc_set[i] = found;
    }

    /* distinct decimal formats */
    int *loc_num = xmalloc(sizeof(int) * nl), *nums = xmalloc(sizeof(int) * nl), nnums = 0;
    for (uint32_t i = 0; i < nl; i++) {
        int f = i18n_locs[i].numfmt, found = -1;
        for (int j = 0; j < nnums; j++)
            if (nums[j] == f) found = j;
        if (found < 0) nums[found = nnums++] = f;
        loc_num[i] = found;
    }

    strs_t domain = {0};
    build_map(&domain);

    buf_t o = {0};
    puts_(&o, "{\"v\":");
    jstr(&o, i18n_data_version());
    puts_(&o, ",\n\"keys\":[");
    for (int k = 0; k < nk; k++) {
        if (k) puts_(&o, ",");
        jstr(&o, keys[k]);
    }
    puts_(&o, "],\n\"en\":[");
    for (int k = 0; k < nk; k++) {
        if (k) puts_(&o, ",\n");
        jstr(&o, i18n_msg(en, (i18n_msgid_t) ids[k], NULL));
    }
    puts_(&o, "],\n\"sets\":[");
    for (int j = 0; j < nsets; j++) {
        if (j) puts_(&o, ",\n");
        puts_(&o, sets[j]);
    }
    puts_(&o, "],\n\"nums\":[");
    for (int j = 0; j < nnums; j++) {
        const i18n_numfmt_t *nf = &i18n_numfmts[nums[j]];
        if (j) puts_(&o, ",\n");
        puts_(&o, "{\"d\":[");
        for (int d = 0; d < 10; d++) {
            if (d) puts_(&o, ",");
            jcp(&o, i18n_digitsets[nf->digitset][d]);
        }
        puts_(&o, "],\"dec\":");
        jstr(&o, POOL(nf->decimal));
        puts_(&o, ",\"grp\":");
        jstr(&o, POOL(nf->group));
        putf(&o, ",\"g1\":%lld", nf->groups[0] & 0x0F);
        putf(&o, ",\"g2\":%lld", nf->groups[0] >> 4);
        putf(&o, ",\"min\":%lld", nf->min_group);
        const char *nm[4] = {"pp", "ps", "np", "ns"};
        for (int a = 0; a < 4; a++) {
            puts_(&o, ",\"");
            puts_(&o, nm[a]);
            puts_(&o, "\":");
            affix(&o, nf, POOL(nf->affix[a]));
        }
        puts_(&o, "}");
    }
    puts_(&o, "],\n\"locs\":[");
    for (uint32_t i = 0; i < nl; i++) {
        const i18n_locale_t *l = i18n_locale_at(i);
        if (i) puts_(&o, ",\n");
        puts_(&o, "[");
        jstr(&o, i18n_locale_tag(l));
        puts_(&o, ",");
        jstr(&o, i18n_locale_autonym(l));
        putf(&o, ",%lld", i18n_locale_rtl(l) ? 1 : 0);
        putf(&o, ",%lld", loc_set[i]);
        putf(&o, ",%lld", loc_num[i]);
        putf(&o, ",%lld]", lang_is_en(l) ? 0 : 1);
    }
    putf(&o, "],\n\"def\":%lld,\n\"map\":{", en_index);
    int first = 1;
    for (uint32_t i = 0; i < MAP_CAP; i++) {
        if (!map_key[i]) continue;
        if (!first) puts_(&o, ",");
        first = 0;
        jstr(&o, map_key[i]);
        putf(&o, ":%lld", map_val[i]);
    }
    puts_(&o, "}}\n");

    FILE *f = fopen(argv[1], "wb");
    if (!f || fwrite(o.b, 1, o.n, f) != o.n || fclose(f)) die("cannot write", argv[1]);

    /* ---------- samples for the page's script ---------- */
    buf_t s = {0};
    char out[256];
    puts_(&s, "{\"negotiate\":[");
    static const char *const fixed[] = {"",
                                        "en",
                                        "EN_us",
                                        "en-US-u-ca-gregory",
                                        "en-x-private",
                                        "de",
                                        "de-AT",
                                        "de-CH-1996",
                                        "zh-TW",
                                        "zh-HK",
                                        "zh-Hans-CN",
                                        "zh_CN",
                                        "sr-ME",
                                        "sr-Latn-RS",
                                        "iw",
                                        "iw-IL",
                                        "tl",
                                        "tl-PH",
                                        "no",
                                        "pt-AO",
                                        "pt-BR",
                                        "es-419",
                                        "es-MX",
                                        "ar-EG",
                                        "fa-IR",
                                        "he",
                                        "ur-PK",
                                        "pa-PK",
                                        "ja-JP",
                                        "sw-KE",
                                        "xx",
                                        "x-klingon",
                                        "qaa-XX",
                                        "ar-u-nu-latn"};
    int n = 0;
    for (size_t i = 0; i < sizeof fixed / sizeof *fixed; i++, n++) {
        if (n) puts_(&s, ",");
        puts_(&s, "[");
        jstr(&s, fixed[i]);
        puts_(&s, ",");
        jstr(&s, i18n_locale_tag(i18n_locale_resolve(fixed[i])));
        puts_(&s, "]");
    }
    for (size_t i = 0; i < domain.n; i += 211) {
        puts_(&s, ",[");
        jstr(&s, domain.v[i]);
        puts_(&s, ",");
        jstr(&s, i18n_locale_tag(i18n_locale_resolve(domain.v[i])));
        puts_(&s, "]");
    }
    /* integers as strings: the page formats 64-bit values through BigInt */
    static const char *const ints[] = {"0",
                                       "7",
                                       "-7",
                                       "999",
                                       "1000",
                                       "12345",
                                       "123456",
                                       "1234567",
                                       "-1234567890",
                                       "9007199254740993",
                                       "9223372036854775807"};
    static const long long intv[] = {0,
                                     7,
                                     -7,
                                     999,
                                     1000,
                                     12345,
                                     123456,
                                     1234567,
                                     -1234567890LL,
                                     9007199254740993LL,
                                     9223372036854775807LL};
    static const long long decs[][2] = {{0, 2}, {5, 2}, {123456, 2}, {-1234567, 3}, {1, 0}};
    puts_(&s, "],\n\"num\":[");
    n = 0;
    for (int j = 0; j < nnums; j++) {
        const i18n_locale_t *l = NULL;
        for (uint32_t i = 0; i < nl && !l; i++)
            if (loc_num[i] == j) l = i18n_locale_at(i);
        for (size_t v = 0; v < sizeof ints / sizeof *ints; v++, n++) {
            if (i18n_fmt_int(l, intv[v], out, sizeof out) < 0) die("fmt_int overflow", NULL);
            if (n) puts_(&s, ",");
            puts_(&s, "[");
            jstr(&s, i18n_locale_tag(l));
            puts_(&s, ",");
            jstr(&s, ints[v]);
            puts_(&s, ",");
            jstr(&s, out);
            puts_(&s, "]");
        }
    }
    puts_(&s, "],\n\"dec\":[");
    n = 0;
    for (int j = 0; j < nnums; j++) {
        const i18n_locale_t *l = NULL;
        for (uint32_t i = 0; i < nl && !l; i++)
            if (loc_num[i] == j) l = i18n_locale_at(i);
        for (size_t v = 0; v < sizeof decs / sizeof *decs; v++, n++) {
            if (i18n_fmt_decimal(l, decs[v][0], (uint32_t) decs[v][1], out, sizeof out) < 0)
                die("fmt_decimal overflow", NULL);
            if (n) puts_(&s, ",");
            puts_(&s, "[");
            jstr(&s, i18n_locale_tag(l));
            putf(&s, ",%lld", decs[v][0]);
            putf(&s, ",%lld,", decs[v][1]);
            jstr(&s, out);
            puts_(&s, "]");
        }
    }
    puts_(&s, "],\n\"fmt\":[");
    static const char *const ftags[] = {"en", "ar", "he", "de"};
    static const char *const args[][2] = {
        {"1,234", "ZXV"}, {"\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D", "7"}, {"", "x"}};
    static const char *const tmpls[] = {"{0} of {1} tokens", "{1} ({0}) {2} {", "plain"};
    n = 0;
    for (size_t t = 0; t < sizeof ftags / sizeof *ftags; t++)
        for (size_t a = 0; a < sizeof args / sizeof *args; a++)
            for (size_t m = 0; m < sizeof tmpls / sizeof *tmpls; m++, n++) {
                const i18n_locale_t *l = i18n_locale_find(ftags[t]);
                if (i18n_format_msg(l, tmpls[m], args[a], 2, out, sizeof out) < 0)
                    die("format_msg overflow", NULL);
                if (n) puts_(&s, ",");
                puts_(&s, "[");
                jstr(&s, ftags[t]);
                puts_(&s, ",");
                jstr(&s, tmpls[m]);
                puts_(&s, ",[");
                jstr(&s, args[a][0]);
                puts_(&s, ",");
                jstr(&s, args[a][1]);
                puts_(&s, "],");
                jstr(&s, out);
                puts_(&s, "]");
            }
    puts_(&s, "]}\n");
    f = fopen(argv[2], "wb");
    if (!f || fwrite(s.b, 1, s.n, f) != s.n || fclose(f)) die("cannot write", argv[2]);
    fprintf(stderr, "gen_ui_strings: %d keys, %d string sets, %d number formats, %u locales\n", nk,
            nsets, nnums, nl);
    return 0;
}
