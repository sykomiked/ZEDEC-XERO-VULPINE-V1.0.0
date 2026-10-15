// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import java.io.File
import java.lang.reflect.Modifier
import kotlin.test.Test
import kotlin.test.assertEquals
import org.zedec.zxv.mobile.internal.Jni
import org.zedec.zxv.mobile.internal.Jni15
import org.zedec.zxv.mobile.internal.Jni16

/**
 * The Kotlin `external` declarations and the C shim's exported functions
 * name the same set (JNI only resolves a name on first call, so a typo
 * would otherwise surface in production). The C side's calls into libzxv
 * are checked by the compiler against zxv_api.h.
 */
class ShimDriftTest {
    @Test
    fun everyNativeMethodHasAShimFunctionAndBack() {
        val src = File(System.getProperty("zxv.jniSource")).readText()
        val direct = Regex("""FN\((\w+)\)\(""").findAll(src).map { it.groupValues[1] }.toSet()
        val viaMacro = Regex("""RAT_BIN\((\w+),""").findAll(src).map { it.groupValues[1] }.toSet() - "name"
        val c = direct + viaMacro - "name"
        val kotlin = Jni::class.java.declaredMethods.filter { Modifier.isNative(it.modifiers) }.map { it.name }.toSet()
        assertEquals(kotlin.sorted(), c.sorted())
        // Calling each one resolves it; shimAbi() did so already at load.
        assertEquals(Jni.SHIM_ABI, Jni.shimAbi())
    }

    @Test
    fun everyAbi15NativeMethodHasAShimFunctionAndBack() {
        val src = File(System.getProperty("zxv.jni15Source")).readText()
        val direct = Regex("""FN15\((\w+)\)\(""").findAll(src).map { it.groupValues[1] }.toSet()
        val viaMacro = Regex("""PIC_BIN\((\w+),""").findAll(src).map { it.groupValues[1] }.toSet()
        val c = direct + viaMacro - "name"
        val kotlin = Jni15::class.java.declaredMethods.filter { Modifier.isNative(it.modifiers) }.map { it.name }.toSet()
        assertEquals(kotlin.sorted(), c.sorted())
        assertEquals(202, c.size)
    }

    @Test
    fun everyAbi16NativeMethodHasAShimFunctionAndBack() {
        val src = File(System.getProperty("zxv.jni16Source")).readText()
        val c = Regex("""FN16\((\w+)\)\(""").findAll(src).map { it.groupValues[1] }.toSet() - "name"
        val kotlin = Jni16::class.java.declaredMethods.filter { Modifier.isNative(it.modifiers) }.map { it.name }.toSet()
        assertEquals(kotlin.sorted(), c.sorted())
        // Every zxv_vino_* function in the header has a shim function.
        val header = File(System.getProperty("zxv.vectors")).resolve("../../libzxv/include/zxv_api.h").canonicalFile.readText()
        val vino = Regex("""ZXV_API\s+\S+\s+\*?\s*(zxv_vino_\w+)\(""").findAll(header).map { it.groupValues[1] }.toSet()
        val called = Regex("""(zxv_vino_\w+)\(""").findAll(src).map { it.groupValues[1] }.toSet()
        assertEquals(emptySet(), vino - called, "vino functions without a shim")
    }
}
