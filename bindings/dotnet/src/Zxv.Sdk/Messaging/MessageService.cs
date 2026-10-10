// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Globalization;
using Microsoft.Extensions.Logging;
using Zxv.Interop;

namespace Zxv.Messaging;

/// <summary>
/// Serializes ISO 20022 messages through the kernel builders (deterministic,
/// bounded, XML-escaped). Obtain it from <see cref="ZxvPlatform.Messages"/>.
/// Thread-safe: each call uses its own native message handle.
/// </summary>
/// <remarks>
/// Currency rules enforced natively: the <c>Ccy</c> attribute is always an
/// ISO 4217 alpha code; private units (VFV) and the Vino rail numerics are
/// refused; the amount scale must equal the ISO 4217 minor unit.
/// Structural well-formedness only: validate against the published XSD (and the
/// scheme's usage guidelines) before production use.
/// </remarks>
public sealed class MessageService
{
    /// <summary>The maximum number of entries in one camt.053 page.</summary>
    public const int MaxStatementEntries = 32;

    private const int FldMsgId = 1, FldCreDtTm = 2, FldDebtorName = 3, FldDebtorAcct = 4, FldCreditorName = 5,
        FldCreditorAcct = 6, FldEndToEndId = 7, FldCcy = 8, FldAcctId = 9;

    private const int AmtSettlement = 1, AmtOpening = 2, AmtClosing = 3;

    private readonly ZxvContextHandle _ctx;
    private readonly ILogger _logger;

    internal MessageService(ZxvContextHandle ctx, ILogger logger)
    {
        _ctx = ctx;
        _logger = logger;
    }

    /// <summary>Formats a timestamp as an ISO 20022 ISODateTime in UTC (<c>2026-10-09T12:00:00Z</c>).</summary>
    /// <param name="value">The timestamp.</param>
    /// <returns>The formatted value.</returns>
    public static string FormatDateTime(DateTimeOffset value) =>
        value.UtcDateTime.ToString("yyyy-MM-dd'T'HH:mm:ss'Z'", CultureInfo.InvariantCulture);

    /// <summary>Builds a pacs.008.001.09 document.</summary>
    /// <param name="message">The content.</param>
    /// <returns>The serialized document.</returns>
    /// <exception cref="ZxvException">A field is invalid, too long, or the currency is not a wire currency.</exception>
    public MxDocument BuildPacs008(Pacs008Message message)
    {
        ArgumentNullException.ThrowIfNull(message);
        ArgumentNullException.ThrowIfNull(message.Debtor);
        ArgumentNullException.ThrowIfNull(message.Creditor);
        ArgumentOutOfRangeException.ThrowIfLessThan(message.NumberOfTransactions, 1);
        return Build(MxMessageKind.Pacs008, message.MessageId, msg =>
        {
            SetText(msg, FldMsgId, message.MessageId, nameof(message.MessageId));
            SetText(msg, FldCreDtTm, FormatDateTime(message.CreatedAt), nameof(message.CreatedAt));
            SetText(msg, FldDebtorName, message.Debtor.Name, "Debtor.Name");
            SetText(msg, FldDebtorAcct, message.Debtor.Account, "Debtor.Account");
            SetText(msg, FldCreditorName, message.Creditor.Name, "Creditor.Name");
            SetText(msg, FldCreditorAcct, message.Creditor.Account, "Creditor.Account");
            SetText(msg, FldEndToEndId, message.EndToEndId, nameof(message.EndToEndId));
            SetText(msg, FldCcy, message.Amount.Currency, "Amount.Currency");
            NativeCall.Check(NativeMethods.MsgSetAmount(msg, AmtSettlement, message.Amount.MinorUnits, message.Amount.Scale), "Messages.BuildPacs008(Amount)");
            NativeCall.Check(NativeMethods.MsgSetTxCount(msg, (uint)message.NumberOfTransactions), "Messages.BuildPacs008(NbOfTxs)");
        });
    }

    /// <summary>Builds a camt.053.001.08 document.</summary>
    /// <param name="message">The content.</param>
    /// <returns>The serialized document.</returns>
    /// <exception cref="ZxvException">Includes <see cref="ZxvStatus.InvariantViolated"/> when opening plus booked entries is not the closing balance.</exception>
    public MxDocument BuildCamt053(Camt053Message message)
    {
        ArgumentNullException.ThrowIfNull(message);
        ArgumentNullException.ThrowIfNull(message.Entries);
        if (message.Entries.Count > MaxStatementEntries)
        {
            throw new ArgumentException($"A camt.053 page holds at most {MaxStatementEntries} entries; page the statement.", nameof(message));
        }

        return Build(MxMessageKind.Camt053, message.MessageId, msg =>
        {
            SetText(msg, FldMsgId, message.MessageId, nameof(message.MessageId));
            SetText(msg, FldCreDtTm, FormatDateTime(message.CreatedAt), nameof(message.CreatedAt));
            SetText(msg, FldAcctId, message.AccountId, nameof(message.AccountId));
            SetText(msg, FldCcy, message.Opening.Currency, "Opening.Currency");
            NativeCall.Check(NativeMethods.MsgSetAmount(msg, AmtOpening, message.Opening.MinorUnits, message.Opening.Scale), "Messages.BuildCamt053(Opening)");
            NativeCall.Check(NativeMethods.MsgSetAmount(msg, AmtClosing, message.Closing.MinorUnits, message.Closing.Scale), "Messages.BuildCamt053(Closing)");
            foreach (StatementEntry e in message.Entries)
            {
                if (!string.Equals(e.Amount.Currency, message.Opening.Currency, StringComparison.Ordinal))
                {
                    throw new ArgumentException("Every statement entry must be in the account currency.", nameof(message));
                }

                if (e.Amount.IsZero)
                {
                    throw new ArgumentException("Statement entries must be non-zero.", nameof(message));
                }

                int cdtDbt = e.Amount.IsNegative ? 1 : 0;
                long magnitude = Math.Abs(e.Amount.MinorUnits);
                NativeCall.Check(NativeMethods.MsgAddEntry(msg, magnitude, e.Amount.Scale, cdtDbt, (int)e.Status), "Messages.BuildCamt053(Entry)");
            }
        });
    }

    /// <summary>Returns whether the loaded native library can build a message kind.</summary>
    /// <param name="kind">The message kind.</param>
    /// <returns><see langword="true"/> when supported.</returns>
    public bool IsSupported(MxMessageKind kind)
    {
        int rc = NativeMethods.MsgCreate(_ctx, (int)kind, out ZxvMessageHandle h);
        h.Dispose();
        return rc == (int)ZxvStatus.Ok;
    }

    private static void SetText(ZxvMessageHandle msg, int field, string value, string name)
    {
        Guard.Text(value, name);
        NativeCall.Check(NativeMethods.MsgSetText(msg, field, value), "Messages.Set(" + name + ")");
    }

    private unsafe MxDocument Build(MxMessageKind kind, string messageId, Action<ZxvMessageHandle> fill)
    {
        // R6: a message must not outlive its context. Hold a reference on the
        // context for the whole lifetime of the native message.
        bool added = false;
        _ctx.DangerousAddRef(ref added);
        try
        {
            NativeCall.Check(NativeMethods.MsgCreate(_ctx, (int)kind, out ZxvMessageHandle msg), "Messages.Create(" + kind + ")");
            using (msg)
            {
                fill(msg);
                string xml = NativeCall.ReadText((byte* b, nuint c, out nuint l) => NativeMethods.MsgRender(msg, b, c, out l), "Messages.Render(" + kind + ")");
                Log.MessageBuilt(_logger, kind.ToString(), messageId, xml.Length);
                return new MxDocument(kind, messageId, xml);
            }
        }
        finally
        {
            if (added)
            {
                _ctx.DangerousRelease();
            }
        }
    }
}
