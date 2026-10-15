/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxp_rt.c — the runtime under projected programs (zxp_rt.h). Each
 * function does what the same instruction does in zl_run (zl_ir.c); the
 * projection tests run every projected program against zl_run. */
#include "zxp_rt.h"

void zxp_begin(zxp_io_t *io, uint32_t nslots)
{
    volatile int64_t *m = io->mem; /* keep the loop from becoming memset */
    for (uint32_t i = 0; i < nslots; i++) m[i] = 0;
    if (!io->fuel) io->fuel = 10000000ull;
    io->used = 0;
    io->trap_pc = 0;
    io->inpos = 0;
    io->len = 0;
    io->col = 0;
    if (io->cap) io->out[0] = 0;
}

int zxp_fits(uint32_t bits, int64_t v)
{
    return bits == 64 || (v >= INT32_MIN && v <= INT32_MAX);
}

static int put(zxp_io_t *io, const char *s, uint64_t n)
{
    for (uint64_t i = 0; i < n; i++) {
        if (io->len + 1 > io->cap) return ZL_E_OUTPUT;
        io->out[io->len++] = s[i];
        io->col = s[i] == '\n' ? 0 : io->col + 1;
    }
    if (io->len < io->cap) io->out[io->len] = 0;
    return ZL_OK;
}

static uint64_t digits(int64_t x, char *num, uint64_t k)
{
    char t[24];
    uint64_t n = 0, u = x < 0 ? (uint64_t) 0 - (uint64_t) x : (uint64_t) x;
    do t[n++] = (char) ('0' + u % 10u);
    while ((u /= 10u) != 0);
    if (x < 0) num[k++] = '-';
    while (n) num[k++] = t[--n];
    return k;
}

int zxp_outi(zxp_io_t *io, int64_t x, int64_t flags, int64_t width)
{
    char num[24];
    uint64_t k = 0, n, w = (uint64_t) width;
    int s;
    if ((flags & ZL_FMT_SIGNSPACE) && x >= 0) num[k++] = ' ';
    n = digits(x, num, k);
    for (; w > n; w--)
        if ((s = put(io, " ", 1)) != ZL_OK) return s;
    if ((s = put(io, num, n)) != ZL_OK) return s;
    if (flags & ZL_FMT_TRAIL) return put(io, " ", 1);
    return ZL_OK;
}

int zxp_outs(zxp_io_t *io, const char *s, uint64_t n)
{
    return put(io, s, n);
}

int zxp_outc(zxp_io_t *io, int64_t x)
{
    char ch = (char) x;
    if (x < 0 || x > 255) return ZL_E_DOMAIN;
    return put(io, &ch, 1);
}

int zxp_nl(zxp_io_t *io)
{
    return put(io, "\n", 1);
}

int zxp_tab(zxp_io_t *io, int64_t zone)
{
    uint64_t z = (uint64_t) zone, target = (io->col / z + 1u) * z;
    int s;
    while (io->col < target)
        if ((s = put(io, " ", 1)) != ZL_OK) return s;
    return ZL_OK;
}

int zxp_outd(zxp_io_t *io, int64_t x, int64_t a, int64_t b)
{
    char num[48], dg[24];
    uint64_t k = 0, nd = 0, w = (uint64_t) (a >> 8), sc = (uint64_t) (a & 255);
    uint64_t m = x < 0 ? (uint64_t) 0 - (uint64_t) x : (uint64_t) x;
    int s;
    do dg[nd++] = (char) ('0' + m % 10u);
    while ((m /= 10u) != 0);
    while (nd <= sc) dg[nd++] = '0';
    if (x < 0)
        num[k++] = '-';
    else if (b & ZL_FMT_SIGNSPACE)
        num[k++] = ' ';
    while (nd) {
        if (nd == sc) num[k++] = '.';
        num[k++] = dg[--nd];
    }
    for (; w > k; w--)
        if ((s = put(io, " ", 1)) != ZL_OK) return s;
    if ((s = put(io, num, k)) != ZL_OK) return s;
    if (b & ZL_FMT_TRAIL) return put(io, " ", 1);
    return ZL_OK;
}

int zxp_in(zxp_io_t *io, uint32_t bits, int64_t *x)
{
    if (io->inpos >= io->nin) return ZL_E_INPUT;
    *x = io->in[io->inpos++];
    return zxp_fits(bits, *x) ? ZL_OK : ZL_E_OVERFLOW;
}

int zxp_div(int op, int64_t x, int64_t y, uint32_t bits, int64_t *z)
{
    if (y == 0) return ZL_E_DIVZERO;
    if (y == -1) {
        if (op == ZL_MODT || op == ZL_MODF) {
            *z = 0;
            return ZL_OK;
        }
        if (x == INT64_MIN) return ZL_E_OVERFLOW;
        *z = -x;
        return zxp_fits(bits, *z) ? ZL_OK : ZL_E_OVERFLOW;
    }
    int64_t q = x / y, m = x % y;
    if (op == ZL_DIVX && m != 0) return ZL_E_INEXACT;
    if ((op == ZL_DIVF || op == ZL_MODF) && m != 0 && ((m < 0) != (y < 0))) {
        q -= 1;
        m += y;
    }
    *z = (op == ZL_MODT || op == ZL_MODF) ? m : q;
    return zxp_fits(bits, *z) ? ZL_OK : ZL_E_OVERFLOW;
}

int zxp_pow(int64_t x, int64_t y, int64_t mode, uint32_t bits, int64_t *z)
{
    if (y < 0) {
        if (x == 0) return ZL_E_DIVZERO;
        if (x == 1 || x == -1) {
            *z = x == 1 || !(y & 1) ? 1 : -1;
            return ZL_OK;
        }
        if (mode == 1) return ZL_E_INEXACT;
        *z = 0;
        return ZL_OK;
    }
    int64_t r = 1, b = x;
    uint64_t k = (uint64_t) y;
    int b_ok = 1;
    while (k) {
        if (k & 1)
            if (!b_ok || __builtin_mul_overflow(r, b, &r) || !zxp_fits(bits, r))
                return ZL_E_OVERFLOW;
        k >>= 1;
        if (k && (!b_ok || __builtin_mul_overflow(b, b, &b) || !zxp_fits(bits, b))) b_ok = 0;
    }
    *z = r;
    return ZL_OK;
}

int zxp_shift(int op, int64_t x, int64_t y, uint32_t bits, int64_t *z)
{
    if (y < 0 || y > 63) return ZL_E_DOMAIN;
    if (op == ZL_SHR)
        *z = x >> y;
    else {
        if (x && (y > 62 || (x > 0 ? x > (INT64_MAX >> y) : x < (INT64_MIN >> y))))
            return ZL_E_OVERFLOW;
        *z = (int64_t) ((uint64_t) x << y);
    }
    return zxp_fits(bits, *z) ? ZL_OK : ZL_E_OVERFLOW;
}

int zxp_wrap(int64_t x, int64_t a, int64_t b, uint32_t bits, int64_t *z)
{
    uint64_t m = (uint64_t) 1 << a, u = (uint64_t) x & (m - 1u);
    *z = (int64_t) (b || u < (m >> 1) ? u : u | ~(m - 1u));
    return zxp_fits(bits, *z) ? ZL_OK : ZL_E_OVERFLOW;
}

int zxp_range(int64_t x, int64_t a, int64_t b)
{
    return x > a || x < -a - b ? ZL_E_OVERFLOW : ZL_OK;
}
