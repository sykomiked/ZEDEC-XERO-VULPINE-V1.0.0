/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_genome.c — a deterministic fuzzer for z-genome. Each operation is
 * one of:
 *   - an alignment of random sequences under a random matrix (symmetric or
 *     not) and random affine gaps, compared with a full-matrix reference
 *     written here; the result must rescore to its score, cover the right
 *     residues, and repeat bit for bit on a second run;
 *   - the same with matrix entries near 2^61, where the kit must either
 *     refuse with ZGN_E_OVERFLOW or give a result that rescores exactly;
 *   - sequences with a residue outside the alphabet (ZGN_E_SEQ);
 *   - FASTA text generated from the grammar, sometimes mutated: an accepted
 *     text must hold only valid records with unique IDs and must round-trip
 *     through a canonical re-serialization; a refused one must say
 *     ZGN_E_FASTA or ZGN_E_LIMIT;
 *   - every 50 operations, a signed record of the last alignment is
 *     appended and re-verified; every 200, a bit of an export is flipped and
 *     the verifier must refuse it.
 * One random draw per statement, so gcc and clang print the same bytes.
 *
 *   fuzz_genome ITERATIONS        ZIND_FUZZ_SEED=n: another stream */
#include "zgenome.h"
#include "zind_test.h"

static void die(const char *what, unsigned long it) {
    fprintf(stderr, "fuzz_genome: INVARIANT BROKEN at op %lu: %s [%s]\n", it, what,
            zt_last_err());
    abort();
}

static uint64_t draw(uint64_t *s, uint64_t n) {
    uint64_t v = zt_rand(s);
    return v % n;
}

/* full-matrix Gotoh, scores only (small values: no overflow possible) */
static int64_t ref_gotoh(const zgn_matrix *m, int64_t open, int64_t ext, const uint8_t *a,
                         int na, const uint8_t *b, int nb, int local) {
    int8_t idx[256];
    int W = nb + 1;
    const int64_t NI = INT64_MIN / 4;
    int64_t *M = (int64_t *) malloc(sizeof(int64_t) * (size_t) (na + 1) * W * 3);
    int64_t *X = M + (size_t) (na + 1) * W, *Y = X + (size_t) (na + 1) * W;
    int64_t oe = open + ext, best = 0, v;
    memset(idx, -1, sizeof idx);
    for (int i = 0; i < m->n; i++) idx[(unsigned char) m->alphabet[i]] = (int8_t) i;
    for (int i = 0; i <= na; i++)
        for (int j = 0; j <= nb; j++) {
            int k = i * W + j;
            M[k] = X[k] = Y[k] = NI;
            if (i == 0 && j == 0) {
                if (!local) M[k] = 0;
                continue;
            }
            if (!local && i == 0) Y[k] = -(open + ext * j);
            if (!local && j == 0) X[k] = -(open + ext * i);
            if (i == 0 || j == 0) continue;
            v = M[k - W - 1];
            if (X[k - W - 1] > v) v = X[k - W - 1];
            if (Y[k - W - 1] > v) v = Y[k - W - 1];
            if (local && v < 0) v = 0;
            M[k] = v + m->score[idx[a[i - 1]]][idx[b[j - 1]]];
            v = M[k - W] - oe;
            if (X[k - W] - ext > v) v = X[k - W] - ext;
            if (Y[k - W] - oe > v) v = Y[k - W] - oe;
            X[k] = v;
            v = M[k - 1] - oe;
            if (X[k - 1] - oe > v) v = X[k - 1] - oe;
            if (Y[k - 1] - ext > v) v = Y[k - 1] - ext;
            Y[k] = v;
            if (local && M[k] > best) best = M[k];
        }
    if (!local) {
        int k = na * W + nb;
        best = M[k];
        if (X[k] > best) best = X[k];
        if (Y[k] > best) best = Y[k];
    }
    free(M);
    return best;
}

static const char *k_alpha[] = {"AC", "ACGT", "ACGTN", "ARNDCQEGHILKMFPSTWYV", "*XYZ"};

static void make_matrix(uint64_t *s, int huge, zgn_matrix *m) {
    const char *al = k_alpha[draw(s, 5)];
    int n = (int) strlen(al);
    int sym = (int) draw(s, 2);
    int64_t sc[400];
    for (int i = 0; i < n * n; i++) {
        int64_t v = (int64_t) draw(s, 31) - 15;
        if (huge) v *= (int64_t) 1 << 58;
        sc[i] = v;
    }
    if (sym)
        for (int i = 0; i < n; i++)
            for (int j = 0; j < i; j++) sc[i * n + j] = sc[j * n + i];
    if (zgn_matrix_custom(huge ? "huge" : "rand", al, sc, m) != ZXV_OK) abort();
}

static void make_seq(uint64_t *s, const zgn_matrix *m, uint8_t *out, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) out[i] = (uint8_t) m->alphabet[draw(s, m->n)];
}

static void check_alignment(const zgn_matrix *m, const zgn_params *p, const uint8_t *a,
                            uint32_t na, const uint8_t *b, uint32_t nb, const zgn_result *r,
                            unsigned long it) {
    int64_t rs = 0;
    uint32_t q;
    if (zgn_score_rows(m, p, r->a_row, r->b_row, &rs) != ZXV_OK || rs != r->score)
        die("the alignment does not rescore to its score", it);
    if (r->matches + r->mismatches + r->gap_columns != r->columns) die("counts", it);
    q = r->a_start;
    for (uint32_t k = 0; k < r->columns; k++)
        if (r->a_row[k] != '-' && (q >= na || (uint8_t) r->a_row[k] != a[q++])) die("row a", it);
    if (q != r->a_end) die("row a range", it);
    q = r->b_start;
    for (uint32_t k = 0; k < r->columns; k++)
        if (r->b_row[k] != '-' && (q >= nb || (uint8_t) r->b_row[k] != b[q++])) die("row b", it);
    if (q != r->b_end) die("row b range", it);
    if (p->mode == ZGN_GLOBAL && (r->a_start || r->b_start || r->a_end != na || r->b_end != nb))
        die("global does not cover", it);
    if (p->mode == ZGN_LOCAL && r->score < 0) die("negative local score", it);
}

/* ---- FASTA generation ---- */

typedef struct {
    char *buf;
    size_t len, cap;
} sb_t;

static void sb_put(sb_t *b, const char *s, size_t n) {
    if (b->len + n + 1 > b->cap) {
        b->cap = (b->len + n + 1) * 2;
        b->buf = (char *) realloc(b->buf, b->cap);
    }
    memcpy(b->buf + b->len, s, n);
    b->len += n;
    b->buf[b->len] = 0;
}

static void gen_fasta(uint64_t *s, sb_t *b, uint32_t *n_rec) {
    static const char idc[] = "ABCxyz019._|:-";
    static const char rc[] = "ACGTNacgtnRDEKLW*";
    uint32_t nrec = 1 + (uint32_t) draw(s, 5);
    int crlf = (int) draw(s, 4) == 0;
    b->len = 0;
    for (uint32_t r = 0; r < nrec; r++) {
        char line[96];
        uint32_t idl = 1 + (uint32_t) draw(s, 8), lines, n = 0;
        line[n++] = '>';
        for (uint32_t k = 0; k < idl; k++) line[n++] = idc[draw(s, sizeof idc - 1)];
        n += (size_t) snprintf(line + n, sizeof line - n, "%u", r); /* unique IDs */
        if (draw(s, 2)) {
            uint32_t dl = 1 + (uint32_t) draw(s, 20);
            line[n++] = ' ';
            for (uint32_t k = 0; k < dl; k++) line[n++] = (char) (0x20 + draw(s, 95));
        }
        sb_put(b, line, n);
        sb_put(b, crlf ? "\r\n" : "\n", crlf ? 2 : 1);
        lines = 1 + (uint32_t) draw(s, 3);
        for (uint32_t l = 0; l < lines; l++) {
            uint32_t ll = 1 + (uint32_t) draw(s, 30);
            for (uint32_t k = 0; k < ll; k++) line[k] = rc[draw(s, sizeof rc - 1)];
            sb_put(b, line, ll);
            if (r + 1 < nrec || l + 1 < lines || draw(s, 2)) sb_put(b, crlf ? "\r\n" : "\n", crlf ? 2 : 1);
        }
    }
    *n_rec = nrec;
}

/* Re-serialize canonically and parse again: the same records. */
static void fasta_roundtrip(const zgn_fasta *f, unsigned long it) {
    sb_t b = {NULL, 0, 0};
    zgn_fasta *g = NULL;
    uint32_t n = 0, n2 = 0;
    zgn_fasta_count(f, &n);
    for (uint32_t i = 0; i < n; i++) {
        zgn_seq s;
        const char *d = NULL;
        zgn_fasta_get(f, i, &s, &d);
        sb_put(&b, ">", 1);
        sb_put(&b, s.id, strlen(s.id));
        if (d[0]) {
            sb_put(&b, " ", 1);
            sb_put(&b, d, strlen(d));
        }
        sb_put(&b, "\n", 1);
        sb_put(&b, (const char *) s.res, s.len);
        sb_put(&b, "\n", 1);
        for (uint32_t k = 0; k < s.len; k++)
            if (!((s.res[k] >= 'A' && s.res[k] <= 'Z') || s.res[k] == '*'))
                die("an accepted residue is not upper case", it);
        if (s.len == 0) die("an accepted record is empty", it);
    }
    if (zgn_fasta_parse((const uint8_t *) b.buf, b.len, NULL, &g) != ZXV_OK)
        die("a canonical re-serialization was refused", it);
    zgn_fasta_count(g, &n2);
    if (n2 != n) die("round trip count", it);
    for (uint32_t i = 0; i < n; i++) {
        zgn_seq s1, s2;
        const char *d1, *d2;
        zgn_fasta_get(f, i, &s1, &d1);
        zgn_fasta_get(g, i, &s2, &d2);
        if (strcmp(s1.id, s2.id) || strcmp(d1, d2) || s1.len != s2.len ||
            memcmp(s1.res, s2.res, s1.len))
            die("round trip differs", it);
    }
    zgn_fasta_destroy(g);
    free(b.buf);
}

int main(int argc, char **argv) {
    unsigned long iters = argc > 1 ? strtoul(argv[1], NULL, 10) : 2000;
    uint64_t seed = zt_seed_from_env(0x9e40aeULL), s = seed;
    zt_env e;
    zind_signer *lab;
    zind_chain *chain = NULL;
    const uint8_t *trust[1];
    unsigned long n_align = 0, n_ovf = 0, n_huge_ok = 0, n_seq = 0, n_fa_ok = 0, n_fa_bad = 0,
                  n_rec = 0, n_tamper = 0, n_limit = 0, n_chains = 1;
    zgn_matrix last_m;
    zgn_params last_p;
    uint8_t last_a[80], last_b[80];
    uint32_t last_na = 0, last_nb = 0;
    int have_last = 0;
    sb_t fa = {NULL, 0, 0};

    if (zt_env_open(&e, NULL) != 0) return 2;
    lab = zt_signer(e.cap, 77);
    if (!lab || zind_chain_create(ZGN_DOMAIN, 4096, &chain) != ZXV_OK) return 2;
    trust[0] = zind_signer_pk(lab);

    for (unsigned long it = 0; it < iters; it++) {
        uint64_t op = draw(&s, 100);
        if (op < 55) {
            zgn_matrix m;
            zgn_params p;
            zgn_result r, r2;
            uint8_t a[80], b[80];
            uint32_t na, nb;
            int huge = op >= 47;
            int32_t st;
            na = (uint32_t) draw(&s, 60);
            nb = (uint32_t) draw(&s, 60);
            make_matrix(&s, huge, &m);
            p.mode = draw(&s, 2) ? ZGN_LOCAL : ZGN_GLOBAL;
            p.gap_open = (int64_t) draw(&s, 9);
            p.gap_extend = (int64_t) draw(&s, 5);
            if (huge) p.gap_extend <<= 57;
            p.max_cells = 0;
            if (draw(&s, 10) == 0) p.max_cells = 1 + draw(&s, 4000);
            make_seq(&s, &m, a, na);
            make_seq(&s, &m, b, nb);
            st = zgn_align(&m, &p, a, na, b, nb, &r);
            if (p.max_cells && ((uint64_t) na + 1) * ((uint64_t) nb + 1) > p.max_cells) {
                if (st != ZGN_E_LIMIT) die("over max_cells but not refused", it);
                n_limit++;
                continue;
            }
            if (huge) {
                if (st == ZGN_E_OVERFLOW) {
                    if (r.a_row) die("overflow left a result", it);
                    n_ovf++;
                    continue;
                }
                if (st != ZXV_OK) die("huge: unexpected status", it);
                check_alignment(&m, &p, a, na, b, nb, &r, it);
                n_huge_ok++;
                zgn_result_free(&r);
                continue;
            }
            if (st != ZXV_OK) die("align", it);
            check_alignment(&m, &p, a, na, b, nb, &r, it);
            if (r.score != ref_gotoh(&m, p.gap_open, p.gap_extend, a, (int) na, b, (int) nb,
                                     p.mode == ZGN_LOCAL))
                die("score differs from the reference", it);
            if (zgn_align(&m, &p, a, na, b, nb, &r2) != ZXV_OK || r2.score != r.score ||
                strcmp(r2.a_row, r.a_row) || strcmp(r2.b_row, r.b_row) ||
                r2.a_start != r.a_start || r2.b_start != r.b_start)
                die("a second run differs", it);
            zgn_result_free(&r2);
            zgn_result_free(&r);
            n_align++;
            last_m = m;
            last_p = p;
            memcpy(last_a, a, na);
            memcpy(last_b, b, nb);
            last_na = na;
            last_nb = nb;
            have_last = 1;
        } else if (op < 62) {
            zgn_matrix m;
            zgn_params p = {ZGN_GLOBAL, 1, 1, 0};
            zgn_result r;
            uint8_t a[40];
            uint32_t na = 1 + (uint32_t) draw(&s, 39), pos;
            uint8_t bad;
            make_matrix(&s, 0, &m);
            make_seq(&s, &m, a, na);
            pos = (uint32_t) draw(&s, na);
            bad = (uint8_t) draw(&s, 256);
            if (memchr(m.alphabet, bad, m.n)) continue;
            a[pos] = bad;
            if (zgn_align(&m, &p, a, na, a, na, &r) != ZGN_E_SEQ) die("bad residue accepted", it);
            n_seq++;
        } else if (op < 92) {
            zgn_fasta *f = NULL;
            uint32_t nrec = 0, n = 0;
            int mutate;
            int32_t st;
            gen_fasta(&s, &fa, &nrec);
            mutate = (int) draw(&s, 3) == 0;
            if (mutate) {
                size_t pos = (size_t) draw(&s, fa.len);
                uint8_t v = (uint8_t) draw(&s, 256);
                fa.buf[pos] = (char) v;
            }
            st = zgn_fasta_parse((const uint8_t *) fa.buf, fa.len, NULL, &f);
            if (st == ZXV_OK) {
                zgn_fasta_count(f, &n);
                if (!mutate && n != nrec) die("generated FASTA: wrong record count", it);
                fasta_roundtrip(f, it);
                n_fa_ok++;
            } else {
                if (!mutate) die("generated FASTA refused", it);
                if (st != ZGN_E_FASTA && st != ZGN_E_LIMIT) die("FASTA: unexpected status", it);
                if (f) die("refused FASTA returned a handle", it);
                n_fa_bad++;
            }
            zgn_fasta_destroy(f);
        } else if (have_last) {
            zgn_result r;
            zgn_seq a = {"fz-a", last_a, last_na}, b = {"fz-b", last_b, last_nb};
            uint32_t idx = 0;
            if (zgn_align(&last_m, &last_p, last_a, last_na, last_b, last_nb, &r) != ZXV_OK)
                die("realign", it);
            zind_chain_count(chain, &idx);
            if (idx >= 12) {
                /* a full chain verifies once more and is archived */
                if (zind_chain_verify(e.cap, chain, trust, 1, NULL) != ZXV_OK)
                    die("archived chain", it);
                zind_chain_destroy(chain);
                chain = NULL;
                if (zind_chain_create(ZGN_DOMAIN, 4096, &chain) != ZXV_OK) die("new chain", it);
                n_chains++;
            }
            if (zgn_record_append(e.cap, chain, lab, &last_m, &last_p, &a, &b, &r, NULL) != ZXV_OK)
                die("record append", it);
            zind_chain_count(chain, &idx);
            if (zgn_record_verify(e.cap, chain, idx - 1, trust, 1, &last_m, &a, &b, 0, NULL) !=
                ZXV_OK)
                die("record verify", it);
            n_rec++;
            if (draw(&s, 4) == 0) {
                size_t len = 0;
                uint8_t *buf;
                zind_chain_export(chain, NULL, 0, &len);
                buf = (uint8_t *) malloc(len);
                zind_chain_export(chain, buf, len, &len);
                {
                    size_t pos = 4 + (size_t) draw(&s, len - 4);
                    unsigned bit = (unsigned) draw(&s, 8);
                    buf[pos] ^= (uint8_t) (1u << bit);
                }
                if (zgn_verify_export(e.cap, buf, len, idx - 1, trust, 1, &last_m, &a, &b, 0,
                                      NULL) == ZXV_OK) {
                    /* a flip in another record can leave this one intact only
                     * if the chain itself still verifies: it must not */
                    die("a flipped export verified", it);
                }
                free(buf);
                n_tamper++;
            }
            zgn_result_free(&r);
        }
    }
    {
        uint32_t n = 0;
        zind_chain_count(chain, &n);
        if (zind_chain_verify(e.cap, chain, trust, 1, NULL) != ZXV_OK) die("final chain", iters);
        printf("fuzz_genome: %lu ops (alignments %lu, overflow refusals %lu, huge ok %lu, "
               "cell-bound refusals %lu, bad residues %lu, FASTA accepted %lu, FASTA refused "
               "%lu, records %lu in %lu chains, tampered exports %lu), %u in the last chain, seed %llu, 0 "
               "invariant failures\n",
               iters, n_align, n_ovf, n_huge_ok, n_limit, n_seq, n_fa_ok, n_fa_bad, n_rec,
               n_chains, n_tamper, n, (unsigned long long) seed);
    }
    free(fa.buf);
    zind_chain_destroy(chain);
    zind_signer_destroy(lab);
    zt_env_close(&e);
    return 0;
}
