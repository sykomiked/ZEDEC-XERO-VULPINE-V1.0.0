/* zxv_ti3_shim.c — the 128-bit integer builtins the rational module uses,
 * implemented in C for the wasm32 engine (no compiler-rt builtins ship
 * with the host clang). Everything is done on the 64-bit halves so no
 * 128-bit shift helper is needed either; the results are exact.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>

typedef unsigned __int128 u128;
typedef __int128 i128;

typedef struct {
    uint64_t lo, hi;
} u128p;

static u128p split(u128 v)
{
    u128p r;
    r.lo = (uint64_t) v;
    r.hi = (uint64_t) (v >> 64);
    return r;
}

static u128 join(u128p p)
{
    return ((u128) p.hi << 64) | (u128) p.lo;
}

/* the bit `i` of p (i < 128) */
static uint32_t bit(u128p p, int i)
{
    return i < 64 ? (uint32_t) ((p.lo >> i) & 1u) : (uint32_t) ((p.hi >> (i - 64)) & 1u);
}

/* p = p << 1, plus `add` */
static u128p shl1(u128p p, uint32_t add)
{
    u128p r;
    r.lo = (p.lo << 1) | add;
    r.hi = (p.hi << 1) | (p.lo >> 63);
    return r;
}

/* p -= d (unsigned, p >= d) */
static u128p sub(u128p p, u128p d)
{
    u128p r;
    r.lo = p.lo - d.lo;
    r.hi = p.hi - d.hi - (p.lo < d.lo ? 1u : 0u);
    return r;
}

/* the quotient bit i set */
static u128p setbit(u128p q, int i)
{
    if (i < 64) q.lo |= ((uint64_t) 1) << i;
    else q.hi |= ((uint64_t) 1) << (i - 64);
    return q;
}

static int ge(u128p a, u128p b)
{
    return a.hi != b.hi ? a.hi > b.hi : a.lo >= b.lo;
}

static void udivmod128(u128 n, u128 d, u128 *q, u128 *r)
{
    u128p nn = split(n), dd = split(d);
    u128p quot = {0, 0}, rem = {0, 0};
    if (d == 0) {
        *q = 0;
        *r = 0;
        return;
    }
    for (int i = 127; i >= 0; i--) {
        rem = shl1(rem, bit(nn, i));
        if (ge(rem, dd)) {
            rem = sub(rem, dd);
            quot = setbit(quot, i);
        }
    }
    *q = join(quot);
    *r = join(rem);
}

unsigned __int128 __udivti3(unsigned __int128 n, unsigned __int128 d)
{
    u128 q, r;
    udivmod128(n, d, &q, &r);
    return q;
}

unsigned __int128 __umodti3(unsigned __int128 n, unsigned __int128 d)
{
    u128 q, r;
    udivmod128(n, d, &q, &r);
    return r;
}

/* 128-bit multiply on the halves */
__int128 __multi3(__int128 a, __int128 b)
{
    u128p x = split((u128) a), y = split((u128) b);
    uint64_t lolo = x.lo * y.lo;
    uint64_t lohi = x.lo * y.hi;
    uint64_t hilo = x.hi * y.lo;
    uint64_t hihi = x.hi * y.hi;
    uint64_t mid = (lolo >> 32) + (uint32_t) lohi + (uint32_t) hilo;
    u128p r;
    r.lo = (lolo & 0xffffffffu) | (mid << 32);
    r.hi = hihi + (lohi >> 32) + (hilo >> 32) + (mid >> 32);
    return (i128) join(r);
}

__int128 __divti3(__int128 n, __int128 d)
{
    int neg = 0;
    u128 un = (u128) n, ud = (u128) d;
    if (n < 0) { un = ~un + 1u; neg ^= 1; }
    if (d < 0) { ud = ~ud + 1u; neg ^= 1; }
    u128 q, r;
    udivmod128(un, ud, &q, &r);
    return neg ? -(i128) q : (i128) q;
}

__int128 __modti3(__int128 n, __int128 d)
{
    int neg = n < 0;
    u128 un = (u128) n, ud = (u128) d;
    if (n < 0) un = ~un + 1u;
    if (d < 0) ud = ~ud + 1u;
    u128 q, r;
    udivmod128(un, ud, &q, &r);
    return neg ? -(i128) r : (i128) r;
}
