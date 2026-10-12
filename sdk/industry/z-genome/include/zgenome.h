/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zgenome.h — z-genome: exact, reproducible sequence alignment scores.
 *
 *   matrices   integer substitution matrices: BLOSUM62 (NCBI), a DNA
 *              match/mismatch matrix over ACGTN, or your own (up to 32
 *              symbols); every entry an int64
 *   alignment  Needleman-Wunsch (global) and Smith-Waterman (local) with
 *              affine gaps (Gotoh): a gap of k columns costs
 *              gap_open + k * gap_extend. Every score is an int64; a sum
 *              that would overflow is refused (ZGN_E_OVERFLOW), never
 *              wrapped or saturated
 *   tie-break  fixed and documented (below), so the traceback, not only
 *              the score, is identical on every machine and compiler
 *   FASTA      a strict, bounded parser
 *   records    a signed result (libzind chain, ML-DSA-87) carrying the
 *              SHA3-256 digests of both inputs and of the matrix, the
 *              parameters, the score and an alignment digest; another
 *              machine re-verifies it by recomputing from the inputs
 *   ratios     identity and score per column as exact rationals (zxv_rat)
 *
 * TIE-BREAK (the whole rule; zgn_align follows it exactly):
 *   1. States: M (residue against residue), X (a residue of `a` against a
 *      gap) and Y (a gap against a residue of `b`).
 *   2. When two predecessors give the same score, prefer M, then X, then
 *      Y. This holds for every cell and for the final state of a global
 *      alignment.
 *   3. Local: a match cell starts a fresh alignment when the best
 *      predecessor score is <= 0 (a zero-scoring prefix is dropped). The
 *      end cell is the M cell with the highest score; among equal scores
 *      the first in row-major order (smallest i, then smallest j). A best
 *      score <= 0 gives the empty alignment with score 0.
 *
 * MEMORY: (len_a + 1) * (len_b + 1) bytes of traceback plus
 * 6 * (len_b + 1) int64 working rows. The cell count is bounded by
 * zgn_params.max_cells (default ZGN_CELLS_DEFAULT, at most ZGN_CELLS_MAX);
 * a larger problem is refused (ZGN_E_LIMIT), never truncated.
 *
 * Research use only. Not a clinical diagnostic and not a medical device;
 * no IEC 62304, FDA or CE claim (README "Limits"). Integer code only (R11).
 * Kit API 0.1 (preview). */
#ifndef ZGENOME_H
#define ZGENOME_H

#include "zind_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZGN_VERSION_MAJOR 0
#define ZGN_VERSION_MINOR 1

/* Status codes (-3500..-3599). */
#define ZGN_E_SEQ      (-3501) /* a residue is not in the matrix alphabet          */
#define ZGN_E_FASTA    (-3502) /* malformed FASTA (strict parser; see zgn_fasta)    */
#define ZGN_E_LIMIT    (-3503) /* over a size bound (length, cells, records, bytes) */
#define ZGN_E_OVERFLOW (-3504) /* an int64 score would overflow: refused            */
#define ZGN_E_MATRIX   (-3505) /* a malformed matrix, or not the matrix a record names */
#define ZGN_E_MISMATCH (-3506) /* re-verification found other inputs or results     */

const char *zgn_status_name(int32_t status);

/* ===== Substitution matrices ===== */
#define ZGN_ALPHA_MAX 32u
#define ZGN_NAME_MAX  23u

typedef struct {
    char name[ZGN_NAME_MAX + 1];         /* label only; not part of the digest     */
    char alphabet[ZGN_ALPHA_MAX + 1];    /* distinct symbols of [A-Z*], NUL-ended */
    uint8_t n;                           /* strlen(alphabet), 1..32               */
    int64_t score[ZGN_ALPHA_MAX][ZGN_ALPHA_MAX]; /* score[i][j]: alphabet[i] vs [j];
                                                    INT64_MIN is refused           */
} zgn_matrix;

/* BLOSUM62 as published by NCBI, alphabet "ARNDCQEGHILKMFPSTWYVBZX*". */
int32_t zgn_matrix_blosum62(zgn_matrix *out);
/* Alphabet "ACGTN": match on the diagonal of ACGT, mismatch everywhere
 * else (N never matches, not even N). */
int32_t zgn_matrix_dna(int64_t match, int64_t mismatch, zgn_matrix *out);
/* Your own: n = strlen(alphabet) symbols, scores row-major (n * n). */
int32_t zgn_matrix_custom(const char *name, const char *alphabet, const int64_t *scores,
                          zgn_matrix *out);
/* ZXV_OK, or ZGN_E_MATRIX for a malformed matrix. */
int32_t zgn_matrix_check(const zgn_matrix *m);
/* SHA3-256("zgn/matrix/v1" || u8 n || alphabet || n*n int64 LE). */
int32_t zgn_matrix_digest(const zgn_matrix *m, uint8_t out[32]);
/* 1 if score[i][j] == score[j][i] everywhere. */
int zgn_matrix_symmetric(const zgn_matrix *m);

/* ===== Sequences ===== */
#define ZGN_SEQ_MAX        (1u << 20) /* residues per sequence             */
#define ZGN_CELLS_DEFAULT  (1u << 24) /* 16 Mi cells: 16 MiB of traceback  */
#define ZGN_CELLS_MAX      (1u << 28) /* 256 Mi cells: 256 MiB             */
#define ZGN_ID_MAX         64u
#define ZGN_DESC_MAX       200u

/* A borrowed view: residues are upper-case symbols, not NUL-terminated. */
typedef struct {
    const char *id;       /* may be NULL or "" for an anonymous sequence */
    const uint8_t *res;
    uint32_t len;
} zgn_seq;

/* SHA3-256("zgn/seq/v1" || u32le len || residues). The id is not hashed. */
int32_t zgn_seq_digest(const uint8_t *res, uint32_t len, uint8_t out[32]);

/* ===== Alignment ===== */
#define ZGN_GLOBAL 1 /* Needleman-Wunsch */
#define ZGN_LOCAL  2 /* Smith-Waterman   */

typedef struct {
    int32_t mode;        /* ZGN_GLOBAL or ZGN_LOCAL                          */
    int64_t gap_open;    /* >= 0: charged once per gap                        */
    int64_t gap_extend;  /* >= 0: charged per gap column (a linear gap: open 0) */
    uint64_t max_cells;  /* 0 = ZGN_CELLS_DEFAULT; at most ZGN_CELLS_MAX      */
} zgn_params;

typedef struct {
    int64_t score;
    uint32_t a_start, a_end; /* half-open residue range of `a` in the alignment */
    uint32_t b_start, b_end;
    uint32_t columns;        /* alignment length                               */
    uint32_t matches, mismatches, gap_columns, gap_opens;
    char *a_row, *b_row;     /* NUL-terminated, '-' for a gap; owned: zgn_result_free */
} zgn_result;

/* Align a against b. Residues must be in the matrix alphabet (ZGN_E_SEQ
 * names the first one that is not). On any refusal *out is left empty. */
int32_t zgn_align(const zgn_matrix *m, const zgn_params *p, const uint8_t *a, uint32_t len_a,
                  const uint8_t *b, uint32_t len_b, zgn_result *out);
void zgn_result_free(zgn_result *r);

/* Score two gapped rows ('-' for a gap, same length, no column of two
 * gaps) under the affine rule. An alignment from zgn_align always rescores
 * to its own score. */
int32_t zgn_score_rows(const zgn_matrix *m, const zgn_params *p, const char *a_row,
                       const char *b_row, int64_t *out_score);

/* Exact ratios through zxv_rat: matches / columns and score / columns.
 * The empty alignment has none (ZXV_E_STATE). */
int32_t zgn_identity(const zgn_result *r, zind_qty *out);
int32_t zgn_score_per_column(const zgn_result *r, zind_qty *out);

/* SHA3-256("zgn/aln/v1" || u32le columns || a_row || b_row). */
int32_t zgn_alignment_digest(const zgn_result *r, uint8_t out[32]);

/* ===== FASTA (strict) =====
 * Accepted: records of a header line ">ID[ DESCRIPTION]" and one or more
 * sequence lines. ID: 1..64 bytes of printable ASCII without space or '>';
 * DESCRIPTION: up to 200 printable ASCII bytes after one space or tab.
 * Sequence lines: [A-Za-z*] only; lower case is upper-cased. Lines end in
 * "\n" or "\r\n"; the last newline is optional. Refused (ZGN_E_FASTA, with
 * the line number in zind_last_error): anything before the first '>', a
 * blank line, a gap or digit or space in a sequence, a NUL byte, a lone
 * "\r", a record with no residues and a repeated ID. Over a limit:
 * ZGN_E_LIMIT. */
typedef struct {
    uint32_t max_records;  /* 0 = 1024; at most 65536                */
    uint32_t max_residues; /* per record; 0 = ZGN_SEQ_MAX (the most)  */
    size_t max_bytes;      /* 0 = 64 MiB (the most)                   */
} zgn_fasta_limits;

typedef struct zgn_fasta zgn_fasta;
int32_t zgn_fasta_parse(const uint8_t *buf, size_t len, const zgn_fasta_limits *lim,
                        zgn_fasta **out);
void zgn_fasta_destroy(zgn_fasta *f);
int32_t zgn_fasta_count(const zgn_fasta *f, uint32_t *out_count);
/* Record i as a view (valid while f lives); desc may be NULL. */
int32_t zgn_fasta_get(const zgn_fasta *f, uint32_t i, zgn_seq *out, const char **out_desc);
/* By ID; ZXV_E_NOT_FOUND if absent. */
int32_t zgn_fasta_find(const zgn_fasta *f, const char *id, zgn_seq *out);

/* ===== Signed result records =====
 * One record per alignment in a libzind chain of domain ZGN_DOMAIN, kind
 * ZGN_REC_RESULT, signed with ML-DSA-87 by the machine that computed it.
 * Payload (little-endian):
 *   u8 version(1) u8 mode  matrix_digest[32] str matrix_name
 *   u64 gap_open u64 gap_extend
 *   digest_a[32] u32 len_a str id_a  digest_b[32] u32 len_b str id_b
 *   i64 score u32 a_start a_end b_start b_end columns matches mismatches
 *   gap_columns gap_opens  alignment_digest[32]
 * Nothing in it depends on the machine, the compiler or the clock. */
#define ZGN_DOMAIN     "z-genome/result"
#define ZGN_REC_RESULT 1

typedef struct {
    int32_t mode;
    uint8_t matrix_digest[32];
    char matrix_name[ZGN_NAME_MAX + 1];
    int64_t gap_open, gap_extend;
    uint8_t digest_a[32], digest_b[32];
    uint32_t len_a, len_b;
    char id_a[ZGN_ID_MAX + 1], id_b[ZGN_ID_MAX + 1];
    int64_t score;
    uint32_t a_start, a_end, b_start, b_end;
    uint32_t columns, matches, mismatches, gap_columns, gap_opens;
    uint8_t alignment_digest[32];
} zgn_claim;

/* Append a signed record of `r` (computed from a, b, m, p) to `chain`
 * (domain ZGN_DOMAIN). The record is refused (ZGN_E_MISMATCH) unless `r`
 * really is an alignment of a and b that rescores to r->score. Needs
 * CRYPTO_SIGN. */
int32_t zgn_record_append(const zxv_cap *cap, zind_chain *chain, const zind_signer *signer,
                          const zgn_matrix *m, const zgn_params *p, const zgn_seq *a,
                          const zgn_seq *b, const zgn_result *r, uint64_t *out_seq);
/* Decode a record payload (structure only). ZXV_E_FORMAT if malformed. */
int32_t zgn_claim_decode(const uint8_t *payload, size_t len, zgn_claim *out);

/* Re-verify record `index` of `chain` on this machine:
 *   1. every link and signature of the chain against the trusted keys
 *      (ZIND_E_CHAIN, ZIND_E_UNTRUSTED);
 *   2. the record decodes (ZXV_E_FORMAT) and names this matrix
 *      (ZGN_E_MATRIX);
 *   3. a and b hash to the recorded digests and lengths (ZGN_E_MISMATCH);
 *   4. the alignment recomputed from a, b, m and the recorded parameters
 *      gives the same score, coordinates, counts and alignment digest
 *      (ZGN_E_MISMATCH).
 * out (may be NULL) receives the claim. max_cells bounds the recompute
 * (0 = ZGN_CELLS_MAX). Needs CRYPTO_SIGN. */
int32_t zgn_record_verify(const zxv_cap *cap, const zind_chain *chain, uint32_t index,
                          const uint8_t *const *trusted, size_t n_trusted, const zgn_matrix *m,
                          const zgn_seq *a, const zgn_seq *b, uint64_t max_cells,
                          zgn_claim *out);
/* The same from zind_chain_export bytes (as sent to another machine). */
int32_t zgn_verify_export(const zxv_cap *cap, const uint8_t *buf, size_t len, uint32_t index,
                          const uint8_t *const *trusted, size_t n_trusted, const zgn_matrix *m,
                          const zgn_seq *a, const zgn_seq *b, uint64_t max_cells,
                          zgn_claim *out);

#ifdef __cplusplus
}
#endif

#endif /* ZGENOME_H */
