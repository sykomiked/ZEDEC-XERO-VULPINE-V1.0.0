// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonNull
import kotlinx.serialization.json.JsonPrimitive
import kotlinx.serialization.json.boolean
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive

/** Helpers for the ABI 1.5 vector tests: 64-bit numbers in the vectors are decimal strings. */
object V15 {
    fun sec(name: String): JsonElement = Vectors[name]

    /** A uint64 given as a decimal string (or a small JSON number), as Long bits. */
    fun u64(e: JsonElement): Long {
        val p = e.jsonPrimitive
        return if (p.isString) p.content.toULong().toLong() else p.content.toLong()
    }

    /** A signed 64-bit value given as a decimal string. */
    fun i64(e: JsonElement): Long = e.jsonPrimitive.content.toLong()

    fun str(e: JsonElement?): String? = if (e == null || e is JsonNull) null else e.jsonPrimitive.content

    fun bool(e: JsonElement): Boolean = e.jsonPrimitive.boolean

    fun hexOf(e: JsonElement): ByteArray = hex(e.s())

    fun utf8(e: JsonElement): ByteArray = e.s().toByteArray(Charsets.UTF_8)

    fun utf8(s: String): ByteArray = s.toByteArray(Charsets.UTF_8)

    fun ramp(n: Int): ByteArray = ByteArray(n) { it.toByte() }

    fun fill(value: Int, n: Int): ByteArray = ByteArray(n) { value.toByte() }

    private val codes: Map<String, Int> by lazy {
        listOf("status", "status_abi15").flatMap { t -> Vectors[t].arr().map { it.obj("name").s() to it.obj("code").i() } }.toMap()
    }

    /** The code of a status name, from the vectors' status tables. */
    fun code(name: String): Int = codes[name] ?: error(name)

    /** Runs `f` expecting `status`: ZXV_OK returns its value; any other status must throw a [ZxvException] with that code. */
    fun <T> expect(status: JsonElement?, f: () -> T): T? {
        val s = status?.s() ?: "ZXV_OK"
        if (s == "ZXV_OK") return f()
        val e = assertFailsWith<ZxvException>("expected $s") { f() }
        assertEquals(code(s), e.status, "expected $s, got ${e.statusName}: ${e.message}")
        return null
    }

    fun root(ctx: ZxvContext): Capability = ctx.rootCapability(Permission.all())

    /** `cap` without the given permissions. */
    fun without(cap: Capability, vararg drop: Permission): Capability = cap.attenuate(Permission.all() - drop.toSet())

    fun JsonElement.opt(key: String): JsonElement? = jsonObject[key]

    fun JsonElement.optS(key: String, def: String): String = (jsonObject[key] as? JsonPrimitive)?.content ?: def
}
