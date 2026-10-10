/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_jni.c — JNI glue between org.zedec.zxv.core.NativeCore and
 * mobile/core/zxv_mobile.h. Every call happens on the thread that calls
 * NativeCore (the main thread); callbacks run on that same thread, so the
 * JNIEnv of the current call is reused for them. */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <jni.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "zxv_mobile.h"

static JavaVM *g_vm;
static jobject g_self; /* global ref to the NativeCore instance */
static jmethodID g_on_send, g_on_event, g_on_reply, g_on_money;

static JNIEnv *env_now(void)
{
    JNIEnv *env = 0;
    if (!g_vm) return 0;
    if ((*g_vm)->GetEnv(g_vm, (void **) &env, JNI_VERSION_1_6) != JNI_OK) return 0;
    return env;
}

static jbyteArray bytes(JNIEnv *env, const uint8_t *p, uint32_t n)
{
    jbyteArray a = (*env)->NewByteArray(env, (jsize) n);
    if (a && n) (*env)->SetByteArrayRegion(env, a, 0, (jsize) n, (const jbyte *) p);
    return a;
}

/* Copy a Java byte[] of exactly n bytes; 0 on success. */
static int take(JNIEnv *env, jbyteArray a, uint8_t *out, jsize n)
{
    if (!a || (*env)->GetArrayLength(env, a) != n) return -1;
    (*env)->GetByteArrayRegion(env, a, 0, n, (jbyte *) out);
    return 0;
}

/* ---- host callbacks ---- */

static void h_random(void *ctx, uint8_t *out, uint32_t n)
{
    (void) ctx;
    uint32_t got = 0;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    while (fd >= 0 && got < n) {
        ssize_t r = read(fd, out + got, n - got);
        if (r <= 0) break;
        got += (uint32_t) r;
    }
    if (fd >= 0) close(fd);
    /* Without randomness keys would be predictable: refuse to continue. */
    if (got != n) __builtin_trap();
}

static int h_send(void *ctx, const uint8_t to[ZXV_ID_BYTES], const uint8_t *frame, uint32_t len)
{
    (void) ctx;
    JNIEnv *env = env_now();
    if (!env || !g_self) return -1;
    jbyteArray jt = bytes(env, to, ZXV_ID_BYTES), jf = bytes(env, frame, len);
    if (!jt || !jf) return -1;
    jint r = (*env)->CallIntMethod(env, g_self, g_on_send, jt, jf);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        r = -1;
    }
    (*env)->DeleteLocalRef(env, jt);
    (*env)->DeleteLocalRef(env, jf);
    return r;
}

static void h_event(void *ctx, int kind, const uint8_t peer[ZXV_ID_BYTES], uint32_t sas,
                    uint64_t value)
{
    (void) ctx;
    JNIEnv *env = env_now();
    if (!env || !g_self) return;
    static const uint8_t zero[ZXV_ID_BYTES];
    jbyteArray jp = bytes(env, peer ? peer : zero, ZXV_ID_BYTES);
    if (!jp) return;
    (*env)->CallVoidMethod(env, g_self, g_on_event, (jint) kind, jp, (jint) sas, (jlong) value);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, jp);
}

static void h_reply(void *ctx, uint32_t req_id, const uint8_t *data, uint32_t len, int final)
{
    (void) ctx;
    JNIEnv *env = env_now();
    if (!env || !g_self) return;
    jbyteArray jd = bytes(env, data, len);
    if (!jd) return;
    (*env)->CallVoidMethod(env, g_self, g_on_reply, (jint) (req_id & 0x7fffffffu), jd,
                           (jboolean) (final != 0));
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, jd);
}

static void h_money(void *ctx, uint64_t amount, uint32_t rail, const char *memo)
{
    (void) ctx;
    JNIEnv *env = env_now();
    if (!env || !g_self) return;
    jstring jm = (*env)->NewStringUTF(env, memo ? memo : "");
    if (!jm) return;
    (*env)->CallVoidMethod(env, g_self, g_on_money, (jlong) amount, (jint) rail, jm);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, jm);
}

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved)
{
    (void) reserved;
    g_vm = vm;
    return JNI_VERSION_1_6;
}

#define FN(name) Java_org_zedec_zxv_core_NativeCore_##name

/* A Java string as bounded modified-UTF-8 in buf; 0 on success. */
static int str(JNIEnv *env, jstring s, char *buf, jsize cap)
{
    if (!s) return -1;
    jsize n = (*env)->GetStringUTFLength(env, s);
    if (n + 1 > cap) return -1;
    (*env)->GetStringUTFRegion(env, s, 0, (*env)->GetStringLength(env, s), buf);
    buf[n] = 0;
    return 0;
}

JNIEXPORT jint JNICALL FN(nativeStart)(JNIEnv *env, jobject self, jbyteArray seed, jstring name,
                                       jint role, jint flags, jint level)
{
    uint8_t s[64];
    char nm[32];
    if (take(env, seed, s, 64) || str(env, name, nm, sizeof nm)) return -1;
    jclass c = (*env)->GetObjectClass(env, self);
    g_on_send = (*env)->GetMethodID(env, c, "onSend", "([B[B)I");
    g_on_event = (*env)->GetMethodID(env, c, "onEvent", "(I[BIJ)V");
    g_on_reply = (*env)->GetMethodID(env, c, "onReply", "(I[BZ)V");
    g_on_money = (*env)->GetMethodID(env, c, "onMoney", "(JILjava/lang/String;)V");
    if (!g_on_send || !g_on_event || !g_on_reply || !g_on_money) return -1;
    if (g_self) (*env)->DeleteGlobalRef(env, g_self);
    g_self = (*env)->NewGlobalRef(env, self);
    zxv_host_t h;
    memset(&h, 0, sizeof h);
    h.random = h_random;
    h.send = h_send;
    h.event = h_event;
    h.reply = h_reply;
    h.money = h_money;
    int r = zxv_start(&h, s, nm, role, flags, level);
    memset(s, 0, sizeof s);
    return r;
}

JNIEXPORT jint JNICALL FN(nativeSelfId)(JNIEnv *env, jobject self, jbyteArray out)
{
    (void) self;
    uint8_t id[ZXV_ID_BYTES];
    int r = zxv_self_id(id);
    if (r == 0 && out && (*env)->GetArrayLength(env, out) == ZXV_ID_BYTES)
        (*env)->SetByteArrayRegion(env, out, 0, ZXV_ID_BYTES, (const jbyte *) id);
    return r;
}

JNIEXPORT jint JNICALL FN(nativeCreateMesh)(JNIEnv *env, jobject self, jlong now)
{
    (void) env;
    (void) self;
    return zxv_create_mesh((uint64_t) now);
}

JNIEXPORT jobjectArray JNICALL FN(nativeInvite)(JNIEnv *env, jobject self, jint role, jint flags,
                                                jlong now)
{
    (void) self;
    static char qr[2048];
    char code[11];
    if (zxv_invite(role, flags, (uint64_t) now, qr, sizeof qr, code) != 0) return 0;
    jclass sc = (*env)->FindClass(env, "java/lang/String");
    jobjectArray a = (*env)->NewObjectArray(env, 2, sc, 0);
    if (!a) return 0;
    (*env)->SetObjectArrayElement(env, a, 0, (*env)->NewStringUTF(env, qr));
    (*env)->SetObjectArrayElement(env, a, 1, (*env)->NewStringUTF(env, code));
    memset(code, 0, sizeof code);
    return a;
}

JNIEXPORT jint JNICALL FN(nativeJoinQr)(JNIEnv *env, jobject self, jstring text, jlong now)
{
    (void) self;
    static char buf[2048];
    if (str(env, text, buf, sizeof buf)) return -1;
    return zxv_join_qr_text(buf, (uint64_t) now);
}

JNIEXPORT jint JNICALL FN(nativeJoinCode)(JNIEnv *env, jobject self, jstring code, jlong now)
{
    (void) self;
    char buf[32];
    if (str(env, code, buf, sizeof buf)) return -1;
    int r = zxv_join_code(buf, (uint64_t) now);
    memset(buf, 0, sizeof buf);
    return r;
}

JNIEXPORT jint JNICALL FN(nativeConfirmSas)(JNIEnv *env, jobject self, jbyteArray peer,
                                            jboolean match, jlong now)
{
    (void) self;
    uint8_t p[ZXV_ID_BYTES];
    if (take(env, peer, p, ZXV_ID_BYTES)) return -1;
    return zxv_confirm_sas(p, match ? 1 : 0, (uint64_t) now);
}

JNIEXPORT jint JNICALL FN(nativeReceive)(JNIEnv *env, jobject self, jbyteArray frame, jlong now)
{
    (void) self;
    static uint8_t buf[98304]; /* DM_FRAME_MAX */
    if (!frame) return -1;
    jsize n = (*env)->GetArrayLength(env, frame);
    if (n <= 0 || (size_t) n > sizeof buf) return -1;
    (*env)->GetByteArrayRegion(env, frame, 0, n, (jbyte *) buf);
    return zxv_receive(buf, (uint32_t) n, (uint64_t) now);
}

JNIEXPORT void JNICALL FN(nativeTick)(JNIEnv *env, jobject self, jlong now)
{
    (void) env;
    (void) self;
    zxv_tick((uint64_t) now);
}

JNIEXPORT jint JNICALL FN(nativeDeviceCount)(JNIEnv *env, jobject self)
{
    (void) env;
    (void) self;
    return zxv_device_count();
}

JNIEXPORT jint JNICALL FN(nativeDevice)(JNIEnv *env, jobject self, jint i, jbyteArray idOut)
{
    (void) self;
    uint8_t id[ZXV_ID_BYTES];
    char name[32];
    int r = zxv_device(i, id, name);
    if (r >= 0 && idOut && (*env)->GetArrayLength(env, idOut) == ZXV_ID_BYTES)
        (*env)->SetByteArrayRegion(env, idOut, 0, ZXV_ID_BYTES, (const jbyte *) id);
    return r;
}

JNIEXPORT jstring JNICALL FN(nativeDeviceName)(JNIEnv *env, jobject self, jint i)
{
    (void) self;
    uint8_t id[ZXV_ID_BYTES];
    char name[33];
    memset(name, 0, sizeof name);
    if (zxv_device(i, id, name) < 0) name[0] = 0;
    /* names are UTF-8 from the roster; replace bytes JNI's modified UTF-8 rejects */
    for (int k = 0; k < 32; k++)
        if ((unsigned char) name[k] >= 0x80) name[k] = '?';
    return (*env)->NewStringUTF(env, name);
}

JNIEXPORT jint JNICALL FN(nativeRename)(JNIEnv *env, jobject self, jbyteArray id, jstring name,
                                        jlong now)
{
    (void) self;
    uint8_t d[ZXV_ID_BYTES];
    char nm[32];
    if (take(env, id, d, ZXV_ID_BYTES) || str(env, name, nm, sizeof nm)) return -1;
    return zxv_rename(d, nm, (uint64_t) now);
}

JNIEXPORT jint JNICALL FN(nativeRevoke)(JNIEnv *env, jobject self, jbyteArray id, jlong now)
{
    (void) self;
    uint8_t d[ZXV_ID_BYTES];
    if (take(env, id, d, ZXV_ID_BYTES)) return -1;
    return zxv_revoke(d, (uint64_t) now);
}

JNIEXPORT jint JNICALL FN(nativeSetCaps)(JNIEnv *env, jobject self, jint ram, jint compute,
                                         jint battery, jboolean charging, jint net, jint features,
                                         jlong now)
{
    (void) env;
    (void) self;
    if (ram < 0 || compute < 0) return -1;
    return zxv_set_caps((uint32_t) ram, (uint32_t) compute, battery, charging ? 1 : 0, net,
                        (uint32_t) features, (uint64_t) now);
}

JNIEXPORT jstring JNICALL FN(nativeLocalModel)(JNIEnv *env, jobject self, jint i)
{
    (void) self;
    const char *n = zxv_local_model(i);
    return n ? (*env)->NewStringUTF(env, n) : 0;
}

JNIEXPORT jint JNICALL FN(nativeRoute)(JNIEnv *env, jobject self, jint kind, jint cls, jlong now,
                                       jbyteArray target)
{
    (void) self;
    uint8_t t[ZXV_ID_BYTES];
    memset(t, 0, sizeof t);
    int r = zxv_route(kind, cls, (uint64_t) now, t);
    if (target && (*env)->GetArrayLength(env, target) == ZXV_ID_BYTES)
        (*env)->SetByteArrayRegion(env, target, 0, ZXV_ID_BYTES, (const jbyte *) t);
    return r;
}

/* Returns the request id (>0) or a negative status. */
JNIEXPORT jint JNICALL FN(nativePrompt)(JNIEnv *env, jobject self, jbyteArray target, jstring text,
                                        jlong now)
{
    (void) self;
    static char buf[16384];
    uint8_t t[ZXV_ID_BYTES];
    if (take(env, target, t, ZXV_ID_BYTES) || str(env, text, buf, sizeof buf)) return -1;
    uint32_t id = 0;
    int r = zxv_prompt(t, buf, (uint64_t) now, &id);
    return r < 0 ? r : (jint) (id & 0x7fffffffu);
}

JNIEXPORT jint JNICALL FN(nativeSetSetting)(JNIEnv *env, jobject self, jint key, jstring value,
                                            jlong now)
{
    (void) self;
    char buf[256];
    if (key <= 0 || key > 0xffff || str(env, value, buf, sizeof buf)) return -1;
    return zxv_set_setting((uint16_t) key, buf, (uint64_t) now);
}

JNIEXPORT jstring JNICALL FN(nativeGetSetting)(JNIEnv *env, jobject self, jint key)
{
    (void) self;
    char buf[256];
    if (key <= 0 || key > 0xffff || zxv_get_setting((uint16_t) key, buf, sizeof buf) < 0) return 0;
    return (*env)->NewStringUTF(env, buf);
}

JNIEXPORT jint JNICALL FN(nativeMoneyAnswer)(JNIEnv *env, jobject self, jboolean approve, jlong now)
{
    (void) env;
    (void) self;
    return zxv_money_answer(approve ? 1 : 0, (uint64_t) now);
}

JNIEXPORT jlong JNICALL FN(nativeAssureFee)(JNIEnv *env, jobject self, jlong amount)
{
    (void) env;
    (void) self;
    return amount < 0 ? -1 : (jlong) zxv_assure_fee((uint64_t) amount);
}
