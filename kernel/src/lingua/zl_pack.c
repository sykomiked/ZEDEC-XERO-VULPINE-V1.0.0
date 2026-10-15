/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_pack.c — precompiled Lingua programs as text ("zlir").
 *
 * An app ships its legacy-language logic already compiled: zl_pack writes a
 * program's IR as plain text that fits in a manifest, and zl_unpack reads it
 * back without the front-end. The text is line based so it diffs and reviews
 * like source:
 *
 *   zlir 1
 *   lang 1
 *   bits 32
 *   slots 3
 *   code 4
 *   PUSH 7 0 2          one line per instruction: op, a, b, source line
 *   ...
 *   str 5 48454c4c4f    one line per string: length and bytes in hex
 *   digest 0123456789abcdef
 *   end
 *
 * zl_unpack is strict: every field in this order, single spaces, decimal
 * numbers, lowercase hex, '\n' line ends. The digest must equal
 * zl_prog_digest of what was read, and the program must pass zl_verify, so
 * a damaged or hand-edited file is refused rather than run. Freestanding. */
#include "zl_int.h"

static const char *const OPN[ZL_OP__COUNT] = {
    "HALT",   "PUSH",   "LOAD",  "STORE", "LOADX",   "STOREX", "ADD",   "SUB",   "MUL",    "DIVT",
    "MODT",   "DIVF",   "MODF",  "DIVX",  "POW",     "NEG",    "ABS",   "SGN",   "MIN",    "MAX",
    "EQ",     "NE",     "LT",    "LE",    "GT",      "GE",     "AND",   "OR",    "XOR",    "INVERT",
    "NOT",    "DUP",    "DROP",  "SWAP",  "OVER",    "ROT",    "JMP",   "JZ",    "JNZ",    "CALL",
    "RET",    "OUTI",   "OUTS",  "OUTC",  "NL",      "TAB",    "IN",    "LOADA", "STOREA", "TRAP",
    "ENTER",  "LEAVE",  "LINK",  "LOADF", "STOREF",  "ADDRF",  "BOUND", "CONS",  "CAR",    "CDR",
    "SETCAR", "SETCDR", "ISA",   "OUTV",  "VNEW",    "VLEN",   "VREF",  "VSET",  "SCAT",   "SSUB",
    "SCMP",   "SCONV",  "CALLI", "CATCH", "UNCATCH", "THROW"};

const char *zl_op_name(int op)
{
    return op >= 0 && op < ZL_OP__COUNT ? OPN[op] : "?";
}

static const char HEX[] = "0123456789abcdef";

static void put_line(zl_out_t *o, const char *key, int64_t v)
{
    zl_puts(o, key);
    zl_putc(o, ' ');
    zl_puti(o, v);
    zl_putc(o, '\n');
}

int zl_pack(const zl_prog_t *p, char *out, size_t cap, size_t *out_len)
{
    if (!p || !out_len || (cap && !out)) return ZL_E_ARG;
    *out_len = 0;
    if (zl_verify(p, 0) != ZL_OK) return ZL_E_VERIFY;
    zl_out_t o;
    zl_bzero(&o, sizeof o);
    o.p = out;
    o.cap = cap;
    zl_puts(&o, "zlir 1\n");
    put_line(&o, "lang", p->lang);
    put_line(&o, "bits", p->word_bits);
    put_line(&o, "slots", p->nslots);
    put_line(&o, "code", p->ncode);
    for (uint32_t i = 0; i < p->ncode; i++) {
        const zl_ins_t *in = &p->code[i];
        zl_puts(&o, OPN[in->op]);
        zl_putc(&o, ' ');
        zl_puti(&o, in->a);
        zl_putc(&o, ' ');
        zl_puti(&o, in->b);
        zl_putc(&o, ' ');
        zl_puti(&o, in->line);
        zl_putc(&o, '\n');
    }
    for (uint32_t i = 0; i < p->nstr; i++) {
        zl_puts(&o, "str ");
        zl_puti(&o, p->str_len[i]);
        zl_putc(&o, ' ');
        for (uint32_t k = 0; k < p->str_len[i]; k++) {
            unsigned char ch = (unsigned char) p->str[p->str_off[i] + k];
            zl_putc(&o, HEX[ch >> 4]);
            zl_putc(&o, HEX[ch & 15]);
        }
        zl_putc(&o, '\n');
    }
    uint64_t dg = zl_prog_digest(p);
    zl_puts(&o, "digest ");
    for (int s = 60; s >= 0; s -= 4) zl_putc(&o, HEX[(dg >> s) & 15]);
    zl_puts(&o, "\nend\n");
    if (o.over) {
        if (cap) out[0] = 0;
        return ZL_E_LIMIT;
    }
    *out_len = o.len;
    return ZL_OK;
}

/* ---------------------------------------------------------------- reading */

typedef struct {
    const char *s;
    size_t n, i;
    uint32_t line;
    int bad;
} rd_t;

static int lit(rd_t *r, const char *w)
{
    size_t k = 0;
    while (w[k]) {
        if (r->i + k >= r->n || r->s[r->i + k] != w[k]) return 0;
        k++;
    }
    r->i += k;
    return 1;
}

static void want(rd_t *r, const char *w)
{
    if (!r->bad && !lit(r, w)) r->bad = 1;
}

static void eol(rd_t *r)
{
    want(r, "\n");
    if (!r->bad) r->line++;
}

/* A decimal integer in [lo, hi]: optional '-', no leading zeros, no '+'. */
static int64_t num(rd_t *r, int64_t lo, int64_t hi)
{
    if (r->bad) return 0;
    int neg = 0;
    if (r->i < r->n && r->s[r->i] == '-') {
        neg = 1;
        r->i++;
    }
    size_t st = r->i;
    uint64_t v = 0;
    while (r->i < r->n && zl_isdigit(r->s[r->i])) {
        uint64_t d = (uint64_t) (r->s[r->i] - '0');
        if (v > (0x8000000000000000ull - d) / 10u) {
            r->bad = 1;
            return 0;
        }
        v = v * 10u + d;
        r->i++;
    }
    size_t len = r->i - st;
    if (!len || (len > 1 && r->s[st] == '0') || (neg && v == 0) ||
        (!neg && v > 0x7fffffffffffffffull)) {
        r->bad = 1;
        return 0;
    }
    int64_t x =
        neg ? (v == 0x8000000000000000ull ? (int64_t) (-0x7fffffffffffffffll - 1) : -(int64_t) v)
            : (int64_t) v;
    if (x < lo || x > hi) r->bad = 1;
    return x;
}

static int hexv(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

int zl_unpack(const char *src, size_t len, zl_prog_t *p, zl_diag_t *d)
{
    zl_diag_t dd;
    if (!d) d = &dd;
    d->status = ZL_OK;
    d->line = d->col = 0;
    d->msg = "ok";
    if (!p || (len && !src)) {
        d->status = ZL_E_ARG;
        d->msg = "no program or text";
        return ZL_E_ARG;
    }
    zl_bzero(p, sizeof *p);
    rd_t r = {src, len, 0, 1, 0};
    const char *why = "malformed zlir text";
    want(&r, "zlir 1");
    eol(&r);
    want(&r, "lang ");
    p->lang = (int) num(&r, 1, ZL_LANG__COUNT - 1);
    eol(&r);
    want(&r, "bits ");
    p->word_bits = (uint32_t) num(&r, 32, 64);
    eol(&r);
    want(&r, "slots ");
    p->nslots = (uint32_t) num(&r, 0, ZL_MAX_SLOTS);
    eol(&r);
    want(&r, "code ");
    p->ncode = (uint32_t) num(&r, 1, ZL_MAX_CODE);
    eol(&r);
    for (uint32_t i = 0; i < p->ncode && !r.bad; i++) {
        zl_ins_t *in = &p->code[i];
        int op = -1;
        for (int k = 0; k < ZL_OP__COUNT && op < 0; k++) {
            size_t save = r.i;
            if (lit(&r, OPN[k]) && r.i < r.n && r.s[r.i] == ' ')
                op = k;
            else
                r.i = save;
        }
        if (op < 0) {
            why = "unknown instruction";
            r.bad = 1;
            break;
        }
        in->op = (uint8_t) op;
        want(&r, " ");
        in->a = num(&r, (-0x7fffffffffffffffll - 1), 0x7fffffffffffffffll);
        want(&r, " ");
        in->b = (int32_t) num(&r, -0x7fffffffll - 1, 0x7fffffffll);
        want(&r, " ");
        in->line = (uint32_t) num(&r, 0, 0xffffffffll);
        eol(&r);
    }
    while (!r.bad && lit(&r, "str ")) {
        if (p->nstr >= ZL_MAX_STR) {
            why = "too many strings";
            r.bad = 1;
            break;
        }
        uint32_t sl = (uint32_t) num(&r, 0, ZL_MAX_STRBYTES);
        want(&r, " ");
        if (r.bad || sl > ZL_MAX_STRBYTES - p->strbytes) {
            r.bad = 1;
            break;
        }
        p->str_off[p->nstr] = p->strbytes;
        p->str_len[p->nstr] = sl;
        for (uint32_t k = 0; k < sl && !r.bad; k++) {
            int hi = r.i < r.n ? hexv(r.s[r.i]) : -1;
            int lo = r.i + 1 < r.n ? hexv(r.s[r.i + 1]) : -1;
            if (hi < 0 || lo < 0) {
                r.bad = 1;
                break;
            }
            p->str[p->strbytes + k] = (char) (hi * 16 + lo);
            r.i += 2;
        }
        p->strbytes += sl;
        p->nstr++;
        eol(&r);
    }
    uint64_t dg = 0;
    want(&r, "digest ");
    for (int k = 0; k < 16 && !r.bad; k++) {
        int h = r.i < r.n ? hexv(r.s[r.i]) : -1;
        if (h < 0) r.bad = 1;
        dg = (dg << 4) | (uint64_t) (h < 0 ? 0 : h);
        r.i++;
    }
    eol(&r);
    want(&r, "end");
    eol(&r);
    if (!r.bad && r.i != r.n) r.bad = 1;
    int s = ZL_OK;
    if (r.bad) {
        s = ZL_E_SYNTAX;
        d->msg = why;
        d->line = r.line;
    } else if (zl_verify(p, d) != ZL_OK) {
        s = ZL_E_VERIFY;
    } else if (zl_prog_digest(p) != dg) {
        s = ZL_E_VERIFY;
        d->msg = "digest does not match the program";
    }
    if (s != ZL_OK) {
        zl_bzero(p, sizeof *p);
        d->status = s;
    }
    return s;
}
