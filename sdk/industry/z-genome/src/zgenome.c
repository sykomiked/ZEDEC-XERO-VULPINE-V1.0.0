/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zgenome.c — z-genome: substitution matrices, Needleman-Wunsch and
 * Smith-Waterman with affine gaps in checked int64 arithmetic, the fixed
 * tie-break of zgenome.h, a strict FASTA parser, and signed result records
 * that another machine re-verifies by recomputing. Integer code only
 * (R11). */
#include <stdlib.h>
#include <string.h>

#include "zgenome.h"

#define NEG INT64_MIN /* "no path": never a score (scores never reach it) */

const char *zgn_status_name(int32_t status) {
    switch (status) {
    case ZGN_E_SEQ: return "ZGN_E_SEQ";
    case ZGN_E_FASTA: return "ZGN_E_FASTA";
    case ZGN_E_LIMIT: return "ZGN_E_LIMIT";
    case ZGN_E_OVERFLOW: return "ZGN_E_OVERFLOW";
    case ZGN_E_MATRIX: return "ZGN_E_MATRIX";
    case ZGN_E_MISMATCH: return "ZGN_E_MISMATCH";
    default: return zind_status_name(status);
    }
}

/* ===== checked int64 =====
 * Results are kept above INT64_MIN, which is reserved for NEG. */

static int add_ck(int64_t a, int64_t b, int64_t *out) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN + 1 - b)) return -1;
    *out = a + b;
    return 0;
}

/* a - pen, pen >= 0 */
static int sub_ck(int64_t a, int64_t pen, int64_t *out) {
    if (a < INT64_MIN + 1 + pen) return -1;
    *out = a - pen;
    return 0;
}

/* ===== matrices ===== */

static const char k_b62_alpha[] = "ARNDCQEGHILKMFPSTWYVBZX*";
static const int8_t k_b62[24][24] = {
    /*        A   R   N   D   C   Q   E   G   H   I   L   K   M   F   P   S   T   W   Y   V   B   Z   X   * */
    /* A */ {4, -1, -2, -2, 0, -1, -1, 0, -2, -1, -1, -1, -1, -2, -1, 1, 0, -3, -2, 0, -2, -1, 0, -4},
    /* R */ {-1, 5, 0, -2, -3, 1, 0, -2, 0, -3, -2, 2, -1, -3, -2, -1, -1, -3, -2, -3, -1, 0, -1, -4},
    /* N */ {-2, 0, 6, 1, -3, 0, 0, 0, 1, -3, -3, 0, -2, -3, -2, 1, 0, -4, -2, -3, 3, 0, -1, -4},
    /* D */ {-2, -2, 1, 6, -3, 0, 2, -1, -1, -3, -4, -1, -3, -3, -1, 0, -1, -4, -3, -3, 4, 1, -1, -4},
    /* C */ {0, -3, -3, -3, 9, -3, -4, -3, -3, -1, -1, -3, -1, -2, -3, -1, -1, -2, -2, -1, -3, -3, -2, -4},
    /* Q */ {-1, 1, 0, 0, -3, 5, 2, -2, 0, -3, -2, 1, 0, -3, -1, 0, -1, -2, -1, -2, 0, 3, -1, -4},
    /* E */ {-1, 0, 0, 2, -4, 2, 5, -2, 0, -3, -3, 1, -2, -3, -1, 0, -1, -3, -2, -2, 1, 4, -1, -4},
    /* G */ {0, -2, 0, -1, -3, -2, -2, 6, -2, -4, -4, -2, -3, -3, -2, 0, -2, -2, -3, -3, -1, -2, -1, -4},
    /* H */ {-2, 0, 1, -1, -3, 0, 0, -2, 8, -3, -3, -1, -2, -1, -2, -1, -2, -2, 2, -3, 0, 0, -1, -4},
    /* I */ {-1, -3, -3, -3, -1, -3, -3, -4, -3, 4, 2, -3, 1, 0, -3, -2, -1, -3, -1, 3, -3, -3, -1, -4},
    /* L */ {-1, -2, -3, -4, -1, -2, -3, -4, -3, 2, 4, -2, 2, 0, -3, -2, -1, -2, -1, 1, -4, -3, -1, -4},
    /* K */ {-1, 2, 0, -1, -3, 1, 1, -2, -1, -3, -2, 5, -1, -3, -1, 0, -1, -3, -2, -2, 0, 1, -1, -4},
    /* M */ {-1, -1, -2, -3, -1, 0, -2, -3, -2, 1, 2, -1, 5, 0, -2, -1, -1, -1, -1, 1, -3, -1, -1, -4},
    /* F */ {-2, -3, -3, -3, -2, -3, -3, -3, -1, 0, 0, -3, 0, 6, -4, -2, -2, 1, 3, -1, -3, -3, -1, -4},
    /* P */ {-1, -2, -2, -1, -3, -1, -1, -2, -2, -3, -3, -1, -2, -4, 7, -1, -1, -4, -3, -2, -2, -1, -2, -4},
    /* S */ {1, -1, 1, 0, -1, 0, 0, 0, -1, -2, -2, 0, -1, -2, -1, 4, 1, -3, -2, -2, 0, 0, 0, -4},
    /* T */ {0, -1, 0, -1, -1, -1, -1, -2, -2, -1, -1, -1, -1, -2, -1, 1, 5, -2, -2, 0, -1, -1, 0, -4},
    /* W */ {-3, -3, -4, -4, -2, -2, -3, -2, -2, -3, -2, -3, -1, 1, -4, -3, -2, 11, 2, -3, -4, -3, -2, -4},
    /* Y */ {-2, -2, -2, -3, -2, -1, -2, -3, 2, -1, -1, -2, -1, 3, -3, -2, -2, 2, 7, -1, -3, -2, -1, -4},
    /* V */ {0, -3, -3, -3, -1, -2, -2, -3, -3, 3, 1, -2, 1, -1, -2, -2, 0, -3, -1, 4, -3, -2, -1, -4},
    /* B */ {-2, -1, 3, 4, -3, 0, 1, -1, 0, -3, -4, 0, -3, -3, -2, 0, -1, -4, -3, -3, 4, 1, -1, -4},
    /* Z */ {-1, 0, 0, 1, -3, 3, 4, -2, 0, -3, -3, 1, -1, -3, -1, 0, -1, -3, -2, -2, 1, 4, -1, -4},
    /* X */ {0, -1, -1, -1, -2, -1, -1, -1, -1, -1, -1, -1, -1, -1, -2, 0, 0, -2, -1, -1, -1, -1, -1, -4},
    /* * */ {-4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, -4, 1},
};

static int name_ok(const char *s, size_t max) {
    size_t n;
    if (!s) return 0;
    for (n = 0; s[n]; n++)
        if (n >= max || (unsigned char) s[n] < 0x20 || (unsigned char) s[n] > 0x7e) return 0;
    return 1;
}

int32_t zgn_matrix_check(const zgn_matrix *m) {
    uint8_t seen[256] = {0};
    if (!m) return ZXV_E_ARG;
    if (m->n == 0 || m->n > ZGN_ALPHA_MAX || memchr(m->alphabet, 0, ZGN_ALPHA_MAX + 1) == NULL ||
        strlen(m->alphabet) != m->n || memchr(m->name, 0, ZGN_NAME_MAX + 1) == NULL ||
        !name_ok(m->name, ZGN_NAME_MAX)) {
        zind_set_error("zgn_matrix: 1..32 alphabet symbols with n = strlen(alphabet) and a "
                       "printable name of up to 23 bytes");
        return ZGN_E_MATRIX;
    }
    for (uint32_t i = 0; i < m->n; i++) {
        unsigned char c = (unsigned char) m->alphabet[i];
        if (!((c >= 'A' && c <= 'Z') || c == '*') || seen[c]) {
            zind_set_error("zgn_matrix: alphabet symbol %u is not a distinct one of [A-Z*]", i);
            return ZGN_E_MATRIX;
        }
        seen[c] = 1;
        for (uint32_t j = 0; j < m->n; j++)
            if (m->score[i][j] == INT64_MIN) {
                zind_set_error("zgn_matrix: INT64_MIN is not a score (entry %u,%u)", i, j);
                return ZGN_E_MATRIX;
            }
    }
    return ZXV_OK;
}

static void matrix_init(zgn_matrix *m, const char *name, const char *alpha) {
    memset(m, 0, sizeof *m);
    memcpy(m->name, name, strlen(name) + 1);
    memcpy(m->alphabet, alpha, strlen(alpha) + 1);
    m->n = (uint8_t) strlen(alpha);
}

int32_t zgn_matrix_blosum62(zgn_matrix *out) {
    if (!out) return ZXV_E_ARG;
    matrix_init(out, "BLOSUM62", k_b62_alpha);
    for (int i = 0; i < 24; i++)
        for (int j = 0; j < 24; j++) out->score[i][j] = k_b62[i][j];
    return ZXV_OK;
}

int32_t zgn_matrix_dna(int64_t match, int64_t mismatch, zgn_matrix *out) {
    if (!out) return ZXV_E_ARG;
    if (match == INT64_MIN || mismatch == INT64_MIN) {
        zind_set_error("zgn_matrix_dna: INT64_MIN is not a score");
        return ZGN_E_MATRIX;
    }
    matrix_init(out, "DNA", "ACGTN");
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 5; j++) out->score[i][j] = (i == j && i < 4) ? match : mismatch;
    return ZXV_OK;
}

int32_t zgn_matrix_custom(const char *name, const char *alphabet, const int64_t *scores,
                          zgn_matrix *out) {
    size_t n;
    if (!out || !alphabet || !scores) return ZXV_E_ARG;
    n = strlen(alphabet);
    if (!name || strlen(name) > ZGN_NAME_MAX || n == 0 || n > ZGN_ALPHA_MAX) {
        zind_set_error("zgn_matrix_custom: name of up to 23 bytes and 1..32 symbols");
        return ZGN_E_MATRIX;
    }
    matrix_init(out, name, alphabet);
    for (size_t i = 0; i < n; i++)
        for (size_t j = 0; j < n; j++) out->score[i][j] = scores[i * n + j];
    return zgn_matrix_check(out);
}

int32_t zgn_matrix_digest(const zgn_matrix *m, uint8_t out[32]) {
    uint8_t buf[13 + 1 + ZGN_ALPHA_MAX + ZGN_ALPHA_MAX * ZGN_ALPHA_MAX * 8];
    zind_wr w;
    int32_t st;
    if (!m || !out) return ZXV_E_ARG;
    st = zgn_matrix_check(m);
    if (st != ZXV_OK) return st;
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_bytes(&w, "zgn/matrix/v1", 13);
    zind_wr_u8(&w, m->n);
    zind_wr_bytes(&w, m->alphabet, m->n);
    for (uint32_t i = 0; i < m->n; i++)
        for (uint32_t j = 0; j < m->n; j++) zind_wr_u64(&w, (uint64_t) m->score[i][j]);
    if (w.overflow) return ZXV_E_INTERNAL;
    return zxv_sha3_256(buf, w.len, out);
}

int zgn_matrix_symmetric(const zgn_matrix *m) {
    if (!m || zgn_matrix_check(m) != ZXV_OK) return 0;
    for (uint32_t i = 0; i < m->n; i++)
        for (uint32_t j = 0; j < i; j++)
            if (m->score[i][j] != m->score[j][i]) return 0;
    return 1;
}

static void matrix_index(const zgn_matrix *m, int8_t idx[256]) {
    memset(idx, -1, 256);
    for (uint32_t i = 0; i < m->n; i++) idx[(unsigned char) m->alphabet[i]] = (int8_t) i;
}

/* ===== sequences ===== */

int32_t zgn_seq_digest(const uint8_t *res, uint32_t len, uint8_t out[32]) {
    uint8_t stackbuf[512], *buf = stackbuf;
    size_t total = 10 + 4 + (size_t) len;
    int32_t st;
    if ((!res && len) || !out) return ZXV_E_ARG;
    if (len > ZGN_SEQ_MAX) return ZGN_E_LIMIT;
    if (total > sizeof stackbuf) {
        buf = (uint8_t *) malloc(total);
        if (!buf) return ZXV_E_NOMEM;
    }
    memcpy(buf, "zgn/seq/v1", 10);
    for (int i = 0; i < 4; i++) buf[10 + i] = (uint8_t) (len >> (8 * i));
    if (len) memcpy(buf + 14, res, len);
    st = zxv_sha3_256(buf, total, out);
    if (buf != stackbuf) free(buf);
    return st;
}

/* ===== alignment ===== */

static int32_t check_params(const zgn_params *p, uint64_t *out_max) {
    if (!p) return ZXV_E_ARG;
    if ((p->mode != ZGN_GLOBAL && p->mode != ZGN_LOCAL) || p->gap_open < 0 || p->gap_extend < 0 ||
        p->max_cells > ZGN_CELLS_MAX) {
        zind_set_error("zgn_params: mode GLOBAL or LOCAL, gap_open and gap_extend >= 0, "
                       "max_cells <= 2^28");
        return ZXV_E_ARG;
    }
    if (out_max) *out_max = p->max_cells ? p->max_cells : ZGN_CELLS_DEFAULT;
    return ZXV_OK;
}

void zgn_result_free(zgn_result *r) {
    if (!r) return;
    free(r->a_row);
    free(r->b_row);
    memset(r, 0, sizeof *r);
}

/* The best of three candidates (NEG = absent), preferring the lowest index
 * on ties: M (0), X (1), Y (2). */
static int64_t best3(int64_t c0, int64_t c1, int64_t c2, uint8_t *src) {
    int64_t b = c0;
    *src = 0;
    if (c1 != NEG && (b == NEG || c1 > b)) {
        b = c1;
        *src = 1;
    }
    if (c2 != NEG && (b == NEG || c2 > b)) {
        b = c2;
        *src = 2;
    }
    return b;
}

/* c - pen, NEG stays NEG. -1 on overflow. */
static int pen_of(int64_t c, int64_t pen, int64_t *out) {
    if (c == NEG) {
        *out = NEG;
        return 0;
    }
    if (sub_ck(c, pen, out) == 0) return 0;
    *out = NEG; /* refused: the caller stops at the end of the row */
    return 1;
}

/* Count the columns of two rows; -1 if a column is two gaps. */
static int count_rows(const char *ar, const char *br, uint32_t cols, zgn_result *r) {
    r->matches = r->mismatches = r->gap_columns = r->gap_opens = 0;
    for (uint32_t k = 0; k < cols; k++) {
        int ga = ar[k] == '-', gb = br[k] == '-';
        if (ga && gb) return -1;
        if (ga || gb) {
            r->gap_columns++;
            if (ga && (k == 0 || ar[k - 1] != '-')) r->gap_opens++;
            if (gb && (k == 0 || br[k - 1] != '-')) r->gap_opens++;
        } else if (ar[k] == br[k]) {
            r->matches++;
        } else {
            r->mismatches++;
        }
    }
    return 0;
}

static int32_t score_rows(const zgn_matrix *m, const int8_t idx[256], int64_t open,
                          int64_t ext, const char *ar, const char *br, size_t cols,
                          int64_t *out) {
    int64_t s = 0, oe;
    if (add_ck(open, ext, &oe)) return ZGN_E_OVERFLOW;
    for (size_t k = 0; k < cols; k++) {
        int ga = ar[k] == '-', gb = br[k] == '-';
        int ok;
        if (ga && gb) return ZXV_E_ARG;
        if (ga) ok = !sub_ck(s, (k == 0 || ar[k - 1] != '-') ? oe : ext, &s);
        else if (gb) ok = !sub_ck(s, (k == 0 || br[k - 1] != '-') ? oe : ext, &s);
        else {
            int ia = idx[(unsigned char) ar[k]], ib = idx[(unsigned char) br[k]];
            if (ia < 0 || ib < 0) return ZGN_E_SEQ;
            ok = !add_ck(s, m->score[ia][ib], &s);
        }
        if (!ok) return ZGN_E_OVERFLOW;
    }
    *out = s;
    return ZXV_OK;
}

int32_t zgn_score_rows(const zgn_matrix *m, const zgn_params *p, const char *a_row,
                       const char *b_row, int64_t *out_score) {
    int8_t idx[256];
    size_t n;
    int32_t st;
    if (!m || !a_row || !b_row || !out_score) return ZXV_E_ARG;
    st = zgn_matrix_check(m);
    if (st == ZXV_OK) st = check_params(p, NULL);
    if (st != ZXV_OK) return st;
    n = strlen(a_row);
    if (strlen(b_row) != n) {
        zind_set_error("zgn_score_rows: the rows have different lengths");
        return ZXV_E_ARG;
    }
    matrix_index(m, idx);
    st = score_rows(m, idx, p->gap_open, p->gap_extend, a_row, b_row, n, out_score);
    if (st == ZXV_E_ARG) zind_set_error("zgn_score_rows: a column holds two gaps");
    if (st == ZGN_E_SEQ) zind_set_error("zgn_score_rows: a symbol is not in the alphabet");
    if (st == ZGN_E_OVERFLOW) zind_set_error("zgn_score_rows: the score overflows int64");
    return st;
}

static int32_t check_residues(const int8_t idx[256], const uint8_t *s, uint32_t n,
                              const char *which) {
    for (uint32_t i = 0; i < n; i++)
        if (idx[s[i]] < 0) {
            zind_set_error("zgn_align: residue %u of %s (byte 0x%02x) is not in the matrix "
                           "alphabet",
                           i, which, s[i]);
            return ZGN_E_SEQ;
        }
    return ZXV_OK;
}

int32_t zgn_align(const zgn_matrix *m, const zgn_params *p, const uint8_t *a, uint32_t na,
                  const uint8_t *b, uint32_t nb, zgn_result *out) {
    int8_t idx[256];
    uint64_t max_cells = 0, cells;
    int64_t oe = 0, ext, *rows = NULL, *Mp, *Xp, *Yp, *Mc, *Xc, *Yc, best = 0;
    uint8_t *tb = NULL;
    size_t W;
    uint32_t bi = 0, bj = 0;
    int local, ovf = 0;
    int32_t st;
    char *ar = NULL, *br = NULL;

    if (!out) return ZXV_E_ARG;
    memset(out, 0, sizeof *out);
    if (!m || (!a && na) || (!b && nb)) return ZXV_E_ARG;
    st = zgn_matrix_check(m);
    if (st == ZXV_OK) st = check_params(p, &max_cells);
    if (st != ZXV_OK) return st;
    if (na > ZGN_SEQ_MAX || nb > ZGN_SEQ_MAX) {
        zind_set_error("zgn_align: a sequence is over %u residues", ZGN_SEQ_MAX);
        return ZGN_E_LIMIT;
    }
    cells = ((uint64_t) na + 1) * ((uint64_t) nb + 1);
    if (cells > max_cells) {
        zind_set_error("zgn_align: %llu cells is over the bound of %llu (max_cells)",
                       (unsigned long long) cells, (unsigned long long) max_cells);
        return ZGN_E_LIMIT;
    }
    matrix_index(m, idx);
    st = check_residues(idx, a, na, "a");
    if (st == ZXV_OK) st = check_residues(idx, b, nb, "b");
    if (st != ZXV_OK) return st;
    if (add_ck(p->gap_open, p->gap_extend, &oe)) {
        zind_set_error("zgn_align: gap_open + gap_extend overflows int64");
        return ZGN_E_OVERFLOW;
    }
    ext = p->gap_extend;
    local = p->mode == ZGN_LOCAL;
    W = (size_t) nb + 1;

    rows = (int64_t *) malloc(6 * W * sizeof *rows);
    tb = (uint8_t *) malloc((size_t) cells);
    if (!rows || !tb) {
        st = ZXV_E_NOMEM;
        goto done;
    }
    Mp = rows;
    Xp = rows + W;
    Yp = rows + 2 * W;
    Mc = rows + 3 * W;
    Xc = rows + 4 * W;
    Yc = rows + 5 * W;

    /* row 0 */
    Mp[0] = local ? NEG : 0;
    Xp[0] = Yp[0] = NEG;
    tb[0] = 0;
    for (size_t j = 1; j < W; j++) {
        int64_t c0 = NEG, c1 = NEG, c2 = NEG;
        uint8_t ys = 0;
        Mp[j] = Xp[j] = NEG;
        if (!local) {
            ovf |= pen_of(Mp[j - 1], oe, &c0);
            ovf |= pen_of(Xp[j - 1], oe, &c1);
            ovf |= pen_of(Yp[j - 1], ext, &c2);
        }
        Yp[j] = best3(c0, c1, c2, &ys);
        tb[j] = (uint8_t) (ys << 4);
    }
    if (ovf) goto overflow;

    for (size_t i = 1; i <= na; i++) {
        int ia = idx[a[i - 1]];
        uint8_t *trow = tb + i * W;
        int64_t *t;
        { /* column 0 */
            int64_t c0 = NEG, c1 = NEG, c2 = NEG;
            uint8_t xs = 0;
            if (!local) {
                ovf |= pen_of(Mp[0], oe, &c0);
                ovf |= pen_of(Xp[0], ext, &c1);
                ovf |= pen_of(Yp[0], oe, &c2);
            }
            Mc[0] = Yc[0] = NEG;
            Xc[0] = best3(c0, c1, c2, &xs);
            trow[0] = (uint8_t) (xs << 2);
        }
        for (size_t j = 1; j < W; j++) {
            int64_t c0 = NEG, c1 = NEG, c2 = NEG, pb, sub = m->score[ia][idx[b[j - 1]]];
            uint8_t ms = 0, xs = 0, ys = 0;
            /* M: residue against residue */
            pb = best3(Mp[j - 1], Xp[j - 1], Yp[j - 1], &ms);
            if (local && (pb == NEG || pb <= 0)) {
                ms = 3; /* start fresh */
                Mc[j] = sub;
            } else if (pb == NEG) {
                Mc[j] = NEG;
            } else if (add_ck(pb, sub, &Mc[j])) {
                Mc[j] = NEG;
                ovf = 1;
            }
            /* X: a[i-1] against a gap (from the row above) */
            ovf |= pen_of(Mp[j], oe, &c0);
            ovf |= pen_of(Xp[j], ext, &c1);
            ovf |= pen_of(Yp[j], oe, &c2);
            Xc[j] = best3(c0, c1, c2, &xs);
            /* Y: a gap against b[j-1] (from the left) */
            ovf |= pen_of(Mc[j - 1], oe, &c0);
            ovf |= pen_of(Xc[j - 1], oe, &c1);
            ovf |= pen_of(Yc[j - 1], ext, &c2);
            Yc[j] = best3(c0, c1, c2, &ys);
            trow[j] = (uint8_t) (ms | (xs << 2) | (ys << 4));
            if (local && Mc[j] != NEG && Mc[j] > best) {
                best = Mc[j];
                bi = (uint32_t) i;
                bj = (uint32_t) j;
            }
        }
        if (ovf) goto overflow;
        t = Mp, Mp = Mc, Mc = t;
        t = Xp, Xp = Xc, Xc = t;
        t = Yp, Yp = Yc, Yc = t;
    }

    {
        /* traceback; the last computed row is in Mp/Xp/Yp */
        size_t i, j, k = 0, cap = (size_t) na + nb + 1;
        int state;
        uint8_t s0 = 0;
        ar = (char *) malloc(cap);
        br = (char *) malloc(cap);
        if (!ar || !br) {
            st = ZXV_E_NOMEM;
            goto done;
        }
        if (local) {
            i = bi;
            j = bj;
            state = best > 0 ? 0 : -1;
            out->score = best > 0 ? best : 0;
            out->a_end = bi;
            out->b_end = bj;
        } else {
            i = na;
            j = nb;
            out->score = best3(Mp[nb], Xp[nb], Yp[nb], &s0);
            state = (na == 0 && nb == 0) ? -1 : s0;
            if (na == 0 && nb == 0) out->score = 0;
            out->a_end = na;
            out->b_end = nb;
        }
        while (state >= 0 && (i > 0 || j > 0)) {
            uint8_t ptr = tb[i * W + j];
            if (state == 0) {
                if (i == 0 || j == 0) break;
                ar[k] = (char) a[i - 1];
                br[k] = (char) b[j - 1];
                k++;
                state = ptr & 3;
                i--;
                j--;
                if (state == 3) break; /* the local alignment starts here */
            } else if (state == 1) {
                if (i == 0) break;
                ar[k] = (char) a[i - 1];
                br[k] = '-';
                k++;
                state = (ptr >> 2) & 3;
                i--;
            } else {
                if (j == 0) break;
                ar[k] = '-';
                br[k] = (char) b[j - 1];
                k++;
                state = (ptr >> 4) & 3;
                j--;
            }
            if (state > 2 && state != 3) break;
        }
        if (!local && (i != 0 || j != 0)) {
            st = ZXV_E_INTERNAL;
            goto done;
        }
        out->a_start = local ? (uint32_t) i : 0;
        out->b_start = local ? (uint32_t) j : 0;
        if (local && best <= 0) out->a_start = out->a_end = out->b_start = out->b_end = 0;
        for (size_t x = 0; x < k / 2; x++) {
            char ta = ar[x], tbb = br[x];
            ar[x] = ar[k - 1 - x];
            ar[k - 1 - x] = ta;
            br[x] = br[k - 1 - x];
            br[k - 1 - x] = tbb;
        }
        ar[k] = br[k] = '\0';
        out->columns = (uint32_t) k;
        out->a_row = ar;
        out->b_row = br;
        ar = br = NULL;
        if (count_rows(out->a_row, out->b_row, out->columns, out) != 0) {
            st = ZXV_E_INTERNAL;
            goto done;
        }
        { /* self-check: the alignment rescores to its own score */
            int64_t rs = 0;
            if (score_rows(m, idx, p->gap_open, p->gap_extend, out->a_row, out->b_row, k, &rs) !=
                    ZXV_OK ||
                rs != out->score) {
                zind_set_error("zgn_align: internal: traceback does not rescore");
                st = ZXV_E_INTERNAL;
                goto done;
            }
        }
    }
    st = ZXV_OK;
    goto done;
overflow:
    zind_set_error("zgn_align: a score overflows int64; refused (scale the matrix or gaps down)");
    st = ZGN_E_OVERFLOW;
done:
    free(rows);
    free(tb);
    free(ar);
    free(br);
    if (st != ZXV_OK) zgn_result_free(out);
    return st;
}

int32_t zgn_identity(const zgn_result *r, zind_qty *out) {
    if (!r || !out) return ZXV_E_ARG;
    if (r->columns == 0) return ZXV_E_STATE;
    return zind_qty_make(r->matches, r->columns, out);
}

int32_t zgn_score_per_column(const zgn_result *r, zind_qty *out) {
    if (!r || !out) return ZXV_E_ARG;
    if (r->columns == 0) return ZXV_E_STATE;
    return zind_qty_make(r->score, r->columns, out);
}

static int32_t aln_digest(const char *ar, const char *br, uint32_t cols, uint8_t out[32]) {
    size_t total = 10 + 4 + 2 * (size_t) cols;
    uint8_t *buf = (uint8_t *) malloc(total);
    int32_t st;
    if (!buf) return ZXV_E_NOMEM;
    memcpy(buf, "zgn/aln/v1", 10);
    for (int i = 0; i < 4; i++) buf[10 + i] = (uint8_t) (cols >> (8 * i));
    if (cols) {
        memcpy(buf + 14, ar, cols);
        memcpy(buf + 14 + cols, br, cols);
    }
    st = zxv_sha3_256(buf, total, out);
    free(buf);
    return st;
}

int32_t zgn_alignment_digest(const zgn_result *r, uint8_t out[32]) {
    if (!r || !out || (r->columns && (!r->a_row || !r->b_row))) return ZXV_E_ARG;
    if (r->columns && (strlen(r->a_row) != r->columns || strlen(r->b_row) != r->columns))
        return ZXV_E_ARG;
    return aln_digest(r->a_row ? r->a_row : "", r->b_row ? r->b_row : "", r->columns, out);
}

/* ===== FASTA ===== */

typedef struct {
    uint32_t id_off, desc_off, res_off, len;
} frec_t;

struct zgn_fasta {
    uint32_t n;
    frec_t *recs;
    char *text;   /* ids and descriptions, NUL-terminated */
    uint8_t *res; /* every record's residues, back to back */
    uint32_t *hash; /* id hash table: record index + 1, 0 = empty */
    uint32_t hmask;
};

static uint32_t fnv(const char *s) {
    uint32_t h = 2166136261u;
    while (*s) h = (h ^ (uint8_t) *s++) * 16777619u;
    return h;
}

void zgn_fasta_destroy(zgn_fasta *f) {
    if (!f) return;
    free(f->recs);
    free(f->text);
    free(f->res);
    free(f->hash);
    free(f);
}

static int32_t fasta_fail(zgn_fasta *f, int32_t st, size_t line, const char *why) {
    zind_set_error("zgn_fasta_parse: line %zu: %s", line, why);
    zgn_fasta_destroy(f);
    return st;
}

int32_t zgn_fasta_parse(const uint8_t *buf, size_t len, const zgn_fasta_limits *lim,
                        zgn_fasta **out) {
    uint32_t max_rec = 1024, max_res = ZGN_SEQ_MAX, alloc = 0;
    size_t max_bytes = (size_t) 64 << 20, pos = 0, line = 0, tpos = 0, rpos = 0, nhdr = 0;
    zgn_fasta *f;
    frec_t *cur = NULL;
    if (!out || (!buf && len)) return ZXV_E_ARG;
    *out = NULL;
    if (lim) {
        if (lim->max_records > 65536 || lim->max_residues > ZGN_SEQ_MAX ||
            lim->max_bytes > max_bytes)
            return ZXV_E_ARG;
        if (lim->max_records) max_rec = lim->max_records;
        if (lim->max_residues) max_res = lim->max_residues;
        if (lim->max_bytes) max_bytes = lim->max_bytes;
    }
    if (len > max_bytes) {
        zind_set_error("zgn_fasta_parse: %zu bytes is over the bound of %zu", len, max_bytes);
        return ZGN_E_LIMIT;
    }
    f = (zgn_fasta *) calloc(1, sizeof *f);
    if (!f) return ZXV_E_NOMEM;
    if (len == 0) return fasta_fail(f, ZGN_E_FASTA, 1, "empty input");
    for (size_t k = 0; k < len; k++)
        if (buf[k] == '>' && (k == 0 || buf[k - 1] == '\n')) nhdr++;
    f->text = (char *) malloc(len + 2 * nhdr + 1);
    f->res = (uint8_t *) malloc(len);
    if (!f->text || !f->res) {
        zgn_fasta_destroy(f);
        return ZXV_E_NOMEM;
    }
    while (pos < len) {
        const uint8_t *nl = (const uint8_t *) memchr(buf + pos, '\n', len - pos);
        size_t eol = nl ? (size_t) (nl - buf) : len, end = eol;
        line++;
        if (end > pos && buf[end - 1] == '\r') {
            if (!nl) return fasta_fail(f, ZGN_E_FASTA, line, "a lone carriage return");
            end--;
        }
        if (end == pos) return fasta_fail(f, ZGN_E_FASTA, line, "a blank line");
        if (buf[pos] == '>') {
            size_t q = pos + 1, idl;
            if (cur && cur->len == 0)
                return fasta_fail(f, ZGN_E_FASTA, line - 1, "a record with no residues");
            if (f->n >= max_rec) return fasta_fail(f, ZGN_E_LIMIT, line, "too many records");
            while (q < end && buf[q] != ' ' && buf[q] != '\t') {
                if (buf[q] < 0x21 || buf[q] > 0x7e || buf[q] == '>')
                    return fasta_fail(f, ZGN_E_FASTA, line, "an ID byte that is not printable");
                q++;
            }
            idl = q - pos - 1;
            if (idl == 0 || idl > ZGN_ID_MAX)
                return fasta_fail(f, ZGN_E_FASTA, line, "an ID must be 1..64 bytes");
            if (f->n == alloc) {
                uint32_t a = alloc ? alloc * 2 : 16;
                frec_t *nr = (frec_t *) realloc(f->recs, (size_t) a * sizeof *nr);
                if (!nr) {
                    zgn_fasta_destroy(f);
                    return ZXV_E_NOMEM;
                }
                f->recs = nr;
                alloc = a;
            }
            cur = &f->recs[f->n++];
            cur->id_off = (uint32_t) tpos;
            memcpy(f->text + tpos, buf + pos + 1, idl);
            tpos += idl;
            f->text[tpos++] = '\0';
            cur->desc_off = (uint32_t) tpos;
            if (q < end) {
                size_t dl = end - q - 1;
                if (dl == 0) return fasta_fail(f, ZGN_E_FASTA, line, "an empty description");
                if (dl > ZGN_DESC_MAX)
                    return fasta_fail(f, ZGN_E_FASTA, line, "a description over 200 bytes");
                for (size_t k = q + 1; k < end; k++)
                    if (buf[k] < 0x20 || buf[k] > 0x7e)
                        return fasta_fail(f, ZGN_E_FASTA, line,
                                          "a description byte that is not printable");
                memcpy(f->text + tpos, buf + q + 1, dl);
                tpos += dl;
            }
            f->text[tpos++] = '\0';
            cur->res_off = (uint32_t) rpos;
            cur->len = 0;
        } else {
            if (!cur) return fasta_fail(f, ZGN_E_FASTA, line, "a sequence before any header");
            for (size_t k = pos; k < end; k++) {
                uint8_t c = buf[k];
                if (c >= 'a' && c <= 'z') c = (uint8_t) (c - 32);
                if (!((c >= 'A' && c <= 'Z') || c == '*'))
                    return fasta_fail(f, ZGN_E_FASTA, line,
                                      "a sequence byte outside [A-Za-z*] (gaps, digits and "
                                      "spaces are refused)");
                if (cur->len >= max_res)
                    return fasta_fail(f, ZGN_E_LIMIT, line, "a record over the residue bound");
                f->res[rpos++] = c;
                cur->len++;
            }
        }
        pos = eol + 1;
    }
    if (cur && cur->len == 0) return fasta_fail(f, ZGN_E_FASTA, line, "a record with no residues");
    /* repeated IDs */
    {
        uint32_t slots = 16;
        while (slots < 2 * f->n) slots <<= 1;
        f->hash = (uint32_t *) calloc(slots, sizeof *f->hash);
        if (!f->hash) {
            zgn_fasta_destroy(f);
            return ZXV_E_NOMEM;
        }
        f->hmask = slots - 1;
        for (uint32_t r = 0; r < f->n; r++) {
            const char *id = f->text + f->recs[r].id_off;
            uint32_t h = fnv(id) & f->hmask;
            while (f->hash[h]) {
                if (strcmp(f->text + f->recs[f->hash[h] - 1].id_off, id) == 0) {
                    zind_set_error("zgn_fasta_parse: record %u repeats the ID %s", r + 1, id);
                    zgn_fasta_destroy(f);
                    return ZGN_E_FASTA;
                }
                h = (h + 1) & f->hmask;
            }
            f->hash[h] = r + 1;
        }
    }
    *out = f;
    return ZXV_OK;
}

int32_t zgn_fasta_count(const zgn_fasta *f, uint32_t *out_count) {
    if (!f || !out_count) return ZXV_E_ARG;
    *out_count = f->n;
    return ZXV_OK;
}

int32_t zgn_fasta_get(const zgn_fasta *f, uint32_t i, zgn_seq *out, const char **out_desc) {
    if (!f || !out) return ZXV_E_ARG;
    if (i >= f->n) return ZXV_E_NOT_FOUND;
    out->id = f->text + f->recs[i].id_off;
    out->res = f->res + f->recs[i].res_off;
    out->len = f->recs[i].len;
    if (out_desc) *out_desc = f->text + f->recs[i].desc_off;
    return ZXV_OK;
}

int32_t zgn_fasta_find(const zgn_fasta *f, const char *id, zgn_seq *out) {
    uint32_t h;
    if (!f || !id || !out) return ZXV_E_ARG;
    h = fnv(id) & f->hmask;
    while (f->hash[h]) {
        if (strcmp(f->text + f->recs[f->hash[h] - 1].id_off, id) == 0)
            return zgn_fasta_get(f, f->hash[h] - 1, out, NULL);
        h = (h + 1) & f->hmask;
    }
    return ZXV_E_NOT_FOUND;
}

/* ===== signed result records ===== */

static int32_t check_seq(const zgn_seq *s) {
    if (!s || (!s->res && s->len)) return ZXV_E_ARG;
    if (s->len > ZGN_SEQ_MAX) return ZGN_E_LIMIT;
    if (s->id && s->id[0] && !name_ok(s->id, ZGN_ID_MAX)) {
        zind_set_error("zgn: a sequence ID is up to 64 printable bytes");
        return ZXV_E_ARG;
    }
    return ZXV_OK;
}

/* The residues of one row without its gaps equal s[start..end). */
static int row_matches(const char *row, uint32_t cols, const uint8_t *s, uint32_t start,
                       uint32_t end) {
    uint32_t q = start;
    for (uint32_t k = 0; k < cols; k++) {
        if (row[k] == '-') continue;
        if (q >= end || (uint8_t) row[k] != s[q]) return 0;
        q++;
    }
    return q == end;
}

int32_t zgn_record_append(const zxv_cap *cap, zind_chain *chain, const zind_signer *signer,
                          const zgn_matrix *m, const zgn_params *p, const zgn_seq *a,
                          const zgn_seq *b, const zgn_result *r, uint64_t *out_seq) {
    uint8_t md[32], da[32], db[32], ad[32], buf[512];
    int8_t idx[256];
    zgn_result cnt;
    int64_t rs = 0;
    zind_wr w;
    int32_t st;
    if (!cap || !chain || !signer || !m || !r) return ZXV_E_ARG;
    if (strcmp(zind_chain_domain(chain), ZGN_DOMAIN) != 0) {
        zind_set_error("zgn_record_append: the chain's domain must be \"%s\"", ZGN_DOMAIN);
        return ZXV_E_ARG;
    }
    st = zgn_matrix_check(m);
    if (st == ZXV_OK) st = check_params(p, NULL);
    if (st == ZXV_OK) st = check_seq(a);
    if (st == ZXV_OK) st = check_seq(b);
    if (st != ZXV_OK) return st;
    /* r must be an alignment of a and b under m and p */
    if ((r->columns && (!r->a_row || !r->b_row)) ||
        (r->columns && (strlen(r->a_row) != r->columns || strlen(r->b_row) != r->columns)) ||
        r->a_start > r->a_end || r->a_end > a->len || r->b_start > r->b_end ||
        r->b_end > b->len ||
        (p->mode == ZGN_GLOBAL &&
         (r->a_start != 0 || r->a_end != a->len || r->b_start != 0 || r->b_end != b->len)) ||
        !row_matches(r->columns ? r->a_row : "", r->columns, a->res, r->a_start, r->a_end) ||
        !row_matches(r->columns ? r->b_row : "", r->columns, b->res, r->b_start, r->b_end)) {
        zind_set_error("zgn_record_append: the result is not an alignment of these sequences");
        return ZGN_E_MISMATCH;
    }
    matrix_index(m, idx);
    memset(&cnt, 0, sizeof cnt);
    if (r->columns &&
        (count_rows(r->a_row, r->b_row, r->columns, &cnt) != 0 ||
         score_rows(m, idx, p->gap_open, p->gap_extend, r->a_row, r->b_row, r->columns, &rs) !=
             ZXV_OK))
        return ZGN_E_MISMATCH;
    if (rs != r->score || cnt.matches != r->matches || cnt.mismatches != r->mismatches ||
        cnt.gap_columns != r->gap_columns || cnt.gap_opens != r->gap_opens) {
        zind_set_error("zgn_record_append: the score or counts do not match the alignment");
        return ZGN_E_MISMATCH;
    }
    st = zgn_matrix_digest(m, md);
    if (st == ZXV_OK) st = zgn_seq_digest(a->res, a->len, da);
    if (st == ZXV_OK) st = zgn_seq_digest(b->res, b->len, db);
    if (st == ZXV_OK) st = zgn_alignment_digest(r, ad);
    if (st != ZXV_OK) return st;
    zind_wr_init(&w, buf, sizeof buf);
    zind_wr_u8(&w, 1);
    zind_wr_u8(&w, (uint8_t) p->mode);
    zind_wr_bytes(&w, md, 32);
    zind_wr_str(&w, m->name);
    zind_wr_u64(&w, (uint64_t) p->gap_open);
    zind_wr_u64(&w, (uint64_t) p->gap_extend);
    zind_wr_bytes(&w, da, 32);
    zind_wr_u32(&w, a->len);
    zind_wr_str(&w, a->id ? a->id : "");
    zind_wr_bytes(&w, db, 32);
    zind_wr_u32(&w, b->len);
    zind_wr_str(&w, b->id ? b->id : "");
    zind_wr_u64(&w, (uint64_t) r->score);
    zind_wr_u32(&w, r->a_start);
    zind_wr_u32(&w, r->a_end);
    zind_wr_u32(&w, r->b_start);
    zind_wr_u32(&w, r->b_end);
    zind_wr_u32(&w, r->columns);
    zind_wr_u32(&w, r->matches);
    zind_wr_u32(&w, r->mismatches);
    zind_wr_u32(&w, r->gap_columns);
    zind_wr_u32(&w, r->gap_opens);
    zind_wr_bytes(&w, ad, 32);
    if (w.overflow) return ZXV_E_INTERNAL;
    return zind_chain_append(cap, chain, signer, ZGN_REC_RESULT, buf, w.len, out_seq, NULL);
}

int32_t zgn_claim_decode(const uint8_t *payload, size_t len, zgn_claim *c) {
    zind_rd r;
    char tmp[256];
    if (!payload || !c) return ZXV_E_ARG;
    memset(c, 0, sizeof *c);
    zind_rd_init(&r, payload, len);
    if (zind_rd_u8(&r) != 1) goto bad;
    c->mode = zind_rd_u8(&r);
    for (int i = 0; i < 32; i++) c->matrix_digest[i] = zind_rd_u8(&r);
    zind_rd_str(&r, tmp, sizeof tmp);
    if (r.bad || strlen(tmp) > ZGN_NAME_MAX || !name_ok(tmp, ZGN_NAME_MAX)) goto bad;
    memcpy(c->matrix_name, tmp, strlen(tmp) + 1);
    c->gap_open = (int64_t) zind_rd_u64(&r);
    c->gap_extend = (int64_t) zind_rd_u64(&r);
    for (int i = 0; i < 32; i++) c->digest_a[i] = zind_rd_u8(&r);
    c->len_a = zind_rd_u32(&r);
    zind_rd_str(&r, tmp, sizeof tmp);
    if (r.bad || strlen(tmp) > ZGN_ID_MAX || !name_ok(tmp, ZGN_ID_MAX)) goto bad;
    memcpy(c->id_a, tmp, strlen(tmp) + 1);
    for (int i = 0; i < 32; i++) c->digest_b[i] = zind_rd_u8(&r);
    c->len_b = zind_rd_u32(&r);
    zind_rd_str(&r, tmp, sizeof tmp);
    if (r.bad || strlen(tmp) > ZGN_ID_MAX || !name_ok(tmp, ZGN_ID_MAX)) goto bad;
    memcpy(c->id_b, tmp, strlen(tmp) + 1);
    c->score = (int64_t) zind_rd_u64(&r);
    c->a_start = zind_rd_u32(&r);
    c->a_end = zind_rd_u32(&r);
    c->b_start = zind_rd_u32(&r);
    c->b_end = zind_rd_u32(&r);
    c->columns = zind_rd_u32(&r);
    c->matches = zind_rd_u32(&r);
    c->mismatches = zind_rd_u32(&r);
    c->gap_columns = zind_rd_u32(&r);
    c->gap_opens = zind_rd_u32(&r);
    for (int i = 0; i < 32; i++) c->alignment_digest[i] = zind_rd_u8(&r);
    if (r.bad || r.pos != len || (c->mode != ZGN_GLOBAL && c->mode != ZGN_LOCAL) ||
        c->gap_open < 0 || c->gap_extend < 0 || c->len_a > ZGN_SEQ_MAX ||
        c->len_b > ZGN_SEQ_MAX || c->a_start > c->a_end || c->a_end > c->len_a ||
        c->b_start > c->b_end || c->b_end > c->len_b || c->score == INT64_MIN)
        goto bad;
    return ZXV_OK;
bad:
    zind_set_error("zgn_claim_decode: malformed result record");
    memset(c, 0, sizeof *c);
    return ZXV_E_FORMAT;
}

int32_t zgn_record_verify(const zxv_cap *cap, const zind_chain *chain, uint32_t index,
                          const uint8_t *const *trusted, size_t n_trusted, const zgn_matrix *m,
                          const zgn_seq *a, const zgn_seq *b, uint64_t max_cells,
                          zgn_claim *out) {
    zind_record rec;
    zgn_claim c;
    zgn_params p;
    zgn_result r;
    uint8_t md[32], da[32], db[32], ad[32];
    int32_t st;
    if (out) memset(out, 0, sizeof *out);
    if (!cap || !chain || !m || max_cells > ZGN_CELLS_MAX) return ZXV_E_ARG;
    st = check_seq(a);
    if (st == ZXV_OK) st = check_seq(b);
    if (st == ZXV_OK) st = zgn_matrix_check(m);
    if (st != ZXV_OK) return st;
    if (strcmp(zind_chain_domain(chain), ZGN_DOMAIN) != 0) return ZXV_E_ARG;
    st = zind_chain_verify(cap, chain, trusted, n_trusted, NULL);
    if (st != ZXV_OK) return st;
    st = zind_chain_at(chain, index, &rec);
    if (st != ZXV_OK) return st;
    if (rec.kind != ZGN_REC_RESULT) {
        zind_set_error("zgn_record_verify: record %u is not a result", index);
        return ZXV_E_FORMAT;
    }
    st = zgn_claim_decode(rec.payload, rec.payload_len, &c);
    if (st != ZXV_OK) return st;
    if (out) *out = c;
    st = zgn_matrix_digest(m, md);
    if (st != ZXV_OK) return st;
    if (memcmp(md, c.matrix_digest, 32) != 0) {
        zind_set_error("zgn_record_verify: the record was scored with another matrix (%s)",
                       c.matrix_name);
        return ZGN_E_MATRIX;
    }
    st = zgn_seq_digest(a->res, a->len, da);
    if (st == ZXV_OK) st = zgn_seq_digest(b->res, b->len, db);
    if (st != ZXV_OK) return st;
    if (a->len != c.len_a || b->len != c.len_b || memcmp(da, c.digest_a, 32) != 0 ||
        memcmp(db, c.digest_b, 32) != 0) {
        zind_set_error("zgn_record_verify: the sequences are not the ones recorded");
        return ZGN_E_MISMATCH;
    }
    p.mode = c.mode;
    p.gap_open = c.gap_open;
    p.gap_extend = c.gap_extend;
    p.max_cells = max_cells ? max_cells : ZGN_CELLS_MAX;
    st = zgn_align(m, &p, a->res, a->len, b->res, b->len, &r);
    if (st != ZXV_OK) return st;
    st = zgn_alignment_digest(&r, ad);
    if (st == ZXV_OK &&
        (r.score != c.score || r.a_start != c.a_start || r.a_end != c.a_end ||
         r.b_start != c.b_start || r.b_end != c.b_end || r.columns != c.columns ||
         r.matches != c.matches || r.mismatches != c.mismatches ||
         r.gap_columns != c.gap_columns || r.gap_opens != c.gap_opens ||
         memcmp(ad, c.alignment_digest, 32) != 0)) {
        zind_set_error("zgn_record_verify: recomputing gives another result (score %lld, "
                       "recorded %lld)",
                       (long long) r.score, (long long) c.score);
        st = ZGN_E_MISMATCH;
    }
    zgn_result_free(&r);
    return st;
}

int32_t zgn_verify_export(const zxv_cap *cap, const uint8_t *buf, size_t len, uint32_t index,
                          const uint8_t *const *trusted, size_t n_trusted, const zgn_matrix *m,
                          const zgn_seq *a, const zgn_seq *b, uint64_t max_cells,
                          zgn_claim *out) {
    zind_chain *c = NULL;
    int32_t st;
    if (out) memset(out, 0, sizeof *out);
    if (!cap || !buf) return ZXV_E_ARG;
    st = zind_chain_import(ZGN_DOMAIN, buf, len, &c);
    if (st != ZXV_OK) return st;
    st = zgn_record_verify(cap, c, index, trusted, n_trusted, m, a, b, max_cells, out);
    zind_chain_destroy(c);
    return st;
}
