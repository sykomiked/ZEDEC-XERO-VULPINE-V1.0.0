// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile.internal

import java.io.File

/**
 * Finds and loads libzxv and the JNI shim, then refuses a library this SDK
 * was not written for (SDK_DESIGN section 1, ABI R8).
 *
 * libzxv, in order: the path given to `Zxv.load`; the `zxv.library` system
 * property; the `ZXV_LIBRARY_PATH` environment variable (a file or a
 * directory); then `System.loadLibrary("zxv")`, which on Android is the copy
 * packaged in the APK/AAR under `lib/<abi>/` and on a desktop JVM is
 * `java.library.path`.
 *
 * The shim (libzxvjni), in order: the explicit path; the `zxv.jni.library`
 * property; `ZXV_JNI_LIBRARY_PATH`; a `libzxvjni` file next to the libzxv that
 * was loaded; then `System.loadLibrary("zxvjni")`. libzxv is always loaded
 * first, so the shim's dependency on `libzxv.so` resolves to that copy.
 */
internal object NativeLoader {
    @Volatile private var explicitLib: String? = null
    @Volatile private var explicitJni: String? = null
    @Volatile private var loaded = false
    private var loading = false
    @Volatile private var failure: Throwable? = null
    @Volatile var loadedFrom: String = "(not loaded)"
        private set

    /** True on Android's runtime (ART/Dalvik). */
    val isAndroid: Boolean =
        System.getProperty("java.vm.name").orEmpty().contains("Dalvik", ignoreCase = true) ||
            System.getProperty("java.vendor").orEmpty().contains("Android", ignoreCase = true)

    @Synchronized
    fun setExplicitPaths(libzxv: String?, jni: String?) {
        check(!loaded) { "libzxv is already loaded from $loadedFrom" }
        explicitLib = libzxv
        explicitJni = jni
    }

    @Synchronized
    fun ensureLoaded() {
        // `loading`: load() touches Jni, whose initializer calls back here on
        // the same thread (the monitor is reentrant).
        if (loaded || loading) return
        failure?.let { throw it }
        loading = true
        try {
            load()
            loaded = true
        } catch (t: Throwable) {
            failure = t
            throw t
        } finally {
            loading = false
        }
    }

    private fun load() {
        val libName = System.mapLibraryName("zxv")
        val jniName = System.mapLibraryName("zxvjni")
        val lib = explicitLib
            ?: System.getProperty("zxv.library")
            ?: System.getenv("ZXV_LIBRARY_PATH")?.takeIf { it.isNotEmpty() }
        var libFile: File? = null
        if (lib != null) {
            val f = File(lib).let { if (it.isDirectory) File(it, libName) else it }
            if (!f.isFile) throw UnsatisfiedLinkError("libzxv not found at $f")
            System.load(f.absolutePath)
            libFile = f.absoluteFile
            loadedFrom = f.absolutePath
        } else {
            System.loadLibrary("zxv")
            loadedFrom = if (isAndroid) "the APK (lib/<abi>/$libName)" else "java.library.path ($libName)"
        }

        val jni = explicitJni
            ?: System.getProperty("zxv.jni.library")
            ?: System.getenv("ZXV_JNI_LIBRARY_PATH")?.takeIf { it.isNotEmpty() }
        val sibling = libFile?.parentFile?.let { File(it, jniName) }
        when {
            jni != null -> {
                val f = File(jni).let { if (it.isDirectory) File(it, jniName) else it }
                if (!f.isFile) throw UnsatisfiedLinkError("libzxvjni not found at $f")
                System.load(f.absolutePath)
            }
            sibling != null && sibling.isFile -> System.load(sibling.absolutePath)
            else -> System.loadLibrary("zxvjni")
        }

        val shim = Jni.shimAbi()
        if (shim != Jni.SHIM_ABI) {
            throw UnsatisfiedLinkError("libzxvjni shim ABI $shim, this SDK needs ${Jni.SHIM_ABI}: rebuild jni/")
        }
        val v = IntArray(3)
        val rc = Jni.version(v)
        if (rc != 0) throw UnsatisfiedLinkError("zxv_version failed with status $rc")
        if (v[0] != 1 || v[1] < 6) {
            throw UnsatisfiedLinkError(
                "need libzxv ABI 1.6 or a later 1.x, found ${v[0]}.${v[1]}.${v[2]} at $loadedFrom",
            )
        }
    }
}
