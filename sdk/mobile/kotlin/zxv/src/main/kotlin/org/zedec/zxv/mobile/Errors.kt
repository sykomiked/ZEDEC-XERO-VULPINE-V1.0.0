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

/**
 * Every libzxv status code by name (`ZXV_OK` and `ZXV_E_*`), for branching
 * on [ZxvException.status]. The numbers are stable: libzxv never renumbers a
 * status and only adds new ones. The class (USAGE, REFUSED, FATAL, SYSTEM)
 * decides the exception type; the codes added in ABI 1.5 (-32..-40) also
 * have their own subclasses (for example [TreatyException] for [E_TREATY]).
 * -41..-45 are reserved by the ABI and not defined here.
 */
object ZxvStatus {
    /** `ZXV_OK`: Success. */
    const val OK: Int = 0

    /** `ZXV_E_ARG`: NULL handle/pointer or invalid argument value. */
    const val E_ARG: Int = -1

    /** `ZXV_E_BUFFER`: Output buffer too small; *out_len = needed. */
    const val E_BUFFER: Int = -2

    /** `ZXV_E_NOTIMPL`: Feature not built into this library yet. */
    const val E_NOTIMPL: Int = -3

    /** `ZXV_E_NOT_FOUND`: Unknown account, card, currency or index. */
    const val E_NOT_FOUND: Int = -4

    /** `ZXV_E_CAPACITY`: A fixed-size table is full. */
    const val E_CAPACITY: Int = -5

    /** `ZXV_E_FUNDS`: Holder account would go below zero. */
    const val E_FUNDS: Int = -6

    /** `ZXV_E_CURRENCY`: Unknown, disabled, non-ISO or mismatched ccy. */
    const val E_CURRENCY: Int = -7

    /** `ZXV_E_RANGE`: Amount <= 0 or beyond the exact range. */
    const val E_RANGE: Int = -8

    /** `ZXV_E_STATE`: Operation not valid in the current state. */
    const val E_STATE: Int = -9

    /** `ZXV_E_INVARIANT`: An internal accounting invariant failed. */
    const val E_INVARIANT: Int = -10

    /** `ZXV_E_UTF8`: Input string is not valid UTF-8. */
    const val E_UTF8: Int = -11

    /** `ZXV_E_TOO_LONG`: Input string exceeds the field's maximum. */
    const val E_TOO_LONG: Int = -12

    /** `ZXV_E_DUPLICATE`: Reference/id already used. */
    const val E_DUPLICATE: Int = -13

    /** `ZXV_E_NOMEM`: Allocation failed. */
    const val E_NOMEM: Int = -14

    /** `ZXV_E_DECLINED`: Card authorization declined (not a fault). */
    const val E_DECLINED: Int = -15

    /** `ZXV_E_FIELD`: Field not valid for this message kind / unset. */
    const val E_FIELD: Int = -16

    /** `ZXV_E_CAP`: The capability lacks the permission, is revoked, or its attestation is not TRUE. */
    const val E_CAP: Int = -17

    /** `ZXV_E_POISONED`: The context halted after a broken invariant; terminal. */
    const val E_POISONED: Int = -18

    /** `ZXV_E_BUDGET`: An rmag or swarm budget is exhausted (denied). */
    const val E_BUDGET: Int = -19

    /** `ZXV_E_RMAG_INVALID`: A malformed or overflowing rational amount; never retry the same value. */
    const val E_RMAG_INVALID: Int = -20

    /** `ZXV_E_VERIFY`: Signature / tag did not verify (not a fault). */
    const val E_VERIFY: Int = -21

    /** `ZXV_E_FORMAT`: Malformed input (number text, GGUF, frame). */
    const val E_FORMAT: Int = -22

    /** `ZXV_E_IO`: A file could not be opened or read. */
    const val E_IO: Int = -23

    /** `ZXV_E_FLOAT`: NaN or infinity refused at the float boundary. */
    const val E_FLOAT: Int = -24

    /** `ZXV_E_INTERNAL`: Unexpected kernel status; see zxv_last_error. */
    const val E_INTERNAL: Int = -99

    /** `ZXV_E_NOPROVIDER`: No proven implementation may serve the call (ABI 1.4). */
    const val E_NOPROVIDER: Int = -25

    /** `ZXV_E_DISPUTE`: A shadow check found two implementations disagree (ABI 1.4). */
    const val E_DISPUTE: Int = -26

    /** `ZXV_E_QUARANTINED`: Image failed in hardware: rolled back, logged (ABI 1.4). */
    const val E_QUARANTINED: Int = -27

    /** `ZXV_E_ROLLBACK`: Image or contract not newer / below the floor (ABI 1.4). */
    const val E_ROLLBACK: Int = -28

    /** `ZXV_E_BLOCKED`: Image already quarantined; never reloaded (ABI 1.4). */
    const val E_BLOCKED: Int = -29

    /** `ZXV_E_TARGET`: Image is for other hardware (ABI 1.4). */
    const val E_TARGET: Int = -30

    /** `ZXV_E_POLICY`: Refused by the function's placement policy (ABI 1.4). */
    const val E_POLICY: Int = -31

    /** `ZXV_E_DENOM`: Not a Fibonacci ladder face (coins, denominations) (ABI 1.5). */
    const val E_DENOM: Int = -32

    /** `ZXV_E_TREATY`: Treaty fails validation; detail in zxv_last_error (ABI 1.5). */
    const val E_TREATY: Int = -33

    /** `ZXV_E_CID`: Content does not match its CID, or bad CID (ABI 1.5). */
    const val E_CID: Int = -34

    /** `ZXV_E_CONSENT`: No valid consent for this agent action (ABI 1.5). */
    const val E_CONSENT: Int = -35

    /** `ZXV_E_ERASURE`: Too few freight rows to rebuild the packet (ABI 1.5). */
    const val E_ERASURE: Int = -36

    /** `ZXV_E_PEG`: No valid signed peg for this currency pair (ABI 1.5). */
    const val E_PEG: Int = -37

    /** `ZXV_E_LIMIT`: Refused by a hub limit rule (not a fault) (ABI 1.5). */
    const val E_LIMIT: Int = -38

    /** `ZXV_E_PARADOX_STEP`: A paradox graph reached its step limit and was escalated to the owner (ABI 1.5). */
    const val E_PARADOX_STEP: Int = -39

    /** `ZXV_E_PARADOX_OWNER`: The action needs the owner's approval (a money or device policy) (ABI 1.5). */
    const val E_PARADOX_OWNER: Int = -40
}

// ---- ABI 1.5 status subclasses ----

/**
 * A face that is not on the Fibonacci ladder (`ZXV_E_DENOM`, -32, USAGE): a coin
 * or token face such as 4, 6, 7 or 100, or zero. Split arbitrary amounts with [Coins.split].
 */
class NotADenominationException(status: Int, statusName: String, detail: String) :
    ZxvUsageException(status, statusName, detail)

/**
 * A treaty fails validation (`ZXV_E_TREATY`, -33, USAGE): fix the treaty; the
 * same input is refused again. [fault] says which rule was broken and, for a
 * parse, [offset] is the byte offset of the first fault (else -1).
 */
class TreatyException @JvmOverloads constructor(
    status: Int,
    statusName: String,
    detail: String,
    /** The rule that was broken ([TreatyFault.UNKNOWN] if the call gave none). */
    val fault: TreatyFault = TreatyFault.UNKNOWN,
    /** The byte offset of the first fault of a parse, or -1. */
    val offset: Long = -1,
) : ZxvUsageException(status, statusName, detail)

/**
 * Content does not match its CID, or the bytes or text are not a canonical CID
 * (`ZXV_E_CID`, -34, REFUSED). A mismatch means the bytes you hold are not the
 * ones the CID names: drop them and fetch again.
 */
class CidException(status: Int, statusName: String, detail: String) :
    ZxvRefusedException(status, statusName, detail)

/**
 * No valid consent for this agent action (`ZXV_E_CONSENT`, -35, REFUSED): no token,
 * one for another human, agent, action, scope or amount, one outside its window,
 * a bad signature, or one already used or revoked. Nothing was allowed.
 */
class ConsentException(status: Int, statusName: String, detail: String) :
    ZxvRefusedException(status, statusName, detail)

/**
 * Too few distinct freight rows arrived to rebuild the payload (`ZXV_E_ERASURE`,
 * -36, REFUSED). Nothing was decoded; add packets again once more rows arrive.
 */
class ErasureException(status: Int, statusName: String, detail: String) :
    ZxvRefusedException(status, statusName, detail)

/**
 * No valid signed peg for this currency pair (`ZXV_E_PEG`, -37, REFUSED): a peg is
 * unset, or a new peg's epoch is not above the current one. Nothing changed.
 */
class PegException(status: Int, statusName: String, detail: String) :
    ZxvRefusedException(status, statusName, detail)

/**
 * A hub limit rule refused the swap (`ZXV_E_LIMIT`, -38, REFUSED): a policy decision,
 * not a fault; nothing was posted. [VHub.nextEpoch] starts every sender at zero again.
 */
class LimitException(status: Int, statusName: String, detail: String) :
    ZxvRefusedException(status, statusName, detail)

/**
 * A paradox graph reached its step limit without resolving (`ZXV_E_PARADOX_STEP`,
 * -39, REFUSED). The pass WAS logged and the graph stays readable; it now goes to the owner.
 */
class StepLimitException(status: Int, statusName: String, detail: String) :
    ZxvRefusedException(status, statusName, detail)

/**
 * The action needs the owner's approval (`ZXV_E_PARADOX_OWNER`, -40, REFUSED): a
 * registry entry flagged MONEY or DEVICE was added without `ownerOk`. Nothing changed.
 * "Owner ok" is the caller's assertion; in one process libzxv cannot check who the owner is.
 */
class OwnerApprovalRequiredException(status: Int, statusName: String, detail: String) :
    ZxvRefusedException(status, statusName, detail)

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
