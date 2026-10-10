// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Zxv.Interop;

namespace Zxv;

/// <summary>
/// Raised when the ZXV platform core refuses an operation. Branch on
/// <see cref="Status"/>; <see cref="Detail"/> is diagnostic text from the native
/// library and is not a stable contract.
/// </summary>
/// <remarks>
/// The SDK uses exceptions for every refused operation and for faults. The one
/// business outcome that is not an exception is a card decline, which is
/// returned as an <see cref="Cards.AuthorizationResult"/>.
/// Argument problems detected in managed code before any native call raise the
/// standard <see cref="ArgumentException"/> family instead.
/// </remarks>
public class ZxvException : Exception
{
    /// <summary>Initializes a new instance with a generic message.</summary>
    public ZxvException()
        : this("The ZXV platform core refused the operation.")
    {
    }

    /// <summary>Initializes a new instance with a message.</summary>
    /// <param name="message">The error message.</param>
    public ZxvException(string message)
        : base(message)
    {
        Status = ZxvStatus.Internal;
        Operation = string.Empty;
        Detail = string.Empty;
    }

    /// <summary>Initializes a new instance with a message and inner exception.</summary>
    /// <param name="message">The error message.</param>
    /// <param name="innerException">The cause.</param>
    public ZxvException(string message, Exception innerException)
        : base(message, innerException)
    {
        Status = ZxvStatus.Internal;
        Operation = string.Empty;
        Detail = string.Empty;
    }

    /// <summary>Initializes a new instance from a native status.</summary>
    /// <param name="status">The native status code.</param>
    /// <param name="operation">The SDK operation that failed, e.g. <c>Ledger.Post</c>.</param>
    /// <param name="detail">The native last-error text.</param>
    public ZxvException(ZxvStatus status, string operation, string detail)
        : base(FormatMessage(status, operation, detail))
    {
        Status = status;
        Operation = operation ?? string.Empty;
        Detail = detail ?? string.Empty;
    }

    /// <summary>Gets the native status code.</summary>
    public ZxvStatus Status { get; }

    /// <summary>Gets the SDK operation that failed.</summary>
    public string Operation { get; }

    /// <summary>Gets the native diagnostic text (may be empty).</summary>
    public string Detail { get; }

    /// <summary>Gets the stable symbolic code, e.g. <c>ZXV_E_FUNDS</c>, suitable for logs and problem details.</summary>
    public string Code => NativeText.StatusName((int)Status);

    private static string FormatMessage(ZxvStatus status, string operation, string detail)
    {
        string code = NativeText.StatusName((int)status);
        return string.IsNullOrEmpty(detail) ? $"{operation} failed: {code}." : $"{operation} failed: {code}: {detail}";
    }
}

/// <summary>
/// Raised when a feature is present in the API but not built into the loaded
/// native library yet (<see cref="ZxvStatus.NotImplemented"/>). Check
/// <see cref="ZxvPlatform.Features"/> to avoid it.
/// </summary>
public sealed class ZxvFeatureNotAvailableException : ZxvException
{
    /// <summary>Initializes a new instance.</summary>
    public ZxvFeatureNotAvailableException()
    {
    }

    /// <summary>Initializes a new instance with a message.</summary>
    /// <param name="message">The error message.</param>
    public ZxvFeatureNotAvailableException(string message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance with a message and inner exception.</summary>
    /// <param name="message">The error message.</param>
    /// <param name="innerException">The cause.</param>
    public ZxvFeatureNotAvailableException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>Initializes a new instance from a native status.</summary>
    /// <param name="operation">The SDK operation that failed.</param>
    /// <param name="detail">The native last-error text.</param>
    public ZxvFeatureNotAvailableException(string operation, string detail)
        : base(ZxvStatus.NotImplemented, operation, detail)
    {
    }
}

/// <summary>
/// Raised by <see cref="Payments.PaymentService"/> when a payment was booked on
/// the ledger but the message transport failed. The ledger posting stands; the
/// message in <see cref="Receipt"/> can be retransmitted.
/// </summary>
public sealed class ZxvTransportException : ZxvException
{
    /// <summary>Initializes a new instance.</summary>
    public ZxvTransportException()
    {
    }

    /// <summary>Initializes a new instance with a message.</summary>
    /// <param name="message">The error message.</param>
    public ZxvTransportException(string message)
        : base(message)
    {
    }

    /// <summary>Initializes a new instance with a message and inner exception.</summary>
    /// <param name="message">The error message.</param>
    /// <param name="innerException">The cause.</param>
    public ZxvTransportException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>Initializes a new instance for a booked payment whose transmission failed.</summary>
    /// <param name="receipt">The receipt of the booked payment.</param>
    /// <param name="innerException">The transport failure.</param>
    public ZxvTransportException(Payments.PaymentReceipt receipt, Exception innerException)
        : base($"Payment {receipt?.EndToEndId} was booked but transmission failed: {innerException?.Message}", innerException!)
    {
        Receipt = receipt;
    }

    /// <summary>Gets the receipt of the booked payment, if any.</summary>
    public Payments.PaymentReceipt? Receipt { get; }
}
