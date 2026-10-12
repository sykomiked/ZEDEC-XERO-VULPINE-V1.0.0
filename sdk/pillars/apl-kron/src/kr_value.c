/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* kr_value.c — reference-counted, immutable array values, and the
 * caller-facing value calls. Integer code only (R11). */
#include <stdlib.h>
#include <string.h>

#include "kr_internal.h"

kr_value *kr_val_new(int kind, uint32_t rank, int64_t s0, int64_t s1) {
    kr_value *v;
    uint64_t n;
    if (rank > 2 || s0 < 0 || s1 < 0) return NULL;
    n = rank == 0 ? 1u : rank == 1 ? (uint64_t) s0 : (uint64_t) s0 * (uint64_t) s1;
    if (n > KR_MAX_ELEMENTS) return NULL;
    v = (kr_value *) calloc(1, sizeof *v);
    if (!v) return NULL;
    v->refs = 1;
    v->kind = (uint8_t) kind;
    v->rank = (uint8_t) rank;
    v->shape[0] = rank >= 1 ? s0 : 0;
    v->shape[1] = rank == 2 ? s1 : 0;
    v->n = n;
    if (kind == KR_KIND_TRUTH) {
        v->t = (int8_t *) calloc(n ? n : 1u, 1);
        if (!v->t) {
            free(v);
            return NULL;
        }
    } else {
        v->num = (int64_t *) calloc(n ? n : 1u, sizeof *v->num);
        v->den = (int64_t *) calloc(n ? n : 1u, sizeof *v->den);
        if (!v->num || !v->den) {
            free(v->num);
            free(v->den);
            free(v);
            return NULL;
        }
        for (uint64_t i = 0; i < n; i++) v->den[i] = 1;
    }
    return v;
}

kr_value *kr_val_ref(kr_value *v) {
    if (v) v->refs++;
    return v;
}

void kr_val_unref(kr_value *v) {
    if (!v || --v->refs > 0) return;
    free(v->num);
    free(v->den);
    free(v->t);
    free(v);
}

void kr_value_destroy(kr_value *v) { kr_val_unref(v); }

int32_t kr_value_info(const kr_value *v, int32_t *out_kind, uint32_t *out_rank, int64_t out_shape[2],
                      uint64_t *out_count) {
    if (!v) return ZXV_E_ARG;
    if (out_kind) *out_kind = v->kind;
    if (out_rank) *out_rank = v->rank;
    if (out_shape) {
        out_shape[0] = v->shape[0];
        out_shape[1] = v->shape[1];
    }
    if (out_count) *out_count = v->n;
    return ZXV_OK;
}

int32_t kr_value_rats(const kr_value *v, int64_t *num, int64_t *den, uint64_t cap) {
    if (!v || ((!num || !den) && v->n)) return ZXV_E_ARG;
    if (v->kind != KR_KIND_NUM) {
        kr_set_error("the value holds truth, not numbers");
        return KR_E_DOMAIN;
    }
    if (cap < v->n) return ZXV_E_BUFFER;
    if (v->n) {
        memcpy(num, v->num, v->n * sizeof *num);
        memcpy(den, v->den, v->n * sizeof *den);
    }
    return ZXV_OK;
}

int32_t kr_value_truths(const kr_value *v, int32_t *truth, uint64_t cap) {
    if (!v || (!truth && v->n)) return ZXV_E_ARG;
    if (v->kind != KR_KIND_TRUTH) {
        kr_set_error("the value holds numbers, not truth");
        return KR_E_DOMAIN;
    }
    if (cap < v->n) return ZXV_E_BUFFER;
    for (uint64_t i = 0; i < v->n; i++) truth[i] = v->t[i];
    return ZXV_OK;
}

int32_t kr_value_make(int32_t kind, uint32_t rank, const int64_t shape[2], const int64_t *num,
                      const int64_t *den, const int32_t *truth, kr_value **out_value) {
    kr_value *v;
    int64_t s0 = 0, s1 = 0;
    if (!out_value) return ZXV_E_ARG;
    *out_value = NULL;
    if ((kind != KR_KIND_NUM && kind != KR_KIND_TRUTH) || rank > 2 || (rank && !shape)) return ZXV_E_ARG;
    if (rank >= 1) s0 = shape[0];
    if (rank == 2) s1 = shape[1];
    if (s0 < 0 || s1 < 0) return ZXV_E_ARG;
    if (s0 > (int64_t) KR_MAX_ELEMENTS || s1 > (int64_t) KR_MAX_ELEMENTS ||
        (rank == 2 && (uint64_t) s0 * (uint64_t) s1 > KR_MAX_ELEMENTS) ||
        (rank == 1 && (uint64_t) s0 > KR_MAX_ELEMENTS)) {
        kr_set_error("the value is above the element limit");
        return KR_E_LIMIT;
    }
    v = kr_val_new(kind, rank, s0, s1);
    if (!v) return ZXV_E_NOMEM;
    if (v->n && ((kind == KR_KIND_NUM && (!num || !den)) || (kind == KR_KIND_TRUTH && !truth))) {
        kr_val_unref(v);
        return ZXV_E_ARG;
    }
    for (uint64_t i = 0; i < v->n; i++) {
        if (kind == KR_KIND_NUM) {
            int32_t st = zxv_rat_normalize(num[i], den[i], &v->num[i], &v->den[i]);
            if (st != ZXV_OK) {
                kr_set_errorf("element %llu (%lld/%lld) is not an exact rational: %s",
                              (unsigned long long) i, (long long) num[i], (long long) den[i],
                              zxv_status_name(st));
                kr_val_unref(v);
                return st;
            }
        } else {
            if (truth[i] < ZXV_LP_NEITHER || truth[i] > ZXV_LP_BOTH) {
                kr_set_errorf("element %llu is not a ZXV_LP_* code", (unsigned long long) i);
                kr_val_unref(v);
                return ZXV_E_ARG;
            }
            v->t[i] = (int8_t) truth[i];
        }
    }
    *out_value = v;
    return ZXV_OK;
}
