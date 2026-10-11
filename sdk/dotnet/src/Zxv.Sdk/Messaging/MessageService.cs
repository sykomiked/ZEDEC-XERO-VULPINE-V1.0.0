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

    private readonly ContextRef _context;
    private readonly Capability _service;
    private readonly ILogger _logger;

    /// <summary>Initializes the service.</summary>
    /// <param name="context">The platform context.</param>
    /// <param name="service">
    /// The platform's own capability (never exposed), presented by the members
    /// that take none, so every message is made with <c>zxv_msg_create_cap</c>.
    /// </param>
    /// <param name="logger">The logger.</param>
    internal MessageService(ContextRef context, Capability service, ILogger logger)
    {
        _context = context;
        _service = service;
        _logger = logger;
    }

    /// <summary>Formats a timestamp as an ISO 20022 ISODateTime in UTC (<c>2026-10-09T12:00:00Z</c>).</summary>
    /// <param name="value">The timestamp.</param>
    /// <returns>The formatted value.</returns>
    public static string FormatDateTime(DateTimeOffset value) =>
        value.UtcDateTime.ToString("yyyy-MM-dd'T'HH:mm:ss'Z'", CultureInfo.InvariantCulture);

    /// <summary>
    /// Builds a pacs.008.001.09 document through <paramref name="capability"/>
    /// (needs <see cref="CapabilityRights.LedgerRead"/>; the native message
    /// re-checks it on every field).
    /// </summary>
    /// <param name="capability">A capability of this platform.</param>
    /// <param name="message">The content.</param>
    /// <returns>The serialized document.</returns>
    /// <exception cref="CapabilityDeniedException">The capability lacks <see cref="CapabilityRights.LedgerRead"/>, is revoked or is not attested.</exception>
    /// <exception cref="ArgumentException">The capability belongs to a different platform.</exception>
    /// <exception cref="ZxvException">A field is invalid, too long, or the currency is not a wire currency.</exception>
    public MxDocument BuildPacs008(Capability capability, Pacs008Message message) => BuildPacs008Core(RequireOwn(capability), message);

    /// <summary>
    /// Builds a camt.053.001.08 document through <paramref name="capability"/>
    /// (needs <see cref="CapabilityRights.LedgerRead"/>).
    /// </summary>
    /// <param name="capability">A capability of this platform.</param>
    /// <param name="message">The content.</param>
    /// <returns>The serialized document.</returns>
    /// <exception cref="CapabilityDeniedException">The capability cannot read.</exception>
    /// <exception cref="ArgumentException">The capability belongs to a different platform.</exception>
    public MxDocument BuildCamt053(Capability capability, Camt053Message message) => BuildCamt053Core(RequireOwn(capability), message);

    /// <summary>Builds a pacs.008.001.09 document, without a capability.</summary>
    /// <param name="message">The content.</param>
    /// <returns>The serialized document.</returns>
    /// <exception cref="ZxvException">A field is invalid, too long, or the currency is not a wire currency.</exception>
    [Obsolete("Deprecated since ABI 1.2 (no capability): use BuildPacs008(Capability, Pacs008Message), which needs LedgerRead.")]
    public MxDocument BuildPacs008(Pacs008Message message) => BuildPacs008Core(null, message);

    /// <summary>Builds a camt.053.001.08 document, without a capability.</summary>
    /// <param name="message">The content.</param>
    /// <returns>The serialized document.</returns>
    [Obsolete("Deprecated since ABI 1.2 (no capability): use BuildCamt053(Capability, Camt053Message), which needs LedgerRead.")]
    public MxDocument BuildCamt053(Camt053Message message) => BuildCamt053Core(null, message);

    /// <summary>pacs.008 through <paramref name="cap"/>, or through the platform's own capability when it is null (platform services).</summary>
    internal MxDocument BuildPacs008Core(Capability? cap, Pacs008Message message)
    {
        ArgumentNullException.ThrowIfNull(message);
        ArgumentNullException.ThrowIfNull(message.Debtor);
        ArgumentNullException.ThrowIfNull(message.Creditor);
        ArgumentOutOfRangeException.ThrowIfLessThan(message.NumberOfTransactions, 1);
        return Build(cap, MxMessageKind.Pacs008, message.MessageId, msg =>
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

    /// <summary>camt.053 through <paramref name="cap"/>, or through the platform's own capability when it is null (platform services).</summary>
    /// <exception cref="ZxvUsageException">Includes <see cref="ZxvStatus.InvalidField"/> when opening plus booked entries is not the closing balance (libzxv 1.1.1; 1.1.0 reported <see cref="ZxvStatus.InvariantViolated"/>, which the SDK also maps to a usage error because the context does not halt).</exception>
    internal MxDocument BuildCamt053Core(Capability? cap, Camt053Message message)
    {
        ArgumentNullException.ThrowIfNull(message);
        ArgumentNullException.ThrowIfNull(message.Entries);
        if (message.Entries.Count > MaxStatementEntries)
        {
            throw new ArgumentException($"A camt.053 page holds at most {MaxStatementEntries} entries; page the statement.", nameof(message));
        }

        return Build(cap, MxMessageKind.Camt053, message.MessageId, msg =>
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
    /// <returns><see langword="true"/> when supported; <see langword="false"/> also when the context is halted.</returns>
    /// <exception cref="ObjectDisposedException">The platform is disposed.</exception>
    /// <remarks>Probes with <c>zxv_msg_create_cap</c> through the platform's own capability (ABI 1.2).</remarks>
    public bool IsSupported(MxMessageKind kind)
    {
        _context.ThrowIfDisposed();
        bool added = false;
        _service.Handle.DangerousAddRef(ref added);
        try
        {
            // R6: the probe message is destroyed before the reference on its cap is dropped.
            int rc = NativeMethods.MsgCreateCap(_service.Handle, (int)kind, out ZxvMessageHandle h);
            h.Dispose();
            return rc == (int)ZxvStatus.Ok;
        }
        finally
        {
            if (added)
            {
                _service.Handle.DangerousRelease();
            }
        }
    }

    private static void SetText(ZxvMessageHandle msg, int field, string value, string name)
    {
        Guard.Text(value, name);
        NativeCall.Check(NativeMethods.MsgSetText(msg, field, value), "Messages.Set(" + name + ")");
    }

    private Capability RequireOwn(Capability capability)
    {
        ArgumentNullException.ThrowIfNull(capability);
        _context.ThrowIfDisposed();
        if (!ReferenceEquals(capability.Context, _context))
        {
            throw new ArgumentException("The capability belongs to a different platform.", nameof(capability));
        }

        return capability;
    }

    private unsafe MxDocument Build(Capability? requested, MxMessageKind kind, string messageId, Action<ZxvMessageHandle> fill)
    {
        _context.ThrowIfDisposed();
        Capability cap = requested ?? _service;

        // R6: a message must not outlive its context, nor (ABI 1.2) the
        // capability it was made with. The cap already keeps its context
        // alive; hold a reference on the cap for the whole lifetime of the
        // native message.
        cap.Enter("Messages.Create(" + kind + ")");
        bool capAdded = false;
        cap.Handle.DangerousAddRef(ref capAdded);
        try
        {
            int crc = NativeMethods.MsgCreateCap(cap.Handle, (int)kind, out ZxvMessageHandle msg);
            if (crc != (int)ZxvStatus.Ok)
            {
                msg.Dispose();
                cap.Check(crc, "Messages.Create(" + kind + ")");
            }

            using (msg)
            {
                fill(msg);
                int rc = NativeText.TryRead((byte* b, nuint c, out nuint l) => NativeMethods.MsgRender(msg, b, c, out l), out string xml);
                if (rc != (int)ZxvStatus.Ok)
                {
                    // libzxv 1.1.0 reported an unreconciled statement as
                    // ZXV_E_INVARIANT without halting the context; passing the
                    // context lets NativeCall confirm that and keep it a usage error.
                    throw NativeCall.Create(rc, "Messages.Render(" + kind + ")", _context.Handle);
                }

                Log.MessageBuilt(_logger, kind.ToString(), messageId, xml.Length);
                return new MxDocument(kind, messageId, xml);
            }
        }
        finally
        {
            if (capAdded)
            {
                cap.Handle.DangerousRelease();
            }
        }
    }
}
