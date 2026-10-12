/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_zxp.c — grammar fuzz for contract Sutra text and records.
 *
 *   fuzz_zxp [cases] [seed]     (defaults: 1000000 cases, seed 1)
 *
 *   GEN  a random valid contract built from the rules (every section at
 *        random sizes up to its limit, GATEs and AFTER links to earlier
 *        clauses, amounts across PIC S9(18)). Must validate, emit in both
 *        forms, and parse back from each to the same AST (zxp.h C3, C4).
 *   MUT  a generated text or record with 1..4 random edits. Either refused
 *        with the output all zero (C2), or accepted, and then it must
 *        validate and re-emit byte for byte (C3).
 *   RAW  random bytes, same oracle as MUT.
 * Deterministic for a seed. Exit 0 when every case held. */
#include "zxp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t g_rng;
static uint64_t rnd(void)
{
    g_rng ^= g_rng >> 12;
    g_rng ^= g_rng << 25;
    g_rng ^= g_rng >> 27;
    return g_rng * 2685821657736338717ULL;
}
static uint64_t below(uint64_t n)
{
    return n ? rnd() % n : 0;
}

static int64_t amount(void)
{
    static const int64_t edge[] = {1, 9, 10, 999999999, 1000000000, ZXP_PIC18_MAX - 1, ZXP_PIC18_MAX};
    if (below(4) == 0) return edge[below(sizeof edge / sizeof edge[0])];
    return 1 + (int64_t) below((uint64_t) ZXP_PIC18_MAX >> below(60));
}

static void name(char *dst, uint32_t max)
{
    static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
    uint32_t n = 1 + (uint32_t) below(max);
    for (uint32_t i = 0; i < n; i++) dst[i] = A[i == 0 ? below(26) : below(37)];
    if (dst[n - 1] == '-') dst[n - 1] = 'Z';
    dst[n] = 0;
}

static void jur(char *dst)
{
    static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    memset(dst, 0, ZXP_JUR_MAX + 1u);
    dst[0] = (char) ('A' + below(26));
    dst[1] = (char) ('A' + below(26));
    if (below(2)) {
        uint32_t n = 1 + (uint32_t) below(3);
        dst[2] = '-';
        for (uint32_t i = 0; i < n; i++) dst[3 + i] = A[below(36)];
    }
}

static uint32_t date(uint32_t lo, uint32_t span)
{
    uint32_t d = zxp_date_add(lo, (int32_t) below(span + 1u));
    return d ? d : lo;
}

static void asset(char *a)
{
    for (int i = 0; i < 3; i++) a[i] = (char) ('A' + below(i == 0 ? 3 : 26));
    a[3] = 0;
}

static int cmp_jur(const void *a, const void *b)
{
    return strcmp((const char *) a, (const char *) b);
}

/* Sort and drop duplicates in a tag list; returns the new count. */
static uint8_t uniq_tags(char (*t)[ZXP_JUR_MAX + 1u], uint32_t n)
{
    qsort(t, n, ZXP_JUR_MAX + 1u, cmp_jur);
    uint32_t k = 0;
    for (uint32_t i = 0; i < n; i++)
        if (k == 0 || strcmp(t[k - 1], t[i]) != 0) memmove(t[k++], t[i], ZXP_JUR_MAX + 1u);
    for (uint32_t i = k; i < n; i++) memset(t[i], 0, ZXP_JUR_MAX + 1u);
    return (uint8_t) k;
}

static void gen(zxp_contract_t *c)
{
    memset(c, 0, sizeof *c);
    name(c->name, ZXP_NAME_MAX);
    c->date = date(19900101, 20000);
    for (int i = 0; i < 32; i++) c->doc[i] = (uint8_t) rnd();
    uint32_t nl = 1 + (uint32_t) below(ZXP_MAX_LAWS);
    for (uint32_t i = 0; i < nl; i++) jur(c->law[i]);
    c->n_law = uniq_tags(c->law, nl);
    c->n_party = (uint8_t) (1 + below(ZXP_MAX_PARTIES));
    for (uint32_t i = 0; i < c->n_party; i++) {
        do {
            memset(c->party[i].id, 0, sizeof c->party[i].id);
            name(c->party[i].id, ZXP_ID_MAX);
            bool dup = strcmp(c->party[i].id, ZXP_ESCROW_ID) == 0;
            for (uint32_t k = 0; k < i; k++) dup = dup || strcmp(c->party[k].id, c->party[i].id) == 0;
            if (!dup) break;
        } while (1);
        jur(c->party[i].jur);
    }
    c->sign = (uint8_t) (1 + below(c->n_party));
    c->window.from = date(19900101, 30000);
    c->window.to = date(c->window.from, 3000);
    uint32_t d = 19900101;
    c->n_lock = (uint8_t) below(ZXP_MAX_LOCKS + 1u);
    for (uint32_t i = 0; i < c->n_lock; i++) {
        c->lock[i].from = date(zxp_date_add(d, 1), 400);
        c->lock[i].to = date(c->lock[i].from, 30);
        d = c->lock[i].to;
    }
    uint32_t nc = (uint32_t) below(ZXP_MAX_CAPS + 1u);
    for (uint32_t i = 0; i < nc; i++) {
        asset(c->cap[i].asset);
        c->cap[i].amount = amount();
    }
    for (uint32_t i = 0; i < nc; i++) /* insertion sort by asset, drop dups */
        for (uint32_t k = i; k > 0 && strcmp(c->cap[k - 1].asset, c->cap[k].asset) > 0; k--) {
            zxp_cap_t x = c->cap[k];
            c->cap[k] = c->cap[k - 1];
            c->cap[k - 1] = x;
        }
    for (uint32_t i = 0; i < nc; i++)
        if (c->n_cap == 0 || strcmp(c->cap[c->n_cap - 1].asset, c->cap[i].asset) != 0) c->cap[c->n_cap++] = c->cap[i];
    for (uint32_t i = c->n_cap; i < ZXP_MAX_CAPS; i++) memset(&c->cap[i], 0, sizeof c->cap[i]);
    uint32_t nm = (uint32_t) below(ZXP_MAX_LIMITS + 1u);
    for (uint32_t i = 0; i < nm; i++) {
        zxp_limit_t l;
        memset(&l, 0, sizeof l);
        l.party = (uint8_t) below(c->n_party);
        asset(l.asset);
        l.amount = amount();
        uint32_t k = c->n_limit;
        bool dup = false;
        for (uint32_t j = 0; j < c->n_limit; j++)
            dup = dup || (c->limit[j].party == l.party && strcmp(c->limit[j].asset, l.asset) == 0);
        if (dup) continue;
        while (k > 0 && (c->limit[k - 1].party > l.party ||
                         (c->limit[k - 1].party == l.party && strcmp(c->limit[k - 1].asset, l.asset) > 0))) {
            c->limit[k] = c->limit[k - 1];
            k--;
        }
        c->limit[k] = l;
        c->n_limit++;
    }
    uint32_t na = (uint32_t) below(ZXP_MAX_TAGS + 1u), nd = (uint32_t) below(ZXP_MAX_TAGS + 1u);
    for (uint32_t i = 0; i < na; i++) jur(c->allow[i]);
    for (uint32_t i = 0; i < nd; i++) jur(c->deny[i]);
    c->n_allow = uniq_tags(c->allow, na);
    c->n_deny = uniq_tags(c->deny, nd);
    c->n_clause = (uint8_t) below(ZXP_MAX_CLAUSES + 1u);
    for (uint32_t i = 0; i < c->n_clause; i++) {
        zxp_clause_t *k = &c->clause[i];
        do {
            memset(k->id, 0, sizeof k->id);
            name(k->id, ZXP_ID_MAX);
            bool dup = false;
            for (uint32_t j = 0; j < i; j++) dup = dup || strcmp(c->clause[j].id, k->id) == 0;
            if (!dup) break;
        } while (1);
        if (i >= 2 && below(4) == 0) {
            k->kind = ZXP_CL_GATE;
            k->op = (uint8_t) (1 + below(2));
            k->n_ref = (uint8_t) (2 + below((i < ZXP_MAX_REFS ? i : ZXP_MAX_REFS) - 1u));
            for (uint32_t r = 0; r < k->n_ref; r++) {
                uint8_t x;
                bool dup;
                do {
                    x = (uint8_t) below(i);
                    dup = false;
                    for (uint32_t q = 0; q < r; q++) dup = dup || k->ref[q] == x;
                } while (dup);
                k->ref[r] = x;
            }
        } else if (c->n_party >= 2) {
            k->kind = ZXP_CL_DUE;
            k->payer = (uint8_t) below(c->n_party);
            do k->payee = (uint8_t) below(c->n_party);
            while (k->payee == k->payer);
            asset(k->asset);
            k->amount = amount();
            k->date = date(c->date, 5000);
            k->penalty = below(2) ? amount() : 0;
            k->after = i > 0 && below(2) ? (uint8_t) (1 + below(i)) : 0;
        } else {
            c->n_clause = (uint8_t) i;
            memset(k, 0, sizeof *k);
            break;
        }
    }
}

static bool all_zero(const void *p, size_t n)
{
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++)
        if (b[i]) return false;
    return true;
}

static uint64_t g_fail;
static void bad(const char *what, uint64_t i)
{
    if (g_fail++ < 10) printf("FAIL case %llu: %s\n", (unsigned long long) i, what);
}

static zxp_contract_t A, B;
static char TEXT[ZXP_SUTRA_MAX + 64], TEXT2[ZXP_SUTRA_MAX + 64];
static uint8_t REC[ZXP_REC_LEN], REC2[ZXP_REC_LEN];

static void oracle_text(const char *s, uint32_t n, uint64_t i)
{
    uint32_t off, m;
    memset(&B, 0x5A, sizeof B);
    if (zxp_parse_sutra(s, n, &B, &off) != ZXP_OK) {
        if (!all_zero(&B, sizeof B)) bad("refused text left data", i);
        if (off > n) bad("fault offset past the input", i);
        return;
    }
    if (zxp_validate(&B) != ZXP_OK) bad("accepted text does not validate", i);
    if (zxp_emit_sutra(&B, TEXT2, sizeof TEXT2, &m) != ZXP_OK || m != n || memcmp(TEXT2, s, n) != 0)
        bad("accepted text does not re-emit byte for byte", i);
}

static void oracle_rec(const uint8_t *r, uint64_t i)
{
    uint32_t off;
    memset(&B, 0x5A, sizeof B);
    if (zxp_parse_record(r, ZXP_REC_LEN, &B, &off) != ZXP_OK) {
        if (!all_zero(&B, sizeof B)) bad("refused record left data", i);
        if (off >= ZXP_REC_LEN) bad("record fault offset out of range", i);
        return;
    }
    if (zxp_validate(&B) != ZXP_OK) bad("accepted record does not validate", i);
    if (zxp_emit_record(&B, REC2, sizeof REC2) != ZXP_OK || memcmp(REC2, r, ZXP_REC_LEN) != 0)
        bad("accepted record does not re-emit byte for byte", i);
}

static uint32_t mutate(char *s, uint32_t n, uint32_t cap)
{
    static const char ALPHA[] = "ACDEGLNOPRSTUWY0123456789 -\nKEFIM";
    uint32_t edits = 1 + (uint32_t) below(4);
    for (uint32_t e = 0; e < edits && n > 0; e++) {
        uint32_t at = (uint32_t) below(n);
        switch (below(6)) {
        case 0: s[at] = ALPHA[below(sizeof ALPHA - 1)]; break;
        case 1: s[at] = (char) (s[at] + (below(2) ? 1 : -1)); break;
        case 2: /* delete */
            memmove(s + at, s + at + 1, n - at - 1);
            n--;
            break;
        case 3: /* insert */
            if (n + 1 < cap) {
                memmove(s + at + 1, s + at, n - at);
                s[at] = ALPHA[below(sizeof ALPHA - 1)];
                n++;
            }
            break;
        case 4: { /* drop a line */
            uint32_t b = at, en = at;
            while (b > 0 && s[b - 1] != '\n') b--;
            while (en < n && s[en] != '\n') en++;
            if (en < n) en++;
            memmove(s + b, s + en, n - en);
            n -= en - b;
            break;
        }
        default: /* truncate */
            n = at;
            break;
        }
    }
    return n;
}

int main(int argc, char **argv)
{
    uint64_t cases = argc > 1 ? strtoull(argv[1], NULL, 10) : 1000000ULL;
    g_rng = (argc > 2 ? strtoull(argv[2], NULL, 10) : 1ULL) * 0x9E3779B97F4A7C15ULL + 1;
    uint64_t gen_n = 0, acc = 0;
    for (uint64_t i = 0; i < cases; i++) {
        uint32_t kind = (uint32_t) (i % 5u), n = 0;
        if (kind <= 3) {
            gen(&A);
            if (zxp_validate(&A) != ZXP_OK) {
                bad("generated contract does not validate", i);
                continue;
            }
            if (zxp_emit_sutra(&A, TEXT, sizeof TEXT, &n) != ZXP_OK || zxp_emit_record(&A, REC, sizeof REC) != ZXP_OK) {
                bad("generated contract does not emit", i);
                continue;
            }
        }
        if (kind == 0) {
            gen_n++;
            uint32_t off;
            if (zxp_parse_sutra(TEXT, n, &B, &off) != ZXP_OK || !zxp_equal(&A, &B))
                bad("text round trip", i);
            if (zxp_parse_record(REC, sizeof REC, &B, &off) != ZXP_OK || !zxp_equal(&A, &B))
                bad("record round trip", i);
            for (uint32_t k = 0; k < A.n_clause; k++) {
                int32_t o = zxp_clause_offset(&A, k);
                if (o < 0 || memcmp(TEXT + o, A.clause[k].kind == ZXP_CL_DUE ? "DUE " : "GATE ", 4) != 0) {
                    bad("clause offset", i);
                    break;
                }
            }
        } else if (kind == 1 || kind == 2) {
            n = mutate(TEXT, n, sizeof TEXT);
            oracle_text(TEXT, n, i);
            acc += zxp_parse_sutra(TEXT, n, &B, NULL) == ZXP_OK;
        } else if (kind == 3) {
            uint32_t edits = 1 + (uint32_t) below(4);
            for (uint32_t e = 0; e < edits; e++) {
                static const char ALPHA[] = "0123456789+- ABDEGLNUY";
                REC[below(ZXP_REC_LEN)] = (uint8_t) ALPHA[below(sizeof ALPHA - 1)];
            }
            oracle_rec(REC, i);
        } else {
            n = (uint32_t) below(600);
            for (uint32_t k = 0; k < n; k++) TEXT[k] = (char) below(256);
            if (below(2)) memcpy(TEXT, "CONTRACT ", n >= 9 ? 9 : n);
            oracle_text(TEXT, n, i);
            for (uint32_t k = 0; k < ZXP_REC_LEN; k++) REC[k] = (uint8_t) below(256);
            memcpy(REC, ZXP_REC_MAGIC, 4);
            oracle_rec(REC, i);
        }
    }
    printf("%s: %llu cases (%llu generated round trips, %llu mutated texts accepted), %llu failures\n",
           g_fail ? "FAIL" : "PASS", (unsigned long long) cases, (unsigned long long) gen_n,
           (unsigned long long) acc, (unsigned long long) g_fail);
    return g_fail ? 1 : 0;
}
