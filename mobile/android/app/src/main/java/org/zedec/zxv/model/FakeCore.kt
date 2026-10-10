// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.model

/** In-memory stand-in for previews and the JVM type-check. It is not the core:
 *  no cryptography, no network. */
class FakeCore : CoreApi {
    override var listener: CoreListener? = null
    private val self = ByteArray(16) { it.toByte() }
    private val list = mutableListOf(Device(self, "This phone", Zxv.ROLE_STANDALONE, Zxv.FLAG_ADMIN or Zxv.FLAG_HELD, false))
    override fun selfId() = self
    override fun createMesh(now: Long) = 0
    override fun invite(role: Int, flags: Int, now: Long) = Invitation("ZXVPEXAMPLEQRTEXT", "7K3QD9XM2A")
    override fun joinQr(text: String, now: Long) = -3
    override fun joinCode(code: String, now: Long) = -3
    override fun confirmSas(peer: ByteArray, match: Boolean, now: Long) = 0
    override fun devices(): List<Device> = list.toList()
    override fun rename(id: ByteArray, name: String, now: Long): Int {
        val i = list.indexOfFirst { it.id.contentEquals(id) }
        if (i >= 0) list[i] = list[i].copy(name = name)
        return 0
    }
    override fun revoke(id: ByteArray, now: Long) = -10
    override fun setCaps(ramMb: Int, compute: Int, batteryPct: Int, charging: Boolean, net: Int, features: Int, now: Long) = 0
    override fun localModels() = listOf("qwen2.5-0.5b-instruct-q4_k_m", "qwen2.5-1.5b-instruct-q4_k_m")
    override fun route(kind: Int, modelClass: Int, now: Long) = RouteResult(Zxv.ROUTE_LOCAL_DEGRADED, self)
    override fun prompt(target: ByteArray, text: String, now: Long) = -12
    override fun setSetting(key: Int, value: String, now: Long) = 0
    override fun getSetting(key: Int): String? = null
    override fun moneyAnswer(approve: Boolean, now: Long) = 0
    /** Same integer formula as pay_assure_fee (0.08889%), for the fake only. */
    override fun assureFee(amountMinor: Long): Long {
        val a = java.math.BigInteger.valueOf(amountMinor)
        return a.multiply(java.math.BigInteger.valueOf(8889)).divide(java.math.BigInteger.valueOf(10_000_000)).toLong()
    }
    override fun tick(now: Long) {}
}
