// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

namespace Zxv.Interop;

/// <summary>
/// Status codes returned by the libzxv C ABI (<c>ZXV_OK</c> / <c>ZXV_E_*</c> in
/// <c>zxv_api.h</c>). Values are stable and never renumbered; new codes are
/// additive, so treat any unknown negative value as a failure.
/// </summary>
#pragma warning disable CA1028 // Enum storage should be Int32: it is Int32; matches int32_t.
public enum ZxvStatus
#pragma warning restore CA1028
{
    /// <summary>The call succeeded (<c>ZXV_OK</c>).</summary>
    Ok = 0,

    /// <summary>A handle or pointer was null or an argument value is invalid (<c>ZXV_E_ARG</c>).</summary>
    InvalidArgument = -1,

    /// <summary>A caller-owned output buffer was too small (<c>ZXV_E_BUFFER</c>). Handled internally by the SDK.</summary>
    BufferTooSmall = -2,

    /// <summary>The feature is not built into this native library yet (<c>ZXV_E_NOTIMPL</c>).</summary>
    NotImplemented = -3,

    /// <summary>Unknown account, card, currency or index (<c>ZXV_E_NOT_FOUND</c>).</summary>
    NotFound = -4,

    /// <summary>A fixed-size native table is full (<c>ZXV_E_CAPACITY</c>).</summary>
    CapacityExceeded = -5,

    /// <summary>A holder account would go below zero (<c>ZXV_E_FUNDS</c>).</summary>
    InsufficientFunds = -6,

    /// <summary>Unknown, disabled, non-ISO or mismatched currency (<c>ZXV_E_CURRENCY</c>).</summary>
    InvalidCurrency = -7,

    /// <summary>Amount is not positive or exceeds the exact range of 2^53 - 1 minor units (<c>ZXV_E_RANGE</c>).</summary>
    AmountOutOfRange = -8,

    /// <summary>The operation is not valid in the current state (<c>ZXV_E_STATE</c>).</summary>
    InvalidState = -9,

    /// <summary>An internal accounting invariant failed (<c>ZXV_E_INVARIANT</c>).</summary>
    InvariantViolated = -10,

    /// <summary>An input string was not valid UTF-8 (<c>ZXV_E_UTF8</c>).</summary>
    InvalidUtf8 = -11,

    /// <summary>An input string exceeds the field's maximum length (<c>ZXV_E_TOO_LONG</c>).</summary>
    TooLong = -12,

    /// <summary>A reference or identifier was already used (<c>ZXV_E_DUPLICATE</c>).</summary>
    Duplicate = -13,

    /// <summary>Native allocation failed (<c>ZXV_E_NOMEM</c>).</summary>
    OutOfMemory = -14,

    /// <summary>A card authorization was declined (<c>ZXV_E_DECLINED</c>). Not a fault.</summary>
    Declined = -15,

    /// <summary>A message field is not valid for the message kind, or a mandatory field is unset (<c>ZXV_E_FIELD</c>).</summary>
    InvalidField = -16,

    /// <summary>The capability handle lacks the right, is revoked, or its attestation is not TRUE (<c>ZXV_E_CAP</c>, ABI 1.1).</summary>
    CapabilityDenied = -17,

    /// <summary>The context halted after a broken invariant; terminal (<c>ZXV_E_POISONED</c>, ABI 1.1).</summary>
    Poisoned = -18,

    /// <summary>An rmag or swarm budget is exhausted (<c>ZXV_E_BUDGET</c>, ABI 1.1).</summary>
    BudgetExhausted = -19,

    /// <summary>A malformed or overflowing rational amount; never retry the same value (<c>ZXV_E_RMAG_INVALID</c>, ABI 1.1).</summary>
    BudgetAmountInvalid = -20,

    /// <summary>A signature or tag did not verify; not a fault (<c>ZXV_E_VERIFY</c>, ABI 1.1).</summary>
    VerificationFailed = -21,

    /// <summary>Malformed input: number text, GGUF file, frame (<c>ZXV_E_FORMAT</c>, ABI 1.1).</summary>
    InvalidFormat = -22,

    /// <summary>A file could not be opened or read (<c>ZXV_E_IO</c>, ABI 1.1).</summary>
    IOError = -23,

    /// <summary>NaN or infinity refused at the float boundary (<c>ZXV_E_FLOAT</c>, ABI 1.1).</summary>
    FloatRejected = -24,

    /// <summary>Unexpected kernel status (<c>ZXV_E_INTERNAL</c>).</summary>
    Internal = -99,
}

/// <summary>
/// The class of a status (<c>zxv_status_class</c>, ABI 1.1). Every ZXV SDK maps
/// failures to exceptions by class, so the same failure has the same shape in
/// every language.
/// </summary>
public enum ZxvStatusClass
{
    /// <summary>Success.</summary>
    Ok = 0,

    /// <summary>A caller bug: fix the arguments; retrying the same call fails again.</summary>
    Usage = 1,

    /// <summary>A well-formed request the platform declined (funds, capability, budget, signature). A retry may succeed after the situation changes.</summary>
    Refused = 2,

    /// <summary>The context is halted and rolled back; never retry.</summary>
    Fatal = 3,

    /// <summary>Out of memory, I/O or an internal fault.</summary>
    System = 4,
}
