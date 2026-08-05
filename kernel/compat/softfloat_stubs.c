/* softfloat_stubs.c — minimal soft-float helper stubs for RISC-V 32
 *
 * Provides __fixdfsi and __fixdfdi that GCC generates calls to
 * when converting double to int without hardware float support.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * License: SEL-3.3
 */

typedef unsigned long long u64;
typedef signed long long s64;

/* Convert double to int (32-bit) */
int __fixdfsi(double d) {
    if (d >= 2147483647.0) return 2147483647;
    if (d <= -2147483648.0) return -2147483648;
    return (int)d;
}

/* Convert double to long long (64-bit) */
s64 __fixdfdi(double d) {
    if (d >= 9223372036854775807.0) return 9223372036854775807LL;
    if (d <= -9223372036854775808.0) return (-9223372036854775807LL - 1);
    return (s64)d;
}

/* Convert float to int */
int __fixsfsi(float f) {
    if (f >= 2147483647.0f) return 2147483647;
    if (f <= -2147483648.0f) return -2147483648;
    return (int)f;
}

/* Convert int to double */
double __floatsidf(int i) {
    return (double)i;
}

/* Convert long long to double */
double __floatdidf(s64 i) {
    return (double)i;
}

/* Convert unsigned long long to double */
double __floatundidf(u64 i) {
    return (double)i;
}

/* Convert double to unsigned int */
unsigned int __fixunssfsi(float f) {
    if (f <= 0.0f) return 0;
    if (f >= 4294967295.0f) return 4294967295u;
    return (unsigned int)f;
}

/* Convert double to unsigned long long */
u64 __fixunsdfdi(double d) {
    if (d <= 0.0) return 0;
    if (d >= 18446744073709551615.0) return 0xFFFFFFFFFFFFFFFFULL;
    return (u64)d;
}

/* Convert double to unsigned long */
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

/* Convert float to long long */
s64 __fixsfdi(float f) {
    if (f >= 9223372036854775807.0f) return 9223372036854775807LL;
    if (f <= -9223372036854775808.0f) return (-9223372036854775807LL - 1);
    return (s64)f;
}

/* Convert int to float */
float __floatsisf(int i) {
    return (float)i;
}

/* Convert long long to float */
float __floatdisf(s64 i) {
    return (float)i;
}

/* Convert float to double */
double __extendsfdf2(float f) {
    return (double)f;
}

/* Convert double to float */
float __truncdfsf2(double d) {
    return (float)d;
}

/* 64-bit division helpers for rv32 (no native 64-bit div) */
s64 __divdi3(s64 a, s64 b) {
    if (b == 0) return 0;
    int neg = 0;
    if (a < 0) { a = -a; neg = 1; }
    if (b < 0) { b = -b; neg ^= 1; }
    u64 ua = (u64)a, ub = (u64)b, q = 0;
    while (ua >= ub) { ua -= ub; q++; }
    return neg ? -(s64)q : (s64)q;
}

s64 __moddi3(s64 a, s64 b) {
    if (b == 0) return 0;
    int neg = (a < 0);
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    u64 ua = (u64)a, ub = (u64)b;
    while (ua >= ub) ua -= ub;
    return neg ? -(s64)ua : (s64)ua;
}

u64 __udivdi3(u64 a, u64 b) {
    if (b == 0) return 0;
    u64 q = 0;
    while (a >= b) { a -= b; q++; }
    return q;
}

u64 __umoddi3(u64 a, u64 b) {
    if (b == 0) return 0;
    while (a >= b) a -= b;
    return a;
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
