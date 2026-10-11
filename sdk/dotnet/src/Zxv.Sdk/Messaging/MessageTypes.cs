// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;

namespace Zxv.Messaging;

/// <summary>ISO 20022 message definitions the platform can serialize.</summary>
public enum MxMessageKind
{
    /// <summary>pacs.008.001.09 FI-to-FI customer credit transfer.</summary>
    Pacs008 = 1,

    /// <summary>camt.053.001.08 bank-to-customer statement.</summary>
    Camt053 = 2,

    /// <summary>pacs.002 payment status report (pending kernel/src/cbank).</summary>
    Pacs002 = 3,

    /// <summary>pacs.004 payment return (pending kernel/src/cbank).</summary>
    Pacs004 = 4,

    /// <summary>pacs.009 FI credit transfer (pending kernel/src/cbank).</summary>
    Pacs009 = 5,

    /// <summary>camt.056 cancellation request (pending kernel/src/cbank).</summary>
    Camt056 = 6,

    /// <summary>camt.029 resolution of investigation (pending kernel/src/cbank).</summary>
    Camt029 = 7,
}

/// <summary>A serialized ISO 20022 document.</summary>
/// <param name="Kind">Message definition.</param>
/// <param name="MessageId">GrpHdr/MsgId.</param>
/// <param name="Xml">The UTF-8 XML document.</param>
public sealed record MxDocument(MxMessageKind Kind, string MessageId, string Xml);

/// <summary>A party and its account, as carried in pacs.008 Dbtr/DbtrAcct or Cdtr/CdtrAcct.</summary>
/// <param name="Name">Party name (at most 70 bytes UTF-8).</param>
/// <param name="Account">Account identifier (at most 35 bytes), e.g. an IBAN or local account number.</param>
public sealed record PartyAccount(string Name, string Account);

/// <summary>Content of a pacs.008 customer credit transfer.</summary>
/// <param name="MessageId">GrpHdr/MsgId (at most 35).</param>
/// <param name="CreatedAt">GrpHdr/CreDtTm; rendered in UTC.</param>
/// <param name="Debtor">Debtor and account.</param>
/// <param name="Creditor">Creditor and account.</param>
/// <param name="Amount">Positive amount in an ISO 4217 currency; the scale must match ISO 4217.</param>
/// <param name="EndToEndId">PmtId/EndToEndId (at most 35).</param>
public sealed record Pacs008Message(
    string MessageId,
    DateTimeOffset CreatedAt,
    PartyAccount Debtor,
    PartyAccount Creditor,
    Money Amount,
    string EndToEndId)
{
    /// <summary>Gets the GrpHdr/NbOfTxs value. Defaults to 1.</summary>
    public int NumberOfTransactions { get; init; } = 1;
}

/// <summary>Status of a statement entry.</summary>
public enum EntryStatus
{
    /// <summary>Booked (BOOK); counts toward the closing balance.</summary>
    Booked = 0,

    /// <summary>Pending (PDNG).</summary>
    Pending = 1,

    /// <summary>Informational (INFO).</summary>
    Info = 2,
}

/// <summary>One camt.053 statement entry.</summary>
/// <param name="Amount">Signed amount: positive is CRDT, negative is DBIT.</param>
/// <param name="Status">Entry status.</param>
public sealed record StatementEntry(Money Amount, EntryStatus Status = EntryStatus.Booked);

/// <summary>Content of a camt.053 statement. Opening plus booked entries must equal closing.</summary>
/// <param name="MessageId">GrpHdr/MsgId.</param>
/// <param name="CreatedAt">GrpHdr/CreDtTm.</param>
/// <param name="AccountId">Stmt/Acct/Id/Othr/Id.</param>
/// <param name="Opening">OPBD balance.</param>
/// <param name="Closing">CLBD balance.</param>
/// <param name="Entries">At most 32 entries.</param>
public sealed record Camt053Message(
    string MessageId,
    DateTimeOffset CreatedAt,
    string AccountId,
    Money Opening,
    Money Closing,
    IReadOnlyList<StatementEntry> Entries);
