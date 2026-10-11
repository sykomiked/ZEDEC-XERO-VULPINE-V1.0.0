// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import java.io.File
import java.lang.reflect.Modifier
import kotlin.test.Test
import kotlin.test.assertEquals
import org.zedec.zxv.mobile.internal.Jni

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
}
