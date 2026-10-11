// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import java.io.File
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.int
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.long

/** sdk/conformance/vectors.json, the answer key every SDK tests against. */
object Vectors {
    val root: JsonObject by lazy {
        val path = System.getProperty("zxv.vectors") ?: error("set -Dzxv.vectors")
        Json.parseToJsonElement(File(path).readText()).jsonObject
    }

    operator fun get(key: String): JsonElement = root[key] ?: error("vectors.json has no $key")
}

fun JsonElement.obj(key: String): JsonElement = jsonObject[key] ?: error("no $key")
fun JsonElement.arr(): JsonArray = jsonArray
fun JsonElement.i(): Int = jsonPrimitive.int
fun JsonElement.l(): Long = jsonPrimitive.long
fun JsonElement.s(): String = jsonPrimitive.content
fun JsonElement.has(key: String): Boolean = jsonObject.containsKey(key)
fun JsonElement.pair(): Pair<Long, Long> = jsonArray.let { it[0].l() to it[1].l() }

fun hex(s: String): ByteArray = ByteArray(s.length / 2) { s.substring(2 * it, 2 * it + 2).toInt(16).toByte() }
fun ByteArray.toHex(): String = joinToString("") { "%02x".format(it) }
