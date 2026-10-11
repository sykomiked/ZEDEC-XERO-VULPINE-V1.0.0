// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv;

/// <summary>Turns native status codes into the SDK's exception model, in one place.</summary>
internal static class NativeCall
{
    /// <summary>Throws the matching exception when <paramref name="rc"/> is not <see cref="ZxvStatus.Ok"/>.</summary>
    internal static void Check(int rc, string operation)
    {
        if (rc != (int)ZxvStatus.Ok)
        {
            throw Create(rc, operation);
        }
    }

    /// <summary>Throws the matching exception; a FATAL status also marks <paramref name="context"/> dead.</summary>
    internal static void Check(int rc, string operation, ContextRef context)
    {
        if (rc != (int)ZxvStatus.Ok)
        {
            throw Create(rc, operation, context.Handle, context);
        }
    }

    /// <summary>
    /// Creates (without throwing) the exception for a failed native call, chosen
    /// by <c>zxv_status_class</c>.
    /// </summary>
    /// <param name="rc">The native status.</param>
    /// <param name="operation">The SDK operation, e.g. <c>Ledger.Post</c>.</param>
    /// <param name="ctx">
    /// When given, a FATAL-class status is confirmed against
    /// <c>zxv_ctx_is_poisoned</c>: a context that did not actually halt (for
    /// example a camt.053 whose balances do not add up, which the C ABI reports
    /// as <c>ZXV_E_INVARIANT</c>) is reported as a USAGE error instead.
    /// </param>
    /// <param name="context">Marked halted on a FATAL status.</param>
    internal static Exception Create(int rc, string operation, ZxvContextHandle? ctx = null, ContextRef? context = null)
    {
        string detail = NativeText.LastError();
        var status = (ZxvStatus)rc;
        switch (status)
        {
            case ZxvStatus.NotImplemented: return new ZxvFeatureNotAvailableException(operation, detail);
            case ZxvStatus.CapabilityDenied: return new CapabilityDeniedException(operation, detail);
            case ZxvStatus.BudgetExhausted: return new BudgetExhaustedException(operation, detail);
            case ZxvStatus.InsufficientFunds: return new InsufficientFundsException(operation, detail);
            case ZxvStatus.VerificationFailed: return new VerificationFailedException(operation, detail);
            case ZxvStatus.FloatRejected: return new FloatRejectedException(operation, detail);
        }

        switch (ZxvLibrary.ClassOf(status))
        {
            case ZxvStatusClass.Usage:
                return new ZxvUsageException(status, operation, detail);
            case ZxvStatusClass.Refused:
                return new ZxvRefusedException(status, operation, detail);
            case ZxvStatusClass.Fatal:
                if (ctx is not null && !ctx.IsClosed && NativeMethods.CtxIsPoisoned(ctx, out int poisoned) == (int)ZxvStatus.Ok && poisoned == 0)
                {
                    return new ZxvUsageException(status, operation, detail);
                }

                context?.MarkHalted();
                return ZxvLibrary.Fatal(new ZxvFatalException(status, operation, detail));
            default:
                return new ZxvSystemException(status, operation, detail);
        }
    }

    /// <summary>Reads native text and throws on failure.</summary>
    internal static string ReadText(NativeTextCall call, string operation)
    {
        int rc = NativeText.TryRead(call, out string value);
        Check(rc, operation);
        return value;
    }
}

/// <summary>
/// A platform's context as the ABI 1.1 objects see it: the native handle plus
/// the SDK's own "this context is dead" mark, set on the first FATAL status so
/// later calls fail fast in managed code.
/// </summary>
internal sealed class ContextRef
{
    private volatile bool _halted;
    private volatile bool _disposed;

    internal ContextRef(ZxvContextHandle handle)
    {
        Handle = handle;
    }

    internal ZxvContextHandle Handle { get; }

    internal bool IsHalted => _halted;

    /// <summary>Gets a value indicating whether the owning <see cref="ZxvPlatform"/> was disposed.</summary>
    /// <remarks>
    /// The native context can outlive the platform while child handles
    /// (capabilities, budgets, ...) still hold a reference on it: SafeHandle
    /// ref-counting keeps it alive and destroys it after the last child (R6).
    /// This flag is what makes the platform itself observably disposed meanwhile.
    /// </remarks>
    internal bool IsDisposed => _disposed;

    internal void MarkHalted() => _halted = true;

    internal void MarkDisposed() => _disposed = true;

    /// <summary>Throws <see cref="ObjectDisposedException"/> once the owning platform is disposed.</summary>
    internal void ThrowIfDisposed() => ObjectDisposedException.ThrowIf(_disposed, typeof(ZxvPlatform));

    /// <summary>Gets the context handle for a platform-level call, or throws once the platform is disposed.</summary>
    internal ZxvContextHandle Live
    {
        get
        {
            ThrowIfDisposed();
            return Handle;
        }
    }

    /// <summary>Throws <see cref="ZxvFatalException"/> when the context is already known to be halted.</summary>
    internal void ThrowIfHalted(string operation)
    {
        if (_halted)
        {
            throw ZxvLibrary.Fatal(new ZxvFatalException(ZxvStatus.Poisoned, operation, "the context halted earlier; dispose it"));
        }
    }
}

/// <summary>Managed argument checks applied before crossing into native code.</summary>
internal static class Guard
{
    /// <summary>Rejects null, empty and strings with an embedded NUL (which C would silently truncate).</summary>
    internal static string Text(string? value, string paramName, bool allowEmpty = false)
    {
        ArgumentNullException.ThrowIfNull(value, paramName);
        if (!allowEmpty && value.Length == 0)
        {
            throw new ArgumentException("Value must not be empty.", paramName);
        }

        if (value.Contains('\0', StringComparison.Ordinal))
        {
            throw new ArgumentException("Value must not contain a NUL character.", paramName);
        }

        return value;
    }
}
