/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * zxv_jni15.c - the JNI shim for the libzxv ABI 1.5 groups (treaty, aipi,
 * coin, fee, vhub, freight, web4, paradox), plus zxv_features2 and the
 * ML-DSA-87 calls the V-Bill hub's pegs need. Kotlin side:
 * org.zedec.zxv.mobile.internal.Jni15.
 *
 * Every rule at the top of zxv_jni.c holds here too: one mechanical function
 * per libzxv call, handles as jlong, narrow unsigned values range-checked
 * before the cast, strings as NUL-terminated UTF-8 byte[], arrays copied in
 * and out (never pinned across a call), heap copies wiped before free, no
 * global references, and out values copied back only on ZXV_OK. Conventions
 * added for 1.5:
 *
 *  - uint64 values cross as jlong bits (the Kotlin side documents them as
 *    unsigned). Several scalar out values come back packed in one long[] in
 *    the order the C function lists them.
 *  - A CID out is a byte[ZXV_CID_MAX] plus its length in a long[1].
 *  - "R4 for bytes" results use the same two-call shape as text: buf == NULL
 *    asks for the size.
 *  - Secret inputs (seeds, node keys, signing keys) are addresses of
 *    SecretKey memory (jlong, 0 for none), never Java arrays.
 *  - The one callback in the 1.5 ABI, the freight CIDREF resolver, never
 *    enters the JVM: Kotlin passes a table of (cid, bytes) it already
 *    resolved, and a miss is reported back so Kotlin can resolve it and call
 *    again (see freightOpsExpand).
 */
#include "zxvj_common.h"

/* Every function takes (JNIEnv *, jclass); most never touch the class. */
#pragma GCC diagnostic ignored "-Wunused-parameter"

#define FN15(name) Java_org_zedec_zxv_mobile_internal_Jni15_##name
#define JARGS JNIEnv *env, jclass cls

static inline int u16_ok(jint v) { return v >= 0 && v <= 65535; }

/* A byte[] copied in (NULL stays NULL). jb_in returns 0 only on NOMEM. */
typedef struct {
    uint8_t *p;
    size_t n;
} jb;

static int jb_in(JNIEnv *env, jbyteArray a, jb *b)
{
    int ok;
    b->p = bytes_in(env, a, &b->n, &ok);
    return ok;
}

static void jb_free(jb *b)
{
    bytes_free(b->p, b->n);
    b->p = NULL;
    b->n = 0;
}

/* Exactly n bytes into dst; 0 if a is NULL or another length. */
static int fix_in(JNIEnv *env, jbyteArray a, jsize n, uint8_t *dst)
{
    if (!len_is(env, a, n)) return 0;
    (*env)->GetByteArrayRegion(env, a, 0, n, (jbyte *) dst);
    return 1;
}

/* fix_in for an optional array: NULL gives *p = NULL. */
static int fix_opt(JNIEnv *env, jbyteArray a, jsize n, uint8_t *dst, const uint8_t **p)
{
    *p = NULL;
    if (!a) return 1;
    if (!fix_in(env, a, n, dst)) return 0;
    *p = dst;
    return 1;
}

static void set_bytes(JNIEnv *env, jbyteArray a, jsize n, const uint8_t *src)
{
    (*env)->SetByteArrayRegion(env, a, 0, n, (const jbyte *) src);
}

static void set_longs(JNIEnv *env, jlongArray a, jsize n, const jlong *v) { (*env)->SetLongArrayRegion(env, a, 0, n, v); }

/* A uint64_t[n] from a long[n] (bits kept). */
static void longs_in(JNIEnv *env, jlongArray a, jsize n, uint64_t *dst)
{
    jlong tmp[64];
    jsize done = 0;
    while (done < n) {
        jsize k = n - done < 64 ? n - done : 64;
        (*env)->GetLongArrayRegion(env, a, done, k, tmp);
        for (jsize i = 0; i < k; i++) dst[done + i] = (uint64_t) tmp[i];
        done += k;
    }
}

static void longs_out(JNIEnv *env, jlongArray a, jsize n, const uint64_t *src)
{
    jlong tmp[64];
    jsize done = 0;
    while (done < n) {
        jsize k = n - done < 64 ? n - done : 64;
        for (jsize i = 0; i < k; i++) tmp[i] = (jlong) src[done + i];
        (*env)->SetLongArrayRegion(env, a, done, k, tmp);
        done += k;
    }
}

/* One call of the R4 two-call pattern for bytes (or text, cast b_): sets
 * rc, never returns, so the caller can free what it copied in. */
#define R4(env, buf, outLen, rc, CALL)                                                       \
    do {                                                                                     \
        size_t need_ = 0;                                                                    \
        if (!len_at_least(env, outLen, 1)) {                                                 \
            rc = ZXV_E_ARG;                                                                  \
            break;                                                                           \
        }                                                                                    \
        if (!(buf)) {                                                                        \
            uint8_t *b_ = NULL;                                                              \
            size_t c_ = 0;                                                                   \
            rc = CALL;                                                                       \
        } else {                                                                             \
            jsize n_ = (*env)->GetArrayLength(env, buf);                                     \
            uint8_t *b_ = (uint8_t *) malloc(n_ > 0 ? (size_t) n_ : 1);                      \
            size_t c_ = (size_t) n_;                                                         \
            if (!b_) {                                                                       \
                rc = ZXV_E_NOMEM;                                                            \
                break;                                                                       \
            }                                                                                \
            rc = CALL;                                                                       \
            if (rc == ZXV_OK) (*env)->SetByteArrayRegion(env, buf, 0, n_, (jbyte *) b_);     \
            zxv_secure_zero(b_, n_ > 0 ? (size_t) n_ : 1);                                   \
            free(b_);                                                                        \
        }                                                                                    \
        if (rc == ZXV_OK || rc == ZXV_E_BUFFER) put_long(env, outLen, 0, (jlong) need_);     \
    } while (0)

/* A CID out: the bytes into cidOut[ZXV_CID_MAX], the length into outLen[0]. */
#define CID_OUT_OK(env, cidOut, outLen) (len_is(env, cidOut, ZXV_CID_MAX) && len_at_least(env, outLen, 1))

static void cid_out(JNIEnv *env, jbyteArray cidOut, jlongArray outLen, const uint8_t *cid, size_t n)
{
    set_bytes(env, cidOut, ZXV_CID_MAX, cid);
    put_long(env, outLen, 0, (jlong) n);
}

/* ===== features2 ===== */

JNIEXPORT jint JNICALL FN15(features2)(JARGS, jlongArray out)
{
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    uint32_t m = 0;
    int32_t rc = zxv_features2(&m);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) m);
    return rc;
}

/* ===== ML-DSA-87 (the secret key stays in secret memory) ===== */

JNIEXPORT jint JNICALL FN15(mldsa87Keygen)(JARGS, jlong cap, jbyteArray pk, jlong sk)
{
    if (!len_is(env, pk, ZXV_MLDSA87_PK_BYTES) || !sk) return ZXV_E_ARG;
    uint8_t p[ZXV_MLDSA87_PK_BYTES];
    int32_t rc = zxv_mldsa87_keygen(CH(zxv_cap, cap), p, H(uint8_t, sk));
    if (rc == ZXV_OK) set_bytes(env, pk, ZXV_MLDSA87_PK_BYTES, p);
    return rc;
}

JNIEXPORT jint JNICALL FN15(mldsa87KeygenSeed)(JARGS, jlong cap, jlong seed, jbyteArray pk, jlong sk)
{
    if (!seed || !len_is(env, pk, ZXV_MLDSA87_PK_BYTES) || !sk) return ZXV_E_ARG;
    uint8_t p[ZXV_MLDSA87_PK_BYTES];
    int32_t rc = zxv_mldsa87_keygen_seed(CH(zxv_cap, cap), CH(uint8_t, seed), p, H(uint8_t, sk));
    if (rc == ZXV_OK) set_bytes(env, pk, ZXV_MLDSA87_PK_BYTES, p);
    return rc;
}

static jint mldsa_sign(JNIEnv *env, jlong cap, jlong sk, jbyteArray msg, jbyteArray ctx, jbyteArray sig, int det)
{
    if (!sk || !msg || !len_is(env, sig, ZXV_MLDSA87_SIG_BYTES)) return ZXV_E_ARG;
    jb m, c;
    if (!jb_in(env, msg, &m)) return ZXV_E_NOMEM;
    if (!jb_in(env, ctx, &c)) {
        jb_free(&m);
        return ZXV_E_NOMEM;
    }
    uint8_t *s = (uint8_t *) malloc(ZXV_MLDSA87_SIG_BYTES);
    int32_t rc = ZXV_E_NOMEM;
    if (s) {
        rc = det ? zxv_mldsa87_sign_deterministic(CH(zxv_cap, cap), CH(uint8_t, sk), m.p, m.n, c.p, c.n, s)
                 : zxv_mldsa87_sign(CH(zxv_cap, cap), CH(uint8_t, sk), m.p, m.n, c.p, c.n, s);
        if (rc == ZXV_OK) set_bytes(env, sig, ZXV_MLDSA87_SIG_BYTES, s);
        free(s);
    }
    jb_free(&m);
    jb_free(&c);
    return rc;
}

JNIEXPORT jint JNICALL FN15(mldsa87Sign)(JARGS, jlong cap, jlong sk, jbyteArray msg, jbyteArray ctx, jbyteArray sig)
{
    return mldsa_sign(env, cap, sk, msg, ctx, sig, 0);
}

JNIEXPORT jint JNICALL FN15(mldsa87SignDeterministic)(JARGS, jlong cap, jlong sk, jbyteArray msg, jbyteArray ctx,
                                                      jbyteArray sig)
{
    return mldsa_sign(env, cap, sk, msg, ctx, sig, 1);
}

JNIEXPORT jint JNICALL FN15(mldsa87Verify)(JARGS, jlong cap, jbyteArray pk, jbyteArray msg, jbyteArray ctx,
                                           jbyteArray sig)
{
    if (!msg) return ZXV_E_ARG;
    uint8_t *p = (uint8_t *) malloc(ZXV_MLDSA87_PK_BYTES + ZXV_MLDSA87_SIG_BYTES);
    if (!p) return ZXV_E_NOMEM;
    uint8_t *s = p + ZXV_MLDSA87_PK_BYTES;
    int32_t rc = ZXV_E_ARG;
    if (fix_in(env, pk, ZXV_MLDSA87_PK_BYTES, p) && fix_in(env, sig, ZXV_MLDSA87_SIG_BYTES, s)) {
        jb m, c;
        rc = ZXV_E_NOMEM;
        if (jb_in(env, msg, &m)) {
            if (jb_in(env, ctx, &c)) {
                rc = zxv_mldsa87_verify(CH(zxv_cap, cap), p, m.p, m.n, c.p, c.n, s);
                jb_free(&c);
            }
            jb_free(&m);
        }
    }
    free(p);
    return rc;
}

/* ===== treaty ===== */

JNIEXPORT jint JNICALL FN15(treatyCreate)(JARGS, jlongArray out)
{
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    zxv_treaty *t = NULL;
    int32_t rc = zxv_treaty_create(&t);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) (intptr_t) t);
    return rc;
}

JNIEXPORT void JNICALL FN15(treatyDestroy)(JARGS, jlong t) { zxv_treaty_destroy(H(zxv_treaty, t)); }

JNIEXPORT jint JNICALL FN15(treatyClear)(JARGS, jlong t) { return zxv_treaty_clear(H(zxv_treaty, t)); }

/* fault = {offset, fault}: written on ZXV_OK and on ZXV_E_TREATY. */
JNIEXPORT jint JNICALL FN15(treatyParseSutra)(JARGS, jlong t, jbyteArray text, jlongArray fault)
{
    if (!len_is(env, fault, 2)) return ZXV_E_ARG;
    int ok;
    char *s = cstr_in(env, text, &ok);
    if (!ok || !s) return ZXV_E_ARG;
    uint32_t off = 0;
    int32_t f = 0;
    int32_t rc = zxv_treaty_parse_sutra(H(zxv_treaty, t), s, &off, &f);
    cstr_free(s);
    if (rc == ZXV_OK || rc == ZXV_E_TREATY) {
        jlong v[2] = {(jlong) off, (jlong) f};
        set_longs(env, fault, 2, v);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(treatyParseRecord)(JARGS, jlong t, jbyteArray rec, jlongArray fault)
{
    if (!rec || !len_is(env, fault, 2)) return ZXV_E_ARG;
    jb r;
    if (!jb_in(env, rec, &r)) return ZXV_E_NOMEM;
    uint32_t off = 0;
    int32_t f = 0;
    int32_t rc = zxv_treaty_parse_record(H(zxv_treaty, t), r.p, r.n, &off, &f);
    jb_free(&r);
    if (rc == ZXV_OK || rc == ZXV_E_TREATY) {
        jlong v[2] = {(jlong) off, (jlong) f};
        set_longs(env, fault, 2, v);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(treatyEmitSutra)(JARGS, jlong t, jbyteArray buf, jlongArray outLen)
{
    TEXT_CALL(env, buf, outLen, zxv_treaty_emit_sutra(CH(zxv_treaty, t), b_, c_, &need_));
}

JNIEXPORT jint JNICALL FN15(treatyEmitRecord)(JARGS, jlong t, jbyteArray buf, jlongArray outLen)
{
    int32_t rc;
    R4(env, buf, outLen, rc, zxv_treaty_emit_record(CH(zxv_treaty, t), b_, c_, &need_));
    return rc;
}

/* fault[0] is written on ZXV_OK and on ZXV_E_TREATY. */
JNIEXPORT jint JNICALL FN15(treatyValidate)(JARGS, jlong t, jintArray fault)
{
    if (!len_is(env, fault, 1)) return ZXV_E_ARG;
    int32_t f = 0;
    int32_t rc = zxv_treaty_validate(CH(zxv_treaty, t), &f);
    if (rc == ZXV_OK || rc == ZXV_E_TREATY) put_int(env, fault, 0, f);
    return rc;
}

JNIEXPORT jint JNICALL FN15(treatyEqual)(JARGS, jlong a, jlong b, jintArray out)
{
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    int32_t e = 0;
    int32_t rc = zxv_treaty_equal(CH(zxv_treaty, a), CH(zxv_treaty, b), &e);
    if (rc == ZXV_OK) put_int(env, out, 0, e);
    return rc;
}

JNIEXPORT jint JNICALL FN15(treatyName)(JARGS, jlong t, jbyteArray buf, jlongArray outLen)
{
    TEXT_CALL(env, buf, outLen, zxv_treaty_name(CH(zxv_treaty, t), b_, c_, &need_));
}

JNIEXPORT jint JNICALL FN15(treatyAsset)(JARGS, jlong t, jbyteArray buf, jlongArray outLen)
{
    TEXT_CALL(env, buf, outLen, zxv_treaty_asset(CH(zxv_treaty, t), b_, c_, &need_));
}

/* out = {op, parties, owes}. */
JNIEXPORT jint JNICALL FN15(treatyHeader)(JARGS, jlong t, jlongArray out)
{
    if (!len_is(env, out, 3)) return ZXV_E_ARG;
    int32_t op = 0;
    uint32_t np = 0, no = 0;
    int32_t rc = zxv_treaty_header(CH(zxv_treaty, t), &op, &np, &no);
    if (rc == ZXV_OK) {
        jlong v[3] = {op, np, no};
        set_longs(env, out, 3, v);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(treatyPartyId)(JARGS, jlong t, jlong index, jbyteArray buf, jlongArray outLen)
{
    if (!u32_ok(index)) return ZXV_E_ARG;
    TEXT_CALL(env, buf, outLen, zxv_treaty_party_id(CH(zxv_treaty, t), (uint32_t) index, b_, c_, &need_));
}

/* out = {deposit, claim, w_num, w_den}. */
JNIEXPORT jint JNICALL FN15(treatyParty)(JARGS, jlong t, jlong index, jlongArray out)
{
    if (!u32_ok(index) || !len_is(env, out, 4)) return ZXV_E_ARG;
    int64_t dep = 0, cl = 0;
    uint32_t wn = 0, wd = 0;
    int32_t rc = zxv_treaty_party(CH(zxv_treaty, t), (uint32_t) index, &dep, &cl, &wn, &wd);
    if (rc == ZXV_OK) {
        jlong v[4] = {dep, cl, wn, wd};
        set_longs(env, out, 4, v);
    }
    return rc;
}

/* out = {from, to, amount}. */
JNIEXPORT jint JNICALL FN15(treatyOwe)(JARGS, jlong t, jlong index, jlongArray out)
{
    if (!u32_ok(index) || !len_is(env, out, 3)) return ZXV_E_ARG;
    uint32_t f = 0, to = 0;
    int64_t amt = 0;
    int32_t rc = zxv_treaty_owe(CH(zxv_treaty, t), (uint32_t) index, &f, &to, &amt);
    if (rc == ZXV_OK) {
        jlong v[3] = {f, to, amt};
        set_longs(env, out, 3, v);
    }
    return rc;
}

#define PIC_BIN(name, cfn)                                                   \
    JNIEXPORT jint JNICALL FN15(name)(JARGS, jlong a, jlong b, jlongArray out) \
    {                                                                        \
        if (!len_is(env, out, 1)) return ZXV_E_ARG;                          \
        int64_t r = 0;                                                       \
        int32_t rc = cfn(a, b, &r);                                          \
        if (rc == ZXV_OK) put_long(env, out, 0, r);                          \
        return rc;                                                           \
    }

PIC_BIN(treatyPicAdd, zxv_treaty_pic_add)
PIC_BIN(treatyPicSub, zxv_treaty_pic_sub)
PIC_BIN(treatyPicMul, zxv_treaty_pic_mul)

/* out = {quotient, remainder}. */
JNIEXPORT jint JNICALL FN15(treatyPicDiv)(JARGS, jlong a, jlong b, jlongArray out)
{
    if (!len_is(env, out, 2)) return ZXV_E_ARG;
    int64_t q = 0, r = 0;
    int32_t rc = zxv_treaty_pic_div(a, b, &q, &r);
    if (rc == ZXV_OK) {
        jlong v[2] = {q, r};
        set_longs(env, out, 2, v);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(treatyRun)(JARGS, jlong t, jlongArray out)
{
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    zxv_treaty_result *r = NULL;
    int32_t rc = zxv_treaty_run(CH(zxv_treaty, t), &r);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) (intptr_t) r);
    return rc;
}

JNIEXPORT void JNICALL FN15(treatyResultDestroy)(JARGS, jlong r) { zxv_treaty_result_destroy(H(zxv_treaty_result, r)); }

/* out = {parties, legs, fees}. */
JNIEXPORT jint JNICALL FN15(treatyResultSummary)(JARGS, jlong r, jlongArray out)
{
    if (!len_is(env, out, 3)) return ZXV_E_ARG;
    uint32_t np = 0, nl = 0;
    int64_t fees = 0;
    int32_t rc = zxv_treaty_result_summary(CH(zxv_treaty_result, r), &np, &nl, &fees);
    if (rc == ZXV_OK) {
        jlong v[3] = {np, nl, fees};
        set_longs(env, out, 3, v);
    }
    return rc;
}

/* out = {net, balance, dividend}. */
JNIEXPORT jint JNICALL FN15(treatyResultParty)(JARGS, jlong r, jlong index, jlongArray out)
{
    if (!u32_ok(index) || !len_is(env, out, 3)) return ZXV_E_ARG;
    int64_t n = 0, b = 0, d = 0;
    int32_t rc = zxv_treaty_result_party(CH(zxv_treaty_result, r), (uint32_t) index, &n, &b, &d);
    if (rc == ZXV_OK) {
        jlong v[3] = {n, b, d};
        set_longs(env, out, 3, v);
    }
    return rc;
}

/* out = {from, to, amount}. */
JNIEXPORT jint JNICALL FN15(treatyResultLeg)(JARGS, jlong r, jlong index, jlongArray out)
{
    if (!u32_ok(index) || !len_is(env, out, 3)) return ZXV_E_ARG;
    uint32_t f = 0, to = 0;
    int64_t amt = 0;
    int32_t rc = zxv_treaty_result_leg(CH(zxv_treaty_result, r), (uint32_t) index, &f, &to, &amt);
    if (rc == ZXV_OK) {
        jlong v[3] = {f, to, amt};
        set_longs(env, out, 3, v);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(treatyResultRecord)(JARGS, jlong r, jbyteArray buf, jlongArray outLen)
{
    int32_t rc;
    R4(env, buf, outLen, rc, zxv_treaty_result_record(CH(zxv_treaty_result, r), b_, c_, &need_));
    return rc;
}

/* ===== aipi: CIDs (pure) ===== */

JNIEXPORT jint JNICALL FN15(cidCompute)(JARGS, jlong codec, jbyteArray data, jbyteArray cidOut, jlongArray outLen)
{
    if (!u32_ok(codec) || !data || !CID_OUT_OK(env, cidOut, outLen)) return ZXV_E_ARG;
    jb d;
    if (!jb_in(env, data, &d)) return ZXV_E_NOMEM;
    uint8_t cid[ZXV_CID_MAX] = {0};
    size_t n = 0;
    int32_t rc = zxv_cid_compute((uint32_t) codec, d.p, d.n, cid, &n);
    jb_free(&d);
    if (rc == ZXV_OK) cid_out(env, cidOut, outLen, cid, n);
    return rc;
}

JNIEXPORT jint JNICALL FN15(cidUnixfs)(JARGS, jbyteArray data, jbyteArray cidOut, jlongArray outLen)
{
    if (!data || !CID_OUT_OK(env, cidOut, outLen)) return ZXV_E_ARG;
    jb d;
    if (!jb_in(env, data, &d)) return ZXV_E_NOMEM;
    uint8_t cid[ZXV_CID_MAX] = {0};
    size_t n = 0;
    int32_t rc = zxv_cid_unixfs(d.p, d.n, cid, &n);
    jb_free(&d);
    if (rc == ZXV_OK) cid_out(env, cidOut, outLen, cid, n);
    return rc;
}

JNIEXPORT jint JNICALL FN15(cidVerify)(JARGS, jbyteArray cid, jbyteArray data)
{
    if (!cid || !data) return ZXV_E_ARG;
    jb c, d;
    if (!jb_in(env, cid, &c)) return ZXV_E_NOMEM;
    if (!jb_in(env, data, &d)) {
        jb_free(&c);
        return ZXV_E_NOMEM;
    }
    int32_t rc = zxv_cid_verify(c.p, c.n, d.p, d.n);
    jb_free(&c);
    jb_free(&d);
    return rc;
}

JNIEXPORT jint JNICALL FN15(cidToText)(JARGS, jbyteArray cid, jlong base, jbyteArray buf, jlongArray outLen)
{
    if (!cid || !u32_ok(base)) return ZXV_E_ARG;
    jb c;
    if (!jb_in(env, cid, &c)) return ZXV_E_NOMEM;
    int32_t rc;
    R4(env, buf, outLen, rc, zxv_cid_to_text(c.p, c.n, (uint32_t) base, (char *) b_, c_, &need_));
    jb_free(&c);
    return rc;
}

JNIEXPORT jint JNICALL FN15(cidFromText)(JARGS, jbyteArray text, jbyteArray cidOut, jlongArray outLen)
{
    if (!CID_OUT_OK(env, cidOut, outLen)) return ZXV_E_ARG;
    int ok;
    char *s = cstr_in(env, text, &ok);
    if (!ok || !s) return ZXV_E_ARG;
    uint8_t cid[ZXV_CID_MAX] = {0};
    size_t n = 0;
    int32_t rc = zxv_cid_from_text(s, cid, &n);
    cstr_free(s);
    if (rc == ZXV_OK) cid_out(env, cidOut, outLen, cid, n);
    return rc;
}

/* out = {version, codec, hash}; digest[ZXV_CID_DIGEST_MAX], its length in digestLen[0]. */
JNIEXPORT jint JNICALL FN15(cidInfo)(JARGS, jbyteArray cid, jlongArray out, jbyteArray digest, jlongArray digestLen)
{
    if (!cid || !len_is(env, out, 3) || !len_is(env, digest, ZXV_CID_DIGEST_MAX) || !len_at_least(env, digestLen, 1))
        return ZXV_E_ARG;
    jb c;
    if (!jb_in(env, cid, &c)) return ZXV_E_NOMEM;
    uint32_t ver = 0, codec = 0, hash = 0;
    uint8_t dg[ZXV_CID_DIGEST_MAX] = {0};
    size_t dn = 0;
    int32_t rc = zxv_cid_info(c.p, c.n, &ver, &codec, &hash, dg, &dn);
    jb_free(&c);
    if (rc == ZXV_OK) {
        jlong v[3] = {ver, codec, hash};
        set_longs(env, out, 3, v);
        set_bytes(env, digest, ZXV_CID_DIGEST_MAX, dg);
        put_long(env, digestLen, 0, (jlong) dn);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(cidToV1)(JARGS, jbyteArray cid, jbyteArray cidOut, jlongArray outLen)
{
    if (!cid || !CID_OUT_OK(env, cidOut, outLen)) return ZXV_E_ARG;
    jb c;
    if (!jb_in(env, cid, &c)) return ZXV_E_NOMEM;
    uint8_t o[ZXV_CID_MAX] = {0};
    size_t n = 0;
    int32_t rc = zxv_cid_to_v1(c.p, c.n, o, &n);
    jb_free(&c);
    if (rc == ZXV_OK) cid_out(env, cidOut, outLen, o, n);
    return rc;
}

JNIEXPORT jint JNICALL FN15(aipiProviderKey)(JARGS, jbyteArray cid, jbyteArray out)
{
    if (!cid || !len_is(env, out, 32)) return ZXV_E_ARG;
    jb c;
    if (!jb_in(env, cid, &c)) return ZXV_E_NOMEM;
    uint8_t k[32];
    int32_t rc = zxv_aipi_provider_key(c.p, c.n, k);
    jb_free(&c);
    if (rc == ZXV_OK) set_bytes(env, out, 32, k);
    return rc;
}

/* ===== aipi: records (pure) ===== */

/* u = {intent, verify, privacy, determinism, quorum_n, quorum_k} (uint32 each). */
JNIEXPORT jint JNICALL FN15(aipiFnEncode)(JARGS, jlongArray u, jlong fuelMax, jbyteArray module, jbyteArray contract,
                                          jbyteArray entry, jbyteArray checker, jbyteArray certifier, jbyteArray buf,
                                          jlongArray outLen)
{
    if (!len_is(env, u, 6) || !module) return ZXV_E_ARG;
    jlong f[6];
    (*env)->GetLongArrayRegion(env, u, 0, 6, f);
    for (int i = 0; i < 6; i++)
        if (!u32_ok(f[i])) return ZXV_E_ARG;
    int32_t rc = ZXV_E_NOMEM;
    jb m = {0}, c = {0};
    char *en = NULL, *ch = NULL, *ce = NULL;
    int ok1, ok2, ok3;
    if (!jb_in(env, module, &m) || !jb_in(env, contract, &c)) goto out;
    en = cstr_in(env, entry, &ok1);
    ch = cstr_in(env, checker, &ok2);
    ce = cstr_in(env, certifier, &ok3);
    if (!ok1 || !ok2 || !ok3 || !en) {
        rc = ZXV_E_ARG;
        goto out;
    }
    R4(env, buf, outLen, rc,
       zxv_aipi_fn_encode((uint32_t) f[0], (uint32_t) f[1], (uint32_t) f[2], (uint32_t) f[3], (uint32_t) f[4],
                          (uint32_t) f[5], (uint64_t) fuelMax, m.p, m.n, c.p, c.n, en, ch, ce, b_, c_, &need_));
out:
    jb_free(&m);
    jb_free(&c);
    cstr_free(en);
    cstr_free(ch);
    cstr_free(ce);
    return rc;
}

/* out = {intent, verify, privacy, determinism, quorum_n, quorum_k, fuel_max}. */
JNIEXPORT jint JNICALL FN15(aipiFnDecode)(JARGS, jbyteArray rec, jlongArray out)
{
    if (!rec || !len_is(env, out, 7)) return ZXV_E_ARG;
    jb r;
    if (!jb_in(env, rec, &r)) return ZXV_E_NOMEM;
    uint32_t a = 0, b = 0, c = 0, d = 0, qn = 0, qk = 0;
    uint64_t fuel = 0;
    int32_t rc = zxv_aipi_fn_decode(r.p, r.n, &a, &b, &c, &d, &qn, &qk, &fuel);
    jb_free(&r);
    if (rc == ZXV_OK) {
        jlong v[7] = {a, b, c, d, qn, qk, (jlong) fuel};
        set_longs(env, out, 7, v);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(aipiFnGetCid)(JARGS, jbyteArray rec, jlong field, jbyteArray cidOut, jlongArray outLen)
{
    if (!rec || !u32_ok(field) || !CID_OUT_OK(env, cidOut, outLen)) return ZXV_E_ARG;
    jb r;
    if (!jb_in(env, rec, &r)) return ZXV_E_NOMEM;
    uint8_t cid[ZXV_CID_MAX] = {0};
    size_t n = 0;
    int32_t rc = zxv_aipi_fn_get_cid(r.p, r.n, (uint32_t) field, cid, &n);
    jb_free(&r);
    if (rc == ZXV_OK) cid_out(env, cidOut, outLen, cid, n);
    return rc;
}

JNIEXPORT jint JNICALL FN15(aipiFnGetName)(JARGS, jbyteArray rec, jlong field, jbyteArray buf, jlongArray outLen)
{
    if (!rec || !u32_ok(field)) return ZXV_E_ARG;
    jb r;
    if (!jb_in(env, rec, &r)) return ZXV_E_NOMEM;
    int32_t rc;
    R4(env, buf, outLen, rc, zxv_aipi_fn_get_name(r.p, r.n, (uint32_t) field, (char *) b_, c_, &need_));
    jb_free(&r);
    return rc;
}

JNIEXPORT jint JNICALL FN15(aipiFnCid)(JARGS, jbyteArray rec, jbyteArray cidOut, jlongArray outLen)
{
    if (!rec || !CID_OUT_OK(env, cidOut, outLen)) return ZXV_E_ARG;
    jb r;
    if (!jb_in(env, rec, &r)) return ZXV_E_NOMEM;
    uint8_t cid[ZXV_CID_MAX] = {0};
    size_t n = 0;
    int32_t rc = zxv_aipi_fn_cid(r.p, r.n, cid, &n);
    jb_free(&r);
    if (rc == ZXV_OK) cid_out(env, cidOut, outLen, cid, n);
    return rc;
}

JNIEXPORT jint JNICALL FN15(aipiCallEncode)(JARGS, jbyteArray fnCid, jbyteArray input, jbyteArray buf,
                                            jlongArray outLen)
{
    if (!fnCid) return ZXV_E_ARG;
    jb f, in;
    if (!jb_in(env, fnCid, &f)) return ZXV_E_NOMEM;
    if (!jb_in(env, input, &in)) {
        jb_free(&f);
        return ZXV_E_NOMEM;
    }
    int32_t rc;
    R4(env, buf, outLen, rc, zxv_aipi_call_encode(f.p, f.n, in.p, in.n, b_, c_, &need_));
    jb_free(&f);
    jb_free(&in);
    return rc;
}

/* out = {fn_cid_off, fn_cid_len, input_off, input_len}. */
JNIEXPORT jint JNICALL FN15(aipiCallDecode)(JARGS, jbyteArray rec, jlongArray out)
{
    if (!rec || !len_is(env, out, 4)) return ZXV_E_ARG;
    jb r;
    if (!jb_in(env, rec, &r)) return ZXV_E_NOMEM;
    size_t a = 0, b = 0, c = 0, d = 0;
    int32_t rc = zxv_aipi_call_decode(r.p, r.n, &a, &b, &c, &d);
    jb_free(&r);
    if (rc == ZXV_OK) {
        jlong v[4] = {(jlong) a, (jlong) b, (jlong) c, (jlong) d};
        set_longs(env, out, 4, v);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(aipiCallCid)(JARGS, jbyteArray fnCid, jbyteArray input, jbyteArray cidOut,
                                         jlongArray outLen)
{
    if (!fnCid || !CID_OUT_OK(env, cidOut, outLen)) return ZXV_E_ARG;
    jb f, in;
    if (!jb_in(env, fnCid, &f)) return ZXV_E_NOMEM;
    if (!jb_in(env, input, &in)) {
        jb_free(&f);
        return ZXV_E_NOMEM;
    }
    uint8_t cid[ZXV_CID_MAX] = {0};
    size_t n = 0;
    int32_t rc = zxv_aipi_call_cid(f.p, f.n, in.p, in.n, cid, &n);
    jb_free(&f);
    jb_free(&in);
    if (rc == ZXV_OK) cid_out(env, cidOut, outLen, cid, n);
    return rc;
}

JNIEXPORT jint JNICALL FN15(aipiClaimEncode)(JARGS, jbyteArray callCid, jbyteArray executor, jbyteArray output,
                                             jbyteArray cert, jbyteArray buf, jlongArray outLen)
{
    uint8_t ex[32];
    if (!callCid || !fix_in(env, executor, 32, ex)) return ZXV_E_ARG;
    jb c = {0}, o = {0}, ce = {0};
    int32_t rc = ZXV_E_NOMEM;
    if (jb_in(env, callCid, &c) && jb_in(env, output, &o) && jb_in(env, cert, &ce)) {
        R4(env, buf, outLen, rc, zxv_aipi_claim_encode(c.p, c.n, ex, o.p, o.n, ce.p, ce.n, b_, c_, &need_));
    }
    jb_free(&c);
    jb_free(&o);
    jb_free(&ce);
    return rc;
}

/* out = {call_off, call_len, executor_off, output_off, output_len, cert_off, cert_len}. */
JNIEXPORT jint JNICALL FN15(aipiClaimDecode)(JARGS, jbyteArray rec, jlongArray out)
{
    if (!rec || !len_is(env, out, 7)) return ZXV_E_ARG;
    jb r;
    if (!jb_in(env, rec, &r)) return ZXV_E_NOMEM;
    size_t v[7] = {0};
    int32_t rc = zxv_aipi_claim_decode(r.p, r.n, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6]);
    jb_free(&r);
    if (rc == ZXV_OK) {
        jlong w[7];
        for (int i = 0; i < 7; i++) w[i] = (jlong) v[i];
        set_longs(env, out, 7, w);
    }
    return rc;
}

/* ===== aipi: the node ===== */

JNIEXPORT jint JNICALL FN15(aipiCreate)(JARGS, jlong cap, jlong storeBytes, jlong maxBlocks, jlong nodeKey,
                                        jlongArray out)
{
    if (storeBytes < 0 || !u32_ok(maxBlocks) || !len_is(env, out, 1)) return ZXV_E_ARG;
    zxv_aipi *a = NULL;
    int32_t rc = zxv_aipi_create(CH(zxv_cap, cap), (size_t) storeBytes, (uint32_t) maxBlocks, CH(uint8_t, nodeKey), &a);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) (intptr_t) a);
    return rc;
}

JNIEXPORT void JNICALL FN15(aipiDestroy)(JARGS, jlong a) { zxv_aipi_destroy(H(zxv_aipi, a)); }

JNIEXPORT jint JNICALL FN15(aipiPut)(JARGS, jlong cap, jlong a, jbyteArray data, jint isPrivate, jbyteArray cidOut,
                                     jlongArray outLen)
{
    if (!data || !CID_OUT_OK(env, cidOut, outLen)) return ZXV_E_ARG;
    jb d;
    if (!jb_in(env, data, &d)) return ZXV_E_NOMEM;
    uint8_t cid[ZXV_CID_MAX] = {0};
    size_t n = 0;
    int32_t rc = zxv_aipi_put(CH(zxv_cap, cap), H(zxv_aipi, a), d.p, d.n, (int32_t) isPrivate, cid, &n);
    jb_free(&d);
    if (rc == ZXV_OK) cid_out(env, cidOut, outLen, cid, n);
    return rc;
}

JNIEXPORT jint JNICALL FN15(aipiGet)(JARGS, jlong a, jbyteArray cid, jbyteArray buf, jlongArray outLen)
{
    if (!cid) return ZXV_E_ARG;
    jb c;
    if (!jb_in(env, cid, &c)) return ZXV_E_NOMEM;
    int32_t rc;
    R4(env, buf, outLen, rc, zxv_aipi_get(H(zxv_aipi, a), c.p, c.n, b_, c_, &need_));
    jb_free(&c);
    return rc;
}

JNIEXPORT jint JNICALL FN15(aipiPublishFn)(JARGS, jlong cap, jlong a, jbyteArray rec, jbyteArray cidOut,
                                           jlongArray outLen)
{
    if (!rec || !CID_OUT_OK(env, cidOut, outLen)) return ZXV_E_ARG;
    jb r;
    if (!jb_in(env, rec, &r)) return ZXV_E_NOMEM;
    uint8_t cid[ZXV_CID_MAX] = {0};
    size_t n = 0;
    int32_t rc = zxv_aipi_publish_fn(CH(zxv_cap, cap), H(zxv_aipi, a), r.p, r.n, cid, &n);
    jb_free(&r);
    if (rc == ZXV_OK) cid_out(env, cidOut, outLen, cid, n);
    return rc;
}

JNIEXPORT jint JNICALL FN15(aipiLoadFn)(JARGS, jlong a, jbyteArray fnCid, jbyteArray buf, jlongArray outLen)
{
    if (!fnCid) return ZXV_E_ARG;
    jb c;
    if (!jb_in(env, fnCid, &c)) return ZXV_E_NOMEM;
    int32_t rc;
    R4(env, buf, outLen, rc, zxv_aipi_load_fn(H(zxv_aipi, a), c.p, c.n, b_, c_, &need_));
    jb_free(&c);
    return rc;
}

JNIEXPORT jint JNICALL FN15(aipiPlan)(JARGS, jlong a, jbyteArray fnCid, jbyteArray input, jint inputPrivate,
                                      jintArray out)
{
    if (!fnCid || !len_is(env, out, 1)) return ZXV_E_ARG;
    jb c, in;
    if (!jb_in(env, fnCid, &c)) return ZXV_E_NOMEM;
    if (!jb_in(env, input, &in)) {
        jb_free(&c);
        return ZXV_E_NOMEM;
    }
    int32_t route = 0;
    int32_t rc = zxv_aipi_plan(H(zxv_aipi, a), c.p, c.n, in.p, in.n, (int32_t) inputPrivate, &route);
    jb_free(&c);
    jb_free(&in);
    if (rc == ZXV_OK) put_int(env, out, 0, route);
    return rc;
}

/* out = {blocks, store_used, gets_verified, forbidden}. */
JNIEXPORT jint JNICALL FN15(aipiInfo)(JARGS, jlong a, jlongArray out)
{
    if (!len_is(env, out, 4)) return ZXV_E_ARG;
    uint32_t b = 0, g = 0, f = 0;
    uint64_t used = 0;
    int32_t rc = zxv_aipi_info(H(zxv_aipi, a), &b, &used, &g, &f);
    if (rc == ZXV_OK) {
        jlong v[4] = {b, (jlong) used, g, f};
        set_longs(env, out, 4, v);
    }
    return rc;
}

/* ===== coin ===== */

JNIEXPORT jint JNICALL FN15(coinFace)(JARGS, jlong rung, jlongArray out)
{
    if (!u32_ok(rung) || !len_is(env, out, 1)) return ZXV_E_ARG;
    uint64_t f = 0;
    int32_t rc = zxv_coin_face((uint32_t) rung, &f);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) f);
    return rc;
}

JNIEXPORT jint JNICALL FN15(coinRung)(JARGS, jlong face, jlongArray out)
{
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    uint32_t r = 0;
    int32_t rc = zxv_coin_rung((uint64_t) face, &r);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) r);
    return rc;
}

/* Two-call on outCount[0]: rungs == NULL asks for the count. */
JNIEXPORT jint JNICALL FN15(coinSplit)(JARGS, jlong amount, jintArray rungs, jlongArray outCount)
{
    if (!len_at_least(env, outCount, 1)) return ZXV_E_ARG;
    size_t n = 0;
    int32_t rc;
    if (!rungs) {
        rc = zxv_coin_split((uint64_t) amount, NULL, 0, &n);
    } else {
        jsize cap = (*env)->GetArrayLength(env, rungs);
        uint32_t *r = (uint32_t *) malloc(cap > 0 ? (size_t) cap * sizeof(uint32_t) : 1);
        if (!r) return ZXV_E_NOMEM;
        rc = zxv_coin_split((uint64_t) amount, r, (size_t) cap, &n);
        if (rc == ZXV_OK) (*env)->SetIntArrayRegion(env, rungs, 0, (jsize) n, (const jint *) r);
        free(r);
    }
    if (rc == ZXV_OK || rc == ZXV_E_BUFFER) put_long(env, outCount, 0, (jlong) n);
    return rc;
}

/* out = {face, is_super}. */
JNIEXPORT jint JNICALL FN15(tokenFace)(JARGS, jlong tier, jlongArray out)
{
    if (!u32_ok(tier) || !len_is(env, out, 2)) return ZXV_E_ARG;
    uint64_t f = 0;
    int32_t s = 0;
    int32_t rc = zxv_token_face((uint32_t) tier, &f, &s);
    if (rc == ZXV_OK) {
        jlong v[2] = {(jlong) f, s};
        set_longs(env, out, 2, v);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(tokenTier)(JARGS, jlong face, jlongArray out)
{
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    uint32_t t = 0;
    int32_t rc = zxv_token_tier((uint64_t) face, &t);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) t);
    return rc;
}

/* out = {value, pieces}. */
JNIEXPORT jint JNICALL FN15(tokenValue)(JARGS, jlongArray counts, jlongArray out)
{
    if (!len_is(env, counts, ZXV_TOKEN_TIERS) || !len_is(env, out, 2)) return ZXV_E_ARG;
    uint64_t c[ZXV_TOKEN_TIERS], v = 0, p = 0;
    longs_in(env, counts, ZXV_TOKEN_TIERS, c);
    int32_t rc = zxv_token_value(c, &v, &p);
    if (rc == ZXV_OK) {
        jlong w[2] = {(jlong) v, (jlong) p};
        set_longs(env, out, 2, w);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(tokenCanonical)(JARGS, jlong value, jlong octane, jlongArray outCounts)
{
    if (!u32_ok(octane) || !len_is(env, outCounts, ZXV_TOKEN_TIERS)) return ZXV_E_ARG;
    uint64_t c[ZXV_TOKEN_TIERS];
    int32_t rc = zxv_token_canonical((uint64_t) value, (uint32_t) octane, c);
    if (rc == ZXV_OK) longs_out(env, outCounts, ZXV_TOKEN_TIERS, c);
    return rc;
}

JNIEXPORT jint JNICALL FN15(tokenIsCanonical)(JARGS, jlongArray counts, jlong octane, jintArray out)
{
    if (!len_is(env, counts, ZXV_TOKEN_TIERS) || !u32_ok(octane) || !len_is(env, out, 1)) return ZXV_E_ARG;
    uint64_t c[ZXV_TOKEN_TIERS];
    longs_in(env, counts, ZXV_TOKEN_TIERS, c);
    int32_t k = 0;
    int32_t rc = zxv_token_is_canonical(c, (uint32_t) octane, &k);
    if (rc == ZXV_OK) put_int(env, out, 0, k);
    return rc;
}

/* counts is changed in place (copied back only on ZXV_OK). */
JNIEXPORT jint JNICALL FN15(tokenCompress)(JARGS, jlongArray counts, jlong octane)
{
    if (!len_is(env, counts, ZXV_TOKEN_TIERS) || !u32_ok(octane)) return ZXV_E_ARG;
    uint64_t c[ZXV_TOKEN_TIERS];
    longs_in(env, counts, ZXV_TOKEN_TIERS, c);
    int32_t rc = zxv_token_compress(c, (uint32_t) octane);
    if (rc == ZXV_OK) longs_out(env, counts, ZXV_TOKEN_TIERS, c);
    return rc;
}

JNIEXPORT jint JNICALL FN15(tokenFuse)(JARGS, jlongArray counts, jlong octane, jlong tier)
{
    if (!len_is(env, counts, ZXV_TOKEN_TIERS) || !u32_ok(octane) || !u32_ok(tier)) return ZXV_E_ARG;
    uint64_t c[ZXV_TOKEN_TIERS];
    longs_in(env, counts, ZXV_TOKEN_TIERS, c);
    int32_t rc = zxv_token_fuse(c, (uint32_t) octane, (uint32_t) tier);
    if (rc == ZXV_OK) longs_out(env, counts, ZXV_TOKEN_TIERS, c);
    return rc;
}

JNIEXPORT jint JNICALL FN15(tokenSplit)(JARGS, jlongArray counts, jlong tier)
{
    if (!len_is(env, counts, ZXV_TOKEN_TIERS) || !u32_ok(tier)) return ZXV_E_ARG;
    uint64_t c[ZXV_TOKEN_TIERS];
    longs_in(env, counts, ZXV_TOKEN_TIERS, c);
    int32_t rc = zxv_token_split(c, (uint32_t) tier);
    if (rc == ZXV_OK) longs_out(env, counts, ZXV_TOKEN_TIERS, c);
    return rc;
}

JNIEXPORT jint JNICALL FN15(tokenCodeBits)(JARGS, jlong value, jlongArray out)
{
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    uint32_t b = 0;
    int32_t rc = zxv_token_code_bits((uint64_t) value, &b);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) b);
    return rc;
}

/* buf and bit[0] are changed in place (copied back only on ZXV_OK). */
JNIEXPORT jint JNICALL FN15(tokenCodePut)(JARGS, jbyteArray buf, jlongArray bit, jlong value)
{
    if (!buf || !len_is(env, bit, 1)) return ZXV_E_ARG;
    jlong b0;
    (*env)->GetLongArrayRegion(env, bit, 0, 1, &b0);
    if (!u32_ok(b0)) return ZXV_E_ARG;
    jb b;
    if (!jb_in(env, buf, &b)) return ZXV_E_NOMEM;
    uint32_t pos = (uint32_t) b0;
    int32_t rc = zxv_token_code_put(b.p, b.n, &pos, (uint64_t) value);
    if (rc == ZXV_OK) {
        if (b.n) set_bytes(env, buf, (jsize) b.n, b.p);
        put_long(env, bit, 0, (jlong) pos);
    }
    jb_free(&b);
    return rc;
}

JNIEXPORT jint JNICALL FN15(tokenCodeGet)(JARGS, jbyteArray buf, jlongArray bit, jlongArray out)
{
    if (!buf || !len_is(env, bit, 1) || !len_is(env, out, 1)) return ZXV_E_ARG;
    jlong b0;
    (*env)->GetLongArrayRegion(env, bit, 0, 1, &b0);
    if (!u32_ok(b0)) return ZXV_E_ARG;
    jb b;
    if (!jb_in(env, buf, &b)) return ZXV_E_NOMEM;
    uint32_t pos = (uint32_t) b0;
    uint64_t v = 0;
    int32_t rc = zxv_token_code_get(b.p, b.n, &pos, &v);
    jb_free(&b);
    if (rc == ZXV_OK) {
        put_long(env, bit, 0, (jlong) pos);
        put_long(env, out, 0, (jlong) v);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(tokenWalletCreate)(JARGS, jlong cap, jlong modelId, jlong octane, jlongArray out)
{
    if (!u32_ok(modelId) || !u32_ok(octane) || !len_is(env, out, 1)) return ZXV_E_ARG;
    zxv_token_wallet *w = NULL;
    int32_t rc = zxv_token_wallet_create(CH(zxv_cap, cap), (uint32_t) modelId, (uint32_t) octane, &w);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) (intptr_t) w);
    return rc;
}

JNIEXPORT void JNICALL FN15(tokenWalletDestroy)(JARGS, jlong w) { zxv_token_wallet_destroy(H(zxv_token_wallet, w)); }

JNIEXPORT jint JNICALL FN15(tokenWalletMint)(JARGS, jlong cap, jlong w, jlong amount)
{
    return zxv_token_wallet_mint(CH(zxv_cap, cap), H(zxv_token_wallet, w), (uint64_t) amount);
}

JNIEXPORT jint JNICALL FN15(tokenWalletSpend)(JARGS, jlong cap, jlong w, jlong amount)
{
    return zxv_token_wallet_spend(CH(zxv_cap, cap), H(zxv_token_wallet, w), (uint64_t) amount);
}

JNIEXPORT jint JNICALL FN15(tokenWalletTransfer)(JARGS, jlong cap, jlong from, jlong to, jlong amount)
{
    return zxv_token_wallet_transfer(CH(zxv_cap, cap), H(zxv_token_wallet, from), H(zxv_token_wallet, to),
                                     (uint64_t) amount);
}

JNIEXPORT jint JNICALL FN15(tokenWalletSetOctane)(JARGS, jlong cap, jlong w, jlong octane)
{
    if (!u32_ok(octane)) return ZXV_E_ARG;
    return zxv_token_wallet_set_octane(CH(zxv_cap, cap), H(zxv_token_wallet, w), (uint32_t) octane);
}

/* out = {model_id, octane, value, pieces}. */
JNIEXPORT jint JNICALL FN15(tokenWalletInfo)(JARGS, jlong w, jlongArray out)
{
    if (!len_is(env, out, 4)) return ZXV_E_ARG;
    uint32_t m = 0, o = 0;
    uint64_t v = 0, p = 0;
    int32_t rc = zxv_token_wallet_info(H(zxv_token_wallet, w), &m, &o, &v, &p);
    if (rc == ZXV_OK) {
        jlong x[4] = {m, o, (jlong) v, (jlong) p};
        set_longs(env, out, 4, x);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(tokenWalletCounts)(JARGS, jlong w, jlongArray outCounts)
{
    if (!len_is(env, outCounts, ZXV_TOKEN_TIERS)) return ZXV_E_ARG;
    uint64_t c[ZXV_TOKEN_TIERS];
    int32_t rc = zxv_token_wallet_counts(H(zxv_token_wallet, w), c);
    if (rc == ZXV_OK) longs_out(env, outCounts, ZXV_TOKEN_TIERS, c);
    return rc;
}

/* ===== fee (pure) ===== */

JNIEXPORT jint JNICALL FN15(feeAssure)(JARGS, jlong gross, jlongArray out)
{
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    uint64_t f = 0;
    int32_t rc = zxv_fee_assure((uint64_t) gross, &f);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) f);
    return rc;
}

/* out = {fee, rem}. */
JNIEXPORT jint JNICALL FN15(feeAssureCarry)(JARGS, jlong remIn, jlong gross, jlongArray out)
{
    if (!len_is(env, out, 2)) return ZXV_E_ARG;
    uint64_t f = 0, r = 0;
    int32_t rc = zxv_fee_assure_carry((uint64_t) remIn, (uint64_t) gross, &f, &r);
    if (rc == ZXV_OK) {
        jlong v[2] = {(jlong) f, (jlong) r};
        set_longs(env, out, 2, v);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(feeSplit)(JARGS, jlong fee, jlongArray out)
{
    if (!len_is(env, out, ZXV_FEE_BUCKETS)) return ZXV_E_ARG;
    uint64_t p[ZXV_FEE_BUCKETS];
    int32_t rc = zxv_fee_split((uint64_t) fee, p);
    if (rc == ZXV_OK) longs_out(env, out, ZXV_FEE_BUCKETS, p);
    return rc;
}

/* out = {pct[4], allow_below, rate_num[4], rate_den[4], mult_num, mult_den, commons_num, commons_den} (17). */
JNIEXPORT jint JNICALL FN15(feePolicyDefault)(JARGS, jlongArray out)
{
    if (!len_is(env, out, 17)) return ZXV_E_ARG;
    uint32_t pct[4];
    int32_t allow = 0;
    uint64_t rn[4], rd[4], mn = 0, md = 0, cn = 0, cd = 0;
    int32_t rc = zxv_fee_policy_default(pct, &allow, rn, rd, &mn, &md, &cn, &cd);
    if (rc == ZXV_OK) {
        jlong v[17];
        for (int i = 0; i < 4; i++) {
            v[i] = pct[i];
            v[5 + i] = (jlong) rn[i];
            v[9 + i] = (jlong) rd[i];
        }
        v[4] = allow;
        v[13] = (jlong) mn;
        v[14] = (jlong) md;
        v[15] = (jlong) cn;
        v[16] = (jlong) cd;
        set_longs(env, out, 17, v);
    }
    return rc;
}

/* ===== vhub ===== */

JNIEXPORT jint JNICALL FN15(vhubCreate)(JARGS, jlong cap, jbyteArray seed, jlongArray out)
{
    uint8_t s[32];
    const uint8_t *sp;
    if (!fix_opt(env, seed, 32, s, &sp) || !len_is(env, out, 1)) return ZXV_E_ARG;
    zxv_vhub *h = NULL;
    int32_t rc = zxv_vhub_create(CH(zxv_cap, cap), sp, &h);
    zxv_secure_zero(s, sizeof s);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) (intptr_t) h);
    return rc;
}

JNIEXPORT void JNICALL FN15(vhubDestroy)(JARGS, jlong hub) { zxv_vhub_destroy(H(zxv_vhub, hub)); }

JNIEXPORT jint JNICALL FN15(vhubAddCcy)(JARGS, jlong cap, jlong hub, jint slot, jbyteArray code, jint minorUnits,
                                        jbyteArray issuerPk, jlong poolFunding, jlong vbillBacking)
{
    if (!u8_ok(slot) || !u8_ok(minorUnits)) return ZXV_E_ARG;
    uint8_t *pk = (uint8_t *) malloc(ZXV_MLDSA87_PK_BYTES);
    if (!pk) return ZXV_E_NOMEM;
    int32_t rc = ZXV_E_ARG;
    if (fix_in(env, issuerPk, ZXV_MLDSA87_PK_BYTES, pk)) {
        int ok;
        char *c = cstr_in(env, code, &ok);
        if (ok && c) {
            rc = zxv_vhub_add_ccy(CH(zxv_cap, cap), H(zxv_vhub, hub), (uint8_t) slot, c, (uint8_t) minorUnits, pk,
                                  (uint64_t) poolFunding, (uint64_t) vbillBacking);
            cstr_free(c);
        }
    }
    free(pk);
    return rc;
}

/* out = {issuer_account, pool_account, vfloat_account, peg_num, peg_den, peg_epoch}. */
JNIEXPORT jint JNICALL FN15(vhubCcyInfo)(JARGS, jlong cap, jlong hub, jint slot, jlongArray out)
{
    if (!u8_ok(slot) || !len_is(env, out, 6)) return ZXV_E_ARG;
    uint32_t ia = 0, pa = 0, fa = 0;
    uint64_t n = 0, d = 0, e = 0;
    int32_t rc = zxv_vhub_ccy_info(CH(zxv_cap, cap), H(zxv_vhub, hub), (uint8_t) slot, &ia, &pa, &fa, &n, &d, &e);
    if (rc == ZXV_OK) {
        jlong v[6] = {ia, pa, fa, (jlong) n, (jlong) d, (jlong) e};
        set_longs(env, out, 6, v);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(vhubPegMsg)(JARGS, jint slot, jlong num, jlong den, jlong epoch, jbyteArray out)
{
    if (!u8_ok(slot) || !len_is(env, out, ZXV_VHUB_PEG_MSG_BYTES)) return ZXV_E_ARG;
    uint8_t m[ZXV_VHUB_PEG_MSG_BYTES];
    int32_t rc = zxv_vhub_peg_msg((uint8_t) slot, (uint64_t) num, (uint64_t) den, (uint64_t) epoch, m);
    if (rc == ZXV_OK) set_bytes(env, out, ZXV_VHUB_PEG_MSG_BYTES, m);
    return rc;
}

JNIEXPORT jint JNICALL FN15(vhubSetPeg)(JARGS, jlong cap, jlong hub, jint slot, jlong num, jlong den, jlong epoch,
                                        jbyteArray sig)
{
    if (!u8_ok(slot)) return ZXV_E_ARG;
    uint8_t *s = (uint8_t *) malloc(ZXV_MLDSA87_SIG_BYTES);
    if (!s) return ZXV_E_NOMEM;
    int32_t rc = ZXV_E_ARG;
    if (fix_in(env, sig, ZXV_MLDSA87_SIG_BYTES, s))
        rc = zxv_vhub_set_peg(CH(zxv_cap, cap), H(zxv_vhub, hub), (uint8_t) slot, (uint64_t) num, (uint64_t) den,
                              (uint64_t) epoch, s);
    free(s);
    return rc;
}

JNIEXPORT jint JNICALL FN15(vhubOpenAccount)(JARGS, jlong cap, jlong hub, jint slot, jlong owner, jlongArray out)
{
    if (!u8_ok(slot) || !u32_ok(owner) || !len_is(env, out, 1)) return ZXV_E_ARG;
    uint32_t a = 0;
    int32_t rc = zxv_vhub_open_account(CH(zxv_cap, cap), H(zxv_vhub, hub), (uint8_t) slot, (uint32_t) owner, &a);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) a);
    return rc;
}

JNIEXPORT jint JNICALL FN15(vhubIssue)(JARGS, jlong cap, jlong hub, jlong account, jlong amount)
{
    if (!u32_ok(account)) return ZXV_E_ARG;
    return zxv_vhub_issue(CH(zxv_cap, cap), H(zxv_vhub, hub), (uint32_t) account, (uint64_t) amount);
}

/* out = {debit, credit, equity}. */
JNIEXPORT jint JNICALL FN15(vhubBalance)(JARGS, jlong cap, jlong hub, jlong account, jlongArray out)
{
    if (!u32_ok(account) || !len_is(env, out, 3)) return ZXV_E_ARG;
    uint64_t d = 0, c = 0;
    int64_t e = 0;
    int32_t rc = zxv_vhub_balance(CH(zxv_cap, cap), H(zxv_vhub, hub), (uint32_t) account, &d, &c, &e);
    if (rc == ZXV_OK) {
        jlong v[3] = {(jlong) d, (jlong) c, e};
        set_longs(env, out, 3, v);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(vhubAddLimit)(JARGS, jlong cap, jlong hub, jint slot, jlong limit)
{
    if (!u8_ok(slot)) return ZXV_E_ARG;
    return zxv_vhub_add_limit(CH(zxv_cap, cap), H(zxv_vhub, hub), (uint8_t) slot, (uint64_t) limit);
}

JNIEXPORT jint JNICALL FN15(vhubNextEpoch)(JARGS, jlong cap, jlong hub, jlongArray out)
{
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    uint64_t e = 0;
    int32_t rc = zxv_vhub_next_epoch(CH(zxv_cap, cap), H(zxv_vhub, hub), &e);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) e);
    return rc;
}

/* out = {amount, vbills, received}. */
JNIEXPORT jint JNICALL FN15(vhubQuote)(JARGS, jlong cap, jlong hub, jint from, jint to, jlong want, jlongArray out)
{
    if (!u8_ok(from) || !u8_ok(to) || !len_is(env, out, 3)) return ZXV_E_ARG;
    uint64_t a = 0, v = 0, r = 0;
    int32_t rc = zxv_vhub_quote(CH(zxv_cap, cap), H(zxv_vhub, hub), (uint8_t) from, (uint8_t) to, (uint64_t) want,
                                &a, &v, &r);
    if (rc == ZXV_OK) {
        jlong x[3] = {(jlong) a, (jlong) v, (jlong) r};
        set_longs(env, out, 3, x);
    }
    return rc;
}

/* out = {vbills, received}. */
JNIEXPORT jint JNICALL FN15(vhubSwap)(JARGS, jlong cap, jlong hub, jint from, jint to, jlong sender, jlong receiver,
                                      jlong amount, jlong tick, jlongArray out)
{
    if (!u8_ok(from) || !u8_ok(to) || !u32_ok(sender) || !u32_ok(receiver) || !len_is(env, out, 2))
        return ZXV_E_ARG;
    uint64_t v = 0, r = 0;
    int32_t rc = zxv_vhub_swap(CH(zxv_cap, cap), H(zxv_vhub, hub), (uint8_t) from, (uint8_t) to, (uint32_t) sender,
                               (uint32_t) receiver, (uint64_t) amount, (uint64_t) tick, &v, &r);
    if (rc == ZXV_OK) {
        jlong x[2] = {(jlong) v, (jlong) r};
        set_longs(env, out, 2, x);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(vhubCheck)(JARGS, jlong cap, jlong hub, jlongArray out)
{
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    uint64_t p = 0;
    int32_t rc = zxv_vhub_check(CH(zxv_cap, cap), H(zxv_vhub, hub), &p);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) p);
    return rc;
}

/* ===== freight (pure) ===== */

JNIEXPORT jint JNICALL FN15(freightHeaderInit)(JARGS, jlong freightId, jint k, jint flags, jlong expandLimit,
                                               jbyteArray payload, jbyteArray outHeader)
{
    if (!u8_ok(k) || !u8_ok(flags) || !payload || !len_is(env, outHeader, ZXV_FREIGHT_HEADER_BYTES)) return ZXV_E_ARG;
    jb p;
    if (!jb_in(env, payload, &p)) return ZXV_E_NOMEM;
    uint8_t h[ZXV_FREIGHT_HEADER_BYTES];
    int32_t rc = zxv_freight_header_init((uint64_t) freightId, (uint8_t) k, (uint8_t) flags, (uint64_t) expandLimit,
                                         p.p, p.n, h);
    jb_free(&p);
    if (rc == ZXV_OK) set_bytes(env, outHeader, ZXV_FREIGHT_HEADER_BYTES, h);
    return rc;
}

/* f = {k, flags, payload_len, seq, seq_count} (uint8, uint8, uint16, uint16, uint16). */
JNIEXPORT jint JNICALL FN15(freightHeaderSerialize)(JARGS, jintArray f, jlong freightId, jlong expandLimit,
                                                    jbyteArray sha, jbyteArray outHeader)
{
    uint8_t s[32];
    if (!len_is(env, f, 5) || !fix_in(env, sha, 32, s) || !len_is(env, outHeader, ZXV_FREIGHT_HEADER_BYTES))
        return ZXV_E_ARG;
    jint v[5];
    (*env)->GetIntArrayRegion(env, f, 0, 5, v);
    if (!u8_ok(v[0]) || !u8_ok(v[1]) || !u16_ok(v[2]) || !u16_ok(v[3]) || !u16_ok(v[4])) return ZXV_E_ARG;
    uint8_t h[ZXV_FREIGHT_HEADER_BYTES];
    int32_t rc = zxv_freight_header_serialize((uint8_t) v[0], (uint8_t) v[1], (uint16_t) v[2], (uint16_t) v[3],
                                              (uint16_t) v[4], (uint64_t) freightId, (uint64_t) expandLimit, s, h);
    if (rc == ZXV_OK) set_bytes(env, outHeader, ZXV_FREIGHT_HEADER_BYTES, h);
    return rc;
}

/* out = {version, k, flags, payload_len, seq, seq_count, freight_id, expand_limit}; sha[32]. */
JNIEXPORT jint JNICALL FN15(freightHeaderParse)(JARGS, jbyteArray header, jlongArray out, jbyteArray sha)
{
    uint8_t h[ZXV_FREIGHT_HEADER_BYTES];
    if (!fix_in(env, header, ZXV_FREIGHT_HEADER_BYTES, h) || !len_is(env, out, 8) || !len_is(env, sha, 32))
        return ZXV_E_ARG;
    uint8_t ver = 0, k = 0, fl = 0, s[32];
    uint16_t pl = 0, seq = 0, sc = 0;
    uint64_t id = 0, lim = 0;
    int32_t rc = zxv_freight_header_parse(h, &ver, &k, &fl, &pl, &seq, &sc, &id, &lim, s);
    if (rc == ZXV_OK) {
        jlong v[8] = {ver, k, fl, pl, seq, sc, (jlong) id, (jlong) lim};
        set_longs(env, out, 8, v);
        set_bytes(env, sha, 32, s);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(freightHeaderCheck)(JARGS, jbyteArray header)
{
    uint8_t h[ZXV_FREIGHT_HEADER_BYTES];
    if (!fix_in(env, header, ZXV_FREIGHT_HEADER_BYTES, h)) return ZXV_E_ARG;
    return zxv_freight_header_check(h);
}

JNIEXPORT jint JNICALL FN15(freightEncode)(JARGS, jbyteArray header, jbyteArray payload, jbyteArray outPackets)
{
    uint8_t h[ZXV_FREIGHT_HEADER_BYTES];
    if (!fix_in(env, header, ZXV_FREIGHT_HEADER_BYTES, h) || !payload || !len_is(env, outPackets, ZXV_FREIGHT_BYTES))
        return ZXV_E_ARG;
    jb p;
    if (!jb_in(env, payload, &p)) return ZXV_E_NOMEM;
    uint8_t *o = (uint8_t *) malloc(ZXV_FREIGHT_BYTES);
    int32_t rc = ZXV_E_NOMEM;
    if (o) {
        rc = zxv_freight_encode(h, p.p, p.n, o);
        if (rc == ZXV_OK) set_bytes(env, outPackets, ZXV_FREIGHT_BYTES, o);
        zxv_secure_zero(o, ZXV_FREIGHT_BYTES);
        free(o);
    }
    jb_free(&p);
    return rc;
}

JNIEXPORT jint JNICALL FN15(freightDecode)(JARGS, jbyteArray header, jbyteArray packets, jbyteArray buf,
                                           jlongArray outLen)
{
    uint8_t h[ZXV_FREIGHT_HEADER_BYTES];
    if (!fix_in(env, header, ZXV_FREIGHT_HEADER_BYTES, h) || !packets) return ZXV_E_ARG;
    jb p;
    if (!jb_in(env, packets, &p)) return ZXV_E_NOMEM;
    int32_t rc;
    R4(env, buf, outLen, rc, zxv_freight_decode(h, p.p, p.n, b_, c_, &need_));
    jb_free(&p);
    return rc;
}

JNIEXPORT jint JNICALL FN15(freightSetCount)(JARGS, jlong len, jint k, jlongArray out)
{
    if (!u8_ok(k) || !len_is(env, out, 1)) return ZXV_E_ARG;
    uint32_t n = 0;
    int32_t rc = zxv_freight_set_count((uint64_t) len, (uint8_t) k, &n);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) n);
    return rc;
}

JNIEXPORT jint JNICALL FN15(freightSplit)(JARGS, jlong freightId, jint k, jint flags, jlong expandLimit,
                                          jbyteArray data, jlong seq, jbyteArray outHeader, jbyteArray outPackets)
{
    if (!u8_ok(k) || !u8_ok(flags) || !u32_ok(seq) || !data || !len_is(env, outHeader, ZXV_FREIGHT_HEADER_BYTES) ||
        !len_is(env, outPackets, ZXV_FREIGHT_BYTES))
        return ZXV_E_ARG;
    jb d;
    if (!jb_in(env, data, &d)) return ZXV_E_NOMEM;
    uint8_t h[ZXV_FREIGHT_HEADER_BYTES];
    uint8_t *o = (uint8_t *) malloc(ZXV_FREIGHT_BYTES);
    int32_t rc = ZXV_E_NOMEM;
    if (o) {
        rc = zxv_freight_split((uint64_t) freightId, (uint8_t) k, (uint8_t) flags, (uint64_t) expandLimit, d.p, d.n,
                               (uint32_t) seq, h, o);
        if (rc == ZXV_OK) {
            set_bytes(env, outHeader, ZXV_FREIGHT_HEADER_BYTES, h);
            set_bytes(env, outPackets, ZXV_FREIGHT_BYTES, o);
        }
        zxv_secure_zero(o, ZXV_FREIGHT_BYTES);
        free(o);
    }
    jb_free(&d);
    return rc;
}

JNIEXPORT jint JNICALL FN15(freightJoinCreate)(JARGS, jlong capacity, jlongArray out)
{
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    zxv_freight_join *j = NULL;
    int32_t rc = zxv_freight_join_create((uint64_t) capacity, &j);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) (intptr_t) j);
    return rc;
}

JNIEXPORT void JNICALL FN15(freightJoinDestroy)(JARGS, jlong j) { zxv_freight_join_destroy(H(zxv_freight_join, j)); }

JNIEXPORT jint JNICALL FN15(freightJoinAdd)(JARGS, jlong j, jbyteArray header, jbyteArray packets)
{
    uint8_t h[ZXV_FREIGHT_HEADER_BYTES];
    if (!fix_in(env, header, ZXV_FREIGHT_HEADER_BYTES, h) || !packets) return ZXV_E_ARG;
    jb p;
    if (!jb_in(env, packets, &p)) return ZXV_E_NOMEM;
    int32_t rc = zxv_freight_join_add(H(zxv_freight_join, j), h, p.p, p.n);
    jb_free(&p);
    return rc;
}

/* out = {received, count, complete, total}. */
JNIEXPORT jint JNICALL FN15(freightJoinStatus)(JARGS, jlong j, jlongArray out)
{
    if (!len_is(env, out, 4)) return ZXV_E_ARG;
    uint32_t r = 0, c = 0;
    int32_t done = 0;
    uint64_t t = 0;
    int32_t rc = zxv_freight_join_status(CH(zxv_freight_join, j), &r, &c, &done, &t);
    if (rc == ZXV_OK) {
        jlong v[4] = {r, c, done, (jlong) t};
        set_longs(env, out, 4, v);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(freightJoinResult)(JARGS, jlong j, jbyteArray buf, jlongArray outLen)
{
    int32_t rc;
    R4(env, buf, outLen, rc, zxv_freight_join_result(CH(zxv_freight_join, j), b_, c_, &need_));
    return rc;
}

/* out = {total, has_sha}. */
JNIEXPORT jint JNICALL FN15(freightOpsPeek)(JARGS, jbyteArray ops, jlongArray out)
{
    if (!ops || !len_is(env, out, 2)) return ZXV_E_ARG;
    jb o;
    if (!jb_in(env, ops, &o)) return ZXV_E_NOMEM;
    uint64_t t = 0;
    int32_t s = 0;
    int32_t rc = zxv_freight_ops_peek(o.p, o.n, &t, &s);
    jb_free(&o);
    if (rc == ZXV_OK) {
        jlong v[2] = {(jlong) t, s};
        set_longs(env, out, 2, v);
    }
    return rc;
}

/* The CIDREF table: entry i is cid bytes cids[...cidLens[i]] resolving to
 * data[...dataLens[i]], both arrays concatenated in entry order. */
typedef struct {
    const uint8_t *cids;
    const jint *cid_lens;
    const uint8_t *data;
    const jlong *data_lens;
    jsize n;
    uint8_t miss[ZXV_FREIGHT_CID_MAX];
    size_t miss_len;
    uint64_t miss_need;
} zxvj_cid_table;

static int32_t table_resolve(void *user, const uint8_t *cid, size_t cid_len, uint8_t *out, uint64_t len)
{
    zxvj_cid_table *t = (zxvj_cid_table *) user;
    size_t co = 0;
    uint64_t dof = 0;
    for (jsize i = 0; i < t->n; i++) {
        size_t cl = (size_t) t->cid_lens[i];
        uint64_t dl = (uint64_t) t->data_lens[i];
        if (cl == cid_len && dl == len && memcmp(t->cids + co, cid, cl) == 0) {
            if (len) memcpy(out, t->data + dof, (size_t) len);
            return ZXV_OK;
        }
        co += cl;
        dof += dl;
    }
    if (t->miss_len == 0 && cid_len >= 1 && cid_len <= ZXV_FREIGHT_CID_MAX) {
        memcpy(t->miss, cid, cid_len);
        t->miss_len = cid_len;
        t->miss_need = len;
    }
    return ZXV_E_NOT_FOUND;
}

/* Expand with the table above (all four table arrays NULL for none). When a
 * CIDREF is not in the table the call fails (ZXV_E_NOT_FOUND) and, as an
 * exception to the out-on-OK rule, missCid[128] / missInfo = {cid_len, len}
 * name the first unresolved reference (cid_len 0 if none) so Kotlin can
 * resolve it and call again. */
JNIEXPORT jint JNICALL FN15(freightOpsExpand)(JARGS, jbyteArray ops, jlong limit, jbyteArray tblCids,
                                              jintArray tblCidLens, jbyteArray tblData, jlongArray tblDataLens,
                                              jbyteArray buf, jlongArray outLen, jbyteArray missCid,
                                              jlongArray missInfo)
{
    if (!ops || !len_is(env, missCid, ZXV_FREIGHT_CID_MAX) || !len_is(env, missInfo, 2)) return ZXV_E_ARG;
    int has_tbl = tblCids || tblCidLens || tblData || tblDataLens;
    if (has_tbl && !(tblCids && tblCidLens && tblData && tblDataLens)) return ZXV_E_ARG;
    zxvj_cid_table t;
    memset(&t, 0, sizeof t);
    jb o = {0}, tc = {0}, td = {0};
    jint *cl = NULL;
    jlong *dl = NULL;
    int32_t rc = ZXV_E_NOMEM;
    if (!jb_in(env, ops, &o)) goto out;
    if (has_tbl) {
        jsize n = (*env)->GetArrayLength(env, tblCidLens);
        if ((*env)->GetArrayLength(env, tblDataLens) != n) {
            rc = ZXV_E_ARG;
            goto out;
        }
        cl = (jint *) malloc(n > 0 ? (size_t) n * sizeof(jint) : 1);
        dl = (jlong *) malloc(n > 0 ? (size_t) n * sizeof(jlong) : 1);
        if (!cl || !dl || !jb_in(env, tblCids, &tc) || !jb_in(env, tblData, &td)) goto out;
        (*env)->GetIntArrayRegion(env, tblCidLens, 0, n, cl);
        (*env)->GetLongArrayRegion(env, tblDataLens, 0, n, dl);
        uint64_t sc = 0, sd = 0;
        for (jsize i = 0; i < n; i++) {
            if (cl[i] < 0 || dl[i] < 0) {
                rc = ZXV_E_ARG;
                goto out;
            }
            sc += (uint64_t) cl[i];
            sd += (uint64_t) dl[i];
        }
        if (sc != tc.n || sd != td.n) {
            rc = ZXV_E_ARG;
            goto out;
        }
        t.cids = tc.p;
        t.cid_lens = cl;
        t.data = td.p;
        t.data_lens = dl;
        t.n = n;
    }
    R4(env, buf, outLen, rc,
       zxv_freight_ops_expand(o.p, o.n, (uint64_t) limit, has_tbl ? table_resolve : NULL, &t, b_, c_, &need_));
    {
        jlong mi[2] = {(jlong) t.miss_len, (jlong) t.miss_need};
        set_longs(env, missInfo, 2, mi);
        if (t.miss_len) set_bytes(env, missCid, ZXV_FREIGHT_CID_MAX, t.miss);
    }
out:
    jb_free(&o);
    jb_free(&tc);
    jb_free(&td);
    free(cl);
    free(dl);
    return rc;
}

/* out's length is the number of bytes to generate. */
JNIEXPORT jint JNICALL FN15(freightGenRun)(JARGS, jint gen, jbyteArray params, jbyteArray out)
{
    if (!u8_ok(gen) || !params || !out) return ZXV_E_ARG;
    jb p;
    if (!jb_in(env, params, &p)) return ZXV_E_NOMEM;
    jsize n = (*env)->GetArrayLength(env, out);
    uint8_t *o = (uint8_t *) malloc(n > 0 ? (size_t) n : 1);
    int32_t rc = ZXV_E_NOMEM;
    if (o) {
        rc = zxv_freight_gen_run((uint8_t) gen, p.p, p.n, o, (size_t) n);
        if (rc == ZXV_OK && n) set_bytes(env, out, n, o);
        free(o);
    }
    jb_free(&p);
    return rc;
}

JNIEXPORT jint JNICALL FN15(freightOpsEncode)(JARGS, jbyteArray in, jint withSha, jbyteArray buf, jlongArray outLen)
{
    if (!in) return ZXV_E_ARG;
    jb i;
    if (!jb_in(env, in, &i)) return ZXV_E_NOMEM;
    int32_t rc;
    R4(env, buf, outLen, rc, zxv_freight_ops_encode(i.p, i.n, (int32_t) withSha, b_, c_, &need_));
    jb_free(&i);
    return rc;
}

JNIEXPORT jint JNICALL FN15(freightOpsBound)(JARGS, jlong n, jint withSha, jlongArray out)
{
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    uint64_t b = 0;
    int32_t rc = zxv_freight_ops_bound((uint64_t) n, (int32_t) withSha, &b);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) b);
    return rc;
}

JNIEXPORT jint JNICALL FN15(freightOpsWriterCreate)(JARGS, jlong declaredTotal, jbyteArray sha, jlong maxBytes,
                                                    jlongArray out)
{
    uint8_t s[32];
    const uint8_t *sp;
    if (!fix_opt(env, sha, 32, s, &sp) || !len_is(env, out, 1)) return ZXV_E_ARG;
    zxv_freight_ops_writer *w = NULL;
    int32_t rc = zxv_freight_ops_writer_create((uint64_t) declaredTotal, sp, (uint64_t) maxBytes, &w);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) (intptr_t) w);
    return rc;
}

JNIEXPORT void JNICALL FN15(freightOpsWriterDestroy)(JARGS, jlong w)
{
    zxv_freight_ops_writer_destroy(H(zxv_freight_ops_writer, w));
}

JNIEXPORT jint JNICALL FN15(freightOpsPutLiteral)(JARGS, jlong w, jbyteArray bytes)
{
    if (!bytes) return ZXV_E_ARG;
    jb b;
    if (!jb_in(env, bytes, &b)) return ZXV_E_NOMEM;
    int32_t rc = zxv_freight_ops_put_literal(H(zxv_freight_ops_writer, w), b.p, b.n);
    jb_free(&b);
    return rc;
}

JNIEXPORT jint JNICALL FN15(freightOpsPutCopy)(JARGS, jlong w, jlong distance, jlong len)
{
    return zxv_freight_ops_put_copy(H(zxv_freight_ops_writer, w), (uint64_t) distance, (uint64_t) len);
}

JNIEXPORT jint JNICALL FN15(freightOpsPutCidref)(JARGS, jlong w, jbyteArray cid, jlong len)
{
    if (!cid) return ZXV_E_ARG;
    jb c;
    if (!jb_in(env, cid, &c)) return ZXV_E_NOMEM;
    int32_t rc = zxv_freight_ops_put_cidref(H(zxv_freight_ops_writer, w), c.p, c.n, (uint64_t) len);
    jb_free(&c);
    return rc;
}

JNIEXPORT jint JNICALL FN15(freightOpsPutSeed)(JARGS, jlong w, jint gen, jbyteArray params, jlong len)
{
    if (!u8_ok(gen) || !params) return ZXV_E_ARG;
    jb p;
    if (!jb_in(env, params, &p)) return ZXV_E_NOMEM;
    int32_t rc = zxv_freight_ops_put_seed(H(zxv_freight_ops_writer, w), (uint8_t) gen, p.p, p.n, (uint64_t) len);
    jb_free(&p);
    return rc;
}

JNIEXPORT jint JNICALL FN15(freightOpsWriterFinish)(JARGS, jlong w, jbyteArray buf, jlongArray outLen)
{
    int32_t rc;
    R4(env, buf, outLen, rc, zxv_freight_ops_writer_finish(CH(zxv_freight_ops_writer, w), b_, c_, &need_));
    return rc;
}

/* ===== web4 ===== */

JNIEXPORT jint JNICALL FN15(w4AgentCreate)(JARGS, jlong cap, jlong seed, jbyteArray name, jlong node, jlong nowMs,
                                           jlongArray out)
{
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    int ok;
    char *n = cstr_in(env, name, &ok);
    if (!ok || !n) return ZXV_E_ARG;
    zxv_w4_agent *a = NULL;
    int32_t rc = zxv_w4_agent_create(CH(zxv_cap, cap), CH(uint8_t, seed), n, CH(zxv_w4_agent, node),
                                     (uint64_t) nowMs, &a);
    cstr_free(n);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) (intptr_t) a);
    return rc;
}

JNIEXPORT void JNICALL FN15(w4AgentDestroy)(JARGS, jlong a) { zxv_w4_agent_destroy(H(zxv_w4_agent, a)); }

JNIEXPORT jint JNICALL FN15(w4AgentInfo)(JARGS, jlong a, jbyteArray agentId, jbyteArray peerId, jbyteArray pk)
{
    if (!len_is(env, agentId, 32) || !len_is(env, peerId, 32) || !len_is(env, pk, ZXV_W4_PK_BYTES)) return ZXV_E_ARG;
    uint8_t ai[32], pi[32], p[ZXV_W4_PK_BYTES];
    int32_t rc = zxv_w4_agent_info(CH(zxv_w4_agent, a), ai, pi, p);
    if (rc == ZXV_OK) {
        set_bytes(env, agentId, 32, ai);
        set_bytes(env, peerId, 32, pi);
        set_bytes(env, pk, ZXV_W4_PK_BYTES, p);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(w4AgentCard)(JARGS, jlong a, jbyteArray buf, jlongArray outLen)
{
    int32_t rc;
    R4(env, buf, outLen, rc, zxv_w4_agent_card(CH(zxv_w4_agent, a), b_, c_, &need_));
    return rc;
}

JNIEXPORT jint JNICALL FN15(w4CardVerify)(JARGS, jbyteArray card, jbyteArray agentId, jbyteArray peerId,
                                          jbyteArray name, jlongArray boundMs)
{
    if (!card || !len_is(env, agentId, 32) || !len_is(env, peerId, 32) || !len_is(env, name, 32) ||
        !len_is(env, boundMs, 1))
        return ZXV_E_ARG;
    jb c;
    if (!jb_in(env, card, &c)) return ZXV_E_NOMEM;
    uint8_t ai[32], pi[32];
    char nm[32] = {0};
    uint64_t b = 0;
    int32_t rc = zxv_w4_card_verify(c.p, c.n, ai, pi, nm, &b);
    jb_free(&c);
    if (rc == ZXV_OK) {
        set_bytes(env, agentId, 32, ai);
        set_bytes(env, peerId, 32, pi);
        set_bytes(env, name, 32, (const uint8_t *) nm);
        put_long(env, boundMs, 0, (jlong) b);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(w4DirCreate)(JARGS, jlong capacity, jlongArray out)
{
    if (!u32_ok(capacity) || !len_is(env, out, 1)) return ZXV_E_ARG;
    zxv_w4_dir *d = NULL;
    int32_t rc = zxv_w4_dir_create((uint32_t) capacity, &d);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) (intptr_t) d);
    return rc;
}

JNIEXPORT void JNICALL FN15(w4DirDestroy)(JARGS, jlong d) { zxv_w4_dir_destroy(H(zxv_w4_dir, d)); }

JNIEXPORT jint JNICALL FN15(w4DirAdd)(JARGS, jlong d, jbyteArray card)
{
    if (!card) return ZXV_E_ARG;
    jb c;
    if (!jb_in(env, card, &c)) return ZXV_E_NOMEM;
    int32_t rc = zxv_w4_dir_add(H(zxv_w4_dir, d), c.p, c.n);
    jb_free(&c);
    return rc;
}

JNIEXPORT jint JNICALL FN15(w4DirFind)(JARGS, jlong d, jbyteArray agentId, jbyteArray buf, jlongArray outLen)
{
    uint8_t ai[32];
    if (!fix_in(env, agentId, 32, ai)) return ZXV_E_ARG;
    int32_t rc;
    R4(env, buf, outLen, rc, zxv_w4_dir_find(CH(zxv_w4_dir, d), ai, b_, c_, &need_));
    return rc;
}

JNIEXPORT jint JNICALL FN15(w4DirCount)(JARGS, jlong d, jlongArray out)
{
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    uint32_t n = 0;
    int32_t rc = zxv_w4_dir_count(CH(zxv_w4_dir, d), &n);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) n);
    return rc;
}

JNIEXPORT jint JNICALL FN15(w4ManifestCreate)(JARGS, jlong version, jlong issuedMs, jlong expiresMs, jlongArray out)
{
    if (!u32_ok(version) || !len_is(env, out, 1)) return ZXV_E_ARG;
    zxv_w4_manifest *m = NULL;
    int32_t rc = zxv_w4_manifest_create((uint32_t) version, (uint64_t) issuedMs, (uint64_t) expiresMs, &m);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) (intptr_t) m);
    return rc;
}

JNIEXPORT void JNICALL FN15(w4ManifestDestroy)(JARGS, jlong m) { zxv_w4_manifest_destroy(H(zxv_w4_manifest, m)); }

/* u = {rate_per_min, burst, flags, max_input} (uint32 each). */
JNIEXPORT jint JNICALL FN15(w4ManifestAddTool)(JARGS, jlong m, jbyteArray name, jlong priceVfv, jlongArray u)
{
    if (!len_is(env, u, 4)) return ZXV_E_ARG;
    jlong v[4];
    (*env)->GetLongArrayRegion(env, u, 0, 4, v);
    for (int i = 0; i < 4; i++)
        if (!u32_ok(v[i])) return ZXV_E_ARG;
    int ok;
    char *n = cstr_in(env, name, &ok);
    if (!ok || !n) return ZXV_E_ARG;
    int32_t rc = zxv_w4_manifest_add_tool(H(zxv_w4_manifest, m), n, (uint64_t) priceVfv, (uint32_t) v[0],
                                          (uint32_t) v[1], (uint32_t) v[2], (uint32_t) v[3]);
    cstr_free(n);
    return rc;
}

JNIEXPORT jint JNICALL FN15(w4ManifestSign)(JARGS, jlong cap, jlong agent, jlong m)
{
    return zxv_w4_manifest_sign(CH(zxv_cap, cap), CH(zxv_w4_agent, agent), H(zxv_w4_manifest, m));
}

JNIEXPORT jint JNICALL FN15(w4ManifestVerify)(JARGS, jlong m, jbyteArray pk, jlong nowMs)
{
    uint8_t p[ZXV_W4_PK_BYTES];
    if (!fix_in(env, pk, ZXV_W4_PK_BYTES, p)) return ZXV_E_ARG;
    return zxv_w4_manifest_verify(CH(zxv_w4_manifest, m), p, (uint64_t) nowMs);
}

JNIEXPORT jint JNICALL FN15(w4ManifestEncode)(JARGS, jlong m, jbyteArray buf, jlongArray outLen)
{
    int32_t rc;
    R4(env, buf, outLen, rc, zxv_w4_manifest_encode(CH(zxv_w4_manifest, m), b_, c_, &need_));
    return rc;
}

JNIEXPORT jint JNICALL FN15(w4ManifestDecode)(JARGS, jbyteArray bytes, jlongArray out)
{
    if (!bytes || !len_is(env, out, 1)) return ZXV_E_ARG;
    jb b;
    if (!jb_in(env, bytes, &b)) return ZXV_E_NOMEM;
    zxv_w4_manifest *m = NULL;
    int32_t rc = zxv_w4_manifest_decode(b.p, b.n, &m);
    jb_free(&b);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) (intptr_t) m);
    return rc;
}

JNIEXPORT jint JNICALL FN15(w4ManifestToJson)(JARGS, jlong m, jbyteArray buf, jlongArray outLen)
{
    TEXT_CALL(env, buf, outLen, zxv_w4_manifest_to_json(CH(zxv_w4_manifest, m), b_, c_, &need_));
}

/* out = {version, issued_ms, expires_ms, tool_count, signed}. */
JNIEXPORT jint JNICALL FN15(w4ManifestInfo)(JARGS, jlong m, jbyteArray agentId, jbyteArray peerId, jlongArray out)
{
    if (!len_is(env, agentId, 32) || !len_is(env, peerId, 32) || !len_is(env, out, 5)) return ZXV_E_ARG;
    uint8_t ai[32], pi[32];
    uint32_t ver = 0, tc = 0;
    uint64_t is = 0, ex = 0;
    int32_t sg = 0;
    int32_t rc = zxv_w4_manifest_info(CH(zxv_w4_manifest, m), ai, pi, &ver, &is, &ex, &tc, &sg);
    if (rc == ZXV_OK) {
        set_bytes(env, agentId, 32, ai);
        set_bytes(env, peerId, 32, pi);
        jlong v[5] = {ver, (jlong) is, (jlong) ex, tc, sg};
        set_longs(env, out, 5, v);
    }
    return rc;
}

/* out = {price_vfv, rate_per_min, burst, flags, max_input}; name[32] NUL-terminated. */
JNIEXPORT jint JNICALL FN15(w4ManifestTool)(JARGS, jlong m, jlong index, jbyteArray name, jlongArray out)
{
    if (!u32_ok(index) || !len_is(env, name, 32) || !len_is(env, out, 5)) return ZXV_E_ARG;
    char nm[32] = {0};
    uint64_t price = 0;
    uint32_t rate = 0, burst = 0, flags = 0, mi = 0;
    int32_t rc = zxv_w4_manifest_tool(CH(zxv_w4_manifest, m), (uint32_t) index, nm, &price, &rate, &burst, &flags, &mi);
    if (rc == ZXV_OK) {
        set_bytes(env, name, 32, (const uint8_t *) nm);
        jlong v[5] = {(jlong) price, rate, burst, flags, mi};
        set_longs(env, out, 5, v);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(w4ManifestFind)(JARGS, jlong m, jbyteArray name, jlongArray out)
{
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    int ok;
    char *n = cstr_in(env, name, &ok);
    if (!ok || !n) return ZXV_E_ARG;
    uint32_t i = 0;
    int32_t rc = zxv_w4_manifest_find(CH(zxv_w4_manifest, m), n, &i);
    cstr_free(n);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) i);
    return rc;
}

JNIEXPORT jint JNICALL FN15(w4ActionDigest)(JARGS, jbyteArray tool, jbyteArray from, jbyteArray to, jlong reqId,
                                            jbyteArray payload, jbyteArray out)
{
    uint8_t f[32], t[32];
    if (!fix_in(env, from, 32, f) || !fix_in(env, to, 32, t) || !payload || !len_is(env, out, 32)) return ZXV_E_ARG;
    int ok;
    char *tl = cstr_in(env, tool, &ok);
    if (!ok || !tl) return ZXV_E_ARG;
    jb p;
    if (!jb_in(env, payload, &p)) {
        cstr_free(tl);
        return ZXV_E_NOMEM;
    }
    uint8_t d[32];
    int32_t rc = zxv_w4_action_digest(tl, f, t, (uint64_t) reqId, p.p, p.n, d);
    cstr_free(tl);
    jb_free(&p);
    if (rc == ZXV_OK) set_bytes(env, out, 32, d);
    return rc;
}

/* t = {max_vfv, issued_ms, expires_ms}. */
JNIEXPORT jint JNICALL FN15(w4ConsentIssue)(JARGS, jlong cap, jlong human, jbyteArray agentId, jbyteArray action,
                                            jlong scope, jlongArray t, jbyteArray nonce, jbyteArray outToken)
{
    uint8_t ai[32], ac[32], nc[16];
    const uint8_t *np;
    if (!fix_in(env, agentId, 32, ai) || !fix_in(env, action, 32, ac) || !u32_ok(scope) || !len_is(env, t, 3) ||
        !fix_opt(env, nonce, 16, nc, &np) || !len_is(env, outToken, ZXV_W4_CONSENT_BYTES))
        return ZXV_E_ARG;
    jlong v[3];
    (*env)->GetLongArrayRegion(env, t, 0, 3, v);
    uint8_t *tok = (uint8_t *) malloc(ZXV_W4_CONSENT_BYTES);
    if (!tok) return ZXV_E_NOMEM;
    int32_t rc = zxv_w4_consent_issue(CH(zxv_cap, cap), CH(zxv_w4_agent, human), ai, ac, (uint32_t) scope,
                                      (uint64_t) v[0], (uint64_t) v[1], (uint64_t) v[2], np, tok);
    if (rc == ZXV_OK) set_bytes(env, outToken, ZXV_W4_CONSENT_BYTES, tok);
    free(tok);
    return rc;
}

/* out = {scope, max_vfv, issued_ms, expires_ms}. */
JNIEXPORT jint JNICALL FN15(w4ConsentInfo)(JARGS, jbyteArray token, jbyteArray humanId, jbyteArray agentId,
                                           jbyteArray action, jlongArray out, jbyteArray nonce)
{
    if (!len_is(env, humanId, 32) || !len_is(env, agentId, 32) || !len_is(env, action, 32) || !len_is(env, out, 4) ||
        !len_is(env, nonce, 16))
        return ZXV_E_ARG;
    uint8_t *tok = (uint8_t *) malloc(ZXV_W4_CONSENT_BYTES);
    if (!tok) return ZXV_E_NOMEM;
    int32_t rc = ZXV_E_ARG;
    if (fix_in(env, token, ZXV_W4_CONSENT_BYTES, tok)) {
        uint8_t hi[32], ai[32], ac[32], nc[16];
        uint32_t sc = 0;
        uint64_t mx = 0, is = 0, ex = 0;
        rc = zxv_w4_consent_info(tok, hi, ai, ac, &sc, &mx, &is, &ex, nc);
        if (rc == ZXV_OK) {
            set_bytes(env, humanId, 32, hi);
            set_bytes(env, agentId, 32, ai);
            set_bytes(env, action, 32, ac);
            set_bytes(env, nonce, 16, nc);
            jlong v[4] = {sc, (jlong) mx, (jlong) is, (jlong) ex};
            set_longs(env, out, 4, v);
        }
    }
    free(tok);
    return rc;
}

JNIEXPORT jint JNICALL FN15(w4ConsentLogCreate)(JARGS, jlong capacity, jlongArray out)
{
    if (!u32_ok(capacity) || !len_is(env, out, 1)) return ZXV_E_ARG;
    zxv_w4_consent_log *l = NULL;
    int32_t rc = zxv_w4_consent_log_create((uint32_t) capacity, &l);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) (intptr_t) l);
    return rc;
}

JNIEXPORT void JNICALL FN15(w4ConsentLogDestroy)(JARGS, jlong l) { zxv_w4_consent_log_destroy(H(zxv_w4_consent_log, l)); }

/* token may be NULL (no token: ZXV_E_CONSENT). t = {need, amount, now_ms}. */
JNIEXPORT jint JNICALL FN15(w4ConsentCheck)(JARGS, jlong l, jbyteArray token, jbyteArray humanPk, jbyteArray agentId,
                                            jbyteArray action, jlongArray t)
{
    uint8_t ai[32], ac[32];
    if (!fix_in(env, agentId, 32, ai) || !fix_in(env, action, 32, ac) || !len_is(env, t, 3)) return ZXV_E_ARG;
    if (token && !len_is(env, token, ZXV_W4_CONSENT_BYTES)) return ZXV_E_ARG;
    jlong v[3];
    (*env)->GetLongArrayRegion(env, t, 0, 3, v);
    if (!u32_ok(v[0])) return ZXV_E_ARG;
    uint8_t *mem = (uint8_t *) malloc(ZXV_W4_PK_BYTES + ZXV_W4_CONSENT_BYTES);
    if (!mem) return ZXV_E_NOMEM;
    uint8_t *pk = mem, *tok = mem + ZXV_W4_PK_BYTES;
    int32_t rc = ZXV_E_ARG;
    if (fix_in(env, humanPk, ZXV_W4_PK_BYTES, pk)) {
        if (token) (*env)->GetByteArrayRegion(env, token, 0, ZXV_W4_CONSENT_BYTES, (jbyte *) tok);
        rc = zxv_w4_consent_check(H(zxv_w4_consent_log, l), token ? tok : NULL, pk, ai, ac, (uint32_t) v[0],
                                  (uint64_t) v[1], (uint64_t) v[2]);
    }
    free(mem);
    return rc;
}

JNIEXPORT jint JNICALL FN15(w4ConsentRevoke)(JARGS, jlong l, jbyteArray token, jlong nowMs)
{
    uint8_t *tok = (uint8_t *) malloc(ZXV_W4_CONSENT_BYTES);
    if (!tok) return ZXV_E_NOMEM;
    int32_t rc = ZXV_E_ARG;
    if (fix_in(env, token, ZXV_W4_CONSENT_BYTES, tok))
        rc = zxv_w4_consent_revoke(H(zxv_w4_consent_log, l), tok, (uint64_t) nowMs);
    free(tok);
    return rc;
}

JNIEXPORT jint JNICALL FN15(w4ConsentLogPrune)(JARGS, jlong l, jlong nowMs, jlongArray out)
{
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    uint32_t r = 0;
    int32_t rc = zxv_w4_consent_log_prune(H(zxv_w4_consent_log, l), (uint64_t) nowMs, &r);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) r);
    return rc;
}

/* u = {req_id, chunk, flags, now_ms}; consentToken may be NULL. */
JNIEXPORT jint JNICALL FN15(w4EnvSeal)(JARGS, jlong cap, jlong agent, jint kind, jbyteArray to, jlongArray u,
                                       jbyteArray tool, jbyteArray payload, jbyteArray consentToken, jbyteArray buf,
                                       jlongArray outLen)
{
    uint8_t t[32];
    if (!fix_in(env, to, 32, t) || !len_is(env, u, 4) || !payload) return ZXV_E_ARG;
    if (consentToken && !len_is(env, consentToken, ZXV_W4_CONSENT_BYTES)) return ZXV_E_ARG;
    jlong v[4];
    (*env)->GetLongArrayRegion(env, u, 0, 4, v);
    if (!u32_ok(v[1]) || !u32_ok(v[2])) return ZXV_E_ARG;
    int ok;
    char *tl = cstr_in(env, tool, &ok);
    if (!ok || !tl) return ZXV_E_ARG;
    jb p = {0}, ct = {0};
    int32_t rc = ZXV_E_NOMEM;
    if (jb_in(env, payload, &p) && jb_in(env, consentToken, &ct)) {
        R4(env, buf, outLen, rc,
           zxv_w4_env_seal(CH(zxv_cap, cap), H(zxv_w4_agent, agent), (int32_t) kind, t, (uint64_t) v[0],
                           (uint32_t) v[1], tl, (uint32_t) v[2], p.p, p.n, ct.p, (uint64_t) v[3], b_, c_, &need_));
    }
    cstr_free(tl);
    jb_free(&p);
    jb_free(&ct);
    return rc;
}

/* u = {chunk, flags, now_ms}. */
JNIEXPORT jint JNICALL FN15(w4EnvReply)(JARGS, jlong cap, jlong agent, jbyteArray request, jint kind, jlongArray u,
                                        jbyteArray payload, jbyteArray buf, jlongArray outLen)
{
    if (!request || !len_is(env, u, 3) || !payload) return ZXV_E_ARG;
    jlong v[3];
    (*env)->GetLongArrayRegion(env, u, 0, 3, v);
    if (!u32_ok(v[0]) || !u32_ok(v[1])) return ZXV_E_ARG;
    jb r = {0}, p = {0};
    int32_t rc = ZXV_E_NOMEM;
    if (jb_in(env, request, &r) && jb_in(env, payload, &p)) {
        R4(env, buf, outLen, rc,
           zxv_w4_env_reply(CH(zxv_cap, cap), H(zxv_w4_agent, agent), r.p, r.n, (int32_t) kind, (uint32_t) v[0],
                            (uint32_t) v[1], p.p, p.n, (uint64_t) v[2], b_, c_, &need_));
    }
    jb_free(&r);
    jb_free(&p);
    return rc;
}

/* out = {kind, flags, seq, ts_ms, req_id, chunk, payload_len}; tool[32] NUL-terminated. */
JNIEXPORT jint JNICALL FN15(w4EnvDecode)(JARGS, jbyteArray envl, jlongArray out, jbyteArray from, jbyteArray to,
                                         jbyteArray tool)
{
    if (!envl || !len_is(env, out, 7) || !len_is(env, from, 32) || !len_is(env, to, 32) || !len_is(env, tool, 32))
        return ZXV_E_ARG;
    jb e;
    if (!jb_in(env, envl, &e)) return ZXV_E_NOMEM;
    int32_t kind = 0;
    uint32_t flags = 0, chunk = 0, plen = 0;
    uint8_t f[32], t[32];
    uint64_t seq = 0, ts = 0, req = 0;
    char tl[32] = {0};
    int32_t rc = zxv_w4_env_decode(e.p, e.n, &kind, &flags, f, t, &seq, &ts, &req, &chunk, tl, &plen);
    jb_free(&e);
    if (rc == ZXV_OK) {
        jlong v[7] = {kind, flags, (jlong) seq, (jlong) ts, (jlong) req, chunk, plen};
        set_longs(env, out, 7, v);
        set_bytes(env, from, 32, f);
        set_bytes(env, to, 32, t);
        set_bytes(env, tool, 32, (const uint8_t *) tl);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(w4EnvPayload)(JARGS, jbyteArray envl, jbyteArray buf, jlongArray outLen)
{
    if (!envl) return ZXV_E_ARG;
    jb e;
    if (!jb_in(env, envl, &e)) return ZXV_E_NOMEM;
    int32_t rc;
    R4(env, buf, outLen, rc, zxv_w4_env_payload(e.p, e.n, b_, c_, &need_));
    jb_free(&e);
    return rc;
}

JNIEXPORT jint JNICALL FN15(w4EnvConsent)(JARGS, jbyteArray envl, jbyteArray outToken)
{
    if (!envl || !len_is(env, outToken, ZXV_W4_CONSENT_BYTES)) return ZXV_E_ARG;
    jb e;
    if (!jb_in(env, envl, &e)) return ZXV_E_NOMEM;
    uint8_t *tok = (uint8_t *) malloc(ZXV_W4_CONSENT_BYTES);
    int32_t rc = ZXV_E_NOMEM;
    if (tok) {
        rc = zxv_w4_env_consent(e.p, e.n, tok);
        if (rc == ZXV_OK) set_bytes(env, outToken, ZXV_W4_CONSENT_BYTES, tok);
        free(tok);
    }
    jb_free(&e);
    return rc;
}

JNIEXPORT jint JNICALL FN15(w4EnvVerify)(JARGS, jbyteArray envl, jbyteArray senderPk)
{
    uint8_t pk[ZXV_W4_PK_BYTES];
    if (!envl || !fix_in(env, senderPk, ZXV_W4_PK_BYTES, pk)) return ZXV_E_ARG;
    jb e;
    if (!jb_in(env, envl, &e)) return ZXV_E_NOMEM;
    int32_t rc = zxv_w4_env_verify(e.p, e.n, pk);
    jb_free(&e);
    return rc;
}

/* ===== paradox ===== */

JNIEXPORT jint JNICALL FN15(pdxSignature)(JARGS, jbyteArray data, jbyteArray out)
{
    if (!data || !len_is(env, out, 32)) return ZXV_E_ARG;
    jb d;
    if (!jb_in(env, data, &d)) return ZXV_E_NOMEM;
    uint8_t s[32];
    int32_t rc = zxv_paradox_signature(d.p, d.n, s);
    jb_free(&d);
    if (rc == ZXV_OK) set_bytes(env, out, 32, s);
    return rc;
}

JNIEXPORT jint JNICALL FN15(pdxKindOfTag)(JARGS, jint tag, jintArray out)
{
    if (!u8_ok(tag) || !len_is(env, out, 1)) return ZXV_E_ARG;
    int32_t k = 0;
    int32_t rc = zxv_paradox_kind_of_tag((uint8_t) tag, &k);
    if (rc == ZXV_OK) put_int(env, out, 0, k);
    return rc;
}

JNIEXPORT jint JNICALL FN15(pdxAction)(JARGS, jint kind, jint tag, jintArray out)
{
    if (!u8_ok(tag) || !len_is(env, out, 1)) return ZXV_E_ARG;
    int32_t d = 0;
    int32_t rc = zxv_paradox_action((int32_t) kind, (uint8_t) tag, &d);
    if (rc == ZXV_OK) put_int(env, out, 0, d);
    return rc;
}

JNIEXPORT jint JNICALL FN15(pdxOutcomeAction)(JARGS, jint outcome, jintArray out)
{
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    int32_t a = 0;
    int32_t rc = zxv_paradox_outcome_action((int32_t) outcome, &a);
    if (rc == ZXV_OK) put_int(env, out, 0, a);
    return rc;
}

JNIEXPORT jint JNICALL FN15(pdxRepairVerify)(JARGS, jbyteArray proof)
{
    uint8_t p[ZXV_PDX_REPAIR_BYTES];
    if (!fix_in(env, proof, ZXV_PDX_REPAIR_BYTES, p)) return ZXV_E_ARG;
    return zxv_paradox_repair_verify(p);
}

/* Static ASCII from libzxv, so NewStringUTF is exact here. */
JNIEXPORT jstring JNICALL FN15(pdxKindName)(JARGS, jint kind)
{
    return (*env)->NewStringUTF(env, zxv_paradox_kind_name((int32_t) kind));
}

JNIEXPORT jstring JNICALL FN15(pdxOutcomeName)(JARGS, jint outcome)
{
    return (*env)->NewStringUTF(env, zxv_paradox_outcome_name((int32_t) outcome));
}

JNIEXPORT jstring JNICALL FN15(pdxNodeStateName)(JARGS, jint state)
{
    return (*env)->NewStringUTF(env, zxv_paradox_node_state_name((int32_t) state));
}

JNIEXPORT jint JNICALL FN15(pdxCreate)(JARGS, jlong cap, jlong logCapacity, jlong registryCapacity, jlongArray out)
{
    if (!u32_ok(logCapacity) || !u32_ok(registryCapacity) || !len_is(env, out, 1)) return ZXV_E_ARG;
    zxv_paradox *b = NULL;
    int32_t rc = zxv_paradox_create(CH(zxv_cap, cap), (uint32_t) logCapacity, (uint32_t) registryCapacity, &b);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) (intptr_t) b);
    return rc;
}

JNIEXPORT void JNICALL FN15(pdxDestroy)(JARGS, jlong b) { zxv_paradox_destroy(H(zxv_paradox, b)); }

/* book may be 0 (no registry lookup); evidence {pos0, neg0, pos1, neg1} and sig may be NULL.
 * out = {kind, tag, reg_index}. */
JNIEXPORT jint JNICALL FN15(pdxClassify)(JARGS, jlong book, jint tag, jlongArray evidence, jint stale, jbyteArray sig,
                                         jlongArray out)
{
    uint8_t s[32];
    const uint8_t *sp;
    if (!u8_ok(tag) || !fix_opt(env, sig, 32, s, &sp) || !len_is(env, out, 3)) return ZXV_E_ARG;
    uint32_t ev[4];
    const uint32_t *ep = NULL;
    if (evidence) {
        if (!len_is(env, evidence, 4)) return ZXV_E_ARG;
        jlong e[4];
        (*env)->GetLongArrayRegion(env, evidence, 0, 4, e);
        for (int i = 0; i < 4; i++) {
            if (!u32_ok(e[i])) return ZXV_E_ARG;
            ev[i] = (uint32_t) e[i];
        }
        ep = ev;
    }
    int32_t k = 0, ri = 0;
    uint8_t ot = 0;
    int32_t rc = zxv_paradox_classify(CH(zxv_paradox, book), (uint8_t) tag, ep, (int32_t) stale, sp, &k, &ot, &ri);
    if (rc == ZXV_OK) {
        jlong v[3] = {k, ot, ri};
        set_longs(env, out, 3, v);
    }
    return rc;
}

/* f = {reason, flags} (uint8 each). */
JNIEXPORT jint JNICALL FN15(pdxRegistryAdd)(JARGS, jlong cap, jlong book, jbyteArray sig, jint kind, jint reason,
                                            jint flags, jint ownerOk, jlongArray out)
{
    uint8_t s[32];
    if (!fix_in(env, sig, 32, s) || !u8_ok(reason) || !u8_ok(flags) || !len_is(env, out, 1)) return ZXV_E_ARG;
    uint32_t i = 0;
    int32_t rc = zxv_paradox_registry_add(CH(zxv_cap, cap), H(zxv_paradox, book), s, (int32_t) kind, (uint8_t) reason,
                                          (uint8_t) flags, (int32_t) ownerOk, &i);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) i);
    return rc;
}

JNIEXPORT jint JNICALL FN15(pdxRegistryFind)(JARGS, jlong book, jbyteArray sig, jlongArray out)
{
    uint8_t s[32];
    if (!fix_in(env, sig, 32, s) || !len_is(env, out, 1)) return ZXV_E_ARG;
    uint32_t i = 0;
    int32_t rc = zxv_paradox_registry_find(CH(zxv_paradox, book), s, &i);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) i);
    return rc;
}

JNIEXPORT jint JNICALL FN15(pdxRegistryCount)(JARGS, jlong book, jlongArray out)
{
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    uint32_t n = 0;
    int32_t rc = zxv_paradox_registry_count(CH(zxv_paradox, book), &n);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) n);
    return rc;
}

/* out = {kind, reason, flags, src}. */
JNIEXPORT jint JNICALL FN15(pdxRegistryAt)(JARGS, jlong book, jlong index, jbyteArray sig, jlongArray out)
{
    if (!u32_ok(index) || !len_is(env, sig, 32) || !len_is(env, out, 4)) return ZXV_E_ARG;
    uint8_t s[32], reason = 0, flags = 0;
    int32_t k = 0;
    uint64_t src = 0;
    int32_t rc = zxv_paradox_registry_at(CH(zxv_paradox, book), (uint32_t) index, s, &k, &reason, &flags, &src);
    if (rc == ZXV_OK) {
        set_bytes(env, sig, 32, s);
        jlong v[4] = {k, reason, flags, (jlong) src};
        set_longs(env, out, 4, v);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(pdxLogCount)(JARGS, jlong book, jint which, jlongArray out)
{
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    uint32_t n = 0;
    int32_t rc = zxv_paradox_log_count(CH(zxv_paradox, book), (int32_t) which, &n);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) n);
    return rc;
}

JNIEXPORT jint JNICALL FN15(pdxLogHead)(JARGS, jlong book, jint which, jbyteArray out)
{
    if (!len_is(env, out, 32)) return ZXV_E_ARG;
    uint8_t h[32];
    int32_t rc = zxv_paradox_log_head(CH(zxv_paradox, book), (int32_t) which, h);
    if (rc == ZXV_OK) set_bytes(env, out, 32, h);
    return rc;
}

JNIEXPORT jint JNICALL FN15(pdxLogRecord)(JARGS, jlong book, jint which, jlong index, jbyteArray buf, jlongArray outLen)
{
    if (!u32_ok(index)) return ZXV_E_ARG;
    int32_t rc;
    R4(env, buf, outLen, rc,
       zxv_paradox_log_record(CH(zxv_paradox, book), (int32_t) which, (uint32_t) index, b_, c_, &need_));
    return rc;
}

JNIEXPORT jint JNICALL FN15(pdxLogVerify)(JARGS, jlong book, jint which)
{
    return zxv_paradox_log_verify(H(zxv_paradox, book), (int32_t) which);
}

/* An int64_t[n] copy of a long[] (NULL stays NULL); *ok = 0 on NOMEM. */
static int64_t *i64_in(JNIEnv *env, jlongArray a, jsize *n, int *ok)
{
    *ok = 1;
    *n = 0;
    if (!a) return NULL;
    *n = (*env)->GetArrayLength(env, a);
    int64_t *p = (int64_t *) malloc(*n > 0 ? (size_t) *n * sizeof(int64_t) : 1);
    if (!p) {
        *ok = 0;
        return NULL;
    }
    if (*n) (*env)->GetLongArrayRegion(env, a, 0, *n, (jlong *) p);
    return p;
}

JNIEXPORT jint JNICALL FN15(pdxPolicyCreate)(JARGS, jlong cap, jlongArray lo, jlongArray hi, jlongArray initial,
                                             jlong flags, jlongArray out)
{
    if (!lo || !hi || !initial || !u32_ok(flags) || !len_is(env, out, 1)) return ZXV_E_ARG;
    jsize n1, n2, n3;
    int ok1, ok2, ok3;
    int64_t *l = i64_in(env, lo, &n1, &ok1), *h = i64_in(env, hi, &n2, &ok2), *in = i64_in(env, initial, &n3, &ok3);
    int32_t rc = ZXV_E_NOMEM;
    if (ok1 && ok2 && ok3) {
        if (n1 != n2 || n1 != n3) {
            rc = ZXV_E_ARG;
        } else {
            zxv_paradox_policy *p = NULL;
            rc = zxv_paradox_policy_create(CH(zxv_cap, cap), (uint32_t) n1, l, h, in, (uint32_t) flags, &p);
            if (rc == ZXV_OK) put_long(env, out, 0, (jlong) (intptr_t) p);
        }
    }
    free(l);
    free(h);
    free(in);
    return rc;
}

JNIEXPORT void JNICALL FN15(pdxPolicyDestroy)(JARGS, jlong p) { zxv_paradox_policy_destroy(H(zxv_paradox_policy, p)); }

/* synth may be NULL (n is then ignored by libzxv). */
JNIEXPORT jint JNICALL FN15(pdxConstructive)(JARGS, jlong cap, jlong book, jlong policy, jint kind, jint tag,
                                             jlongArray synth, jlong n, jlong tension, jintArray out)
{
    if (!u8_ok(tag) || !u32_ok(n) || !u32_ok(tension) || !len_is(env, out, 1)) return ZXV_E_ARG;
    jsize sn;
    int ok;
    int64_t *s = i64_in(env, synth, &sn, &ok);
    if (!ok) return ZXV_E_NOMEM;
    if (s && (jlong) sn != n) {
        free(s);
        return ZXV_E_ARG;
    }
    int32_t o = 0;
    int32_t rc = zxv_paradox_constructive(CH(zxv_cap, cap), H(zxv_paradox, book), H(zxv_paradox_policy, policy),
                                          (int32_t) kind, (uint8_t) tag, s, (uint32_t) n, (uint32_t) tension, &o);
    free(s);
    if (rc == ZXV_OK) put_int(env, out, 0, o);
    return rc;
}

/* outVal/outPrev hold n values each (either may be NULL). out = {armed, tension_at}. */
JNIEXPORT jint JNICALL FN15(pdxPolicyValues)(JARGS, jlong policy, jlongArray outVal, jlongArray outPrev, jlong n,
                                             jlongArray out)
{
    if (!u32_ok(n) || n > ZXV_PDX_POLICY_MAX || !len_is(env, out, 2)) return ZXV_E_ARG;
    if ((outVal && !len_is(env, outVal, (jsize) n)) || (outPrev && !len_is(env, outPrev, (jsize) n))) return ZXV_E_ARG;
    int64_t v[ZXV_PDX_POLICY_MAX], p[ZXV_PDX_POLICY_MAX];
    int32_t armed = 0;
    uint32_t ta = 0;
    int32_t rc = zxv_paradox_policy_values(CH(zxv_paradox_policy, policy), outVal ? v : NULL, outPrev ? p : NULL,
                                           (uint32_t) n, &armed, &ta);
    if (rc == ZXV_OK) {
        if (outVal && n) (*env)->SetLongArrayRegion(env, outVal, 0, (jsize) n, (const jlong *) v);
        if (outPrev && n) (*env)->SetLongArrayRegion(env, outPrev, 0, (jsize) n, (const jlong *) p);
        jlong x[2] = {armed, ta};
        set_longs(env, out, 2, x);
    }
    return rc;
}

/* lo/hi hold n values each (either may be NULL). out = {flags}. */
JNIEXPORT jint JNICALL FN15(pdxPolicyBounds)(JARGS, jlong policy, jlongArray lo, jlongArray hi, jlong n,
                                             jlongArray out)
{
    if (!u32_ok(n) || n > ZXV_PDX_POLICY_MAX || !len_is(env, out, 1)) return ZXV_E_ARG;
    if ((lo && !len_is(env, lo, (jsize) n)) || (hi && !len_is(env, hi, (jsize) n))) return ZXV_E_ARG;
    int64_t l[ZXV_PDX_POLICY_MAX], h[ZXV_PDX_POLICY_MAX];
    uint32_t f = 0;
    int32_t rc = zxv_paradox_policy_bounds(CH(zxv_paradox_policy, policy), lo ? l : NULL, hi ? h : NULL, (uint32_t) n,
                                           &f);
    if (rc == ZXV_OK) {
        if (lo && n) (*env)->SetLongArrayRegion(env, lo, 0, (jsize) n, (const jlong *) l);
        if (hi && n) (*env)->SetLongArrayRegion(env, hi, 0, (jsize) n, (const jlong *) h);
        put_long(env, out, 0, (jlong) f);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(pdxPolicyProposal)(JARGS, jlong policy, jlongArray synth, jbyteArray buf, jlongArray outLen)
{
    if (!synth) return ZXV_E_ARG;
    jsize n;
    int ok;
    int64_t *s = i64_in(env, synth, &n, &ok);
    if (!ok) return ZXV_E_NOMEM;
    int32_t rc;
    R4(env, buf, outLen, rc,
       zxv_paradox_policy_proposal(CH(zxv_paradox_policy, policy), s, (uint32_t) n, b_, c_, &need_));
    free(s);
    return rc;
}

/* d = {steps, step_limit, spent, budget, submitter} (uint32 each); state is
 * changed in place (copied back only on ZXV_OK); out = {outcome}; proof[160]
 * is written only when the outcome is CONTAINED. */
JNIEXPORT jint JNICALL FN15(pdxDestructive)(JARGS, jlong cap, jlong book, jint kind, jint tag, jlongArray d,
                                            jlongArray trace, jint invariantOk, jbyteArray state,
                                            jbyteArray checkpoint, jbyteArray pattern, jintArray out, jbyteArray proof)
{
    if (!u8_ok(tag) || !len_is(env, d, 5) || !state || !checkpoint || !len_is(env, out, 1) ||
        !len_is(env, proof, ZXV_PDX_REPAIR_BYTES))
        return ZXV_E_ARG;
    jlong dv[5];
    (*env)->GetLongArrayRegion(env, d, 0, 5, dv);
    for (int i = 0; i < 5; i++)
        if (!u32_ok(dv[i])) return ZXV_E_ARG;
    jsize sl = (*env)->GetArrayLength(env, state);
    if ((*env)->GetArrayLength(env, checkpoint) != sl) return ZXV_E_ARG;
    jb st = {0}, ck = {0}, pt = {0};
    uint64_t *tr = NULL;
    jsize tn = 0;
    int32_t rc = ZXV_E_NOMEM;
    if (!jb_in(env, state, &st) || !jb_in(env, checkpoint, &ck) || !jb_in(env, pattern, &pt)) goto out;
    if (trace) {
        tn = (*env)->GetArrayLength(env, trace);
        tr = (uint64_t *) malloc(tn > 0 ? (size_t) tn * sizeof(uint64_t) : 1);
        if (!tr) goto out;
        longs_in(env, trace, tn, tr);
    }
    {
        int32_t o = 0;
        uint8_t pf[ZXV_PDX_REPAIR_BYTES] = {0};
        rc = zxv_paradox_destructive(CH(zxv_cap, cap), H(zxv_paradox, book), (int32_t) kind, (uint8_t) tag,
                                     (uint32_t) dv[0], (uint32_t) dv[1], (uint32_t) dv[2], (uint32_t) dv[3], tr,
                                     (uint32_t) tn, (int32_t) invariantOk, st.p, ck.p, (uint32_t) sl, pt.p,
                                     (uint32_t) pt.n, (uint32_t) dv[4], &o, pf);
        if (rc == ZXV_OK) {
            put_int(env, out, 0, o);
            if (sl) set_bytes(env, state, sl, st.p);
            if (o == ZXV_PDX_OUT_CONTAINED) set_bytes(env, proof, ZXV_PDX_REPAIR_BYTES, pf);
        }
    }
out:
    jb_free(&st);
    jb_free(&ck);
    jb_free(&pt);
    free(tr);
    return rc;
}

JNIEXPORT jint JNICALL FN15(pdxHoldCreate)(JARGS, jlong cap, jlong capacity, jlong decisive, jlongArray out)
{
    if (!u32_ok(capacity) || !u32_ok(decisive) || !len_is(env, out, 1)) return ZXV_E_ARG;
    zxv_paradox_hold *h = NULL;
    int32_t rc = zxv_paradox_hold_create(CH(zxv_cap, cap), (uint32_t) capacity, (uint32_t) decisive, &h);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) (intptr_t) h);
    return rc;
}

JNIEXPORT void JNICALL FN15(pdxHoldDestroy)(JARGS, jlong h) { zxv_paradox_hold_destroy(H(zxv_paradox_hold, h)); }

JNIEXPORT jint JNICALL FN15(pdxHoldAdd)(JARGS, jlong h, jbyteArray id)
{
    uint8_t i[32];
    if (!fix_in(env, id, 32, i)) return ZXV_E_ARG;
    return zxv_paradox_hold_add(H(zxv_paradox_hold, h), i);
}

JNIEXPORT jint JNICALL FN15(pdxHoldEvidence)(JARGS, jlong h, jbyteArray id, jint positive)
{
    uint8_t i[32];
    if (!fix_in(env, id, 32, i)) return ZXV_E_ARG;
    return zxv_paradox_hold_evidence(H(zxv_paradox_hold, h), i, (int32_t) positive);
}

/* out = {count, settled, decisive}. */
JNIEXPORT jint JNICALL FN15(pdxHoldInfo)(JARGS, jlong h, jlongArray out)
{
    if (!len_is(env, out, 3)) return ZXV_E_ARG;
    uint32_t n = 0;
    int32_t s = 0, d = 0;
    int32_t rc = zxv_paradox_hold_info(CH(zxv_paradox_hold, h), &n, &s, &d);
    if (rc == ZXV_OK) {
        jlong v[3] = {n, s, d};
        set_longs(env, out, 3, v);
    }
    return rc;
}

/* out = {pos, neg}. */
JNIEXPORT jint JNICALL FN15(pdxHoldBranch)(JARGS, jlong h, jlong index, jbyteArray id, jlongArray out)
{
    if (!u32_ok(index) || !len_is(env, id, 32) || !len_is(env, out, 2)) return ZXV_E_ARG;
    uint8_t i[32];
    uint32_t p = 0, n = 0;
    int32_t rc = zxv_paradox_hold_branch(CH(zxv_paradox_hold, h), (uint32_t) index, i, &p, &n);
    if (rc == ZXV_OK) {
        set_bytes(env, id, 32, i);
        jlong v[2] = {p, n};
        set_longs(env, out, 2, v);
    }
    return rc;
}

/* out = {outcome, settled}. */
JNIEXPORT jint JNICALL FN15(pdxNeutral)(JARGS, jlong cap, jlong book, jlong hold, jint kind, jint tag, jint ownerPick,
                                        jlongArray out)
{
    if (!u8_ok(tag) || !len_is(env, out, 2)) return ZXV_E_ARG;
    int32_t o = 0, s = 0;
    int32_t rc = zxv_paradox_neutral(CH(zxv_cap, cap), H(zxv_paradox, book), H(zxv_paradox_hold, hold), (int32_t) kind,
                                     (uint8_t) tag, (int32_t) ownerPick, &o, &s);
    if (rc == ZXV_OK) {
        jlong v[2] = {o, s};
        set_longs(env, out, 2, v);
    }
    return rc;
}

/* out = {outcome, rec} (rec is UINT32_MAX when nothing was recorded). */
JNIEXPORT jint JNICALL FN15(pdxSpecial)(JARGS, jlong cap, jlong book, jint kind, jint tag, jbyteArray sig,
                                        jbyteArray subject, jlongArray out)
{
    uint8_t s[32], sb[32];
    if (!u8_ok(tag) || !fix_in(env, sig, 32, s) || !fix_in(env, subject, 32, sb) || !len_is(env, out, 2))
        return ZXV_E_ARG;
    int32_t o = 0;
    uint32_t r = 0;
    int32_t rc = zxv_paradox_special(CH(zxv_cap, cap), H(zxv_paradox, book), (int32_t) kind, (uint8_t) tag, s, sb, &o, &r);
    if (rc == ZXV_OK) {
        jlong v[2] = {o, r};
        set_longs(env, out, 2, v);
    }
    return rc;
}

JNIEXPORT jint JNICALL FN15(pdxDraft)(JARGS, jlong cap, jlong book, jlong rec, jint kind, jint reason, jint flags,
                                      jbyteArray outDraft)
{
    if (!u32_ok(rec) || !u8_ok(reason) || !u8_ok(flags) || !len_is(env, outDraft, ZXV_PDX_DRAFT_BYTES))
        return ZXV_E_ARG;
    uint8_t d[ZXV_PDX_DRAFT_BYTES];
    int32_t rc = zxv_paradox_draft(CH(zxv_cap, cap), H(zxv_paradox, book), (uint32_t) rec, (int32_t) kind,
                                   (uint8_t) reason, (uint8_t) flags, d);
    if (rc == ZXV_OK) set_bytes(env, outDraft, ZXV_PDX_DRAFT_BYTES, d);
    return rc;
}

JNIEXPORT jint JNICALL FN15(pdxReview)(JARGS, jlong cap, jlong book, jbyteArray draft, jint ownerOk, jint consensusOk,
                                       jintArray out)
{
    uint8_t d[ZXV_PDX_DRAFT_BYTES];
    if (!fix_in(env, draft, ZXV_PDX_DRAFT_BYTES, d) || !len_is(env, out, 1)) return ZXV_E_ARG;
    int32_t o = 0;
    int32_t rc = zxv_paradox_review(CH(zxv_cap, cap), H(zxv_paradox, book), d, (int32_t) ownerOk,
                                    (int32_t) consensusOk, &o);
    if (rc == ZXV_OK) put_int(env, out, 0, o);
    return rc;
}

JNIEXPORT jint JNICALL FN15(pdxGraphCreate)(JARGS, jlong cap, jlong stepLimit, jlongArray out)
{
    if (!u32_ok(stepLimit) || !len_is(env, out, 1)) return ZXV_E_ARG;
    zxv_paradox_graph *g = NULL;
    int32_t rc = zxv_paradox_graph_create(CH(zxv_cap, cap), (uint32_t) stepLimit, &g);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) (intptr_t) g);
    return rc;
}

JNIEXPORT void JNICALL FN15(pdxGraphDestroy)(JARGS, jlong g) { zxv_paradox_graph_destroy(H(zxv_paradox_graph, g)); }

JNIEXPORT jint JNICALL FN15(pdxGraphAddNode)(JARGS, jlong g, jbyteArray id, jint kind, jint tag, jlong mag, jint dir)
{
    uint8_t i[32];
    if (!fix_in(env, id, 32, i) || !u8_ok(tag) || !u32_ok(mag)) return ZXV_E_ARG;
    return zxv_paradox_graph_add_node(H(zxv_paradox_graph, g), i, (int32_t) kind, (uint8_t) tag, (uint32_t) mag,
                                      (int32_t) dir);
}

JNIEXPORT jint JNICALL FN15(pdxGraphAddDep)(JARGS, jlong g, jbyteArray id, jbyteArray dep)
{
    uint8_t i[32], d[32];
    if (!fix_in(env, id, 32, i) || !fix_in(env, dep, 32, d)) return ZXV_E_ARG;
    return zxv_paradox_graph_add_dep(H(zxv_paradox_graph, g), i, d);
}

JNIEXPORT jint JNICALL FN15(pdxGraphUpdate)(JARGS, jlong g, jbyteArray id, jint kind, jint tag, jlong mag, jint dir)
{
    uint8_t i[32];
    if (!fix_in(env, id, 32, i) || !u8_ok(tag) || !u32_ok(mag)) return ZXV_E_ARG;
    return zxv_paradox_graph_update(H(zxv_paradox_graph, g), i, (int32_t) kind, (uint8_t) tag, (uint32_t) mag,
                                    (int32_t) dir);
}

JNIEXPORT jint JNICALL FN15(pdxGraphPass)(JARGS, jlong cap, jlong book, jlong g, jintArray out)
{
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    int32_t o = 0;
    int32_t rc = zxv_paradox_graph_pass(CH(zxv_cap, cap), H(zxv_paradox, book), H(zxv_paradox_graph, g), &o);
    if (rc == ZXV_OK) put_int(env, out, 0, o);
    return rc;
}

JNIEXPORT jint JNICALL FN15(pdxGraphState)(JARGS, jlong g, jbyteArray id, jintArray out)
{
    uint8_t i[32];
    if (!fix_in(env, id, 32, i) || !len_is(env, out, 1)) return ZXV_E_ARG;
    int32_t s = 0;
    int32_t rc = zxv_paradox_graph_state(CH(zxv_paradox_graph, g), i, &s);
    if (rc == ZXV_OK) put_int(env, out, 0, s);
    return rc;
}

/* out = {kind, state}. */
JNIEXPORT jint JNICALL FN15(pdxGraphNodeAt)(JARGS, jlong g, jlong index, jbyteArray id, jlongArray out)
{
    if (!u32_ok(index) || !len_is(env, id, 32) || !len_is(env, out, 2)) return ZXV_E_ARG;
    uint8_t i[32];
    int32_t k = 0, s = 0;
    int32_t rc = zxv_paradox_graph_node_at(CH(zxv_paradox_graph, g), (uint32_t) index, i, &k, &s);
    if (rc == ZXV_OK) {
        set_bytes(env, id, 32, i);
        jlong v[2] = {k, s};
        set_longs(env, out, 2, v);
    }
    return rc;
}

/* out = {nodes, pass, total_mag, total_dir, escalated}; chain[32]. */
JNIEXPORT jint JNICALL FN15(pdxGraphInfo)(JARGS, jlong g, jlongArray out, jbyteArray chain)
{
    if (!len_is(env, out, 5) || !len_is(env, chain, 32)) return ZXV_E_ARG;
    uint32_t n = 0, p = 0, m = 0;
    int32_t d = 0, e = 0;
    uint8_t c[32];
    int32_t rc = zxv_paradox_graph_info(CH(zxv_paradox_graph, g), &n, &p, &m, &d, &e, c);
    if (rc == ZXV_OK) {
        jlong v[5] = {n, p, m, d, e};
        set_longs(env, out, 5, v);
        set_bytes(env, chain, 32, c);
    }
    return rc;
}
