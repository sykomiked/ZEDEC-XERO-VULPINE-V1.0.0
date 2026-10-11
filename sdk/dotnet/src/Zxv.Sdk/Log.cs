// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using Microsoft.Extensions.Logging;
using Zxv.Ledger;

namespace Zxv;

/// <summary>High-performance, source-generated log messages. Event ids are stable.</summary>
internal static partial class Log
{
    [LoggerMessage(EventId = 1000, Level = LogLevel.Information, Message = "ZXV platform started: ABI {AbiVersion}, features {Features}")]
    internal static partial void PlatformStarted(ILogger logger, string abiVersion, Interop.ZxvFeatures features);

    [LoggerMessage(EventId = 1001, Level = LogLevel.Information, Message = "Currency {Currency} enabled ({MinorUnits} minor units)")]
    internal static partial void CurrencyEnabled(ILogger logger, string currency, byte? minorUnits);

    [LoggerMessage(EventId = 1002, Level = LogLevel.Debug, Message = "ZXV platform disposed")]
    internal static partial void PlatformDisposed(ILogger logger);

    [LoggerMessage(EventId = 1100, Level = LogLevel.Debug, Message = "Account {AccountId} opened in {Currency} as {Kind}")]
    internal static partial void AccountOpened(ILogger logger, uint accountId, string currency, AccountKind kind);

    [LoggerMessage(EventId = 1101, Level = LogLevel.Debug, Message = "Posted entry {EntryId} ref {Reference}: {MinorUnits} minor units {Currency}")]
    internal static partial void Posted(ILogger logger, ulong entryId, string reference, long minorUnits, string currency);

    [LoggerMessage(EventId = 1102, Level = LogLevel.Warning, Message = "Posting {Reference} refused: {Code} {Detail}")]
    internal static partial void PostRefused(ILogger logger, string reference, string code, string detail);

    [LoggerMessage(EventId = 1200, Level = LogLevel.Debug, Message = "Built {Kind} {MessageId} ({Length} bytes)")]
    internal static partial void MessageBuilt(ILogger logger, string kind, string messageId, int length);

    [LoggerMessage(EventId = 1300, Level = LogLevel.Information, Message = "Payment {EndToEndId} booked as entry {EntryId} and handed to {Transport}")]
    internal static partial void PaymentSubmitted(ILogger logger, string endToEndId, ulong entryId, string transport);

    [LoggerMessage(EventId = 1301, Level = LogLevel.Error, Message = "Payment {EndToEndId} booked but transmission failed")]
    internal static partial void PaymentTransmissionFailed(ILogger logger, System.Exception exception, string endToEndId);
}
