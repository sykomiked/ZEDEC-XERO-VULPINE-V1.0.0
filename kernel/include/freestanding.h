/* freestanding.h — Minimal freestanding runtime for VOVINA SHAKINA
 * Provides memset, memcpy, strlen, and stubs for assert/malloc
 * since we compile with -ffreestanding -nostdlib
 */
#ifndef FREESTANDING_H
#define FREESTANDING_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

/* bool type — freestanding_stubs/stdbool.h provides the real _Bool mapping;
 * do not define it here to avoid conflict with files that include <stdbool.h>. */
#ifndef TEST_HOST
/* In case stdbool.h is not available, fall back to an int typedef in C11
 * where _Bool is a keyword but the system doesn't supply <stdbool.h>. */
#include <stdbool.h>
#endif

/* Memory operations */
static inline void *fs_memset(void *dst, int c, size_t n) {
    uint8_t *d = (uint8_t*)dst;
    for (size_t i = 0; i < n; i++) d[i] = (uint8_t)c;
    return dst;
}

static inline void *fs_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static inline int fs_memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

static inline size_t fs_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static inline int fs_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* Substring search. Returns a pointer INTO haystack at the first occurrence of
 * needle, or NULL. An empty needle matches at position 0, as strstr() does.
 * Naive O(n*m) on purpose: every caller in this tree searches short protocol
 * names (panopticon_vpn's "wire"/"ipsec"/"tor" classifier), so the constant
 * factor of a Boyer-Moore skip table would cost more than it saves. */
static inline char *fs_strstr(const char *hay, const char *needle) {
    if (!hay || !needle) return (char*)0;
    if (!needle[0]) return (char*)hay;
    for (size_t i = 0; hay[i]; i++) {
        size_t j = 0;
        while (needle[j] && hay[i + j] == needle[j]) j++;
        if (!needle[j]) return (char*)&hay[i];
    }
    return (char*)0;
}

static inline char *fs_strcpy(char *dst, const char *src) {
    size_t i = 0;
    while (src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
    return dst;
}

/* Simple bump allocator (one 1 MB arena per translation unit). Every block
 * starts 16-byte aligned. n is checked against the space left BEFORE any
 * arithmetic on it, so a huge n cannot wrap the offset. */
static inline void *fs_malloc(size_t n)
{
    static uint8_t heap[1024 * 1024] __attribute__((aligned(16)));
    static size_t heap_off = 0;
    if (n > sizeof(heap) - heap_off) return (void *) 0; /* no wrap for huge n */
    void *p = &heap[heap_off];
    size_t step = (n + 15u) & ~(size_t) 15u; /* n <= 1 MB here: no wrap */
    heap_off = step > sizeof(heap) - heap_off ? sizeof(heap) : heap_off + step;
    return p;
}

static inline void *fs_calloc(size_t count, size_t size)
{
    if (size != 0 && count > (size_t) -1 / size) return (void *) 0; /* count * size overflows */
    void *p = fs_malloc(count * size);
    if (p) fs_memset(p, 0, count * size);
    return p;
}

#define assert(x) ((void)0)

/* No floating point. Kernel images are built with -mgeneral-regs-only (or an
 * integer-only riscv -march/-mabi), so float, double and _Complex do not
 * compile here. The libm-style stubs that used to live here (fs_sqrt, fs_exp,
 * fs_log, fs_pow, fs_sin, fs_cos, fs_cabs, fs_cexp, fs_fabs, fs_atan2) are
 * gone; use the integer fixed-point helpers in zxv_fixed.h instead
 * (fx_isqrt64, fx_sincos_turn, zxv_cq16_t, fx_udiv64). */
#include "zxv_fixed.h"

/* Minimal snprintf for freestanding mode — supports %s, %d, %u, %x, %c */
/* Bounded formatter.
 *
 * THREE BUGS WERE FIXED HERE, all of them buffer overflows in a helper the
 * whole kernel formats through:
 *
 *  1. `case '%'` and the `default:` case wrote one and TWO bytes respectively
 *     with NO bounds check, so an unrecognised conversion could write past
 *     the caller's buffer. This is reachable from any caller that uses a
 *     conversion this function does not implement.
 *  2. The '-' sign in %d was emitted without a bounds check.
 *  3. `max == 0` made the loop condition `pos < max - 1` compare against
 *     SIZE_MAX, so a zero-length buffer was treated as unbounded.
 *
 *  ...and one silent correctness bug: there was no 'l'/'ll' length modifier,
 *  so "%llu" fell through to `default:`, printed a literal "%l", left "lu" in
 *  the output, and — worst of all — never consumed the 64-bit argument, so
 *  EVERY SUBSEQUENT conversion in the same call read the wrong vararg.
 *
 * Returns the number of characters actually written (never >= max), and the
 * buffer is always NUL-terminated when max > 0.
 */
static inline int fs_snprintf(char *buf, size_t max, const char *fmt, ...) {
    va_list args;
    if (!buf || max == 0) return 0;
    if (!fmt) { buf[0] = '\0'; return 0; }
    va_start(args, fmt);
    size_t pos = 0;
/* NOTE: the guard means a side-effecting ARGUMENT would only be evaluated
 * while there is room. Never write FS_PUT(x[--t]) — the decrement would stop
 * happening the moment the buffer filled, and the enclosing loop would spin
 * forever. Callers below decrement first, then emit. */
#define FS_PUT(c) do { if (pos + 1 < max) buf[pos++] = (char)(c); } while (0)
    while (*fmt && pos + 1 < max) {
        if (*fmt == '%') {
            fmt++;
            /* length modifiers: h, hh, l, ll, z — parsed so the matching
             * va_arg type is used and the argument list stays aligned */
            int longness = 0;
            while (*fmt == 'l') { longness++; fmt++; }
            if (*fmt == 'z') { longness = 2; fmt++; }
            while (*fmt == 'h') { fmt++; }
            switch (*fmt) {
                case 's': {
                    const char *s = va_arg(args, const char*);
                    if (!s) s = "(null)";
                    while (*s && pos + 1 < max) buf[pos++] = *s++;
                    break;
                }
                case 'd': case 'i': {
                    long long v = (longness >= 2) ? va_arg(args, long long)
                                : (longness == 1) ? (long long)va_arg(args, long)
                                                  : (long long)va_arg(args, int);
                    unsigned long long uv;
                    if (v < 0) { FS_PUT('-'); uv = (unsigned long long)(-(v + 1)) + 1ull; }
                    else uv = (unsigned long long)v;
                    char tmp[24]; int t = 0;
                    if (uv == 0) tmp[t++] = '0';
                    while (uv > 0 && t < 24) { tmp[t++] = (char)('0' + (uv % 10)); uv /= 10; }
                    while (t > 0) { t--; FS_PUT(tmp[t]); }
                    break;
                }
                case 'u': {
                    unsigned long long v = (longness >= 2) ? va_arg(args, unsigned long long)
                                         : (longness == 1) ? (unsigned long long)va_arg(args, unsigned long)
                                                           : (unsigned long long)va_arg(args, unsigned);
                    char tmp[24]; int t = 0;
                    if (v == 0) tmp[t++] = '0';
                    while (v > 0 && t < 24) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
                    while (t > 0) { t--; FS_PUT(tmp[t]); }
                    break;
                }
                case 'x': case 'X': case 'p': {
                    unsigned long long v = (*fmt == 'p') ? (unsigned long long)(uintptr_t)va_arg(args, void*)
                                         : (longness >= 2) ? va_arg(args, unsigned long long)
                                         : (longness == 1) ? (unsigned long long)va_arg(args, unsigned long)
                                                           : (unsigned long long)va_arg(args, unsigned);
                    char tmp[24]; int t = 0;
                    int upper = (*fmt == 'X');
                    if (v == 0) tmp[t++] = '0';
                    while (v > 0 && t < 24) {
                        int d = (int)(v % 16);
                        tmp[t++] = (char)(d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10);
                        v /= 16;
                    }
                    while (t > 0) { t--; FS_PUT(tmp[t]); }
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    FS_PUT(c);
                    break;
                }
                case '%': FS_PUT('%'); break;
                case '\0': FS_PUT('%'); goto done;
                default:  FS_PUT('%'); FS_PUT(*fmt); break;
            }
        } else {
            FS_PUT(*fmt);
        }
        fmt++;
    }
done:
#undef FS_PUT
    buf[pos] = '\0';
    va_end(args);
    return (int)pos;
}

/* Stub printf for freestanding builds. Real console logging is arch-specific. */
static inline int fs_printf(const char *fmt, ...) {
    (void)fmt;
    return 0;
}
#define printf fs_printf
#define snprintf fs_snprintf

#endif
