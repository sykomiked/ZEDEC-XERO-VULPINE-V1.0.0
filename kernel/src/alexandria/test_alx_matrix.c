/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_alx_matrix.c — content-defined chunking (alx_cdc.h), and the matrix
 * pack_matrix.py wrote (alx_matrix.h): every node is walked and checked,
 * every file is rebuilt from its chunks and compared with the repository,
 * and every cut is made again here.
 * argv[1]: pack_matrix.py's --out-dir; argv[2]: the repository root. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alx_cdc.h"
#include "alx_z.h"
#include "alx_matrix.h"
#include "../robin_debanks/sha256.h"

static unsigned checks, fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("[FAIL] %s:%d: ", __FILE__, __LINE__);                                          \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static const char *DIR_;

static void cid_str(const uint8_t c[36], char out[64])
{
    static const char A[] = "abcdefghijklmnopqrstuvwxyz234567";
    uint32_t acc = 0, bits = 0, k = 0;
    out[k++] = 'b';
    for (int i = 0; i < 36; i++) {
        acc = acc << 8 | c[i];
        bits += 8;
        while (bits >= 5) out[k++] = A[(acc >> (bits -= 5)) & 31];
    }
    if (bits) out[k++] = A[(acc << (5 - bits)) & 31];
    out[k] = 0;
}

static uint8_t *slurp(const char *path, uint32_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(len ? (size_t) len : 1u);
    *n = (uint32_t) fread(b, 1, (size_t) len, f);
    fclose(f);
    return b;
}

static uint32_t ZBOOK;
static uint64_t ZSTORED, ZPLAIN;

static uint8_t *block(const uint8_t cid[36], uint32_t *n)
{
    char name[64], path[2048];
    cid_str(cid, name);
    snprintf(path, sizeof path, "%s/blocks/%s", DIR_, name);
    uint8_t *b = slurp(path, n);
    if (!b) {
        printf("[FAIL] missing block %s\n", name);
        exit(1);
    }
    return b;
}

/* ---- chunking ---- */

static uint32_t rnd(uint64_t *s)
{
    *s = *s * 6364136223846793005ull + 1442695040888963407ull;
    return (uint32_t) (*s >> 33);
}

/* text-like bytes: words from a small vocabulary, so runs repeat */
static void fill(uint8_t *b, uint32_t n, uint64_t seed)
{
    static const char *Wd[] = {"int ", "return ",   "x",   " = ", "(a, b)",     ";\n", "for ",
                               "if ",  "uint32_t ", "{\n", "}\n", "/* note */", "k++", "0x7f"};
    uint64_t s = seed;
    for (uint32_t i = 0; i < n;) {
        const char *w = Wd[rnd(&s) % 14u];
        while (*w && i < n) b[i++] = (uint8_t) *w++;
        if (i < n && rnd(&s) % 7u == 0) b[i++] = (uint8_t) ('a' + rnd(&s) % 26u);
    }
}

static uint32_t cuts(const uint8_t *b, uint32_t n, const alx_cdc_tier_t *T, uint32_t *at,
                     uint32_t cap)
{
    uint32_t k = 0;
    for (uint32_t o = 0; o < n && k < cap;) {
        uint32_t c = alx_cdc_cut_with(b + o, n - o, T);
        if (!c) break;
        o += c;
        at[k++] = o;
    }
    return k;
}

static void test_cdc(void)
{
    CHECK(alx_cdc_tier(3) == 0 && alx_cdc_cut((const uint8_t *) "x", 1, 3) == 0, "tier 3");
    CHECK(alx_cdc_cut(0, 5, 0) == 0 && alx_cdc_cut((const uint8_t *) "x", 0, 0) == 0, "empty");
    /* splitmix64(1): the gear table is the documented one */
    CHECK(alx_cdc_gear(0) == 0xe220a8397b1dcdafull, "gear 0: %016llx",
          (unsigned long long) alx_cdc_gear(0));
    for (uint32_t i = 0; i < 256; i++)
        for (uint32_t j = 0; j < i; j++)
            if (alx_cdc_gear((uint8_t) i) == alx_cdc_gear((uint8_t) j))
                CHECK(0, "gear %u = gear %u", i, j);
    static uint8_t b[4u << 20], c[(4u << 20) + 1];
    static uint32_t at[8192], at2[8192];
    fill(b, sizeof b, 7);
    /* the three tiers, then tiers of any size: Fibonacci ones included */
    static const uint32_t FIBT[][3] = {
        {1597, 2584, 6765}, {987, 4181, 17711}, {4181, 17711, 75025}, {75025, 317811, 832040}};
    for (uint32_t t = 0; t < 7; t++) {
        alx_cdc_tier_t mk;
        const alx_cdc_tier_t *T = t < 3 ? alx_cdc_tier(t) : &mk;
        if (t >= 3) CHECK(alx_cdc_make(&mk, FIBT[t - 3][0], FIBT[t - 3][1], FIBT[t - 3][2]) == 0, "make %u", t);
        CHECK(T->min < T->avg && T->avg < T->max && T->max <= ALX_BOOK_MAX - 128u, "tier %u", t);
        uint32_t n = cuts(b, sizeof b, T, at, 8192), prev = 0, small = 0;
        CHECK(n > 0 && at[n - 1] == sizeof b, "tier %u covers the buffer", t);
        uint64_t sum = 0;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t l = at[i] - prev;
            if (i + 1 < n) {
                CHECK(l > T->min && l <= T->max, "tier %u chunk %u is %u", t, i, l);
                small += l < T->avg;
            }
            sum += l;
            prev = at[i];
        }
        /* normalised: the mean sits near the average, never at either bound */
        uint64_t mean = sum / n;
        CHECK(mean > T->avg / 2u && mean < T->avg * 2u, "tier %u mean %llu", t,
              (unsigned long long) mean);
        CHECK(alx_cdc_cut_with(b, T->min, T) == T->min &&
                  alx_cdc_cut_with(b, T->min - 1u, T) == T->min - 1u,
              "tier %u: a short tail is one chunk", t);
        CHECK(alx_cdc_cut_with(b, T->min + 1u, T) <= T->min + 1u, "tier %u: never past the end", t);
        /* one byte inserted at the front moves only the first chunks */
        c[0] = '#';
        memcpy(c + 1, b, sizeof b);
        uint32_t m = cuts(c, sizeof b + 1u, T, at2, 8192), same = 0, j = 0;
        for (uint32_t i = 0; i < n; i++) {
            while (j < m && at2[j] < at[i] + 1u) j++;
            same += j < m && at2[j] == at[i] + 1u;
        }
        CHECK(same + 3u >= n, "tier %u: after an insert %u of %u cuts stay", t, same, n);
        /* deterministic */
        uint32_t again = cuts(b, sizeof b, T, at2, 8192);
        CHECK(again == n && !memcmp(at, at2, n * sizeof at[0]), "tier %u is deterministic", t);
    }
    /* a made tier of the built-in sizes is the built-in tier */
    for (uint32_t t = 0; t < 3; t++) {
        const alx_cdc_tier_t *T = alx_cdc_tier(t);
        alx_cdc_tier_t mk;
        CHECK(alx_cdc_make(&mk, T->min, T->avg, T->max) == 0 && mk.hard == T->hard &&
                  mk.easy == T->easy,
              "tier %u made", t);
    }
    for (uint32_t i = 0; i < 64; i++) { /* the mask form, over random hashes */
        uint64_t s = 0x243F6A8885A308D3ull + i, h = (uint64_t) rnd(&s) << 32;
        h |= rnd(&s);
        /* for powers of two the threshold rule is the mask rule */
        for (uint32_t t = 0; t < 3; t++) {
            alx_cdc_tier_t P;
            const alx_cdc_tier_t *T = &P;
            alx_cdc_make(&P, 1u << (10 + 3 * t), 1u << (12 + 3 * t), 1u << (14 + 3 * t));
            uint32_t k = 2u;
            for (uint32_t a = T->avg; a > 1u; a >>= 1) k++;
            uint64_t hard = ~0ull << (64 - k), easy = ~0ull << (64 - (k - 4u));
            for (uint32_t z = 0; z < 2; z++) { /* and with the top bits cleared */
                uint64_t x = z ? h & ~(~0ull << (64 - k)) : h;
                CHECK(((uint32_t) (x >> 32) < T->hard) == !(x & hard) &&
                          ((uint32_t) (x >> 32) < T->easy) == !(x & easy),
                      "tier %u threshold is the mask", t);
            }
        }
    }
    {
        alx_cdc_tier_t mk = {1, 2, 3, 4, 5};
        CHECK(alx_cdc_make(&mk, 63, 100, 200) < 0 && alx_cdc_make(&mk, 100, 100, 200) < 0 &&
                  alx_cdc_make(&mk, 100, 200, 200) < 0 &&
                  alx_cdc_make(&mk, 100, 200, ALX_CDC_MAXLEN + 1u) < 0 && alx_cdc_make(0, 64, 65, 66) < 0 &&
                  mk.min == 1 && mk.easy == 5,
              "make refuses, and leaves the tier");
        CHECK(alx_cdc_make(&mk, 64, 65, ALX_CDC_MAXLEN) == 0 && mk.hard > 0 && mk.easy > mk.hard,
              "make at the bounds");
        CHECK(alx_cdc_cut_with((const uint8_t *) "x", 1, 0) == 0, "no tier");
    }
    /* a run of one byte never cuts early: the maximum holds */
    static uint8_t z[1u << 20];
    CHECK(alx_cdc_cut(z, sizeof z, 0) == alx_cdc_tier(0)->max ||
              alx_cdc_cut(z, sizeof z, 0) > alx_cdc_tier(0)->min,
          "zeros");
    CHECK(alx_cdc_cut(z, sizeof z, 2) <= alx_cdc_tier(2)->max, "cold maximum");
}

/* ---- the packed matrix ---- */

static uint8_t *ROOT;
static uint32_t ROOTN;
static uint8_t **BOOK[ALX_MX_LAYERS];
static uint32_t *BOOKN[ALX_MX_LAYERS], NB[ALX_MX_LAYERS];
static uint8_t *GLUE[ALX_MX_LAYERS];
static uint32_t GLUEN[ALX_MX_LAYERS];

/* walk layer k's tree to booklet i, checking every node on the way */
static uint8_t *booklet_at(uint32_t k, uint32_t i, uint32_t *n)
{
    if (BOOK[k][i]) {
        *n = BOOKN[k][i];
        return BOOK[k][i];
    }
    alx_mx_layer_t l;
    alx_mx_layer(ROOT, k, &l);
    uint32_t depth = alx_mx_depth(l.booklets), len;
    const uint8_t *cid = l.root;
    uint8_t *node = 0;
    for (uint32_t lv = depth; lv-- > 0;) {
        uint32_t level, kids;
        uint64_t under;
        node = block(cid, &len);
        CHECK(alx_node_check(node, len, &level, &kids, &under) == ALX_OK && level == lv,
              "layer %u node level %u", k, lv);
        CHECK(lv + 1u != depth || under == l.booklets, "layer %u counts %llu booklets", k,
              (unsigned long long) under);
        CHECK(alx_mx_digit(i, lv) < kids, "layer %u booklet %u digit", k, i);
        static uint8_t c[36];
        memcpy(c, alx_node_child(node, alx_mx_digit(i, lv)), 36);
        free(node);
        cid = c;
    }
    uint8_t *b = block(cid, &len);
    uint8_t h[36];
    h[0] = 1, h[1] = 0x55, h[2] = 0x12, h[3] = 0x20;
    sha256(b, len, h + 4);
    CHECK(!memcmp(h, cid, 36), "booklet %u.%u has the CID its node names", k, i);
    /* a compressed booklet: open it, and its bytes are the ones its plain CID names */
    if (alx_z_is(b, len)) {
        uint32_t pl = alx_z_plain_len(b, len);
        uint8_t *p = malloc(pl ? pl : 1u);
        int32_t r = alx_z_open(b, len, p, pl);
        CHECK(r == (int32_t) pl && pl > len, "booklet %u.%u opens (%d)", k, i, r);
        ZBOOK++, ZSTORED += len, ZPLAIN += pl;
        free(b);
        b = p;
        len = pl;
    } else {
        ZSTORED += len, ZPLAIN += len;
    }
    uint32_t lay, atoms;
    CHECK(alx_bk2_check(b, len, &lay, &atoms) == ALX_OK && lay == k, "layer %u booklet %u", k, i);
    BOOK[k][i] = b;
    BOOKN[k][i] = len;
    *n = len;
    return b;
}

static void test_layout(void)
{
    char path[2048];
    snprintf(path, sizeof path, "%s/MATRIX.ROOT", DIR_);
    ROOT = slurp(path, &ROOTN);
    CHECK(ROOT && alx_mx_check(ROOT, ROOTN) == ALX_OK, "the root");
    uint32_t used = 0, icons = 0;
    for (uint32_t k = 0; k < ALX_MX_LAYERS; k++) {
        alx_mx_layer_t l;
        alx_mx_layer(ROOT, k, &l);
        NB[k] = l.booklets;
        BOOK[k] = calloc(l.booklets + 1u, sizeof *BOOK[k]);
        BOOKN[k] = calloc(l.booklets + 1u, sizeof *BOOKN[k]);
        icons += k && l.icon[0];
        /* every layer has its glue; N exactly when it holds nothing */
        GLUE[k] = block(l.glue, &GLUEN[k]);
        uint32_t lay, nf, ne, nr;
        CHECK(alx_glue_check(GLUE[k], GLUEN[k], &lay, &nf, &ne, &nr) == ALX_OK && lay == k,
              "layer %u glue", k);
        CHECK(alx_glue_state(GLUE[k]) == (l.booklets ? 1 : 0), "layer %u's state", k);
        if (!l.booklets) {
            CHECK(nf == 0 && ne == 0 && nr == 0, "scaffold layer %u carries nothing", k);
            continue;
        }
        used++;
        for (uint32_t i = 0; i < l.booklets; i++) {
            uint32_t n;
            booklet_at(k, i, &n);
        }
    }
    CHECK(used > 1 && NB[0] > 0, "the core and some apps hold content (%u layers)", used);
    printf("matrix: %u layers hold content, %u icons\n", used, icons);
}

/* rebuild every file from its refs and cut it again */
static void test_files(const char *repo)
{
    char path[4096], line[4096];
    snprintf(path, sizeof path, "%s/FILES.tsv", DIR_);
    FILE *f = fopen(path, "r");
    CHECK(f != 0, "FILES.tsv");
    if (!f) return;
    uint32_t files = 0, refs = 0, prefetched = 0, boot = 0;
    while (fgets(line, sizeof line, f)) {
        char *p = strtok(line, "\t"), *ls = strtok(0, "\t"), *ts = strtok(0, "\t");
        if (!p || !ls || !ts) continue;
        uint32_t k = (uint32_t) atoi(ls), tier = (uint32_t) atoi(ts), n;
        snprintf(path, sizeof path, "%s/%s", repo, p);
        uint8_t *d = slurp(path, &n);
        CHECK(d != 0, "%s", p);
        if (!d) continue;
        uint8_t cid[36];
        cid[0] = 1, cid[1] = 0x55, cid[2] = 0x12, cid[3] = 0x20;
        sha256(d, n, cid + 4);
        int32_t fi = alx_glue_find(GLUE[k], cid);
        CHECK(fi >= 0, "%s is in layer %u's glue", p, k);
        if (fi < 0) {
            free(d);
            continue;
        }
        uint32_t at = 0, nr = alx_glue_nrefs(GLUE[k], (uint32_t) fi), ok = 1;
        for (uint32_t i = 0; i < nr && ok; i++) {
            uint32_t l, bk, a, bn, off, len;
            alx_glue_ref(GLUE[k], (uint32_t) fi, i, &l, &bk, &a);
            ok = l < ALX_MX_LAYERS && bk < NB[l];
            CHECK(ok, "%s ref %u names booklet %u.%u", p, i, l, bk);
            if (!ok) break;
            /* booklet 000, the alphabet and its Dhātu view, is the core's first */
            if (!strcmp(p, "kernel/src/lingua/alphabet.tsv") ||
                !strcmp(p, "kernel/src/lingua/zl_proj.c") || !strcmp(p, "kernel/src/lingua/zl_sutra.c")) {
                CHECK(l == 0 && bk == 0, "%s is in booklet 000, not %u.%u", p, l, bk);
                boot++;
            }
            uint8_t *b = booklet_at(l, bk, &bn);
            uint32_t atoms = 0;
            alx_bk2_check(b, bn, 0, &atoms);
            ok = a < atoms;
            CHECK(ok, "%s ref %u atom %u of %u", p, i, a, atoms);
            if (!ok) break;
            alx_bk2_atom(b, a, 0, &off, &len);
            /* the kernel's own cut gives the same chunk */
            ok = at + len <= n && alx_cdc_cut(d + at, n - at, tier) == len &&
                 !memcmp(b + off, d + at, len);
            CHECK(ok, "%s chunk %u at %u", p, i, at);
            at += len;
            refs++;
        }
        CHECK(!ok || at == n, "%s rebuilt to %u of %u bytes", p, at, n);
        uint32_t out[8], other, w = alx_glue_prefetch(GLUE[k], (uint32_t) fi, out, 8, &other);
        CHECK(w <= 8, "prefetch cap");
        for (uint32_t i = 0; i < w; i++) {
            CHECK(out[i] < NB[k], "%s prefetches booklet %u of %u", p, out[i], NB[k]);
            for (uint32_t j = 0; j < i; j++) CHECK(out[i] != out[j], "%s prefetch repeats", p);
            for (uint32_t r = 0; r < nr; r++) {
                uint32_t l, bk;
                alx_glue_ref(GLUE[k], (uint32_t) fi, r, &l, &bk, 0);
                CHECK(l != k || bk != out[i], "%s prefetches its own booklet", p);
            }
        }
        prefetched += w > 0;
        files++;
        free(d);
    }
    fclose(f);
    CHECK(files > 100 && prefetched > 10, "%u files, %u with prefetch", files, prefetched);
    CHECK(boot >= 2, "booklet 000 checked (%u chunks)", boot);
    printf("matrix: %u files rebuilt from %u chunk refs\n", files, refs);
}

/* each refusal on a copy of real nodes */
/* the spine and the frame (MATRIX.md sections 9 and 10) */
static int64_t ZPOS[ALX_MX_LAYERS][16];

static void zeck_unique(void)
{
    /* Zeckendorf: every n has exactly one sum of non-consecutive Fibonacci
     * numbers (from 1, 2, 3, 5 ...); the split takes its largest part */
    static const uint32_t fib[] = {1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233};
    static uint8_t ways[400];
    memset(ways, 0, sizeof ways);
    for (uint32_t set = 0; set < (1u << 12); set++) {
        if (set & (set >> 1)) continue; /* two consecutive */
        uint32_t sum = 0;
        for (uint32_t i = 0; i < 12; i++)
            if (set >> i & 1) sum += fib[i];
        if (sum < sizeof ways && ways[sum] < 255) ways[sum]++;
    }
    for (uint32_t n = 1; n < 377; n++) {
        CHECK(ways[n] == 1, "%u has one Zeckendorf sum (%u)", n, ways[n]);
        uint32_t a, m;
        alx_mx_split(n, &a, &m);
        uint32_t big = 0;
        for (uint32_t i = 0; i < 12; i++)
            if (fib[i] <= n) big = fib[i];
        if (alx_mx_anchor(n) >= 0 && n < ALX_MX_LAYERS)
            CHECK(m == 0 && a == alx_mx_anchor_below(n), "anchor %u hangs from the anchor below", n);
        else
            CHECK(a == big && m == n - big && (m == 0 || m < n - m), "%u = %u + %u", n, a, m);
    }
}

/* the golden lineage table (docs/spec/ZECKENDORF_ANCESTORS.tsv): the one
 * the packer writes and the fabricator checks; the kernel's rule must say
 * the same for every pair of layers */
static void lineage_table(const char *repo)
{
    char path[2048], line[1024];
    snprintf(path, sizeof path, "%s/docs/spec/ZECKENDORF_ANCESTORS.tsv", repo);
    FILE *f = fopen(path, "r");
    CHECK(f != 0, "%s", path);
    if (!f) return;
    uint32_t rows = 0, cells = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        unsigned k, a, m;
        char fl[8];
        int at = 0;
        if (sscanf(line, "%u\t%7s\t%u\t%u\t%n", &k, fl, &a, &m, &at) != 4 || k >= ALX_MX_LAYERS) {
            CHECK(0, "a lineage row: %s", line);
            break;
        }
        uint32_t wa, wm;
        alx_mx_split(k, &wa, &wm);
        CHECK(k == rows && wa == a && wm == m && (fl[0] == 'F') == (alx_mx_anchor(k) >= 0),
              "row %u: anchor %u, pattern %u", k, a, m);
        uint8_t in[ALX_MX_LAYERS] = {0};
        const char *c = line + at;
        while (*c >= '0' && *c <= '9') {
            unsigned l;
            int used = 0;
            if (sscanf(c, "%u%n", &l, &used) != 1 || l >= ALX_MX_LAYERS) break;
            in[l] = 1;
            c += used;
            while (*c == ' ') c++;
        }
        for (uint32_t l = 0; l < ALX_MX_LAYERS; l++, cells++)
            CHECK(alx_mx_draws(k, l) == (in[l] != 0), "layer %u may draw on %u: %s", k, l,
                  in[l] ? "yes" : "no");
        rows++;
    }
    fclose(f);
    CHECK(rows == ALX_MX_LAYERS, "the table has every layer (%u)", rows);
    printf("matrix: the kernel's lineage rule agrees with ZECKENDORF_ANCESTORS.tsv on %u pairs\n", cells);
}

static void test_spine(const char *dir, const char *repo)
{
    lineage_table(repo);
    static const int idx[ALX_MX_LAYERS + 1] = {[0] = 0, [1] = 1, [2] = 3, [3] = 4, [5] = 5, [8] = 6,
                                               [13] = 7, [21] = 8, [34] = 9, [55] = 10, [89] = 11,
                                               [144] = 12};
    uint32_t anchors = 0;
    for (uint32_t k = 0; k <= ALX_MX_LAYERS; k++) {
        int want = (k == 0 || idx[k]) && k < ALX_MX_LAYERS ? idx[k] : -1;
        CHECK(alx_mx_anchor(k) == want, "layer %u is F%d", k, want);
        anchors += want >= 0;
        uint32_t below = 0;
        for (uint32_t j = 0; j < k && j < ALX_MX_LAYERS; j++)
            if (j == 0 || idx[j]) below = j;
        CHECK(alx_mx_anchor_below(k) == below, "layer %u's anchor below is %u", k, below);
    }
    CHECK(anchors == 12, "twelve anchors (%u)", anchors);
    zeck_unique();

    /* positions: exact in Z[phi], one per layer, the kernel's rule equal to
     * the packer's (SPINE.tsv) */
    char path[2048];
    snprintf(path, sizeof path, "%s/SPINE.tsv", dir);
    FILE *f = fopen(path, "r");
    CHECK(f != 0, "the packer wrote SPINE.tsv");
    uint32_t rows = 0;
    for (uint32_t k = 0; k < ALX_MX_LAYERS; k++) {
        CHECK(alx_mx_pos(k, ZPOS[k]) == ALX_OK, "layer %u has a position", k);
        for (uint32_t j = 0; j < k; j++)
            CHECK(memcmp(ZPOS[j], ZPOS[k], sizeof ZPOS[k]) != 0, "layers %u and %u apart", j, k);
        uint32_t a, m;
        alx_mx_split(k, &a, &m);
        if (alx_mx_anchor(k) >= 0) {
            /* a corner of the + tetrahedron: an even number of minus signs,
             * so the vertex over the scale is a doubled E8 root */
            uint32_t minus = 0;
            for (uint32_t j = 0; j < 8; j++) minus += ZPOS[k][2 * j] < 0 || ZPOS[k][2 * j + 1] < 0;
            CHECK(minus % 2 == 0, "anchor %u sits on an even corner", k);
        } else {
            /* the recursion, checked from outside: phi (vertex - pos) = pos(M) */
            int64_t v[16];
            alx_mx_pos(a, v);
            for (uint32_t j = 0; j < 8; j++) {
                int64_t p = v[2 * j] - ZPOS[k][2 * j], q = v[2 * j + 1] - ZPOS[k][2 * j + 1];
                CHECK(q == ZPOS[m][2 * j] && p + q == ZPOS[m][2 * j + 1],
                      "layer %u: (vertex %u - pos) phi = pos(%u)", k, a, m);
            }
        }
        if (!f) continue;
        char line[1024];
        if (!fgets(line, sizeof line, f)) break;
        unsigned lk, la, lm, ls;
        char fl[8], st[4];
        int at = 0;
        if (sscanf(line, "%u\t%7s\t%u\t%u\t%3s\t%u\t%n", &lk, fl, &la, &lm, st, &ls, &at) != 6) break;
        int ok = lk == k && la == a && lm == m && ls < ALX_MX_LAYERS && alx_mx_fill(ls) == k;
        const char *c = line + at;
        for (uint32_t j = 0; j < 16 && ok; j += 2) {
            long long p, q;
            int used = 0;
            ok = sscanf(c, "%lld:%lld%n", &p, &q, &used) == 2 && p == ZPOS[k][j] && q == ZPOS[k][j + 1];
            c += used;
        }
        alx_mx_layer_t l;
        alx_mx_layer(ROOT, k, &l);
        CHECK(ok && (st[0] == 'T') == (l.booklets != 0), "SPINE.tsv row %u matches the kernel's rule", k);
        rows++;
    }
    if (f) fclose(f);
    CHECK(rows == ALX_MX_LAYERS, "SPINE.tsv has every layer (%u)", rows);

    /* the two half-cycles: the fill (core out) is a permutation with every
     * layer after its whole lineage, starting at the core and taking each
     * star's anchors before its bands; the gather is its reverse */
    uint32_t step[ALX_MX_LAYERS];
    uint8_t seen[ALX_MX_LAYERS] = {0};
    for (uint32_t i = 0; i < ALX_MX_LAYERS; i++) {
        uint32_t k = alx_mx_fill(i);
        CHECK(k < ALX_MX_LAYERS && !seen[k], "fill step %u is a new layer (%u)", i, k);
        if (k >= ALX_MX_LAYERS) return;
        seen[k] = 1;
        step[k] = i;
        CHECK(alx_mx_gather(ALX_MX_LAYERS - 1u - i) == k, "the gather reverses step %u", i);
    }
    CHECK(alx_mx_fill(0) == 0 && alx_mx_gather(ALX_MX_LAYERS - 1u) == 0, "both half-cycles meet at the core");
    CHECK(alx_mx_fill(ALX_MX_LAYERS) == ALX_MX_LAYERS && alx_mx_gather(ALX_MX_LAYERS) == ALX_MX_LAYERS,
          "past the end");
    CHECK(alx_mx_fill(4) == 5 && alx_mx_fill(8) == 4, "star 1's anchors come before its bands");
    for (uint32_t k = 0; k < ALX_MX_LAYERS; k++)
        for (uint32_t l = 0; l < ALX_MX_LAYERS; l++)
            if (alx_mx_draws(k, l)) CHECK(step[l] < step[k], "layer %u is filled after %u", k, l);

    /* the glue: hung from its anchor and its pattern, refs in the lineage */
    uint32_t hung = 0, refs_checked = 0, scaffold = 0;
    for (uint32_t k = 0; k < ALX_MX_LAYERS; k++) {
        alx_mx_layer_t self;
        alx_mx_layer(ROOT, k, &self);
        scaffold += !self.booklets;
        uint32_t up = 99, pat = 99, nf = 0, ne = 0, a, m;
        alx_mx_split(k, &a, &m);
        const uint8_t *cid = alx_glue_anchor(GLUE[k], &up);
        const uint8_t *pc = alx_glue_pattern(GLUE[k], GLUEN[k], &pat);
        if (k == 0) {
            CHECK(!cid && up == 0 && !pc, "the core hangs from nothing");
            continue;
        }
        alx_mx_layer_t l, lp;
        alx_mx_layer(ROOT, a, &l);
        alx_mx_layer(ROOT, m, &lp);
        CHECK(up == a && cid && memcmp(cid, l.glue, 36) == 0, "layer %u hangs from %u's glue", k, a);
        CHECK(pat == m && (m ? pc && memcmp(pc, lp.glue, 36) == 0 : !pc),
              "layer %u repeats %u's glue", k, m);
        hung++;
        uint32_t nr = 0;
        alx_glue_check(GLUE[k], GLUEN[k], 0, &nf, &ne, &nr);
        for (uint32_t fi = 0; fi < nf; fi++)
            for (uint32_t i = 0; i < alx_glue_nrefs(GLUE[k], fi); i++) {
                uint32_t lay, bk, at;
                alx_glue_ref(GLUE[k], fi, i, &lay, &bk, &at);
                CHECK(lay == k || alx_mx_draws(k, lay), "layer %u's chunk is in %u, its lineage", k, lay);
                refs_checked++;
            }
    }
    printf("matrix: %u layers hang from the spine, %u are scaffold (N), %u chunk refs all in "
           "their lineage\n", hung, scaffold, refs_checked);

    /* refusals */
    uint32_t k = 4; /* a branch: 3 + 1 */
    uint8_t *g = malloc(GLUEN[k] + 64u);
    memcpy(g, GLUE[k], GLUEN[k]);
    g[4] = 2;
    CHECK(alx_glue_check(g, GLUEN[k], 0, 0, 0, 0) == ALX_E_FORM, "version 2 glue is the old format");
    memcpy(g, GLUE[k], GLUEN[k]);
    g[57] = 2; /* hangs from 2, not 3 */
    CHECK(alx_glue_check(g, GLUEN[k], 0, 0, 0, 0) == ALX_E_FORM, "hanging from the wrong anchor");
    memcpy(g, GLUE[k], GLUEN[k]);
    g[59] = 0; /* no pattern */
    CHECK(alx_glue_check(g, GLUEN[k], 0, 0, 0, 0) == ALX_E_FORM, "a branch without its pattern");
    memcpy(g, GLUE[k], GLUEN[k]);
    g[61] = 1;
    CHECK(alx_glue_check(g, GLUEN[k], 0, 0, 0, 0) == ALX_E_FORM, "reserved bytes set");
    memcpy(g, GLUE[k], GLUEN[k]);
    g[5] = 3; /* B is not a glue state */
    CHECK(alx_glue_check(g, GLUEN[k], 0, 0, 0, 0) == ALX_E_FORM, "a state other than N or T");
    free(g);
    /* N never carries content: a holding layer marked N, and a ref out of the lineage */
    for (k = 1; k < ALX_MX_LAYERS; k++) {
        uint32_t nf;
        if (alx_glue_check(GLUE[k], GLUEN[k], 0, &nf, 0, 0) != ALX_OK || !nf) continue;
        g = malloc(GLUEN[k]);
        memcpy(g, GLUE[k], GLUEN[k]);
        g[5] = 0;
        CHECK(alx_glue_check(g, GLUEN[k], 0, 0, 0, 0) == ALX_E_FORM, "layer %u marked N with content", k);
        /* the first ref, pointed at a layer outside its lineage */
        uint32_t ne = (uint32_t) g[12] << 24 | (uint32_t) g[13] << 16 | (uint32_t) g[14] << 8 | g[15];
        uint8_t *rf = g + 64u + nf * ALX_MX_FILE + ne * ALX_MX_EDGE;
        uint32_t stranger = ALX_MX_LAYERS - 1u;
        while (stranger == k || alx_mx_draws(k, stranger)) stranger--;
        memcpy(g, GLUE[k], GLUEN[k]);
        rf[0] = (uint8_t) (stranger >> 8), rf[1] = (uint8_t) stranger;
        CHECK(alx_glue_check(g, GLUEN[k], 0, 0, 0, 0) == ALX_E_FORM, "layer %u drawing from %u", k, stranger);
        free(g);
        break;
    }
    g = malloc(GLUEN[0]);
    memcpy(g, GLUE[0], GLUEN[0]);
    g[20] = 1;
    CHECK(alx_glue_check(g, GLUEN[0], 0, 0, 0, 0) == ALX_E_FORM, "the core hanging from something");
    free(g);
}

static void test_refusals(void)
{
    uint8_t *r = malloc(ROOTN);
    memcpy(r, ROOT, ROOTN);
    r[200] ^= 1;
    CHECK(alx_mx_check(r, ROOTN) == ALX_E_HASH, "a changed root");
    memcpy(r, ROOT, ROOTN);
    CHECK(alx_mx_check(r, ROOTN - 1u) == ALX_E_FORM && alx_mx_check(0, 0) == ALX_E_FORM, "length");
    memcpy(r, ROOT, ROOTN);
    memset(r + 64, 0, ALX_MX_LAYER); /* an empty core */
    sha256(r, ROOTN - 32u, r + ROOTN - 32u);
    CHECK(alx_mx_check(r, ROOTN) == ALX_E_FORM, "the core is never empty");
    memcpy(r, ROOT, ROOTN);
    memset(r + 64 + 108, 0, 4); /* a tree with no booklets */
    sha256(r, ROOTN - 32u, r + ROOTN - 32u);
    CHECK(alx_mx_check(r, ROOTN) == ALX_E_FORM, "a tree that counts no booklets");
    memcpy(r, ROOT, ROOTN);
    r[64 + 72] = 9; /* an icon that is not a CID */
    sha256(r, ROOTN - 32u, r + ROOTN - 32u);
    CHECK(alx_mx_check(r, ROOTN) == ALX_E_FORM, "an icon that is not a CID");
    free(r);

    uint32_t n = BOOKN[0][0];
    uint8_t *b = malloc(n);
    memcpy(b, BOOK[0][0], n);
    uint32_t off, len;
    alx_bk2_atom(b, 0, 0, &off, &len);
    b[off] ^= 1;
    CHECK(alx_bk2_check(b, n, 0, 0) == ALX_E_HASH, "a changed atom");
    memcpy(b, BOOK[0][0], n);
    b[4] = 1;
    CHECK(alx_bk2_check(b, n, 0, 0) == ALX_E_FORM, "version 1 is the old format");
    memcpy(b, BOOK[0][0], n);
    CHECK(alx_bk2_check(b, n - 1u, 0, 0) == ALX_E_FORM, "a short booklet");
    b[64 + 36 + 3] ^= 64; /* first atom moved */
    CHECK(alx_bk2_check(b, n, 0, 0) == ALX_E_FORM, "a moved atom");
    free(b);

    uint8_t *g = malloc(GLUEN[0]);
    memcpy(g, GLUE[0], GLUEN[0]);
    uint32_t nf = 0, ne = 0;
    alx_glue_check(g, GLUEN[0], 0, &nf, &ne, 0);
    CHECK(ne > 1, "the core has glue edges");
    uint8_t *e0 = g + 64 + nf * ALX_MX_FILE, tmp[16];
    memcpy(tmp, e0, 16);
    memcpy(e0, e0 + 16, 16);
    memcpy(e0 + 16, tmp, 16);
    CHECK(alx_glue_check(g, GLUEN[0], 0, 0, 0, 0) == ALX_E_FORM, "edges out of order");
    memcpy(g, GLUE[0], GLUEN[0]);
    e0[12] = 0, e0[13] = 0;
    CHECK(alx_glue_check(g, GLUEN[0], 0, 0, 0, 0) == ALX_E_FORM, "a weightless edge");
    memcpy(g, GLUE[0], GLUEN[0]);
    g[64 + 40 + 3] ^= 1; /* first file's ref count */
    CHECK(alx_glue_check(g, GLUEN[0], 0, 0, 0, 0) == ALX_E_FORM, "refs that do not add up");
    memcpy(g, GLUE[0], GLUEN[0]);
    memcpy(g + 64 + ALX_MX_FILE, g + 64, 36); /* two files with one CID */
    CHECK(alx_glue_check(g, GLUEN[0], 0, 0, 0, 0) == ALX_E_FORM, "a file twice");
    uint8_t none[36] = {1, 0x55, 0x12, 0x20};
    CHECK(alx_glue_find(GLUE[0], none) == -1, "an unknown file");
    free(g);

    /* a fan-out node: the counts must say every child but the last is full */
    uint8_t node[64 + 2 * 36] = {'A', 'L', 'X', 'N', 1, 1};
    node[11] = 2;
    for (int i = 0; i < 2; i++) memcpy(node + 64 + 36 * i, "\x01\x55\x12\x20", 4);
    node[22] = 1, node[23] = 1; /* 257 booklets beneath */
    uint32_t lv, kids;
    uint64_t under;
    CHECK(alx_node_check(node, sizeof node, &lv, &kids, &under) == ALX_OK && lv == 1 && kids == 2,
          "257 booklets in two children");
    node[22] = 0, node[23] = 200;
    CHECK(alx_node_check(node, sizeof node, 0, 0, 0) == ALX_E_FORM, "200 cannot fill one child");
    node[22] = 2, node[23] = 1;
    CHECK(alx_node_check(node, sizeof node, 0, 0, 0) == ALX_E_FORM, "513 is too many");
    CHECK(alx_mx_depth(1) == 1 && alx_mx_depth(256) == 1 && alx_mx_depth(257) == 2 &&
              alx_mx_depth(65536) == 2 && alx_mx_depth(65537) == 3,
          "depth is log256");
    CHECK(alx_mx_digit(0x10203, 0) == 3 && alx_mx_digit(0x10203, 1) == 2 &&
              alx_mx_digit(0x10203, 2) == 1,
          "digits");
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        printf("usage: test_alx_matrix OUT-DIR REPO\n");
        return 2;
    }
    DIR_ = argv[1];
    test_cdc();
    test_layout();
    test_files(argv[2]);
    test_refusals();
    test_spine(argv[1], argv[2]);
    /* the packer stores booklets compressed (--zpack): every one is read back above */
    CHECK(ZBOOK > 0 && ZSTORED < ZPLAIN, "booklets are stored compressed");
    printf("matrix: %u booklets compressed; booklets stored in %llu of %llu bytes (%.1f%%)\n", ZBOOK,
           (unsigned long long) ZSTORED, (unsigned long long) ZPLAIN,
           ZPLAIN ? 100.0 * (double) ZSTORED / (double) ZPLAIN : 0.0);
    printf("%u checks, %u failures\n", checks, fails);
    return fails != 0;
}
