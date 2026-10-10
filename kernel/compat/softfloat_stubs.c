/* softfloat_stubs.c — integer compiler-runtime helpers for the kernel images
 *
 * Kernel images are integer-only: arm64/x86_64/arm32 build with
 * -mgeneral-regs-only and riscv with an integer -march/-mabi (no F/D), so
 * any float, double or complex use is a compile or link error. This file
 * therefore no longer carries soft-float conversion or comparison stubs
 * (__fixdfsi, __floatdidf, __eqdf2, __muldc3, ...): providing them would
 * have let a float use link silently on riscv. What remains is the integer
 * runtime GCC calls into on targets without the native instruction:
 *   - 64-bit divide/modulo/shift for rv32 (__udivdi3 and friends);
 *   - 128-bit divide/modulo for rv64 (__udivti3 and friends), because the
 *     only libgcc on this toolchain is built for lp64d and cannot be linked
 *     with the lp64 (soft-float ABI) kernel objects.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

typedef unsigned long long u64;
typedef signed long long s64;

/* 64-bit division helpers for rv32 (no native 64-bit div).
 * Binary long division — O(64), not O(quotient). The previous repeated-
 * subtraction form looped once per unit of quotient, so a divide like
 * 10^18 / 1 would spin ~10^18 times and hang the kernel. */
static u64 udivmod64(u64 num, u64 den, u64 *rem_out) {
    if (den == 0) { if (rem_out) *rem_out = num; return 0; } /* avoid trap */
    u64 quot = 0, rem = 0;
    for (int bit = 63; bit >= 0; bit--) {
        rem = (rem << 1) | ((num >> bit) & 1u);
        if (rem >= den) { rem -= den; quot |= (u64)1 << bit; }
    }
    if (rem_out) *rem_out = rem;
    return quot;
}

u64 __udivdi3(u64 a, u64 b) {
    return udivmod64(a, b, 0);
}

u64 __umoddi3(u64 a, u64 b) {
    u64 rem;
    udivmod64(a, b, &rem);
    return rem;
}

s64 __divdi3(s64 a, s64 b) {
    int neg = 0;
    u64 ua = (a < 0) ? ((u64)(-(a + 1)) + 1u) : (u64)a;   /* INT64_MIN-safe */
    u64 ub = (b < 0) ? ((u64)(-(b + 1)) + 1u) : (u64)b;
    if (a < 0) neg ^= 1;
    if (b < 0) neg ^= 1;
    u64 q = udivmod64(ua, ub, 0);
    return neg ? -(s64)q : (s64)q;
}

s64 __moddi3(s64 a, s64 b) {
    int neg = (a < 0);
    u64 ua = (a < 0) ? ((u64)(-(a + 1)) + 1u) : (u64)a;
    u64 ub = (b < 0) ? ((u64)(-(b + 1)) + 1u) : (u64)b;
    u64 rem;
    udivmod64(ua, ub, &rem);
    return neg ? -(s64)rem : (s64)rem;
}

/* 64-bit shift helpers for rv32 */
s64 __ashldi3(s64 a, int b) {
    if (b <= 0) return a;
    if (b >= 64) return 0;
    return (s64)((u64)a << b);
}

s64 __ashrdi3(s64 a, int b) {
    if (b <= 0) return a;
    if (b >= 64) return (a < 0) ? -1 : 0;
    return a >> b;
}

u64 __lshrdi3(u64 a, int b) {
    if (b <= 0) return a;
    if (b >= 64) return 0;
    return a >> b;
}

#if defined(__riscv) && __riscv_xlen == 64
/* 128-bit divide helpers for rv64 (lp64). The kernel's existing __int128
 * users (surplus.h Q32.32 divide, rational.c, ...) lower to these. Binary
 * long division, O(128); divide by zero returns 0 / the numerator rather
 * than trapping. */
typedef unsigned __int128 u128;
typedef __int128 s128;

static u128 udivmod128(u128 num, u128 den, u128 *rem_out)
{
    u128 quot = 0, rem = 0;
    if (den == 0) {
        if (rem_out) *rem_out = num;
        return 0;
    }
    if ((num >> 64) == 0 && (den >> 64) == 0) { /* native 64-bit divide */
        u64 n = (u64) num, d = (u64) den;
        if (rem_out) *rem_out = n % d;
        return n / d;
    }
    for (int bit = 127; bit >= 0; bit--) {
        rem = (rem << 1) | ((num >> bit) & 1u);
        if (rem >= den) {
            rem -= den;
            quot |= (u128) 1 << bit;
        }
    }
    if (rem_out) *rem_out = rem;
    return quot;
}

static u128 mag128(s128 v)
{
    return v < 0 ? (u128) 0 - (u128) v : (u128) v;
}

u128 __udivti3(u128 a, u128 b);
u128 __umodti3(u128 a, u128 b);
s128 __divti3(s128 a, s128 b);
s128 __modti3(s128 a, s128 b);

u128 __udivti3(u128 a, u128 b)
{
    return udivmod128(a, b, 0);
}

u128 __umodti3(u128 a, u128 b)
{
    u128 r;
    (void) udivmod128(a, b, &r);
    return r;
}

s128 __divti3(s128 a, s128 b)
{
    u128 q = udivmod128(mag128(a), mag128(b), 0);
    return ((a < 0) != (b < 0)) ? (s128) ((u128) 0 - q) : (s128) q;
}

s128 __modti3(s128 a, s128 b)
{
    u128 r;
    (void) udivmod128(mag128(a), mag128(b), &r);
    return a < 0 ? (s128) ((u128) 0 - r) : (s128) r;
}
#endif
