/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_alx_vec.c — the vector plane (alx_vec.h): integer distances against
 * a long-double reference, search against a full sort, summaries, the
 * shape embedding, the refusals, the tensor engine's kernels against the
 * plain loop, and the vector nodes pack_matrix.py --vectorize wrote, with
 * measurements of how well they find related files.
 * argv[1]: pack_matrix.py's --out-dir. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "alx_vec.h"
#include "alx_vec_zt.h"
#include "alx_matrix.h"
#include "../robin_debanks/sha256.h"
#include "../tensor/zt_simd.h"

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

static uint64_t S = 88172645463325252ull;
static uint32_t rnd(void)
{
    S ^= S << 13, S ^= S >> 7, S ^= S << 17;
    return (uint32_t) S;
}

static void rand_row(uint8_t *row, uint32_t nb, int lim)
{
    for (uint32_t b = 0; b < nb; b++) {
        row[b * 34] = 0, row[b * 34 + 1] = 0x3C;
        for (uint32_t i = 0; i < 32; i++)
            row[b * 34 + 2 + i] = (uint8_t) (int8_t) ((int) (rnd() % (2u * lim + 1u)) - lim);
    }
}

/* ---- arithmetic ---- */

static void test_math(void)
{
    enum { NB = 8, N = 300 };
    static uint8_t rows[N][NB * 34];
    int8_t q[NB * 32];
    for (uint32_t i = 0; i < N; i++) rand_row(rows[i], NB, i % 3 ? 127 : 3);
    memset(rows[7] + 2, 0, 32); /* sparse rows, and one all zero */
    for (uint32_t b = 0; b < NB; b++) memset(rows[9] + b * 34 + 2, 0, 32);
    for (uint32_t i = 0; i < NB * 32; i++) q[i] = (int8_t) ((int) (rnd() % 255u) - 127);
    q[0] = -128;
    for (uint32_t i = 0; i < N; i++) {
        long long d = 0, n = 0;
        for (uint32_t j = 0; j < NB * 32; j++) {
            d += (long long) alx_vec_q(rows[i], j) * q[j];
            n += (long long) alx_vec_q(rows[i], j) * alx_vec_q(rows[i], j);
        }
        CHECK(alx_vec_dot(rows[i], q, NB) == d && alx_vec_norm(rows[i], NB) == n, "row %u", i);
    }
    /* extremes: 4096 dims of -128 times -128 */
    static uint8_t big[128 * 34];
    static int8_t qb[4096];
    for (uint32_t b = 0; b < 128; b++)
        big[b * 34] = 0, big[b * 34 + 1] = 0x3C, memset(big + b * 34 + 2, 0x80, 32);
    memset(qb, 0x80, sizeof qb);
    CHECK(alx_vec_dot(big, qb, 128) == 4096 * 16384, "the largest dot");
    CHECK(alx_vec_norm(big, 128) == 4096u * 16384u && alx_vec_qnorm(qb, 128) == 4096u * 16384u,
          "norms");
    /* ranking: against a long-double cosine and distance */
    uint32_t qn = alx_vec_qnorm(q, NB), bad = 0;
    for (uint32_t m = ALX_VEC_COS; m <= ALX_VEC_L2; m++)
        for (uint32_t a = 0; a < N; a++)
            for (uint32_t b = 0; b < N; b += 7) {
                int32_t da = alx_vec_dot(rows[a], q, NB), db = alx_vec_dot(rows[b], q, NB);
                uint32_t na = alx_vec_norm(rows[a], NB), nbn = alx_vec_norm(rows[b], NB);
                long double sa, sb;
                if (m == ALX_VEC_COS) {
                    sa = na ? da / sqrtl((long double) na) : -1e30L;
                    sb = nbn ? db / sqrtl((long double) nbn) : -1e30L;
                } else if (m == ALX_VEC_DOT) {
                    sa = da, sb = db;
                } else {
                    sa = -((long double) na + qn - 2.0L * da),
                    sb = -((long double) nbn + qn - 2.0L * db);
                }
                bool want = sa > sb || (sa == sb && a < b),
                     got = alx_vec_better(m, da, na, a, db, nbn, b, qn);
                if (sa != sb && want != got) bad++;
                if (a == b) CHECK(!got, "a row never ranks before itself");
            }
    CHECK(bad == 0, "%u rankings differ from the reference", bad);
    /* summary: two rows, scales 2 and 1 */
    uint8_t two[2][34], out[34];
    uint32_t sc2[2] = {2, 1}, osc;
    memset(two, 0, sizeof two);
    two[0][1] = two[1][1] = 0x3C;
    two[0][2] = 100, two[1][2] = (uint8_t) -50, two[0][3] = 1, two[1][4] = 127;
    alx_vec_summary(&two[0][0], sc2, 2, 1, out, &osc);
    /* sums: 150, 2, 127 (max 150): 127, 1, 107; scale (150 + 127) / 254 = 1 */
    CHECK((int8_t) out[2] == 127 && (int8_t) out[3] == 1 && (int8_t) out[4] == 107 && osc == 1,
          "summary %d %d %d scale %u", (int8_t) out[2], (int8_t) out[3], (int8_t) out[4], osc);
    uint8_t zero[34] = {0, 0x3C};
    alx_vec_summary(zero, sc2, 1, 1, out, &osc);
    CHECK(osc == 0 && !out[2], "a zero summary");
}

/* ---- search, nodes and refusals ---- */

static uint32_t build(uint8_t *b, uint32_t n, uint32_t nb, uint8_t (*rows)[8 * 34],
                      uint32_t *scales)
{
    memset(b, 0, ALX_VEC_HEAD);
    memcpy(b, "ALXV\x01\x00", 6);
    b[7] = 3; /* layer 3 */
    b[8] = (uint8_t) (n >> 24), b[9] = (uint8_t) (n >> 16), b[10] = (uint8_t) (n >> 8),
    b[11] = (uint8_t) n;
    b[12] = (uint8_t) ((nb * 32) >> 8), b[13] = (uint8_t) (nb * 32);
    memcpy(b + 16, "\x01\x55\x12\x20", 4);
    memcpy(b + 52, "\x01\x55\x12\x20", 4);
    uint32_t at = ALX_VEC_HEAD;
    for (uint32_t i = 0; i < n; i++, at += ALX_VEC_REC) {
        memset(b + at, 0, ALX_VEC_REC);
        memcpy(b + at, "\x01\x55\x12\x20", 4);
        b[at + 4] = (uint8_t) i, b[at + 5] = (uint8_t) (i >> 8);
        b[at + 44] = (uint8_t) (scales[i] >> 24), b[at + 45] = (uint8_t) (scales[i] >> 16);
        b[at + 46] = (uint8_t) (scales[i] >> 8), b[at + 47] = (uint8_t) scales[i];
    }
    uint32_t r0 = (at + 63u) & ~63u;
    memset(b + at, 0, r0 - at);
    for (uint32_t i = 0; i < n; i++) memcpy(b + r0 + i * nb * 34, rows[i], nb * 34);
    uint32_t sc;
    alx_vec_summary(b + r0, scales, n, nb, b + r0 + n * nb * 34, &sc);
    b[88] = (uint8_t) (sc >> 24), b[89] = (uint8_t) (sc >> 16), b[90] = (uint8_t) (sc >> 8),
    b[91] = (uint8_t) sc;
    return r0 + (n + 1) * nb * 34;
}

static void test_nodes(void)
{
    enum { NB = 8, N = 200 };
    static uint8_t rows[N][8 * 34], b[1u << 20], c[1u << 20];
    static uint32_t scales[N];
    for (uint32_t i = 0; i < N; i++) rand_row(rows[i], NB, 127), scales[i] = 1000u + i;
    uint32_t len = build(b, N, NB, rows, scales);
    alx_vec_t v;
    CHECK(alx_vec_open(&v, b, len) == ALX_OK && v.n == N && v.dim == 256 && v.layer == 3, "a node");
    CHECK(alx_vec_scale(&v, 5) == 1005u && alx_vec_word(&v, 5) == 0, "a record");
    int8_t q[NB * 32];
    for (uint32_t i = 0; i < NB * 32; i++) q[i] = (int8_t) ((int) (rnd() % 255u) - 127);
    static uint32_t out[N], all[N];
    static int32_t dots[N];
    for (uint32_t m = ALX_VEC_COS; m <= ALX_VEC_L2; m++) {
        /* the top 10 against a full insertion sort */
        int32_t k = alx_vec_search(&v, m, q, 10, out, dots, 0, 0);
        uint32_t qn = alx_vec_qnorm(q, NB);
        for (uint32_t i = 0; i < N; i++) {
            uint32_t j = i;
            while (j > 0 &&
                   alx_vec_better(m, alx_vec_dot(rows[i], q, NB), alx_vec_norm(rows[i], NB), i,
                                  alx_vec_dot(rows[all[j - 1]], q, NB),
                                  alx_vec_norm(rows[all[j - 1]], NB), all[j - 1], qn)) {
                all[j] = all[j - 1];
                j--;
            }
            all[j] = i;
        }
        CHECK(k == 10 && !memcmp(out, all, 10 * sizeof out[0]), "metric %u top 10", m);
        CHECK(alx_vec_search(&v, m, q, 500, out, dots, 0, 0) == N && !memcmp(out, all, sizeof all),
              "metric %u: k over n gives all, in order", m);
    }
    CHECK(alx_vec_search(&v, 9, q, 3, out, dots, 0, 0) == ALX_E_FORM, "unknown metric");
    /* the tensor engine's kernels give the same integers */
    const zt_kern_t *ks[8];
    uint32_t nk = zt_simd_list(ks, 8);
    static int32_t zd[N];
    for (uint32_t i = 0; i < nk; i++) {
        CHECK(alx_vec_zt_dots((void *) ks[i], v.rows, N, NB, q, zd) == 0, "%s", ks[i]->name);
        for (uint32_t r = 0; r < N; r++) dots[r] = alx_vec_dot(alx_vec_row(&v, r), q, NB);
        CHECK(!memcmp(zd, dots, sizeof zd), "%s gives the plain loop's dot products", ks[i]->name);
    }
    printf("vector kernels checked: %u (", nk);
    for (uint32_t i = 0; i < nk; i++) printf("%s%s", i ? ", " : "", ks[i]->name);
    printf(")\n");
    CHECK(nk >= 1 && alx_vec_zt_dots(0, v.rows, N, NB, q, zd) != 0, "no kernels: the loop runs");
    int32_t kk =
        alx_vec_search(&v, ALX_VEC_COS, q, 10, out, dots, alx_vec_zt_dots, (void *) zt_simd_best());
    CHECK(kk == 10, "search through the tensor engine");

    /* refusals */
#define REFUSE(edit, want, what)                                                                   \
    do {                                                                                           \
        memcpy(c, b, len);                                                                         \
        edit;                                                                                      \
        CHECK(alx_vec_open(&v, c, len) == (want), what);                                           \
    } while (0)
    uint32_t r0 = (ALX_VEC_HEAD + N * ALX_VEC_REC + 63u) & ~63u;
    REFUSE(c[r0 + N * NB * 34 + 2] ^= 1, ALX_E_HASH, "a changed summary");
    REFUSE(c[91] ^= 1, ALX_E_HASH, "a changed summary scale");
    REFUSE(c[r0 + 2] = (uint8_t) (c[r0 + 2] ^ 0x40), ALX_E_HASH, "a changed row moves the summary");
    REFUSE(c[r0 + 1] = 0x3D, ALX_E_FORM, "a block whose scale is not one");
    REFUSE(c[ALX_VEC_HEAD + 36] = 0x10, ALX_E_FORM, "a word outside domain 0x60");
    REFUSE(c[13] = 33, ALX_E_FORM, "dim not a multiple of 32");
    REFUSE(c[5] = 2, ALX_E_FORM, "kind 2");
    REFUSE(c[100] = 1, ALX_E_FORM, "the reserved bytes");
    REFUSE(c[52] = 0, ALX_E_FORM, "a leaf with no glue");
    REFUSE(c[7] = 145, ALX_E_FORM, "a leaf in layer 145");
    REFUSE(c[ALX_VEC_HEAD] = 0, ALX_E_FORM, "a target that is not a CID");
    REFUSE(c[ALX_VEC_HEAD + N * ALX_VEC_REC + 1] = 1, ALX_E_FORM, "a byte in the gap");
    memcpy(c, b, len);
    CHECK(alx_vec_open(&v, c, len - 1) == ALX_E_FORM && alx_vec_open(&v, 0, 0) == ALX_E_FORM,
          "length");

    /* the shape embedding */
    static uint8_t row1[128 * 34], row2[128 * 34];
    static int32_t work[4096 + ALX_VEC_SEEN];
    const char *t1 =
        "int gcd(int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; }";
    uint32_t s1 = alx_vec_shape((const uint8_t *) t1, (uint32_t) strlen(t1), 256, row1, work),
             s2 = alx_vec_shape((const uint8_t *) t1, (uint32_t) strlen(t1), 256, row2, work);
    CHECK(s1 > 0 && s1 == s2 && !memcmp(row1, row2, 8 * 34),
          "the shape embedding is deterministic");
    CHECK(alx_vec_shape((const uint8_t *) "12 + 34;", 8, 64, row1, work) == 0 && !row1[2] &&
              row1[1] == 0x3C,
          "a text with no names: a zero row");
    /* each name counts once: repeating a text changes nothing */
    char t2[400];
    snprintf(t2, sizeof t2, "%s %s %s", t1, t1, t1);
    alx_vec_shape((const uint8_t *) t2, (uint32_t) strlen(t2), 256, row2, work);
    alx_vec_shape((const uint8_t *) t1, (uint32_t) strlen(t1), 256, row1, work);
    CHECK(!memcmp(row1, row2, 8 * 34), "a name counts once");
    /* a name is whole: "ab" inside "xab" is not the name ab */
    alx_vec_shape((const uint8_t *) "ab", 2, 64, row1, work);
    alx_vec_shape((const uint8_t *) "xab", 3, 64, row2, work);
    CHECK(memcmp(row1, row2, 2 * 34), "names are whole words");
    uint8_t m1[36], m2[36];
    alx_vec_shape_model(256, m1);
    alx_vec_shape_model(512, m2);
    CHECK(memcmp(m1, m2, 36) && m1[0] == 1 && m1[1] == 0x55, "each dimension is its own model");
}

/* ---- the packed vector plane ---- */

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

static uint8_t *block(const uint8_t cid[36], uint32_t *n)
{
    char name[64], path[2048];
    cid_str(cid, name);
    snprintf(path, sizeof path, "%s/blocks/%s", DIR_, name);
    uint8_t *b = slurp(path, n);
    CHECK(b != 0, "block %s", name);
    if (!b) exit(1);
    uint8_t h[32];
    sha256(b, *n, h);
    CHECK(!memcmp(h, cid + 4, 32), "block %s has its CID", name);
    return b;
}

static uint32_t from_b32(const char *s, uint8_t out[36])
{
    uint32_t acc = 0, bits = 0, k = 0;
    for (s++; *s && *s != '\n' && k < 36; s++) {
        uint32_t v = *s >= 'a' && *s <= 'z' ? (uint32_t) (*s - 'a') : (uint32_t) (*s - '2' + 26);
        acc = acc << 5 | v;
        bits += 5;
        if (bits >= 8) out[k++] = (uint8_t) (acc >> (bits -= 8));
    }
    return k;
}

/* every file's vector, its layer and its path */
enum { MAXF = 32768 };
static const uint8_t *FROW[MAXF], *FCID[MAXF];
static uint32_t FLAYER[MAXF], NF, NB_;
static char *FPATH[MAXF];
static uint32_t LTOP[ALX_MX_LAYERS], LEAVES;
static uint8_t *LEAFBUF[4096]; /* every leaf stays open until the measurements are done */
static alx_vec_t FIRSTLEAF[ALX_MX_LAYERS];
static alx_vec_t TOP;

static void walk(const uint8_t *cid, const uint8_t *want_row, uint32_t want_scale, uint32_t nb,
                 const uint8_t *model, uint32_t layer, const uint8_t *glue_cid, const uint8_t *glue,
                 int depth)
{
    uint32_t n;
    uint8_t *b = block(cid, &n);
    alx_vec_t v;
    CHECK(alx_vec_open(&v, b, n) == ALX_OK, "vector node at depth %d", depth);
    CHECK(!memcmp(v.model, model, 36) && v.nb == nb, "one model and dimension throughout");
    /* a parent's row for a child is the child's summary */
    CHECK(!memcmp(alx_vec_row(&v, v.n), want_row, nb * 34) && alx_vec_scale(&v, v.n) == want_scale,
          "the parent holds this node's summary");
    CHECK(v.layer == layer, "node in layer %u, not %u", v.layer, layer);
    if (v.kind == ALX_VEC_SUMMARY) {
        for (uint32_t i = 0; i < v.n; i++)
            walk(alx_vec_target(&v, i), alx_vec_row(&v, i), alx_vec_scale(&v, i), nb, model, layer,
                 glue_cid, glue, depth + 1);
        free(b);
        return;
    }
    if (!FIRSTLEAF[layer].b) FIRSTLEAF[layer] = v;
    CHECK(LEAVES < 4096, "leaves");
    if (LEAVES < 4096) LEAFBUF[LEAVES] = b;
    LEAVES++;
    /* fresh against today's glue; all stale against another model */
    CHECK(!memcmp(v.source, glue_cid, 36), "layer %u's vectors name its glue", layer);
    CHECK(alx_vec_stale(&v, model, glue) == 0, "layer %u's vectors are fresh", layer);
    uint8_t other[36];
    alx_vec_shape_model(v.dim * 2u, other);
    CHECK(alx_vec_stale(&v, other, glue) == v.n, "another model: every vector is stale");
    for (uint32_t i = 0; i < v.n && NF < MAXF; i++) {
        FROW[NF] = alx_vec_row(&v, i);
        FCID[NF] = alx_vec_target(&v, i);
        FLAYER[NF] = layer;
        FPATH[NF] = 0;
        NF++;
    }
}

static int32_t rank_of(uint32_t self, uint32_t target, const int32_t *dots, const uint32_t *norms,
                       uint32_t qn)
{
    int32_t r = 0;
    for (uint32_t i = 0; i < NF; i++)
        if (i != self && i != target &&
            alx_vec_better(ALX_VEC_COS, dots[i], norms[i], i, dots[target], norms[target], target,
                           qn))
            r++;
    return r;
}

static void test_packed(void)
{
    char path[2048], line[4096];
    snprintf(path, sizeof path, "%s/VECTORS", DIR_);
    uint32_t n;
    uint8_t *t = slurp(path, &n), top[36];
    CHECK(t && from_b32((char *) t, top) == 36, "VECTORS names the top node");
    if (!t) return;
    free(t);
    snprintf(path, sizeof path, "%s/MATRIX.ROOT", DIR_);
    uint8_t *root = slurp(path, &n);
    CHECK(root && alx_mx_check(root, n) == ALX_OK, "the matrix root");
    uint8_t *tb = block(top, &n);
    CHECK(alx_vec_open(&TOP, tb, n) == ALX_OK && TOP.kind == ALX_VEC_SUMMARY && TOP.layer == 145,
          "the top summary");
    NB_ = TOP.nb;
    /* the top has one row per layer that holds files, in layer order */
    uint32_t r = 0;
    for (uint32_t k = 0; k < ALX_MX_LAYERS; k++) {
        alx_mx_layer_t l;
        alx_mx_layer(root, k, &l);
        if (!l.booklets) continue;
        CHECK(r < TOP.n, "a row for layer %u", k);
        if (r >= TOP.n) break;
        LTOP[r] = k;
        uint32_t gn;
        uint8_t *g = block(l.glue, &gn);
        CHECK(alx_glue_check(g, gn, 0, 0, 0, 0) == ALX_OK, "layer %u's glue", k);
        uint32_t first = NF;
        walk(alx_vec_target(&TOP, r), alx_vec_row(&TOP, r), alx_vec_scale(&TOP, r), TOP.nb,
             TOP.model, k, l.glue, g, 1);
        /* a file whose bytes changed: its CID leaves the glue, its vector is stale */
        uint8_t *g2 = malloc(gn);
        memcpy(g2, g, gn);
        int32_t at = first < NF ? alx_glue_find(g, FCID[first]) : -1;
        CHECK(at >= 0, "layer %u: the first vector's file is in the glue", k);
        if (at >= 0) {
            g2[64 + (uint32_t) at * ALX_MX_FILE + 35] ^= 1; /* that file now has other bytes */
            CHECK(alx_glue_find(g2, FCID[first]) < 0 &&
                      alx_vec_stale(&FIRSTLEAF[k], TOP.model, g2) == 1,
                  "layer %u: a changed file's vector, and only it, is stale", k);
        }
        free(g2);
        free(g);
        r++;
    }
    CHECK(r == TOP.n, "one row per layer");
    /* names, from FILES.tsv */
    snprintf(path, sizeof path, "%s/FILES.tsv", DIR_);
    FILE *f = fopen(path, "r");
    while (f && fgets(line, sizeof line, f)) {
        char *p = strtok(line, "\t"), *ls = strtok(0, "\t"), *ts = strtok(0, "\t"),
             *cs = strtok(0, "\t\n");
        (void) ts;
        uint8_t cid[36];
        if (!p || !ls || !cs || from_b32(cs, cid) != 36) continue;
        uint32_t k = (uint32_t) atoi(ls);
        for (uint32_t i = 0; i < NF; i++)
            if (FLAYER[i] == k && !FPATH[i] && !memcmp(FCID[i], cid, 36)) FPATH[i] = strdup(p);
    }
    if (f) fclose(f);

    /* how well the vectors find related files: for every x.c with an x.h
     * beside it, where x.h ranks among all other files by cosine */
    static int32_t dots[MAXF];
    static uint32_t norms[MAXF], top10[16];
    static int8_t q[ALX_VEC_MAXDIM];
    for (uint32_t i = 0; i < NF; i++) norms[i] = alx_vec_norm(FROW[i], NB_);
    uint32_t pairs = 0, h1 = 0, h5 = 0, h10 = 0, hier_hits[5] = {0}, hier_tot = 0;
    uint64_t rank_sum = 0;
    static const uint32_t BEAM[5] = {1, 2, 4, 8, 16};
    for (uint32_t i = 0; i < NF; i++) {
        const char *p = FPATH[i];
        size_t pl = p ? strlen(p) : 0;
        if (pl < 3 || strcmp(p + pl - 2, ".c")) continue;
        uint32_t h = NF;
        for (uint32_t j = 0; j < NF && h == NF; j++)
            if (FPATH[j] && strlen(FPATH[j]) == pl && !strncmp(FPATH[j], p, pl - 1) &&
                FPATH[j][pl - 1] == 'h')
                h = j;
        if (h == NF || !norms[i]) continue;
        for (uint32_t j = 0; j < NB_ * 32u; j++) q[j] = alx_vec_q(FROW[i], j);
        uint32_t qn = alx_vec_qnorm(q, NB_);
        for (uint32_t j = 0; j < NF; j++) dots[j] = alx_vec_dot(FROW[j], q, NB_);
        int32_t rk = rank_of(i, h, dots, norms, qn);
        pairs++;
        rank_sum += (uint64_t) rk;
        h1 += rk < 1, h5 += rk < 5, h10 += rk < 10;
        /* the flat top 10 (not the query itself) */
        uint32_t w = 0;
        for (uint32_t j = 0; j < NF; j++) {
            if (j == i) continue;
            uint32_t at = w;
            while (at > 0 && alx_vec_better(ALX_VEC_COS, dots[j], norms[j], j, dots[top10[at - 1]],
                                            norms[top10[at - 1]], top10[at - 1], qn))
                at--;
            if (at >= 10) continue;
            for (uint32_t z = (w < 10 ? w : 9); z > at; z--) top10[z] = top10[z - 1];
            top10[at] = j;
            if (w < 10) w++;
        }
        /* through the summaries: the best B layers by their summary, then
         * only their files */
        static uint32_t lorder[ALX_MX_LAYERS];
        static int32_t ld[ALX_MX_LAYERS];
        int32_t nl = alx_vec_search(&TOP, ALX_VEC_COS, q, TOP.n, lorder, ld, 0, 0);
        for (uint32_t bi = 0; bi < 5; bi++) {
            uint32_t found = 0;
            for (uint32_t z = 0; z < w; z++) {
                uint32_t lay = FLAYER[top10[z]], in = 0;
                for (uint32_t y = 0; y < BEAM[bi] && (int32_t) y < nl; y++)
                    in |= LTOP[lorder[y]] == lay;
                found += in;
            }
            hier_hits[bi] += found;
        }
        hier_tot += w;
    }
    CHECK(pairs > 100, "%u .c and .h pairs measured", pairs);
    printf("vectors: %u files, %u layers, %u leaves, dim %u\n", NF, TOP.n, LEAVES, TOP.dim);
    printf("vectors: x.c finds its x.h among %u files: first %.1f%%, top 5 %.1f%%, top 10 %.1f%%, "
           "mean rank %.1f (by chance: top 10 %.2f%%), %u pairs\n",
           NF, 100.0 * h1 / pairs, 100.0 * h5 / pairs, 100.0 * h10 / pairs,
           (double) rank_sum / pairs, 100.0 * 10 / NF, pairs);
    printf("vectors: the flat top 10 reached through the best B layer summaries:");
    for (uint32_t bi = 0; bi < 5; bi++)
        printf(" B=%u %.1f%%", BEAM[bi], 100.0 * hier_hits[bi] / hier_tot);
    printf("\n");
    /* measured at dim 512: 96.2%; a drop below 90% is a regression */
    CHECK(h10 * 10u >= pairs * 9u, "x.h is in x.c's top 10 for at least 90%% of pairs (%u of %u)", h10,
          pairs);
    for (uint32_t i = 0; i < LEAVES && i < 4096; i++) free(LEAFBUF[i]);
    free(root);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        printf("usage: test_alx_vec OUT-DIR\n");
        return 2;
    }
    DIR_ = argv[1];
    test_math();
    test_nodes();
    test_packed();
    printf("%u checks, %u failures\n", checks, fails);
    return fails != 0;
}
