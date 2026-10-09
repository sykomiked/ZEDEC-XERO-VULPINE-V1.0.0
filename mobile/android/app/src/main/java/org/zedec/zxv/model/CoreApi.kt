// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.model

/** What the UI needs from the mobile core (kernel/src/devmesh via mobile/core/zxv_mobile.h).
 *  The Android app implements it with JNI (core/NativeCore.kt); previews and the JVM
 *  type-check use FakeCore. Every call takes the time in milliseconds. */
interface CoreApi {
    fun selfId(): ByteArray
    fun createMesh(now: Long): Int
    fun invite(role: Int, flags: Int, now: Long): Invitation?
    fun joinQr(text: String, now: Long): Int
    fun joinCode(code: String, now: Long): Int
    fun confirmSas(peer: ByteArray, match: Boolean, now: Long): Int
    fun devices(): List<Device>
    fun rename(id: ByteArray, name: String, now: Long): Int
    fun revoke(id: ByteArray, now: Long): Int
    fun setCaps(ramMb: Int, compute: Int, batteryPct: Int, charging: Boolean, net: Int, features: Int, now: Long): Int
    fun localModels(): List<String>
    fun route(kind: Int, modelClass: Int, now: Long): RouteResult
    fun prompt(target: ByteArray, text: String, now: Long): Int
    fun setSetting(key: Int, value: String, now: Long): Int
    fun getSetting(key: Int): String?
    fun moneyAnswer(approve: Boolean, now: Long): Int
    fun tithe(amountMinor: Long): Long
    fun tick(now: Long)
    var listener: CoreListener?
}

interface CoreListener {
    fun onEvent(kind: Int, peer: ByteArray, sas: Int, value: Long)
    fun onReply(reqId: Int, text: String, final: Boolean)
    fun onMoney(amountMinor: Long, rail: Int, memo: String)
}

data class Invitation(val qrText: String, val code: String)

data class Device(
    val id: ByteArray,
    val name: String,
    val role: Int,
    val flags: Int,
    val revoked: Boolean,
) {
    val idHex: String get() = id.joinToString("") { "%02x".format(it) }
    override fun equals(other: Any?): Boolean = other is Device && other.id.contentEquals(id) &&
        other.name == name && other.role == role && other.flags == flags && other.revoked == revoked
    override fun hashCode(): Int = id.contentHashCode()
}

data class RouteResult(val route: Int, val target: ByteArray)

object Zxv {
    const val ROLE_HOME = 1
    const val ROLE_THIN = 2
    const val ROLE_STANDALONE = 3
    const val FLAG_ADMIN = 1
    const val FLAG_HELD = 2
    const val NET_OFFLINE = 0
    const val NET_LAN = 1
    const val NET_UNMETERED = 2
    const val NET_METERED = 3
    const val ROUTE_REMOTE = 1
    const val ROUTE_LOCAL = 2
    const val ROUTE_LOCAL_DEGRADED = 3
    const val ROUTE_NEED_CAPACITY = 4
    const val ROUTE_UNAVAILABLE = 5
    const val ROUTE_CONFIRM = 6
    const val REQ_PROMPT = 1
    const val MODEL_SMALL = 1
    const val MODEL_LARGE = 2
    const val EV_SAS = 1
    const val EV_PAIRED = 2
    const val EV_ROSTER = 3
    const val EV_REVOKED = 4
    const val EV_SELF_REVOKED = 5
    const val EV_PEER_UP = 6
    const val EV_PEER_DOWN = 7
    const val EV_REQ_FALLBACK = 10
    const val EV_PAIR_FAILED = 11
    const val KEY_THEME = 1
    const val KEY_ALLOW_METERED = 3
    const val RAIL_DEBIT = 555
    const val RAIL_CREDIT = 777
    const val RAIL_EQUITY = 888

    fun railName(rail: Int): String = when (rail) {
        RAIL_DEBIT -> "DEBIT 555"
        RAIL_CREDIT -> "CREDIT 777"
        RAIL_EQUITY -> "EQUITY 888"
        else -> "rail $rail"
    }

    /** VFV has two minor digits: 2500 -> "25.00". Integer only. */
    fun vfv(minor: Long): String {
        val neg = minor < 0
        val a = if (neg) -minor else minor
        val s = "${a / 100}.${(a % 100).toString().padStart(2, '0')}"
        return if (neg) "-$s" else s
    }
}
