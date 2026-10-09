/* softfloat_stubs.c — minimal soft-float helper stubs for RISC-V 32
 *
 * Provides __fixdfsi and __fixdfdi that GCC generates calls to
 * when converting double to int without hardware float support.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */

typedef unsigned long long u64;
typedef signed long long s64;
typedef unsigned int u32;

/* IMPORTANT — non-recursion rule for this file:
 * On rv32imafd there is NO 64-bit integer <-> float instruction (fcvt.d.l /
 * fcvt.l.d only exist on rv64), so GCC lowers those conversions to calls into
 * THESE routines. A body written as `return (double)i;` for a 64-bit `i` lowers
 * to a call to the very routine being defined -> infinite self-recursion that
 * marches the stack down until it faults (this actually hung rv32 boot). So the
 * 64-bit conversions below are done by splitting into 32-bit halves and using
 * only the NATIVE 32-bit conversions (u32<->double = fcvt.d.wu / fcvt.wu.d),
 * which are real single instructions and never call back in.
 * (On rv64/arm64 these routines are dead: the compiler inlines native 64-bit
 *  fcvt/scvtf at the call site and never invokes them.) */

#define TWO_POW_32 4294967296.0            /* 2^32 */
#define TWO_POW_64 18446744073709551616.0  /* 2^64 */

/* Convert double to int (32-bit) — native fcvt.w.d, no call-back */
int __fixdfsi(double d) {
    if (d >= 2147483647.0) return 2147483647;
    if (d <= -2147483648.0) return -2147483648;
    return (int)d;
}

/* Convert float to int — native fcvt.w.s */
int __fixsfsi(float f) {
    if (f >= 2147483647.0f) return 2147483647;
    if (f <= -2147483648.0f) return -2147483648;
    return (int)f;
}

/* Convert int (32-bit) to double — native fcvt.d.w */
double __floatsidf(int i) {
    return (double)i;
}

/* Convert unsigned long long to double via 32-bit halves (native u32->double) */
double __floatundidf(u64 i) {
    u32 hi = (u32)(i >> 32);
    u32 lo = (u32)(i & 0xFFFFFFFFu);
    return (double)hi * TWO_POW_32 + (double)lo;
}

/* Convert long long to double: sign + magnitude (INT64_MIN-safe) */
double __floatdidf(s64 i) {
    if (i < 0) {
        u64 mag = (u64)(-(i + 1)) + 1u;   /* avoids overflow at INT64_MIN */
        return -__floatundidf(mag);
    }
    return __floatundidf((u64)i);
}

/* Convert double to unsigned int — native fcvt.wu.d */
unsigned int __fixunssfsi(float f) {
    if (f <= 0.0f) return 0;
    if (f >= 4294967295.0f) return 4294967295u;
    return (unsigned int)f;
}

/* Convert double to unsigned long long via 32-bit halves (native double<->u32) */
u64 __fixunsdfdi(double d) {
    if (d < 1.0) return 0;
    if (d >= TWO_POW_64) return 0xFFFFFFFFFFFFFFFFULL;
    u32 hi = (u32)(d / TWO_POW_32);            /* native fcvt.wu.d */
    double rem = d - (double)hi * TWO_POW_32;  /* native u32->double */
    if (rem < 0.0) rem = 0.0;
    u32 lo = (u32)rem;
    return ((u64)hi << 32) | lo;
}

/* Convert double to long long: sign + magnitude */
s64 __fixdfdi(double d) {
    if (d <= -9223372036854775808.0) return (-9223372036854775807LL - 1);
    if (d >= 9223372036854775807.0) return 9223372036854775807LL;
    if (d < 0.0) return -(s64)__fixunsdfdi(-d);
    return (s64)__fixunsdfdi(d);
}

/* Convert double to unsigned long — native fcvt.wu.d */
unsigned long __fixunsdfsi(double d) {
    if (d <= 0.0) return 0;
    if (d >= 4294967295.0) return 4294967295u;
    return (unsigned long)d;
}

/* Double comparison helpers */
int __eqdf2(double a, double b) { return a == b ? 0 : 1; }
int __nedf2(double a, double b) { return a != b ? 0 : 1; }
int __gtdf2(double a, double b) { return a > b ? 1 : (a < b ? -1 : 0); }
int __gedf2(double a, double b) { return a >= b ? 1 : (a < b ? -1 : 0); }
int __ltdf2(double a, double b) { return a < b ? -1 : (a > b ? 1 : 0); }
int __ledf2(double a, double b) { return a <= b ? -1 : (a > b ? 1 : 0); }

/* Single-precision comparison helpers */
int __eqsf2(float a, float b) { return a == b ? 0 : 1; }
int __nesf2(float a, float b) { return a != b ? 0 : 1; }
int __gtsf2(float a, float b) { return a > b ? 1 : (a < b ? -1 : 0); }
int __gesf2(float a, float b) { return a >= b ? 1 : (a < b ? -1 : 0); }
int __ltsf2(float a, float b) { return a < b ? -1 : (a > b ? 1 : 0); }
int __lesf2(float a, float b) { return a <= b ? -1 : (a > b ? 1 : 0); }

/* Convert float to long long — via double (native float->double), then the
 * non-recursive __fixdfdi. A direct `(s64)f` would call back into __fixsfdi. */
s64 __fixsfdi(float f) {
    return __fixdfdi((double)f);
}

/* Convert float to unsigned long long — via the non-recursive __fixunsdfdi */
u64 __fixunssfdi(float f) {
    return __fixunsdfdi((double)f);
}

/* Convert int to float — native fcvt.s.w */
float __floatsisf(int i) {
    return (float)i;
}

/* Convert long long to float — build the double (non-recursive), then narrow
 * with the native double->float (fcvt.s.d). A direct `(float)i` on a 64-bit i
 * would call back into __floatdisf. */
float __floatdisf(s64 i) {
    return (float)__floatdidf(i);
}

/* Convert unsigned long long to float — same pattern via __floatundidf */
float __floatundisf(u64 i) {
    return (float)__floatundidf(i);
}

/* Convert float to double */
double __extendsfdf2(float f) {
    return (double)f;
}

/* Convert double to float */
float __truncdfsf2(double d) {
    return (float)d;
}

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

/* Complex double multiply helper: (a+bi)*(c+di) = (ac-bd) + (ad+bc)i
 *
 * GCC expects __muldc3 to return _Complex double. */
double _Complex __muldc3(double a, double b, double c, double d) {
    return __builtin_complex(a * c - b * d, a * d + b * c);
}
