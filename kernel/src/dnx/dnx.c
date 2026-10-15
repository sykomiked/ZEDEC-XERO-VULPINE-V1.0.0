/* dnx.c — duplex digital DNA (see dnx.h for the design, D1-D5).
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

#include "dnx.h"

#include <stddef.h>

/* ATG then GTCCA: starts with the start codon; not its own reverse
 * complement (TGGACCAT), so a one-way strand never shows a magic backwards. */
static const uint8_t DNX_MAGIC[DNX_MAGIC_BASES] = {DNX_A, DNX_T, DNX_G, DNX_G,
                                                   DNX_T, DNX_C, DNX_C, DNX_A};
/* GAATTC, the EcoRI site: its own reverse complement. */
static const uint8_t DNX_PIVOT[DNX_PIVOT_BASES] = {DNX_G, DNX_A, DNX_A, DNX_T, DNX_T, DNX_C};

/* ---- D1 conversion ---- */

int32_t dnx_bytes_to_bases(const uint8_t *in, uint32_t n, uint8_t *bases, uint32_t cap)
{
    if ((in == NULL && n != 0) || bases == NULL || n > DNX_MAX_PAYLOAD) return DNX_E_ARG;
    if (cap < 4u * n) return DNX_E_CAP;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t b = in[i];
        bases[4u * i + 0u] = (uint8_t) ((b >> 6) & 3u);
        bases[4u * i + 1u] = (uint8_t) ((b >> 4) & 3u);
        bases[4u * i + 2u] = (uint8_t) ((b >> 2) & 3u);
        bases[4u * i + 3u] = (uint8_t) (b & 3u);
    }
    return (int32_t) (4u * n);
}

int32_t dnx_bases_to_bytes(const uint8_t *bases, uint32_t n, uint8_t *out, uint32_t cap)
{
    if ((bases == NULL && n != 0) || out == NULL || (n & 3u) != 0 || n / 4u > DNX_MAX_PAYLOAD)
        return DNX_E_ARG;
    for (uint32_t i = 0; i < n; i++)
        if (bases[i] > DNX_T) return DNX_E_BASE;
    if (cap < n / 4u) return DNX_E_CAP;
    for (uint32_t i = 0; i < n / 4u; i++)
        out[i] = (uint8_t) ((bases[4u * i] << 6) | (bases[4u * i + 1u] << 4) |
                            (bases[4u * i + 2u] << 2) | bases[4u * i + 3u]);
    return (int32_t) (n / 4u);
}

int32_t dnx_revcomp(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap)
{
    if ((in == NULL && n != 0) || out == NULL || n > DNX_MAX_BASES) return DNX_E_ARG;
    for (uint32_t i = 0; i < n; i++)
        if (in[i] > DNX_T) return DNX_E_BASE;
    if (cap < n) return DNX_E_CAP;
    /* Pairwise from both ends so out == in works. */
    for (uint32_t i = 0, j = n; i < j--; i++) {
        uint8_t a = in[i];
        uint8_t b = in[j];
        out[i] = (uint8_t) (b ^ DNX_COMP);
        out[j] = (uint8_t) (a ^ DNX_COMP);
    }
    return (int32_t) n;
}

int32_t dnx_to_text(const uint8_t *bases, uint32_t n, bool rna, char *out, uint32_t cap)
{
    static const char dna[4] = {'A', 'C', 'G', 'T'};
    if ((bases == NULL && n != 0) || out == NULL || n > DNX_MAX_BASES) return DNX_E_ARG;
    for (uint32_t i = 0; i < n; i++)
        if (bases[i] > DNX_T) return DNX_E_BASE;
    if (cap < n) return DNX_E_CAP;
    for (uint32_t i = 0; i < n; i++) out[i] = (rna && bases[i] == DNX_T) ? 'U' : dna[bases[i]];
    return (int32_t) n;
}

static int dnx_letter(char ch)
{
    switch (ch) {
    case 'A':
    case 'a':
        return DNX_A;
    case 'C':
    case 'c':
        return DNX_C;
    case 'G':
    case 'g':
        return DNX_G;
    case 'T':
    case 't':
    case 'U':
    case 'u':
        return DNX_T;
    default:
        return -1;
    }
}

int32_t dnx_from_text(const char *text, uint32_t n, uint8_t *bases, uint32_t cap)
{
    if ((text == NULL && n != 0) || bases == NULL || n > DNX_MAX_BASES) return DNX_E_ARG;
    for (uint32_t i = 0; i < n; i++)
        if (dnx_letter(text[i]) < 0) return DNX_E_BASE;
    if (cap < n) return DNX_E_CAP;
    for (uint32_t i = 0; i < n; i++) bases[i] = (uint8_t) dnx_letter(text[i]);
    return (int32_t) n;
}

/* ---- D2 metrics ---- */

int32_t dnx_metrics(const uint8_t *bases, uint32_t n, dnx_metrics_t *m)
{
    if ((bases == NULL && n != 0) || m == NULL || n > DNX_MAX_BASES) return DNX_E_ARG;
    for (uint32_t i = 0; i < n; i++)
        if (bases[i] > DNX_T) return DNX_E_BASE;
    dnx_metrics_t r = {0};
    r.bases = n;
    uint32_t run = 0;
    for (uint32_t i = 0; i < n; i++) {
        r.count[bases[i]]++;
        run = (i > 0 && bases[i] == bases[i - 1u]) ? run + 1u : 1u;
        if (run > r.longest_run) r.longest_run = run;
    }
    if (n != 0)
        r.gc_permille = (uint32_t) (((uint64_t) (r.count[DNX_C] + r.count[DNX_G]) * 1000u) / n);
    for (uint32_t i = 0; i + 3u <= n; i += 3u) {
        uint8_t c = dnx_codon_value(bases + i);
        if (c == 14u) /* ATG */
            r.start_codons++;
        if (c == 48u || c == 50u || c == 56u) /* TAA TAG TGA */
            r.stop_codons++;
    }
    r.revcomp_palindrome = (n % 2u) == 0;
    for (uint32_t i = 0; r.revcomp_palindrome && i < n / 2u; i++)
        if (bases[i] != (bases[n - 1u - i] ^ DNX_COMP)) r.revcomp_palindrome = false;
    *m = r;
    return DNX_OK;
}

/* ---- D3 duplex frame ---- */

uint32_t dnx_crc32(const uint8_t *p, uint32_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* A reader over the strand in one direction: forward is the strand itself,
 * reverse is the strand walked from its end with every base complemented. */
typedef struct {
    const uint8_t *s;
    uint32_t n;
    bool rev;
} dnx_reader_t;

static uint8_t rd_base(const dnx_reader_t *r, uint32_t i)
{
    return r->rev ? (uint8_t) (r->s[r->n - 1u - i] ^ DNX_COMP) : r->s[i];
}

static uint8_t rd_byte(const dnx_reader_t *r, uint32_t at)
{
    return (uint8_t) ((rd_base(r, at) << 6) | (rd_base(r, at + 1u) << 4) |
                      (rd_base(r, at + 2u) << 2) | rd_base(r, at + 3u));
}

static uint32_t rd_word(const dnx_reader_t *r, uint32_t at)
{
    uint32_t w = 0;
    for (uint32_t i = 0; i < 4u; i++) w = (w << 8) | rd_byte(r, at + 4u * i);
    return w;
}

#define DNX_BODY_AT (DNX_MAGIC_BASES + DNX_LEN_BASES)

static dnx_read_t dnx_read_one(const dnx_reader_t *r)
{
    dnx_read_t out = {LPRES_STATE_FALSE, DNX_OK, 0, 0};
    if (r->n < DNX_DUPLEX_BASES(0)) {
        out.state = LPRES_STATE_NEITHER;
        out.status = DNX_E_SHORT;
        return out;
    }
    for (uint32_t i = 0; i < DNX_MAGIC_BASES; i++) {
        if (rd_base(r, i) != DNX_MAGIC[i]) {
            out.status = DNX_E_MAGIC;
            return out;
        }
    }
    uint32_t len = rd_word(r, DNX_MAGIC_BASES);
    if (len > DNX_MAX_PAYLOAD || DNX_DUPLEX_BASES(len) != r->n) {
        out.status = DNX_E_LEN;
        return out;
    }
    uint32_t crc_at = DNX_BODY_AT + 4u * len;
    uint32_t pivot_at = crc_at + DNX_CRC_BASES;
    for (uint32_t i = 0; i < DNX_PIVOT_BASES; i++) {
        if (rd_base(r, pivot_at + i) != DNX_PIVOT[i]) {
            out.status = DNX_E_PIVOT;
            return out;
        }
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= rd_byte(r, DNX_BODY_AT + 4u * i);
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    crc = ~crc;
    out.len = len;
    out.crc = rd_word(r, crc_at);
    if (crc != out.crc) {
        out.status = DNX_E_CRC;
        return out;
    }
    out.state = LPRES_STATE_TRUE;
    return out;
}

static void dnx_write_bases(uint8_t *dst, uint32_t v, uint32_t nbases)
{
    for (uint32_t i = 0; i < nbases; i++) dst[i] = (uint8_t) ((v >> (2u * (nbases - 1u - i))) & 3u);
}

int32_t dnx_duplex_encode(const uint8_t *payload, uint32_t len, uint8_t *strand, uint32_t cap)
{
    if ((payload == NULL && len != 0) || strand == NULL || len > DNX_MAX_PAYLOAD) return DNX_E_ARG;
    uint32_t need = DNX_DUPLEX_BASES(len);
    if (cap < need) return DNX_E_CAP;
    uint32_t half = DNX_FRAME_FIXED + 4u * len;
    for (uint32_t i = 0; i < DNX_MAGIC_BASES; i++) strand[i] = DNX_MAGIC[i];
    dnx_write_bases(strand + DNX_MAGIC_BASES, len, DNX_LEN_BASES);
    (void) dnx_bytes_to_bases(payload, len, strand + DNX_BODY_AT, 4u * len);
    dnx_write_bases(strand + DNX_BODY_AT + 4u * len, dnx_crc32(payload, len), DNX_CRC_BASES);
    for (uint32_t i = 0; i < DNX_PIVOT_BASES; i++) strand[half + i] = DNX_PIVOT[i];
    for (uint32_t i = 0; i < half; i++) strand[need - 1u - i] = (uint8_t) (strand[i] ^ DNX_COMP);
    return (int32_t) need;
}

int32_t dnx_duplex_decode(const uint8_t *strand, uint32_t n, uint8_t *fwd_out, uint8_t *rev_out,
                          uint32_t cap, dnx_verdict_t *v)
{
    if ((strand == NULL && n != 0) || v == NULL || n > DNX_MAX_BASES) return DNX_E_ARG;
    for (uint32_t i = 0; i < n; i++)
        if (strand[i] > DNX_T) return DNX_E_BASE;
    dnx_reader_t fr = {strand, n, false};
    dnx_reader_t rr = {strand, n, true};
    v->fwd = dnx_read_one(&fr);
    v->rev = dnx_read_one(&rr);
    v->payloads_equal = false;
    v->state = lpres_kjoin(v->fwd.state, v->rev.state);
    bool fok = v->fwd.state == LPRES_STATE_TRUE;
    bool rok = v->rev.state == LPRES_STATE_TRUE;
    if (fok && rok) {
        v->payloads_equal = v->fwd.len == v->rev.len;
        for (uint32_t i = 0; v->payloads_equal && i < v->fwd.len; i++)
            if (rd_byte(&fr, DNX_BODY_AT + 4u * i) != rd_byte(&rr, DNX_BODY_AT + 4u * i))
                v->payloads_equal = false;
        if (!v->payloads_equal) v->state = LPRES_STATE_BOTH;
    }
    if ((fok && fwd_out != NULL && v->fwd.len > cap) ||
        (rok && rev_out != NULL && v->rev.len > cap))
        return DNX_E_CAP;
    for (uint32_t i = 0; fok && fwd_out != NULL && i < v->fwd.len; i++)
        fwd_out[i] = rd_byte(&fr, DNX_BODY_AT + 4u * i);
    for (uint32_t i = 0; rok && rev_out != NULL && i < v->rev.len; i++)
        rev_out[i] = rd_byte(&rr, DNX_BODY_AT + 4u * i);
    if (v->state == LPRES_STATE_TRUE) return DNX_OK;
    if (fok && rok) return DNX_E_SPLIT;
    return v->fwd.status != DNX_OK ? v->fwd.status : v->rev.status;
}

/* ---- D4 reversible codon program ---- */

/* (k << 1) | inverse for each codon value. Pair k is the k-th smallest codon
 * c with c < rc(c); that codon has inverse 0, rc(c) has inverse 1. The test
 * rebuilds this table from that definition. */
static const uint8_t DNX_CODON_OP[64] = {
    0,  2,  4,  6,  8,  10, 12, 14, 16, 18, 20, 15, 22, 24, 26, 7,  28, 30, 32, 27, 34, 36,
    38, 21, 40, 42, 39, 13, 44, 46, 33, 5,  48, 50, 47, 25, 52, 54, 43, 19, 56, 55, 37, 11,
    58, 51, 31, 3,  60, 59, 45, 23, 62, 57, 41, 17, 63, 53, 35, 9,  61, 49, 29, 1};

/* f_k for k 20..23 multiplies by an odd constant; the inverse multiplies by
 * its inverse mod 2^32. */
#define DNX_MUL     0x9E3779B1u
#define DNX_MUL_INV 0x0E8B2F51u
#define DNX_ROT     7u

static const uint8_t DNX_ADD_PAIR[12][2] = {{0, 1}, {0, 2}, {0, 3}, {1, 0}, {1, 2}, {1, 3},
                                            {2, 0}, {2, 1}, {2, 3}, {3, 0}, {3, 1}, {3, 2}};
static const uint8_t DNX_SWAP_PAIR[6][2] = {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}};

uint8_t dnx_codon_value(const uint8_t *three)
{
    return (uint8_t) (((three[0] & 3u) << 4) | ((three[1] & 3u) << 2) | (three[2] & 3u));
}

uint8_t dnx_codon_revcomp(uint8_t c)
{
    uint8_t b0 = (uint8_t) ((c >> 4) & 3u);
    uint8_t b1 = (uint8_t) ((c >> 2) & 3u);
    uint8_t b2 = (uint8_t) (c & 3u);
    return (uint8_t) (((b2 ^ DNX_COMP) << 4) | ((b1 ^ DNX_COMP) << 2) | (b0 ^ DNX_COMP));
}

uint8_t dnx_codon_op(uint8_t c)
{
    return DNX_CODON_OP[c & 63u];
}

static void dnx_exec(uint8_t op, dnx_state_t *s)
{
    uint32_t k = op >> 1;
    bool inv = (op & 1u) != 0;
    uint32_t *r = s->r;
    if (k < 12u) {
        uint32_t i = DNX_ADD_PAIR[k][0];
        uint32_t j = DNX_ADD_PAIR[k][1];
        r[i] = inv ? r[i] - r[j] : r[i] + r[j];
    } else if (k < 16u) {
        r[k - 12u] = inv ? r[k - 12u] - 1u : r[k - 12u] + 1u;
    } else if (k < 20u) {
        uint32_t x = r[k - 16u];
        r[k - 16u] =
            inv ? (x >> DNX_ROT) | (x << (32u - DNX_ROT)) : (x << DNX_ROT) | (x >> (32u - DNX_ROT));
    } else if (k < 24u) {
        r[k - 20u] *= inv ? DNX_MUL_INV : DNX_MUL;
    } else if (k < 30u) {
        uint32_t i = DNX_SWAP_PAIR[k - 24u][0];
        uint32_t j = DNX_SWAP_PAIR[k - 24u][1];
        uint32_t t = r[i];
        r[i] = r[j];
        r[j] = t;
    } else if (k == 30u) {
        r[1] ^= r[0];
    } else {
        r[3] ^= r[2];
    }
}

static int32_t dnx_prog_check(const uint8_t *bases, uint32_t n, const dnx_state_t *s)
{
    if ((bases == NULL && n != 0) || s == NULL || n % 3u != 0 || n > DNX_MAX_BASES)
        return DNX_E_ARG;
    for (uint32_t i = 0; i < n; i++)
        if (bases[i] > DNX_T) return DNX_E_BASE;
    return DNX_OK;
}

int32_t dnx_prog_run(const uint8_t *bases, uint32_t n, dnx_state_t *s)
{
    int32_t rc = dnx_prog_check(bases, n, s);
    if (rc != DNX_OK) return rc;
    for (uint32_t i = 0; i < n; i += 3u) dnx_exec(DNX_CODON_OP[dnx_codon_value(bases + i)], s);
    return (int32_t) (n / 3u);
}

int32_t dnx_prog_run_reverse(const uint8_t *bases, uint32_t n, dnx_state_t *s)
{
    int32_t rc = dnx_prog_check(bases, n, s);
    if (rc != DNX_OK) return rc;
    dnx_reader_t rr = {bases, n, true};
    for (uint32_t i = 0; i < n; i += 3u) {
        uint8_t three[3] = {rd_base(&rr, i), rd_base(&rr, i + 1u), rd_base(&rr, i + 2u)};
        dnx_exec(DNX_CODON_OP[dnx_codon_value(three)], s);
    }
    return (int32_t) (n / 3u);
}

static bool dnx_state_eq(const dnx_state_t *a, const dnx_state_t *b)
{
    for (uint32_t i = 0; i < DNX_REGS; i++)
        if (a->r[i] != b->r[i]) return false;
    return true;
}

lpres_state_t dnx_prog_bidi(const uint8_t *bases, uint32_t n, const dnx_state_t *x,
                            const dnx_state_t *y, lpres_state_t *fwd, lpres_state_t *back)
{
    lpres_state_t f = LPRES_STATE_NEITHER;
    lpres_state_t b = LPRES_STATE_NEITHER;
    if (x != NULL && y != NULL) {
        dnx_state_t t = *x;
        if (dnx_prog_run(bases, n, &t) >= 0)
            f = dnx_state_eq(&t, y) ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
        t = *y;
        if (dnx_prog_run_reverse(bases, n, &t) >= 0)
            b = dnx_state_eq(&t, x) ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    }
    if (fwd != NULL) *fwd = f;
    if (back != NULL) *back = b;
    return lpres_kjoin(f, b);
}

/* ---- D5 genetic symbols ---- */

char dnx_codon_amino(uint8_t c)
{
    /* Standard code, indexed 16*b0 + 4*b1 + b2 with A C G T = 0 1 2 3. */
    static const char amino[65] =
        "KNKNTTTTRSRSIIMIQHQHPPPPRRRRLLLLEDEDAAAAGGGGVVVV*Y*YSSSS*CWCLFLF";
    return amino[c & 63u];
}
