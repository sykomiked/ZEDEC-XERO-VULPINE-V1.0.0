/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * zxvj_common.h - helpers shared by the JNI shim's two files (zxv_jni.c for
 * ABI 1.0-1.3, zxv_jni15.c for the ABI 1.5 groups). The rules at the top of
 * zxv_jni.c apply to both files.
 */
#ifndef ZXVJ_COMMON_H
#define ZXVJ_COMMON_H

#include <jni.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_api.h"

/* Bumped when a native method is added, removed or changes signature; the
 * Kotlin side refuses a shim whose number differs from the one it expects. */
#define ZXVJ_SHIM_ABI 2

#define H(type, j) ((type *) (intptr_t) (j))
#define CH(type, j) ((const type *) (intptr_t) (j))

static inline int u8_ok(jint v) { return v >= 0 && v <= 255; }
static inline int u32_ok(jlong v) { return v >= 0 && v <= (jlong) UINT32_MAX; }

/* One byte kept for empty buffers: libzxv wants a non-NULL pointer even
 * when the length is 0. */
static uint8_t zxvj_empty[1];

/* A heap copy of a Java byte[] (NULL array -> NULL, *ok = 1). On allocation
 * failure *ok = 0. The copy is wiped and freed by bytes_free. */
static inline uint8_t *bytes_in(JNIEnv *env, jbyteArray a, size_t *len, int *ok)
{
    *ok = 1;
    *len = 0;
    if (!a) return NULL;
    jsize n = (*env)->GetArrayLength(env, a);
    if (n == 0) return zxvj_empty;
    uint8_t *p = (uint8_t *) malloc((size_t) n);
    if (!p) {
        *ok = 0;
        return NULL;
    }
    (*env)->GetByteArrayRegion(env, a, 0, n, (jbyte *) p);
    *len = (size_t) n;
    return p;
}

static inline void bytes_free(uint8_t *p, size_t len)
{
    if (!p || p == zxvj_empty) return;
    zxv_secure_zero(p, len);
    free(p);
}

/* A NUL-terminated C string from a byte[] that Kotlin already terminated.
 * Refuses an array without the terminating NUL. */
static inline char *cstr_in(JNIEnv *env, jbyteArray a, int *ok)
{
    size_t n;
    uint8_t *p = bytes_in(env, a, &n, ok);
    if (!p) return NULL;
    if (n == 0 || p[n - 1] != 0) {
        bytes_free(p, n);
        *ok = 0;
        return NULL;
    }
    return (char *) p;
}

static inline void cstr_free(char *s)
{
    if (s && (uint8_t *) s != zxvj_empty) {
        size_t n = strlen(s) + 1;
        zxv_secure_zero(s, n);
        free(s);
    }
}

static inline int len_is(JNIEnv *env, jarray a, jsize n) { return a && (*env)->GetArrayLength(env, a) == n; }
static inline int len_at_least(JNIEnv *env, jarray a, jsize n) { return a && (*env)->GetArrayLength(env, a) >= n; }

static inline void put_long(JNIEnv *env, jlongArray a, jsize i, jlong v) { (*env)->SetLongArrayRegion(env, a, i, 1, &v); }
static inline void put_int(JNIEnv *env, jintArray a, jsize i, jint v) { (*env)->SetIntArrayRegion(env, a, i, 1, &v); }

/* The R4 two-call pattern, one call of it: buf == NULL asks for the size. */
#define TEXT_CALL(env, buf, outLen, CALL)                                                  \
    do {                                                                                   \
        if (!len_at_least(env, outLen, 1)) return ZXV_E_ARG;                              \
        size_t need_ = 0;                                                                  \
        int32_t rc_;                                                                       \
        if (!(buf)) {                                                                      \
            char *b_ = NULL;                                                               \
            size_t c_ = 0;                                                                 \
            rc_ = CALL;                                                                    \
        } else {                                                                           \
            jsize n_ = (*env)->GetArrayLength(env, buf);                                   \
            char *b_ = (char *) malloc(n_ > 0 ? (size_t) n_ : 1);                          \
            size_t c_ = (size_t) n_;                                                       \
            if (!b_) return ZXV_E_NOMEM;                                                   \
            rc_ = CALL;                                                                    \
            if (rc_ == ZXV_OK) (*env)->SetByteArrayRegion(env, buf, 0, n_, (jbyte *) b_);  \
            zxv_secure_zero(b_, n_ > 0 ? (size_t) n_ : 1);                                 \
            free(b_);                                                                      \
        }                                                                                  \
        if (rc_ == ZXV_OK || rc_ == ZXV_E_BUFFER) put_long(env, outLen, 0, (jlong) need_); \
        return rc_;                                                                        \
    } while (0)

#endif /* ZXVJ_COMMON_H */
