/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zf_runtime.c — the checks generated wrappers make: capability, integer
 * ranges, exact rational -> IEEE-754 bits, cstr copies, results. Integer
 * code only (R11): float values exist only in generated code. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zf_internal.h"

static _Thread_local char g_err[512];

void zf_set_errorf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
}

int32_t zf_last_error(char *buf, size_t cap, size_t *out_len) {
    size_t n = strlen(g_err);
    if (!out_len) return ZXV_E_ARG;
    *out_len = n;
    if (!buf || cap <= n) return ZXV_E_BUFFER;
    memcpy(buf, g_err, n + 1u);
    return ZXV_OK;
}

const char *zf_status_name(int32_t status) {
    switch (status) {
    case ZF_E_SPEC: return "ZF_E_SPEC";
    case ZF_E_RANGE: return "ZF_E_RANGE";
    case ZF_E_INEXACT: return "ZF_E_INEXACT";
    case ZF_E_RESULT: return "ZF_E_RESULT";
    default: return zxv_status_name(status);
    }
}

/* ===== rational -> IEEE-754 ===== */

static unsigned bitlen64(uint64_t v) {
    unsigned n = 0;
    while (v) {
        n++;
        v >>= 1;
    }
    return n;
}

/* num/den = (-1)^sign * m * 2^e with m odd (or 0). */
static int32_t decompose(int64_t num, int64_t den, int *sign, uint64_t *m, int32_t *e) {
    int64_t n = 0, d = 0;
    int32_t st;
    if (den == 0) return ZXV_E_ARG;
    st = zxv_rat_normalize(num, den, &n, &d);
    if (st != ZXV_OK) return st;
    if (n == 0) {
        *sign = 0;
        *m = 0;
        *e = 0;
        return ZXV_OK;
    }
    if (((uint64_t) d & ((uint64_t) d - 1u)) != 0) return ZF_E_INEXACT; /* den not 2^k */
    *sign = n < 0;
    *m = n < 0 ? (uint64_t) 0 - (uint64_t) n : (uint64_t) n;
    *e = -(int32_t) (bitlen64((uint64_t) d) - 1u);
    while ((*m & 1u) == 0) {
        *m >>= 1;
        (*e)++;
    }
    return ZXV_OK;
}

/* Encode m * 2^e (m odd, nonzero) with `mant` fraction bits and exponent
 * bias `bias`. |num|, den < 2^63 keep the exponent far inside the normal
 * range of both formats, so no subnormal or overflow case can arise; the
 * check is kept anyway. */
static int32_t encode(int sign, uint64_t m, int32_t e, unsigned mant, int32_t bias, int32_t emax,
                      unsigned total, uint64_t *out) {
    unsigned L = bitlen64(m);
    int32_t E = e + (int32_t) L - 1;
    if (L > mant + 1u) return ZF_E_INEXACT;
    if (E > emax || E < 1 - bias) return ZF_E_INEXACT;
    uint64_t frac = (m << (mant + 1u - L)) & (((uint64_t) 1 << mant) - 1u);
    *out = ((uint64_t) sign << (total - 1u)) | ((uint64_t) (E + bias) << mant) | frac;
    return ZXV_OK;
}

int32_t zf_rat_to_f64_bits(int64_t num, int64_t den, uint64_t *out_bits) {
    int sign = 0;
    uint64_t m = 0, b = 0;
    int32_t e = 0, st;
    if (!out_bits) return ZXV_E_ARG;
    st = decompose(num, den, &sign, &m, &e);
    if (st != ZXV_OK) return st;
    if (m == 0) {
        *out_bits = 0;
        return ZXV_OK;
    }
    st = encode(sign, m, e, 52u, 1023, 1023, 64u, &b);
    if (st == ZXV_OK) *out_bits = b;
    return st;
}

int32_t zf_rat_to_f32_bits(int64_t num, int64_t den, uint32_t *out_bits) {
    int sign = 0;
    uint64_t m = 0, b = 0;
    int32_t e = 0, st;
    if (!out_bits) return ZXV_E_ARG;
    st = decompose(num, den, &sign, &m, &e);
    if (st != ZXV_OK) return st;
    if (m == 0) {
        *out_bits = 0;
        return ZXV_OK;
    }
    st = encode(sign, m, e, 23u, 127, 127, 32u, &b);
    if (st == ZXV_OK) *out_bits = (uint32_t) b;
    return st;
}

/* ===== wrapper runtime ===== */

int32_t zf_enter(const zxv_cap *cap, uint64_t needs, const char *fn) {
    int32_t st;
    if (!cap) {
        zf_set_errorf("%s: no capability (every call needs one)", fn ? fn : "?");
        return ZXV_E_CAP;
    }
    st = zxv_cap_check(cap, needs);
    if (st != ZXV_OK) {
        zf_set_errorf("%s: the capability does not allow this call (needs 0x%llx, live and "
                      "attested TRUE)", fn ? fn : "?", (unsigned long long) needs);
        return st;
    }
    return ZXV_OK;
}

int32_t zf_null_out(const char *fn) {
    zf_set_errorf("%s: NULL output pointer", fn ? fn : "?");
    return ZXV_E_ARG;
}

int32_t zf_arg_i64(const char *fn, uint32_t index, const char *name, int64_t value, int64_t lo,
                   int64_t hi) {
    if (value < lo) {
        zf_set_errorf("%s: argument %u '%s' = %lld is below %lld", fn, (unsigned) index, name,
                      (long long) value, (long long) lo);
        return ZF_E_RANGE;
    }
    if (value > hi) {
        zf_set_errorf("%s: argument %u '%s' = %lld is above %lld", fn, (unsigned) index, name,
                      (long long) value, (long long) hi);
        return ZF_E_RANGE;
    }
    return ZXV_OK;
}

int32_t zf_arg_u64(const char *fn, uint32_t index, const char *name, uint64_t value, uint64_t lo,
                   uint64_t hi) {
    if (value < lo) {
        zf_set_errorf("%s: argument %u '%s' = %llu is below %llu", fn, (unsigned) index, name,
                      (unsigned long long) value, (unsigned long long) lo);
        return ZF_E_RANGE;
    }
    if (value > hi) {
        zf_set_errorf("%s: argument %u '%s' = %llu is above %llu", fn, (unsigned) index, name,
                      (unsigned long long) value, (unsigned long long) hi);
        return ZF_E_RANGE;
    }
    return ZXV_OK;
}

static int32_t float_arg_status(const char *fn, uint32_t index, const char *name, int64_t num,
                                int64_t den, int32_t st, const char *type) {
    if (st == ZF_E_INEXACT)
        zf_set_errorf("%s: argument %u '%s' = %lld/%lld has no exact %s value (it would be "
                      "rounded)", fn, (unsigned) index, name, (long long) num, (long long) den,
                      type);
    else if (st != ZXV_OK)
        zf_set_errorf("%s: argument %u '%s' = %lld/%lld is not a valid rational", fn,
                      (unsigned) index, name, (long long) num, (long long) den);
    return st;
}

int32_t zf_arg_f64(const char *fn, uint32_t index, const char *name, int64_t num, int64_t den,
                   uint64_t *out_bits) {
    if (!out_bits) return zf_null_out(fn);
    return float_arg_status(fn, index, name, num, den, zf_rat_to_f64_bits(num, den, out_bits),
                            "binary64 (double)");
}

int32_t zf_arg_f32(const char *fn, uint32_t index, const char *name, int64_t num, int64_t den,
                   uint32_t *out_bits) {
    if (!out_bits) return zf_null_out(fn);
    return float_arg_status(fn, index, name, num, den, zf_rat_to_f32_bits(num, den, out_bits),
                            "binary32 (float)");
}

int32_t zf_arg_cstr(const char *fn, uint32_t index, const char *name, const char *s, size_t len,
                    size_t lo, size_t hi, char **out_copy) {
    char *c;
    if (!out_copy) return zf_null_out(fn);
    *out_copy = NULL;
    if (!s && len) {
        zf_set_errorf("%s: argument %u '%s' is NULL with length %zu", fn, (unsigned) index, name,
                      len);
        return ZXV_E_ARG;
    }
    if (len < lo || len > hi) {
        zf_set_errorf("%s: argument %u '%s' has %zu bytes, outside %zu..%zu", fn, (unsigned) index,
                      name, len, lo, hi);
        return ZF_E_RANGE;
    }
    if (len && memchr(s, 0, len)) {
        zf_set_errorf("%s: argument %u '%s' contains a NUL byte (C would see a shorter string)",
                      fn, (unsigned) index, name);
        return ZF_E_RANGE;
    }
    c = (char *) malloc(len + 1u);
    if (!c) return ZXV_E_NOMEM;
    if (len) memcpy(c, s, len);
    c[len] = '\0';
    *out_copy = c;
    return ZXV_OK;
}

void zf_cstr_free(char *copy) { free(copy); }

int32_t zf_res_i64(const char *fn, int64_t value, int64_t lo, int64_t hi, int64_t *out) {
    if (!out) return zf_null_out(fn);
    if (value < lo || value > hi) {
        zf_set_errorf("%s: result %lld is outside %lld..%lld", fn, (long long) value,
                      (long long) lo, (long long) hi);
        return ZF_E_RESULT;
    }
    *out = value;
    return ZXV_OK;
}

int32_t zf_res_u64(const char *fn, uint64_t value, uint64_t lo, uint64_t hi, uint64_t *out) {
    if (!out) return zf_null_out(fn);
    if (value < lo || value > hi) {
        zf_set_errorf("%s: result %llu is outside %llu..%llu", fn, (unsigned long long) value,
                      (unsigned long long) lo, (unsigned long long) hi);
        return ZF_E_RESULT;
    }
    *out = value;
    return ZXV_OK;
}

int32_t zf_res_f64(const char *fn, uint64_t bits, int64_t *out_num, int64_t *out_den) {
    int64_t n = 0, d = 1;
    int32_t st;
    if (!out_num || !out_den) return zf_null_out(fn);
    st = zxv_rat_from_ieee754(bits, &n, &d);
    if (st == ZXV_E_FLOAT) {
        zf_set_errorf("%s: the result is %s (bits 0x%016llx), which is not a number", fn,
                      (bits & UINT64_C(0x000FFFFFFFFFFFFF)) ? "NaN" : "an infinity",
                      (unsigned long long) bits);
        return st;
    }
    if (st != ZXV_OK) {
        zf_set_errorf("%s: the result (bits 0x%016llx) has no int64 rational form", fn,
                      (unsigned long long) bits);
        return st;
    }
    *out_num = n;
    *out_den = d;
    return ZXV_OK;
}
