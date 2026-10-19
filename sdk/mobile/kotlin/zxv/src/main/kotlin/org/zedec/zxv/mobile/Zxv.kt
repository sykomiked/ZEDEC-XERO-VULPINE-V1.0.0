// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

import java.util.EnumSet
import org.zedec.zxv.mobile.internal.Jni
import org.zedec.zxv.mobile.internal.Jni15
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
 * Loading refuses a libzxv whose ABI is not 1.6 or a later 1.x.
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

    /** The ABI 1.5 feature groups live in this build (`zxv_features2`). */
    @JvmStatic
    fun features2(): Set<Feature2> {
        val mask = feature2Mask()
        return Feature2.entries.filterTo(EnumSet.noneOf(Feature2::class.java)) { mask and it.bit != 0L }
    }

    /** True if the ABI 1.5 group `feature` is live in this build. */
    @JvmStatic
    fun hasFeature(feature: Feature2): Boolean = feature2Mask() and feature.bit != 0L

    private fun feature2Mask(): Long {
        NativeLoader.ensureLoaded()
        val out = LongArray(1)
        Status.check(Jni15.features2(out), null)
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
    LEDGER_READ, LEDGER_POST, CRYPTO_SIGN, CRYPTO_KEM, AI_INFERENCE, BUDGET, PHASE, ATTEST, SWARM, EVM,

    /** `ZXV_CAP_HW_MATRIX` (1 << 10). The hardware groups are not bound on mobile; the bit keeps the ordinals aligned. */
    HW_MATRIX,

    /** `ZXV_CAP_HW_RECONF` (1 << 11). Not bound on mobile, as [HW_MATRIX]. */
    HW_RECONF,

    /** Store blocks and publish functions on an [AipiNode] (`ZXV_CAP_AIPI`, ABI 1.5). */
    AIPI,

    /** Mint into a [TokenWallet] (`ZXV_CAP_MINT`, ABI 1.5). */
    MINT,

    /** Create and act on paradox handles ([ParadoxBook]; `ZXV_CAP_PARADOX`, ABI 1.5). */
    PARADOX,

    /**
     * Play-lane vouchers and the spend meter ([PlaySandbox], [SpendMeter];
     * `ZXV_CAP_VINO`, 1 << 15, ABI 1.6). Building payment requests and
     * verifying receipts need no permission; signing a receipt needs
     * [CRYPTO_SIGN] (the wallet's call, not the app's).
     */
    VINO;

    /** The bit, `1 << ordinal`. */
    val bit: Long get() = 1L shl ordinal

    companion object {
        /**
         * `ZXV_CAP_ALL_1_5`: every bit defined as of ABI 1.5 (bits 0..14).
         * Kept as it was: the ABI only adds bits.
         */
        const val ALL_1_5: Long = (1L shl 15) - 1

        /** `ZXV_CAP_ALL_1_6`: every bit defined as of ABI 1.6 (bits 0..15, adds [VINO]). */
        const val ALL_1_6: Long = (1L shl 16) - 1

        /** Every permission this SDK knows (as of ABI 1.6, the mask [ALL_1_6]). */
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

/** An ABI 1.5+ feature group (`ZXV_FEAT2_*`, reported by `zxv_features2`; [VINO] is ABI 1.6). */
enum class Feature2(val bit: Long) {
    /** Treaties: COBOL copybook records and Sutra text ([Treaty]). */
    TREATY(0x01),

    /** Content-addressed functions and IPFS CIDs ([Cid], [AipiNode]). */
    AIPI(0x02),

    /** Fibonacci coins, token denominations and octane ([Coins], [Tokens]). */
    COIN(0x04),

    /** The 0.08889% assurance fee and its split ([AssuranceFee]). */
    FEE(0x08),

    /** The V-Bill hub: pegs, quotes, swaps, limit rules ([VHub]). */
    VHUB(0x10),

    /** Erasure-coded freight packets and op streams ([Freight]). */
    FREIGHT(0x20),

    /** Web 4 agents: manifests, directory, consent, envelopes ([AgentKey]). */
    WEB4(0x40),

    /** The paradox engine ([ParadoxBook]). */
    PARADOX(0x80),

    /** Vino Embed: the play lane, payment requests, receipts and the meter ([PlaySandbox]; ABI 1.6). */
    VINO(0x100),
}
