/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* fuzz_zxt.c — grammar fuzz for treaty Sutra text and records.
 *
 *   fuzz_zxt [cases] [seed]     (defaults: 1000000 cases, seed 1)
 *
 * Each case is one parse of one input, from three sources in turn:
 *   GEN  a random valid treaty from the grammar (1..16 parties, weights
 *        that are exact splits of a random denominator, 0..64 obligations,
 *        amounts across the whole PIC S9(18) range). Must parse, and both
 *        forms must round-trip to the same AST (zxt.h T3, T4).
 *   MUT  a generated text or record with 1..4 random edits (byte swaps
 *        from the grammar's alphabet and its neighbours, inserts, deletes,
 *        line drops, duplicates, swaps and truncation). Either refused with
 *        the output left all zero (T2), or accepted, in which case it must
 *        re-emit byte for byte (T3, one reading) and satisfy the rules,
 *        checked here a second way with 128-bit arithmetic (T5).
 *   RAW  random bytes, same oracle as MUT.
 * Deterministic for a given seed. Exit 0 when every case held. */
#include "zxt.h"

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
static uint64_t below(uint64_t n) { return n ? rnd() % n : 0; }

static uint64_t gcd(uint64_t a, uint64_t b)
{
    while (b) {
        uint64_t r = a % b;
        a = b;
        b = r;
    }
    return a;
}

static int64_t amount(bool allow_zero)
{
    static const int64_t edge[] = {1, 2, 9, 10, 99, 1000, 999999999, 1000000000,
                                   576460752303423488LL, ZXT_PIC18_MAX - 1, ZXT_PIC18_MAX};
    switch (below(4)) {
    case 0:
        return edge[below(sizeof edge / sizeof edge[0])];
    case 1:
        return (int64_t)below(1000) + (allow_zero ? 0 : 1);
    case 2:
        return (int64_t)below((uint64_t)ZXT_PIC18_MAX) + 1;
    default: {
        int64_t v = 1;
        for (uint64_t d = below(18); d > 0; d--)
            v = v * 10 + (int64_t)below(10);
        return v;
    }
    }
}

static void word(char *dst, uint32_t max)
{
    static const char tail[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
    uint32_t n = 1 + (uint32_t)below(max);
    dst[0] = (char)('A' + below(26));
    for (uint32_t i = 1; i < n; i++)
        dst[i] = tail[below(sizeof tail - 1)];
    if (dst[n - 1] == '-')
        dst[n - 1] = 'Z';
}

static void gen(zxt_treaty_t *t)
{
    zxt_clear(t);
    word(t->name, ZXT_NAME_MAX);
    for (int i = 0; i < 3; i++)
        t->asset[i] = (char)('A' + below(26));
    t->op = below(2) ? ZXT_OP_NET : ZXT_OP_SETL;
    t->n_party = (uint8_t)(1 + below(below(4) ? 4 : ZXT_MAX_PARTIES));
    /* weights: an exact split of a random denominator D */
    uint64_t D;
    switch (below(3)) {
    case 0: D = 1 + below(12); break;
    case 1: D = 1 + below(1000000); break;
    default: D = 1 + below(ZXT_WEIGHT_MAX); break;
    }
    uint64_t left = D;
    for (uint32_t i = 0; i < t->n_party; i++) {
        zxt_party_t *p = &t->party[i];
        word(p->id, ZXT_ID_MAX);
        bool dup;
        do {
            dup = false;
            for (uint32_t k = 0; k < i; k++)
                if (strcmp(t->party[k].id, p->id) == 0)
                    dup = true;
            if (dup) {
                memset(p->id, 0, sizeof p->id);
                word(p->id, ZXT_ID_MAX);
            }
        } while (dup);
        p->deposit = below(5) == 0 ? 0 : amount(true);
        p->claim = p->deposit == 0 ? 0 : (below(3) == 0 ? p->deposit : (int64_t)below((uint64_t)p->deposit + 1));
        uint64_t part = i + 1 == t->n_party ? left : below(left + 1);
        left -= part;
        uint64_t g = gcd(part, D);
        p->w_num = (uint32_t)(part / g);
        p->w_den = (uint32_t)(D / g);
    }
    t->n_owe = (uint8_t)(t->n_party < 2 ? 0 : below(below(4) ? 6 : ZXT_MAX_OWES + 1));
    for (uint32_t j = 0; j < t->n_owe; j++) {
        t->owe[j].from = (uint8_t)below(t->n_party);
        do
            t->owe[j].to = (uint8_t)below(t->n_party);
        while (t->owe[j].to == t->owe[j].from);
        t->owe[j].amount = amount(false);
    }
}

/* T5 a second way: 128-bit fractions, explicit loops. */
static bool rules_hold(const zxt_treaty_t *t)
{
    unsigned __int128 an = 0, ad = 1;
    if (t->n_party < 1 || t->n_party > ZXT_MAX_PARTIES || t->n_owe > ZXT_MAX_OWES)
        return false;
    for (uint32_t i = 0; i < t->n_party; i++) {
        const zxt_party_t *p = &t->party[i];
        if (p->deposit < 0 || p->deposit > ZXT_PIC18_MAX || p->claim < 0 || p->claim > p->deposit)
            return false;
        if (p->w_den == 0 || p->w_num > p->w_den || gcd(p->w_num, p->w_den) != 1)
            return false;
        for (uint32_t k = 0; k < i; k++)
            if (strcmp(t->party[k].id, p->id) == 0)
                return false;
        an = an * p->w_den + (unsigned __int128)p->w_num * ad;
        ad *= p->w_den;
        unsigned __int128 a = an, b = ad;
        while (b) {
            unsigned __int128 r = a % b;
            a = b;
            b = r;
        }
        an /= a;
        ad /= a;
    }
    if (an != ad)
        return false;
    for (uint32_t j = 0; j < t->n_owe; j++) {
        const zxt_owe_t *o = &t->owe[j];
        if (o->from >= t->n_party || o->to >= t->n_party || o->from == o->to || o->amount <= 0 ||
            o->amount > ZXT_PIC18_MAX)
            return false;
    }
    return true;
}

static bool all_zero(const void *p, size_t n)
{
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++)
        if (b[i])
            return false;
    return true;
}

static const char ALPHA[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/ \n"
                            "TREATYPARTOWENDSL+az\r\t\x00\xff.";

/* Edit buf (length *n, capacity cap) in place. */
static void mutate(char *buf, uint32_t *n, uint32_t cap, bool lines)
{
    uint32_t edits = 1 + (uint32_t)below(4);
    for (uint32_t e = 0; e < edits; e++) {
        uint32_t len = *n, op = (uint32_t)below(lines ? 7 : 4);
        if (len == 0)
            return;
        uint32_t at = (uint32_t)below(len);
        switch (op) {
        case 0: /* replace */
            buf[at] = ALPHA[below(sizeof ALPHA - 1)];
            break;
        case 1: /* replace with a digit (numbers mutate into numbers) */
            buf[at] = (char)('0' + below(10));
            break;
        case 2: /* insert */
            if (len < cap) {
                memmove(buf + at + 1, buf + at, len - at);
                buf[at] = ALPHA[below(sizeof ALPHA - 1)];
                (*n)++;
            }
            break;
        case 3: /* delete */
            if (!lines)
                break;
            memmove(buf + at, buf + at + 1, len - at - 1);
            (*n)--;
            break;
        default: { /* line ops: drop, duplicate or truncate */
            uint32_t s = at;
            while (s > 0 && buf[s - 1] != '\n')
                s--;
            uint32_t end = at;
            while (end < len && buf[end] != '\n')
                end++;
            if (end < len)
                end++;
            uint32_t ll = end - s;
            if (op == 4) {
                memmove(buf + s, buf + end, len - end);
                *n -= ll;
            } else if (op == 5 && len + ll <= cap) {
                memmove(buf + end + ll, buf + end, len - end);
                memcpy(buf + end, buf + s, ll);
                *n += ll;
            } else {
                *n = at;
            }
        }
        }
    }
}

int main(int argc, char **argv)
{
    uint64_t cases = argc > 1 ? strtoull(argv[1], NULL, 10) : 1000000ULL;
    g_rng = (argc > 2 ? strtoull(argv[2], NULL, 10) : 1ULL) * 0x9E3779B97F4A7C15ULL + 1;
    static char txt[ZXT_SUTRA_MAX + 64], txt2[ZXT_SUTRA_MAX + 64];
    static uint8_t rec[ZXT_REC_LEN], rec2[ZXT_REC_LEN];
    zxt_treaty_t t, u, v;
    uint64_t fails = 0, gen_n = 0, mut_ok = 0, mut_rej = 0, raw_n = 0;
    uint32_t n = 0, n2 = 0, off;
#define BAD(...)                                                                                   \
    do {                                                                                           \
        if (fails++ < 10) {                                                                        \
            printf("case %llu: ", (unsigned long long)c);                                          \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

    for (uint64_t c = 0; c < cases; c++) {
        uint32_t kind = (uint32_t)(c % 8);
        if (kind == 0 || kind == 4) { /* GEN */
            gen(&t);
            gen_n++;
            if (zxt_validate(&t) != ZXT_OK || !rules_hold(&t)) {
                BAD("generator made an invalid treaty (%d)", zxt_validate(&t));
                continue;
            }
            if (zxt_emit_sutra(&t, txt, sizeof txt, &n) != ZXT_OK || zxt_parse_sutra(txt, n, &u, &off) != ZXT_OK ||
                !zxt_equal(&t, &u))
                BAD("sutra round trip");
            if (zxt_emit_record(&t, rec, sizeof rec) != ZXT_OK ||
                zxt_parse_record(rec, sizeof rec, &v, &off) != ZXT_OK || !zxt_equal(&t, &v))
                BAD("record round trip");
            if (zxt_emit_sutra(&v, txt2, sizeof txt2, &n2) != ZXT_OK || n2 != n || memcmp(txt, txt2, n) != 0)
                BAD("record -> sutra differs");
            continue;
        }
        if (kind == 7) { /* RAW */
            raw_n++;
            n2 = (uint32_t)below(96);
            for (uint32_t i = 0; i < n2; i++)
                txt2[i] = below(2) ? ALPHA[below(sizeof ALPHA - 1)] : (char)rnd();
            if (below(2))
                memcpy(txt2, "TREATY ", n2 < 7 ? n2 : 7);
        } else if (kind == 6) { /* MUT record */
            memcpy(rec2, rec, sizeof rec);
            uint32_t k = 1 + (uint32_t)below(3);
            for (uint32_t e = 0; e < k; e++)
                rec2[below(sizeof rec2)] = (uint8_t)("0123456789 +-AZ\0x"[below(17)]);
            memset(&u, 0xA5, sizeof u);
            zxt_status_t st = zxt_parse_record(rec2, sizeof rec2, &u, &off);
            if (st == ZXT_OK) {
                mut_ok++;
                if (!rules_hold(&u) || zxt_emit_record(&u, rec, sizeof rec) != ZXT_OK ||
                    memcmp(rec, rec2, sizeof rec) != 0)
                    BAD("accepted record is not canonical or breaks a rule");
            } else {
                mut_rej++;
                if (!all_zero(&u, sizeof u) || st >= 0 || off > ZXT_REC_LEN)
                    BAD("refused record left state (%d@%u)", st, off);
            }
            continue;
        } else { /* MUT sutra */
            memcpy(txt2, txt, n);
            n2 = n;
            mutate(txt2, &n2, sizeof txt2, true);
        }
        memset(&u, 0xA5, sizeof u);
        zxt_status_t st = zxt_parse_sutra(txt2, n2, &u, &off);
        if (st == ZXT_OK) {
            mut_ok++;
            uint32_t m = 0;
            if (!rules_hold(&u) || zxt_emit_sutra(&u, txt, sizeof txt, &m) != ZXT_OK || m != n2 ||
                memcmp(txt, txt2, m) != 0)
                BAD("accepted text is not canonical or breaks a rule");
            if (zxt_emit_record(&u, rec, sizeof rec) != ZXT_OK || zxt_parse_record(rec, sizeof rec, &v, &off) != ZXT_OK ||
                !zxt_equal(&u, &v))
                BAD("accepted text does not lower to the record");
            n = m;
        } else {
            mut_rej++;
            if (!all_zero(&u, sizeof u) || st >= 0 || (off > n2 && st != ZXT_E_ARG))
                BAD("refused text left state (%d@%u of %u)", st, off, n2);
        }
    }
    printf("%s: %llu cases (%llu generated, %llu mutants accepted, %llu refused, %llu raw), %llu failures\n",
           fails ? "FAIL" : "PASS", (unsigned long long)cases, (unsigned long long)gen_n,
           (unsigned long long)mut_ok, (unsigned long long)mut_rej, (unsigned long long)raw_n,
           (unsigned long long)fails);
    return fails ? 1 : 0;
}
