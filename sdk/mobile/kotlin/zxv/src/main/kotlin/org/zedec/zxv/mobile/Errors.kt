// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
package org.zedec.zxv.mobile

/** The class of a libzxv status (`zxv_status_class`); unknown negative codes are [SYSTEM]. */
enum class StatusClass {
    OK, USAGE, REFUSED, FATAL, SYSTEM;

    companion object {
        /** The class for a `ZXV_CLASS_*` code. */
        fun fromCode(code: Int): StatusClass = entries.getOrElse(code) { SYSTEM }
    }
}

/**
 * Base of every recoverable error the SDK raises for a negative libzxv
 * status. Branch on the subtype or on [status], never on the text.
 *
 * The terminal case is not here: it is [ZxvFatalError], an [Error].
 */
open class ZxvException(
    /** The negative status code, e.g. -6. */
    val status: Int,
    /** The symbolic name, e.g. `ZXV_E_FUNDS`. */
    val statusName: String,
    /** The diagnostic text from `zxv_last_error` (may be empty). For people, not for branching. */
    val detail: String,
) : RuntimeException(if (detail.isEmpty()) statusName else "$statusName: $detail")

/** A caller bug (USAGE class): fix the call, do not retry it. */
open class ZxvUsageException(status: Int, statusName: String, detail: String) :
    ZxvException(status, statusName, detail)

/**
 * A float reached the exact boundary: NaN or infinity given to
 * [Rational.fromDoubleExact] (`ZXV_E_FLOAT`). Every other number must arrive
 * as integers or exact text.
 */
class FloatRejectedException(detail: String) : ZxvUsageException(-24, "ZXV_E_FLOAT", detail)

/** A well-formed request the platform declined (REFUSED class); it may succeed later. */
open class ZxvRefusedException(status: Int, statusName: String, detail: String) :
    ZxvException(status, statusName, detail)

/** The capability lacks the permission, is revoked, unattested, or belongs to another context (`ZXV_E_CAP`). */
class CapabilityDeniedException(status: Int, statusName: String, detail: String) :
    ZxvRefusedException(status, statusName, detail)

/** An enforced budget would be passed; nothing was consumed (`ZXV_E_BUDGET`). */
class BudgetExhaustedException(status: Int, statusName: String, detail: String) :
    ZxvRefusedException(status, statusName, detail)

/** A holder account would go below zero (`ZXV_E_FUNDS`). */
class InsufficientFundsException(status: Int, statusName: String, detail: String) :
    ZxvRefusedException(status, statusName, detail)

/** A signature or tag did not verify (`ZXV_E_VERIFY`). */
class VerificationFailedException(status: Int, statusName: String, detail: String) :
    ZxvRefusedException(status, statusName, detail)

/** The feature is not built into this libzxv (`ZXV_E_NOTIMPL`); see [Zxv.features]. */
class NotImplementedException(status: Int, statusName: String, detail: String) :
    ZxvRefusedException(status, statusName, detail)

/** The environment failed: memory, I/O, an internal or unknown status (SYSTEM class). */
class ZxvSystemException(status: Int, statusName: String, detail: String) :
    ZxvException(status, statusName, detail)

/**
 * The context halted (FATAL class: a broken invariant, a broken quarantine
 * chain, or [ZxvContext.halt]). Terminal: never retry.
 *
 * It extends [Error], outside the [ZxvException] family, so
 * `catch (e: Exception)` and `catch (e: ZxvException)` do not swallow it.
 * (Kotlin's `runCatching` catches every `Throwable`; do not wrap ZXV calls in
 * it.) The context is poisoned in C as well, so even a caught error cannot
 * be retried: every later call on that context fails the same way. For an
 * exit no code can intercept, create the context with [FatalPolicy.ABORT].
 */
class ZxvFatalError(
    /** The status, `ZXV_E_INVARIANT` (-10) or `ZXV_E_POISONED` (-18). */
    val status: Int,
    /** Its symbolic name. */
    val statusName: String,
    /** The diagnostic text. */
    val detail: String,
) : Error(if (detail.isEmpty()) statusName else "$statusName: $detail")

/** What the SDK does when a context halts. */
enum class FatalPolicy {
    /** Throw [ZxvFatalError]. The default. */
    THROW,

    /**
     * Print the reason to standard error and end the process at once with
     * `Runtime.getRuntime().halt(70)`: no shutdown hooks, no `finally`
     * blocks. On Android the app process dies (the user sees the app close).
     * The only form no host code can catch.
     */
    ABORT,
}
