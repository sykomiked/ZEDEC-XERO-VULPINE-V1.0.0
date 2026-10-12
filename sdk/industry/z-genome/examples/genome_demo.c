/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* genome_demo.c — z-genome end to end: parse a small FASTA file strictly,
 * align human hemoglobin alpha and beta under BLOSUM62 with affine gaps
 * (local and global), show the alignment and exact ratios, sign the
 * result, and re-verify it "on another machine" from the exported bytes.
 * A record presented with the wrong sequence, and an export with one bit
 * flipped, are refused. Then a DNA pair, and a matrix whose sums overflow
 * int64, which is refused rather than wrapped. Research use only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zgenome.h"

#define TRY(x)                                                                                 \
    do {                                                                                       \
        int32_t rc_ = (x);                                                                     \
        if (rc_ != ZXV_OK) {                                                                   \
            fprintf(stderr, "%s failed: %s\n", #x, zgn_status_name(rc_));                      \
            return 1;                                                                          \
        }                                                                                      \
    } while (0)

static const char k_fasta[] =
    ">HBA_HUMAN Hemoglobin subunit alpha\n"
    "MVLSPADKTNVKAAWGKVGAHAGEYGAEALERMFLSFPTTKTYFPHFDLSHGSAQVKGHGKKVADALTNAVAHVDDMPNALSA\n"
    "LSDLHAHKLRVDPVNFKLLSHCLLVTLAAHLPAEFTPAVHASLDKFLASVSTVLTSKYR\n"
    ">HBB_HUMAN Hemoglobin subunit beta\n"
    "MVHLTPEEKSAVTALWGKVNVDEVGGEALGRLLVVYPWTQRFFESFGDLSTPDAVMGNPKVKAHGKKVLGAFSDGLAHLDNLKG\n"
    "TFATLSELHCDKLHVDPENFRLLGNVLVCVLAHHFGKEFTPPVQAAYQKVVAGVANALAHKYH\n";

static void show(const zgn_result *r) {
    char q[32];
    size_t n = 0;
    zind_qty id;
    for (uint32_t k = 0; k < r->columns; k += 60) {
        uint32_t w = r->columns - k < 60 ? r->columns - k : 60;
        char mid[61];
        for (uint32_t i = 0; i < w; i++)
            mid[i] = r->a_row[k + i] == r->b_row[k + i] ? '|' : (r->a_row[k + i] == '-' || r->b_row[k + i] == '-') ? ' ' : '.';
        mid[w] = 0;
        printf("    %.*s\n    %s\n    %.*s\n\n", (int) w, r->a_row + k, mid, (int) w, r->b_row + k);
    }
    if (zgn_identity(r, &id) == ZXV_OK && zind_qty_format(id, q, sizeof q, &n) == ZXV_OK)
        printf("    identity %s (an exact rational), %u gap(s) over %u columns\n", q, r->gap_opens,
               r->gap_columns);
}

int main(void) {
    zxv_ctx *ctx = NULL;
    zxv_cap *cap = NULL;
    zind_signer *lab = NULL;
    zind_chain *chain = NULL;
    zgn_fasta *fa = NULL;
    zgn_seq a, b;
    zgn_matrix m;
    zgn_params p = {ZGN_LOCAL, 11, 1, 0};
    zgn_result r;
    zgn_claim cl;
    uint8_t seed[32], *buf;
    size_t len = 0;
    uint32_t n = 0;
    const uint8_t *trust[1];

    TRY(zxv_ctx_create(&ctx));
    TRY(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_CRYPTO_SIGN, &cap));
    for (int i = 0; i < 32; i++) seed[i] = (uint8_t) (0x5a ^ i);
    TRY(zind_signer_from_seed(cap, seed, &lab));
    trust[0] = zind_signer_pk(lab);

    printf("z-genome demo (research use only; not a clinical diagnostic)\n\n");
    TRY(zgn_fasta_parse((const uint8_t *) k_fasta, sizeof k_fasta - 1, NULL, &fa));
    TRY(zgn_fasta_count(fa, &n));
    TRY(zgn_fasta_find(fa, "HBA_HUMAN", &a));
    TRY(zgn_fasta_find(fa, "HBB_HUMAN", &b));
    printf("FASTA: %u records, %s %u aa, %s %u aa\n", n, a.id, a.len, b.id, b.len);
    {
        static const char bad[] = ">x\nMVL-SPA\n";
        zgn_fasta *f2 = NULL;
        char err[200];
        size_t el = 0;
        int32_t st = zgn_fasta_parse((const uint8_t *) bad, sizeof bad - 1, NULL, &f2);
        zind_last_error(err, sizeof err, &el);
        printf("FASTA with a gap character: %s (%s)\n\n", zgn_status_name(st), err);
    }

    TRY(zgn_matrix_blosum62(&m));
    TRY(zgn_align(&m, &p, a.res, a.len, b.res, b.len, &r));
    printf("Smith-Waterman, BLOSUM62, gap open 11 extend 1: score %lld, a %u..%u, b %u..%u\n",
           (long long) r.score, r.a_start, r.a_end, r.b_start, r.b_end);
    show(&r);

    TRY(zind_chain_create(ZGN_DOMAIN, 16, &chain));
    TRY(zgn_record_append(cap, chain, lab, &m, &p, &a, &b, &r, NULL));
    zgn_result_free(&r);
    p.mode = ZGN_GLOBAL;
    TRY(zgn_align(&m, &p, a.res, a.len, b.res, b.len, &r));
    printf("\nNeedleman-Wunsch, same parameters: score %lld over %u columns\n",
           (long long) r.score, r.columns);
    TRY(zgn_record_append(cap, chain, lab, &m, &p, &a, &b, &r, NULL));
    zgn_result_free(&r);

    TRY(zind_chain_export(chain, NULL, 0, &len) == ZXV_E_BUFFER ? ZXV_OK : ZXV_E_INTERNAL);
    buf = (uint8_t *) malloc(len);
    TRY(zind_chain_export(chain, buf, len, &len));
    printf("\nsigned records exported: 2 records, %zu bytes\n", len);
    for (uint32_t i = 0; i < 2; i++) {
        TRY(zgn_verify_export(cap, buf, len, i, trust, 1, &m, &a, &b, 0, &cl));
        printf("  re-verified record %u by recomputing: %s score %lld, %s vs %s, matrix %s\n", i,
               cl.mode == ZGN_LOCAL ? "local" : "global", (long long) cl.score, cl.id_a, cl.id_b,
               cl.matrix_name);
    }
    printf("  record 0 with the sequences swapped: %s\n",
           zgn_status_name(zgn_verify_export(cap, buf, len, 0, trust, 1, &m, &b, &a, 0, NULL)));
    {
        zgn_matrix dna;
        zgn_matrix_dna(1, -1, &dna);
        printf("  record 0 checked against another matrix: %s\n",
               zgn_status_name(zgn_verify_export(cap, buf, len, 0, trust, 1, &dna, &a, &b, 0, NULL)));
    }
    buf[len / 3] ^= 0x01;
    printf("  one bit flipped in the export: %s\n",
           zgn_status_name(zgn_verify_export(cap, buf, len, 0, trust, 1, &m, &a, &b, 0, NULL)));
    free(buf);

    {
        zgn_matrix dna;
        zgn_params dp = {ZGN_LOCAL, 0, 2, 0};
        TRY(zgn_matrix_dna(3, -3, &dna));
        TRY(zgn_align(&dna, &dp, (const uint8_t *) "TGTTACGG", 8, (const uint8_t *) "GGTTGACTA", 9, &r));
        printf("\nDNA, match 3 mismatch -3 gap 2 (local): score %lld\n", (long long) r.score);
        show(&r);
        zgn_result_free(&r);
    }
    {
        int64_t big[4] = {INT64_MAX / 2 + 1, 0, 0, 1};
        zgn_matrix huge;
        zgn_params hp = {ZGN_GLOBAL, 0, 1, 0};
        TRY(zgn_matrix_custom("HUGE", "AB", big, &huge));
        printf("\na matrix whose sums leave int64: %s (refused, never wrapped)\n",
               zgn_status_name(zgn_align(&huge, &hp, (const uint8_t *) "AA", 2, (const uint8_t *) "AA", 2, &r)));
    }

    zind_chain_destroy(chain);
    zgn_fasta_destroy(fa);
    zind_signer_destroy(lab);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
    return 0;
}
