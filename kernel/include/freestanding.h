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

static inline char *fs_strcpy(char *dst, const char *src) {
    size_t i = 0;
    while (src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
    return dst;
}

/* Simple bump allocator */
static inline void *fs_malloc(size_t n) {
    static uint8_t heap[1024*1024];
    static size_t heap_off = 0;
    if (heap_off + n > sizeof(heap)) return (void*)0;
    void *p = &heap[heap_off];
    heap_off += n;
    return p;
}

static inline void *fs_calloc(size_t count, size_t size) {
    void *p = fs_malloc(count * size);
    if (p) fs_memset(p, 0, count * size);
    return p;
}

#define assert(x) ((void)0)

/* Math stubs — x87 FPU inline assembly for freestanding 32-bit */
#define M_PI 3.14159265358979323846

static inline double fs_sqrt(double x) {
    if (x <= 0.0) return 0.0;
    return __builtin_sqrt(x);
}

/* Simple exp using Taylor series — avoids libm dependency */
static inline double fs_exp(double x) {
    if (x > 700.0) x = 700.0;
    if (x < -700.0) return 0.0;
    double term = 1.0, sum = 1.0;
    for (int i = 1; i < 30; i++) {
        term *= x / (double)i;
        sum += term;
        if (term < 1e-15 && term > -1e-15) break;
    }
    return sum;
}

/* Simple log using Newton iteration */
static inline double fs_log(double x) {
    if (x <= 0.0) return -1e300;
    double y = x - 1.0;
    for (int i = 0; i < 20; i++) {
        double ey = fs_exp(y);
        y = y - (ey - x) / ey;
    }
    return y;
}

/* pow(x,y) = exp(y * log(x)) */
static inline double fs_pow(double x, double y) {
    if (x <= 0.0) return 0.0;
    return fs_exp(y * fs_log(x));
}

/* Simple sin/cos using Taylor series with argument reduction */
static inline double fs_cos(double x) {
    while (x > M_PI) x -= 2.0 * M_PI;
    while (x < -M_PI) x += 2.0 * M_PI;
    double term = 1.0, sum = 1.0;
    double x2 = x * x;
    for (int i = 1; i < 15; i++) {
        term *= -x2 / ((double)(2*i) * (double)(2*i - 1));
        sum += term;
        if (term < 1e-15 && term > -1e-15) break;
    }
    return sum;
}

static inline double fs_sin(double x) {
    while (x > M_PI) x -= 2.0 * M_PI;
    while (x < -M_PI) x += 2.0 * M_PI;
    double term = x, sum = x;
    double x2 = x * x;
    for (int i = 1; i < 15; i++) {
        term *= -x2 / ((double)(2*i) * (double)(2*i + 1));
        sum += term;
        if (term < 1e-15 && term > -1e-15) break;
    }
    return sum;
}

/* Provide cabs and cexp as real functions since __builtin_ may emit libm calls */
static inline double fs_cabs(double _Complex z) {
    double r = __builtin_creal(z);
    double i = __builtin_cimag(z);
    if (r < 0) r = -r;
    if (i < 0) i = -i;
    if (r > i) {
        double t = i / r;
        return r * fs_sqrt(1.0 + t * t);
    } else if (i > 0) {
        double t = r / i;
        return i * fs_sqrt(1.0 + t * t);
    }
    return 0.0;
}

static inline double _Complex fs_cexp(double _Complex z) {
    double r = __builtin_creal(z);
    double i = __builtin_cimag(z);
    double er = fs_exp(r);
    double ci = fs_cos(i);
    double si = fs_sin(i);
    return er * ci + er * si * (__extension__ 1.0iF);
}

static inline double fs_fabs(double x) {
    return x < 0 ? -x : x;
}

static inline double fs_atan2(double y, double x) {
    if (x == 0.0) {
        if (y > 0) return M_PI / 2.0;
        if (y < 0) return -M_PI / 2.0;
        return 0.0;
    }
    double z = y / x;
    double atan_z;
    if (fs_fabs(z) < 1.0) {
        double term = z, sum = z;
        double z2 = z * z;
        for (int i = 1; i < 20; i++) {
            term *= -z2 * (2.0 * i - 1.0) / (2.0 * i + 1.0);
            sum += term;
            if (fs_fabs(term) < 1e-15) break;
        }
        atan_z = sum;
    } else {
        double w = 1.0 / z;
        double term = w, sum = w;
        double w2 = w * w;
        for (int i = 1; i < 20; i++) {
            term *= -w2 * (2.0 * i - 1.0) / (2.0 * i + 1.0);
            sum += term;
            if (fs_fabs(term) < 1e-15) break;
        }
        atan_z = (z > 0 ? M_PI / 2.0 : -M_PI / 2.0) - sum;
    }
    if (x < 0) {
        if (y >= 0) atan_z += M_PI;
        else atan_z -= M_PI;
    }
    return atan_z;
}

/* Map standard math functions to freestanding implementations */
#define sqrt fs_sqrt
#define exp fs_exp
#define sin fs_sin
#define cos fs_cos
#define fabs fs_fabs
#define atan2 fs_atan2
/* cabs/cexp are provided by freestanding_stubs/complex.h */

/* Minimal snprintf for freestanding mode — supports %s, %d, %u, %x, %c */
static inline int fs_snprintf(char *buf, size_t max, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    size_t pos = 0;
    while (*fmt && pos < max - 1) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 's': {
                    const char *s = va_arg(args, const char*);
                    while (*s && pos < max - 1) buf[pos++] = *s++;
                    break;
                }
                case 'd': {
                    int v = va_arg(args, int);
                    if (v < 0) { buf[pos++] = '-'; v = -v; }
                    char tmp[16]; int t = 0;
                    if (v == 0) tmp[t++] = '0';
                    while (v > 0 && t < 16) { tmp[t++] = '0' + (v % 10); v /= 10; }
                    while (t > 0 && pos < max - 1) buf[pos++] = tmp[--t];
                    break;
                }
                case 'u': {
                    unsigned v = va_arg(args, unsigned);
                    char tmp[16]; int t = 0;
                    if (v == 0) tmp[t++] = '0';
                    while (v > 0 && t < 16) { tmp[t++] = '0' + (v % 10); v /= 10; }
                    while (t > 0 && pos < max - 1) buf[pos++] = tmp[--t];
                    break;
                }
                case 'x': {
                    unsigned v = va_arg(args, unsigned);
                    char tmp[16]; int t = 0;
                    if (v == 0) tmp[t++] = '0';
                    while (v > 0 && t < 16) { int d = v % 16; tmp[t++] = d < 10 ? '0' + d : 'a' + d - 10; v /= 16; }
                    while (t > 0 && pos < max - 1) buf[pos++] = tmp[--t];
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    if (pos < max - 1) buf[pos++] = c;
                    break;
                }
                case '%': buf[pos++] = '%'; break;
                default: buf[pos++] = '%'; buf[pos++] = *fmt; break;
            }
        } else {
            buf[pos++] = *fmt;
        }
        fmt++;
    }
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
