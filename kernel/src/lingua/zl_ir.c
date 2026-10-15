/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_ir.c — the Lingua IR: emitter, verifier, comparison, the checked
 * runtime, and the zl_compile / zl_twin entry points that dispatch to the
 * front-ends. Freestanding: no libc, no allocation, no floating point. */
#include "zl_int.h"

ZL_STATIC_ASSERT(sizeof(zl_ins_t) == 24, ins_size);

/* ---------------------------------------------------------------- bytes */

void zl_bzero(void *p, size_t n)
{
    volatile unsigned char *b = (volatile unsigned char *) p;
    while (n && ((uintptr_t) b & 7)) {
        *b++ = 0;
        n--;
    }
    volatile uint64_t *q = (volatile uint64_t *) (uintptr_t) b; /* 8-byte aligned here */
    for (; n >= 8; n -= 8) *q++ = 0;
    b = (volatile unsigned char *) q;
    while (n--) *b++ = 0;
}

void zl_bcopy(void *dst, const void *src, size_t n)
{
    volatile unsigned char *d = (volatile unsigned char *) dst;
    const unsigned char *s = (const unsigned char *) src;
    while (n--) *d++ = *s++;
}

int zl_wordeq(const char *a, size_t n, const char *w)
{
    size_t i = 0;
    for (; i < n; i++)
        if (w[i] == 0 || zl_upper((unsigned char) a[i]) != w[i]) return 0;
    return w[i] == 0;
}

size_t zl_itoa(int64_t v, char *buf)
{
    char t[24];
    size_t n = 0, k = 0;
    uint64_t u = v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    do {
        t[n++] = (char) ('0' + (int) (u % 10));
        u /= 10;
    } while (u);
    if (v < 0) buf[k++] = '-';
    while (n) buf[k++] = t[--n];
    return k;
}

/* ---------------------------------------------------------------- text */

void zl_put(zl_out_t *o, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        if (o->cmp) {
            if (!o->diff && (o->len >= o->cmp_len || o->cmp[o->len] != s[i])) {
                o->diff = 1;
                o->diff_at = o->len;
            }
        } else if (o->len + 1 < o->cap) {
            o->p[o->len] = s[i];
        } else {
            o->over = 1;
        }
        o->len++;
    }
    if (!o->cmp && o->p && o->cap) o->p[o->len < o->cap ? o->len : o->cap - 1] = 0;
}

void zl_puts(zl_out_t *o, const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    zl_put(o, s, n);
}

void zl_putc(zl_out_t *o, char ch)
{
    zl_put(o, &ch, 1);
}

void zl_puti(zl_out_t *o, int64_t v)
{
    char b[24];
    zl_put(o, b, zl_itoa(v, b));
}

/* ---------------------------------------------------------------- emitter */

void zl_cc_init(zl_cc_t *c, zl_prog_t *p, zl_diag_t *d, int lang, uint32_t bits)
{
    c->p = p;
    c->d = d;
    c->line = 0;
    c->col = 0;
    c->failed = 0;
    p->lang = lang;
    p->word_bits = bits;
}

int zl_fail(zl_cc_t *c, int status, const char *msg)
{
    if (!c->failed) {
        c->failed = 1;
        c->d->status = status;
        c->d->line = c->line;
        c->d->col = c->col;
        c->d->msg = msg;
    }
    return status;
}

int32_t zl_emit(zl_cc_t *c, int op, int64_t a, int32_t b)
{
    zl_prog_t *p = c->p;
    if (c->failed) return -1;
    if (p->ncode >= ZL_MAX_CODE) {
        zl_fail(c, ZL_E_LIMIT, "program too long for the IR code table");
        return -1;
    }
    zl_ins_t *i = &p->code[p->ncode];
    i->op = (uint8_t) op;
    i->a = a;
    i->b = b;
    i->line = c->line;
    return (int32_t) p->ncode++;
}

void zl_patch(zl_cc_t *c, int32_t at, int64_t target)
{
    if (at >= 0 && (uint32_t) at < c->p->ncode) c->p->code[at].a = target;
}

int32_t zl_addstr(zl_cc_t *c, const char *s, size_t n)
{
    zl_prog_t *p = c->p;
    if (c->failed) return -1;
    for (uint32_t i = 0; i < p->nstr; i++) { /* reuse an identical literal */
        if (p->str_len[i] != n) continue;
        size_t k = 0;
        while (k < n && p->str[p->str_off[i] + k] == s[k]) k++;
        if (k == n) return (int32_t) i;
    }
    if (p->nstr >= ZL_MAX_STR || n > ZL_MAX_STRBYTES - p->strbytes) {
        zl_fail(c, ZL_E_LIMIT, "too many string literals");
        return -1;
    }
    p->str_off[p->nstr] = p->strbytes;
    p->str_len[p->nstr] = (uint32_t) n;
    zl_bcopy(p->str + p->strbytes, s, n);
    p->strbytes += (uint32_t) n;
    return (int32_t) p->nstr++;
}

int32_t zl_slots(zl_cc_t *c, uint32_t n)
{
    zl_prog_t *p = c->p;
    if (c->failed) return -1;
    if (n == 0 || n > ZL_MAX_SLOTS - p->nslots) {
        zl_fail(c, ZL_E_LIMIT, "too much data for the slot table");
        return -1;
    }
    p->nslots += n;
    return (int32_t) (p->nslots - n);
}

int zl_fits(const zl_cc_t *c, int64_t v)
{
    uint32_t b = c->p->word_bits;
    if (b == 62) return v > -ZL_V_REF && v < ZL_V_REF;
    return b == 64 || (v >= INT32_MIN && v <= INT32_MAX);
}

/* ---------------------------------------------------------------- names */

const char *zl_lang_name(int lang)
{
    switch (lang & ~ZL_TWIN) {
    case ZL_LANG_FORTRAN:
        return "FORTRAN";
    case ZL_LANG_BASIC:
        return "BASIC";
    case ZL_LANG_FORTH:
        return "Forth";
    case ZL_LANG_PASCAL:
        return "Pascal";
    case ZL_LANG_ALGOL60:
        return "ALGOL 60";
    case ZL_LANG_LISP:
        return "LISP";
    case ZL_LANG_SCHEME:
        return "Scheme";
    case ZL_LANG_LOGO:
        return "Logo";
    default:
        return "unknown";
    }
}

const char *zl_twin_name(int lang)
{
    switch (lang & ~ZL_TWIN) {
    case ZL_LANG_FORTRAN:
        return "Reckonwyrm";
    case ZL_LANG_BASIC:
        return "Hatchling";
    case ZL_LANG_FORTH:
        return "Cairndrake";
    case ZL_LANG_PASCAL:
        return "Tutordrake";
    case ZL_LANG_ALGOL60:
        return "Rootwyrm";
    case ZL_LANG_LISP:
        return "Coilwyrm";
    case ZL_LANG_SCHEME:
        return "Spiraldrake";
    case ZL_LANG_LOGO:
        return "Kiteling";
    default:
        return "unknown";
    }
}

const char *zl_status_name(int s)
{
    switch (s) {
    case ZL_OK:
        return "ok";
    case ZL_E_ARG:
        return "bad argument";
    case ZL_E_SYNTAX:
        return "syntax error";
    case ZL_E_UNSUPPORTED:
        return "outside the supported subset";
    case ZL_E_NONCANON:
        return "not in canonical twin form";
    case ZL_E_LIMIT:
        return "capacity exceeded";
    case ZL_E_UNDEFINED:
        return "undefined label or word";
    case ZL_E_VERIFY:
        return "IR failed verification";
    case ZL_E_OVERFLOW:
        return "integer overflow";
    case ZL_E_DIVZERO:
        return "division by zero";
    case ZL_E_INEXACT:
        return "inexact result";
    case ZL_E_BOUNDS:
        return "index out of bounds";
    case ZL_E_STACK:
        return "stack underflow or overflow";
    case ZL_E_RSTACK:
        return "call depth exceeded or RETURN without GOSUB";
    case ZL_E_FUEL:
        return "instruction budget spent";
    case ZL_E_OUTPUT:
        return "output buffer full";
    case ZL_E_INPUT:
        return "input exhausted";
    case ZL_E_DOMAIN:
        return "argument outside the domain";
    case ZL_E_HEAP:
        return "list cells exhausted";
    default:
        return "unknown status";
    }
}

/* ---------------------------------------------------------------- verify */

static int vfail(zl_diag_t *d, uint32_t line, const char *msg)
{
    if (d) {
        d->status = ZL_E_VERIFY;
        d->line = line;
        d->col = 0;
        d->msg = msg;
    }
    return ZL_E_VERIFY;
}

int zl_verify(const zl_prog_t *p, zl_diag_t *d)
{
    if (!p) return vfail(d, 0, "no program");
    if (p->lang <= 0 || p->lang >= ZL_LANG__COUNT) return vfail(d, 0, "unknown language");
    if (p->word_bits != 32 && p->word_bits != 62 && p->word_bits != 64)
        return vfail(d, 0, "word width must be 32, 62 or 64");
    if (p->ncode == 0 || p->ncode > ZL_MAX_CODE) return vfail(d, 0, "empty or oversized code");
    if (p->nslots > ZL_MAX_SLOTS || p->nstr > ZL_MAX_STR || p->strbytes > ZL_MAX_STRBYTES)
        return vfail(d, 0, "table counts out of range");
    for (uint32_t i = 0; i < p->nstr; i++)
        if (p->str_off[i] > p->strbytes || p->str_len[i] > p->strbytes - p->str_off[i])
            return vfail(d, 0, "string outside the string table");
    for (uint32_t pc = 0; pc < p->ncode; pc++) {
        const zl_ins_t *i = &p->code[pc];
        switch (i->op) {
        case ZL_LOAD:
        case ZL_STORE:
            if (i->a < 0 || i->a >= (int64_t) p->nslots)
                return vfail(d, i->line, "slot out of range");
            break;
        case ZL_LOADX:
        case ZL_STOREX:
            if (i->b <= 0 || i->a < 0 || i->a + i->b > (int64_t) p->nslots)
                return vfail(d, i->line, "array outside the slot table");
            break;
        case ZL_JMP:
        case ZL_JZ:
        case ZL_JNZ:
        case ZL_CALL:
            if (i->a < 0 || i->a >= (int64_t) p->ncode)
                return vfail(d, i->line, "jump target out of range");
            break;
        case ZL_OUTS:
            if (i->a < 0 || i->a >= (int64_t) p->nstr)
                return vfail(d, i->line, "string index out of range");
            break;
        case ZL_OUTI:
            if (i->a < 0 || i->a > 3 || i->b < 0 || i->b > 64)
                return vfail(d, i->line, "bad number format");
            break;
        case ZL_POW:
            if (i->a != 0 && i->a != 1) return vfail(d, i->line, "bad power mode");
            break;
        case ZL_TAB:
            if (i->a < 1 || i->a > 255) return vfail(d, i->line, "bad tab zone");
            break;
        case ZL_TRAP:
            if (i->a > ZL_E_ARG || i->a < -2699) return vfail(d, i->line, "bad trap status");
            break;
        case ZL_PUSH:
            if (p->word_bits == 32 && (i->a < INT32_MIN || i->a > INT32_MAX))
                return vfail(d, i->line, "constant wider than the word");
            if (p->word_bits == 62 && (i->a <= -ZL_V_REF || i->a >= ZL_V_REF) && i->a != ZL_V_NIL &&
                i->a != ZL_V_TRUE && i->a != ZL_V_FALSE &&
                (i->a < ZL_V_SYM || i->a >= ZL_V_SYM + (int64_t) p->nstr))
                return vfail(d, i->line, "constant is neither a number nor a known value");
            break;
        case ZL_ENTER:
            if (i->a < 0 || i->a > ZL_MAX_FRAMES || i->b < 0 || i->b > i->a || i->b >= ZL_MAX_STACK)
                return vfail(d, i->line, "bad frame size");
            break;
        case ZL_LINK:
            if (i->a < 0 || i->a >= ZL_MAX_CALLS) return vfail(d, i->line, "bad link depth");
            break;
        case ZL_LOADF:
        case ZL_STOREF:
        case ZL_ADDRF:
            if (i->a < 0 || i->a >= ZL_MAX_FRAMES || i->b < 0 || i->b >= ZL_MAX_CALLS)
                return vfail(d, i->line, "bad frame operand");
            break;
        case ZL_BOUND:
            if (i->a <= 0) return vfail(d, i->line, "bad bound");
            break;
        case ZL_CONS:
        case ZL_CAR:
        case ZL_CDR:
        case ZL_SETCAR:
        case ZL_SETCDR:
        case ZL_ISA:
        case ZL_OUTV:
            if (p->word_bits != 62)
                return vfail(d, i->line, "list operation outside a list program");
            if ((i->op == ZL_CAR || i->op == ZL_CDR) && i->a != 0 && i->a != 1)
                return vfail(d, i->line, "bad CAR/CDR mode");
            if (i->op == ZL_ISA && (i->a < 0 || i->a >= ZL_ISA__COUNT))
                return vfail(d, i->line, "bad type test");
            if (i->op == ZL_OUTV && (i->a < 0 || i->a >= ZL_DIALECT__COUNT))
                return vfail(d, i->line, "bad print dialect");
            break;
        default:
            if (i->op >= ZL_OP__COUNT) return vfail(d, i->line, "unknown opcode");
            break;
        }
    }
    {
        uint8_t last = p->code[p->ncode - 1].op;
        if (last != ZL_HALT && last != ZL_JMP && last != ZL_RET && last != ZL_TRAP)
            return vfail(d, p->code[p->ncode - 1].line, "code can run past its end");
    }
    if (d) {
        d->status = ZL_OK;
        d->line = 0;
        d->col = 0;
        d->msg = "ok";
    }
    return ZL_OK;
}

/* ---------------------------------------------------------------- compare */

typedef struct {
    uint64_t h;
} fnv_t;
static void fnv(fnv_t *f, const void *b, size_t n)
{
    const unsigned char *s = (const unsigned char *) b;
    while (n--) {
        f->h ^= *s++;
        f->h *= 0x100000001b3ull;
    }
}
static void fnv64(fnv_t *f, uint64_t v)
{
    unsigned char b[8];
    for (int i = 0; i < 8; i++) b[i] = (unsigned char) (v >> (8 * i));
    fnv(f, b, 8);
}

uint64_t zl_prog_digest(const zl_prog_t *p)
{
    fnv_t f = {0xcbf29ce484222325ull};
    fnv64(&f, (uint64_t) p->lang);
    fnv64(&f, p->word_bits);
    fnv64(&f, p->ncode);
    fnv64(&f, p->nslots);
    fnv64(&f, p->nstr);
    for (uint32_t i = 0; i < p->ncode && i < ZL_MAX_CODE; i++) {
        fnv64(&f, p->code[i].op);
        fnv64(&f, (uint64_t) p->code[i].a);
        fnv64(&f, (uint64_t) (int64_t) p->code[i].b);
    }
    for (uint32_t i = 0; i < p->nstr && i < ZL_MAX_STR; i++) {
        fnv64(&f, p->str_len[i]);
        if (p->str_off[i] <= ZL_MAX_STRBYTES && p->str_len[i] <= ZL_MAX_STRBYTES - p->str_off[i])
            fnv(&f, p->str + p->str_off[i], p->str_len[i]);
    }
    return f.h;
}

int zl_prog_equal(const zl_prog_t *a, const zl_prog_t *b)
{
    if (a->lang != b->lang || a->word_bits != b->word_bits || a->ncode != b->ncode ||
        a->nslots != b->nslots || a->nstr != b->nstr)
        return 0;
    if (a->ncode > ZL_MAX_CODE || a->nstr > ZL_MAX_STR) return 0;
    for (uint32_t i = 0; i < a->ncode; i++)
        if (a->code[i].op != b->code[i].op || a->code[i].a != b->code[i].a ||
            a->code[i].b != b->code[i].b)
            return 0;
    for (uint32_t i = 0; i < a->nstr; i++) {
        if (a->str_len[i] != b->str_len[i]) return 0;
        if (a->str_off[i] > ZL_MAX_STRBYTES || a->str_len[i] > ZL_MAX_STRBYTES - a->str_off[i] ||
            b->str_off[i] > ZL_MAX_STRBYTES || b->str_len[i] > ZL_MAX_STRBYTES - b->str_off[i])
            return 0;
        for (uint32_t k = 0; k < a->str_len[i]; k++)
            if (a->str[a->str_off[i] + k] != b->str[b->str_off[i] + k]) return 0;
    }
    return 1;
}

/* ---------------------------------------------------------------- run */

typedef struct {
    zl_run_t *r;
    size_t col;
} vm_out_t;

static int vm_put(vm_out_t *o, const char *s, size_t n)
{
    zl_run_t *r = o->r;
    for (size_t i = 0; i < n; i++) {
        if (r->out_len + 1 > r->out_cap) return ZL_E_OUTPUT;
        r->out[r->out_len++] = s[i];
        o->col = s[i] == '\n' ? 0 : o->col + 1;
    }
    if (r->out_len < r->out_cap) r->out[r->out_len] = 0;
    return ZL_OK;
}

static int fits(uint32_t bits, int64_t v)
{
    if (bits == 62) return v > -ZL_V_REF && v < ZL_V_REF;
    return bits == 64 || (v >= INT32_MIN && v <= INT32_MAX);
}

/* A pair's cell index, or 0 when v is not a pair. */
static uint32_t pair_of(int64_t v, uint32_t hp)
{
    if (v <= ZL_V_NIL || v >= ZL_V_SYM) return 0;
    int64_t k = v - ZL_V_REF;
    return k < (int64_t) hp ? (uint32_t) k : 0;
}

static int put_cstr(vm_out_t *o, const char *t)
{
    size_t n = 0;
    while (t[n]) n++;
    return vm_put(o, t, n);
}

/* A value that is not a pair. bare: the empty list at the top of a Logo
 * PRINT, which prints nothing. */
static int print_atom(vm_out_t *o, const zl_prog_t *p, int64_t v, int dialect, int bare)
{
    char num[24];
    if (v > -ZL_V_REF && v < ZL_V_REF) return vm_put(o, num, zl_itoa(v, num));
    if (v == ZL_V_NIL)
        return put_cstr(o, dialect == ZL_DIALECT_LISP     ? "NIL"
                           : dialect == ZL_DIALECT_SCHEME ? "()"
                           : bare                         ? ""
                                                          : "[]");
    if (v == ZL_V_TRUE || v == ZL_V_FALSE) {
        int t = v == ZL_V_TRUE;
        return put_cstr(o, dialect == ZL_DIALECT_LISP     ? (t ? "T" : "NIL")
                           : dialect == ZL_DIALECT_SCHEME ? (t ? "#t" : "#f")
                                                          : (t ? "true" : "false"));
    }
    if (v >= ZL_V_SYM && v < ZL_V_SYM + (int64_t) p->nstr) {
        uint32_t i = (uint32_t) (v - ZL_V_SYM);
        return vm_put(o, p->str + p->str_off[i], p->str_len[i]);
    }
    return ZL_E_DOMAIN; /* a dangling reference cannot be printed */
}

/* ZL_OUTV: print a value in a dialect's notation. Lists print without
 * recursion: todo holds the rest of each list still open. A Logo PRINT
 * leaves out the outermost brackets; a cycle runs into the output cap. */
static int print_value(vm_out_t *o, const zl_prog_t *p, const int64_t *heap, uint32_t hp, int64_t v,
                       int dialect)
{
    int64_t todo[ZL_MAX_STACK];
    uint32_t depth = 0;
    int bare = dialect == ZL_DIALECT_LOGO, logo = dialect >= ZL_DIALECT_LOGO, s;
    const char *open = logo ? "[" : "(", *close = logo ? "]" : ")";
    for (;;) {
        uint32_t k = pair_of(v, hp);
        if (k) {
            if (depth >= ZL_MAX_STACK) return ZL_E_DOMAIN;
            if (!(bare && depth == 0) && (s = vm_put(o, open, 1)) != ZL_OK) return s;
            todo[depth++] = heap[2 * k + 1];
            v = heap[2 * k];
            continue;
        }
        if ((s = print_atom(o, p, v, dialect, bare && depth == 0)) != ZL_OK) return s;
        for (;;) {
            if (!depth) return ZL_OK;
            int64_t rest = todo[depth - 1];
            if ((k = pair_of(rest, hp)) != 0) {
                if ((s = vm_put(o, " ", 1)) != ZL_OK) return s;
                todo[depth - 1] = heap[2 * k + 1];
                v = heap[2 * k];
                break;
            }
            if (rest != ZL_V_NIL) { /* a dotted pair */
                if (logo) return ZL_E_DOMAIN;
                if ((s = vm_put(o, " . ", 3)) != ZL_OK) return s;
                todo[depth - 1] = ZL_V_NIL;
                v = rest;
                break;
            }
            depth--;
            if (!(bare && depth == 0) && (s = vm_put(o, close, 1)) != ZL_OK) return s;
        }
    }
}

/* Integer power by squaring. mode 0: a negative exponent truncates toward
 * zero as FORTRAN integer ** does; mode 1: a non-integer result traps. */
static int ipow(int64_t base, int64_t e, int mode, uint32_t bits, int64_t *out)
{
    if (e < 0) {
        if (base == 0) return ZL_E_DIVZERO;
        if (base == 1) {
            *out = 1;
            return ZL_OK;
        }
        if (base == -1) {
            *out = (e & 1) ? -1 : 1;
            return ZL_OK;
        }
        if (mode == 1) return ZL_E_INEXACT;
        *out = 0;
        return ZL_OK;
    }
    int64_t r = 1, b = base;
    uint64_t k = (uint64_t) e;
    int b_ok = 1; /* b no longer needed once its square would only feed k == 0 */
    while (k) {
        if (k & 1) {
            if (!b_ok || __builtin_mul_overflow(r, b, &r) || !fits(bits, r)) return ZL_E_OVERFLOW;
        }
        k >>= 1;
        if (k) {
            if (!b_ok || __builtin_mul_overflow(b, b, &b) || !fits(bits, b)) b_ok = 0;
        }
    }
    *out = r;
    return ZL_OK;
}

int zl_run(const zl_prog_t *p, zl_run_t *r)
{
    uint32_t sp = 0, rp = 0, pc = 0;
    size_t inpos = 0;
    vm_out_t o;
    int s = ZL_OK;

    if (!p || !r || (r->out_cap && !r->out) || (r->nin && !r->in)) return ZL_E_ARG;
    if (zl_verify(p, 0) != ZL_OK) return ZL_E_VERIFY;
    uint64_t fuel = r->fuel ? r->fuel : 10000000ull;
    uint32_t bits = p->word_bits;
    r->out_len = 0;
    r->used = 0;
    r->trap_line = 0;
    r->trap_pc = 0;
    if (r->out_cap) r->out[0] = 0;
    if (r->trace) r->trace->n = 0;
    o.r = r;
    o.col = 0;
    int64_t *st = r->stack, *mem = r->mem, *fr = r->frames, *heap = r->heap;
    uint32_t *rs = r->rstack;
    uint32_t fd = 0, ftop = 0, hp = 1; /* frame depth, first free frame cell, next pair */
    for (uint32_t i = 0; i < p->nslots; i++) mem[i] = 0;
    r->fbase[0] = r->fstat[0] = r->fsize[0] = 0; /* the main program's empty frame */
    if (r->preload) {
        if (r->nin > ZL_MAX_STACK) return ZL_E_STACK;
        for (; sp < r->nin; sp++) st[sp] = r->in[sp];
        inpos = r->nin;
    }

#define NEED(n)                                                                                    \
    do {                                                                                           \
        if (sp < (n)) {                                                                            \
            s = ZL_E_STACK;                                                                        \
            goto trap;                                                                             \
        }                                                                                          \
    } while (0)
#define ROOM(n)                                                                                    \
    do {                                                                                           \
        if (sp + (n) > ZL_MAX_STACK) {                                                             \
            s = ZL_E_STACK;                                                                        \
            goto trap;                                                                             \
        }                                                                                          \
    } while (0)
#define NUM(v) (bits != 62 || ((v) > -ZL_V_REF && (v) < ZL_V_REF))
#define CHK(v)                                                                                     \
    do {                                                                                           \
        if (!fits(bits, (v))) {                                                                    \
            s = ZL_E_OVERFLOW;                                                                     \
            goto trap;                                                                             \
        }                                                                                          \
    } while (0)

    for (;;) {
        if (pc >= p->ncode) {
            s = ZL_E_VERIFY;
            goto trap;
        }
        if (r->used >= fuel) {
            s = ZL_E_FUEL;
            goto trap;
        }
        r->used++;
        const zl_ins_t *in = &p->code[pc];
        uint32_t next = pc + 1;
        int64_t x, y, z;
        switch (in->op) {
        case ZL_HALT:
            return ZL_OK;
        case ZL_PUSH:
            ROOM(1);
            st[sp++] = in->a;
            break;
        case ZL_LOAD:
            ROOM(1);
            st[sp++] = mem[in->a];
            break;
        case ZL_STORE:
            NEED(1);
            mem[in->a] = st[--sp];
            break;
        case ZL_LOADX:
            NEED(1);
            x = st[sp - 1];
            if (x < 0 || x >= in->b) {
                s = ZL_E_BOUNDS;
                goto trap;
            }
            st[sp - 1] = mem[in->a + x];
            break;
        case ZL_STOREX:
            NEED(2);
            y = st[--sp];
            x = st[--sp];
            if (x < 0 || x >= in->b) {
                s = ZL_E_BOUNDS;
                goto trap;
            }
            mem[in->a + x] = y;
            break;
        case ZL_LOADA:
        case ZL_STOREA: {
            /* an address is a slot, or ZL_MAX_SLOTS + a live frame cell */
            int64_t *cell;
            NEED(in->op == ZL_LOADA ? 1 : 2);
            x = st[sp - 1];
            if (x >= 0 && x < (int64_t) p->nslots)
                cell = &mem[x];
            else if (x >= ZL_MAX_SLOTS && x < (int64_t) ZL_MAX_SLOTS + ftop)
                cell = &fr[x - ZL_MAX_SLOTS];
            else {
                s = ZL_E_BOUNDS;
                goto trap;
            }
            if (in->op == ZL_LOADA)
                st[sp - 1] = *cell;
            else {
                *cell = st[sp - 2];
                sp -= 2;
            }
            break;
        }
        case ZL_ADD:
        case ZL_SUB:
        case ZL_MUL:
        case ZL_DIVT:
        case ZL_MODT:
        case ZL_DIVF:
        case ZL_MODF:
        case ZL_DIVX:
        case ZL_POW:
        case ZL_MIN:
        case ZL_MAX:
        case ZL_EQ:
        case ZL_NE:
        case ZL_LT:
        case ZL_LE:
        case ZL_GT:
        case ZL_GE:
        case ZL_AND:
        case ZL_OR:
        case ZL_XOR:
            NEED(2);
            y = st[--sp];
            x = st[sp - 1];
            if (bits == 62 && in->op != ZL_EQ && in->op != ZL_NE && (!NUM(x) || !NUM(y))) {
                s = ZL_E_DOMAIN;
                goto trap;
            }
            switch (in->op) {
            case ZL_ADD:
                if (__builtin_add_overflow(x, y, &z)) {
                    s = ZL_E_OVERFLOW;
                    goto trap;
                }
                break;
            case ZL_SUB:
                if (__builtin_sub_overflow(x, y, &z)) {
                    s = ZL_E_OVERFLOW;
                    goto trap;
                }
                break;
            case ZL_MUL:
                if (__builtin_mul_overflow(x, y, &z)) {
                    s = ZL_E_OVERFLOW;
                    goto trap;
                }
                break;
            case ZL_DIVT:
            case ZL_MODT:
            case ZL_DIVF:
            case ZL_MODF:
            case ZL_DIVX:
                if (y == 0) {
                    s = ZL_E_DIVZERO;
                    goto trap;
                }
                if (y == -1) { /* avoid INT64_MIN / -1 */
                    if (in->op == ZL_MODT || in->op == ZL_MODF) {
                        z = 0;
                        break;
                    }
                    if (x == INT64_MIN) {
                        s = ZL_E_OVERFLOW;
                        goto trap;
                    }
                    z = -x;
                    break;
                }
                {
                    int64_t q = x / y, m = x % y;
                    if (in->op == ZL_DIVX && m != 0) {
                        s = ZL_E_INEXACT;
                        goto trap;
                    }
                    if ((in->op == ZL_DIVF || in->op == ZL_MODF) && m != 0 &&
                        ((m < 0) != (y < 0))) {
                        q -= 1;
                        m += y;
                    }
                    z = (in->op == ZL_MODT || in->op == ZL_MODF) ? m : q;
                }
                break;
            case ZL_POW:
                s = ipow(x, y, (int) in->a, bits, &z);
                if (s != ZL_OK) goto trap;
                break;
            case ZL_MIN:
                z = x < y ? x : y;
                break;
            case ZL_MAX:
                z = x > y ? x : y;
                break;
            case ZL_EQ:
                z = x == y;
                break;
            case ZL_NE:
                z = x != y;
                break;
            case ZL_LT:
                z = x < y;
                break;
            case ZL_LE:
                z = x <= y;
                break;
            case ZL_GT:
                z = x > y;
                break;
            case ZL_GE:
                z = x >= y;
                break;
            case ZL_AND:
                z = x & y;
                break;
            case ZL_OR:
                z = x | y;
                break;
            default:
                z = x ^ y;
                break;
            }
            CHK(z);
            st[sp - 1] = z;
            break;
        case ZL_NEG:
            NEED(1);
            if (!NUM(st[sp - 1])) {
                s = ZL_E_DOMAIN;
                goto trap;
            }
            x = st[sp - 1];
            if (x == INT64_MIN) {
                s = ZL_E_OVERFLOW;
                goto trap;
            }
            CHK(-x);
            st[sp - 1] = -x;
            break;
        case ZL_ABS:
            NEED(1);
            if (!NUM(st[sp - 1])) {
                s = ZL_E_DOMAIN;
                goto trap;
            }
            x = st[sp - 1];
            if (x == INT64_MIN) {
                s = ZL_E_OVERFLOW;
                goto trap;
            }
            x = x < 0 ? -x : x;
            CHK(x);
            st[sp - 1] = x;
            break;
        case ZL_SGN:
            NEED(1);
            if (!NUM(st[sp - 1])) {
                s = ZL_E_DOMAIN;
                goto trap;
            }
            x = st[sp - 1];
            st[sp - 1] = (x > 0) - (x < 0);
            break;
        case ZL_INVERT:
            NEED(1);
            if (!NUM(st[sp - 1])) {
                s = ZL_E_DOMAIN;
                goto trap;
            }
            st[sp - 1] = ~st[sp - 1];
            break;
        case ZL_NOT:
            NEED(1);
            st[sp - 1] = st[sp - 1] == 0;
            break;
        case ZL_DUP:
            NEED(1);
            ROOM(1);
            st[sp] = st[sp - 1];
            sp++;
            break;
        case ZL_DROP:
            NEED(1);
            sp--;
            break;
        case ZL_SWAP:
            NEED(2);
            x = st[sp - 1];
            st[sp - 1] = st[sp - 2];
            st[sp - 2] = x;
            break;
        case ZL_OVER:
            NEED(2);
            ROOM(1);
            st[sp] = st[sp - 2];
            sp++;
            break;
        case ZL_ROT:
            NEED(3);
            x = st[sp - 3];
            st[sp - 3] = st[sp - 2];
            st[sp - 2] = st[sp - 1];
            st[sp - 1] = x;
            break;
        case ZL_JMP:
            next = (uint32_t) in->a;
            break;
        case ZL_JZ:
            NEED(1);
            if (st[--sp] == 0) next = (uint32_t) in->a;
            break;
        case ZL_JNZ:
            NEED(1);
            if (st[--sp] != 0) next = (uint32_t) in->a;
            break;
        case ZL_CALL:
            if (rp >= ZL_MAX_CALLS) {
                s = ZL_E_RSTACK;
                goto trap;
            }
            rs[rp++] = next;
            next = (uint32_t) in->a;
            break;
        case ZL_RET:
            if (rp == 0) {
                s = ZL_E_RSTACK;
                goto trap;
            }
            next = rs[--rp];
            break;
        case ZL_OUTI: {
            char num[24], pad[64];
            size_t n, w = (size_t) in->b, k = 0;
            NEED(1);
            x = st[--sp];
            if (r->trace && r->trace->n < r->trace->cap) r->trace->v[r->trace->n++] = x;
            if ((in->a & ZL_FMT_SIGNSPACE) && x >= 0) num[k++] = ' ';
            n = k + zl_itoa(x, num + k);
            if (w > n) {
                for (size_t i = 0; i < w - n; i++) pad[i] = ' ';
                if ((s = vm_put(&o, pad, w - n)) != ZL_OK) goto trap;
            }
            if ((s = vm_put(&o, num, n)) != ZL_OK) goto trap;
            if ((in->a & ZL_FMT_TRAIL) && (s = vm_put(&o, " ", 1)) != ZL_OK) goto trap;
            break;
        }
        case ZL_OUTS:
            if ((s = vm_put(&o, p->str + p->str_off[in->a], p->str_len[in->a])) != ZL_OK) goto trap;
            break;
        case ZL_OUTC: {
            char ch;
            NEED(1);
            x = st[--sp];
            if (x < 0 || x > 255) {
                s = ZL_E_DOMAIN;
                goto trap;
            }
            ch = (char) x;
            if ((s = vm_put(&o, &ch, 1)) != ZL_OK) goto trap;
            break;
        }
        case ZL_NL:
            if ((s = vm_put(&o, "\n", 1)) != ZL_OK) goto trap;
            break;
        case ZL_TAB: {
            size_t zone = (size_t) in->a, target = (o.col / zone + 1) * zone;
            while (o.col < target)
                if ((s = vm_put(&o, " ", 1)) != ZL_OK) goto trap;
            break;
        }
        case ZL_IN:
            ROOM(1);
            if (inpos >= r->nin) {
                s = ZL_E_INPUT;
                goto trap;
            }
            x = r->in[inpos++];
            CHK(x);
            st[sp++] = x;
            break;
        case ZL_TRAP:
            s = (int) in->a;
            goto trap;
        case ZL_ENTER: {
            /* stack: static link (a frame handle), then b arguments */
            uint32_t nb = (uint32_t) in->b, base = ftop;
            NEED(nb + 1);
            x = st[sp - nb - 1];
            if (x < 0 || x > (int64_t) fd) {
                s = ZL_E_DOMAIN;
                goto trap;
            }
            if (fd + 1 >= ZL_MAX_CALLS || in->a > (int64_t) (ZL_MAX_FRAMES - ftop)) {
                s = ZL_E_RSTACK;
                goto trap;
            }
            fd++;
            r->fbase[fd] = base;
            r->fstat[fd] = (uint32_t) x;
            r->fsize[fd] = (uint32_t) in->a;
            ftop = base + (uint32_t) in->a;
            for (uint32_t i = 0; i < nb; i++) fr[base + i] = st[sp - nb + i];
            for (uint32_t i = nb; i < (uint32_t) in->a; i++) fr[base + i] = 0;
            sp -= nb + 1;
            break;
        }
        case ZL_LEAVE:
            if (fd == 0) {
                s = ZL_E_RSTACK;
                goto trap;
            }
            ftop = r->fbase[fd--];
            break;
        case ZL_LINK:
        case ZL_LOADF:
        case ZL_STOREF:
        case ZL_ADDRF: {
            /* walk hops static links out; handles only ever point inward */
            uint32_t f = fd;
            for (int64_t h = in->op == ZL_LINK ? in->a : in->b; h > 0 && f; h--) f = r->fstat[f];
            if (in->op == ZL_LINK) {
                ROOM(1);
                st[sp++] = f;
                break;
            }
            if (in->a >= (int64_t) r->fsize[f]) {
                s = ZL_E_BOUNDS;
                goto trap;
            }
            uint32_t c = r->fbase[f] + (uint32_t) in->a;
            if (in->op == ZL_LOADF) {
                ROOM(1);
                st[sp++] = fr[c];
            } else if (in->op == ZL_STOREF) {
                NEED(1);
                fr[c] = st[--sp];
            } else {
                ROOM(1);
                st[sp++] = (int64_t) ZL_MAX_SLOTS + c;
            }
            break;
        }
        case ZL_BOUND:
            NEED(1);
            x = st[sp - 1];
            if (x < 0 || x >= in->a) {
                s = ZL_E_BOUNDS;
                goto trap;
            }
            break;
        case ZL_CONS:
            NEED(2);
            if (hp >= ZL_MAX_CELLS) {
                s = ZL_E_HEAP;
                goto trap;
            }
            heap[2 * hp] = st[sp - 2];
            heap[2 * hp + 1] = st[sp - 1];
            st[sp - 2] = ZL_V_REF + hp++;
            sp--;
            break;
        case ZL_CAR:
        case ZL_CDR: {
            NEED(1);
            uint32_t k = pair_of(st[sp - 1], hp);
            if (k)
                st[sp - 1] = heap[2 * k + (in->op == ZL_CDR)];
            else if (!(in->a == 1 && st[sp - 1] == ZL_V_NIL)) {
                s = ZL_E_DOMAIN;
                goto trap;
            }
            break;
        }
        case ZL_SETCAR:
        case ZL_SETCDR: {
            NEED(2);
            uint32_t k = pair_of(st[sp - 2], hp);
            if (!k) {
                s = ZL_E_DOMAIN;
                goto trap;
            }
            heap[2 * k + (in->op == ZL_SETCDR)] = st[sp - 1];
            sp -= 2;
            break;
        }
        case ZL_ISA:
            NEED(1);
            x = st[sp - 1];
            switch (in->a) {
            case ZL_ISA_NUMBER:
                z = x > -ZL_V_REF && x < ZL_V_REF;
                break;
            case ZL_ISA_PAIR:
                z = pair_of(x, hp) != 0;
                break;
            case ZL_ISA_NULL:
                z = x == ZL_V_NIL;
                break;
            default:
                z = x >= ZL_V_SYM && x < ZL_V_SYM + (int64_t) p->nstr;
                break;
            }
            st[sp - 1] = z;
            break;
        case ZL_OUTV:
            NEED(1);
            x = st[--sp];
            if ((s = print_value(&o, p, heap, hp, x, (int) in->a)) != ZL_OK) goto trap;
            break;
        default:
            s = ZL_E_VERIFY;
            goto trap;
        }
        pc = next;
    }
trap:
    r->trap_pc = pc;
    r->trap_line = pc < p->ncode ? p->code[pc].line : 0;
    return s;
#undef NEED
#undef ROOM
#undef CHK
#undef NUM
}

/* ---------------------------------------------------------------- entry */

static int dispatch(int lang, const char *src, size_t len, zl_cc_t *c, zl_out_t *tw, void *fe)
{
    switch (lang) {
    case ZL_LANG_FORTRAN:
        return zl_fortran_compile(src, len, c, tw, fe);
    case ZL_LANG_BASIC:
        return zl_basic_compile(src, len, c, tw, fe);
    case ZL_LANG_FORTH:
        return zl_forth_compile(src, len, c, tw, fe);
    case ZL_LANG_PASCAL:
        return zl_pascal_compile(src, len, c, tw, fe);
    case ZL_LANG_ALGOL60:
        return zl_algol_compile(src, len, c, tw, fe);
    case ZL_LANG_LISP:
    case ZL_LANG_SCHEME:
        return zl_lisp_compile(src, len, c, tw, fe);
    case ZL_LANG_LOGO:
        return zl_logo_compile(src, len, c, tw, fe);
    default:
        return zl_fail(c, ZL_E_ARG, "unknown language");
    }
}

static uint32_t line_of(const char *src, size_t off)
{
    uint32_t line = 1;
    for (size_t i = 0; i < off; i++)
        if (src[i] == '\n') line++;
    return line;
}

int zl_compile(int lang, const char *src, size_t len, zl_prog_t *p, zl_work_t *w, zl_diag_t *d)
{
    zl_cc_t c;
    zl_out_t cmp;
    int base = lang & ~ZL_TWIN, twin = (lang & ZL_TWIN) != 0;
    if (!d) return ZL_E_ARG;
    d->status = ZL_OK;
    d->line = 0;
    d->col = 0;
    d->msg = "ok";
    if (!p || !w || (!src && len) || base <= 0 || base >= ZL_LANG__COUNT ||
        (lang & ~(ZL_TWIN | 0xff))) {
        d->status = ZL_E_ARG;
        d->msg = "bad argument";
        return ZL_E_ARG;
    }
    zl_bzero(p, sizeof *p);
    zl_cc_init(&c, p, d, base, 64);
    zl_bzero(&cmp, sizeof cmp);
    cmp.cmp = src;
    cmp.cmp_len = len;
    int s = dispatch(base, src ? src : "", len, &c, twin ? &cmp : 0, w->fe);
    if (c.failed) s = d->status; /* the first recorded failure is the answer */
    if (s == ZL_OK && !c.failed && twin && (cmp.diff || cmp.len != len)) {
        size_t at = cmp.diff ? cmp.diff_at : (cmp.len < len ? cmp.len : len);
        c.line = line_of(src, at);
        c.col = 0;
        s = zl_fail(&c, ZL_E_NONCANON, "not the canonical twin spelling (zl_twin gives it)");
    }
    if (s == ZL_OK && !c.failed)
        s = zl_verify(p, d);
    else if (s == ZL_OK)
        s = d->status;
    if (s != ZL_OK) zl_bzero(p, sizeof *p);
    return s;
}

int zl_twin(int lang, const char *src, size_t len, char *out, size_t cap, size_t *out_len,
            zl_work_t *w, zl_diag_t *d)
{
    zl_cc_t c;
    zl_out_t o;
    int base = lang & ~ZL_TWIN;
    if (!d) return ZL_E_ARG;
    d->status = ZL_OK;
    d->line = 0;
    d->col = 0;
    d->msg = "ok";
    if (!w || !out || !cap || !out_len || (!src && len) || base <= 0 || base >= ZL_LANG__COUNT ||
        (lang & ~(ZL_TWIN | 0xff))) {
        d->status = ZL_E_ARG;
        d->msg = "bad argument";
        return ZL_E_ARG;
    }
    *out_len = 0;
    out[0] = 0;
    zl_bzero(&w->tmp, sizeof w->tmp);
    zl_cc_init(&c, &w->tmp, d, base, 64);
    zl_bzero(&o, sizeof o);
    o.p = out;
    o.cap = cap;
    int s = dispatch(base, src ? src : "", len, &c, &o, w->fe);
    if (c.failed) s = d->status;
    if (s == ZL_OK && o.over)
        s = zl_fail(&c, ZL_E_LIMIT, "twin text does not fit the output buffer");
    if (s == ZL_OK) s = zl_verify(&w->tmp, d);
    if (s != ZL_OK) {
        out[0] = 0;
        return s;
    }
    *out_len = o.len;
    return ZL_OK;
}
