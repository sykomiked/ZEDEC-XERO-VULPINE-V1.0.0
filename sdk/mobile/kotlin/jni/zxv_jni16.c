/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * zxv_jni16.c - the JNI shim for the libzxv ABI 1.6 group "vino" (Vino
 * Embed: the play lane, payment requests, receipts and the daily meter).
 * Kotlin side: org.zedec.zxv.mobile.internal.Jni16.
 *
 * Every rule at the top of zxv_jni.c and zxv_jni15.c holds here: one
 * mechanical function per libzxv call, handles as jlong, uint32 values
 * range-checked before the cast, uint64 values as jlong bits, fixed-size
 * byte values checked for their exact length, arrays copied in and out
 * (never pinned across a call), out values copied back only when the C
 * call says they were written, and secret keys passed as SecretKey
 * addresses (never Java arrays).
 */
#include "zxvj_common.h"

#pragma GCC diagnostic ignored "-Wunused-parameter"

#define FN16(name) Java_org_zedec_zxv_mobile_internal_Jni16_##name
#define JARGS JNIEnv *env, jclass cls

#define PLAY(p) H(zxv_vino_play, p)
#define CAP(c) CH(zxv_cap, c)

/* Exactly n bytes of a Java byte[] into dst; 0 if NULL or another length. */
static int fix16(JNIEnv *env, jbyteArray a, jsize n, uint8_t *dst)
{
    if (!len_is(env, a, n)) return 0;
    (*env)->GetByteArrayRegion(env, a, 0, n, (jbyte *) dst);
    return 1;
}

static void put_bytes16(JNIEnv *env, jbyteArray a, jsize n, const uint8_t *src)
{
    (*env)->SetByteArrayRegion(env, a, 0, n, (const jbyte *) src);
}

/* ===== the play lane ===== */

JNIEXPORT jint JNICALL FN16(vinoPlayCreate)(JARGS, jlong cap, jlongArray out)
{
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    zxv_vino_play *p = NULL;
    int32_t rc = zxv_vino_play_create(CAP(cap), &p);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) (intptr_t) p);
    return rc;
}

JNIEXPORT void JNICALL FN16(vinoPlayDestroy)(JARGS, jlong p) { zxv_vino_play_destroy(PLAY(p)); }

/* isIssuer is passed through as given: libzxv refuses anything but 0 or 1. */
JNIEXPORT jint JNICALL FN16(vinoPlayOpen)(JARGS, jlong cap, jlong p, jlong owner, jint isIssuer, jlongArray out)
{
    if (!u32_ok(owner) || !len_is(env, out, 1)) return ZXV_E_ARG;
    uint32_t a = 0;
    int32_t rc = zxv_vino_play_open(CAP(cap), PLAY(p), (uint32_t) owner, (int32_t) isIssuer, &a);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) a);
    return rc;
}

JNIEXPORT jint JNICALL FN16(vinoPlayBack)(JARGS, jlong cap, jlong p, jlong issuer, jlong amount)
{
    if (!u32_ok(issuer)) return ZXV_E_ARG;
    return zxv_vino_play_back(CAP(cap), PLAY(p), (uint32_t) issuer, (uint64_t) amount);
}

JNIEXPORT jint JNICALL FN16(vinoPlayUnback)(JARGS, jlong cap, jlong p, jlong issuer, jlong amount)
{
    if (!u32_ok(issuer)) return ZXV_E_ARG;
    return zxv_vino_play_unback(CAP(cap), PLAY(p), (uint32_t) issuer, (uint64_t) amount);
}

JNIEXPORT jint JNICALL FN16(vinoIssue)(JARGS, jlong cap, jlong p, jlong issuer, jlong holder, jlong face, jint rail,
                                       jlongArray out)
{
    if (!u32_ok(issuer) || !u32_ok(holder) || !len_is(env, out, 1)) return ZXV_E_ARG;
    uint64_t id = 0;
    int32_t rc = zxv_vino_issue(CAP(cap), PLAY(p), (uint32_t) issuer, (uint32_t) holder, (uint64_t) face,
                                (int32_t) rail, &id);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) id);
    return rc;
}

JNIEXPORT jint JNICALL FN16(vinoTransfer)(JARGS, jlong cap, jlong p, jlong voucher, jlong from, jlong to, jint rail)
{
    if (!u32_ok(from) || !u32_ok(to)) return ZXV_E_ARG;
    return zxv_vino_transfer(CAP(cap), PLAY(p), (uint64_t) voucher, (uint32_t) from, (uint32_t) to, (int32_t) rail);
}

JNIEXPORT jint JNICALL FN16(vinoSetRail)(JARGS, jlong cap, jlong p, jlong voucher, jlong holder, jint rail)
{
    if (!u32_ok(holder)) return ZXV_E_ARG;
    return zxv_vino_set_rail(CAP(cap), PLAY(p), (uint64_t) voucher, (uint32_t) holder, (int32_t) rail);
}

JNIEXPORT jint JNICALL FN16(vinoRedeem)(JARGS, jlong cap, jlong p, jlong voucher, jlong holder)
{
    if (!u32_ok(holder)) return ZXV_E_ARG;
    return zxv_vino_redeem(CAP(cap), PLAY(p), (uint64_t) voucher, (uint32_t) holder);
}

/* out = {face, issuer, holder, rail, state}. */
JNIEXPORT jint JNICALL FN16(vinoVoucherInfo)(JARGS, jlong cap, jlong p, jlong voucher, jlongArray out)
{
    if (!len_is(env, out, 5)) return ZXV_E_ARG;
    uint64_t face = 0;
    uint32_t is = 0, ho = 0;
    int32_t rail = 0, state = 0;
    int32_t rc = zxv_vino_voucher_info(CAP(cap), PLAY(p), (uint64_t) voucher, &face, &is, &ho, &rail, &state);
    if (rc == ZXV_OK) {
        jlong v[5] = {(jlong) face, (jlong) is, (jlong) ho, (jlong) rail, (jlong) state};
        (*env)->SetLongArrayRegion(env, out, 0, 5, v);
    }
    return rc;
}

/* out = {owner, is_issuer, backing, faces, count}. */
JNIEXPORT jint JNICALL FN16(vinoAccountInfo)(JARGS, jlong cap, jlong p, jlong account, jlongArray out)
{
    if (!u32_ok(account) || !len_is(env, out, 5)) return ZXV_E_ARG;
    uint32_t owner = 0, count = 0;
    int32_t iss = 0;
    uint64_t backing = 0, faces = 0;
    int32_t rc = zxv_vino_account_info(CAP(cap), PLAY(p), (uint32_t) account, &owner, &iss, &backing, &faces, &count);
    if (rc == ZXV_OK) {
        jlong v[5] = {(jlong) owner, (jlong) iss, (jlong) backing, (jlong) faces, (jlong) count};
        (*env)->SetLongArrayRegion(env, out, 0, 5, v);
    }
    return rc;
}

/* The two-call shape: ids == null asks for the count (ZXV_E_BUFFER when it
 * is not 0). outCount[0] is written on ZXV_OK and on ZXV_E_BUFFER; ids is
 * written only on ZXV_OK. */
JNIEXPORT jint JNICALL FN16(vinoVouchers)(JARGS, jlong cap, jlong p, jlong account, jlongArray ids,
                                          jlongArray outCount)
{
    if (!u32_ok(account) || !len_is(env, outCount, 1)) return ZXV_E_ARG;
    size_t n = ids ? (size_t) (*env)->GetArrayLength(env, ids) : 0;
    uint64_t *buf = n ? (uint64_t *) malloc(n * sizeof *buf) : NULL;
    if (n && !buf) return ZXV_E_NOMEM;
    size_t count = 0;
    int32_t rc = zxv_vino_vouchers(CAP(cap), PLAY(p), (uint32_t) account, buf, n, &count);
    if (rc == ZXV_OK && count > 0) (*env)->SetLongArrayRegion(env, ids, 0, (jsize) count, (const jlong *) buf);
    if (rc == ZXV_OK || rc == ZXV_E_BUFFER) put_long(env, outCount, 0, (jlong) count);
    free(buf);
    return rc;
}

JNIEXPORT jint JNICALL FN16(vinoPlayCheck)(JARGS, jlong cap, jlong p) { return zxv_vino_play_check(CAP(cap), PLAY(p)); }

/* ===== payment requests (pure) ===== */

/* memo is a NUL-terminated byte[] (Kotlin terminates it) or null for "".
 * flags is a uint32 carried in jint bits. out and outId are written only on
 * ZXV_OK; outId may be null. */
JNIEXPORT jint JNICALL FN16(vinoRequestEncode)(JARGS, jint lane, jint flags, jbyteArray merchant, jbyteArray nonce,
                                               jlong amount, jlong createdAt, jlong expiresAt, jbyteArray memo,
                                               jbyteArray out, jbyteArray outId)
{
    uint8_t m[32], n[16];
    if (!fix16(env, merchant, 32, m) || !fix16(env, nonce, 16, n) || !len_is(env, out, ZXV_VINO_REQ_BYTES) ||
        (outId && !len_is(env, outId, 32)))
        return ZXV_E_ARG;
    int ok = 1;
    char *memo_c = memo ? cstr_in(env, memo, &ok) : NULL;
    if (!ok) return ZXV_E_ARG;
    uint8_t rec[ZXV_VINO_REQ_BYTES], id[32];
    int32_t rc = zxv_vino_request_encode((int32_t) lane, (uint32_t) flags, m, n, (uint64_t) amount,
                                         (uint64_t) createdAt, (uint64_t) expiresAt, memo_c, rec, id);
    cstr_free(memo_c);
    if (rc == ZXV_OK) {
        put_bytes16(env, out, ZXV_VINO_REQ_BYTES, rec);
        if (outId) put_bytes16(env, outId, 32, id);
    }
    return rc;
}

/* out = {lane, flags (uint32), amount, created_at, expires_at}; merchant[32],
 * nonce[16], memo[65] (NUL-terminated) and id[32]. All written only on ZXV_OK. */
JNIEXPORT jint JNICALL FN16(vinoRequestDecode)(JARGS, jbyteArray in, jlongArray out, jbyteArray merchant,
                                               jbyteArray nonce, jbyteArray memo, jbyteArray id)
{
    uint8_t rec[ZXV_VINO_REQ_BYTES];
    if (!fix16(env, in, ZXV_VINO_REQ_BYTES, rec) || !len_is(env, out, 5) || !len_is(env, merchant, 32) ||
        !len_is(env, nonce, 16) || !len_is(env, memo, ZXV_VINO_MEMO_MAX + 1) || !len_is(env, id, 32))
        return ZXV_E_ARG;
    int32_t lane = 0;
    uint32_t flags = 0;
    uint64_t amount = 0, created = 0, expires = 0;
    uint8_t m[32], n[16], i[32];
    char mm[ZXV_VINO_MEMO_MAX + 1];
    int32_t rc = zxv_vino_request_decode(rec, &lane, &flags, m, n, &amount, &created, &expires, mm, i);
    if (rc == ZXV_OK) {
        jlong v[5] = {(jlong) lane, (jlong) flags, (jlong) amount, (jlong) created, (jlong) expires};
        (*env)->SetLongArrayRegion(env, out, 0, 5, v);
        put_bytes16(env, merchant, 32, m);
        put_bytes16(env, nonce, 16, n);
        put_bytes16(env, memo, ZXV_VINO_MEMO_MAX + 1, (const uint8_t *) mm);
        put_bytes16(env, id, 32, i);
    }
    return rc;
}

/* ===== receipts ===== */

JNIEXPORT jint JNICALL FN16(vinoReceiptMsg)(JARGS, jbyteArray requestId, jbyteArray walletPk, jlong approvedAt,
                                            jbyteArray out)
{
    uint8_t id[32];
    uint8_t *pk = (uint8_t *) malloc(ZXV_MLDSA87_PK_BYTES);
    if (!pk) return ZXV_E_NOMEM;
    int32_t rc = ZXV_E_ARG;
    if (fix16(env, requestId, 32, id) && fix16(env, walletPk, ZXV_MLDSA87_PK_BYTES, pk) &&
        len_is(env, out, ZXV_VINO_RCPT_MSG_BYTES)) {
        uint8_t m[ZXV_VINO_RCPT_MSG_BYTES];
        rc = zxv_vino_receipt_msg(id, pk, (uint64_t) approvedAt, m);
        if (rc == ZXV_OK) put_bytes16(env, out, ZXV_VINO_RCPT_MSG_BYTES, m);
    }
    free(pk);
    return rc;
}

/* walletSk is the address of a SecretKey of ZXV_MLDSA87_SK_BYTES bytes. */
JNIEXPORT jint JNICALL FN16(vinoReceiptSign)(JARGS, jlong cap, jbyteArray request, jlong walletSk,
                                             jbyteArray walletPk, jlong approvedAt, jbyteArray sig)
{
    if (!walletSk || !len_is(env, sig, ZXV_MLDSA87_SIG_BYTES)) return ZXV_E_ARG;
    uint8_t req[ZXV_VINO_REQ_BYTES];
    uint8_t *pk = (uint8_t *) malloc(ZXV_MLDSA87_PK_BYTES + ZXV_MLDSA87_SIG_BYTES);
    if (!pk) return ZXV_E_NOMEM;
    uint8_t *s = pk + ZXV_MLDSA87_PK_BYTES;
    int32_t rc = ZXV_E_ARG;
    if (fix16(env, request, ZXV_VINO_REQ_BYTES, req) && fix16(env, walletPk, ZXV_MLDSA87_PK_BYTES, pk)) {
        rc = zxv_vino_receipt_sign(CAP(cap), req, CH(uint8_t, walletSk), pk, (uint64_t) approvedAt, s);
        if (rc == ZXV_OK) put_bytes16(env, sig, ZXV_MLDSA87_SIG_BYTES, s);
    }
    free(pk);
    return rc;
}

JNIEXPORT jint JNICALL FN16(vinoReceiptVerify)(JARGS, jbyteArray request, jbyteArray walletPk, jlong approvedAt,
                                               jbyteArray sig, jlong now)
{
    uint8_t req[ZXV_VINO_REQ_BYTES];
    uint8_t *pk = (uint8_t *) malloc(ZXV_MLDSA87_PK_BYTES + ZXV_MLDSA87_SIG_BYTES);
    if (!pk) return ZXV_E_NOMEM;
    uint8_t *s = pk + ZXV_MLDSA87_PK_BYTES;
    int32_t rc = ZXV_E_ARG;
    if (fix16(env, request, ZXV_VINO_REQ_BYTES, req) && fix16(env, walletPk, ZXV_MLDSA87_PK_BYTES, pk) &&
        fix16(env, sig, ZXV_MLDSA87_SIG_BYTES, s))
        rc = zxv_vino_receipt_verify(req, pk, (uint64_t) approvedAt, s, (uint64_t) now);
    free(pk);
    return rc;
}

/* ===== the daily meter ===== */

JNIEXPORT jint JNICALL FN16(vinoMeterCreate)(JARGS, jlong cap, jlong dailyCap, jlongArray out)
{
    if (!len_is(env, out, 1)) return ZXV_E_ARG;
    zxv_vino_meter *m = NULL;
    int32_t rc = zxv_vino_meter_create(CAP(cap), (uint64_t) dailyCap, &m);
    if (rc == ZXV_OK) put_long(env, out, 0, (jlong) (intptr_t) m);
    return rc;
}

JNIEXPORT void JNICALL FN16(vinoMeterDestroy)(JARGS, jlong m) { zxv_vino_meter_destroy(H(zxv_vino_meter, m)); }

/* outLeft[0] is written on ZXV_OK and on ZXV_E_LIMIT. */
JNIEXPORT jint JNICALL FN16(vinoMeterAdmit)(JARGS, jlong cap, jlong m, jlong day, jlong amount, jlongArray outLeft)
{
    if (!len_is(env, outLeft, 1)) return ZXV_E_ARG;
    uint64_t left = 0;
    int32_t rc = zxv_vino_meter_admit(CAP(cap), H(zxv_vino_meter, m), (uint64_t) day, (uint64_t) amount, &left);
    if (rc == ZXV_OK || rc == ZXV_E_LIMIT) put_long(env, outLeft, 0, (jlong) left);
    return rc;
}
