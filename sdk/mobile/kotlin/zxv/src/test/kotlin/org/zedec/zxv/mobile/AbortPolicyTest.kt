// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import java.io.File
import java.util.concurrent.TimeUnit
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

/** Run in a child JVM: FatalPolicy.ABORT must end the process with 70, past every catch and finally. */
object AbortProbe {
    @JvmStatic
    fun main(args: Array<String>) {
        try {
            ZxvContext.create(FatalPolicy.ABORT).use { ctx ->
                val root = ctx.rootCapability(Permission.all())
                ctx.halt("abort probe")
                try {
                    root.info()
                } catch (t: Throwable) {
                    println("CAUGHT")
                }
            }
        } finally {
            println("FINALLY")
        }
        println("SURVIVED")
    }
}

class AbortPolicyTest {
    @Test
    fun abortEndsTheProcess() {
        val java = File(System.getProperty("java.home"), "bin/java").path
        val p = ProcessBuilder(
            java, "-cp", System.getProperty("java.class.path"),
            "-Dzxv.library=" + System.getProperty("zxv.library"),
            "-Dzxv.jni.library=" + System.getProperty("zxv.jni.library"),
            AbortProbe::class.java.name,
        ).redirectErrorStream(true).start()
        assertTrue(p.waitFor(60, TimeUnit.SECONDS))
        val out = p.inputStream.bufferedReader().readText()
        assertEquals(70, p.exitValue(), out)
        assertTrue("ZXV FATAL (FatalPolicy.ABORT)" in out, out)
        assertTrue("CAUGHT" !in out && "FINALLY" !in out && "SURVIVED" !in out, out)
    }
}
