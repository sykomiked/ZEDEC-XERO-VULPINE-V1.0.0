// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv;

/// <summary>A USAGE-class failure: the arguments are wrong for this call. Fix the caller; retrying the same call fails again.</summary>
public class ZxvUsageException : ZxvException
{
    /// <summary>Initializes a new instance.</summary>
    public ZxvUsageException()
    {
    }

    /// <summary>Initializes a new instance with a message.</summary>
    /// <param name="message">The error message.</param>
    public ZxvUsageException(string message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance with a message and inner exception.</summary>
    /// <param name="message">The error message.</param>
    /// <param name="innerException">The cause.</param>
    public ZxvUsageException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>Initializes a new instance from a native status.</summary>
    /// <param name="status">The native status code.</param>
    /// <param name="operation">The SDK operation that failed.</param>
    /// <param name="detail">The native last-error text.</param>
    public ZxvUsageException(ZxvStatus status, string operation, string detail)
        : base(status, operation, detail)
    {
    }
}

/// <summary>A NaN or infinity reached the float boundary (<see cref="ZxvStatus.FloatRejected"/>). Only <see cref="Rational.FromDoubleExact(double)"/> accepts a <see cref="double"/>, and only finite ones.</summary>
public sealed class FloatRejectedException : ZxvUsageException
{
    /// <summary>Initializes a new instance.</summary>
    public FloatRejectedException()
    {
    }

    /// <summary>Initializes a new instance with a message.</summary>
    /// <param name="message">The error message.</param>
    public FloatRejectedException(string message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance with a message and inner exception.</summary>
    /// <param name="message">The error message.</param>
    /// <param name="innerException">The cause.</param>
    public FloatRejectedException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>Initializes a new instance from a native status.</summary>
    /// <param name="operation">The SDK operation that failed.</param>
    /// <param name="detail">The native last-error text.</param>
    public FloatRejectedException(string operation, string detail)
        : base(ZxvStatus.FloatRejected, operation, detail)
    {
    }
}

/// <summary>A REFUSED-class failure: a well-formed request the platform declined (funds, capability, budget, signature, a feature not built yet). Handle it; a retry may succeed after the situation changes.</summary>
public class ZxvRefusedException : ZxvException
{
    /// <summary>Initializes a new instance.</summary>
    public ZxvRefusedException()
    {
    }

    /// <summary>Initializes a new instance with a message.</summary>
    /// <param name="message">The error message.</param>
    public ZxvRefusedException(string message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance with a message and inner exception.</summary>
    /// <param name="message">The error message.</param>
    /// <param name="innerException">The cause.</param>
    public ZxvRefusedException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>Initializes a new instance from a native status.</summary>
    /// <param name="status">The native status code.</param>
    /// <param name="operation">The SDK operation that failed.</param>
    /// <param name="detail">The native last-error text.</param>
    public ZxvRefusedException(ZxvStatus status, string operation, string detail)
        : base(status, operation, detail)
    {
    }
}

/// <summary>The capability lacks the needed right, is revoked (or an ancestor is), or its attestation is not TRUE (<see cref="ZxvStatus.CapabilityDenied"/>).</summary>
public sealed class CapabilityDeniedException : ZxvRefusedException
{
    /// <summary>Initializes a new instance.</summary>
    public CapabilityDeniedException()
    {
    }

    /// <summary>Initializes a new instance with a message.</summary>
    /// <param name="message">The error message.</param>
    public CapabilityDeniedException(string message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance with a message and inner exception.</summary>
    /// <param name="message">The error message.</param>
    /// <param name="innerException">The cause.</param>
    public CapabilityDeniedException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>Initializes a new instance from a native status.</summary>
    /// <param name="operation">The SDK operation that failed.</param>
    /// <param name="detail">The native last-error text.</param>
    public CapabilityDeniedException(string operation, string detail)
        : base(ZxvStatus.CapabilityDenied, operation, detail)
    {
    }
}

/// <summary>An enforced budget would be exceeded; nothing was consumed (<see cref="ZxvStatus.BudgetExhausted"/>). <see cref="Budget.ConsumeAsync(Rational, System.Threading.CancellationToken)"/> waits for replenishment instead.</summary>
public sealed class BudgetExhaustedException : ZxvRefusedException
{
    /// <summary>Initializes a new instance.</summary>
    public BudgetExhaustedException()
    {
    }

    /// <summary>Initializes a new instance with a message.</summary>
    /// <param name="message">The error message.</param>
    public BudgetExhaustedException(string message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance with a message and inner exception.</summary>
    /// <param name="message">The error message.</param>
    /// <param name="innerException">The cause.</param>
    public BudgetExhaustedException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>Initializes a new instance from a native status.</summary>
    /// <param name="operation">The SDK operation that failed.</param>
    /// <param name="detail">The native last-error text.</param>
    public BudgetExhaustedException(string operation, string detail)
        : base(ZxvStatus.BudgetExhausted, operation, detail)
    {
    }
}

/// <summary>A holder account would go below zero (<see cref="ZxvStatus.InsufficientFunds"/>).</summary>
public sealed class InsufficientFundsException : ZxvRefusedException
{
    /// <summary>Initializes a new instance.</summary>
    public InsufficientFundsException()
    {
    }

    /// <summary>Initializes a new instance with a message.</summary>
    /// <param name="message">The error message.</param>
    public InsufficientFundsException(string message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance with a message and inner exception.</summary>
    /// <param name="message">The error message.</param>
    /// <param name="innerException">The cause.</param>
    public InsufficientFundsException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>Initializes a new instance from a native status.</summary>
    /// <param name="operation">The SDK operation that failed.</param>
    /// <param name="detail">The native last-error text.</param>
    public InsufficientFundsException(string operation, string detail)
        : base(ZxvStatus.InsufficientFunds, operation, detail)
    {
    }
}

/// <summary>A signature or tag did not verify (<see cref="ZxvStatus.VerificationFailed"/>). The <c>Verify</c> methods return <see langword="false"/> instead of raising it.</summary>
public sealed class VerificationFailedException : ZxvRefusedException
{
    /// <summary>Initializes a new instance.</summary>
    public VerificationFailedException()
    {
    }

    /// <summary>Initializes a new instance with a message.</summary>
    /// <param name="message">The error message.</param>
    public VerificationFailedException(string message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance with a message and inner exception.</summary>
    /// <param name="message">The error message.</param>
    /// <param name="innerException">The cause.</param>
    public VerificationFailedException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>Initializes a new instance from a native status.</summary>
    /// <param name="operation">The SDK operation that failed.</param>
    /// <param name="detail">The native last-error text.</param>
    public VerificationFailedException(string operation, string detail)
        : base(ZxvStatus.VerificationFailed, operation, detail)
    {
    }
}

/// <summary>A SYSTEM-class failure: out of memory, a file that cannot be read, or an unexpected kernel status.</summary>
public sealed class ZxvSystemException : ZxvException
{
    /// <summary>Initializes a new instance.</summary>
    public ZxvSystemException()
    {
    }

    /// <summary>Initializes a new instance with a message.</summary>
    /// <param name="message">The error message.</param>
    public ZxvSystemException(string message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance with a message and inner exception.</summary>
    /// <param name="message">The error message.</param>
    /// <param name="innerException">The cause.</param>
    public ZxvSystemException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>Initializes a new instance from a native status.</summary>
    /// <param name="status">The native status code.</param>
    /// <param name="operation">The SDK operation that failed.</param>
    /// <param name="detail">The native last-error text.</param>
    public ZxvSystemException(ZxvStatus status, string operation, string detail)
        : base(status, operation, detail)
    {
    }
}

/// <summary>
/// The context halted (<see cref="ZxvStatus.Poisoned"/> or
/// <see cref="ZxvStatus.InvariantViolated"/> on a context call): a broken
/// invariant was detected and rolled back, or an operator called
/// <see cref="ZxvPlatform.Halt(string)"/>. The context refuses every later call;
/// never retry. Dispose the platform and start a new one from trusted state.
/// </summary>
/// <remarks>
/// <para>
/// This type deliberately does not derive from <see cref="ZxvException"/>, so
/// handlers written for ordinary refusals (<c>catch (ZxvException)</c>) cannot
/// swallow it and carry on with corrupted state.
/// </para>
/// <para>
/// .NET cannot make an exception uncatchable. For a process that must stop at
/// once, set <see cref="ZxvLibrary.FatalPolicy"/> to
/// <see cref="FatalPolicy.FailFast"/>: the SDK then calls
/// <see cref="Environment.FailFast(string)"/> instead of throwing.
/// </para>
/// </remarks>
public sealed class ZxvFatalException : Exception
{
    /// <summary>Initializes a new instance.</summary>
    public ZxvFatalException()
        : this("The ZXV context halted.")
    {
    }

    /// <summary>Initializes a new instance with a message.</summary>
    /// <param name="message">The error message.</param>
    public ZxvFatalException(string message)
        : base(message)
    {
        Status = ZxvStatus.Poisoned;
        Operation = string.Empty;
        Detail = string.Empty;
    }

    /// <summary>Initializes a new instance with a message and inner exception.</summary>
    /// <param name="message">The error message.</param>
    /// <param name="innerException">The cause.</param>
    public ZxvFatalException(string message, Exception innerException)
        : base(message, innerException)
    {
        Status = ZxvStatus.Poisoned;
        Operation = string.Empty;
        Detail = string.Empty;
    }

    /// <summary>Initializes a new instance from a native status.</summary>
    /// <param name="status">The native status code.</param>
    /// <param name="operation">The SDK operation that failed.</param>
    /// <param name="detail">The native last-error text.</param>
    public ZxvFatalException(ZxvStatus status, string operation, string detail)
        : base(ZxvException.FormatMessage(status, operation, detail))
    {
        Status = status;
        Operation = operation ?? string.Empty;
        Detail = detail ?? string.Empty;
    }

    /// <summary>Gets the native status code.</summary>
    public ZxvStatus Status { get; }

    /// <summary>Gets the symbolic status name, e.g. <c>ZXV_E_POISONED</c>.</summary>
    public string StatusName => NativeText.StatusName((int)Status);

    /// <summary>Gets the SDK operation that failed.</summary>
    public string Operation { get; }

    /// <summary>Gets the native diagnostic text (may be empty).</summary>
    public string Detail { get; }
}

/// <summary>What the SDK does when a context halts.</summary>
public enum FatalPolicy
{
    /// <summary>Throw <see cref="ZxvFatalException"/> (the default).</summary>
    Throw = 0,

    /// <summary>Terminate the process at once with <see cref="Environment.FailFast(string)"/>. The only uncatchable form.</summary>
    FailFast = 1,
}
