/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zl_sutra.c — the Dhātu view of the Lingua IR (zl_sutra.h).
 *
 *   test_zl_sutra ALPHABET.TSV [TABLE.MD]
 *
 * One cell per operation and one operation per cell; the layout pinned by
 * a fixed answer; every operation's kāraka against its stack effect in
 * alphabet.tsv. With TABLE.MD, also writes the table for ALPHABET.md. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zl.h"
#include "zl_sutra.h"
#include "zl_kpy.h"
#include "lpres.h"

static unsigned checks, fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("[FAIL] " __VA_ARGS__);                                                        \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static char NAME[ZL_OP__COUNT][16], FAMILY[ZL_OP__COUNT][16], POPS[ZL_OP__COUNT][4],
    PUSHES[ZL_OP__COUNT][4], MEANING[ZL_OP__COUNT][128];

static uint32_t read_alphabet(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[512];
    uint32_t n = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        line[strcspn(line, "\n")] = 0;
        char *fld[6] = {0};
        char *p = line;
        for (int i = 0; i < 6 && p; i++) {
            fld[i] = p;
            p = strchr(p, '\t');
            if (p) *p++ = 0;
        }
        if (n < ZL_OP__COUNT && fld[5]) {
            snprintf(NAME[n], sizeof NAME[n], "%s", fld[0]);
            snprintf(FAMILY[n], sizeof FAMILY[n], "%s", fld[1]);
            snprintf(POPS[n], sizeof POPS[n], "%s", fld[2]);
            snprintf(PUSHES[n], sizeof PUSHES[n], "%s", fld[3]);
            snprintf(MEANING[n], sizeof MEANING[n], "%s", fld[5]);
        }
        n++;
    }
    fclose(f);
    return n;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: test_zl_sutra ALPHABET.TSV [TABLE.MD]\n");
        return 2;
    }
    uint32_t n = read_alphabet(argv[1]);
    CHECK(n == ZL_OP__COUNT && zl_sutra_count() == ZL_OP__COUNT, "%u alphabet rows, %u cells, %u ops", n,
          zl_sutra_count(), (unsigned) ZL_OP__COUNT);
    if (fails) return 1;

    /* one cell per operation, one operation per cell, any evidence tag */
    for (uint32_t op = 0; op < ZL_OP__COUNT; op++)
        for (uint32_t b = 0; b < 4; b++) {
            uint64_t c = zl_sutra_cell(op, b);
            zl_sutra_t s;
            zl_sutra_unpack(c, &s);
            CHECK(c && zl_sutra_op(c) == (int32_t) op && s.lpres == b, "%s cell %u", NAME[op], b);
            for (uint32_t q = 0; q < op; q++)
                CHECK(zl_sutra_cell(q, b) != c, "%s and %s share a cell", NAME[op], NAME[q]);
        }
    CHECK(zl_sutra_cell(ZL_OP__COUNT, 0) == 0 && zl_sutra_cell(0, 4) == 0, "no cell for an unknown op or tag");

    /* the layout, pinned: ADD (√kal, root 5, with sam-, karman the new top, karaṇa the
     * top, apādāna the second) with evidence T */
    {
        uint64_t want = 5ull | 4ull << 12 | 0ull << 17 | 5ull << (25 + 4) | 1ull << (25 + 8) |
                        2ull << (25 + 16) | 1ull << 60;
        CHECK(zl_sutra_cell(ZL_ADD, 1) == want && !strcmp(zl_sutra_root(ZL_ADD), "kal") &&
                  !strcmp(zl_sutra_upasarga(4), "sam"),
              "ADD's cell is %016llx", (unsigned long long) zl_sutra_cell(ZL_ADD, 1));
    }

    /* pack and unpack */
    uint64_t r = 99;
    for (uint32_t i = 0; i < 100000; i++) {
        r = r * 6364136223846793005ull + 1442695040888963407ull;
        zl_sutra_t s, t;
        s.dhatu = (uint32_t) (r >> 20) & 0xFFFu;
        s.upasarga = (uint32_t) (r >> 40) % 23u;
        s.pratyaya = (uint32_t) (r >> 8) & 0xFFu;
        s.sandhi = (uint32_t) (r >> 50) & 0x7FFu;
        s.lpres = (uint32_t) (r >> 33) & 3u;
        s.svara = (uint32_t) (r >> 61) & 3u;
        for (uint32_t k = 0; k < 6; k++) s.karaka[k] = (uint8_t) ((r >> (4 * k)) & 15u);
        uint64_t c = zl_sutra_pack(&s);
        zl_sutra_unpack(c, &t);
        CHECK(!memcmp(&s.karaka, &t.karaka, 6) && s.dhatu == t.dhatu && s.upasarga == t.upasarga &&
                  s.pratyaya == t.pratyaya && s.sandhi == t.sandhi && s.lpres == t.lpres &&
                  s.svara == t.svara && zl_sutra_pack(&t) == c,
              "pack round trip %u", i);
        if (fails) break;
    }
    {
        zl_sutra_t s = {0, 23, 0, 0, 0, 0, {0}};
        CHECK(zl_sutra_pack(&s) == 0, "upasarga 23");
        s.upasarga = 0, s.dhatu = 4096;
        CHECK(zl_sutra_pack(&s) == 0, "dhātu 4096");
        s.dhatu = 0, s.sandhi = 2048;
        CHECK(zl_sutra_pack(&s) == 0, "sandhi 2048");
        s.sandhi = 0, s.karaka[3] = 16;
        CHECK(zl_sutra_pack(&s) == 0 && zl_sutra_pack(0) == 0, "an operand code 16");
    }
    /* a cell that is not exactly one operation's is refused */
    {
        uint64_t c = zl_sutra_cell(ZL_LOAD, 0);
        CHECK(zl_sutra_op(c | 1ull << 49) < 0, "sandhi set");
        CHECK(zl_sutra_op(c | 1ull << 62) < 0, "svara set");
        CHECK(zl_sutra_op(c ^ 1ull << 29) < 0, "another kāraka");
        CHECK(zl_sutra_op(c ^ 1ull << 12) < 0, "another upasarga");
        CHECK(zl_sutra_op(0xFFFull) < 0, "an unassigned root");
    }

    /* each operation's kāraka against its stack effect */
    for (uint32_t op = 0; op < ZL_OP__COUNT; op++) {
        zl_sutra_t s;
        zl_sutra_unpack(zl_sutra_cell(op, 0), &s);
        CHECK(s.karaka[ZL_KARTR] == ZL_K_NONE, "%s: the agent of an IR op is the running program", NAME[op]);
        CHECK(*zl_sutra_root(op) && *zl_sutra_gloss(op), "%s has a root", NAME[op]);
        if (POPS[op][0] == '*' || PUSHES[op][0] == '*') continue;
        uint32_t pops = (uint32_t) atoi(POPS[op]), pushes = (uint32_t) atoi(PUSHES[op]), seen = 0, top = 0;
        for (uint32_t k = 0; k < 6; k++) {
            uint32_t c = s.karaka[k];
            if (c >= ZL_K_S1 && c <= ZL_K_S3) {
                CHECK(!(seen & 1u << c), "%s names stack cell %u twice", NAME[op], c);
                seen |= 1u << c;
            }
            CHECK(c != ZL_K_SN, "%s has a fixed stack effect", NAME[op]);
            top += c == ZL_K_TOP;
        }
        CHECK(seen == ((1u << (pops + 1)) - 2u), "%s: kāraka names the %u cells it pops", NAME[op], pops);
        CHECK((top == 1) == (pushes > 0) && top <= 1, "%s: a result iff it pushes", NAME[op]);
    }

    /* pooling evidence is lpres's rule: replay each side's evidence into an
     * lpres attestation and compare */
    for (uint32_t a = 0; a < 4; a++)
        for (uint32_t b = 0; b < 4; b++) {
            static lpres_registry_t reg;
            lpres_registry_init(&reg);
            int32_t i = lpres_attest(&reg, "x", LPRES_SUBJECT_SOURCE, LPRES_STATE_NEITHER, "t", 0, 0);
            for (uint32_t s = 0; s < 2; s++) {
                uint32_t t = s ? b : a;
                if (t & 1u) lpres_add_evidence(&reg, (uint32_t) i, true);
                if (t & 2u) lpres_add_evidence(&reg, (uint32_t) i, false);
            }
            uint32_t want = (uint32_t) lpres_get(&reg, (uint32_t) i)->state;
            CHECK(i >= 0 && zl_sutra_evidence(a, b) == want, "evidence %u + %u = %u, lpres says %u", a, b,
                  zl_sutra_evidence(a, b), want);
        }
    /* the fuse: one reading pools its tags; two readings are both kept */
    for (uint32_t op = 0; op < ZL_OP__COUNT; op++)
        for (uint32_t a = 0; a < 4; a++)
            for (uint32_t b = 0; b < 4; b++) {
                uint64_t out[2] = {0, 0}, q = zl_sutra_cell((op * 7u + 3u) % ZL_OP__COUNT, b);
                int32_t n = zl_sutra_fuse(zl_sutra_cell(op, a), zl_sutra_cell(op, b), out);
                CHECK(n == 1 && out[0] == zl_sutra_cell(op, zl_sutra_evidence(a, b)), "%s fuses %u and %u", NAME[op],
                      a, b);
                if (zl_sutra_op(q) == (int32_t) op) continue;
                n = zl_sutra_fuse(zl_sutra_cell(op, a), q, out);
                if (!a != !b) {
                    CHECK(n == 1 && out[0] == (a ? zl_sutra_cell(op, a) : q), "%s: only one side claims", NAME[op]);
                } else {
                    uint32_t t = a ? 3u : 0u;
                    CHECK(n == 2 && out[0] == zl_sutra_cell(op, t) &&
                              zl_sutra_op(out[1]) == zl_sutra_op(q) && (out[1] >> 60) == t,
                          "%s against another reading, tags %u and %u", NAME[op], a, b);
                }
            }
    {
        uint64_t out[2];
        CHECK(zl_sutra_fuse(zl_sutra_cell(ZL_ADD, 1) | 1ull << 50, zl_sutra_cell(ZL_ADD, 1), out) < 0 &&
                  zl_sutra_fuse(zl_sutra_cell(ZL_ADD, 1), zl_sutra_cell(ZL_ADD, 1) | 1ull << 63, out) < 0 &&
                  zl_sutra_fuse(1, 1, 0) < 0,
              "fuse refuses reserved bits");
        /* the classic case: one source says T, the other F, same reading */
        CHECK(zl_sutra_fuse(zl_sutra_cell(ZL_ADD, 1), zl_sutra_cell(ZL_ADD, 2), out) == 1 &&
                  out[0] == zl_sutra_cell(ZL_ADD, 3),
              "T with F is B");
    }

    if (argc > 2) { /* the table for ALPHABET.md */
        FILE *f = fopen(argv[2], "w");
        fprintf(f, "| Op | Family | Root | Sense | Kaṭapayādi | Upasarga | Pratyaya | Kāraka | Cell (N) |\n");
        fprintf(f, "|---|---|---|---|---|---|---|---|---|\n");
        for (uint32_t op = 0; op < ZL_OP__COUNT; op++) {
            zl_sutra_t s;
            uint64_t c = zl_sutra_cell(op, 0);
            zl_sutra_unpack(c, &s);
            uint64_t kv = 0;
            zl_kpy_value(zl_sutra_root(op), &kv);
            char k[7];
            for (uint32_t i = 0; i < 6; i++) k[i] = zl_sutra_kchar(s.karaka[i]);
            k[6] = 0;
            fprintf(f, "| %s | %s | √%s | %s | %llu | %s | %s | `%s` | `%016llx` |\n", NAME[op], FAMILY[op],
                    zl_sutra_root(op), zl_sutra_gloss(op), (unsigned long long) kv, *zl_sutra_upasarga(s.upasarga) ? zl_sutra_upasarga(s.upasarga) : "",
                    zl_sutra_pratyaya(s.pratyaya), k, (unsigned long long) c);
        }
        fclose(f);
    }
    printf("%u checks, %u failures\n", checks, fails);
    return fails != 0;
}
