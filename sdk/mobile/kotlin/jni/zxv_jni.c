/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * zxv_jni.c - the JNI shim between the Kotlin SDK (org.zedec.zxv.mobile) and
 * libzxv. Android has no Java FFM API, so this small C file stands in for it.
 *
 * Rules this file keeps (sdk/docs/ABI.md, guides/writing-a-binding.md):
 *
 *  - It is mechanical. Every function forwards to exactly one zxv_* call and
 *    returns its int32 status unchanged. No policy lives here: capability,
 *    error and lifetime rules are in Kotlin, where they can be tested.
 *  - Handles cross as jlong (intptr_t). The Kotlin side owns them and calls
 *    the matching destroy exactly once, children before parents (R6).
 *  - Narrow unsigned arguments (uint8_t tags, phases, minor units) arrive as
 *    jint and are range-checked here before the cast, so a negative or
 *    oversized value never wraps into a valid one. uint32 values arrive as
 *    jlong and are range-checked the same way. A failed check returns
 *    ZXV_E_ARG; the Kotlin side checks first, so that path is a backstop.
 *  - Strings arrive as NUL-terminated UTF-8 byte[] (Kotlin encodes them), not
 *    as jstring: JNI's GetStringUTFChars yields *modified* UTF-8, which is not
 *    what libzxv validates (R5).
 *  - Java arrays are copied in and out with Get/Set<Type>ArrayRegion. No
 *    array is pinned across a libzxv call, no local reference outlives the
 *    call, and no global reference is ever created, so there is nothing to
 *    leak and no callback can reach the JVM from another thread.
 *  - Byte buffers copied from the Java heap into C memory are wiped with
 *    zxv_secure_zero before free (they may hold messages or key material).
 *  - Out parameters are copied back only on ZXV_OK, except the R4 out_len on
 *    ZXV_E_BUFFER, which the two-call pattern needs.
 */
#include <jni.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "zxv_api.h"

/* Bumped when a native method is added, removed or changes signature; the
 * Kotlin side refuses a shim whose number differs from the one it expects. */
#define ZXVJ_SHIM_ABI 1

#define FN(name) Java_org_zedec_zxv_mobile_internal_Jni_##name
#define H(type, j) ((type *) (intptr_t) (j))
#define CH(type, j) ((const type *) (intptr_t) (j))

static int u8_ok(jint v) { return v >= 0 && v <= 255; }
static int u32_ok(jlong v) { return v >= 0 && v <= (jlong) UINT32_MAX; }

/* One byte kept for empty buffers: libzxv wants a non-NULL pointer even
 * when the length is 0. */
static uint8_t zxvj_empty[1];

/* A heap copy of a Java byte[] (NULL array -> NULL, *ok = 1). On allocation
 * failure *ok = 0. The copy is wiped and freed by bytes_free. */
static uint8_t *bytes_in(JNIEnv *env, jbyteArray a, size_t *len, int *ok)
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

static void bytes_free(uint8_t *p, size_t len)
{
    if (!p || p == zxvj_empty) return;
    zxv_secure_zero(p, len);
    free(p);
}

/* A NUL-terminated C string from a byte[] that Kotlin already terminated.
 * Refuses an array without the terminating NUL. */
static char *cstr_in(JNIEnv *env, jbyteArray a, int *ok)
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

static void cstr_free(char *s)
{
    if (s && (uint8_t *) s != zxvj_empty) {
        size_t n = strlen(s) + 1;
        zxv_secure_zero(s, n);
        free(s);
    }
}

static int len_is(JNIEnv *env, jarray a, jsize n) { return a && (*env)->GetArrayLength(env, a) == n; }
static int len_at_least(JNIEnv *env, jarray a, jsize n) { return a && (*env)->GetArrayLength(env, a) >= n; }

static void put_long(JNIEnv *env, jlongArray a, jsize i, jlong v) { (*env)->SetLongArrayRegion(env, a, i, 1, &v); }
static void put_int(JNIEnv *env, jintArray a, jsize i, jint v) { (*env)->SetIntArrayRegion(env, a, i, 1, &v); }

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

/* ===== library ===== */

JNIEXPORT jint JNICALL FN(shimAbi)(JNIEnv *env, jclass cls)
{
    (void) env;
    (void) cls;
    return ZXVJ_SHIM_ABI;
}

JNIEXPORT jint JNICALL FN(version)(JNIEnv *env, jclass cls, jintArray out)
{
    (void) cls;
    if (!len_is(env, out, 3)) return ZXV_E_ARG;
    uint32_t ma, mi, pa;
    int32_t rc = zxv_version(&ma, &mi, &pa);
    if (rc == ZXV_OK) {
        jint v[3] = {(jint) ma, (jint) mi, (jint) pa};
        (*env)->SetIntArrayRegion(env, out, 0, 3, v);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN(features)(JNIEnv *env, jclass cls, jlongArray out)
{
    (void) cls;
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    uint32_t m;
    int32_t rc = zxv_features(&m);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) m);
    return rc;
}

/* Static ASCII from libzxv, so NewStringUTF is exact here. */
JNIEXPORT jstring JNICALL FN(statusName)(JNIEnv *env, jclass cls, jint status)
{
    (void) cls;
    const char *s = zxv_status_name((int32_t) status);
    return (*env)->NewStringUTF(env, s ? s : "ZXV_E_UNKNOWN");
}

JNIEXPORT jint JNICALL FN(statusClass)(JNIEnv *env, jclass cls, jint status)
{
    (void) env;
    (void) cls;
    return zxv_status_class((int32_t) status);
}

JNIEXPORT jint JNICALL FN(lastError)(JNIEnv *env, jclass cls, jbyteArray buf, jlongArray outLen)
{
    (void) cls;
    TEXT_CALL(env, buf, outLen, zxv_last_error(b_, c_, &need_));
}

/* ===== context ===== */

JNIEXPORT jint JNICALL FN(ctxCreate)(JNIEnv *env, jclass cls, jlongArray out)
{
    (void) cls;
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    zxv_ctx *c = NULL;
    int32_t rc = zxv_ctx_create(&c);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) (intptr_t) c);
    return rc;
}

JNIEXPORT void JNICALL FN(ctxDestroy)(JNIEnv *env, jclass cls, jlong ctx)
{
    (void) env;
    (void) cls;
    zxv_ctx_destroy(H(zxv_ctx, ctx));
}

JNIEXPORT jint JNICALL FN(ctxIsPoisoned)(JNIEnv *env, jclass cls, jlong ctx, jintArray out)
{
    (void) cls;
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    int32_t p = 0;
    int32_t rc = zxv_ctx_is_poisoned(H(zxv_ctx, ctx), &p);
    if (rc == ZXV_OK) put_int(env, out, 0, p);
    return rc;
}

JNIEXPORT jint JNICALL FN(ctxHalt)(JNIEnv *env, jclass cls, jlong ctx, jbyteArray reason)
{
    (void) cls;
    int ok;
    char *r = cstr_in(env, reason, &ok);
    if (!ok) return ZXV_E_ARG;
    int32_t rc = zxv_ctx_halt(H(zxv_ctx, ctx), r);
    cstr_free(r);
    return rc;
}

/* ===== capabilities ===== */

JNIEXPORT jint JNICALL FN(capCreate)(JNIEnv *env, jclass cls, jlong ctx, jlong pid, jbyteArray identity,
                                     jlong caps, jlongArray out)
{
    (void) cls;
    if (!u32_ok(pid) || !len_is(env, out, 1)) return ZXV_E_ARG;
    if (identity && !len_is(env, identity, ZXV_IDENTITY_BYTES)) return ZXV_E_ARG;
    uint8_t id[ZXV_IDENTITY_BYTES];
    if (identity) (*env)->GetByteArrayRegion(env, identity, 0, ZXV_IDENTITY_BYTES, (jbyte *) id);
    zxv_cap *c = NULL;
    int32_t rc = zxv_cap_create(H(zxv_ctx, ctx), (uint32_t) pid, identity ? id : NULL, (uint64_t) caps, &c);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) (intptr_t) c);
    return rc;
}

JNIEXPORT jint JNICALL FN(capAttenuate)(JNIEnv *env, jclass cls, jlong cap, jlong keep, jlongArray out)
{
    (void) cls;
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    zxv_cap *c = NULL;
    int32_t rc = zxv_cap_attenuate(CH(zxv_cap, cap), (uint64_t) keep, &c);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) (intptr_t) c);
    return rc;
}

JNIEXPORT jint JNICALL FN(capRevoke)(JNIEnv *env, jclass cls, jlong cap)
{
    (void) env;
    (void) cls;
    return zxv_cap_revoke(H(zxv_cap, cap));
}

JNIEXPORT jint JNICALL FN(capSetAttestation)(JNIEnv *env, jclass cls, jlong cap, jint lp)
{
    (void) env;
    (void) cls;
    return zxv_cap_set_attestation(H(zxv_cap, cap), (int32_t) lp);
}

JNIEXPORT jint JNICALL FN(capInfo)(JNIEnv *env, jclass cls, jlong cap, jlongArray out)
{
    (void) cls;
    if (!len_is(env, out, 4)) return ZXV_E_ARG;
    uint64_t caps;
    uint32_t pid;
    int32_t att, rev;
    int32_t rc = zxv_cap_info(CH(zxv_cap, cap), &caps, &pid, &att, &rev);
    if (rc == ZXV_OK) {
        jlong v[4] = {(jlong) caps, (jlong) pid, (jlong) att, (jlong) rev};
        (*env)->SetLongArrayRegion(env, out, 0, 4, v);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN(capCheck)(JNIEnv *env, jclass cls, jlong cap, jlong needed)
{
    (void) env;
    (void) cls;
    return zxv_cap_check(CH(zxv_cap, cap), (uint64_t) needed);
}

JNIEXPORT void JNICALL FN(capDestroy)(JNIEnv *env, jclass cls, jlong cap)
{
    (void) env;
    (void) cls;
    zxv_cap_destroy(H(zxv_cap, cap));
}

/* ===== currencies and ledger (the *_cap forms only, R10) ===== */

JNIEXPORT jint JNICALL FN(ccyEnableCap)(JNIEnv *env, jclass cls, jlong cap, jbyteArray alpha)
{
    (void) cls;
    int ok;
    char *a = cstr_in(env, alpha, &ok);
    if (!ok || !a) return ZXV_E_ARG;
    int32_t rc = zxv_ccy_enable_cap(CH(zxv_cap, cap), a);
    cstr_free(a);
    return rc;
}

JNIEXPORT jint JNICALL FN(ccyEnableWithMinorCap)(JNIEnv *env, jclass cls, jlong cap, jbyteArray alpha, jint minor)
{
    (void) cls;
    if (!u8_ok(minor)) return ZXV_E_ARG;
    int ok;
    char *a = cstr_in(env, alpha, &ok);
    if (!ok || !a) return ZXV_E_ARG;
    int32_t rc = zxv_ccy_enable_with_minor_cap(CH(zxv_cap, cap), a, (uint8_t) minor);
    cstr_free(a);
    return rc;
}

JNIEXPORT jint JNICALL FN(ccyRegisterPrivateCap)(JNIEnv *env, jclass cls, jlong cap, jbyteArray code, jint minor)
{
    (void) cls;
    if (!u8_ok(minor)) return ZXV_E_ARG;
    int ok;
    char *a = cstr_in(env, code, &ok);
    if (!ok || !a) return ZXV_E_ARG;
    int32_t rc = zxv_ccy_register_private_cap(CH(zxv_cap, cap), a, (uint8_t) minor);
    cstr_free(a);
    return rc;
}

JNIEXPORT jint JNICALL FN(ledgerOpenAccountCap)(JNIEnv *env, jclass cls, jlong cap, jbyteArray name,
                                                jbyteArray ccy, jint kind, jlongArray out)
{
    (void) cls;
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    int ok1, ok2;
    char *n = cstr_in(env, name, &ok1);
    char *c = cstr_in(env, ccy, &ok2);
    int32_t rc = ZXV_E_ARG;
    uint32_t id = 0;
    if (ok1 && ok2 && n && c) rc = zxv_ledger_open_account_cap(CH(zxv_cap, cap), n, c, (int32_t) kind, &id);
    cstr_free(n);
    cstr_free(c);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) id);
    return rc;
}

JNIEXPORT jint JNICALL FN(ledgerPostCap)(JNIEnv *env, jclass cls, jlong cap, jlong from, jlong to, jlong amount,
                                         jbyteArray ccy, jbyteArray ref, jlongArray out)
{
    (void) cls;
    if (!u32_ok(from) || !u32_ok(to) || !len_is(env, out, 1)) return ZXV_E_ARG;
    int ok1, ok2;
    char *c = cstr_in(env, ccy, &ok1);
    char *r = cstr_in(env, ref, &ok2);
    int32_t rc = ZXV_E_ARG;
    uint64_t id = 0;
    if (ok1 && ok2 && c && r)
        rc = zxv_ledger_post_cap(CH(zxv_cap, cap), (uint32_t) from, (uint32_t) to, (int64_t) amount, c, r, &id);
    cstr_free(c);
    cstr_free(r);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) id);
    return rc;
}

JNIEXPORT jint JNICALL FN(ledgerBalanceCap)(JNIEnv *env, jclass cls, jlong cap, jlong account, jlongArray out)
{
    (void) cls;
    if (!u32_ok(account) || !len_is(env, out, 3)) return ZXV_E_ARG;
    int64_t d, c, e;
    int32_t rc = zxv_ledger_balance_cap(CH(zxv_cap, cap), (uint32_t) account, &d, &c, &e);
    if (rc == ZXV_OK) {
        jlong v[3] = {d, c, e};
        (*env)->SetLongArrayRegion(env, out, 0, 3, v);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN(ledgerCheckCap)(JNIEnv *env, jclass cls, jlong cap)
{
    (void) env;
    (void) cls;
    return zxv_ledger_check_cap(CH(zxv_cap, cap));
}

JNIEXPORT jint JNICALL FN(ledgerAccountCountCap)(JNIEnv *env, jclass cls, jlong cap, jlongArray out)
{
    (void) cls;
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    uint32_t n;
    int32_t rc = zxv_ledger_account_count_cap(CH(zxv_cap, cap), &n);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) n);
    return rc;
}

JNIEXPORT jint JNICALL FN(ledgerAccountNameCap)(JNIEnv *env, jclass cls, jlong cap, jlong account,
                                                jbyteArray buf, jlongArray outLen)
{
    (void) cls;
    if (!u32_ok(account)) return ZXV_E_ARG;
    TEXT_CALL(env, buf, outLen, zxv_ledger_account_name_cap(CH(zxv_cap, cap), (uint32_t) account, b_, c_, &need_));
}

JNIEXPORT jint JNICALL FN(ledgerAccountCcyCap)(JNIEnv *env, jclass cls, jlong cap, jlong account,
                                               jbyteArray buf, jlongArray outLen)
{
    (void) cls;
    if (!u32_ok(account)) return ZXV_E_ARG;
    TEXT_CALL(env, buf, outLen, zxv_ledger_account_ccy_cap(CH(zxv_cap, cap), (uint32_t) account, b_, c_, &need_));
}

JNIEXPORT jint JNICALL FN(ledgerAccountKindCap)(JNIEnv *env, jclass cls, jlong cap, jlong account, jintArray out)
{
    (void) cls;
    if (!u32_ok(account) || !len_is(env, out, 1)) return ZXV_E_ARG;
    int32_t k;
    int32_t rc = zxv_ledger_account_kind_cap(CH(zxv_cap, cap), (uint32_t) account, &k);
    if (rc == ZXV_OK) put_int(env, out, 0, k);
    return rc;
}

JNIEXPORT jint JNICALL FN(ledgerEntryCountCap)(JNIEnv *env, jclass cls, jlong cap, jlong account, jlongArray out)
{
    (void) cls;
    if (!u32_ok(account) || !len_is(env, out, 1)) return ZXV_E_ARG;
    uint32_t n;
    int32_t rc = zxv_ledger_entry_count_cap(CH(zxv_cap, cap), (uint32_t) account, &n);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) n);
    return rc;
}

/* out = {entry id, signed minor, counterparty}; refBuf/outLen follow R4. */
JNIEXPORT jint JNICALL FN(ledgerEntryAtCap)(JNIEnv *env, jclass cls, jlong cap, jlong account, jlong index,
                                            jlongArray out, jbyteArray refBuf, jlongArray outLen)
{
    (void) cls;
    if (!u32_ok(account) || !u32_ok(index) || !len_is(env, out, 3)) return ZXV_E_ARG;
    uint64_t id = 0;
    int64_t amt = 0;
    uint32_t cp = 0;
    int32_t rcAll = ZXV_OK;
#define ENTRY_CALL zxv_ledger_entry_at_cap(CH(zxv_cap, cap), (uint32_t) account, (uint32_t) index, &id, &amt, &cp, b_, c_, &need_)
    /* TEXT_CALL returns; wrap it in a helper block to copy the scalars first. */
    if (!len_at_least(env, outLen, 1)) return ZXV_E_ARG;
    {
        size_t need_ = 0;
        if (!refBuf) {
            char *b_ = NULL;
            size_t c_ = 0;
            rcAll = ENTRY_CALL;
        } else {
            jsize n_ = (*env)->GetArrayLength(env, refBuf);
            char *b_ = (char *) malloc(n_ > 0 ? (size_t) n_ : 1);
            size_t c_ = (size_t) n_;
            if (!b_) return ZXV_E_NOMEM;
            rcAll = ENTRY_CALL;
            if (rcAll == ZXV_OK) (*env)->SetByteArrayRegion(env, refBuf, 0, n_, (jbyte *) b_);
            free(b_);
        }
        if (rcAll == ZXV_OK || rcAll == ZXV_E_BUFFER) put_long(env, outLen, 0, (jlong) need_);
    }
#undef ENTRY_CALL
    if (rcAll == ZXV_OK) {
        jlong v[3] = {(jlong) id, (jlong) amt, (jlong) cp};
        (*env)->SetLongArrayRegion(env, out, 0, 3, v);
    }
    return rcAll;
}

/* ===== exact rationals (pure) =====
 * Each call stores its status before the out values are read: C leaves the
 * order of argument evaluation unspecified, so `f(g(&x), x)` may read x
 * before g writes it. */

static jint rat_out(JNIEnv *env, jlongArray out, int32_t rc, int64_t n, int64_t d)
{
    if (rc == ZXV_OK) {
        jlong v[2] = {n, d};
        (*env)->SetLongArrayRegion(env, out, 0, 2, v);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN(ratNormalize)(JNIEnv *env, jclass cls, jlong n, jlong d, jlongArray out)
{
    (void) cls;
    if (!len_is(env, out, 2)) return ZXV_E_ARG;
    int64_t on = 0, od = 0;
    int32_t rc_ = zxv_rat_normalize(n, d, &on, &od);
    return rat_out(env, out, rc_, on, od);
}

#define RAT_BIN(name, cfn)                                                                          \
    JNIEXPORT jint JNICALL FN(name)(JNIEnv * env, jclass cls, jlong an, jlong ad, jlong bn, jlong bd, \
                                    jlongArray out)                                                 \
    {                                                                                               \
        (void) cls;                                                                                 \
        if (!len_is(env, out, 2)) return ZXV_E_ARG;                                                 \
        int64_t on = 0, od = 0;                                                                             \
        int32_t rc_ = cfn(an, ad, bn, bd, &on, &od);                                                \
        return rat_out(env, out, rc_, on, od);                                                      \
    }
RAT_BIN(ratAdd, zxv_rat_add)
RAT_BIN(ratSub, zxv_rat_sub)
RAT_BIN(ratMul, zxv_rat_mul)
RAT_BIN(ratDiv, zxv_rat_div)

JNIEXPORT jint JNICALL FN(ratCmp)(JNIEnv *env, jclass cls, jlong an, jlong ad, jlong bn, jlong bd, jintArray out)
{
    (void) cls;
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    int32_t c;
    int32_t rc = zxv_rat_cmp(an, ad, bn, bd, &c);
    if (rc == ZXV_OK) put_int(env, out, 0, c);
    return rc;
}

JNIEXPORT jint JNICALL FN(ratParse)(JNIEnv *env, jclass cls, jbyteArray text, jlongArray out)
{
    (void) cls;
    if (!len_is(env, out, 2)) return ZXV_E_ARG;
    int ok;
    char *t = cstr_in(env, text, &ok);
    if (!ok || !t) return ZXV_E_ARG;
    int64_t on = 0, od = 0;
    int32_t rc = zxv_rat_parse(t, &on, &od);
    cstr_free(t);
    return rat_out(env, out, rc, on, od);
}

JNIEXPORT jint JNICALL FN(ratFormat)(JNIEnv *env, jclass cls, jlong n, jlong d, jbyteArray buf, jlongArray outLen)
{
    (void) cls;
    TEXT_CALL(env, buf, outLen, zxv_rat_format(n, d, b_, c_, &need_));
}

JNIEXPORT jint JNICALL FN(ratFromIeee754)(JNIEnv *env, jclass cls, jlong bits, jlongArray out)
{
    (void) cls;
    if (!len_is(env, out, 2)) return ZXV_E_ARG;
    int64_t on = 0, od = 0;
    int32_t rc_ = zxv_rat_from_ieee754((uint64_t) bits, &on, &od);
    return rat_out(env, out, rc_, on, od);
}

JNIEXPORT jint JNICALL FN(ratToQ16)(JNIEnv *env, jclass cls, jlong n, jlong d, jintArray out)
{
    (void) cls;
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    int32_t q;
    int32_t rc = zxv_rat_to_q16(n, d, &q);
    if (rc == ZXV_OK) put_int(env, out, 0, q);
    return rc;
}

/* ===== four-valued logic (pure) ===== */

JNIEXPORT jint JNICALL FN(lpNot)(JNIEnv *env, jclass cls, jint a, jintArray out)
{
    (void) cls;
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    int32_t r;
    int32_t rc = zxv_lp_not(a, &r);
    if (rc == ZXV_OK) put_int(env, out, 0, r);
    return rc;
}

JNIEXPORT jint JNICALL FN(lpAnd)(JNIEnv *env, jclass cls, jint a, jint b, jintArray out)
{
    (void) cls;
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    int32_t r;
    int32_t rc = zxv_lp_and(a, b, &r);
    if (rc == ZXV_OK) put_int(env, out, 0, r);
    return rc;
}

JNIEXPORT jint JNICALL FN(lpOr)(JNIEnv *env, jclass cls, jint a, jint b, jintArray out)
{
    (void) cls;
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    int32_t r;
    int32_t rc = zxv_lp_or(a, b, &r);
    if (rc == ZXV_OK) put_int(env, out, 0, r);
    return rc;
}

/* ===== hexagram tag (pure; every narrow argument range-checked first) ===== */

static jint u8_out(JNIEnv *env, jintArray out, int32_t rc, uint8_t v)
{
    if (rc == ZXV_OK) put_int(env, out, 0, (jint) v);
    return rc;
}

JNIEXPORT jint JNICALL FN(hexPack)(JNIEnv *env, jclass cls, jint base, jint pol, jint phase, jintArray out)
{
    (void) cls;
    if (!u8_ok(base) || !u8_ok(pol) || !u8_ok(phase) || !len_is(env, out, 1)) return ZXV_E_ARG;
    uint8_t t = 0;
    int32_t rc_ = zxv_hex_pack((uint8_t) base, (uint8_t) pol, (uint8_t) phase, &t);
    return u8_out(env, out, rc_, t);
}

JNIEXPORT jint JNICALL FN(hexUnpack)(JNIEnv *env, jclass cls, jint tag, jintArray out)
{
    (void) cls;
    if (!u8_ok(tag)) return ZXV_E_ARG;
    if (!out) return zxv_hex_unpack((uint8_t) tag, NULL, NULL, NULL); /* a range check */
    if (!len_is(env, out, 3)) return ZXV_E_ARG;
    uint8_t b = 0, p = 0, ph = 0;
    int32_t rc = zxv_hex_unpack((uint8_t) tag, &b, &p, &ph);
    if (rc == ZXV_OK) {
        jint v[3] = {b, p, ph};
        (*env)->SetIntArrayRegion(env, out, 0, 3, v);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN(hexAction)(JNIEnv *env, jclass cls, jint tag, jintArray out)
{
    (void) cls;
    if (!u8_ok(tag) || !len_is(env, out, 1)) return ZXV_E_ARG;
    int32_t a = 0;
    int32_t rc = zxv_hex_action((uint8_t) tag, &a);
    if (rc == ZXV_OK) put_int(env, out, 0, a);
    return rc;
}

JNIEXPORT jint JNICALL FN(hexPhaseNext)(JNIEnv *env, jclass cls, jint phase, jintArray out)
{
    (void) cls;
    if (!u8_ok(phase) || !len_is(env, out, 1)) return ZXV_E_ARG;
    uint8_t r = 0;
    int32_t rc_ = zxv_hex_phase_next((uint8_t) phase, &r);
    return u8_out(env, out, rc_, r);
}

JNIEXPORT jint JNICALL FN(hexStep)(JNIEnv *env, jclass cls, jint tag, jint phase, jintArray out)
{
    (void) cls;
    if (!u8_ok(tag) || !u8_ok(phase) || !len_is(env, out, 1)) return ZXV_E_ARG;
    uint8_t r = 0;
    int32_t rc_ = zxv_hex_step((uint8_t) tag, (uint8_t) phase, &r);
    return u8_out(env, out, rc_, r);
}

JNIEXPORT jint JNICALL FN(hexSet)(JNIEnv *env, jclass cls, jint tag, jint base, jint pol, jintArray out)
{
    (void) cls;
    if (!u8_ok(tag) || !u8_ok(base) || !u8_ok(pol) || !len_is(env, out, 1)) return ZXV_E_ARG;
    uint8_t r = 0;
    int32_t rc_ = zxv_hex_set((uint8_t) tag, (uint8_t) base, (uint8_t) pol, &r);
    return u8_out(env, out, rc_, r);
}

JNIEXPORT jint JNICALL FN(hexForbid)(JNIEnv *env, jclass cls, jint tag, jintArray out)
{
    (void) cls;
    if (!u8_ok(tag) || !len_is(env, out, 1)) return ZXV_E_ARG;
    uint8_t r = 0;
    int32_t rc_ = zxv_hex_forbid((uint8_t) tag, &r);
    return u8_out(env, out, rc_, r);
}

JNIEXPORT jint JNICALL FN(hexAnd)(JNIEnv *env, jclass cls, jint a, jint b, jintArray out)
{
    (void) cls;
    if (!u8_ok(a) || !u8_ok(b) || !len_is(env, out, 1)) return ZXV_E_ARG;
    uint8_t r = 0;
    int32_t rc_ = zxv_hex_and((uint8_t) a, (uint8_t) b, &r);
    return u8_out(env, out, rc_, r);
}

JNIEXPORT jint JNICALL FN(hexOr)(JNIEnv *env, jclass cls, jint a, jint b, jintArray out)
{
    (void) cls;
    if (!u8_ok(a) || !u8_ok(b) || !len_is(env, out, 1)) return ZXV_E_ARG;
    uint8_t r = 0;
    int32_t rc_ = zxv_hex_or((uint8_t) a, (uint8_t) b, &r);
    return u8_out(env, out, rc_, r);
}

JNIEXPORT jint JNICALL FN(hexNot)(JNIEnv *env, jclass cls, jint a, jintArray out)
{
    (void) cls;
    if (!u8_ok(a) || !len_is(env, out, 1)) return ZXV_E_ARG;
    uint8_t r = 0;
    int32_t rc_ = zxv_hex_not((uint8_t) a, &r);
    return u8_out(env, out, rc_, r);
}

JNIEXPORT jint JNICALL FN(hexPolarity)(JNIEnv *env, jclass cls, jlong p0, jlong n0, jlong p1, jlong n1, jint stale,
                                       jintArray out)
{
    (void) cls;
    if (!u32_ok(p0) || !u32_ok(n0) || !u32_ok(p1) || !u32_ok(n1) || !len_is(env, out, 1)) return ZXV_E_ARG;
    uint8_t r = 0;
    int32_t rc_ = zxv_hex_polarity((uint32_t) p0, (uint32_t) n0, (uint32_t) p1, (uint32_t) n1, (int32_t) stale, &r);
    return u8_out(env, out, rc_, r);
}

JNIEXPORT jint JNICALL FN(hexFromLp)(JNIEnv *env, jclass cls, jint lp, jintArray out)
{
    (void) cls;
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    uint8_t r = 0;
    int32_t rc_ = zxv_hex_from_lp((int32_t) lp, &r);
    return u8_out(env, out, rc_, r);
}

JNIEXPORT jint JNICALL FN(hexToLp)(JNIEnv *env, jclass cls, jint base, jintArray out)
{
    (void) cls;
    if (!u8_ok(base) || !len_is(env, out, 1)) return ZXV_E_ARG;
    int32_t r = 0;
    int32_t rc = zxv_hex_to_lp((uint8_t) base, &r);
    if (rc == ZXV_OK) put_int(env, out, 0, r);
    return rc;
}

JNIEXPORT jstring JNICALL FN(hexName)(JNIEnv *env, jclass cls, jint tag)
{
    (void) cls;
    return (*env)->NewStringUTF(env, u8_ok(tag) ? zxv_hex_name((uint8_t) tag) : "INVALID");
}

JNIEXPORT jstring JNICALL FN(hexActionName)(JNIEnv *env, jclass cls, jint action)
{
    (void) cls;
    return (*env)->NewStringUTF(env, zxv_hex_action_name((int32_t) action));
}

/* ===== quarantine log ===== */

JNIEXPORT jint JNICALL FN(hexlogCreate)(JNIEnv *env, jclass cls, jlong cap, jlong capacity, jlongArray out)
{
    (void) cls;
    if (!u32_ok(capacity) || !len_is(env, out, 1)) return ZXV_E_ARG;
    zxv_hexlog *l = NULL;
    int32_t rc = zxv_hexlog_create(CH(zxv_cap, cap), (uint32_t) capacity, &l);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) (intptr_t) l);
    return rc;
}

JNIEXPORT void JNICALL FN(hexlogDestroy)(JNIEnv *env, jclass cls, jlong log)
{
    (void) env;
    (void) cls;
    zxv_hexlog_destroy(H(zxv_hexlog, log));
}

JNIEXPORT jint JNICALL FN(hexlogAppend)(JNIEnv *env, jclass cls, jlong cap, jlong log, jint tag, jint reason,
                                        jlong submitter, jbyteArray subject, jlongArray out)
{
    (void) cls;
    if (!u8_ok(tag) || !u8_ok(reason) || !u32_ok(submitter) || !len_is(env, subject, 32) || !len_is(env, out, 1))
        return ZXV_E_ARG;
    uint8_t s[32];
    (*env)->GetByteArrayRegion(env, subject, 0, 32, (jbyte *) s);
    uint64_t seq = 0;
    int32_t rc = zxv_hexlog_append(CH(zxv_cap, cap), H(zxv_hexlog, log), (uint8_t) tag, (uint8_t) reason,
                                   (uint32_t) submitter, s, &seq);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) seq);
    return rc;
}

JNIEXPORT jint JNICALL FN(hexlogCount)(JNIEnv *env, jclass cls, jlong log, jlongArray out)
{
    (void) cls;
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    uint32_t n = 0;
    int32_t rc = zxv_hexlog_count(H(zxv_hexlog, log), &n);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) n);
    return rc;
}

JNIEXPORT jint JNICALL FN(hexlogHead)(JNIEnv *env, jclass cls, jlong log, jbyteArray out)
{
    (void) cls;
    if (!len_is(env, out, 32)) return ZXV_E_ARG;
    uint8_t h[32];
    int32_t rc = zxv_hexlog_head(H(zxv_hexlog, log), h);
    if (rc == ZXV_OK) (*env)->SetByteArrayRegion(env, out, 0, 32, (const jbyte *) h);
    return rc;
}

/* out = {seq, tag, reason, submitter}. */
JNIEXPORT jint JNICALL FN(hexlogAt)(JNIEnv *env, jclass cls, jlong log, jlong index, jlongArray out,
                                    jbyteArray subject, jbyteArray prev, jbyteArray hash)
{
    (void) cls;
    if (!u32_ok(index) || !len_is(env, out, 4) || !len_is(env, subject, 32) || !len_is(env, prev, 32) ||
        !len_is(env, hash, 32))
        return ZXV_E_ARG;
    uint64_t seq = 0;
    uint8_t tag = 0, reason = 0, s[32], p[32], h[32];
    uint32_t who = 0;
    int32_t rc = zxv_hexlog_at(H(zxv_hexlog, log), (uint32_t) index, &seq, &tag, &reason, &who, s, p, h);
    if (rc == ZXV_OK) {
        jlong v[4] = {(jlong) seq, (jlong) tag, (jlong) reason, (jlong) who};
        (*env)->SetLongArrayRegion(env, out, 0, 4, v);
        (*env)->SetByteArrayRegion(env, subject, 0, 32, (const jbyte *) s);
        (*env)->SetByteArrayRegion(env, prev, 0, 32, (const jbyte *) p);
        (*env)->SetByteArrayRegion(env, hash, 0, 32, (const jbyte *) h);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN(hexlogVerify)(JNIEnv *env, jclass cls, jlong log)
{
    (void) env;
    (void) cls;
    return zxv_hexlog_verify(H(zxv_hexlog, log));
}

/* ===== budgets (rmag) ===== */

JNIEXPORT jint JNICALL FN(rmagCreateCap)(JNIEnv *env, jclass cls, jlong cap, jlongArray out)
{
    (void) cls;
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    zxv_rmag *r = NULL;
    int32_t rc = zxv_rmag_create_cap(CH(zxv_cap, cap), &r);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) (intptr_t) r);
    return rc;
}

JNIEXPORT void JNICALL FN(rmagDestroy)(JNIEnv *env, jclass cls, jlong rmag)
{
    (void) env;
    (void) cls;
    zxv_rmag_destroy(H(zxv_rmag, rmag));
}

JNIEXPORT jint JNICALL FN(rmagBudget)(JNIEnv *env, jclass cls, jlong cap, jlong rmag, jbyteArray name, jint resource,
                                      jlong ln, jlong ld, jint enforce, jlongArray out)
{
    (void) cls;
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    int ok;
    char *n = cstr_in(env, name, &ok);
    if (!ok || !n) return ZXV_E_ARG;
    uint32_t id = 0;
    int32_t rc = zxv_rmag_budget(CH(zxv_cap, cap), H(zxv_rmag, rmag), n, (int32_t) resource, ln, ld,
                                 (int32_t) enforce, &id);
    cstr_free(n);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) id);
    return rc;
}

JNIEXPORT jint JNICALL FN(rmagConsume)(JNIEnv *env, jclass cls, jlong cap, jlong rmag, jlong budget, jlong n, jlong d)
{
    (void) env;
    (void) cls;
    if (!u32_ok(budget)) return ZXV_E_ARG;
    return zxv_rmag_consume(CH(zxv_cap, cap), H(zxv_rmag, rmag), (uint32_t) budget, n, d);
}

JNIEXPORT jint JNICALL FN(rmagRelease)(JNIEnv *env, jclass cls, jlong cap, jlong rmag, jlong budget, jlong n, jlong d)
{
    (void) env;
    (void) cls;
    if (!u32_ok(budget)) return ZXV_E_ARG;
    return zxv_rmag_release(CH(zxv_cap, cap), H(zxv_rmag, rmag), (uint32_t) budget, n, d);
}

JNIEXPORT jint JNICALL FN(rmagSetLimit)(JNIEnv *env, jclass cls, jlong cap, jlong rmag, jlong budget, jlong n, jlong d)
{
    (void) env;
    (void) cls;
    if (!u32_ok(budget)) return ZXV_E_ARG;
    return zxv_rmag_set_limit(CH(zxv_cap, cap), H(zxv_rmag, rmag), (uint32_t) budget, n, d);
}

/* out = {remaining num, remaining den, used num, used den, epoch}. */
JNIEXPORT jint JNICALL FN(rmagStatus)(JNIEnv *env, jclass cls, jlong rmag, jlong budget, jlongArray out)
{
    (void) cls;
    if (!u32_ok(budget) || !len_is(env, out, 5)) return ZXV_E_ARG;
    int64_t rn, rd, un, ud;
    uint64_t ep;
    int32_t rc = zxv_rmag_status(H(zxv_rmag, rmag), (uint32_t) budget, &rn, &rd, &un, &ud, &ep);
    if (rc == ZXV_OK) {
        jlong v[5] = {rn, rd, un, ud, (jlong) ep};
        (*env)->SetLongArrayRegion(env, out, 0, 5, v);
    }
    return rc;
}

/* ===== hashes and randomness (pure) ===== */

static jint hash32(JNIEnv *env, jbyteArray data, jbyteArray out, int32_t (*f)(const uint8_t *, size_t, uint8_t *))
{
    if (!len_is(env, out, 32)) return ZXV_E_ARG;
    size_t n;
    int ok;
    uint8_t *p = bytes_in(env, data, &n, &ok);
    if (!ok) return ZXV_E_NOMEM;
    if (!p) return ZXV_E_ARG;
    uint8_t h[32];
    int32_t rc = f(p, n, h);
    bytes_free(p, n);
    if (rc == ZXV_OK) (*env)->SetByteArrayRegion(env, out, 0, 32, (const jbyte *) h);
    return rc;
}

JNIEXPORT jint JNICALL FN(sha3256)(JNIEnv *env, jclass cls, jbyteArray data, jbyteArray out)
{
    (void) cls;
    return hash32(env, data, out, zxv_sha3_256);
}

JNIEXPORT jint JNICALL FN(keccak256)(JNIEnv *env, jclass cls, jbyteArray data, jbyteArray out)
{
    (void) cls;
    return hash32(env, data, out, zxv_keccak256);
}

JNIEXPORT jint JNICALL FN(shake256)(JNIEnv *env, jclass cls, jbyteArray data, jbyteArray out)
{
    (void) cls;
    if (!out) return ZXV_E_ARG;
    size_t n;
    int ok;
    uint8_t *p = bytes_in(env, data, &n, &ok);
    if (!ok) return ZXV_E_NOMEM;
    if (!p) return ZXV_E_ARG;
    jsize on = (*env)->GetArrayLength(env, out);
    uint8_t *o = (uint8_t *) malloc(on > 0 ? (size_t) on : 1);
    if (!o) {
        bytes_free(p, n);
        return ZXV_E_NOMEM;
    }
    int32_t rc = zxv_shake256(p, n, o, (size_t) on);
    bytes_free(p, n);
    if (rc == ZXV_OK) (*env)->SetByteArrayRegion(env, out, 0, on, (const jbyte *) o);
    free(o);
    return rc;
}

/* ===== secret memory: C-heap buffers the GC never sees or copies ===== */

JNIEXPORT jlong JNICALL FN(secretAlloc)(JNIEnv *env, jclass cls, jlong len)
{
    (void) env;
    (void) cls;
    if (len <= 0 || len > (1 << 20)) return 0;
    return (jlong) (intptr_t) calloc(1, (size_t) len);
}

JNIEXPORT void JNICALL FN(secretFree)(JNIEnv *env, jclass cls, jlong ptr, jlong len)
{
    (void) env;
    (void) cls;
    void *p = H(void, ptr);
    if (!p) return;
    zxv_secure_zero(p, (size_t) len);
    free(p);
}

JNIEXPORT jint JNICALL FN(secretCopyIn)(JNIEnv *env, jclass cls, jlong ptr, jbyteArray src)
{
    (void) cls;
    if (!ptr || !src) return ZXV_E_ARG;
    jsize n = (*env)->GetArrayLength(env, src);
    (*env)->GetByteArrayRegion(env, src, 0, n, (jbyte *) H(void, ptr));
    return ZXV_OK;
}

JNIEXPORT jint JNICALL FN(secretCopyOut)(JNIEnv *env, jclass cls, jlong ptr, jbyteArray dst)
{
    (void) cls;
    if (!ptr || !dst) return ZXV_E_ARG;
    jsize n = (*env)->GetArrayLength(env, dst);
    (*env)->SetByteArrayRegion(env, dst, 0, n, (const jbyte *) H(void, ptr));
    return ZXV_OK;
}

/* ===== Ed25519 (the secret key stays in secret memory) ===== */

JNIEXPORT jint JNICALL FN(ed25519Keygen)(JNIEnv *env, jclass cls, jlong cap, jbyteArray pk, jlong sk)
{
    (void) cls;
    if (!len_is(env, pk, 32) || !sk) return ZXV_E_ARG;
    uint8_t p[32];
    int32_t rc = zxv_ed25519_keygen(CH(zxv_cap, cap), p, H(uint8_t, sk));
    if (rc == ZXV_OK) (*env)->SetByteArrayRegion(env, pk, 0, 32, (const jbyte *) p);
    return rc;
}

JNIEXPORT jint JNICALL FN(ed25519Public)(JNIEnv *env, jclass cls, jlong cap, jlong sk, jbyteArray pk)
{
    (void) cls;
    if (!len_is(env, pk, 32) || !sk) return ZXV_E_ARG;
    uint8_t p[32];
    int32_t rc = zxv_ed25519_public(CH(zxv_cap, cap), CH(uint8_t, sk), p);
    if (rc == ZXV_OK) (*env)->SetByteArrayRegion(env, pk, 0, 32, (const jbyte *) p);
    return rc;
}

JNIEXPORT jint JNICALL FN(ed25519Sign)(JNIEnv *env, jclass cls, jlong cap, jlong sk, jbyteArray msg, jbyteArray sig)
{
    (void) cls;
    if (!len_is(env, sig, 64) || !sk) return ZXV_E_ARG;
    size_t n;
    int ok;
    uint8_t *m = bytes_in(env, msg, &n, &ok);
    if (!ok) return ZXV_E_NOMEM;
    if (!m) return ZXV_E_ARG;
    uint8_t s[64];
    int32_t rc = zxv_ed25519_sign(CH(zxv_cap, cap), CH(uint8_t, sk), m, n, s);
    bytes_free(m, n);
    if (rc == ZXV_OK) (*env)->SetByteArrayRegion(env, sig, 0, 64, (const jbyte *) s);
    return rc;
}

JNIEXPORT jint JNICALL FN(ed25519Verify)(JNIEnv *env, jclass cls, jlong cap, jbyteArray pk, jbyteArray msg,
                                         jbyteArray sig)
{
    (void) cls;
    if (!len_is(env, pk, 32) || !len_is(env, sig, 64)) return ZXV_E_ARG;
    uint8_t p[32], s[64];
    (*env)->GetByteArrayRegion(env, pk, 0, 32, (jbyte *) p);
    (*env)->GetByteArrayRegion(env, sig, 0, 64, (jbyte *) s);
    size_t n;
    int ok;
    uint8_t *m = bytes_in(env, msg, &n, &ok);
    if (!ok) return ZXV_E_NOMEM;
    if (!m) return ZXV_E_ARG;
    int32_t rc = zxv_ed25519_verify(CH(zxv_cap, cap), p, m, n, s);
    bytes_free(m, n);
    return rc;
}
