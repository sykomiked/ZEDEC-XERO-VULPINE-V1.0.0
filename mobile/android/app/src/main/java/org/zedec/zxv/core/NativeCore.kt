// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.core

import android.os.Handler
import android.os.Looper
import org.zedec.zxv.model.CoreApi
import org.zedec.zxv.model.CoreListener
import org.zedec.zxv.model.Device
import org.zedec.zxv.model.Invitation
import org.zedec.zxv.model.RouteResult

/** JNI bridge to libzxvcore (mobile/core/zxv_mobile.h). Every call must be made on the
 *  main thread; the native side calls back on the same thread. */
class NativeCore(private val transport: (to: ByteArray, frame: ByteArray) -> Int) : CoreApi {
    override var listener: CoreListener? = null
    private val main = Handler(Looper.getMainLooper())

    fun start(seed: ByteArray, name: String, role: Int, flags: Int): Int =
        nativeStart(seed, name, role, flags, 2)

    override fun selfId(): ByteArray = ByteArray(16).also { nativeSelfId(it) }
    override fun createMesh(now: Long) = nativeCreateMesh(now)
    override fun invite(role: Int, flags: Int, now: Long): Invitation? {
        val r = nativeInvite(role, flags, now) ?: return null
        return Invitation(r[0], r[1])
    }
    override fun joinQr(text: String, now: Long) = nativeJoinQr(text, now)
    override fun joinCode(code: String, now: Long) = nativeJoinCode(code, now)
    override fun confirmSas(peer: ByteArray, match: Boolean, now: Long) = nativeConfirmSas(peer, match, now)
    override fun devices(): List<Device> = (0 until nativeDeviceCount()).map { i ->
        val id = ByteArray(16)
        val info = nativeDevice(i, id)
        Device(id, nativeDeviceName(i), info and 0xff, (info shr 8) and 0xff, ((info shr 16) and 0xff) == 2)
    }
    override fun rename(id: ByteArray, name: String, now: Long) = nativeRename(id, name, now)
    override fun revoke(id: ByteArray, now: Long) = nativeRevoke(id, now)
    override fun setCaps(ramMb: Int, compute: Int, batteryPct: Int, charging: Boolean, net: Int, features: Int, now: Long) =
        nativeSetCaps(ramMb, compute, batteryPct, charging, net, features, now)
    override fun localModels(): List<String> = generateSequence(0) { it + 1 }
        .map { nativeLocalModel(it) }.takeWhile { it != null }.filterNotNull().toList()
    override fun route(kind: Int, modelClass: Int, now: Long): RouteResult {
        val t = ByteArray(16)
        return RouteResult(nativeRoute(kind, modelClass, now, t), t)
    }
    override fun prompt(target: ByteArray, text: String, now: Long) = nativePrompt(target, text, now)
    override fun setSetting(key: Int, value: String, now: Long) = nativeSetSetting(key, value, now)
    override fun getSetting(key: Int): String? = nativeGetSetting(key)
    override fun moneyAnswer(approve: Boolean, now: Long) = nativeMoneyAnswer(approve, now)
    override fun assureFee(amountMinor: Long) = nativeAssureFee(amountMinor)
    override fun tick(now: Long) = nativeTick(now)
    fun receive(frame: ByteArray, now: Long) = nativeReceive(frame, now)

    // ---- called from native code (zxv_jni.c) ----
    @Suppress("unused")
    private fun onSend(to: ByteArray, frame: ByteArray): Int = transport(to, frame)
    @Suppress("unused")
    private fun onEvent(kind: Int, peer: ByteArray, sas: Int, value: Long) {
        main.post { listener?.onEvent(kind, peer, sas, value) }
    }
    @Suppress("unused")
    private fun onReply(reqId: Int, data: ByteArray, final: Boolean) {
        val text = String(data, Charsets.UTF_8)
        main.post { listener?.onReply(reqId, text, final) }
    }
    @Suppress("unused")
    private fun onMoney(amount: Long, rail: Int, memo: String) {
        main.post { listener?.onMoney(amount, rail, memo) }
    }

    private external fun nativeStart(seed: ByteArray, name: String, role: Int, flags: Int, level: Int): Int
    private external fun nativeSelfId(out: ByteArray): Int
    private external fun nativeCreateMesh(now: Long): Int
    private external fun nativeInvite(role: Int, flags: Int, now: Long): Array<String>?
    private external fun nativeJoinQr(text: String, now: Long): Int
    private external fun nativeJoinCode(code: String, now: Long): Int
    private external fun nativeConfirmSas(peer: ByteArray, match: Boolean, now: Long): Int
    private external fun nativeReceive(frame: ByteArray, now: Long): Int
    private external fun nativeTick(now: Long)
    private external fun nativeDeviceCount(): Int
    private external fun nativeDevice(i: Int, idOut: ByteArray): Int
    private external fun nativeDeviceName(i: Int): String
    private external fun nativeRename(id: ByteArray, name: String, now: Long): Int
    private external fun nativeRevoke(id: ByteArray, now: Long): Int
    private external fun nativeSetCaps(ram: Int, compute: Int, battery: Int, charging: Boolean, net: Int, features: Int, now: Long): Int
    private external fun nativeLocalModel(i: Int): String?
    private external fun nativeRoute(kind: Int, cls: Int, now: Long, target: ByteArray): Int
    private external fun nativePrompt(target: ByteArray, text: String, now: Long): Int
    private external fun nativeSetSetting(key: Int, value: String, now: Long): Int
    private external fun nativeGetSetting(key: Int): String?
    private external fun nativeMoneyAnswer(approve: Boolean, now: Long): Int
    private external fun nativeAssureFee(amount: Long): Long

    companion object {
        init { System.loadLibrary("zxvcore_jni") }
    }
}
