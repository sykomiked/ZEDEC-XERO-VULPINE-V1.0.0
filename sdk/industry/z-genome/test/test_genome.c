/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_genome.c — z-genome: matrices (BLOSUM62 entries checked against the
 * NCBI file, digests pinned), parameter and residue refusals, textbook
 * alignments (Durbin et al. HEAGAWGHEE/PAWHEAE, the Wikipedia
 * Needleman-Wunsch and Smith-Waterman examples), the documented tie-break,
 * overflow refusal, property tests against two slow reference
 * implementations written here (an exhaustive enumeration of every
 * alignment, and a full-matrix Gotoh), symmetry, exact ratios, the strict
 * FASTA parser, and signed result records (tamper, wrong key, wrong
 * matrix, other sequences, a forged score, exports with flipped bits). */
#include "zgenome.h"
#include "zind_test.h"

#undef CHECK_RC
#define CHECK_RC(expr, want)                                                                   \
    do {                                                                                       \
        int32_t rc_ = (expr);                                                                  \
        CHECK(rc_ == (want), "%s -> %d (%s), want %d (%s)", #expr, (int) rc_,                  \
              zgn_status_name(rc_), (int) (want), zgn_status_name(want));                     \
    } while (0)

#define U8(s) ((const uint8_t *) (s))

/* Pinned on one machine; every other machine and compiler must agree. */
#define K_DNA_DIGEST "c2c34b660ccbfd3f56f5a92206c889afec5913eadacbcf48dfa8dc8ad0566b68"
#define K_HB_DIGEST  "3aa6603dea242d089e2c7a74cfa80a64bd5dab86cb8f2e4848b39f8a10781a39"

static int32_t align_s(const zgn_matrix *m, int32_t mode, int64_t open, int64_t ext,
                       const char *a, const char *b, zgn_result *r) {
    zgn_params p = {mode, open, ext, 0};
    return zgn_align(m, &p, U8(a), (uint32_t) strlen(a), U8(b), (uint32_t) strlen(b), r);
}

static void hex32(const uint8_t d[32], char out[65]) {
    static const char h[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out[2 * i] = h[d[i] >> 4];
        out[2 * i + 1] = h[d[i] & 15];
    }
    out[64] = 0;
}

/* ===== slow references (independent of the kit's recurrences) ===== */

typedef struct {
    const zgn_matrix *m;
    int8_t idx[256];
    int64_t open, ext;
} ref_t;

static void ref_init(ref_t *r, const zgn_matrix *m, int64_t open, int64_t ext) {
    r->m = m;
    r->open = open;
    r->ext = ext;
    memset(r->idx, -1, sizeof r->idx);
    for (int i = 0; i < m->n; i++) r->idx[(unsigned char) m->alphabet[i]] = (int8_t) i;
}

/* Exhaustive: the best score of aligning a[i..na) with b[j..nb) when the
 * previous column was `last` (0 residue pair or start, 1 gap in b, 2 gap in
 * a), enumerating every alignment (no memo: exponential, tiny inputs). */
static int64_t brute(const ref_t *R, const char *a, int na, const char *b, int nb, int i, int j,
                     int last) {
    int64_t best = INT64_MIN, v;
    if (i == na && j == nb) return 0;
    if (i < na && j < nb) {
        v = R->m->score[R->idx[(unsigned char) a[i]]][R->idx[(unsigned char) b[j]]] +
            brute(R, a, na, b, nb, i + 1, j + 1, 0);
        if (v > best) best = v;
    }
    if (i < na) {
        v = -(last == 1 ? R->ext : R->open + R->ext) + brute(R, a, na, b, nb, i + 1, j, 1);
        if (v > best) best = v;
    }
    if (j < nb) {
        v = -(last == 2 ? R->ext : R->open + R->ext) + brute(R, a, na, b, nb, i, j + 1, 2);
        if (v > best) best = v;
    }
    return best;
}

static int64_t brute_global(const ref_t *R, const char *a, const char *b) {
    return brute(R, a, (int) strlen(a), b, (int) strlen(b), 0, 0, 0);
}

/* Local: the best global score over every pair of substrings, with at
 * least one residue on each side, or 0. */
static int64_t brute_local(const ref_t *R, const char *a, const char *b) {
    int na = (int) strlen(a), nb = (int) strlen(b);
    int64_t best = 0;
    for (int i0 = 0; i0 < na; i0++)
        for (int i1 = i0 + 1; i1 <= na; i1++)
            for (int j0 = 0; j0 < nb; j0++)
                for (int j1 = j0 + 1; j1 <= nb; j1++) {
                    int64_t v = brute(R, a + i0, i1 - i0, b + j0, j1 - j0, 0, 0, 0);
                    if (v > best) best = v;
                }
    return best;
}

/* Full-matrix Gotoh (three (n+1)x(m+1) tables, scores only). */
static int64_t ref_gotoh(const ref_t *R, const char *a, const char *b, int local) {
    int na = (int) strlen(a), nb = (int) strlen(b), W = nb + 1;
    const int64_t NI = INT64_MIN / 4;
    int64_t *M = (int64_t *) malloc(sizeof(int64_t) * (size_t) (na + 1) * W);
    int64_t *X = (int64_t *) malloc(sizeof(int64_t) * (size_t) (na + 1) * W);
    int64_t *Y = (int64_t *) malloc(sizeof(int64_t) * (size_t) (na + 1) * W);
    int64_t oe = R->open + R->ext, best = 0, v;
    for (int i = 0; i <= na; i++)
        for (int j = 0; j <= nb; j++) {
            int64_t *m = &M[i * W + j], *x = &X[i * W + j], *y = &Y[i * W + j];
            *m = *x = *y = NI;
            if (i == 0 && j == 0) {
                if (!local) *m = 0;
                continue;
            }
            if (!local && i == 0) *y = -(R->open + R->ext * j);
            if (!local && j == 0) *x = -(R->open + R->ext * i);
            if (i == 0 || j == 0) continue;
            v = M[(i - 1) * W + j - 1];
            if (X[(i - 1) * W + j - 1] > v) v = X[(i - 1) * W + j - 1];
            if (Y[(i - 1) * W + j - 1] > v) v = Y[(i - 1) * W + j - 1];
            if (local && v < 0) v = 0;
            *m = v + R->m->score[R->idx[(unsigned char) a[i - 1]]][R->idx[(unsigned char) b[j - 1]]];
            v = M[(i - 1) * W + j] - oe;
            if (X[(i - 1) * W + j] - R->ext > v) v = X[(i - 1) * W + j] - R->ext;
            if (Y[(i - 1) * W + j] - oe > v) v = Y[(i - 1) * W + j] - oe;
            *x = v;
            v = M[i * W + j - 1] - oe;
            if (X[i * W + j - 1] - oe > v) v = X[i * W + j - 1] - oe;
            if (Y[i * W + j - 1] - R->ext > v) v = Y[i * W + j - 1] - R->ext;
            *y = v;
            if (local && *m > best) best = *m;
        }
    if (!local) {
        best = M[na * W + nb];
        if (X[na * W + nb] > best) best = X[na * W + nb];
        if (Y[na * W + nb] > best) best = Y[na * W + nb];
        if (na == 0 && nb == 0) best = 0;
    }
    free(M);
    free(X);
    free(Y);
    return best;
}

/* ===== matrices ===== */

static void test_matrices(void) {
    zgn_matrix m, m2;
    uint8_t d[32], d2[32];
    char hx[65];
    int64_t sc[4] = {1, -1, -1, 1};
    static const struct {
        char a, b;
        int v;
    } k_b62[] = {{'A', 'A', 4},  {'R', 'R', 5},   {'N', 'N', 6},  {'D', 'D', 6},  {'C', 'C', 9},
                 {'Q', 'Q', 5},  {'E', 'E', 5},   {'G', 'G', 6},  {'H', 'H', 8},  {'I', 'I', 4},
                 {'L', 'L', 4},  {'K', 'K', 5},   {'M', 'M', 5},  {'F', 'F', 6},  {'P', 'P', 7},
                 {'S', 'S', 4},  {'T', 'T', 5},   {'W', 'W', 11}, {'Y', 'Y', 7},  {'V', 'V', 4},
                 {'B', 'B', 4},  {'Z', 'Z', 4},   {'X', 'X', -1}, {'*', '*', 1},  {'A', 'R', -1},
                 {'W', 'C', -2}, {'D', 'E', 2},   {'I', 'V', 3},  {'F', 'Y', 3},  {'W', 'Y', 2},
                 {'H', 'Y', 2},  {'N', 'B', 3},   {'D', 'B', 4},  {'E', 'Z', 4},  {'Q', 'Z', 3},
                 {'C', 'E', -4}, {'G', 'I', -4},  {'L', 'D', -4}, {'P', 'F', -4}, {'W', 'N', -4},
                 {'A', '*', -4}, {'X', 'P', -2},  {'S', 'A', 1},  {'T', 'S', 1},  {'K', 'R', 2},
                 {'M', 'L', 2},  {'X', '*', -4}};
    CHECK_RC(zgn_matrix_blosum62(&m), ZXV_OK);
    CHECK(m.n == 24 && strcmp(m.alphabet, "ARNDCQEGHILKMFPSTWYVBZX*") == 0, "BLOSUM62 alphabet");
    CHECK(zgn_matrix_symmetric(&m), "BLOSUM62 is symmetric");
    for (size_t k = 0; k < sizeof k_b62 / sizeof k_b62[0]; k++) {
        int i = (int) (strchr(m.alphabet, k_b62[k].a) - m.alphabet);
        int j = (int) (strchr(m.alphabet, k_b62[k].b) - m.alphabet);
        CHECK(m.score[i][j] == k_b62[k].v && m.score[j][i] == k_b62[k].v, "BLOSUM62 %c/%c = %d",
              k_b62[k].a, k_b62[k].b, k_b62[k].v);
    }
    {
        int64_t sum = 0;
        for (int i = 0; i < 24; i++)
            for (int j = 0; j < 24; j++) sum += m.score[i][j];
        CHECK(sum == -726, "BLOSUM62 entries sum to -726 as in the NCBI file (got %lld)", (long long) sum);
    }
    CHECK_RC(zgn_matrix_digest(&m, d), ZXV_OK);
    hex32(d, hx);
    CHECK(strcmp(hx, "6a4824299b0e6696ef3e83d919f403487e4f07437f01a96811a359622a783294") == 0,
          "BLOSUM62 digest pinned (%s)", hx);
    m2 = m;
    memcpy(m2.name, "renamed", 8);
    CHECK_RC(zgn_matrix_digest(&m2, d2), ZXV_OK);
    CHECK(memcmp(d, d2, 32) == 0, "the name is not part of the digest");
    m2.score[3][4]++;
    CHECK_RC(zgn_matrix_digest(&m2, d2), ZXV_OK);
    CHECK(memcmp(d, d2, 32) != 0, "one entry changes the digest");
    CHECK(!zgn_matrix_symmetric(&m2), "an edited entry breaks symmetry");

    CHECK_RC(zgn_matrix_dna(2, -3, &m), ZXV_OK);
    CHECK(strcmp(m.alphabet, "ACGTN") == 0 && m.score[0][0] == 2 && m.score[3][3] == 2 &&
              m.score[4][4] == -3 && m.score[0][1] == -3 && m.score[4][0] == -3,
          "DNA matrix: N never matches");
    CHECK_RC(zgn_matrix_digest(&m, d), ZXV_OK);
    hex32(d, hx);
    CHECK(strcmp(hx, K_DNA_DIGEST) == 0, "DNA(2,-3) digest pinned (%s)", hx);
    CHECK_RC(zgn_matrix_dna(INT64_MIN, 0, &m), ZGN_E_MATRIX);
    CHECK_RC(zgn_matrix_dna(0, INT64_MIN, &m), ZGN_E_MATRIX);
    CHECK_RC(zgn_matrix_dna(0, 0, NULL), ZXV_E_ARG);
    CHECK_RC(zgn_matrix_blosum62(NULL), ZXV_E_ARG);

    CHECK_RC(zgn_matrix_custom("ab", "AB", sc, &m), ZXV_OK);
    CHECK_RC(zgn_matrix_custom("ab", "", sc, &m), ZGN_E_MATRIX);
    CHECK_RC(zgn_matrix_custom("ab", "AA", sc, &m), ZGN_E_MATRIX);
    CHECK_RC(zgn_matrix_custom("ab", "aB", sc, &m), ZGN_E_MATRIX);
    CHECK_RC(zgn_matrix_custom("ab", "A-", sc, &m), ZGN_E_MATRIX);
    CHECK_RC(zgn_matrix_custom("ab", "A ", sc, &m), ZGN_E_MATRIX);
    CHECK_RC(zgn_matrix_custom("ab", "A1", sc, &m), ZGN_E_MATRIX);
    CHECK_RC(zgn_matrix_custom("a name that is far too long", "AB", sc, &m), ZGN_E_MATRIX);
    CHECK_RC(zgn_matrix_custom(NULL, "AB", sc, &m), ZGN_E_MATRIX);
    CHECK_RC(zgn_matrix_custom("ab", NULL, sc, &m), ZXV_E_ARG);
    CHECK_RC(zgn_matrix_custom("ab", "AB", NULL, &m), ZXV_E_ARG);
    {
        int64_t big[33 * 33] = {0};
        CHECK_RC(zgn_matrix_custom("x", "ABCDEFGHIJKLMNOPQRSTUVWXYZ*ABCDEF", big, &m), ZGN_E_MATRIX);
        sc[2] = INT64_MIN;
        CHECK_RC(zgn_matrix_custom("ab", "AB", sc, &m), ZGN_E_MATRIX);
        sc[2] = -1;
    }
    CHECK_RC(zgn_matrix_custom("ab", "AB", sc, &m), ZXV_OK);
    m2 = m;
    m2.n = 3;
    CHECK_RC(zgn_matrix_check(&m2), ZGN_E_MATRIX);
    m2 = m;
    memset(m2.alphabet, 'A', sizeof m2.alphabet);
    CHECK_RC(zgn_matrix_check(&m2), ZGN_E_MATRIX);
    m2 = m;
    m2.name[0] = 1;
    CHECK_RC(zgn_matrix_check(&m2), ZGN_E_MATRIX);
    CHECK_RC(zgn_matrix_check(NULL), ZXV_E_ARG);
    CHECK(!zgn_matrix_symmetric(NULL), "NULL is not symmetric");
}

/* ===== refusals ===== */

static void test_refusals(void) {
    zgn_matrix m;
    zgn_result r;
    zgn_params p = {ZGN_GLOBAL, 1, 1, 0};
    zgn_matrix_dna(1, -1, &m);
    CHECK_RC(zgn_align(&m, &p, U8("AC"), 2, U8("AC"), 2, NULL), ZXV_E_ARG);
    CHECK_RC(zgn_align(NULL, &p, U8("AC"), 2, U8("AC"), 2, &r), ZXV_E_ARG);
    CHECK_RC(zgn_align(&m, NULL, U8("AC"), 2, U8("AC"), 2, &r), ZXV_E_ARG);
    CHECK_RC(zgn_align(&m, &p, NULL, 2, U8("AC"), 2, &r), ZXV_E_ARG);
    CHECK_RC(zgn_align(&m, &p, U8("AC"), 2, NULL, 1, &r), ZXV_E_ARG);
    CHECK(r.a_row == NULL && r.score == 0, "a refusal leaves the result empty");
    p.mode = 0;
    CHECK_RC(zgn_align(&m, &p, U8("AC"), 2, U8("AC"), 2, &r), ZXV_E_ARG);
    p.mode = 3;
    CHECK_RC(zgn_align(&m, &p, U8("AC"), 2, U8("AC"), 2, &r), ZXV_E_ARG);
    p.mode = ZGN_LOCAL;
    p.gap_open = -1;
    CHECK_RC(zgn_align(&m, &p, U8("AC"), 2, U8("AC"), 2, &r), ZXV_E_ARG);
    p.gap_open = 1;
    p.gap_extend = -1;
    CHECK_RC(zgn_align(&m, &p, U8("AC"), 2, U8("AC"), 2, &r), ZXV_E_ARG);
    p.gap_extend = 1;
    p.max_cells = (uint64_t) ZGN_CELLS_MAX + 1;
    CHECK_RC(zgn_align(&m, &p, U8("AC"), 2, U8("AC"), 2, &r), ZXV_E_ARG);
    p.max_cells = 8;
    CHECK_RC(zgn_align(&m, &p, U8("AC"), 2, U8("AC"), 2, &r), ZGN_E_LIMIT); /* 9 cells */
    p.max_cells = 9;
    CHECK_RC(zgn_align(&m, &p, U8("AC"), 2, U8("AC"), 2, &r), ZXV_OK);
    zgn_result_free(&r);
    p.max_cells = 0;
    CHECK_RC(zgn_align(&m, &p, U8("AC"), ZGN_SEQ_MAX + 1, U8("AC"), 2, &r), ZGN_E_LIMIT);
    CHECK_RC(zgn_align(&m, &p, U8("AC"), 2, U8("AC"), ZGN_SEQ_MAX + 1, &r), ZGN_E_LIMIT);
    {
        /* the default bound: 4097 x 4097 cells is over 2^24 */
        static uint8_t big[4096];
        /* 3000 x 3000 fits the default bound */
        memset(big, 'A', sizeof big);
        CHECK_RC(zgn_align(&m, &p, big, 4096, big, 4096, &r), ZGN_E_LIMIT);
        CHECK_RC(zgn_align(&m, &p, big, 3000, big, 3000, &r), ZXV_OK);
        CHECK(r.score == 3000 && r.matches == 3000 && r.columns == 3000, "3000 x 3000 identity");
        zgn_result_free(&r);
    }
    CHECK_RC(zgn_align(&m, &p, U8("ACx"), 3, U8("AC"), 2, &r), ZGN_E_SEQ);
    CHECK_RC(zgn_align(&m, &p, U8("ACU"), 3, U8("AC"), 2, &r), ZGN_E_SEQ);
    CHECK_RC(zgn_align(&m, &p, U8("AC"), 2, U8("A-"), 2, &r), ZGN_E_SEQ);
    CHECK_RC(zgn_align(&m, &p, U8("AC"), 2, U8("A\0"), 2, &r), ZGN_E_SEQ);
    CHECK_RC(zgn_align(&m, &p, U8("ac"), 2, U8("AC"), 2, &r), ZGN_E_SEQ);
    {
        zgn_matrix bad = m;
        bad.n = 0;
        CHECK_RC(zgn_align(&bad, &p, U8("AC"), 2, U8("AC"), 2, &r), ZGN_E_MATRIX);
    }
    /* score_rows refusals */
    {
        int64_t s = 0;
        p.mode = ZGN_GLOBAL;
        CHECK_RC(zgn_score_rows(&m, &p, "AC-", "A-C", &s), ZXV_OK);
        CHECK(s == 1 - 2 - 2, "rescore AC-/A-C = -3 (got %lld)", (long long) s);
        CHECK_RC(zgn_score_rows(&m, &p, "A-", "A-", &s), ZXV_E_ARG);
        CHECK_RC(zgn_score_rows(&m, &p, "AC", "A", &s), ZXV_E_ARG);
        CHECK_RC(zgn_score_rows(&m, &p, "AX", "AC", &s), ZGN_E_SEQ);
        CHECK_RC(zgn_score_rows(&m, &p, NULL, "AC", &s), ZXV_E_ARG);
        CHECK_RC(zgn_score_rows(&m, &p, "", "", &s), ZXV_OK);
        CHECK(s == 0, "empty rows score 0");
    }
    zgn_result_free(NULL);
}

/* ===== textbook alignments ===== */

static void test_textbook(void) {
    zgn_matrix m;
    zgn_result r;
    zind_qty q;
    char buf[32];
    size_t n = 0;
    /* Durbin, Eddy, Krogh & Mitchison, "Biological sequence analysis",
     * section 2.3: BLOSUM50 (the entries for these six residues) and a
     * linear gap of 8. Global score 1; local score 28, AWGHE / AW-HE. */
    static const int64_t b50[36] = {
        /*        A   E   G   H   P   W */
        /* A */ 5,  -1, 0,  -2, -1, -3,
        /* E */ -1, 6,  -3, 0,  -1, -3,
        /* G */ 0,  -3, 8,  -2, -2, -3,
        /* H */ -2, 0,  -2, 10, -2, -3,
        /* P */ -1, -1, -2, -2, 10, -4,
        /* W */ -3, -3, -3, -3, -4, 15,
    };
    CHECK_RC(zgn_matrix_custom("BLOSUM50-AEGHPW", "AEGHPW", b50, &m), ZXV_OK);
    CHECK(zgn_matrix_symmetric(&m), "BLOSUM50 subset symmetric");
    CHECK_RC(align_s(&m, ZGN_GLOBAL, 0, 8, "HEAGAWGHEE", "PAWHEAE", &r), ZXV_OK);
    CHECK(r.score == 1, "Durbin global score 1 (got %lld)", (long long) r.score);
    CHECK(r.columns >= 10 && r.a_start == 0 && r.a_end == 10 && r.b_end == 7, "global covers all");
    zgn_result_free(&r);
    CHECK_RC(align_s(&m, ZGN_LOCAL, 0, 8, "HEAGAWGHEE", "PAWHEAE", &r), ZXV_OK);
    CHECK(r.score == 28, "Durbin local score 28 (got %lld)", (long long) r.score);
    CHECK(r.a_row && strcmp(r.a_row, "AWGHE") == 0 && strcmp(r.b_row, "AW-HE") == 0,
          "Durbin local AWGHE / AW-HE (got %s / %s)", r.a_row, r.b_row);
    CHECK(r.a_start == 4 && r.a_end == 9 && r.b_start == 1 && r.b_end == 5, "Durbin coordinates");
    CHECK(r.matches == 4 && r.mismatches == 0 && r.gap_columns == 1 && r.gap_opens == 1,
          "Durbin counts");
    CHECK_RC(zgn_identity(&r, &q), ZXV_OK);
    CHECK(q.num == 4 && q.den == 5, "identity 4/5");
    CHECK_RC(zgn_score_per_column(&r, &q), ZXV_OK);
    CHECK(q.num == 28 && q.den == 5, "28/5 per column");
    CHECK_RC(zind_qty_format(q, buf, sizeof buf, &n), ZXV_OK);
    CHECK(strcmp(buf, "5.6") == 0, "28/5 formats as 5.6 (exact)");
    zgn_result_free(&r);

    /* Wikipedia, Smith-Waterman: TGTTACGG / GGTTGACTA, match 3, mismatch -3,
     * linear gap 2: score 13, GTT-AC / GTTGAC. */
    CHECK_RC(zgn_matrix_dna(3, -3, &m), ZXV_OK);
    CHECK_RC(align_s(&m, ZGN_LOCAL, 0, 2, "TGTTACGG", "GGTTGACTA", &r), ZXV_OK);
    CHECK(r.score == 13 && strcmp(r.a_row, "GTT-AC") == 0 && strcmp(r.b_row, "GTTGAC") == 0,
          "Wikipedia SW: 13, GTT-AC / GTTGAC (got %lld %s %s)", (long long) r.score, r.a_row,
          r.b_row);
    CHECK(r.a_start == 1 && r.a_end == 6 && r.b_start == 1 && r.b_end == 7, "SW coordinates");
    zgn_result_free(&r);

    /* Wikipedia, Needleman-Wunsch: GCATGCU / GATTACA, match 1, mismatch -1,
     * gap 1: score 0. */
    {
        static const int64_t s5[25] = {1,  -1, -1, -1, -1, -1, 1,  -1, -1, -1, -1, -1, 1,
                                       -1, -1, -1, -1, -1, 1,  -1, -1, -1, -1, -1, 1};
        CHECK_RC(zgn_matrix_custom("ACGTU", "ACGTU", s5, &m), ZXV_OK);
        CHECK_RC(align_s(&m, ZGN_GLOBAL, 0, 1, "GCATGCU", "GATTACA", &r), ZXV_OK);
        CHECK(r.score == 0, "Wikipedia NW score 0 (got %lld)", (long long) r.score);
        zgn_result_free(&r);
    }

    /* Affine vs linear: one long gap is cheaper than two short ones. */
    CHECK_RC(zgn_matrix_dna(5, -4, &m), ZXV_OK);
    CHECK_RC(align_s(&m, ZGN_GLOBAL, 10, 1, "ACGTACGTTTTTACGT", "ACGTACGTACGT", &r), ZXV_OK);
    CHECK(r.gap_opens == 1 && r.gap_columns == 4 && r.score == 12 * 5 - 10 - 4,
          "affine: one gap of 4 (score %lld, opens %u)", (long long) r.score, r.gap_opens);
    zgn_result_free(&r);

    /* BLOSUM62: identical proteins score the sum of their diagonal. */
    CHECK_RC(zgn_matrix_blosum62(&m), ZXV_OK);
    {
        const char *hb = "MVHLTPEEKSAVTALWGKVNVDEVGGEALGRLLVVYPWTQRFFESFGDLSTPDAVMGNPKVKAHGKKVLGAFSDGLAHLDNLKGTFATLSELHCDKLHVDPENFRLLGNVLVCVLAHHFGKEFTPPVQAAYQKVVAGVANALAHKYH";
        int64_t want = 0;
        for (const char *c = hb; *c; c++) {
            int i = (int) (strchr(m.alphabet, *c) - m.alphabet);
            want += m.score[i][i];
        }
        CHECK_RC(align_s(&m, ZGN_GLOBAL, 11, 1, hb, hb, &r), ZXV_OK);
        CHECK(r.score == want && r.matches == strlen(hb) && r.columns == strlen(hb),
              "HBB vs itself = diagonal sum %lld", (long long) want);
        zgn_result_free(&r);
        CHECK_RC(align_s(&m, ZGN_LOCAL, 11, 1, hb, hb, &r), ZXV_OK);
        CHECK(r.score == want && r.a_start == 0 && r.a_end == strlen(hb), "local self = global");
        zgn_result_free(&r);
    }
    /* HBA vs HBB human (BLOSUM62, open 11, extend 1): the scores the kit
     * gives here are pinned, so any machine that disagrees fails. */
    {
        const char *ha = "MVLSPADKTNVKAAWGKVGAHAGEYGAEALERMFLSFPTTKTYFPHFDLSHGSAQVKGHGKKVADALTNAVAHVDDMPNALSALSDLHAHKLRVDPVNFKLLSHCLLVTLAAHLPAEFTPAVHASLDKFLASVSTVLTSKYR";
        const char *hb = "MVHLTPEEKSAVTALWGKVNVDEVGGEALGRLLVVYPWTQRFFESFGDLSTPDAVMGNPKVKAHGKKVLGAFSDGLAHLDNLKGTFATLSELHCDKLHVDPENFRLLGNVLVCVLAHHFGKEFTPPVQAAYQKVVAGVANALAHKYH";
        ref_t R;
        uint8_t d[32];
        char hx[65];
        ref_init(&R, &m, 11, 1);
        CHECK_RC(align_s(&m, ZGN_LOCAL, 11, 1, ha, hb, &r), ZXV_OK);
        CHECK(r.score == ref_gotoh(&R, ha, hb, 1), "HBA/HBB local = reference (%lld)",
              (long long) r.score);
        CHECK(r.score == 285 && r.columns == 145 && r.matches == 61,
              "HBA/HBB local pinned: 285, 145 columns, 61 identical (got %lld)",
              (long long) r.score);
        CHECK_RC(zgn_alignment_digest(&r, d), ZXV_OK);
        hex32(d, hx);
        CHECK(strcmp(hx, K_HB_DIGEST) == 0, "HBA/HBB alignment digest pinned (%s)", hx);
        printf("  HBA/HBB local: score %lld, %u columns, identity %u/%u, digest %.16s\n",
               (long long) r.score, r.columns, r.matches, r.columns, hx);
        zgn_result_free(&r);
        CHECK_RC(align_s(&m, ZGN_GLOBAL, 11, 1, ha, hb, &r), ZXV_OK);
        CHECK(r.score == ref_gotoh(&R, ha, hb, 0), "HBA/HBB global = reference (%lld)",
              (long long) r.score);
        zgn_result_free(&r);
    }
}

/* ===== the documented tie-break ===== */

static void test_tiebreak(void) {
    zgn_matrix m;
    zgn_result r;
    zgn_matrix_dna(1, -1, &m);
    /* A vs AA, linear gap 1: "-A/AA" (M at the end) beats "A-/AA" (Y). */
    CHECK_RC(align_s(&m, ZGN_GLOBAL, 0, 1, "A", "AA", &r), ZXV_OK);
    CHECK(r.score == 0 && strcmp(r.a_row, "-A") == 0 && strcmp(r.b_row, "AA") == 0,
          "tie: end in M (got %s/%s)", r.a_row, r.b_row);
    zgn_result_free(&r);
    /* AA vs A: "-" in b; M at the end again: AA / -A */
    CHECK_RC(align_s(&m, ZGN_GLOBAL, 0, 1, "AA", "A", &r), ZXV_OK);
    CHECK(strcmp(r.a_row, "AA") == 0 && strcmp(r.b_row, "-A") == 0, "tie: AA/-A (got %s/%s)",
          r.a_row, r.b_row);
    zgn_result_free(&r);
    /* AC vs CA with free gaps: X (gap in b) is preferred over Y */
    CHECK_RC(align_s(&m, ZGN_GLOBAL, 0, 0, "AC", "CA", &r), ZXV_OK);
    CHECK(r.score == 1, "AC/CA with free gaps scores 1 (got %lld)", (long long) r.score);
    {
        zgn_result r2;
        CHECK_RC(align_s(&m, ZGN_GLOBAL, 0, 0, "AC", "CA", &r2), ZXV_OK);
        CHECK(strcmp(r.a_row, r2.a_row) == 0 && strcmp(r.b_row, r2.b_row) == 0,
              "the same input gives the same traceback");
        zgn_result_free(&r2);
    }
    zgn_result_free(&r);
    /* local: two equal best cells, the first in row-major order wins */
    CHECK_RC(align_s(&m, ZGN_LOCAL, 0, 5, "AC", "CA", &r), ZXV_OK);
    CHECK(r.score == 1 && r.a_start == 0 && r.a_end == 1 && r.b_start == 1 && r.b_end == 2,
          "local tie: row-major first (a %u..%u, b %u..%u)", r.a_start, r.a_end, r.b_start,
          r.b_end);
    zgn_result_free(&r);
    /* local: a zero-scoring prefix is dropped (A+1, C-1 then GG +2) */
    CHECK_RC(align_s(&m, ZGN_LOCAL, 0, 5, "ACGG", "ATGG", &r), ZXV_OK);
    CHECK(r.score == 2 && strcmp(r.a_row, "GG") == 0 && r.a_start == 2,
          "local: zero prefix dropped (got %s from %u)", r.a_row, r.a_start);
    zgn_result_free(&r);
    /* local: nothing positive is the empty alignment */
    CHECK_RC(align_s(&m, ZGN_LOCAL, 0, 1, "AAAA", "CCCC", &r), ZXV_OK);
    CHECK(r.score == 0 && r.columns == 0 && r.a_row && r.a_row[0] == 0 && r.a_start == 0 &&
              r.a_end == 0,
          "local empty alignment");
    CHECK_RC(zgn_identity(&r, &(zind_qty){0, 1}), ZXV_E_STATE);
    CHECK_RC(zgn_score_per_column(&r, &(zind_qty){0, 1}), ZXV_E_STATE);
    zgn_result_free(&r);
    /* empty inputs */
    CHECK_RC(align_s(&m, ZGN_GLOBAL, 3, 2, "", "", &r), ZXV_OK);
    CHECK(r.score == 0 && r.columns == 0, "empty vs empty");
    zgn_result_free(&r);
    CHECK_RC(align_s(&m, ZGN_GLOBAL, 3, 2, "", "ACG", &r), ZXV_OK);
    CHECK(r.score == -(3 + 3 * 2) && strcmp(r.a_row, "---") == 0 && r.gap_opens == 1,
          "empty vs ACG: one gap (%lld)", (long long) r.score);
    zgn_result_free(&r);
    CHECK_RC(align_s(&m, ZGN_GLOBAL, 3, 2, "ACG", "", &r), ZXV_OK);
    CHECK(r.score == -9 && strcmp(r.b_row, "---") == 0, "ACG vs empty");
    zgn_result_free(&r);
    CHECK_RC(align_s(&m, ZGN_LOCAL, 3, 2, "", "ACG", &r), ZXV_OK);
    CHECK(r.score == 0 && r.columns == 0, "local with an empty side");
    zgn_result_free(&r);
}

/* ===== overflow ===== */

static void test_overflow(void) {
    zgn_matrix m;
    zgn_result r;
    int64_t s = 0;
    int64_t big[4] = {INT64_MAX, 0, 0, INT64_MAX};
    zgn_params p = {ZGN_GLOBAL, 0, 0, 0};
    CHECK_RC(zgn_matrix_custom("big", "AB", big, &m), ZXV_OK);
    CHECK_RC(align_s(&m, ZGN_GLOBAL, 0, 0, "A", "A", &r), ZXV_OK);
    CHECK(r.score == INT64_MAX, "one INT64_MAX match fits");
    zgn_result_free(&r);
    CHECK_RC(align_s(&m, ZGN_GLOBAL, 0, 0, "AA", "AA", &r), ZGN_E_OVERFLOW);
    CHECK(r.a_row == NULL, "overflow leaves no result");
    CHECK_RC(align_s(&m, ZGN_LOCAL, 0, 0, "AA", "AA", &r), ZGN_E_OVERFLOW);
    CHECK_RC(zgn_score_rows(&m, &p, "AA", "AA", &s), ZGN_E_OVERFLOW);
    /* even off the best path: a large negative mismatch overflows a sum */
    big[0] = 1;
    big[1] = big[2] = INT64_MIN + 1;
    big[3] = 1;
    CHECK_RC(zgn_matrix_custom("neg", "AB", big, &m), ZXV_OK);
    CHECK_RC(align_s(&m, ZGN_GLOBAL, 0, 1, "AB", "BA", &r), ZGN_E_OVERFLOW);
    CHECK_RC(align_s(&m, ZGN_GLOBAL, 0, 1, "A", "B", &r), ZXV_OK);
    CHECK(r.score == -2, "A/B: two gaps (-2) beat INT64_MIN+1");
    zgn_result_free(&r);
    /* gap penalties */
    zgn_matrix_dna(1, -1, &m);
    CHECK_RC(align_s(&m, ZGN_GLOBAL, INT64_MAX, 1, "A", "C", &r), ZGN_E_OVERFLOW);
    CHECK_RC(align_s(&m, ZGN_GLOBAL, INT64_MAX / 2 - 1, 1, "A", "C", &r), ZXV_OK);
    CHECK(r.score == -1 && r.gap_columns == 0, "huge gaps: mismatch chosen");
    zgn_result_free(&r);
    CHECK_RC(align_s(&m, ZGN_GLOBAL, INT64_MAX / 2, INT64_MAX / 2, "AA", "", &r), ZGN_E_OVERFLOW);
    CHECK_RC(align_s(&m, ZGN_GLOBAL, 0, INT64_MAX / 4, "AAA", "", &r), ZXV_OK);
    zgn_result_free(&r);
    CHECK_RC(align_s(&m, ZGN_GLOBAL, 0, INT64_MAX / 4, "AAAAA", "", &r), ZGN_E_OVERFLOW);
    p.gap_open = INT64_MAX;
    p.gap_extend = 1;
    CHECK_RC(zgn_score_rows(&m, &p, "A", "C", &s), ZGN_E_OVERFLOW);
    CHECK_RC(zgn_score_rows(&m, &p, "A-", "-C", &s), ZGN_E_OVERFLOW);
}

/* ===== properties against the references ===== */

static void random_seq(uint64_t *s, const char *alpha, int n, int len, char *out) {
    for (int i = 0; i < len; i++) {
        uint64_t v = zt_rand(s);
        out[i] = alpha[v % (uint64_t) n];
    }
    out[len] = 0;
}

static void random_matrix(uint64_t *s, int sym, zgn_matrix *m) {
    static const char *alphas[] = {"AC", "ACG", "ACGT", "ARNDC", "WXYZ*E"};
    uint64_t v = zt_rand(s);
    const char *al = alphas[v % 5];
    int n = (int) strlen(al);
    int64_t sc[36];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++) {
            uint64_t w = zt_rand(s);
            sc[i * n + j] = (int64_t) (w % 21) - 10;
        }
    if (sym)
        for (int i = 0; i < n; i++)
            for (int j = 0; j < i; j++) sc[i * n + j] = sc[j * n + i];
    zgn_matrix_custom("rand", al, sc, m);
}

static void check_result(const zgn_matrix *m, int32_t mode, int64_t open, int64_t ext,
                         const char *a, const char *b, const zgn_result *r) {
    zgn_params p = {mode, open, ext, 0};
    int64_t rs = 0;
    size_t la = strlen(a), lb = strlen(b);
    uint32_t q = 0;
    int ok = 1;
    CHECK(zgn_score_rows(m, &p, r->a_row, r->b_row, &rs) == ZXV_OK && rs == r->score,
          "the alignment rescores to its score");
    CHECK(strlen(r->a_row) == r->columns && strlen(r->b_row) == r->columns, "row lengths");
    CHECK(r->matches + r->mismatches + r->gap_columns == r->columns, "column counts add up");
    /* each row without gaps is the claimed range of its sequence */
    q = r->a_start;
    for (uint32_t k = 0; k < r->columns; k++)
        if (r->a_row[k] != '-') ok &= q < la && r->a_row[k] == a[q++];
    CHECK(ok && q == r->a_end, "row a is a[%u..%u)", r->a_start, r->a_end);
    ok = 1;
    q = r->b_start;
    for (uint32_t k = 0; k < r->columns; k++)
        if (r->b_row[k] != '-') ok &= q < lb && r->b_row[k] == b[q++];
    CHECK(ok && q == r->b_end, "row b is b[%u..%u)", r->b_start, r->b_end);
    if (mode == ZGN_GLOBAL)
        CHECK(r->a_start == 0 && r->a_end == la && r->b_start == 0 && r->b_end == lb,
              "global covers both sequences");
    else if (r->columns)
        CHECK(r->a_row[0] != '-' && r->b_row[0] != '-' && r->a_row[r->columns - 1] != '-' &&
                  r->b_row[r->columns - 1] != '-',
              "local starts and ends with a residue pair");
}

static void test_properties(void) {
    uint64_t s = 0x6e6f6d65ULL;
    int n_brute = 0, n_ref = 0;
    for (int t = 0; t < 240; t++) {
        zgn_matrix m;
        zgn_result g, l, g2, l2;
        ref_t R;
        char a[64], b[64];
        uint64_t v = zt_rand(&s);
        int sym = (int) (v % 3 != 0), small = t < 120;
        int64_t open, ext;
        int la, lb;
        random_matrix(&s, sym, &m);
        v = zt_rand(&s);
        open = (int64_t) (v % 7);
        v = zt_rand(&s);
        ext = (int64_t) (v % 4);
        v = zt_rand(&s);
        la = small ? (int) (v % 6) : 10 + (int) (v % 50);
        v = zt_rand(&s);
        lb = small ? (int) (v % 6) : 10 + (int) (v % 50);
        random_seq(&s, m.alphabet, m.n, la, a);
        random_seq(&s, m.alphabet, m.n, lb, b);
        ref_init(&R, &m, open, ext);
        CHECK_RC(align_s(&m, ZGN_GLOBAL, open, ext, a, b, &g), ZXV_OK);
        CHECK_RC(align_s(&m, ZGN_LOCAL, open, ext, a, b, &l), ZXV_OK);
        if (!g.a_row || !l.a_row) continue;
        check_result(&m, ZGN_GLOBAL, open, ext, a, b, &g);
        check_result(&m, ZGN_LOCAL, open, ext, a, b, &l);
        CHECK(g.score == ref_gotoh(&R, a, b, 0), "global = full-matrix reference (%s/%s)", a, b);
        CHECK(l.score == ref_gotoh(&R, a, b, 1), "local = full-matrix reference (%s/%s)", a, b);
        n_ref += 2;
        if (small) {
            CHECK(g.score == brute_global(&R, a, b), "global = every alignment (%s/%s: %lld)", a,
                  b, (long long) g.score);
            if (la <= 4 && lb <= 4)
                CHECK(l.score == brute_local(&R, a, b), "local = every alignment (%s/%s: %lld)",
                      a, b, (long long) l.score);
            n_brute++;
        }
        CHECK(l.score >= 0 && l.score >= g.score, "local >= max(0, global)");
        /* symmetry: swapping the inputs under a symmetric matrix keeps the score */
        if (sym) {
            CHECK_RC(align_s(&m, ZGN_GLOBAL, open, ext, b, a, &g2), ZXV_OK);
            CHECK_RC(align_s(&m, ZGN_LOCAL, open, ext, b, a, &l2), ZXV_OK);
            CHECK(g2.score == g.score && l2.score == l.score, "symmetric scores (%s/%s)", a, b);
            zgn_result_free(&g2);
            zgn_result_free(&l2);
        }
        /* determinism */
        CHECK_RC(align_s(&m, ZGN_LOCAL, open, ext, a, b, &l2), ZXV_OK);
        CHECK(l2.score == l.score && l2.a_row && strcmp(l2.a_row, l.a_row) == 0 &&
                  strcmp(l2.b_row, l.b_row) == 0 && l2.a_start == l.a_start &&
                  l2.b_start == l.b_start,
              "identical traceback on a second run");
        zgn_result_free(&l2);
        zgn_result_free(&g);
        zgn_result_free(&l);
    }
    printf("  properties: %d reference comparisons, %d exhaustive enumerations\n", n_ref, n_brute);
}

/* ===== FASTA ===== */

static int32_t fasta(const char *text, zgn_fasta **f) {
    return zgn_fasta_parse(U8(text), strlen(text), NULL, f);
}

static void test_fasta(void) {
    zgn_fasta *f = NULL;
    zgn_seq s;
    const char *desc = NULL;
    uint32_t n = 0;
    const char *good = ">sp|P69905|HBA_HUMAN Hemoglobin subunit alpha\r\n"
                       "MVLSPADKTNVKAAWGKVGAHAGEYGAEALERMFLSFPTTKTYFPHFDLSHGSAQVKGHGKKVADALTNAVAHVDDMP\r\n"
                       "naLSALSDLHAHKLRVDPVNFKLLSHCLLVTLAAHLPAEFTPAVHASLDKFLASVSTVLTSKYR\n"
                       ">seq2\n"
                       "ACGT*\n"
                       ">x\tTab description\n"
                       "a";
    CHECK_RC(fasta(good, &f), ZXV_OK);
    CHECK_RC(zgn_fasta_count(f, &n), ZXV_OK);
    CHECK(n == 3, "three records (got %u)", n);
    CHECK_RC(zgn_fasta_get(f, 0, &s, &desc), ZXV_OK);
    CHECK(strcmp(s.id, "sp|P69905|HBA_HUMAN") == 0 && s.len == 142 &&
              strcmp(desc, "Hemoglobin subunit alpha") == 0 && memcmp(s.res + 78, "NALS", 4) == 0,
          "record 1: id, 142 residues, lower case upper-cased");
    CHECK_RC(zgn_fasta_get(f, 1, &s, &desc), ZXV_OK);
    CHECK(strcmp(s.id, "seq2") == 0 && s.len == 5 && memcmp(s.res, "ACGT*", 5) == 0 &&
              desc[0] == 0,
          "record 2");
    CHECK_RC(zgn_fasta_get(f, 2, &s, &desc), ZXV_OK);
    CHECK(strcmp(s.id, "x") == 0 && s.len == 1 && s.res[0] == 'A' &&
              strcmp(desc, "Tab description") == 0,
          "record 3, no final newline");
    CHECK_RC(zgn_fasta_get(f, 3, &s, NULL), ZXV_E_NOT_FOUND);
    CHECK_RC(zgn_fasta_find(f, "seq2", &s), ZXV_OK);
    CHECK(s.len == 5, "find by ID");
    CHECK_RC(zgn_fasta_find(f, "seq3", &s), ZXV_E_NOT_FOUND);
    CHECK_RC(zgn_fasta_find(f, NULL, &s), ZXV_E_ARG);
    zgn_fasta_destroy(f);
    f = NULL;

    {
        static const struct {
            const char *text, *why;
        } bad[] = {
            {"", "empty"},
            {"ACGT\n", "no header"},
            {"\n>a\nACGT\n", "leading blank line"},
            {">a\n\nACGT\n", "blank line inside"},
            {">a\nACGT\n\n", "trailing blank line"},
            {">a\nACGT\n\n>b\nAC\n", "blank line between"},
            {">a\nAC-GT\n", "gap"},
            {">a\nAC.GT\n", "dot"},
            {">a\nAC GT\n", "space"},
            {">a\nAC1GT\n", "digit"},
            {">a\nAC\tGT\n", "tab in sequence"},
            {">a\nACGT\r", "lone CR at end"},
            {">a\nAC\rGT\n", "CR inside"},
            {">a\n", "no residues"},
            {">a\n>b\nAC\n", "empty first record"},
            {">\nACGT\n", "empty ID"},
            {"> desc\nACGT\n", "space before ID"},
            {">a \nACGT\n", "empty description"},
            {">a\nACGT\n>a\nAC\n", "repeated ID"},
            {">a\x01\nACGT\n", "control byte in ID"},
            {">a d\x7f\nACGT\n", "DEL in description"},
            {">a\nAC\x80GT\n", "high byte"},
            {">a b\nAC\0GT\n", "NUL"},
            {">a>b\nACGT\n", "> in ID"},
        };
        for (size_t k = 0; k < sizeof bad / sizeof bad[0]; k++) {
            size_t len = strlen(bad[k].text);
            if (k == sizeof bad / sizeof bad[0] - 2) len = 11; /* include the NUL */
            int32_t st = zgn_fasta_parse(U8(bad[k].text), len, NULL, &f);
            CHECK(st == ZGN_E_FASTA && f == NULL, "FASTA refused: %s (%s)", bad[k].why,
                  zgn_status_name(st));
            CHECK(strstr(zt_last_err(), "line ") != NULL || st != ZGN_E_FASTA ||
                      strstr(zt_last_err(), "repeats") != NULL,
                  "the refusal names a line (%s)", bad[k].why);
        }
    }
    {
        char id[80], text[256];
        memset(id, 'i', 65);
        id[65] = 0;
        snprintf(text, sizeof text, ">%s\nAC\n", id);
        CHECK_RC(fasta(text, &f), ZGN_E_FASTA);
        id[64] = 0;
        snprintf(text, sizeof text, ">%s\nAC\n", id);
        CHECK_RC(fasta(text, &f), ZXV_OK);
        zgn_fasta_destroy(f);
        f = NULL;
    }
    {
        /* limits */
        zgn_fasta_limits lim = {2, 4, 64};
        const char *three = ">a\nAC\n>b\nAC\n>c\nAC\n";
        CHECK_RC(zgn_fasta_parse(U8(three), strlen(three), &lim, &f), ZGN_E_LIMIT);
        lim.max_records = 3;
        CHECK_RC(zgn_fasta_parse(U8(three), strlen(three), &lim, &f), ZXV_OK);
        zgn_fasta_destroy(f);
        f = NULL;
        CHECK_RC(zgn_fasta_parse(U8(">a\nACG\nTA\n"), 10, &lim, &f), ZGN_E_LIMIT);
        CHECK_RC(zgn_fasta_parse(U8(">a\nACG\nT\n"), 9, &lim, &f), ZXV_OK);
        zgn_fasta_destroy(f);
        f = NULL;
        lim.max_bytes = 8;
        CHECK_RC(zgn_fasta_parse(U8(">a\nACG\nT\n"), 9, &lim, &f), ZGN_E_LIMIT);
        lim.max_records = 65537;
        CHECK_RC(zgn_fasta_parse(U8(">a\nA\n"), 5, &lim, &f), ZXV_E_ARG);
        CHECK_RC(zgn_fasta_parse(NULL, 5, NULL, &f), ZXV_E_ARG);
        CHECK_RC(zgn_fasta_parse(U8(">a\nA\n"), 5, NULL, NULL), ZXV_E_ARG);
    }
    {
        /* a long description at the bound */
        char text[400];
        memset(text, 'd', sizeof text);
        memcpy(text, ">a ", 3);
        memcpy(text + 3 + 200, "\nAC\n", 5);
        CHECK_RC(fasta(text, &f), ZXV_OK);
        zgn_fasta_destroy(f);
        f = NULL;
        memset(text, 'd', sizeof text);
        memcpy(text, ">a ", 3);
        memcpy(text + 3 + 201, "\nAC\n", 5);
        CHECK_RC(fasta(text, &f), ZGN_E_FASTA);
    }
    zgn_fasta_destroy(NULL);
    CHECK_RC(zgn_fasta_count(NULL, &n), ZXV_E_ARG);
}

/* ===== signed records ===== */

static void test_records(void) {
    zt_env e;
    zind_signer *lab = NULL, *other = NULL;
    zind_chain *c = NULL, *c2 = NULL;
    zgn_matrix m, dna;
    zgn_params p = {ZGN_LOCAL, 11, 1, 0};
    zgn_result r;
    zgn_claim cl;
    const char *ha = "MVLSPADKTNVKAAWGKVGAHAGEYGAEALERMFLSFPTTKTYFPHFDLSHGSAQVKGHGKKVADALTNAVAHVDDMPNALSALSDLHAHKLRVDPVNFKLLSHCLLVTLAAHLPAEFTPAVHASLDKFLASVSTVLTSKYR";
    const char *hb = "MVHLTPEEKSAVTALWGKVNVDEVGGEALGRLLVVYPWTQRFFESFGDLSTPDAVMGNPKVKAHGKKVLGAFSDGLAHLDNLKGTFATLSELHCDKLHVDPENFRLLGNVLVCVLAHHFGKEFTPPVQAAYQKVVAGVANALAHKYH";
    zgn_seq a = {"HBA_HUMAN", U8(ha), (uint32_t) strlen(ha)};
    zgn_seq b = {"HBB_HUMAN", U8(hb), (uint32_t) strlen(hb)};
    const uint8_t *trust[1], *wrong[1];
    uint64_t seq = 99;
    uint8_t *buf = NULL;
    size_t len = 0;

    if (zt_env_open(&e, NULL) != 0) {
        CHECK(0, "env");
        return;
    }
    lab = zt_signer(e.cap, 41);
    other = zt_signer(e.cap, 42);
    trust[0] = zind_signer_pk(lab);
    wrong[0] = zind_signer_pk(other);
    zgn_matrix_blosum62(&m);
    zgn_matrix_dna(1, -1, &dna);
    CHECK_RC(zind_chain_create(ZGN_DOMAIN, 64, &c), ZXV_OK);
    CHECK_RC(zind_chain_create("other/domain", 64, &c2), ZXV_OK);
    CHECK_RC(zgn_align(&m, &p, a.res, a.len, b.res, b.len, &r), ZXV_OK);
    CHECK_RC(zgn_record_append(e.cap, c2, lab, &m, &p, &a, &b, &r, NULL), ZXV_E_ARG);
    CHECK_RC(zgn_record_append(e.cap, c, lab, &m, &p, &a, &b, &r, &seq), ZXV_OK);
    CHECK(seq == 0, "first record");
    CHECK_RC(zgn_record_verify(e.cap, c, 0, trust, 1, &m, &a, &b, 0, &cl), ZXV_OK);
    CHECK(cl.score == r.score && cl.mode == ZGN_LOCAL && cl.gap_open == 11 && cl.len_a == a.len &&
              strcmp(cl.id_a, "HBA_HUMAN") == 0 && strcmp(cl.matrix_name, "BLOSUM62") == 0,
          "the claim carries the result");
    /* refusals at append: a result that does not belong */
    {
        zgn_result bad = r;
        bad.score++;
        CHECK_RC(zgn_record_append(e.cap, c, lab, &m, &p, &a, &b, &bad, NULL), ZGN_E_MISMATCH);
        bad = r;
        bad.matches--;
        bad.mismatches++;
        CHECK_RC(zgn_record_append(e.cap, c, lab, &m, &p, &a, &b, &bad, NULL), ZGN_E_MISMATCH);
        bad = r;
        bad.a_start++;
        CHECK_RC(zgn_record_append(e.cap, c, lab, &m, &p, &a, &b, &bad, NULL), ZGN_E_MISMATCH);
        CHECK_RC(zgn_record_append(e.cap, c, lab, &m, &p, &b, &a, &r, NULL), ZGN_E_MISMATCH);
        bad = r;
        bad.columns--;
        CHECK_RC(zgn_record_append(e.cap, c, lab, &m, &p, &a, &b, &bad, NULL), ZGN_E_MISMATCH);
        bad.a_end = a.len + 1;
        CHECK_RC(zgn_record_append(e.cap, c, lab, &m, &p, &a, &b, &bad, NULL), ZGN_E_MISMATCH);
        p.mode = ZGN_GLOBAL; /* a local result is not a global alignment */
        CHECK_RC(zgn_record_append(e.cap, c, lab, &m, &p, &a, &b, &r, NULL), ZGN_E_MISMATCH);
        p.mode = ZGN_LOCAL;
        p.gap_open = 10; /* other parameters rescore differently */
        CHECK_RC(zgn_record_append(e.cap, c, lab, &m, &p, &a, &b, &r, NULL), ZGN_E_MISMATCH);
        p.gap_open = 11;
        {
            zgn_seq badid = a;
            badid.id = "has space\x01";
            CHECK_RC(zgn_record_append(e.cap, c, lab, &m, &p, &badid, &b, &r, NULL), ZXV_E_ARG);
        }
        CHECK_RC(zgn_record_append(NULL, c, lab, &m, &p, &a, &b, &r, NULL), ZXV_E_ARG);
    }
    /* a global record too, then verify both */
    {
        zgn_result g;
        p.mode = ZGN_GLOBAL;
        CHECK_RC(zgn_align(&m, &p, a.res, a.len, b.res, b.len, &g), ZXV_OK);
        CHECK_RC(zgn_record_append(e.cap, c, lab, &m, &p, &a, &b, &g, &seq), ZXV_OK);
        CHECK(seq == 1, "second record");
        zgn_result_free(&g);
        p.mode = ZGN_LOCAL;
    }
    CHECK_RC(zgn_record_verify(e.cap, c, 1, trust, 1, &m, &a, &b, 0, &cl), ZXV_OK);
    CHECK(cl.mode == ZGN_GLOBAL, "record 1 is global");
    CHECK_RC(zgn_record_verify(e.cap, c, 2, trust, 1, &m, &a, &b, 0, NULL), ZXV_E_NOT_FOUND);
    CHECK_RC(zgn_record_verify(e.cap, c, 0, wrong, 1, &m, &a, &b, 0, NULL), ZIND_E_UNTRUSTED);
    CHECK_RC(zgn_record_verify(e.cap, c, 0, trust, 1, &dna, &a, &b, 0, NULL), ZGN_E_MATRIX);
    CHECK_RC(zgn_record_verify(e.cap, c, 0, trust, 1, &m, &b, &a, 0, NULL), ZGN_E_MISMATCH);
    {
        zgn_seq a2 = a;
        a2.len--;
        CHECK_RC(zgn_record_verify(e.cap, c, 0, trust, 1, &m, &a2, &b, 0, NULL), ZGN_E_MISMATCH);
        a2.id = "renamed"; /* the ID is a label, not hashed */
        a2.len++;
        CHECK_RC(zgn_record_verify(e.cap, c, 0, trust, 1, &m, &a2, &b, 0, NULL), ZXV_OK);
    }
    CHECK_RC(zgn_record_verify(e.cap, c, 0, trust, 1, &m, &a, &b, 1000, NULL), ZGN_E_LIMIT);
    CHECK_RC(zgn_record_verify(e.cap, c, 0, trust, 1, &m, &a, &b, (uint64_t) ZGN_CELLS_MAX + 1,
                               NULL),
             ZXV_E_ARG);
    CHECK_RC(zgn_record_verify(e.cap, c2, 0, trust, 1, &m, &a, &b, 0, NULL), ZXV_E_ARG);
    /* a validly signed record with a false score: recomputing exposes it */
    {
        zind_record rec;
        uint8_t forged[512];
        zgn_claim fc;
        CHECK_RC(zind_chain_at(c, 0, &rec), ZXV_OK);
        memcpy(forged, rec.payload, rec.payload_len);
        CHECK_RC(zgn_claim_decode(forged, rec.payload_len, &fc), ZXV_OK);
        {
            /* the score field sits after the fixed prefix; find it by value */
            size_t off = 0;
            for (size_t k = 0; k + 8 <= rec.payload_len; k++) {
                uint64_t v = 0;
                for (int q = 7; q >= 0; q--) v = (v << 8) | forged[k + q];
                if (v == (uint64_t) fc.score) {
                    off = k;
                    break;
                }
            }
            forged[off] ^= 1;
        }
        CHECK_RC(zind_chain_append(e.cap, c, lab, ZGN_REC_RESULT, forged, rec.payload_len, NULL,
                                   NULL),
                 ZXV_OK);
        CHECK_RC(zgn_record_verify(e.cap, c, 2, trust, 1, &m, &a, &b, 0, &fc), ZGN_E_MISMATCH);
        CHECK(fc.score != r.score, "the forged claim is decoded and refused");
        /* a record of another kind, and a malformed payload */
        CHECK_RC(zind_chain_append(e.cap, c, lab, 9, forged, rec.payload_len, NULL, NULL), ZXV_OK);
        CHECK_RC(zgn_record_verify(e.cap, c, 3, trust, 1, &m, &a, &b, 0, NULL), ZXV_E_FORMAT);
        CHECK_RC(zind_chain_append(e.cap, c, lab, ZGN_REC_RESULT, forged, rec.payload_len - 1, NULL,
                                   NULL),
                 ZXV_OK);
        CHECK_RC(zgn_record_verify(e.cap, c, 4, trust, 1, &m, &a, &b, 0, NULL), ZXV_E_FORMAT);
        for (size_t cut = 0; cut < rec.payload_len; cut += 7)
            CHECK_RC(zgn_claim_decode(forged, cut, &fc), ZXV_E_FORMAT);
        forged[0] = 2;
        CHECK_RC(zgn_claim_decode(forged, rec.payload_len, &fc), ZXV_E_FORMAT);
        forged[0] = 1;
        forged[1] = 7;
        CHECK_RC(zgn_claim_decode(forged, rec.payload_len, &fc), ZXV_E_FORMAT);
    }
    /* export: verify on "another machine" from bytes; flip bits; truncate */
    CHECK_RC(zind_chain_export(c, NULL, 0, &len), ZXV_E_BUFFER);
    buf = (uint8_t *) malloc(len);
    CHECK_RC(zind_chain_export(c, buf, len, &len), ZXV_OK);
    CHECK_RC(zgn_verify_export(e.cap, buf, len, 0, trust, 1, &m, &a, &b, 0, &cl), ZXV_OK);
    CHECK_RC(zgn_verify_export(e.cap, buf, len, 1, trust, 1, &m, &a, &b, 0, &cl), ZXV_OK);
    CHECK_RC(zgn_verify_export(e.cap, buf, len - 1, 0, trust, 1, &m, &a, &b, 0, NULL),
             ZXV_E_FORMAT);
    for (int k = 0; k < 48; k++) {
        size_t off = 4 + (size_t) k * (len - 5) / 47;
        int32_t st;
        buf[off] ^= 0x10;
        st = zgn_verify_export(e.cap, buf, len, 0, trust, 1, &m, &a, &b, 0, NULL);
        CHECK(st != ZXV_OK, "a bit flipped at %zu of %zu is refused (%s)", off, len,
              zgn_status_name(st));
        buf[off] ^= 0x10;
    }
    CHECK_RC(zgn_verify_export(e.cap, buf, len, 0, trust, 1, &m, &a, &b, 0, NULL), ZXV_OK);
    CHECK_RC(zgn_verify_export(NULL, buf, len, 0, trust, 1, &m, &a, &b, 0, NULL), ZXV_E_ARG);
    {
        /* without CRYPTO_SIGN nothing verifies */
        zxv_cap *weak = NULL;
        CHECK_RC(zxv_cap_attenuate(e.cap, ZXV_CAP_ATTEST, &weak), ZXV_OK);
        CHECK_RC(zgn_record_verify(weak, c, 0, trust, 1, &m, &a, &b, 0, NULL), ZXV_E_CAP);
        CHECK_RC(zgn_record_append(weak, c, lab, &m, &p, &a, &b, &r, NULL), ZXV_E_CAP);
        zxv_cap_destroy(weak);
    }
    free(buf);
    zgn_result_free(&r);
    zind_chain_destroy(c);
    zind_chain_destroy(c2);
    zind_signer_destroy(lab);
    zind_signer_destroy(other);
    zt_env_close(&e);
}

int main(void) {
    test_matrices();
    test_refusals();
    test_textbook();
    test_tiebreak();
    test_overflow();
    test_properties();
    test_fasta();
    test_records();
    return zt_report("test_genome");
}
