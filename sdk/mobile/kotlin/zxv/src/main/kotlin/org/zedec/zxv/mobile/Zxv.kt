// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import java.util.EnumSet
import org.zedec.zxv.mobile.internal.Jni
import org.zedec.zxv.mobile.internal.NativeLoader
import org.zedec.zxv.mobile.internal.Status

/**
 * Library entry point: loading, version, features, status names.
 *
 * On Android nothing needs configuring: `libzxv.so` and `libzxvjni.so` ship
 * in the AAR under `lib/<abi>/` and load on first use. On a desktop JVM, give
 * the paths with [load], the `zxv.library` / `zxv.jni.library` system
 * properties, or `ZXV_LIBRARY_PATH` / `ZXV_JNI_LIBRARY_PATH`.
 *
 * Loading refuses a libzxv whose ABI is not 1.3 or a later 1.x.
 */
object Zxv {
    /** An ABI version. */
    data class Version(val major: Int, val minor: Int, val patch: Int) {
        override fun toString(): String = "$major.$minor.$patch"
    }

    /**
     * Uses the libraries at these paths (files, or directories holding them).
     * Call before anything else touches ZXV.
     *
     * @param libzxv libzxv.so / libzxv.dylib, or null for the default search
     * @param jni libzxvjni, or null for the default search (next to libzxv first)
     * @throws IllegalStateException if the libraries are already loaded
     */
    @JvmStatic
    @JvmOverloads
    fun load(libzxv: String?, jni: String? = null) {
        NativeLoader.setExplicitPaths(libzxv, jni)
        NativeLoader.ensureLoaded()
    }

    /** Where libzxv was loaded from (loads it if needed). */
    @JvmStatic
    val libraryLocation: String
        get() {
            NativeLoader.ensureLoaded()
            return NativeLoader.loadedFrom
        }

    /** The loaded library's ABI version. */
    @JvmStatic
    fun version(): Version {
        NativeLoader.ensureLoaded()
        val v = IntArray(3)
        Status.check(Jni.version(v), null)
        return Version(v[0], v[1], v[2])
    }

    /** The feature groups live in this build (`zxv_features`, R9). */
    @JvmStatic
    fun features(): Set<Feature> {
        val mask = featureMask()
        return Feature.entries.filterTo(EnumSet.noneOf(Feature::class.java)) { mask and it.bit != 0L }
    }

    /** True if `feature` is live in this build. */
    @JvmStatic
    fun hasFeature(feature: Feature): Boolean = featureMask() and feature.bit != 0L

    private fun featureMask(): Long {
        NativeLoader.ensureLoaded()
        val out = LongArray(1)
        Status.check(Jni.features(out), null)
        return out[0]
    }

    /** The symbolic name of a status ("ZXV_E_FUNDS"); "ZXV_E_UNKNOWN" for unknown codes. */
    @JvmStatic
    fun statusName(status: Int): String {
        NativeLoader.ensureLoaded()
        return Status.name(status)
    }

    /** The class of a status; unknown negative codes are SYSTEM. */
    @JvmStatic
    fun statusClass(status: Int): StatusClass {
        NativeLoader.ensureLoaded()
        return Status.cls(status)
    }
}

/** A capability permission bit (`ZXV_CAP_*`, ABI R10). */
enum class Permission {
    LEDGER_READ, LEDGER_POST, CRYPTO_SIGN, CRYPTO_KEM, AI_INFERENCE, BUDGET, PHASE, ATTEST, SWARM, EVM;

    /** The bit, `1 << ordinal`. */
    val bit: Long get() = 1L shl ordinal

    companion object {
        /** Every permission. */
        @JvmStatic
        fun all(): Set<Permission> = EnumSet.allOf(Permission::class.java)

        /** The mask of a set. */
        @JvmStatic
        fun mask(perms: Collection<Permission>): Long = perms.fold(0L) { m, p -> m or p.bit }

        /** The set in a mask (unknown bits ignored). */
        @JvmStatic
        fun fromMask(mask: Long): Set<Permission> =
            entries.filterTo(EnumSet.noneOf(Permission::class.java)) { mask and it.bit != 0L }
    }
}

/** A feature group (`ZXV_FEAT_*`). */
enum class Feature(val bit: Long) {
    LEDGER(0x00000001), MX_PACS008(0x00000002), MX_CAMT053(0x00000004), ISO4217(0x00000008),
    MX_PACS002(0x00000010), MX_PACS004(0x00000020), MX_PACS009(0x00000040), MX_CAMT056(0x00000080),
    MX_CAMT029(0x00000100), NETTING(0x00000200), CARDS(0x00000400), CARD_CHECK(0x00000800),
    VSS(0x00001000), RATIONAL(0x00002000), LPRES(0x00004000), CAPS(0x00008000), RMAG(0x00010000),
    PHASE(0x00020000), WIRE(0x00040000), SWARM(0x00080000), HASH(0x00100000), MLKEM1024(0x00200000),
    MLDSA87(0x00400000), ED25519(0x00800000), TENSOR(0x01000000), LORA(0x02000000), EVM(0x04000000),
    HEX(0x08000000),
}
